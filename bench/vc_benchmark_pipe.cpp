// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Tier-2 MICRO benchmarks — kernels and single stages, and the lower rungs of
// the rung ladder (docs/benchmarking.md Sec 6): rung 1 (raw op) and rung 2a
// (stage.process()). The delta between them is ONE layer of framework — the
// std::any box/unbox, the context map lookups, the virtual process() dispatch.
// Rung 2b (pipeline.run()) is the sibling vc_benchmark_pipeline suite, per the
// one-binary-per-category split (Sec 7).
//
// REALITY GATE (Sec 9): EVERY case in this suite runs and prints (each calls
// the REAL stage API and is wired to light up untouched the moment its rep
// lands), but NONE is baseline-eligible yet, so nothing here enters the
// committed timeseries today. `passthrough` process() is implemented, but both
// its ladder rungs copy a vc_image whose buffer is null under the stubbed ctor
// (see the case comment) — a "partial floor", gated on the vc_image ctor.
// `grayscale` / `mean_brightness` process() bodies are still your TODO reps,
// gated on those kernels. The vc_benchmark_harness suite carries the real
// baselines today; this suite's cases land as each rep is implemented and its
// case flipped to eligible.
//
// Run:  vc_benchmark_pipe
//       vc_benchmark_pipe passthrough --baseline
//       vc_benchmark_pipe --list

#include <string>
#include <vector>

#include "nanobench.h"
#include "vc_bench_support.h"

#include "vc/pipe/stages/vc_grayscale_stage.h"
#include "vc/pipe/stages/vc_mean_brightness_stage.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/vc_image.h"

namespace {

constexpr vc::image_dim kWidth = 1600;
constexpr vc::image_dim kHeight = 1200;
constexpr vc::channel_count kChannels = 3;

// Drive one stage's process() once through a fresh context: inject `in_slot`
// with the image, run, and return whether the expected output slot was
// produced. Isolates rung 2a — the box/unbox + map lookups + virtual dispatch
// (Sec 6) — from per-iteration context construction by taking the context by
// reference (the caller reuses one).
bool run_process_once(const vc::pipe::i_pipe& stage,
                      vc::pipe::vc_pipe_context& ctx,
                      const vc::pipe::slot_name& out_slot) {
    stage.process(ctx);
    return ctx.has_output(out_slot);
}

std::vector<vc::bench::bench_case> micro_cases() {
    std::vector<vc::bench::bench_case> cases;

    // ---- passthrough: the real rung ladder (rung 1 vs rung 2a) ----
    // NOT baseline-eligible YET: both rungs copy a vc_image whose pixels_ is a
    // null shared_ptr under the stubbed ctor, so today they measure the box/map/
    // dispatch plumbing but NOT the refcount atomic a real copy carries. rung1
    // (a bare handle copy) is dominated by that absent atomic — ~0.26 ns is
    // essentially "no control block touched". Implementing the vc_image ctor
    // changes these numbers but is NOT a "bench code change", so the Sec 8
    // lifecycle rule would not flag a committed baseline for regeneration — the
    // exact reason grayscale/mean are excluded. So passthrough is gated the same
    // way: it runs and prints (the ladder is wired and ready), but is committed
    // only once the ctor lands. Flip to true then (and add a correctness gate).
    cases.push_back(
        {"passthrough", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_passthrough_stage;
             const stage_t stage("bench_passthrough");
             const vc::vc_image image(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::in.name};
             const vc::pipe::slot_name out_slot{stage_t::slots::out.name};

             // Reused context: input injected once; process() overwrites its
             // output each call. This is what makes the timed region rung 2a's
             // own overhead, not repeated map allocation.
             vc::pipe::vc_pipe_context ctx;
             ctx.set_input(in_slot, vc::pipe::vc_pipe_packet{image});

             // Sanity before timing (Sec 4.5): the stage really republishes the
             // image on its output slot.
             vc::bench::check(run_process_once(stage, ctx, out_slot),
                              "passthrough produced no output");

             bench.unit("op").batch(1.0).relative(true);

             // Rung 1 — raw framework identity op: a shallow vc_image copy.
             // NOT the user's algorithm; the framework's own no-op carry, so
             // writing it here respects the infra-only boundary. Caveat (Sec 9):
             // with the stubbed ctor pixels_ is a NULL shared_ptr, so this copy
             // does no refcount/atomic work today (copying a null shared_ptr
             // touches no control block) — it becomes a real refcount bump once
             // the ctor allocates. A partial floor, like rung 2a.
             bench.run("rung1 raw shallow copy", [&] {
                 vc::vc_image out = image;
                 ankerl::nanobench::doNotOptimizeAway(out.width());
             });

             // Rung 2a — the same carry THROUGH stage.process().
             bench.run("rung2a stage.process()", [&] {
                 stage.process(ctx);
                 ankerl::nanobench::doNotOptimizeAway(ctx.has_output(out_slot));
             });
         }});

    // ---- grayscale: wired + ready, NOT baselined (stubbed process today) ----
    cases.push_back(
        {"grayscale", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_grayscale_stage;
             const stage_t stage("bench_grayscale");
             const vc::vc_image image(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::rgb.name};

             vc::pipe::vc_pipe_context ctx;
             ctx.set_input(in_slot, vc::pipe::vc_pipe_packet{image});

             // No correctness gate yet: process() is a no-op stub, so there is
             // no output to assert. Add a check() here when you implement the
             // luminance kernel (and flip baseline_eligible to true).
             bench.unit("pixel")
                 .batch(static_cast<double>(kWidth) * kHeight)
                 .relative(false);
             bench.run("grayscale stage.process() [STUB]", [&] {
                 stage.process(ctx);
                 ankerl::nanobench::doNotOptimizeAway(
                     ctx.has_output(vc::pipe::slot_name{stage_t::slots::grey.name}));
             });
         }});

    // ---- mean_brightness: wired + ready, NOT baselined (stubbed today) ----
    cases.push_back(
        {"mean_brightness", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_mean_brightness_stage;
             const stage_t stage("bench_mean");
             const vc::vc_image image(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::image.name};

             vc::pipe::vc_pipe_context ctx;
             ctx.set_input(in_slot, vc::pipe::vc_pipe_packet{image});

             bench.unit("pixel")
                 .batch(static_cast<double>(kWidth) * kHeight)
                 .relative(false);
             bench.run("mean_brightness stage.process() [STUB]", [&] {
                 stage.process(ctx);
                 ankerl::nanobench::doNotOptimizeAway(
                     ctx.has_output(vc::pipe::slot_name{stage_t::slots::mean.name}));
             });
         }});

    return cases;
}

} // namespace

int main(int argc, char** argv) {
    return vc::bench::run_suite(argc, argv, "pipe", micro_cases());
}
