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
#include <unordered_map>
#include <utility>
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

    // ---- passthrough: the real rung ladder (rung 1 vs rung 2a) ----
    // NOT baseline-eligible YET: both rungs copy a vc_image whose pixels_ is a
    // null shared_ptr under the stubbed vc_image_info::element_count() (still a
    // TODO(you) rep, so zeros()/with_fill() allocate a 0-element buffer), so
    // today they measure the box/map/dispatch plumbing but NOT the refcount
    // atomic a real copy carries. rung1 (a bare handle copy) is dominated by
    // that absent atomic — ~0.26 ns is essentially "no control block touched".
    // Implementing element_count() changes these numbers but is NOT a "bench
    // code change", so the Sec 8 lifecycle rule would not flag a committed
    // baseline for regeneration — the exact reason grayscale/mean are excluded.
    // So passthrough is gated the same way: it runs and prints (the ladder is
    // wired and ready), but is committed only once the rep lands. Flip to true
    // then (and add a correctness gate).
    cases.push_back(
        {"passthrough", /*baseline_eligible=*/false,
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
             // writing it here respects the infra-only boundary. Caveat (Sec 9):
             // with the stubbed ctor pixels_ is a NULL shared_ptr, so this copy
             // does no refcount/atomic work today (copying a null shared_ptr
             // touches no control block) — it becomes a real refcount bump once
             // the ctor allocates. A partial floor, like rung 2a.
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

    // ---- grayscale: wired + ready, NOT baselined (stubbed process today) ----
    cases.push_back(
        {"grayscale", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_grayscale_stage;
             const stage_t stage("bench_grayscale");
             const vc::vc_image image =
                 vc::vc_image::zeros<vc::buf_f32>(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::rgb.name};

             vc::pipe::vc_pipe_context ctx{one_input(in_slot, image)};

             // No correctness gate yet: process() is a no-op stub, so there is
             // no output to assert. Add a check() here when you implement the
             // luminance kernel (and flip baseline_eligible to true).
             bench.unit("pixel")
                 .batch(static_cast<double>(kWidth) * kHeight)
                 .relative(false);
             bench.run("grayscale stage.process() [STUB]", [&] {
                 stage.process(ctx);
                 ankerl::nanobench::doNotOptimizeAway(&ctx);
             });
         }});

    // ---- mean_brightness: wired + ready, NOT baselined (stubbed today) ----
    cases.push_back(
        {"mean_brightness", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_mean_brightness_stage;
             const stage_t stage("bench_mean");
             const vc::vc_image image =
                 vc::vc_image::zeros<vc::buf_f32>(kWidth, kHeight, kChannels);

             const vc::pipe::slot_name in_slot{stage_t::slots::image.name};

             vc::pipe::vc_pipe_context ctx{one_input(in_slot, image)};

             bench.unit("pixel")
                 .batch(static_cast<double>(kWidth) * kHeight)
                 .relative(false);
             bench.run("mean_brightness stage.process() [STUB]", [&] {
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
