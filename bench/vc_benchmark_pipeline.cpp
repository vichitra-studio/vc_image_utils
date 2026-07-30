// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Tier-2 MACRO benchmarks — representative, END-TO-END pipelines through
// vc_pipeline::run(): rung 2b of the ladder (docs/benchmarking.md Sec 6). This
// is the "is the design causing slowdowns?" instrument at the whole-pipe level;
// a missed/accidental deep copy shows as an MP/s cliff above the known plumbing
// cost. We benchmark REPRESENTATIVE / shipped pipelines only — NOT the powerset
// of stages (Sec 5.3): per-stage + plumbing overhead already predicts a novel
// wiring.
//
// REALITY GATE (Sec 9): vc_pipeline::run() and validate() are fully
// implemented — run() walks stages in insertion order, resolves open
// inputs/outputs, and moves real packets through process(). This case
// baseline-eligible since 2026-07-27 — see the case comment for the epoch
// tuning that converged the cross-run median.
//
// Run:  vc_benchmark_pipeline
//       vc_benchmark_pipeline --list

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nanobench.h"
#include "vc_bench_support.h"

#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/vc_image.h"

namespace {

constexpr vc::image_dim kWidth = 1600;
constexpr vc::image_dim kHeight = 1200;
constexpr vc::channel_count kChannels = 3;

using packet_map =
    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet>;

std::vector<vc::bench::bench_case> macro_cases() {
    std::vector<vc::bench::bench_case> cases;

    // Assembled once (add/connect are implemented); run() is driven per
    // iteration because it is a move-SINK — it consumes its inputs map, so a
    // fresh map is built each time (that construction + move IS part of the
    // rung-2b cost the ladder attributes).
    cases.push_back(
        {"passthrough_pipeline", /*baseline_eligible=*/true,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_passthrough_stage;

             vc::pipe::vc_pipeline pipe;
             const vc::pipe::stage_name pt =
                 pipe.add(std::make_unique<stage_t>("pt"));

             const vc::vc_image image =
                 vc::vc_image::zeros<vc::buf_f32>(kWidth, kHeight, kChannels);

             // The open input of this one-stage graph is pt.in; builds a fresh
             // sink map for one run().
             const auto make_inputs = [&] {
                 packet_map inputs;
                 inputs.emplace(vc::pipe::stage_port{pt, stage_t::slots::in},
                                vc::pipe::vc_pipe_packet{image});
                 return inputs;
             };

             // Correctness gate (Sec 4.5), on a throwaway run so the timed
             // loop below stays untouched: the open output pt.out must carry
             // the same image through, real width/height/channels intact.
             {
                 auto outputs = pipe.run(make_inputs());
                 const auto out_port =
                     vc::pipe::stage_port{pt, stage_t::slots::out};
                 auto it = outputs.find(out_port);
                 vc::bench::check(
                     it != outputs.end(),
                     "passthrough_pipeline produced no open output");
                 const auto& out_image = it->second.get<vc::vc_image>();
                 vc::bench::check(
                     out_image.width() == kWidth &&
                         out_image.height() == kHeight &&
                         out_image.channels() == kChannels,
                     "passthrough_pipeline output geometry mismatch");
             }

             // A bare stage, off the pipeline graph, driven directly through
             // process() on an equivalent input/context — rung 2a of the
             // ladder (docs/benchmarking.md Sec 6), added here (not just in
             // the sibling vc_benchmark_pipe suite) so it shares ONE Bench
             // with the run() rung below: nanobench's relative(true) then
             // reports run() as a ratio against it directly, in-process,
             // instead of two separate baselined binaries a reader has to
             // relate by hand.
             const stage_t bare_stage("pt_bare");
             const vc::pipe::slot_name in_slot{stage_t::slots::in.name};
             const vc::pipe::slot_name out_slot{stage_t::slots::out.name};
             const auto one_input = [&] {
                 std::unordered_map<vc::pipe::slot_name,
                                    vc::pipe::vc_pipe_packet>
                     inputs;
                 inputs.emplace(in_slot, vc::pipe::vc_pipe_packet{image});
                 return inputs;
             };
             // Reused context for the timed rung 2a loop (mirrors
             // vc_benchmark_pipe.cpp's passthrough case): built once so the
             // timed region is process()'s own cost, not per-iteration map
             // construction.
             vc::pipe::vc_pipe_context process_ctx{one_input()};

             // Correctness gate for the bare-stage rung (Sec 4.5), on its own
             // throwaway context so the reused `process_ctx` above stays
             // untouched.
             {
                 vc::pipe::vc_pipe_context throwaway{one_input()};
                 bare_stage.process(throwaway);
                 const auto bare_outputs = std::move(throwaway).take_outputs();
                 vc::bench::check(bare_outputs.find(out_slot) !=
                                      bare_outputs.end(),
                                  "bare stage.process() produced no output");
             }

             bench.unit("op").batch(1.0).relative(true);
             // A fatter epoch, same remedy the substrate `copy` deep-copy row
             // uses (Sec 4.1's "raise minEpochTime" trap answer): this case
             // rebuilds an unordered_map input sink every iteration, and that
             // per-iteration allocation is malloc-driven, not measured by
             // err% alone — nanobench's within-run err% stays low at the
             // default 20 ms epoch (the noise doesn't show up WITHIN a run),
             // but the ACROSS-run median is reproducibly unstable at that
             // epoch: independent re-runs have observed roughly 7-17% median
             // spread, the exact figure varying with machine load rather than
             // being a fixed property of the case. 200 ms converges the
             // cross-run median to roughly 3% spread in observed runs. Applies
             // to both rungs below (one Bench, one epoch budget); the cheaper
             // rung 2a just gets proportionally more iterations in it.
             bench.minEpochTime(std::chrono::milliseconds(200));

             // Rung 2a — bare stage.process(). Run FIRST so relative(true)
             // marks it as the 100% baseline; rung 2b's row then reports
             // run()'s cost as a ratio against it (the "plumbing overhead vs
             // raw kernel" quantity this rung exists to make tracked and
             // baselined, instead of derived by hand from two binaries).
             bench.run("rung2a stage.process()", [&] {
                 bare_stage.process(process_ctx);
                 ankerl::nanobench::doNotOptimizeAway(&process_ctx);
             });

             // Rung 2b — the same carry through a full vc_pipeline::run().
             bench.run("passthrough pipeline.run()", [&] {
                 auto outputs = pipe.run(make_inputs());
                 ankerl::nanobench::doNotOptimizeAway(outputs.size());
             });
         }});

    return cases;
}

} // namespace

int main(int argc, char** argv) {
    return vc::bench::run_suite(argc, argv, "pipeline", macro_cases());
}
