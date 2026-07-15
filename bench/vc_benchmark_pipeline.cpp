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
// REALITY GATE (Sec 9): vc_pipeline::run() is a stub today — its body is
// `return {}` (it walks no stages, moves no packets, calls no process()). So
// these cases currently time an empty-map construction + early return, NOT the
// plumbing the ladder describes. They are fully wired against the real add() /
// connect() / run() API and will become real rung-2b numbers untouched once you
// implement run()/validate() — but are marked NOT baseline-eligible until then,
// so an empty return never enters the committed timeseries.
//
// Run:  vc_benchmark_pipeline
//       vc_benchmark_pipeline --list

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nanobench.h"
#include "vc_bench_support.h"

#include "vc/pipe/stages/vc_passthrough_stage.h"
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

    // ---- single-stage representative pipeline through run() (rung 2b) ----
    // Assembled once (add/connect are implemented); run() is driven per
    // iteration because it is a move-SINK — it consumes its inputs map, so a
    // fresh map is built each time (that construction + move IS part of the
    // rung-2b cost the ladder attributes).
    cases.push_back(
        {"passthrough_pipeline", /*baseline_eligible=*/false,
         [](ankerl::nanobench::Bench& bench) {
             using stage_t = vc::pipe::vc_passthrough_stage;

             vc::pipe::vc_pipeline pipe;
             const vc::pipe::stage_name pt =
                 pipe.add(std::make_unique<stage_t>("pt"));

             const vc::vc_image image(kWidth, kHeight, kChannels);

             // The open input of this one-stage graph is pt.in; builds a fresh
             // sink map for one run().
             const auto make_inputs = [&] {
                 packet_map inputs;
                 inputs.emplace(vc::pipe::stage_port{pt, stage_t::slots::in},
                                vc::pipe::vc_pipe_packet{image});
                 return inputs;
             };

             // No correctness gate: run() returns {} today (Sec 9). When you
             // implement run(), assert here that the open output pt.out carries
             // the image through, then flip baseline_eligible to true.

             bench.unit("op").batch(1.0).relative(false);
             bench.run("passthrough pipeline.run() [STUB]", [&] {
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
