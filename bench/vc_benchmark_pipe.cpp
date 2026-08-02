// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Tier-2 MICRO benchmarks — kernels and single stages, and the lower rungs of
// the rung ladder (docs/benchmarking.md Sec 6): rung 1 (raw op) and rung 2a
// (stage.process()). The delta between them is ONE layer of framework — the
// std::any box/unbox, the context map lookups, the virtual process() dispatch.
// Rung 2b (pipeline.run()) is the sibling vc_benchmark_pipeline suite, per the
// one-binary-per-category split (Sec 7).
//
// REALITY GATE (Sec 9): the one case in this suite calls the REAL stage API
// on a REAL buffer — vc_image_info::element_count() (and so zeros()/
// with_fill()) has been implemented since commit 5daa148, so this is no
// longer a null-pixels partial floor. Baseline-eligible since 2026-07-27 —
// see the case comment for the numbers that justified the flip.
//
// grayscale/mean_brightness (and blur) moved to tests/samples/ — worked
// examples for pipe-framework mechanics, not production stages the library
// links, so they are no longer benchmark subjects here.
//
// Run:  vc_benchmark_pipe
//       vc_benchmark_pipe passthrough --baseline
//       vc_benchmark_pipe --list

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nanobench.h"
#include "vc_bench_support.h"

#include "vc/core/vc_image.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_packet.h"

namespace {

constexpr vc::image_dim kWidth = 1600;
constexpr vc::image_dim kHeight = 1200;
constexpr vc::channel_count kChannels = 3;

// Build the one-entry input map binding `in_slot` to `image`. Inputs now enter
// a context ONLY through its constructor (the name-keyed setter was encapsulated
// away, docs/pipe_design.md Sec 12.5), so a fresh input map is the supported
// handoff.
std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet>
one_input(const vc::pipe::slot_name& in_slot, const vc::vc_image& image) {
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(in_slot, vc::pipe::vc_pipe_packet{image});
    return inputs;
}

// One-time correctness gate (NOT timed): run the stage once through a THROWAWAY
// context and report whether it published `out_slot`. take_outputs() is the only
// harvest surface and consumes the outputs, so this uses its own context and
// leaves the reused timed context untouched. The timed loops below isolate rung
// 2a — box/unbox + map lookups + virtual dispatch (Sec 6) — by reusing a single
// pre-built context, so this construction cost stays out of the measurement.
bool produces_output(
    const vc::pipe::i_pipe& stage,
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs,
    const vc::pipe::slot_name& out_slot) {
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    stage.process(ctx);
    const auto outputs = std::move(ctx).take_outputs();
    return outputs.find(out_slot) != outputs.end();
}

std::vector<vc::bench::bench_case> micro_cases() {
    std::vector<vc::bench::bench_case> cases;

    // Baseline-eligible: element_count() is implemented (commit 5daa148), so
    // zeros()/with_fill() allocate a REAL buffer with a REAL shared_ptr — both
    // rungs copy a live vc_image, not a null handle. rung1 (a bare handle
    // copy) now prices a genuine refcount atomic: ~3.15 ns, not the ~0.26 ns
    // a null-control-block copy would show (verified 2026-07-27, low err%,
    // stable across repeated runs). A correctness gate already exists below
    // (produces_output, checked before timing, Sec 4.5). Note this is
    // unrelated to grayscale/mean_brightness: those never lived here — they
    // moved to tests/samples/ (top-of-file note) and are not benchmark
    // subjects in this suite at all, stub or otherwise.
    cases.push_back(
        {"passthrough", /*baseline_eligible=*/true,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_passthrough_stage;
             const stage_t stage("bench_passthrough");
             const vc::vc_image image =
                 vc::vc_image::zeros<vc::buf_f32>(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::in.name};
             const vc::pipe::slot_name out_slot{stage_t::slots::out.name};

             // Reused context: input injected at construction; process()
             // overwrites its output each call. Building it ONCE makes the timed
             // region rung 2a's own overhead, not repeated map allocation.
             vc::pipe::vc_pipe_context ctx{one_input(in_slot, image)};

             // Sanity before timing (Sec 4.5): the stage really republishes the
             // image on its output slot — checked on a throwaway context so the
             // reused `ctx` above is left untouched.
             vc::bench::check(
                 produces_output(stage, one_input(in_slot, image), out_slot),
                 "passthrough produced no output");

             bench.unit("op").batch(1.0).relative(true);

             // Rung 1 — raw framework identity op: a shallow vc_image copy.
             // NOT the user's algorithm; the framework's own no-op carry, so
             // writing it here respects the infra-only boundary. `image` holds
             // a real, allocated buffer (element_count() is implemented), so
             // this copy is a genuine shared_ptr refcount bump — the full
             // floor, not a partial one.
             bench.run("rung1 raw shallow copy", [&] {
                 vc::vc_image out = image;
                 ankerl::nanobench::doNotOptimizeAway(out.width());
             });

             // Rung 2a — the same carry THROUGH stage.process(). The virtual
             // process() writes ctx's private output map (a real side effect, so
             // it is never elided); doNotOptimizeAway(&ctx) keeps the loop body
             // anchored without a per-iteration harvest that would pollute the
             // timing (take_outputs() moves the whole map out).
             bench.run("rung2a stage.process()", [&] {
                 stage.process(ctx);
                 ankerl::nanobench::doNotOptimizeAway(&ctx);
             });
         }});

    return cases;
}

} // namespace

int main(int argc, char** argv) {
    return vc::bench::run_suite(argc, argv, "pipe", micro_cases());
}
