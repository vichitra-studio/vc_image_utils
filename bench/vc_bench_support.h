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
    // baseline. Cases that today exercise a stubbed body (a no-op kernel, the
    // empty-map `pipeline::run()`) set this false: they still run and print to
    // stdout (the harness value is that they are wired and ready), but they are
    // EXCLUDED from the written baseline files — baselining an empty-map return
    // would pollute the git-history-as-timeseries, and a later stub->real fill
    // is not a "bench code change", so the Sec 8 lifecycle rule would not flag
    // it for regeneration. See docs/benchmarking.md Sec 8, Sec 9.
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
//   --baseline     after running, WRITE bench/baselines/<suite>-<host>-
//                  <compiler>.{json,md} from the baseline-eligible cases.
//   --help         usage and exit.
// Runs the selected cases (each into its own configured Bench, printing its
// Markdown table to stdout), and returns a process exit code.
int run_suite(int argc,
              char** argv,
              const std::string& suite,
              const std::vector<bench_case>& cases);

} // namespace vc::bench
