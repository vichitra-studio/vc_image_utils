// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <functional>
#include <string>
#include <vector>

// Forward-declared so this header stays light: a case only names Bench by
// reference. The full definition is included where a case body or the runner
// actually calls into nanobench (the bench_*.cpp files and vc_bench_support.cpp).
namespace ankerl::nanobench {
class Bench;
} // namespace ankerl::nanobench

namespace vc::bench {

// One named benchmark case. `body` receives a FRESH Bench that run_suite has
// already titled and given the house defaults (§ configure below); the body
// adds one or more `.run(...)` rows and MAY call `.relative(true)`. Because the
// Bench belongs to this case alone, `.relative()` is scoped to this case's own
// rungs — which is exactly what the rung ladder (docs/benchmarking.md Sec 6)
// needs: three rungs of the SAME kernel compared in one relative group, with no
// bleed from an unrelated case.
struct bench_case {
    std::string name; // matched (substring) against argv filters

    // Whether this case's numbers are trustworthy enough to COMMIT as a
    // baseline. A case that exercises a stubbed body (a `TODO(you)` kernel
    // that has not landed yet) should set this false: it still runs and
    // prints to stdout (the harness value is that it is wired and ready), but
    // is EXCLUDED from the written baseline files — baselining a stub's
    // output would pollute the git-history-as-timeseries, and a later
    // stub->real fill is not a "bench code change", so the Sec 8 lifecycle
    // rule would not flag it for regeneration. As of 2026-07-27 every wired
    // case in bench/ measures real, implemented code and is eligible — this
    // flag exists for the next stub, not a currently-gated one. See
    // docs/benchmarking.md Sec 8, Sec 9.
    bool baseline_eligible = true;

    std::function<void(ankerl::nanobench::Bench&)> body;
};

// Correctness gate. A benchmark of a wrong kernel is a fast lie
// (docs/benchmarking.md Sec 4.5): assert the expected result ONCE, outside the
// timed loop, before trusting a number. Prints `what` and aborts on failure —
// deliberately hard (not an exception a case might swallow), and NDEBUG-proof
// (unlike assert(), which the Release bench build compiles out).
void check(bool ok, const std::string& what);

// House-default configuration applied to every case's Bench before its body
// runs. The load-bearing part is STABILITY: enough epoch time / min iterations
// to lift a mid-to-heavy op off the "~1.0 iters / unstable" cliff (a single
// 12 MP pass can otherwise exceed one epoch's min time and report one iter).
// It is a FLOOR, not a settle-guarantee for every op: an exceptionally heavy,
// memory-bound case (e.g. the substrate `copy` deep-copy row, ~340 µs) can
// still show single-digit err% at this budget and raises its OWN minEpochTime
// on top. It
// also sets a "pixel" unit as a convenience default, but every current case
// sets its own `.unit()`/`.batch()` (per-pixel or per-op), so that default is
// just a fallback for a future case that forgets. Exposed (not private to
// run_suite) so a case that needs a SECOND Bench can re-apply the same defaults.
void configure(ankerl::nanobench::Bench& bench, const std::string& title);

// The entry point every bench_*.cpp main() forwards to. Parses argv:
//   (positional)   substring filters — a case runs if its name contains ANY
//                  filter; with no filter, every case runs.
//   --list         print case names and exit (the stand-in for the
//                  auto-registration/-filter nanobench lacks, Sec 3.4/Sec 7).
//   --baseline     after running, WRITE <baseline-dir>/<suite>-<host>-
//                  <compiler>.{json,md} from the baseline-eligible cases.
//                  <baseline-dir> is the compiled-in default (the committed
//                  bench/baselines/, baked in by CMake) unless overridden by
//                  --baseline-dir.
//   --baseline-dir <path>   override the directory --baseline writes into
//                  (default unchanged when omitted). Created (mkdir -p) if
//                  it does not already exist. Meaningless, and rejected as a
//                  hard error, without --baseline in the same invocation —
//                  --compare already takes a full file path, so it has no
//                  use for a directory. A SEPARATE flag rather than
//                  `--baseline <path>`: `--baseline` takes no argument and
//                  [FILTER ...] is positional, so `--baseline /tmp/x` would
//                  be genuinely ambiguous between "the write directory" and
//                  "a filter substring that happens to look like a path" —
//                  this stays unambiguous and leaves --baseline's existing
//                  meaning untouched. Use a scratch directory when
//                  experimenting; never point this at the source tree's own
//                  bench/baselines/ except via the ordinary --baseline path.
//   --compare <path>   after running, READ the baseline .json at `path` and
//                  print a per-benchmark delta table against the just-run
//                  numbers. NEVER writes `path` (that is what makes this the
//                  detector `--baseline` alone is not: `--baseline` overwrites
//                  the file in place, so nothing stops a regression from being
//                  silently promoted to the new baseline; `--compare` cannot do
//                  that even by accident). Only baseline-eligible cases are
//                  compared (a stubbed case was never meant to be trustworthy
//                  enough to gate on). Cannot be combined with `--baseline` in
//                  the same invocation — rejected at argument-parsing time,
//                  before either mode runs (see vc_bench_support.cpp:
//                  run_suite for why: --baseline WRITES before --compare
//                  would READ, so combining them risks, or with a matching
//                  path guarantees, a self-comparison that is always
//                  "+0.00% ok").
//
//                  Exit code: 0 iff EVERY row present on both the baseline
//                  and this run was actually comparable, and none regressed
//                  past --threshold outside its err% noise band. Otherwise
//                  the exit code is split so a caller can tell "the
//                  comparison ran cleanly but found a real regression"
//                  (advisory-worthy, e.g. safe to soften with `|| true` in a
//                  non-blocking CI step) apart from "the comparison could not
//                  actually be trusted" (never worth softening) — see
//                  run_compare's function comment in vc_bench_support.cpp for
//                  the full contract:
//                    1  an INFRASTRUCTURE failure — something kept part of
//                       the comparison from happening at all:
//                         - a missing/unreadable/malformed baseline FILE
//                           (nothing usable to compare against at all);
//                         - a row present on both the baseline and this run
//                           whose median is missing, zero, or negative on
//                           either side (its own "NOT COMPARABLE" status,
//                           never a "+0.00%" delta — a baseline whose rows
//                           are simply missing the expected numeric field,
//                           e.g. after a nanobench field rename, used to
//                           silently read as "no change" for the entire
//                           suite);
//                         - a row with any field present but of the wrong
//                           JSON type (a hand-edited or version-skewed
//                           baseline) — that one row is skipped with a clear
//                           message rather than aborting the whole
//                           comparison;
//                         - a `benches` array element that is not a JSON
//                           object, an object missing its "results" field,
//                           an object whose "results" is present but not an
//                           array, a "results" array element that is not
//                           a JSON object, or a "results" array element that
//                           IS a JSON object but carries none of nanobench's
//                           row fields (title/name/median(elapsed)/
//                           medianAbsolutePercentError(elapsed)) — none of
//                           these are shapes `--baseline` can ever write, so
//                           each is reported by name and contributes zero
//                           rows rather than
//                           silently vanishing (previously: a `benches`
//                           array mixing such elements among valid case
//                           objects was indistinguishable from an
//                           intentionally-trimmed baseline — nothing named
//                           the garbage element, and the run could exit 0).
//                           A well-formed case object with a genuinely empty
//                           "results" array is NOT one of these — that shape
//                           is exactly what nanobench's own json() render
//                           produces for a case whose Bench completed zero
//                           `.run()` calls, so it is reported informationally
//                           only and does not affect the exit code;
//                         - a duplicate row/case key — two or more rows
//                           whose (title, name) pair is identical, on EITHER
//                           side (the baseline file, or this run's own case
//                           results) — at ANY filter level (unlike the two
//                           whole-suite-only causes below, this one is NOT
//                           gated on whole_suite). A duplicate key means
//                           there is no way to know which asserted value is
//                           authoritative; silently keeping the first would
//                           discard every other duplicate's value without it
//                           ever appearing as "missing" or "NOT COMPARABLE";
//                         - (whole-suite invocations only, i.e. no
//                           positional FILTER) zero rows anywhere reaching an
//                           actual delta comparison — every row present on
//                           only one side ("new"/"missing", see below) or
//                           NOT COMPARABLE, so nothing was actually certified
//                           even though nothing individually failed. NOT
//                           checked when a FILTER narrowed the run: printing
//                           "new (not in baseline)" for a brand-new,
//                           not-yet-baselined case is a designed interactive
//                           workflow (see the "new (not in baseline)"
//                           handling in run_compare) and must stay exit 0.
//                         - (whole-suite invocations only, i.e. no positional
//                           FILTER) at least one row that is "missing" — in
//                           the baseline but absent from this run. A missing
//                           row means baseline COVERAGE was lost (a case
//                           renamed or removed without regenerating the
//                           baseline), which a rename-heavy refactor could
//                           otherwise drop most of a suite's rows into while
//                           still exiting 0. NOT checked when a FILTER
//                           narrowed the run: every OTHER baseline case
//                           legitimately shows as "missing" whenever
//                           inspecting one case by name, and that must stay
//                           exit 0 — this is the same whole-suite-only
//                           gating as the zero-rows case above, and does NOT
//                           extend to "new" (current-only) rows, which stay
//                           purely informational at every filter level: a
//                           newly added case is benign by construction,
//                           unlike a case that silently stopped comparing.
//                    2  a PURE regression — the comparison itself ran
//                       cleanly (none of the above fired) but at least one
//                       row regressed past --threshold outside its err%
//                       noise band. If both an infrastructure failure and a
//                       regression occur in the same run, 1 wins — an
//                       infrastructure failure is the more serious claim and
//                       is never silently downgraded to 2.
//                  A "new" row (in this run, not the baseline) is ALWAYS
//                  purely informational and never fails the run, at any
//                  filter level — nothing was ever asserted about a case
//                  that simply didn't exist yet. A "missing" row (in the
//                  baseline, not this run) is informational ONLY for a
//                  FILTERED invocation (a narrower --list filter is exactly
//                  what makes every other baseline case "missing", by
//                  design); for a WHOLE-SUITE invocation it is fatal (exit
//                  1, see above) — the whole point of an unfiltered compare
//                  is to certify the entire suite, and a case that quietly
//                  stopped being compared is lost gate coverage, not a
//                  benign absence. Nothing in this mode ever crashes: every
//                  failure mode above is a `std::cerr` message plus a
//                  non-zero return, never an unhandled exception. Every
//                  summary line (OK or FAILED) reports how many rows were
//                  actually compared, new, and missing — the OK line no
//                  longer claims "every comparable row was actually
//                  compared" unconditionally, since that read as true even
//                  when a rename had silently dropped most of the suite out
//                  of the comparison.
//
//                  Provenance (host/compiler/-march/flags/git SHA/measured-
//                  at, from the baseline's `meta` block and this run's own
//                  identity) is PRINTED in the header for every USABLE
//                  baseline (i.e. once the baseline FILE itself parsed —
//                  an unreadable/malformed-JSON/no-"benches" file still
//                  exits 1 before any provenance is printed, same as
//                  before), and a difference in host/compiler/march/flags
//                  is WARNED on by name — but this is reporting, not
//                  enforcement: it never
//                  changes the exit code. A CI-generated baseline's
//                  meta.host is an ephemeral runner hostname, different on
//                  every run, so hard-failing on a provenance mismatch would
//                  make the committed CI compare job permanently red — the
//                  operator remains responsible for deciding whether two
//                  runs are actually comparable. A baseline with a missing
//                  or malformed `meta` block is handled gracefully (reported
//                  as "provenance unknown"), never a crash or a silent
//                  blank.
//   --threshold <pct>  override the --compare regression threshold (percent
//                  median slowdown). Default is documented next to its
//                  constant in vc_bench_support.cpp. A delta is only flagged
//                  once it ALSO exceeds the two measurements' combined err%
//                  band, so a threshold-crossing that is really just noise
//                  does not fail the run — UNLESS either side's own err% is
//                  implausibly high (above a fixed, --threshold-independent
//                  ceiling; see kMaxTrustworthyErrFrac in
//                  vc_bench_support.cpp), in which case that measurement is
//                  not trusted enough to license suppression and the row
//                  falls through to the ordinary threshold check instead.
//   --help         usage and exit.
// Runs the selected cases (each into its own configured Bench, printing its
// Markdown table to stdout), and returns a process exit code.
int run_suite(int argc,
              char** argv,
              const std::string& suite,
              const std::vector<bench_case>& cases);

} // namespace vc::bench
