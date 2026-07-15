// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc_bench_support.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "nanobench.h"

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
        std::cerr << "vc_bench: could not open baseline file for writing: "
                  << path << " (does bench/baselines/ exist?)\n";
        return false;
    }
    out << contents;
    if (!out) {
        std::cerr << "vc_bench: write failed for baseline file: " << path << "\n";
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
    return captured.substr(line_start == std::string::npos ? 0 : line_start + 1);
}

} // namespace

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "vc_bench: correctness check FAILED before timing: " << what
                  << "\n";
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

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--baseline") {
            want_baseline = true;
        } else if (arg == "--list") {
            want_list = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "usage: " << argv[0]
                      << " [--list] [--baseline] [FILTER ...]\n"
                      << "  FILTER      run only cases whose name contains a "
                         "filter substring\n"
                      << "  --list      print case names and exit\n"
                      << "  --baseline  write bench/baselines/" << suite
                      << "-<host>-<compiler>.{json,md}\n";
            return 0;
        } else {
            filters.push_back(arg);
        }
    }

    if (want_list) {
        for (const auto& c : cases) {
            std::cout << c.name << (c.baseline_eligible ? "" : "  [not baselined]")
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

        if (want_baseline && c.baseline_eligible) {
            md_body += table_only(table.str()) + "\n"; // blank line between cases
            std::ostringstream js;
            bench.render(ankerl::nanobench::templates::json(), js);
            json_parts.push_back(js.str());
        }
    }

    if (ran == 0) {
        std::cerr << "vc_bench: no case matched the given filter(s).\n";
        return 1;
    }

    if (want_baseline && json_parts.empty()) {
        std::cout << "\nvc_bench: --baseline requested but no baseline-eligible "
                     "case ran; wrote nothing (a stubbed case is not baselined "
                     "until its real body lands — see docs/benchmarking.md "
                     "Sec 9).\n";
    } else if (want_baseline) {
        const std::string stem =
            std::string{VC_BENCH_BASELINE_DIR} + "/" + suite + "-" +
            host_compiler_key();

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

    return 0;
}

} // namespace vc::bench
