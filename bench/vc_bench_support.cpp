// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc_bench_support.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "nanobench.h"
// third_party/, SYSTEM INTERFACE (see CMakeLists.txt) — used ONLY to READ a
// baseline for --compare; the WRITE path below is unchanged hand-assembled
// JSON (Sec 8's committed format, which --compare must parse without
// changing).
#include "nlohmann/json.hpp"

// Reproducibility metadata, injected by CMake as compile definitions (the same
// mechanism the test target uses for VC_TEST_DATA_DIR). A number is only valid
// for a fixed (bench definition + host + compiler + flags), so every baseline
// is keyed and stamped with these — never compare across them
// (docs/benchmarking.md Sec 8). Fallbacks keep this TU compilable on its own.
#ifndef VC_BENCH_HOST
#define VC_BENCH_HOST "unknown-host"
#endif
#ifndef VC_BENCH_COMPILER
#define VC_BENCH_COMPILER "unknown-compiler"
#endif
#ifndef VC_BENCH_MARCH
#define VC_BENCH_MARCH "unknown-march"
#endif
#ifndef VC_BENCH_FLAGS
#define VC_BENCH_FLAGS "unknown-flags"
#endif
#ifndef VC_BENCH_GIT_SHA
#define VC_BENCH_GIT_SHA "unknown-sha"
#endif
#ifndef VC_BENCH_BASELINE_DIR
#define VC_BENCH_BASELINE_DIR "bench/baselines"
#endif

namespace vc::bench {

namespace {

// A UTC timestamp for the baseline stamp. Runtime (not a CMake configure-time
// value) because it records when the numbers were actually MEASURED.
std::string utc_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::array<char, 32> buf{};
    // ISO 8601, e.g. 2026-07-15T09:41:00Z.
    std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string{buf.data()};
}

// A "host-compiler" filename stem, sanitised so it is filesystem-safe. Keys the
// baseline file so two machines' numbers never overwrite each other.
std::string host_compiler_key() {
    std::string key = std::string{VC_BENCH_HOST} + "-" + VC_BENCH_COMPILER;
    for (char& c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                        c == '_';
        if (!ok) {
            c = '_';
        }
    }
    return key;
}

// The provenance block prepended to a .md baseline. A reader (or a future me
// diffing git history) must be able to tell at a glance which machine and build
// produced these numbers, and that they are not cross-comparable.
std::string markdown_metadata(const std::string& suite) {
    std::ostringstream os;
    os << "# Benchmark baseline: " << suite << "\n\n"
       << "| key | value |\n"
       << "|---|---|\n"
       << "| suite | " << suite << " |\n"
       << "| host | " << VC_BENCH_HOST << " |\n"
       << "| compiler | " << VC_BENCH_COMPILER << " |\n"
       << "| -march | " << VC_BENCH_MARCH << " |\n"
       << "| flags | " << VC_BENCH_FLAGS << " |\n"
       << "| git | " << VC_BENCH_GIT_SHA << " |\n"
       << "| generated | " << utc_now() << " |\n\n"
       << "> Machine-specific — never compare across host/compiler/flags. "
          "Regenerate (and stamp the new git SHA) when the bench CODE changes, "
          "not every commit. See docs/benchmarking.md Sec 8.\n\n";
    return os.str();
}

// Minimal JSON string escaper for the metadata values. They come from CMake
// (hostname, compiler id, flags) and realistically contain no JSON-special
// characters — but injecting them raw is an unstated assumption, and a stray
// backslash or quote would silently corrupt the committed baseline (the exact
// artifact the git-diff timeseries depends on). Escapes the two characters JSON
// requires in a string; control chars are not expected from these sources.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// The metadata object that heads a .json baseline. nanobench's own json()
// template carries per-result config but NO host/CPU/compiler identity
// (verified against the vendored header), so we supply that ourselves.
std::string json_metadata(const std::string& suite) {
    std::ostringstream os;
    os << "  \"meta\": {\n"
       << "    \"suite\": \"" << json_escape(suite) << "\",\n"
       << "    \"host\": \"" << json_escape(VC_BENCH_HOST) << "\",\n"
       << "    \"compiler\": \"" << json_escape(VC_BENCH_COMPILER) << "\",\n"
       << "    \"march\": \"" << json_escape(VC_BENCH_MARCH) << "\",\n"
       << "    \"flags\": \"" << json_escape(VC_BENCH_FLAGS) << "\",\n"
       << "    \"git\": \"" << json_escape(VC_BENCH_GIT_SHA) << "\",\n"
       << "    \"generated\": \"" << json_escape(utc_now()) << "\"\n"
       << "  }";
    return os.str();
}

// Returns false on any failure so the caller can exit non-zero — a silently
// unwritten baseline would otherwise read as success to CI/scripts.
bool write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        // Names the ACTUAL path just attempted, not a hardcoded guess at one —
        // `path` is already the real target (default dir, or a --baseline-dir
        // override), so a fixed "bench/baselines/" suggestion here would name
        // the wrong directory once an override exists.
        std::cerr << "vc_bench: could not open baseline file for writing: "
                  << path
                  << " (does its parent directory exist and is it "
                     "writable?)\n";
        return false;
    }
    out << contents;
    if (!out) {
        std::cerr << "vc_bench: write failed for baseline file: " << path
                  << "\n";
        return false;
    }
    return true;
}

// Strip nanobench's one-time environment preamble (on Linux it prints
// CPU-frequency-scaling warnings to the bench output before the first table)
// from a captured table before it goes into a COMMITTED baseline — otherwise
// that machine-note pollutes the git-diff timeseries. Keeps everything from the
// first Markdown table row (a line whose first non-space char is '|') onward;
// the per-row instability marker (:wavy_dash:) lives inside the table and is
// preserved. The full table, warnings included, still goes to stdout.
std::string table_only(const std::string& captured) {
    const std::size_t bar = captured.find('|');
    if (bar == std::string::npos) {
        return captured; // no table (e.g. empty) — leave as-is
    }
    // Back up to the start of the line containing that first '|'.
    const std::size_t line_start = captured.rfind('\n', bar);
    return captured.substr(line_start == std::string::npos ? 0
                                                           : line_start + 1);
}

// ---------------------------------------------------------------------------
// --compare: read-only regression detection against a committed baseline.
//
// Nothing in bench/ used to READ a baseline (--baseline only WRITES); a
// regression could land, `--baseline` never gets run again by accident, and
// the suite stays green forever. --compare closes that gap without touching
// the write path: it reuses the SAME per-case json() render `--baseline`
// already produces (see the `results` loop in run_suite) and, for the
// baseline side, PARSES the committed .json with the vendored nlohmann
// library rather than hand-rolling a reader for a hand-assembled format.
// ---------------------------------------------------------------------------

// Default regression threshold, in percent median slowdown. Justification
// (docs/benchmarking.md Sec 4.3, and measured directly on this dev box while
// building and verifying this mode): the brief's own working figure was
// "~3% cross-run spread, ~5-6% practical floor." Measured reality on THIS
// box, across six clean (unmodified-code) runs of the pipeline suite's new
// "rung2a stage.process()" row while verifying Task 2, was WORSE than that:
// 36.73 / 36.75 / 36.79 / 37.27 / 37.91 / 39.98 ns — a +8.85% top-to-bottom
// spread with zero source changes (macOS has no core pinning, Sec 4.3). A
// 10% default would leave almost no margin above that observed noise and
// risks a spurious CI failure on an ordinary noisy run. 15% sits clearly
// above the WORST spread actually observed (not just the brief's estimate)
// while still catching anything a human would call a real regression long
// before it reaches the multi-hundred-x blowup a busy-loop or an accidental
// deep copy produces — this suite's own injected-regression acceptance test
// showed a +471,490% delta, so there is enormous headroom between "real
// regression" and "15% of noise". It is a default, not a law — --threshold
// overrides it per invocation (e.g. tighter once more cross-run data justifies
// it, or looser on a known-noisier CI runner).
constexpr double kDefaultThresholdPct = 15.0;

// Ceiling above which a row's OWN medianAbsolutePercentError is treated as a
// broken/untrustworthy measurement rather than legitimate noise (BUG 4 fix).
// Without a cap, `combined_err_abs` below (the SUM of each side's absolute
// error IN SECONDS — `median_s * err_frac` per side, not a sum of
// percentages) can suppress an arbitrarily large regression just because
// both sides happened to report a wide err% — reproduced with a baseline
// err% of 150% swallowing a +105.16% delta as "ok (within combined err%
// band)". This ceiling does not cap `combined_err_abs` itself; it gates
// each side's `err_frac` individually (`b.err_frac <= kMaxTrustworthyErrFrac
// && c.err_frac <= kMaxTrustworthyErrFrac`) — if EITHER side alone is this
// untrustworthy, the err%-band is not applied at all (the row falls through
// to the plain --threshold decision) rather than the ceiling ever appearing
// inside the combined-error arithmetic. 10% is the brief's own suggested
// figure and is deliberately an ABSOLUTE, --threshold-independent ceiling
// (not "N × --threshold"): err% measures WITHIN-RUN measurement quality, a
// property of the run itself, while --threshold measures how much slowdown
// a human cares about; tying the two together would make the err% sanity
// check tighten or loosen for reasons that have nothing to do with
// measurement quality (e.g. a CI runner that lowers --threshold for
// stricter gating would also — accidentally — make ordinary noisy-but-honest
// rows start being flagged as untrustworthy).
// Verified NOT to over-correct: the reviewer-confirmed good case (a
// baseline err% of 10%, current err% of 0.4%, combined ~10.4%) sits AT this
// ceiling (10% is `<= kMaxTrustworthyErrFrac`, so it stays trustworthy) and
// that case's +18.66% delta is still correctly flagged as a regression by
// the ordinary combined-band math below (18.66% > 10.4%) — this cap never
// enters into that decision. The one committed baseline row whose err%
// exceeds 2% (`alloc_fill :: alloc+fill+sum u8`, 2.70%) is nowhere near 10%
// either, so no committed baseline is affected (verified directly against
// the committed baseline JSON — see docs/benchmarking.md §8 for the
// measured err% distribution across 121 runs).
constexpr double kMaxTrustworthyErrFrac = 0.10;

// One (title, name) row's timing, however it was sourced (freshly rendered
// from this run, or parsed back out of a committed baseline) — the two are
// deliberately the SAME shape so one comparison function handles both.
struct bench_row {
    std::string title; // the bench_case name (nanobench's Bench::title())
    std::string name;  // the .run() row name (one rung)
    double median_s = 0.0;
    double err_frac = 0.0; // medianAbsolutePercentError(elapsed), a FRACTION
                           // (0.0035 == 0.35%), matching the JSON field as-is

    // BUG 1 fix: whether `median_s` is actually usable as a comparison point.
    // false when "median(elapsed)" was ABSENT from the source row, or present
    // but <= 0 — three shapes that used to be indistinguishable from "no
    // change" because `.value("median(elapsed)", 0.0)` silently defaults an
    // absent key to 0.0, and the delta formula's `b.median_s > 0.0` guard
    // turned that (and any negative value) into a flat "+0.00% ok" for the
    // WHOLE SUITE — a syntactically valid, well-shaped baseline could pass
    // every case without a single real comparison happening. A row with
    // `median_ok == false` gets its own "NOT COMPARABLE" status (never a
    // 0.00% delta) and forces run_compare's exit code non-zero: the baseline
    // (or, symmetrically, this run) ASSERTED a row exists and that assertion
    // is corrupt, which is different from a row being absent altogether (see
    // `median_issue` and the "new"/"missing" branches in run_compare, which
    // stay exit-0 — nothing was ever asserted about a row that just isn't
    // there).
    bool median_ok = true;
    std::string median_issue; // human-readable reason, set iff !median_ok
};

// The HUMAN-FACING identity of a row: `title` alone is not enough (one case
// can hold several rungs, e.g. "rung1"/"rung2a"); the pair is what
// run_suite's own JSON writer already treats as identifying one row (see the
// `results` array shape written by --baseline). Used ONLY for text a person
// reads — the "case" column of the delta table and diagnostic messages —
// never as a map/dedup key; see match_key() below for why.
std::string row_key(const bench_row& r) {
    return r.title + " :: " + r.name;
}

// The key a baseline row and a current-run row are actually MATCHED on
// (map keys in run_compare's `baseline_by_key`/`current_by_key`, and
// dedup_rows' collision detection). Deliberately NOT row_key(): row_key()'s
// " :: " separator is not escaped, so two structurally different (title,
// name) pairs can render to the identical string — e.g. title="X",
// name="Y :: Z" and title="X :: Y", name="Z" both produce "X :: Y :: Z".
// Within one side that is still caught (dedup_rows sees the same string key
// twice and forces a duplicate-key exit); the unhandled case was ACROSS
// sides — a baseline row and a current row colliding via different
// constructions would be silently treated as a legitimate match and diffed
// against each other with a nonsensical delta and no diagnostic at all
// (silent misattribution, not a loud rejection). A fixed-width, zero-padded
// decimal length prefix pins the exact byte offset where `title` ends and
// `name` begins, so two different (title, name) pairs can never collapse
// into the same match_key() string regardless of what bytes either one
// contains (including a literal " :: ", any control character, or anything
// else) — this is structurally impossible, not merely unlikely. Not
// reachable from any case/row name in the tree today (checked: no `.run()`
// or case name in any of the three suites contains " :: "), but nothing
// upstream enforces that as an invariant, so this closes the hole rather
// than narrowing it. row_key() is intentionally left alone for display: its
// output already appears in the printed delta table (the "case" column,
// every row of every --compare run) and in dedup_rows' diagnostic message,
// and swapping in a non-printable separator there would corrupt or obscure
// that primary human-readable output for every ordinary, non-colliding row.
std::string match_key(const bench_row& r) {
    std::ostringstream oss;
    oss << std::setw(20) << std::setfill('0') << r.title.size();
    return oss.str() + r.title + r.name;
}

// Extracts one row from a "results" array element belonging to `context` (a
// human-readable locator used only in error messages, e.g. "<path> ::
// benches[2]"). Four distinct failure shapes, handled differently:
//
//  - FINDING (2026-07-27): the element IS a JSON object but has NONE of
//    nanobench's row fields (e.g. `{"totally":"unrecognized"}`) — see the
//    dedicated comment at its check below for why the trigger is "all four
//    of title/name/median(elapsed)/medianAbsolutePercentError(elapsed)
//    absent", not just one. Reported by position, `had_error` is set, and
//    the element is skipped — on the same footing as BUG 5 below, since
//    there is nothing row-shaped to salvage from it either.
//  - BUG 5 (the element is not even a JSON object): a "results" array element
//    can never legitimately be anything but an object — nanobench's own
//    json() template renders one row object per `{{#result}}` iteration,
//    nothing else. This USED TO be a silent `return` with no report ("a
//    non-object array element is not a row") — the same class of defect as
//    parse_case_results's pre-fix guard a few lines below (see its header
//    comment): a case object whose OWN "results" array mixed non-object
//    "rows" among real ones contributed those rows as zero, invisibly,
//    exactly like the top-level `benches`-array version of the bug.
//    Reproduced directly: appending one bare string to an otherwise-valid
//    case's "results" array left the case's real rows untouched and printed
//    no diagnostic anywhere. Now reported by name (position and JSON type
//    found) and `had_error` is set, on the same footing as the two bullets
//    below — never silent.
//  - BUG 2 (a PRESENT field with the WRONG JSON type): nlohmann's `.value()`
//    throws `json::type_error` only when a key exists with an incompatible
//    type (an ABSENT key just returns the default — see the next bullet).
//    Nothing upstream of here used to catch that, so one hand-edited or
//    version-skewed field (e.g. "median(elapsed)" as a string) SIGABRT'd the
//    whole process. Caught here: once one field on a row has thrown, none of
//    the row's other fields can be trusted either (a single JSON object went
//    through some encoding path serious enough to get a field's whole TYPE
//    wrong), so the row is SKIPPED, with a clear message naming the row's
//    position and the field/error, and `had_error` is set so the caller can
//    force the run's exit code non-zero — a row silently dropped because it
//    could not be parsed must not look like an ordinary clean comparison
//    that simply had one baseline-only or current-only row.
//  - BUG 1 (an ABSENT "median(elapsed)", or one present but <= 0): the row
//    IS still usable — title/name/err% are unaffected — but is not a valid
//    comparison point. Recorded on the row itself rather than dropped (see
//    `bench_row::median_ok` above), so it still shows up in the delta table
//    with its own status instead of silently vanishing OR — the original
//    bug — silently defaulting to 0.0 and reading as "+0.00% ok".
void extract_row(const nlohmann::json& r,
                 const std::string& context,
                 std::size_t row_index,
                 std::vector<bench_row>& rows,
                 bool& had_error) {
    if (!r.is_object()) {
        std::cerr << "vc_bench: --compare: " << context << " results["
                  << row_index
                  << "] is not a JSON object (JSON type: " << r.type_name()
                  << ") — a \"results\" array element must be a row object; "
                     "skipping it, it cannot be compared.\n";
        had_error = true;
        return;
    }
    // FINDING (2026-07-27 convergence review): an element that IS a JSON
    // object but carries NONE of nanobench's row fields is just as
    // unusable as the non-object case above, but the `.value()`-based
    // extraction below would silently absorb it — every field defaults
    // (title/name -> "", "median(elapsed)" absent -> its own NOT COMPARABLE
    // status below, "medianAbsolutePercentError(elapsed)" -> 0.0), giving a
    // row keyed " :: " with no diagnostic ever printed for THIS element
    // specifically; it only surfaces later, if at all, as an ordinary
    // unexplained "new"/"missing" row. nanobench's own `json()` template
    // (third_party/nanobench/nanobench.h) renders "title", "name",
    // "median(elapsed)", and "medianAbsolutePercentError(elapsed)"
    // unconditionally for every row it emits — no `{{#...}}` guard around
    // any of the four — so a genuine nanobench row ALWAYS carries all of
    // them. Requiring absence of ALL FOUR (not just one) is deliberate: a
    // row missing only "median(elapsed)" is already handled precisely by
    // the BUG 1 branch below (title/name are still present, and it gets its
    // own NOT COMPARABLE status downstream) — that is a different, already
    // diagnosed case, not this one. This only fires when there is nothing
    // row-shaped left to read at all. Verified against all three committed
    // baselines in bench/baselines/: every results[] element there carries
    // all four fields, so this cannot flag real nanobench output.
    if (!r.contains("title") && !r.contains("name") &&
        !r.contains("median(elapsed)") &&
        !r.contains("medianAbsolutePercentError(elapsed)")) {
        std::cerr << "vc_bench: --compare: " << context << " results["
                  << row_index
                  << "] is a JSON object but has none of nanobench's row "
                     "fields (\"title\"/\"name\"/\"median(elapsed)\"/"
                     "\"medianAbsolutePercentError(elapsed)\") — not a "
                     "recognizable row; skipping it, it cannot be compared.\n";
        had_error = true;
        return;
    }
    bench_row row;
    try {
        row.title = r.value("title", std::string{});
        row.name = r.value("name", std::string{});
        if (r.contains("median(elapsed)")) {
            row.median_s = r.at("median(elapsed)").get<double>();
            if (row.median_s <= 0.0) {
                row.median_ok = false;
                // No `== 0.0` comparison (avoids an equality-on-double smell
                // even though -Wfloat-equal is not currently enabled): the
                // branch is already `<= 0.0`, so "not negative" means "zero".
                row.median_issue = row.median_s < 0.0
                                       ? "median(elapsed) is negative"
                                       : "median(elapsed) is exactly 0";
            }
        } else {
            row.median_s = 0.0;
            row.median_ok = false;
            row.median_issue = "median(elapsed) is absent from this row";
        }
        row.err_frac = r.value("medianAbsolutePercentError(elapsed)", 0.0);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "vc_bench: --compare: " << context << " results["
                  << row_index << "] has a field with an unexpected JSON type ("
                  << e.what()
                  << ") — skipping this row; it cannot be safely compared.\n";
        had_error = true;
        return;
    }
    rows.push_back(std::move(row));
}

// FINDING 3 fix: provenance visibility with teeth. `--compare` never
// enforced the "never compare across host/compiler/march/flags" invariant
// docs/benchmarking.md documents (`parse_baseline_file` only ever read
// `doc["benches"]`; `doc["meta"]` was write-only) — a hand-edited or
// stale-arch baseline compared meaningless numbers and reported clean.
// Deliberately NOT a hard error (see the run_compare header comment at the
// call site below for why: a CI runner's `meta.host`/`meta.compiler` are
// expected to differ from any fixed value on every run, so hard equality
// would make the committed CI compare job permanently red). Instead this
// struct backs a REPORT-and-WARN path: both sides' provenance are always
// printed, and a difference in the fields that actually affect
// comparability (host/compiler/march/flags) gets a named, prominent warning
// that does not touch the exit code.
struct baseline_meta {
    bool present = false;   // true iff the baseline JSON had a "meta" object
    bool malformed = false; // true iff "meta" existed but was not an object,
                            // or a field inside it was not a string — never
                            // thrown; the caller degrades to "unknown"
                            // gracefully (see the header comment above).
    std::string host, compiler, march, flags, git, generated;
};

// Extracts `doc["meta"]` gracefully: a missing "meta" key, a "meta" that is
// not an object, or an individual field of the wrong JSON type are all
// reported via `present`/`malformed` rather than thrown — `--compare` must
// never abort on a baseline whose meta block is absent or hand-mangled, only
// its "benches" array is load-bearing for the comparison itself.
baseline_meta parse_baseline_meta(const nlohmann::json& doc) {
    baseline_meta m;
    if (!doc.contains("meta")) {
        return m; // present == false: no meta block at all
    }
    const auto& meta = doc["meta"];
    if (!meta.is_object()) {
        m.malformed = true;
        return m;
    }
    m.present = true;
    const auto get_field = [&](const char* key, std::string& out) {
        if (!meta.contains(key)) {
            return; // absent field: leave blank, not itself malformed
        }
        if (!meta[key].is_string()) {
            m.malformed = true;
            return;
        }
        out = meta[key].get<std::string>();
    };
    get_field("host", m.host);
    get_field("compiler", m.compiler);
    get_field("march", m.march);
    get_field("flags", m.flags);
    get_field("git", m.git);
    get_field("generated", m.generated);
    return m;
}

// This run's OWN provenance, built from the same CMake-injected macros
// `json_metadata` above stamps into a WRITTEN baseline — so "current" in the
// --compare header is the exact same identity a `--baseline` run right now
// would stamp, not a re-derived approximation.
baseline_meta current_run_meta() {
    baseline_meta m;
    m.present = true;
    m.host = VC_BENCH_HOST;
    m.compiler = VC_BENCH_COMPILER;
    m.march = VC_BENCH_MARCH;
    m.flags = VC_BENCH_FLAGS;
    m.git = VC_BENCH_GIT_SHA;
    m.generated = utc_now();
    return m;
}

// Pulls the rows out of one case's own json() object: {"results": [ {title,
// name, "median(elapsed)", "medianAbsolutePercentError(elapsed)", ...}, ... ]}
// — the exact shape both the --baseline writer emits per case (json_parts,
// below) and a committed baseline's `benches` array holds one of.
//
// BUG 5 fix: the CLASS-level defect underlying BUG 1/BUG 2/DUPLICATE-KEY
// above, and the one that had escaped all of them — a parse path meeting an
// unexpected shape returned "nothing" (an empty `rows`) instead of "something
// is wrong". Reproduced directly: a `benches` array with 4 non-object
// elements (a bare string, number, `null`, and array) mixed among 2 valid
// case objects printed "2 compared, 6 new, 0 missing" and exited **0** — the
// 4 garbage elements were indistinguishable from an intentionally-trimmed
// baseline because NOTHING named them. The pre-fix guard here was exactly
// `if (!case_obj.is_object() || !case_obj.contains("results") ||
// !case_obj["results"].is_array()) { return rows; }` — one unconditional
// early return covering three structurally different shapes, none reported.
//
// Those three shapes are NOT all equally suspect, though, and the fix does
// not treat them as one case. nanobench's own `json()` template (see
// `third_party/nanobench/nanobench.h`) emits `"results": [` UNCONDITIONALLY
// — only the `{{#result}}...{{/result}}` section body is conditional on how
// many `.run()` calls happened. That means "an object, with a `results` key,
// whose value is an array of length zero" is a shape run_suite's own
// `--baseline` writer CAN legitimately emit (a case whose Bench completed
// zero `.run()` calls before `.render()`), so treating it as an error would
// flag a well-formed, honestly-empty case as malformed input. It is reported
// (informationally, on stdout, no `had_error`) purely so the class-level
// promise below still holds literally — "every element either contributes a
// row or says why it didn't" — but it does not fail the run; the empty-array
// case's zero rows are already accounted for at the array level (see
// `baseline_rows.empty()` and the zero-compared-rows check in run_compare).
//
// By contrast, "not an object at all", "an object with no `results` key",
// and "an object whose `results` is present but not an array" are shapes
// `--baseline`'s writer can NEVER emit — no committed baseline, and no
// current run's own json_parts, can legitimately look like this. Each is
// therefore malformed input in the same class as a row with an unusable
// median or a wrong-typed field (BUG 1 / BUG 2): reported by name, and
// `had_error` is set so the caller's exit code is forced non-zero. Per-row
// malformation *within* a well-formed `results` array is handled by
// extract_row above (also fixed under BUG 5 — see its header comment).
std::vector<bench_row> parse_case_results(const nlohmann::json& case_obj,
                                          const std::string& context,
                                          bool& had_error) {
    std::vector<bench_row> rows;
    if (!case_obj.is_object()) {
        std::cerr << "vc_bench: --compare: " << context
                  << " is not a JSON object (JSON type: "
                  << case_obj.type_name()
                  << ") — a \"benches\" array element must be a case object; "
                     "skipping it, it contributes zero rows and cannot be "
                     "compared.\n";
        had_error = true;
        return rows;
    }
    if (!case_obj.contains("results")) {
        std::cerr << "vc_bench: --compare: " << context
                  << " has no \"results\" field — every case object "
                     "nanobench's own json() render ever produces has one "
                     "(possibly an empty array); skipping it, it contributes "
                     "zero rows and cannot be compared.\n";
        had_error = true;
        return rows;
    }
    if (!case_obj["results"].is_array()) {
        std::cerr << "vc_bench: --compare: " << context
                  << " has a \"results\" field that is not an array (JSON "
                     "type: "
                  << case_obj["results"].type_name()
                  << ") — skipping it, it contributes zero rows and cannot "
                     "be compared.\n";
        had_error = true;
        return rows;
    }
    if (case_obj["results"].empty()) {
        // Legitimate, not an error (see the header comment above): reported
        // purely so this is never a SILENT zero-row contribution, but does
        // not set `had_error` and does not touch the exit code.
        std::cout << "vc_bench: --compare: " << context
                  << " is a well-formed case object with an empty "
                     "\"results\" array — contributes zero rows; this is a "
                     "valid shape (a case whose Bench completed zero .run() "
                     "calls), not an error.\n";
    }
    std::size_t row_index = 0;
    for (const auto& r : case_obj["results"]) {
        extract_row(r, context, row_index, rows, had_error);
        ++row_index;
    }
    return rows;
}

// Loads a committed baseline .json and flattens every case's rows into one
// list. `ok` is false (with `error` set) on anything that keeps this from
// being a usable baseline: unreadable file, unparsable JSON, or a missing/
// non-array "benches". An EMPTY "benches" array (parses fine, zero cases) is
// NOT an error here — see the call site for why that is handled as a
// distinct, non-fatal case. `had_row_error` (BUG 2, broadened by BUG 5) is
// set to true if any individual row inside an otherwise-valid "benches"
// array could not be parsed (a present field with the wrong JSON type,
// BUG 2), or if any `benches`/`results` array element did not even have a
// recognizable case/row shape and so could not contribute a row at all
// (BUG 5 — see parse_case_results and extract_row below) — that is
// orthogonal to `ok`: the FILE is still usable, but the caller must not
// report a clean comparison when at least one element was silently
// contributing zero rows. `meta_out`
// (FINDING 3) is filled from `doc["meta"]` via `parse_baseline_meta` on a
// best-effort basis — never a reason to fail this function; a baseline with
// no/malformed meta is still a perfectly usable baseline for the row
// comparison itself, just one `--compare` cannot verify the provenance of.
std::vector<bench_row> parse_baseline_file(const std::string& path,
                                           bool& ok,
                                           std::string& error,
                                           bool& had_row_error,
                                           baseline_meta& meta_out) {
    ok = false;
    had_row_error = false;
    meta_out = baseline_meta{};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not open file";
        return {};
    }
    std::ostringstream contents;
    contents << in.rdbuf();

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(contents.str());
    } catch (const nlohmann::json::parse_error& e) {
        error = std::string{"malformed JSON: "} + e.what();
        return {};
    }

    if (!doc.is_object() || !doc.contains("benches") ||
        !doc["benches"].is_array()) {
        error = "missing or non-array top-level \"benches\" field — not a "
                "vc_bench baseline file";
        return {};
    }

    meta_out = parse_baseline_meta(doc);

    std::vector<bench_row> rows;
    std::size_t case_index = 0;
    for (const auto& case_obj : doc["benches"]) {
        const std::string context =
            path + " :: benches[" + std::to_string(case_index) + "]";
        auto case_rows = parse_case_results(case_obj, context, had_row_error);
        rows.insert(rows.end(), case_rows.begin(), case_rows.end());
        ++case_index;
    }
    ok = true;
    return rows;
}

// Renders ns for display the same way nanobench's own Markdown table does
// (seconds are unreadably small for these cases), without pulling in
// nanobench's private formatting helpers.
double to_ns(double seconds) {
    return seconds * 1.0e9;
}

// DUPLICATE-KEY fix: builds `by_key`/`order` from `rows`, first occurrence
// of each `match_key()` wins, and reports (never silently) any key that
// appeared more than once via `had_duplicate_key`. This is NOT a "pick a
// winner" convenience — a duplicate key means the source data (a committed
// baseline file, or, symmetrically, this run's own case results) asserts
// two or more different values for the SAME row (an identical `(title,
// name)` pair — see match_key() above; NOT the display string row_key()
// produces, which two different pairs can collide on), and
// there is no principled way to know which one is authoritative. Silently
// keeping the first (the pre-fix behavior, an unconditional
// `std::unordered_map::emplace`, whose documented behavior is a no-op when
// the key already exists) is precisely the wrong answer: every duplicate
// beyond the first vanished into no bucket at all — not `missing`, not
// `NOT COMPARABLE`, not `had_row_error` — while `order` (an unconditional
// `push_back`, never deduplicated) still iterated the SAME surviving entry
// once per duplicate, so the delta table printed and counted one row
// multiple times and a wildly-off duplicate's asserted value never
// appeared anywhere. Confirmed unreachable from any COMMITTED baseline
// today (checked all three files in bench/baselines/ — zero duplicate keys
// in any of them) and not produced by any suite definition in the tree
// today (each `bench_case` in a suite's `cases()` normally has a distinct
// name, and a case's own `.run()` calls normally use distinct row names)
// — but nothing in this file's parsing enforces either of those as
// an invariant, so a hand-edited baseline, a copy-paste bug in a suite's
// case list, or a version-skewed regenerate-and-diff step could still
// produce one. A baseline (or run) that cannot say which of two asserted
// values is real is malformed input, in the same class as a row with a
// missing/zero/negative median or a field of the wrong JSON type — so this
// forces the run's exit code non-zero exactly like those, via
// `had_duplicate_key` at the call site.
void dedup_rows(const std::vector<bench_row>& rows,
                const std::string& side_label,
                std::unordered_map<std::string, bench_row>& by_key,
                std::vector<std::string>& order,
                bool& had_duplicate_key) {
    std::unordered_map<std::string, std::size_t> counts;
    for (const auto& r : rows) {
        ++counts[match_key(r)];
    }
    for (const auto& r : rows) {
        const auto key = match_key(r);
        if (by_key.find(key) != by_key.end()) {
            continue; // not the first occurrence of this key: already
                      // recorded in by_key/order, and if it's a duplicate,
                      // already reported below on the first occurrence.
        }
        by_key.emplace(key, r);
        order.push_back(key);
        const auto count = counts[key];
        if (count > 1) {
            std::cerr << "vc_bench: --compare: " << side_label << " contains "
                      << count
                      << " rows whose (title, name) pair is identical "
                         "(shown as \""
                      << row_key(r)
                      << "\") — a baseline or run cannot "
                         "assert "
                      << count
                      << " different values for one row; keeping only the "
                         "first and discarding the rest silently is exactly "
                         "the wrong answer (see dedup_rows in "
                         "vc_bench_support.cpp). Treating this as malformed "
                         "input; the comparison's exit code will be forced "
                         "non-zero.\n";
            had_duplicate_key = true;
        }
    }
}

// Runs the comparison and prints the delta table. `current_json` is this
// run's own baseline-eligible cases, already rendered to the SAME json()
// shape a committed baseline uses (built by run_suite alongside the existing
// --baseline path, just not written to disk). `whole_suite` is
// `filters.empty()` at the call site — true iff this invocation ran every
// case (no positional [FILTER ...]) rather than an interactively-narrowed
// subset; see the CHANGE 3 (zero-compared-rows) paragraph below for why the
// distinction matters. Returns the process exit code, split two ways so a
// caller can tell "the comparison ran cleanly but found a real regression"
// (advisory-worthy) apart from "the comparison could not actually be trusted"
// (never worth softening):
//   0  every compared row was actually comparable and none regressed past
//      `threshold_pct` outside its err% band. The summary line names the
//      exact compared/new/missing counts (FINDING 1) rather than the
//      previous, unconditional "every comparable row was actually compared"
//      claim, which stayed literally true only by accident once most of a
//      suite's rows landed in "new"/"missing" instead of a real comparison.
//   1  an INFRASTRUCTURE failure: something kept part of the comparison from
//      happening at all — a missing/malformed baseline file, a row present
//      on both sides that could not actually be compared (BUG 1: a missing/
//      zero/negative median; BUG 2: a field with the wrong JSON type),
//      a `benches` array element (BUG 5, see parse_case_results below) or a
//      `results` array element (BUG 5, see extract_row below) that could not
//      even be recognized as a case/row shape and so could not contribute
//      any rows, (CHANGE 3, whole-suite invocations only) zero rows anywhere
//      reaching a real delta comparison, or (FINDING 2, whole-suite
//      invocations only) at least one row present in the baseline but
//      MISSING from this run — lost coverage (a renamed/removed case), not a
//      comparison gap that's safe to ignore. A CURRENT-only row ("new")
//      never sets this at any filter level — nothing was ever asserted about
//      a case that simply didn't exist yet; that asymmetry (new = benign,
//      missing = coverage loss) is deliberate. For a FILTERED invocation, a
//      baseline-only row also stays purely informational — see
//      `whole_suite` below. Also infrastructure (DUPLICATE-KEY fix): two or
//      more rows sharing the same `match_key()` on EITHER side — see
//      `dedup_rows` below for why a duplicate key is malformed input, not
//      something that can be silently resolved by keeping the first.
//   2  a pure REGRESSION: the comparison itself ran cleanly (no reason 1
//      condition fired) but at least one row regressed past `threshold_pct`
//      outside its err% band.
// If both an infrastructure failure and a regression are present in the same
// run, 1 wins the RETURN VALUE — an infrastructure failure is the more
// serious claim ("this run's verdict cannot be trusted at all") and must
// never be silently downgraded to the softer, advisory-shaped 2 just because
// a real regression also happened to be visible in the rows that DID
// compare. Both FAILED messages still print either way — only the exit code
// coalesces to 1, never the diagnostic output. See docs/benchmarking.md and
// bench/baselines/README.md.
int run_compare(const std::string& suite,
                const std::string& baseline_path,
                double threshold_pct,
                const std::vector<std::string>& current_json,
                bool whole_suite) {
    std::cout << "\nvc_bench: --compare " << suite << " against "
              << baseline_path << " (threshold " << threshold_pct << "%)\n";

    // Missing file: treated as an ERROR, not a skip. --compare's path is
    // always given explicitly by the caller (there is no "look for a default
    // baseline" mode) — a wrong or stale path is a configuration mistake, and
    // a CI gate that silently exits 0 because someone renamed/deleted the
    // baseline file is a worse failure mode than a loud, obvious error. (This
    // is a DIFFERENT question from "the baseline exists but has zero cases in
    // it", handled below — that one degrades gracefully because a real file
    // was actually found and read.)
    bool ok = false;
    std::string parse_error;
    bool had_row_error = false; // BUG 2: set if any row anywhere (baseline OR
                                // current) had a field of the wrong JSON type
                                // and was skipped; forces a non-zero exit.
    baseline_meta baseline_provenance;
    const auto baseline_rows = parse_baseline_file(
        baseline_path, ok, parse_error, had_row_error, baseline_provenance);
    if (!ok) {
        std::cerr << "vc_bench: --compare could not use baseline file \""
                  << baseline_path << "\": " << parse_error
                  << "\nvc_bench: treating a missing/unreadable/malformed "
                     "--compare target as a hard error (not a silent skip) — "
                     "see vc_bench_support.cpp for why.\n";
        return 1;
    }

    // FINDING 3: provenance is reported and warned on, never enforced (see
    // the header comment on `baseline_meta` above for why hard equality is
    // rejected — a CI runner's host/compiler churn every run). Print both
    // sides unconditionally so a mismatch is impossible to miss in a log,
    // then, ONLY for the fields that actually affect comparability
    // (host/compiler/march/flags — deliberately not `git`/`generated`, which
    // are EXPECTED to differ on every ordinary "compare current work against
    // an older baseline" run and would make this warning fire constantly for
    // no reason), name exactly which ones differ. Never touches the exit
    // code — comparability across host/compiler/flags stays the operator's
    // responsibility, this only makes it visible.
    const auto current_provenance = current_run_meta();
    std::cout << "vc_bench: --compare provenance:\n";
    if (baseline_provenance.malformed) {
        std::cout << "  baseline: \"meta\" block present but malformed "
                     "(a field had an unexpected JSON type) — provenance "
                     "unknown for this baseline; --compare does not abort "
                     "on this, but treat the comparison with extra caution.\n";
    } else if (!baseline_provenance.present) {
        std::cout << "  baseline: no \"meta\" block in this file — "
                     "provenance unknown for this baseline; --compare does "
                     "not abort on this, but treat the comparison with "
                     "extra caution.\n";
    } else {
        std::cout << "  baseline: host=" << baseline_provenance.host
                  << " compiler=" << baseline_provenance.compiler
                  << " march=" << baseline_provenance.march
                  << " flags=" << baseline_provenance.flags
                  << " git=" << baseline_provenance.git
                  << " generated=" << baseline_provenance.generated << "\n";
    }
    std::cout << "  current:  host=" << current_provenance.host
              << " compiler=" << current_provenance.compiler
              << " march=" << current_provenance.march
              << " flags=" << current_provenance.flags
              << " git=" << current_provenance.git
              << " generated=" << current_provenance.generated << "\n";
    if (baseline_provenance.present && !baseline_provenance.malformed) {
        std::vector<std::string> differing_fields;
        if (baseline_provenance.host != current_provenance.host) {
            differing_fields.push_back("host");
        }
        if (baseline_provenance.compiler != current_provenance.compiler) {
            differing_fields.push_back("compiler");
        }
        if (baseline_provenance.march != current_provenance.march) {
            differing_fields.push_back("march");
        }
        if (baseline_provenance.flags != current_provenance.flags) {
            differing_fields.push_back("flags");
        }
        if (!differing_fields.empty()) {
            std::cout << "vc_bench: WARNING — baseline provenance differs "
                         "from this run's in: ";
            for (std::size_t i = 0; i < differing_fields.size(); ++i) {
                std::cout << differing_fields[i]
                          << (i + 1 < differing_fields.size() ? ", " : "");
            }
            std::cout << ". Numbers are machine/toolchain-specific — "
                         "comparing across host/compiler/march/flags is not "
                         "meaningful (docs/benchmarking.md Sec 8). This is a "
                         "WARNING only and does not change the exit code — "
                         "verify the baseline is still the right one to "
                         "compare against.\n";
        }
    }

    std::unordered_map<std::string, bench_row> baseline_by_key;
    std::vector<std::string> baseline_order;
    bool had_duplicate_key = false; // DUPLICATE-KEY fix: set if either side's
                                    // rows assert an identical (title, name)
                                    // pair more than once (see dedup_rows
                                    // above); forces a non-zero exit
                                    // alongside had_row_error.
    dedup_rows(baseline_rows, "baseline \"" + baseline_path + "\"",
               baseline_by_key, baseline_order, had_duplicate_key);

    // Fires whenever zero ROWS were extracted from the baseline file — not
    // only when the JSON "benches" array is literally `[]`. A `benches` array
    // whose every element is a well-formed case object with a legitimately
    // empty "results" array also lands here (BUG 5: see parse_case_results'
    // header comment for why THAT shape alone is not itself an error) — not
    // an error by itself: a real, parseable file was found, it just has
    // nothing usable in it, and every case in this run will print as "new"
    // below. A `benches` array with elements that are non-objects, or
    // otherwise not a shape `--baseline` could ever have written, is a
    // DIFFERENT situation as of BUG 5: each such element is now individually
    // reported (by parse_case_results, above, via `had_error`) at the point
    // it was parsed, rather than only being visible here as an unexplained
    // drop in the total row count — this block can still fire alongside
    // those per-element reports (e.g. a `benches` array that is ALL garbage),
    // but no longer needs to be the only place a garbage element's presence
    // is discoverable, which was the actual hole (a `benches` array MIXING
    // garbage elements among valid ones never made `baseline_rows` empty at
    // all, so this block never fired and the garbage was invisible).
    if (baseline_rows.empty()) {
        std::cout << "vc_bench: baseline \"" << baseline_path
                  << "\" parsed but zero comparable rows were extracted from "
                     "it — nothing to compare against. Every case in this "
                     "run will print as \"new\" below; this by itself is "
                     "reported, not silently swallowed.";
        if (whole_suite) {
            // CHANGE 3: unlike the pre-fix behavior, this is NOT harmless for
            // a whole-suite (unfiltered) invocation — with nothing in the
            // baseline, zero rows below can reach a real comparison, and the
            // zero-compared-rows check further down turns that into a
            // non-zero exit rather than a silently clean run.
            std::cout << " Because this is a whole-suite invocation (no "
                         "positional filter), that means nothing will reach "
                         "a real comparison below, which now forces a "
                         "non-zero exit (see the zero-compared-rows check).";
        } else {
            std::cout << " Because this run was narrowed with a positional "
                         "filter, that does not force a regression-style "
                         "failure by itself (there is nothing in the "
                         "baseline to regress against, and inspecting a "
                         "new, not-yet-baselined case this way is a "
                         "designed workflow).";
        }
        std::cout << (had_row_error
                          ? " NOTE: it does NOT mean this run is clean — see "
                            "the row-parse error(s) already printed above; "
                            "those still force a non-zero exit below.\n"
                          : "\n");
    }

    std::vector<bench_row> current_rows;
    for (std::size_t i = 0; i < current_json.size(); ++i) {
        nlohmann::json case_obj;
        try {
            case_obj = nlohmann::json::parse(current_json[i]);
        } catch (const nlohmann::json::parse_error& e) {
            // This run produced `current_json[i]` itself (via nanobench's own
            // json() render), so this is not expected to fail — but per the
            // "nothing in this tool should ever abort/silently pass" rule,
            // an unexpected failure here is reported and forces the exit
            // code non-zero rather than silently dropping the case (which
            // used to make it read as "missing (in baseline, not this run)",
            // a status that never fails the run).
            std::cerr << "vc_bench: --compare: this run's own case[" << i
                      << "] JSON render could not be re-parsed (" << e.what()
                      << ") — skipping it; it cannot be compared.\n";
            had_row_error = true;
            continue;
        }
        const std::string context =
            "this run's own case[" + std::to_string(i) + "]";
        auto rows = parse_case_results(case_obj, context, had_row_error);
        current_rows.insert(current_rows.end(), rows.begin(), rows.end());
    }

    // DUPLICATE-KEY fix, applied SYMMETRICALLY to this run's own rows (see
    // dedup_rows above and its call on baseline_rows). Not reachable from
    // any case name in the committed suites today (each bench_case's own
    // name, and each `.run()` row name inside it, is distinct by
    // convention), but nothing here enforces that as an invariant, and the
    // baseline side already needs this exact handling for the identical
    // shape — see bench_row's header comment on why both sides are always
    // validated symmetrically rather than trusting one because "it's our
    // own output".
    std::unordered_map<std::string, bench_row> current_by_key;
    std::vector<std::string> current_order;
    dedup_rows(current_rows, "this run's own case results", current_by_key,
               current_order, had_duplicate_key);

    // Stable order: baseline order first (so a long-lived case keeps its row
    // position run to run), then any current-only keys (a newly added rung —
    // e.g. this very patch's pipeline "rung2a" — appended at the end).
    std::vector<std::string> ordered_keys = baseline_order;
    for (const auto& key : current_order) {
        if (baseline_by_key.find(key) == baseline_by_key.end()) {
            ordered_keys.push_back(key);
        }
    }

    // Every numeric field below is printed with a LITERAL leading space
    // ahead of its std::setw() — not just setw alone. setw only sets a
    // MINIMUM width, so a column that overflows it (a busy-loop regression
    // prints a six-figure "current(ns)" and a five-figure "delta%") would
    // otherwise butt straight up against its neighbor with no gap at all.
    // The explicit space guarantees a separator regardless of how wide the
    // number gets.
    // "ns/iter", not "ns/pixel": these are median(elapsed) verbatim (the raw
    // per-ITERATION time nanobench itself measures), independent of any
    // case's own .unit()/.batch(). A pixel-batched case's own printed table
    // above (e.g. "ns/pixel") divides this same number by its batch size —
    // do not compare the two columns to each other, they are different units
    // by design.
    std::cout << std::left << std::setw(46) << "case" << std::right << " "
              << std::setw(13) << "baseline(ns/iter)"
              << " " << std::setw(13) << "current(ns/iter)"
              << " " << std::setw(9) << "delta%"
              << " " << std::setw(8) << "b.err%"
              << " " << std::setw(8) << "c.err%"
              << "  status\n";

    bool any_regression = false;
    bool any_not_comparable = false; // BUG 1: a row present on both sides but
                                     // whose median could not be trusted.
    // CHANGE 3 (zero-compared-rows fix): counts rows that reached the REAL
    // delta comparison below (past the median_ok guard) — incremented for
    // ok/REGRESSION/IMPROVEMENT alike, never for "new"/"missing"/"NOT
    // COMPARABLE". A whole-suite run that ends with this still at 0 exited 0
    // before this fix despite having certified nothing: every row was either
    // one-sided (new/missing, silently informational) or NOT COMPARABLE
    // (which already forces non-zero via any_not_comparable — but the empty
    // intersection can equally happen with zero NOT COMPARABLE rows too, e.g.
    // a baseline that parses to zero rows, or total key drift from a rename
    // — see the zero-compared check after this loop).
    std::size_t compared_rows = 0;
    // FINDING 1/2: counted alongside `compared_rows` so every summary line
    // below can report real numbers instead of the vague, previously-FALSE
    // "every comparable row was actually compared" claim — a rename-heavy
    // refactor could drop most of a suite's rows into "new"/"missing" and
    // still print that sentence unchanged. `new_rows` (current-only) stays
    // purely informational (see FINDING 2 below for why the asymmetry is
    // deliberate); `missing_rows` (baseline-only) is ALSO used, further
    // down, to fail a whole-suite run — a missing row means baseline
    // COVERAGE was lost (a renamed/removed case silently leaving the gate),
    // which is a materially different claim than "a new case was added".
    std::size_t new_rows = 0;
    std::size_t missing_rows = 0;
    for (const auto& key : ordered_keys) {
        const auto bit = baseline_by_key.find(key);
        const auto cit = current_by_key.find(key);

        // `key` here is match_key()'s internal, non-human-readable encoding
        // (see match_key()'s header comment) — the printed "case" column
        // uses row_key() instead, reconstructed from whichever side actually
        // has the row (at least one always does: `ordered_keys` is
        // `baseline_order` plus only those `current_order` keys not already
        // in `baseline_by_key`, and by_key/order are always populated
        // together in dedup_rows, so a key present in `ordered_keys` is
        // guaranteed to resolve on at least one side here).
        const std::string display = bit != baseline_by_key.end()
                                        ? row_key(bit->second)
                                        : row_key(cit->second);
        std::cout << std::left << std::setw(46)
                  << (display.size() > 46 ? display.substr(0, 45) + "…"
                                          : display)
                  << std::right;

        if (bit == baseline_by_key.end()) {
            // In this run, not in the baseline: expected whenever a case or
            // rung is newly added (Task 2's pipeline rung2a is exactly this
            // the first time --compare runs against the un-regenerated
            // baseline) — informational, never fails the run. This is
            // deliberately NOT the same as "NOT COMPARABLE" below: nothing
            // was ever asserted about a row that simply is not present on
            // one side, whereas a row present on BOTH sides with a corrupt
            // median means a comparison was expected and did not happen.
            std::cout << " " << std::setw(13) << "-" << " " << std::setw(13)
                      << "-"
                      << " " << std::setw(9) << "-" << " " << std::setw(8)
                      << "-"
                      << " " << std::setw(8) << "-"
                      << "  new (not in baseline)\n";
            ++new_rows;
            continue;
        }
        if (cit == current_by_key.end()) {
            // In the baseline, not in this run: expected whenever this run
            // used a narrower --list filter than the baseline covers, or a
            // case was removed/renamed. Informational, never fails the run —
            // it is not evidence of a regression, only of an incomplete
            // comparison.
            std::cout << " " << std::setw(13) << "-" << " " << std::setw(13)
                      << "-"
                      << " " << std::setw(9) << "-" << " " << std::setw(8)
                      << "-"
                      << " " << std::setw(8) << "-"
                      << "  missing (in baseline, not this run)\n";
            ++missing_rows;
            continue;
        }

        const auto& b = bit->second;
        const auto& c = cit->second;

        // BUG 1 fix: a row present on BOTH sides but whose median is not a
        // usable comparison point (baseline OR current — see bench_row's
        // comment for why both sides are checked symmetrically) gets its own
        // status and is NEVER allowed to fall through to the delta_pct math
        // below, which used to divide only when `b.median_s > 0.0` and
        // silently substitute 0.0 (== "no change") otherwise — turning a
        // missing/zero/negative baseline median into a flat "+0.00% ok" for
        // every such row.
        if (!b.median_ok || !c.median_ok) {
            std::string reason;
            if (!b.median_ok) {
                reason += "baseline: " + b.median_issue;
            }
            if (!c.median_ok) {
                reason += (reason.empty() ? "" : "; ") +
                          std::string{"current: "} + c.median_issue;
            }
            std::cout << std::fixed << std::setprecision(2) << " "
                      << std::setw(13) << to_ns(b.median_s) << " "
                      << std::setw(13) << to_ns(c.median_s) << " "
                      << std::setw(9) << "-"
                      << " " << std::setw(7) << (b.err_frac * 100.0) << "%"
                      << " " << std::setw(7) << (c.err_frac * 100.0) << "%"
                      << "  NOT COMPARABLE (" << reason << ")\n";
            any_not_comparable = true;
            continue;
        }

        // Reached the real comparison — count it regardless of what status
        // it ends up with below (ok/REGRESSION/IMPROVEMENT all count; only
        // an early `continue` above — new/missing/NOT COMPARABLE — skips
        // this).
        ++compared_rows;

        const double delta_pct = (c.median_s - b.median_s) / b.median_s * 100.0;
        // err%-aware gating (the brief's core ask). IMPORTANT, and stated
        // honestly rather than oversold: nanobench's medianAbsolutePercentError
        // is a WITHIN-RUN statistic (how much the epochs of THIS ONE run
        // disagreed with each other) — it structurally cannot see CROSS-RUN
        // drift, and docs/benchmarking.md Sec 9 records a concrete case where
        // that mattered (the pipeline case showed low within-run err% while
        // its across-run median still drifted, malloc-driven — an earlier
        // "~15%" figure for that drift did not reproduce; Sec 9's corrected
        // note gives roughly 7-17% at the default epoch, converging to ~3%
        // once minEpochTime is raised, per-run). So this band is NOT the
        // guard against the ~8.85% cross-run noise this file's
        // kDefaultThresholdPct comment measured — --threshold is. What this
        // band IS good for: suppressing a case that nominally clears
        // --threshold only because ITS OWN measurement (this run or the
        // baseline) was visibly untrustworthy (a high within-run err%) —
        // a marginal threshold-crossing that the data itself says not to
        // trust, not a substitute for a wide-enough --threshold. Summing the
        // two absolute error bands (baseline + current) is a conservative,
        // no-independence-assumed combination.
        const double baseline_err_abs = b.median_s * b.err_frac;
        const double current_err_abs = c.median_s * c.err_frac;
        const double combined_err_abs = baseline_err_abs + current_err_abs;
        const double delta_abs = std::fabs(c.median_s - b.median_s);

        // BUG 4 fix: an UNCAPPED combined err% band can suppress an
        // arbitrarily large regression if both sides happen to report a wide
        // err% (reproduced: a baseline err% of 150% suppressed a +105.16%
        // delta as "ok"). Once either side's own err% is implausibly high
        // (see kMaxTrustworthyErrFrac's comment for why 10% and why it is
        // NOT tied to --threshold), that measurement is not trustworthy
        // enough to LICENSE suppression — so the band is simply not applied
        // (`exceeds_err_band` forced true) and the row falls through to the
        // ordinary threshold-based REGRESSION/IMPROVEMENT decision instead.
        // This does not touch the case the band exists for: a "normal" high
        // err% (e.g. the reviewer-confirmed 10%/0.4% band correctly flagging
        // an 18.66% delta) never crosses this ceiling and reaches the exact
        // same combined-band math as before.
        const bool err_band_trustworthy =
            b.err_frac <= kMaxTrustworthyErrFrac &&
            c.err_frac <= kMaxTrustworthyErrFrac;
        const bool exceeds_err_band =
            !err_band_trustworthy || delta_abs > combined_err_abs;
        const bool exceeds_threshold = std::fabs(delta_pct) >= threshold_pct;

        std::string status = "ok";
        if (exceeds_threshold && exceeds_err_band) {
            const std::string untrustworthy_note =
                err_band_trustworthy
                    ? ""
                    : " [err% measurement untrustworthy > " +
                          std::to_string(static_cast<int>(
                              kMaxTrustworthyErrFrac * 100.0)) +
                          "% — band not applied]";
            if (delta_pct > 0.0) {
                status = "REGRESSION" + untrustworthy_note;
                any_regression = true;
            } else {
                // Flagged informationally (Sec-4.5-flavored "a fast lie" risk
                // in reverse): a large speedup usually means the benchmark
                // stopped measuring what it used to (dead-code-eliminated,
                // or the workload silently changed) rather than the code
                // actually getting that much faster. Does NOT fail the run —
                // there is no reliable automatic way to tell a genuine
                // optimization from a broken measurement from here, so this
                // is a prompt for a human to look, not a gate.
                status = "IMPROVEMENT (verify — large speedups often mean "
                         "the bench stopped measuring the real work)" +
                         untrustworthy_note;
            }
        } else if (exceeds_threshold && !exceeds_err_band) {
            status = "ok (within combined err% band)";
        }

        std::cout << std::fixed << std::setprecision(2) << " " << std::setw(13)
                  << to_ns(b.median_s) << " " << std::setw(13)
                  << to_ns(c.median_s) << " " << std::showpos << std::setw(8)
                  << delta_pct << "%" << std::noshowpos << " " << std::setw(7)
                  << (b.err_frac * 100.0) << "%"
                  << " " << std::setw(7) << (c.err_frac * 100.0) << "%"
                  << "  " << status << "\n";
    }

    // CHANGE 3: zero rows anywhere reaching a real comparison, in a
    // whole-suite (unfiltered) invocation, is itself an infrastructure
    // failure — every enumerated way this can happen (a baseline that parses
    // to zero rows; total key drift from a rename; every matched case
    // failing baseline_eligible, so json_parts ends up empty) means
    // "--compare could not certify anything" even though nothing
    // individually threw or crashed. Gated on `whole_suite`: a --compare
    // invocation NARROWED with a positional filter to inspect one brand-new,
    // not-yet-baselined case is a designed interactive workflow (see the
    // "new (not in baseline)" branch above, and bench/baselines/README.md's
    // "Regression check" section) — that filtered run legitimately compares
    // zero rows every time and must stay exit 0. [CORRECTED 2026-07-27:
    // `.github/workflows/benchmark.yml`'s `benchmark` job now invokes all
    // three of these binaries with `--compare` and no positional filter on
    // every push/pull_request, so CI IS a whole-suite caller today, not the
    // hypothetical this comment originally described. (Only the first step,
    // `harness`, actually reaches a comparison right now — no CI baseline
    // exists yet at `bench/baselines/ci/`, so it fails closed with "could
    // not open file" and Actions' step-failure-stops-job behavior means the
    // job never reaches the `pipe`/`pipeline` steps; see
    // `docs/benchmarking.md` Sec 8 and `bench/baselines/README.md` for the
    // bootstrap procedure that fixes this. That doesn't change the reasoning
    // below: CI never passes a filter, so gating on `whole_suite` closes
    // exactly the hole CI is exposed to.)] Gating on `whole_suite` closes
    // the CI-relevant hole without breaking the interactive one.
    const bool zero_compared = whole_suite && compared_rows == 0;

    // FINDING 1: every summary line below — OK or FAILED — reports these
    // three counts, so the OK sentence in particular is now literally true
    // of what happened rather than the previous unconditional "every
    // comparable row was actually compared" claim (reproduced: renaming 7 of
    // 8 baseline rows still printed that exact sentence with only 1 row
    // actually compared).
    const std::string row_counts =
        std::to_string(compared_rows) + " compared, " +
        std::to_string(new_rows) + " new (current-only), " +
        std::to_string(missing_rows) + " missing (baseline-only)";

    if (zero_compared) {
        std::cout << "\nvc_bench: --compare FAILED — zero rows reached an "
                     "actual delta comparison in this whole-suite run (no "
                     "positional filter was given). Every row printed above "
                     "was either present on only one side (\"new\"/"
                     "\"missing\", uncounted by design) or NOT COMPARABLE "
                     "(already reported, if any, above) — nothing here "
                     "actually certifies \"no regression\". Common causes: "
                     "the baseline parsed to zero rows (see the message "
                     "above, if printed), total key drift from a rename "
                     "between the baseline and this run, or every matched "
                     "case being excluded from --compare (not "
                     "baseline-eligible). Row counts: "
                  << row_counts << ".\n";
    }

    // FINDING 2: a "missing" row (present in the baseline, absent from this
    // run) means baseline COVERAGE was lost — a renamed or removed case
    // silently leaving the gate — which is a materially different claim from
    // "new" (a newly added case, benign by construction: nothing was ever
    // asserted about a row that simply didn't exist yet). Fatal ONLY in
    // whole-suite mode, exactly like `zero_compared` above and for the same
    // reason: a --compare narrowed with a positional filter to inspect one
    // case is a designed interactive workflow where every OTHER baseline row
    // legitimately shows as "missing" every time, and must stay exit 0.
    // Deliberately does NOT touch `new_rows`/the "new (not in baseline)"
    // status — that asymmetry is the point.
    const bool missing_in_whole_suite = whole_suite && missing_rows > 0;
    if (missing_in_whole_suite) {
        std::cout << "\nvc_bench: --compare FAILED — " << missing_rows
                  << " row(s) present in the baseline did not appear in this "
                     "whole-suite run (no positional filter was given). A "
                     "\"missing\" row means baseline coverage was LOST — the "
                     "case was renamed, removed, or otherwise stopped "
                     "running — not that it is new and unbaselined (that is "
                     "\"new\", which never fails the run). This is fatal "
                     "only for a whole-suite invocation; --compare narrowed "
                     "with a positional filter is unaffected. Row counts: "
                  << row_counts << ".\n";
    }

    // Consolidated pass/fail decision, split into two exit codes (full
    // contract in the function-level comment above): 1 for an
    // INFRASTRUCTURE failure (any_not_comparable, had_row_error, or
    // zero_compared — the comparison itself could not be trusted), 2 for a
    // PURE regression (the comparison ran cleanly but flagged a real
    // slowdown). Infrastructure wins the RETURN VALUE if both kinds are
    // present in the same run — it is the strictly more serious claim
    // ("this verdict cannot be trusted at all") and must never be silently
    // downgraded to the softer, advisory-shaped 2 just because a real
    // regression was also visible among the rows that DID compare. But
    // every applicable FAILED message below is still printed regardless of
    // which one decides the exit code — a run with both problems must
    // report both, not have the regression finding silently disappear just
    // because the exit code coalesces to 1. (Before this exit-code split
    // existed, the regression message printed unconditionally, ahead of the
    // infra messages, then the function always returned 1 if anything
    // failed; this preserves that exact print order/content, only the
    // return value split is new.)
    bool infra_failed = false;
    if (any_regression) {
        std::cout << "\nvc_bench: --compare FAILED — at least one "
                     "baseline-eligible case regressed past "
                  << threshold_pct
                  << "% and outside the combined err% noise band. Row "
                     "counts: "
                  << row_counts << ".\n";
    }
    if (any_not_comparable) {
        std::cout << "\nvc_bench: --compare FAILED — at least one row present "
                     "in both the baseline and this run had a missing, zero, "
                     "or negative median and could not actually be compared "
                     "(see \"NOT COMPARABLE\" above). A baseline that cannot "
                     "be compared against must not report as a clean run. "
                     "Row counts: "
                  << row_counts << ".\n";
        infra_failed = true;
    }
    if (had_row_error) {
        std::cout << "\nvc_bench: --compare FAILED — at least one row or case "
                     "could not be parsed (a field had an unexpected JSON "
                     "type, BUG 2; or a \"benches\"/\"results\" array element "
                     "was not even a recognizable case/row shape, BUG 5; see "
                     "the vc_bench: --compare: ... message(s) above) and was "
                     "skipped. A run with a skipped, unverifiable row or case "
                     "must not report as a clean comparison. Row counts: "
                  << row_counts << ".\n";
        infra_failed = true;
    }
    if (had_duplicate_key) {
        std::cout << "\nvc_bench: --compare FAILED — at least one side (the "
                     "baseline and/or this run) asserted the same case/row "
                     "key more than once (see the \"vc_bench: --compare: ... "
                     "contains N rows whose (title, name) pair is identical\" "
                     "message(s) above). A duplicate key means there is no "
                     "way to know which asserted value is authoritative; "
                     "silently keeping the first would discard every other "
                     "duplicate's value without it ever appearing as "
                     "\"missing\" or \"NOT COMPARABLE\". Row counts: "
                  << row_counts << ".\n";
        infra_failed = true;
    }
    if (zero_compared) {
        infra_failed =
            true; // message already printed above, at `if (zero_compared)`
    }
    if (missing_in_whole_suite) {
        infra_failed =
            true; // message already printed above, at `if (missing_in_whole_suite)`
    }
    if (infra_failed) {
        return 1;
    }
    if (any_regression) {
        return 2; // message already printed above, at `if (any_regression)`
    }
    std::cout << "\nvc_bench: --compare OK — " << row_counts
              << "; none of the compared rows regressed past " << threshold_pct
              << "%.\n";
    return 0;
}

} // namespace

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "vc_bench: correctness check FAILED before timing: "
                  << what << "\n";
        std::abort();
    }
}

void configure(ankerl::nanobench::Bench& bench, const std::string& title) {
    // Throughput unit; a non-pixel case (a refcount bump, one box/unbox)
    // overrides .unit()/.batch() itself.
    //
    // Stability (docs/benchmarking.md Sec 4.1): give each epoch a real time
    // budget so err% settles. minEpochTime auto-scales the iteration count to
    // the op's speed — a fast alloc runs thousands of iters, a heavy 12 MP pass
    // runs few — and the minEpochIterations floor keeps even a mid-speed op off
    // the "~1 iter / unstable" cliff. Time-based, not a fixed iteration count,
    // so one setting serves both cheap and fat kernels.
    bench.title(title)
        .unit("pixel")
        .minEpochTime(std::chrono::milliseconds(20))
        .minEpochIterations(10);
}

int run_suite(int argc,
              char** argv,
              const std::string& suite,
              const std::vector<bench_case>& cases) {
    std::vector<std::string> filters;
    bool want_baseline = false;
    bool want_list = false;
    bool want_compare = false;
    std::string compare_path;
    double threshold_pct = kDefaultThresholdPct;
    // CHANGE 1: --baseline-dir override. A SEPARATE flag, not `--baseline
    // <path>`: `--baseline` takes no argument today and [FILTER ...] is
    // positional, so `--baseline /tmp/x` is genuinely ambiguous between "the
    // write directory" and "a filter substring that happens to look like a
    // path" — and a "does it look like a path" heuristic is exactly this
    // codebase's dominant defect class (silently doing the wrong thing while
    // still exiting 0). A separate flag has no such ambiguity and leaves
    // `--baseline`'s existing meaning (and every existing invocation of it)
    // untouched.
    bool baseline_dir_given = false;
    std::string baseline_dir_override;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--baseline") {
            want_baseline = true;
        } else if (arg == "--baseline-dir") {
            if (i + 1 >= argc) {
                std::cerr << "vc_bench: --baseline-dir requires a directory "
                             "argument (usage: --baseline-dir <path>)\n";
                return 1;
            }
            baseline_dir_given = true;
            baseline_dir_override = argv[++i];
        } else if (arg == "--list") {
            want_list = true;
        } else if (arg == "--compare") {
            if (i + 1 >= argc) {
                std::cerr << "vc_bench: --compare requires a path argument "
                             "(usage: --compare <baseline.json>)\n";
                return 1;
            }
            want_compare = true;
            compare_path = argv[++i];
        } else if (arg == "--threshold") {
            if (i + 1 >= argc) {
                std::cerr << "vc_bench: --threshold requires a percent "
                             "argument (usage: --threshold <percent>)\n";
                return 1;
            }
            const std::string value = argv[++i];
            try {
                std::size_t consumed = 0;
                threshold_pct = std::stod(value, &consumed);
                if (consumed != value.size() || threshold_pct < 0.0) {
                    throw std::invalid_argument{"not a non-negative number"};
                }
            } catch (const std::exception&) {
                std::cerr << "vc_bench: --threshold expects a non-negative "
                             "percent (got \""
                          << value << "\")\n";
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "usage: " << argv[0]
                << " [--list] [--baseline] [--baseline-dir <path>] "
                   "[--compare <baseline.json>] [--threshold <percent>] "
                   "[FILTER ...]\n"
                << "  FILTER      run only cases whose name contains a "
                   "filter substring\n"
                << "  --list      print case names and exit\n"
                << "  --baseline  write <baseline-dir>/" << suite
                << "-<host>-<compiler>.{json,md} (OVERWRITES any existing "
                   "file at that path); <baseline-dir> defaults to the "
                   "committed bench/baselines/ (baked in at CMake configure "
                   "time) unless overridden below\n"
                << "  --baseline-dir <path>  write --baseline's output "
                   "under <path> instead of the compiled-in default; "
                   "created (mkdir -p) if it does not exist. Only meaningful "
                   "with --baseline (an error otherwise — --compare already "
                   "takes a full file path, so it has no use for a "
                   "directory). Use a scratch directory, never the source "
                   "tree's bench/baselines/, when experimenting.\n"
                << "  --compare <baseline.json>  READ-ONLY: run, then print a "
                   "delta table against the given baseline; never writes it. "
                   "Exit code: 0 if every present-on-both-sides row was "
                   "actually comparable and none regressed past --threshold "
                   "outside its err% noise band; 1 if the comparison itself "
                   "could not be trusted (at any filter level: "
                   "missing/malformed baseline, a row or case that could "
                   "not actually be compared, one that was not even a "
                   "recognizable shape, or a duplicated row/case key; "
                   "whole-suite runs only, i.e. no FILTER: zero rows "
                   "reaching a real comparison, or a baseline row missing "
                   "from this run); 2 if the comparison ran cleanly but "
                   "found a real regression. 1 wins if both 1- and "
                   "2-shaped problems occur in the same run.\n"
                << "  --threshold <percent>  override the --compare "
                   "regression threshold (default "
                << kDefaultThresholdPct << ")\n"
                << "  --baseline and --compare cannot be combined in one "
                   "invocation (see vc_bench_support.cpp: run_suite for why) "
                   "— run them separately.\n";
            return 0;
        } else {
            filters.push_back(arg);
        }
    }

    // BUG 3 fix: --baseline WRITES <baseline-dir>/<suite>-<host>-<compiler>
    // (<baseline-dir> defaults to the committed bench/baselines/, or
    // --baseline-dir's override — CHANGE 1) before --compare READS its
    // target, so passing both together — most
    // realistically `--baseline --compare <that same just-written path>`,
    // e.g. a CI script's "do both to be safe" habit, or a copy-paste typo —
    // makes --compare silently compare the just-written run against itself:
    // every row +0.00% ok, exit 0, ALWAYS, regardless of what actually
    // regressed. Rejected unconditionally (not just when the paths happen to
    // textually match): even when --compare targets a genuinely different
    // file, running both in one invocation is still a confusing "write a
    // fresh ground truth, then read a stale/unrelated one back" combination
    // that has no real use case a single invocation needs — anyone who wants
    // to both update the baseline AND check a run against a separate,
    // archived snapshot can simply run the binary twice (`--baseline`, then
    // `--compare <archived.json>`), which is unambiguous and costs nothing.
    // A same-path-only check was considered and rejected as the weaker,
    // fragile option: it cannot see through relative-vs-absolute paths or
    // symlinks, so a "clever" invocation could still slip through and hit
    // exactly the self-comparison this exists to prevent. See
    // docs/benchmarking.md and bench/baselines/README.md for the documented
    // decision.
    // CHANGE 1: --baseline-dir only means anything on the --baseline WRITE
    // path (--compare already takes an explicit full file path, so it has no
    // use for a directory override). Silently ignoring an override the user
    // typed would be a worse failure mode than refusing to run: the user
    // asked for output to go somewhere specific, and a silent fallback to the
    // committed bench/baselines/ default risks the exact clobber this flag
    // exists to prevent. Loud and non-zero instead.
    if (baseline_dir_given && !want_baseline) {
        std::cerr << "vc_bench: --baseline-dir has no effect without "
                     "--baseline (it names the directory --baseline writes "
                     "to; --compare already takes a full file path). Pass "
                     "--baseline as well, or drop --baseline-dir.\n";
        return 1;
    }

    if (want_baseline && want_compare) {
        std::cerr << "vc_bench: --baseline and --compare cannot be combined "
                     "in one invocation: --baseline WRITES the baseline file "
                     "before --compare would READ it, so passing both risks "
                     "(or, if the paths coincide, guarantees) comparing a run "
                     "against itself — a self-comparison that is always "
                     "+0.00% ok and can never detect a regression. Run them "
                     "separately: `--baseline` to update the committed "
                     "baseline, then a later `--compare <path>` to check a "
                     "run against it.\n";
        return 1;
    }

    if (want_list) {
        for (const auto& c : cases) {
            std::cout << c.name
                      << (c.baseline_eligible ? "" : "  [not baselined]")
                      << "\n";
        }
        return 0;
    }

    const auto selected = [&](const bench_case& c) {
        if (filters.empty()) {
            return true;
        }
        for (const auto& f : filters) {
            if (c.name.find(f) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    std::string md_body;
    std::vector<std::string> json_parts;
    std::size_t ran = 0;

    for (const auto& c : cases) {
        if (!selected(c)) {
            continue;
        }
        ++ran;

        // Capture nanobench's own default Markdown table into a string rather
        // than letting it print straight to stdout, so the exact table can be
        // both echoed here AND written verbatim to the .md baseline.
        std::ostringstream table;
        ankerl::nanobench::Bench bench;
        configure(bench, c.name);
        bench.output(&table);
        c.body(bench);
        std::cout << table.str();

        // Same per-case json() render feeds both --baseline (written to disk)
        // and --compare (read back in-process, never written) — one source
        // of truth for "what does a baseline-eligible case's result look
        // like as JSON". Gated on baseline_eligible for --compare too: a
        // stubbed case was never trustworthy enough to WRITE into a
        // baseline, so it is equally not trustworthy enough to gate a
        // regression check on.
        if ((want_baseline || want_compare) && c.baseline_eligible) {
            std::ostringstream js;
            bench.render(ankerl::nanobench::templates::json(), js);
            json_parts.push_back(js.str());
        }
        if (want_baseline && c.baseline_eligible) {
            md_body +=
                table_only(table.str()) + "\n"; // blank line between cases
        }
    }

    if (ran == 0) {
        std::cerr << "vc_bench: no case matched the given filter(s).\n";
        return 1;
    }

    if (want_baseline && json_parts.empty()) {
        std::cout
            << "\nvc_bench: --baseline requested but no baseline-eligible "
               "case ran; wrote nothing (a stubbed case is not baselined "
               "until its real body lands — see docs/benchmarking.md "
               "Sec 9).\n";
    } else if (want_baseline) {
        // CHANGE 1: default preserved EXACTLY when --baseline-dir is
        // omitted (the compile-time VC_BENCH_BASELINE_DIR, unchanged) — only
        // an explicit override touches this.
        const std::string baseline_dir =
            baseline_dir_given ? baseline_dir_override
                               : std::string{VC_BENCH_BASELINE_DIR};

        if (baseline_dir_given) {
            // Auto-create ONLY the explicitly-requested override directory —
            // never the compile-time default, which is always the committed
            // bench/baselines/ and must already exist. The write path below
            // (write_file) does not create directories on its own (a plain
            // std::ofstream fails open() against a missing parent), so
            // without this, a scratch directory that doesn't exist yet would
            // just fail with "could not open ... for writing" — a worse
            // first-run experience for the exact scratch-testing workflow
            // this override exists for. A typo'd or unwritable path still
            // fails loudly below (write_file's own error path). Uses
            // std::filesystem directly rather than vc::io::ensure_directory
            // (src/io/vc_io_fs.cpp): that helper throws vc::vc_exception on
            // failure, but this whole --baseline/--compare mode is
            // deliberately exception-free end to end (see run_suite's
            // header comment) — no bench main() wraps run_suite() in a
            // try/catch, so a thrown exception here would propagate out of
            // main() as an unhandled exception (std::terminate) instead of
            // the clean std::cerr + non-zero return every other failure
            // path in this file uses.
            std::error_code ec;
            std::filesystem::create_directories(baseline_dir, ec);
            if (ec) {
                std::cerr << "vc_bench: --baseline-dir: could not create "
                             "directory \""
                          << baseline_dir << "\": " << ec.message() << "\n";
                return 1;
            }
        }

        const std::string stem =
            baseline_dir + "/" + suite + "-" + host_compiler_key();

        bool ok = write_file(stem + ".md", markdown_metadata(suite) + md_body);

        // { "meta": {...}, "benches": [ {results}, {results} ] } — each element
        // is one case's self-contained json() render, so the array is valid
        // JSON (verified: json() emits a top-level object).
        std::ostringstream json;
        json << "{\n" << json_metadata(suite) << ",\n  \"benches\": [\n";
        for (std::size_t i = 0; i < json_parts.size(); ++i) {
            json << json_parts[i];
            if (i + 1 < json_parts.size()) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]\n}\n";
        ok = write_file(stem + ".json", json.str()) && ok;

        if (!ok) {
            return 1; // a failed baseline write must not look like success
        }
        std::cout << "\nvc_bench: wrote baseline " << stem << ".{json,md} ("
                  << json_parts.size() << " baseline-eligible case(s))\n";
    }

    if (want_compare) {
        // CHANGE 3: `filters.empty()` is exactly "this is a whole-suite
        // invocation" — the same predicate `selected()` above already used
        // to decide "run everything" vs. "run only what matches a filter".
        return run_compare(suite, compare_path, threshold_pct, json_parts,
                           /*whole_suite=*/filters.empty());
    }

    return 0;
}

} // namespace vc::bench
