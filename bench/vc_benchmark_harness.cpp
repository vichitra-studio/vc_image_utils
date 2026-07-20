// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Tier-1 SUBSTRATE benchmarks — the data layer that sits UNDER every stage
// (docs/benchmarking.md Sec 5.1). A cost here is a hidden tax on everything and
// is invisible in a per-stage number, which is why this tier is benchmarked
// first. Unlike the micro/macro suites, every case here measures REAL,
// fully-implemented code today (vc_pixel_buffer, vc_pipe_packet) — so all are
// baseline-eligible.
//
// Run:  vc_benchmark_harness                # all cases
//       vc_benchmark_harness copy --baseline
//       vc_benchmark_harness --list

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "nanobench.h"
#include "vc_bench_support.h"

#include "vc/pipe/vc_pipe_packet.h"
#include "vc/vc_image.h"
#include "vc/vc_pixel_buffer.h"

namespace {

// A ~2 MP RGB frame's worth of elements — realistic for the routine set; a
// size SWEEP (cache cliffs, O(n) linearity) is an investigation sweep, not a
// routine number (docs/benchmarking.md Sec 5.2, Sec 7).
constexpr std::size_t kWidth = 1600;
constexpr std::size_t kHeight = 1200;
constexpr std::size_t kChannels = 3;
constexpr std::size_t kCount = kWidth * kHeight * kChannels;

std::vector<vc::bench::bench_case> substrate_cases() {
    std::vector<vc::bench::bench_case> cases;

    // --- allocate + fill + consume a frame, priced by element size ---
    // Prices the HDR streaming lifecycle (allocate -> fill -> use -> discard,
    // Sec 5.1), NOT an isolated memset — which is deliberately unmeasurable
    // here: clang's heap-allocation elision deletes a fill whose buffer is only
    // escaped by pointer and never read (even a non-zero fill + a "memory"
    // clobber does not stop it for a malloc'd-then-freed buffer — verified). So
    // the case allocates, fills, and SUMS every element (escaping the sum) — a
    // full produce-then-consume pass that the optimizer cannot elide because
    // the values genuinely flow to an observed result. Expect u8 (1 B/px)
    // FASTER per pixel than f32 (4 B/px): less memory traffic.
    cases.push_back(
        {"alloc_fill", true, [](ankerl::nanobench::Bench& bench) {
             constexpr vc::buf_f32 fill_f32{0.5f};
             constexpr vc::buf_u8 fill_u8{7};

             // Sanity before timing (Sec 4.5): the fill+sum really touches every
             // element (a wrong sum would mean the fill was elided).
             {
                 vc::vc_pixel_buffer f(kCount, fill_f32);
                 double s = 0.0;
                 for (float v : f.as<vc::buf_f32>()) {
                     s += static_cast<double>(v);
                 }
                 vc::bench::check(s == static_cast<double>(kCount) *
                                           static_cast<double>(fill_f32),
                                  "alloc+fill f32 sum wrong (fill elided?)");

                 vc::vc_pixel_buffer u(kCount, fill_u8);
                 std::uint64_t us = 0;
                 for (std::uint8_t v : u.as<vc::buf_u8>()) {
                     us += v;
                 }
                 vc::bench::check(us == static_cast<std::uint64_t>(kCount) * fill_u8,
                                  "alloc+fill u8 sum wrong (fill elided?)");
             }

             bench.unit("pixel").batch(static_cast<double>(kCount)).relative(true);
             bench.run("alloc+fill+sum f32", [] {
                 vc::vc_pixel_buffer buf(kCount, fill_f32);
                 double sum = 0.0;
                 for (float v : buf.as<vc::buf_f32>()) {
                     sum += static_cast<double>(v);
                 }
                 ankerl::nanobench::doNotOptimizeAway(sum);
             });
             bench.run("alloc+fill+sum u8", [] {
                 vc::vc_pixel_buffer buf(kCount, fill_u8);
                 std::uint64_t sum = 0;
                 for (std::uint8_t v : buf.as<vc::buf_u8>()) {
                     sum += v;
                 }
                 ankerl::nanobench::doNotOptimizeAway(sum);
             });
         }});

    // --- shallow copy vs deep copy ---
    // PROVES the shared-handle assumption: a vc_image / shared_ptr copy is a
    // refcount bump, while copying the vc_pixel_buffer VALUE deep-copies the
    // backing vector. The ratio prices the deep-copy cases we cannot avoid
    // (Sec 5.1). unit is per-op, not per-pixel.
    cases.push_back(
        {"copy", true, [](ankerl::nanobench::Bench& bench) {
             auto shared = std::make_shared<vc::vc_pixel_buffer>(
                 kCount, vc::buf_f32{0.25f});
             vc::vc_pixel_buffer value(kCount, vc::buf_f32{0.25f});

             // Sanity: the shared handle really shares (use_count climbs on
             // copy), and the deep copy really is independent storage.
             {
                 auto probe = shared; // NOLINT: intentional refcount bump
                 vc::bench::check(shared.use_count() == 2,
                                  "shared_ptr copy did not bump use_count");
                 vc::vc_pixel_buffer deep = value;
                 vc::bench::check(deep.size() == value.size(),
                                  "deep copy lost elements");
             }

             bench.unit("op").batch(1.0).relative(true);
             // The deep-copy row is a ~23 MB memcpy (~340 µs/op). At the house
             // 20 ms epoch (configure()) that is only ~60 iters, and nanobench
             // flags it "unstable" (~5% err%, :wavy_dash:) on some runs — so a
             // --baseline invocation could COMMIT a flagged-unstable number. A
             // memory-bound copy is inherently noisier than a cache-resident op,
             // so give THIS case a fatter epoch (nanobench's own remedy) to
             // settle the committed number. The fast shallow row is unaffected —
             // it just runs proportionally more iterations in the same budget.
             bench.minEpochTime(std::chrono::milliseconds(200));
             bench.run("shallow copy (shared_ptr refcount)", [&] {
                 auto handle = shared; // refcount bump only
                 ankerl::nanobench::doNotOptimizeAway(handle);
             });
             bench.run("deep copy (buffer value)", [&] {
                 vc::vc_pixel_buffer deep = value; // full backing-vector copy
                 // Escape an ELEMENT, not deep.size(): size() reads the inline
                 // size member, leaving the copied heap bytes unobserved and so
                 // DCE-eligible (the same trap as alloc_fill). Touching an
                 // element forces the memcpy to be materialized. Does not
                 // manifest on AppleClang 17 today, but is fragile under -flto.
                 ankerl::nanobench::doNotOptimizeAway(deep.as<vc::buf_f32>()[0]);
             });
         }});

    // --- as<T>() access: hoisted once vs per-element ---
    // as<T>() runs a holds_alternative check every call. Hoisting it once and
    // iterating the returned span vs re-fetching per element quantifies that
    // check — the result BECOMES a coding guideline (Sec 5.1).
    cases.push_back(
        {"as_access", true, [](ankerl::nanobench::Bench& bench) {
             vc::vc_pixel_buffer buf(kCount, vc::buf_f32{1.0f});

             // Sanity: both regimes must compute the same sum before we time.
             double hoisted_sum = 0.0;
             {
                 const std::span<const vc::buf_f32> s = buf.as<vc::buf_f32>();
                 for (float v : s) {
                     hoisted_sum += static_cast<double>(v);
                 }
                 vc::bench::check(hoisted_sum == static_cast<double>(kCount),
                                  "as<T> hoisted sum wrong");
             }

             bench.unit("pixel").batch(static_cast<double>(kCount)).relative(true);
             bench.run("as<T> hoisted once", [&] {
                 const std::span<const vc::buf_f32> s = buf.as<vc::buf_f32>();
                 double sum = 0.0;
                 for (std::size_t i = 0; i < s.size(); ++i) {
                     sum += static_cast<double>(s[i]);
                 }
                 ankerl::nanobench::doNotOptimizeAway(sum);
             });
             bench.run("as<T> per element", [&] {
                 double sum = 0.0;
                 for (std::size_t i = 0; i < kCount; ++i) {
                     const std::span<const vc::buf_f32> s = buf.as<vc::buf_f32>();
                     // Force each as<T>() call to actually happen: buf is
                     // loop-invariant, so without this barrier the compiler
                     // hoists the holds_alternative check out of the loop and
                     // this run collapses into the hoisted one (measuring
                     // nothing). Guarding the per-iteration span defeats that.
                     ankerl::nanobench::doNotOptimizeAway(s.data());
                     sum += static_cast<double>(s[i]);
                 }
                 ankerl::nanobench::doNotOptimizeAway(sum);
             });
         }});

    // --- std::any packet box/unbox: image handle vs scalar ---
    // A vc_pipe_packet wraps its payload in std::any. A ~32-byte vc_image handle
    // exceeds the small-buffer optimization of common std libs, so a heap
    // allocation per packet is EXPECTED; a double fits inline. The ratio
    // confirms that threshold cost (Sec 5.1). The dominant HEAP-ALLOCATION cost
    // is independent of the stubbed vc_image_info::element_count() (still a
    // TODO(you) rep, so zeros() below allocates a 0-element buffer) — boxing
    // copies the ~32-byte handle either way — but the shared_ptr refcount atomic
    // inside that copy is null/free today and lands (a small addition, not the
    // dominant cost) once the rep is implemented and the buffer is non-empty.
    // See Sec 9.
    cases.push_back(
        {"packet", true, [](ankerl::nanobench::Bench& bench) {
             const vc::vc_image image =
                 vc::vc_image::zeros(kWidth, kHeight, kChannels);

             // Sanity: a boxed value round-trips back to the same type.
             {
                 vc::pipe::vc_pipe_packet p{image};
                 vc::bench::check(p.has_value(),
                                  "packet did not store the image");
                 (void)p.get<vc::vc_image>(); // throws if the type is wrong
                 vc::pipe::vc_pipe_packet d{3.14};
                 vc::bench::check(d.get<double>() == 3.14,
                                  "packet scalar round-trip wrong");
             }

             bench.unit("op").batch(1.0).relative(true);
             bench.run("box+unbox vc_image handle", [&] {
                 vc::pipe::vc_pipe_packet p{image};
                 ankerl::nanobench::doNotOptimizeAway(p.get<vc::vc_image>().width());
             });
             bench.run("box+unbox double scalar", [] {
                 vc::pipe::vc_pipe_packet p{2.71828};
                 ankerl::nanobench::doNotOptimizeAway(p.get<double>());
             });
         }});

    return cases;
}

} // namespace

int main(int argc, char** argv) {
    return vc::bench::run_suite(argc, argv, "harness", substrate_cases());
}
