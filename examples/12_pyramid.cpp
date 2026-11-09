// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 12_pyramid -- acceptance checks for the Gaussian and Laplacian pyramids.
//
// P2 section C's S5.5: Gaussian (REDUCE = separable blur + subsample) plus
// Laplacian (L_l = G_l - EXPAND(G_{l+1})) with collapse under 1e-4 MAE. The
// aliasing half of S5.5 is already satisfied by 10_defect_atlas entries 01/02,
// so this file owes only the pyramid.
//
// Reasoning, the hand-worked 1-D case and the measured band table are in
// curriculum/notes/pyramids.txt.
//
// ---- WHAT EACH GROUP CAN AND CANNOT CATCH ----
//
// 1-2   max_pyramid_levels, pure arithmetic. Includes 1x1 (the floor) and a
//       non-square odd case, where the count is driven by the LARGER side.
//
// 3-4   reduce: ceil geometry (a 5-wide row gives 3, from indices 0,2,4) and
//       that a flat image stays flat. The flat check catches a kernel that does
//       not sum to 1 -- invisible against noise.
//
// 5-7   expand. THE MOST IMPORTANT GROUP, because collapse cannot police it:
//       the same expand() runs on both sides of a = (a-b)+b, so a systematically
//       wrong expand still collapses perfectly. Only a direct assertion sees it.
//       6 is GAIN (a flat image, border included). 6c is SHAPE -- values worked
//       by hand from the two per-parity stencils, because a flat image is blind
//       to swapping them, both weighing 8. 6d pins `zero`'s fading border.
//
// 8     the bands are signed. Catches storing them unsigned or clamping to 0,
//       which would also pass collapse on a smooth image and fail on an edge.
//
// 9-11  COLLAPSE, the graded criterion. Noise, structure, and odd/non-square.
//
// 12    levels == 1, where there are no bands at all and top IS the source.
//
// 13    geometry agreement between the two pyramids, level by level.
//
// 14    the rejections -- each PAIRED with an acceptance, because a function
//       that refused everything would satisfy a rejection check on its own.
//
// Tolerances are relative to signal RMS per section F.3, except collapse, which
// section C states as an absolute MAE of 1e-4 on 0..1 pixel data.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/pixelops/vc_pyramid.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

float rms(const vc::vc_image& img) {
    double acc = 0.0;
    std::size_t n = 0;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const vc::buf_f32 v : px) {
            acc += static_cast<double>(v) * static_cast<double>(v);
        }
        n = px.size();
    });
    return n == 0 ? 0.0F
                  : static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

// Infinity on a geometry mismatch rather than indexing off the shorter buffer.
// A wrong output size is a failure, not undefined behaviour -- the same guard
// 06_dft's max_abs_diff grew after exactly that bug.
float max_abs_diff(const vc::vc_image& a, const vc::vc_image& b) {
    if (a.width() != b.width() || a.height() != b.height() ||
        a.channels() != b.channels()) {
        return std::numeric_limits<float>::infinity();
    }
    float worst = 0.0F;
    a.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> pa) {
        b.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> pb) {
            for (std::size_t i = 0; i < pa.size(); ++i) {
                worst = std::max(worst, std::fabs(pa[i] - pb[i]));
            }
        });
    });
    return worst;
}

// Mean absolute error -- what section C states the collapse bound against.
// Infinity on a geometry mismatch, same reason as above.
float mae(const vc::vc_image& a, const vc::vc_image& b) {
    if (a.width() != b.width() || a.height() != b.height() ||
        a.channels() != b.channels()) {
        return std::numeric_limits<float>::infinity();
    }
    double acc = 0.0;
    std::size_t n = 0;
    a.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> pa) {
        b.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> pb) {
            for (std::size_t i = 0; i < pa.size(); ++i) {
                acc += std::fabs(static_cast<double>(pa[i]) -
                                 static_cast<double>(pb[i]));
            }
            n = pa.size();
        });
    });
    return n == 0 ? 0.0F : static_cast<float>(acc / static_cast<double>(n));
}

float rel_diff(const vc::vc_image& got, const vc::vc_image& want) {
    return max_abs_diff(got, want) / std::max(rms(want), 1e-12F);
}

// MAE relative to the reference's RMS. Section C states the collapse bound as
// an ABSOLUTE 1e-4, which is only meaningful for 0..1 data -- float error
// scales with magnitude, so the same correct code measures 1.14e-04 on
// 0..65535 input and would fail its own criterion. Every absolute check below
// uses 0..1 data; this is the form that holds at any scale.
float rel_mae(const vc::vc_image& got, const vc::vc_image& want) {
    return mae(got, want) / std::max(rms(want), 1e-12F);
}

vc::vc_image make_image(vc::image_dim w,
                        vc::image_dim h,
                        vc::channel_count ch,
                        const std::vector<float>& values) {
    vc::vc_image_writer out{w, h, ch, vc::buf_f32{0.0F}};
    std::size_t i = 0;
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            for (vc::channel_count c = 0; c < ch; ++c) {
                out.at<vc::buf_f32>(x, y, c) = values[i++];
            }
        }
    }
    return std::move(out).seal();
}

// A fixed sequence rather than <random>, so a failure reproduces from the
// source alone. Values land in 0..1, which is what the collapse MAE bound
// assumes.
vc::vc_image noise_image(vc::image_dim w,
                         vc::image_dim h,
                         vc::channel_count ch,
                         std::uint32_t seed,
                         float amplitude = 1.0F) {
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(w) * h * ch);
    std::uint32_t s = seed;
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h * ch; ++i) {
        s = s * 1664525U + 1013904223U;
        v.push_back(amplitude * static_cast<float>(s >> 8U) /
                    static_cast<float>(1U << 24U));
    }
    return make_image(w, h, ch, v);
}

// Something with real structure at several scales -- a broad gradient, a
// mid-scale ramp and a fine checkerboard. Noise alone is almost all top-octave,
// so it barely exercises the deeper levels.
vc::vc_image structured_image(vc::image_dim w, vc::image_dim h) {
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(w) * h);
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            const float broad = static_cast<float>(x) / static_cast<float>(w);
            const float mid = 0.25F * static_cast<float>((x / 8 + y / 8) % 2);
            const float fine = 0.1F * static_cast<float>((x + y) % 2);
            v.push_back(std::clamp(0.35F * broad + mid + fine, 0.0F, 1.0F));
        }
    }
    return make_image(w, h, 1, v);
}

bool rejects(const std::function<void()>& f) {
    try {
        f();
    } catch (const vc::vc_exception& e) {
        return e.code() == vc::vc_error_code::invalid_argument;
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

bool accepts(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// Does any sample fall below zero? The band levels must be able to.
bool has_negative(const vc::vc_image& img) {
    bool found = false;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const vc::buf_f32 v : px) {
            if (v < 0.0F) {
                found = true;
            }
        }
    });
    return found;
}

} // namespace

int main() {
    using vc::pixelops::collapse;
    using vc::pixelops::expand;
    using vc::pixelops::gaussian_pyramid;
    using vc::pixelops::laplacian_pyramid;
    using vc::pixelops::max_pyramid_levels;
    using vc::pixelops::reduce;
    using vc::pixelops::vc_edge_policy;
    using vc::pixelops::vc_laplacian_pyramid;
    using vc::pixelops::vc_pyramid;

    // clamp throughout: it preserves a flat image, which `zero` does not --
    // vc_edge_policy.h records a 3x3 box reading 44.4 from a flat 100 at a
    // corner, and under a pyramid that deficit would recur at every level.
    constexpr vc_edge_policy k_policy = vc_edge_policy::clamp;

    int passed = 0;
    int total = 0;
    auto expect = [&](bool ok, const char* what) {
        ++total;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // ---- 1. max_pyramid_levels, the ordinary cases --------------------
        //
        // Halving until BOTH sides reach 1, so the count is driven by the
        // LARGER side: 1 + ceil(log2(max(w,h))).
        {
            expect(max_pyramid_levels(8, 8) == 4,
                   "8x8 supports 4 levels (8,4,2,1)");
            expect(max_pyramid_levels(256, 256) == 9,
                   "256x256 supports 9 levels");
            expect(max_pyramid_levels(1, 1) == 1,
                   "1x1 supports exactly 1 level, never 0");
        }

        // ---- 2. max_pyramid_levels on non-square and odd ------------------
        //
        // 5x3 reduces 5x3 -> 3x2 -> 2x1 -> 1x1, so FOUR levels, decided by the
        // 5 and not the 3. An implementation using min() would say 2.
        {
            expect(max_pyramid_levels(5, 3) == 4,
                   "5x3 supports 4 levels -- driven by the LARGER side");
            expect(max_pyramid_levels(3, 5) == 4,
                   "3x5 agrees with 5x3, so the axes are not swapped");
            expect(max_pyramid_levels(6, 6) == 4,
                   "6x6 supports 4 levels (6,3,2,1) -- ceil, not floor");
        }

        // ---- 3. reduce: geometry is CEIL, not floor -----------------------
        //
        // Keeping indices 0,2,4 of a 5-wide row leaves 3. An implementation
        // using w/2 would give 2 and silently drop the last column.
        {
            const vc::vc_image even = noise_image(8, 8, 1, 3U);
            const vc::vc_image r_even = reduce(even, k_policy);
            expect(r_even.width() == 4 && r_even.height() == 4,
                   "reduce 8x8 -> 4x4");

            const vc::vc_image odd = noise_image(5, 3, 1, 7U);
            const vc::vc_image r_odd = reduce(odd, k_policy);
            expect(r_odd.width() == 3 && r_odd.height() == 2,
                   "reduce 5x3 -> 3x2 (CEIL: indices 0,2,4)");

            const vc::vc_image one = noise_image(1, 1, 1, 11U);
            expect(reduce(one, k_policy).width() == 1 &&
                       reduce(one, k_policy).height() == 1,
                   "reduce 1x1 -> 1x1, not 0x0");

            expect(r_even.channels() == 1 &&
                       reduce(noise_image(8, 8, 3, 13U), k_policy).channels() ==
                           3,
                   "reduce preserves the channel count");
        }

        // ---- 4. reduce preserves a FLAT image -----------------------------
        //
        // Every tap reads the same value and a Gaussian sums to 1, so the
        // output is that value. Catches a kernel that is not normalised --
        // invisible against noise, obvious here.
        {
            const vc::vc_image flat =
                make_image(16, 16, 1, std::vector<float>(256, 0.42F));
            const vc::vc_image r = reduce(flat, k_policy);
            expect(rel_diff(
                       r, make_image(8, 8, 1, std::vector<float>(64, 0.42F))) <=
                       1e-5F,
                   "reduce leaves a flat image flat (clamp)");
        }

        // ---- 5. expand: the target size is GIVEN, and constrained ---------
        //
        // A 3-wide level could have come from 5 or 6, so both are legal
        // targets and nothing else is. expand() cannot infer this by doubling,
        // which is why it takes the target.
        {
            const vc::vc_image three = noise_image(3, 2, 1, 17U);

            expect(accepts([&] { (void)expand(three, 6, 4, k_policy); }),
                   "expand 3x2 -> 6x4 is accepted (exactly doubled)");
            expect(accepts([&] { (void)expand(three, 5, 3, k_policy); }),
                   "expand 3x2 -> 5x3 is accepted (doubled minus one)");
            expect(rejects([&] { (void)expand(three, 7, 4, k_policy); }),
                   "expand 3x2 -> 7x4 is REJECTED (unreachable)");
            expect(rejects([&] { (void)expand(three, 3, 2, k_policy); }),
                   "expand 3x2 -> 3x2 is REJECTED (not an upsample)");

            const vc::vc_image e = expand(three, 5, 3, k_policy);
            expect(e.width() == 5 && e.height() == 3,
                   "expand returns exactly the requested geometry");
        }

        // ---- 6. expand's GAIN. Collapse cannot catch any of this ----------
        //
        // 1e-5 over the WHOLE image, border included. The per-parity stencils
        // weigh 8 at every position and read only real samples, so a flat image
        // is exact under clamp, reflect and wrap alike -- there is no scale
        // factor and no hole pattern for a border to get the wrong parity of.
        //
        // The two formulations this replaced are recorded in
        // curriculum/notes/pyramids.txt section 9 (and compared in 9a): a
        // constant x4 gives 0.125 .. 1.125 at the border under clamp for ANY
        // kernel, plus a +/-4.64% checkerboard for anything but Burt's taps,
        // and mask normalisation fixes both at the cost of a second blur and a
        // divide.
        //
        // This group sees a wrong GAIN. It cannot see a wrong SHAPE -- group 6c
        // is for that. Collapse sees neither: the same expand runs on both sides
        // of a = (a-b)+b, so a systematically wrong one reconstructs perfectly.
        {
            const vc::vc_image flat =
                make_image(4, 4, 1, std::vector<float>(16, 0.6F));
            const vc::vc_image e = expand(flat, 8, 8, k_policy);
            expect(
                rel_diff(e, make_image(8, 8, 1,
                                       std::vector<float>(64, 0.6F))) <= 1e-5F,
                "expand keeps a flat image flat EVERYWHERE, border included");

            // Non-square and odd, where the border parity differs per axis --
            // a 5-wide target has its last column at an even index and a
            // 6-wide target at an odd one, so they fail differently.
            const vc::vc_image small =
                make_image(3, 2, 1, std::vector<float>(6, 0.25F));
            expect(rel_diff(expand(small, 5, 3, k_policy),
                            make_image(5, 3, 1,
                                       std::vector<float>(15, 0.25F))) <= 1e-5F,
                   "expand 3x2 -> 5x3 is flat: odd target, both axes");
            expect(rel_diff(expand(small, 6, 4, k_policy),
                            make_image(6, 4, 1,
                                       std::vector<float>(24, 0.25F))) <= 1e-5F,
                   "expand 3x2 -> 6x4 is flat: even target, both axes");
        }

        // ---- 6b. and the reason it matters: EDITING a band -----------------
        //
        // Collapse is immune to a rippling expand -- the ripple is subtracted
        // into L and added straight back, so a = (a-b)+b holds regardless.
        // Measured MAE 0.000e+00 for a rippling expand AND a clean one.
        //
        // It bites the moment a band is MODIFIED and then collapsed, which is
        // exactly what exposure fusion and local contrast do. On a FLAT image
        // any surviving texture is pure artifact, and with a constant-x4 expand
        // the artifact measured 9.28% x |gain-1| in the interior -- 27.8% at a
        // gain of 4. With the per-parity stencils it is zero at every gain,
        // because every band of a flat image is itself exactly zero.
        //
        // So this check is the one that justifies the specification, rather
        // than just asserting it.
        {
            const vc::vc_image flat =
                make_image(32, 32, 1, std::vector<float>(1024, 0.5F));
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(flat, 3, k_policy);

            // Every band of a flat image must be ZERO -- there is no detail at
            // any scale. A rippling expand puts its ripple here instead.
            expect(rms(lap.bands.front()) <= 1e-5F,
                   "every band of a FLAT image is zero, so nothing to amplify");

            // Boost the finest band 4x and collapse. Still flat.
            vc_laplacian_pyramid boosted = lap;
            vc::vc_image_writer scaled{32, 32, 1, vc::buf_f32{0.0F}};
            lap.bands.front().with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> px) {
                    for (vc::image_dim y = 0; y < 32; ++y) {
                        for (vc::image_dim x = 0; x < 32; ++x) {
                            scaled.at<vc::buf_f32>(x, y, 0) =
                                4.0F * px[static_cast<std::size_t>(y) * 32 + x];
                        }
                    }
                });
            boosted.bands.front() = std::move(scaled).seal();
            expect(rel_diff(collapse(boosted, k_policy), flat) <= 1e-5F,
                   "boosting a band 4x on a flat image stays flat (no ripple)");
        }

        // ---- 6c. PINNED VALUES. The only group that sees a stencil swap ----
        //
        // Groups 5, 6, 6b and every collapse check below are ALL blind to a
        // whole family of wrong expands. A flat image stays exactly flat if the
        // two per-parity stencils are SWAPPED (both weigh 8), if j = i/2 is off
        // by one, or if a pass reads the wrong axis length (anything square
        // hides that). Collapse is blind to all three, for the usual reason.
        //
        // So these are worked by hand from the stencils themselves:
        //
        //     i EVEN -> (c[j-1] + 6*c[j] + c[j+1])/8        j = i/2
        //     i ODD  -> (c[j]            + c[j+1])/2
        //
        // Coarse row [10, 20, 30]. The only out-of-range reads are c[-1] and
        // c[3], and THE POLICY SUPPLIES THEM FROM THE COARSE GRID -- so the same
        // data gives different ends under clamp and reflect, while the middle
        // three values, which touch no policy at all, are identical.
        //
        //   target 5 (= 2n-1; the last index is EVEN, so an even stencil)
        //     reflect   c[-1] = c[1] = 20,  c[3] = c[1] = 20
        //       0  (20 + 6*10 + 20)/8 = 100/8 = 12.5
        //       1  (10 + 20)/2        =         15
        //       2  (10 + 6*20 + 30)/8 = 160/8 = 20
        //       3  (20 + 30)/2        =         25
        //       4  (20 + 6*30 + 20)/8 = 220/8 = 27.5
        //     clamp     c[-1] = c[0] = 10,  c[3] = c[2] = 30
        //       0  (10 + 6*10 + 20)/8 =  90/8 = 11.25
        //       4  (20 + 6*30 + 30)/8 = 230/8 = 28.75
        //
        //   target 6 (= 2n; the last index is ODD, so an odd stencil)
        //       5  clamp    (c[2] + c[3])/2 = (30 + 30)/2 = 30
        //       5  reflect  (c[2] + c[1])/2 = (30 + 20)/2 = 25   <- it turns back
        //
        // Every one of these is an exact binary fraction, so the tolerance is
        // slack, not load-bearing.
        {
            const vc::vc_image row = make_image(3, 1, 1, {10.0F, 20.0F, 30.0F});

            expect(max_abs_diff(expand(row, 5, 1, vc_edge_policy::reflect),
                                make_image(5, 1, 1,
                                           {12.5F, 15.0F, 20.0F, 25.0F,
                                            27.5F})) <= 1e-4F,
                   "expand 3x1 -> 5x1 reflect is 12.5 15 20 25 27.5, by hand");

            expect(max_abs_diff(expand(row, 5, 1, vc_edge_policy::clamp),
                                make_image(5, 1, 1,
                                           {11.25F, 15.0F, 20.0F, 25.0F,
                                            28.75F})) <= 1e-4F,
                   "expand 3x1 -> 5x1 clamp moves ONLY the two end pixels");

            expect(max_abs_diff(expand(row, 6, 1, vc_edge_policy::clamp),
                                make_image(6, 1, 1,
                                           {11.25F, 15.0F, 20.0F, 25.0F, 28.75F,
                                            30.0F})) <= 1e-4F,
                   "expand 3x1 -> 6x1 clamp: the extra ODD output is 30");

            expect(max_abs_diff(expand(row, 6, 1, vc_edge_policy::reflect),
                                make_image(6, 1, 1,
                                           {12.5F, 15.0F, 20.0F, 25.0F, 27.5F,
                                            25.0F})) <= 1e-4F,
                   "expand 3x1 -> 6x1 reflect: the extra ODD output turns BACK "
                   "to 25");

            // THE SAME TABLE DOWN Y. A 3x1 IS the x-axis table and a 1x3 IS the
            // y-axis one -- the idiom vc_edge_policy.h already uses for exactly
            // this, because the two passes share expand_tap() but NOT their
            // index arithmetic, and nothing square can tell them apart.
            //
            // It also drives a 1-wide coarse axis through reflect, where the
            // mirror period 2*(n-1) is zero and resolve_index has a special
            // case. The even stencil there reads (c0 + 6*c0 + c0)/8 = c0.
            const vc::vc_image col = make_image(1, 3, 1, {10.0F, 20.0F, 30.0F});
            expect(
                max_abs_diff(
                    expand(col, 1, 5, vc_edge_policy::reflect),
                    make_image(1, 5, 1, {12.5F, 15.0F, 20.0F, 25.0F, 27.5F})) <=
                    1e-4F,
                "expand 1x3 -> 1x5 reflect gives the SAME five values down Y");

            // INTERIOR RAMP, a stencil-swap detector that touches no policy.
            // On a linear ramp of step d the even stencil gives
            // (c-d + 6c + c+d)/8 = c and the odd one the midpoint, so the
            // interior must be the same ramp at half the step. Swap the
            // branches and index 2 reads 25 where 20 belongs.
            const vc::vc_image ramp =
                make_image(5, 1, 1, {10.0F, 20.0F, 30.0F, 40.0F, 50.0F});
            const vc::vc_image e_ramp = expand(ramp, 10, 1, k_policy);
            bool interior_ramp = true;
            e_ramp.with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> p) {
                    for (std::size_t i = 2; i <= 7; ++i) {
                        const float want = 10.0F + 5.0F * static_cast<float>(i);
                        interior_ramp =
                            interior_ramp && std::fabs(p[i] - want) <= 1e-4F;
                    }
                });
            expect(
                interior_ramp,
                "expand reproduces a ramp exactly inside: 20 25 30 35 40 45");

            // AXIS-LENGTH SWAP, both directions. Content varying only along X
            // must give identical ROWS, because the Y pass on a constant column
            // is exact: (1+6+1)/8 = 1 and (4+4)/8 = 1. Non-square and odd on
            // both axes, so passing height where width belongs cannot survive.
            const vc::vc_image bars =
                make_image(3, 2, 1, {10.0F, 20.0F, 30.0F, 10.0F, 20.0F, 30.0F});
            expect(max_abs_diff(
                       expand(bars, 5, 3, vc_edge_policy::reflect),
                       make_image(5, 3, 1,
                                  {12.5F, 15.0F, 20.0F, 25.0F, 27.5F, 12.5F,
                                   15.0F, 20.0F, 25.0F, 27.5F, 12.5F, 15.0F,
                                   20.0F, 25.0F, 27.5F})) <= 1e-4F,
                   "expand 3x2 -> 5x3: X-only content gives three equal rows");

            const vc::vc_image stripes =
                make_image(2, 3, 1, {10.0F, 10.0F, 20.0F, 20.0F, 30.0F, 30.0F});
            expect(max_abs_diff(
                       expand(stripes, 3, 5, vc_edge_policy::reflect),
                       make_image(3, 5, 1,
                                  {12.5F, 12.5F, 12.5F, 15.0F, 15.0F, 15.0F,
                                   20.0F, 20.0F, 20.0F, 25.0F, 25.0F, 25.0F,
                                   27.5F, 27.5F, 27.5F})) <= 1e-4F,
                   "expand 2x3 -> 3x5: Y-only content gives five equal rows");
        }

        // ---- 6d. `zero` NO LONGER PRESERVES A FLAT IMAGE, deliberately -----
        //
        // A behaviour change, pinned so it stays a decision. `zero` now darkens
        // expand()'s border the way it darkens every other neighbourhood
        // operation's -- vc_edge_policy.h's documented "a flat 100 reads back as
        // 44.4". The mask formulation this replaced CANCELLED `zero` away:
        // numerator and denominator lost exactly the same taps, so the quotient
        // came back flat, and expand was the one operation in the library where
        // a documented fading border silently did not happen.
        //
        // 3x3 -> 5x5, so both ends of both axes are EVEN outputs:
        //
        //     per axis   (0 + 6*1 + 1)/8 = 7/8
        //     corner     7/8 x 7/8 = 49/64 = 0.765625        exactly
        //     centre     untouched by any policy             exactly 1
        {
            const vc::vc_image flat3 =
                make_image(3, 3, 1, std::vector<float>(9, 1.0F));
            const vc::vc_image ez = expand(flat3, 5, 5, vc_edge_policy::zero);
            expect(std::fabs(ez.at<vc::buf_f32>(0, 0, 0) - 0.765625F) <= 1e-5F,
                   "expand under `zero` reads 49/64 at a corner, not 1");
            expect(std::fabs(ez.at<vc::buf_f32>(2, 2, 0) - 1.0F) <= 1e-5F,
                   "and is still exactly 1 in the interior under `zero`");
            // THE EVEN TARGET IS A DIFFERENT CASE and must be checked too:
            // 3x3 -> 6x6 ends on an ODD output, so the last value is
            // (c[2] + c[3])/2 and only the policy decides c[3]. Under `zero`
            // that is (1 + 0)/2 = 0.5 per axis, so 0.25 at the corner -- a
            // different number from the odd target's 49/64, from the same rule.
            const vc::vc_image ez6 = expand(flat3, 6, 6, vc_edge_policy::zero);
            expect(std::fabs(ez6.at<vc::buf_f32>(5, 5, 0) - 0.25F) <= 1e-5F,
                   "expand under `zero` reads 1/4 at an EVEN target's corner");

            // clamp, reflect and wrap all stay exact everywhere, at BOTH target
            // parities, because the stencils read REAL SAMPLES whatever index
            // the policy returns. c[3] resolves to c[2], c[1] and c[0]
            // respectively, and all three hold 1.
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::reflect,
                  vc_edge_policy::wrap}) {
                expect(
                    rel_diff(expand(flat3, 5, 5, p),
                             make_image(5, 5, 1,
                                        std::vector<float>(25, 1.0F))) <= 1e-5F,
                    "expand keeps flat flat, ODD target, clamp/reflect/wrap");
                expect(
                    rel_diff(expand(flat3, 6, 6, p),
                             make_image(6, 6, 1,
                                        std::vector<float>(36, 1.0F))) <= 1e-5F,
                    "expand keeps flat flat, EVEN target, clamp/reflect/wrap");
            }
        }

        // ---- 7. expand then reduce returns the geometry, not the image -----
        //
        // Geometry only. The round trip is LOSSY by design -- what it destroys
        // is exactly what a Laplacian band stores -- so asserting the pixels
        // would be asserting that the pyramid does nothing.
        {
            const vc::vc_image src = noise_image(8, 6, 1, 19U);
            const vc::vc_image round =
                reduce(expand(reduce(src, k_policy), 8, 6, k_policy), k_policy);
            expect(round.width() == 4 && round.height() == 3,
                   "reduce(expand(reduce(x))) returns to the reduced geometry");
        }

        // ---- 8. the band levels are SIGNED --------------------------------
        //
        // L = G - expand(reduce(G)) straddles zero wherever the blur overshot.
        // Catches a band stored unsigned or clamped at 0, which would pass
        // collapse on a smooth image and fail on an edge.
        {
            const vc::vc_image img = structured_image(32, 32);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 4, k_policy);
            expect(lap.bands.size() == 3, "4 levels gives 3 bands plus a top");
            expect(has_negative(lap.bands.front()),
                   "the finest band contains NEGATIVE samples");
        }

        // ---- 9. COLLAPSE IS EXACT -- the graded criterion ------------------
        //
        // a = (a - b) + b, so this is exact arithmetic and not an
        // approximation. Section C states MAE < 1e-4.
        {
            const vc::vc_image img = noise_image(32, 32, 1, 23U);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 4, k_policy);
            const vc::vc_image back = collapse(lap, k_policy);

            expect(back.width() == 32 && back.height() == 32,
                   "collapse returns the source geometry");
            expect(mae(back, img) < 1e-4F,
                   "collapse of NOISE is exact to MAE < 1e-4");
        }

        // ---- 10. collapse on content with several scales -------------------
        //
        // Noise is almost all top-octave, so group 9 barely exercises the deep
        // levels. This image has a broad gradient, a mid-scale ramp and a fine
        // checkerboard, so every band carries something.
        {
            const vc::vc_image img = structured_image(64, 64);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 5, k_policy);
            expect(mae(collapse(lap, k_policy), img) < 1e-4F,
                   "collapse of MULTI-SCALE structure is exact, 5 levels");
        }

        // ---- 11. collapse on odd and non-square ---------------------------
        //
        // Where the ceil geometry and expand's target actually matter: 13x7
        // reduces to 7x4, then 4x2. An expand that doubled instead of taking
        // the target would subtract mismatched images here and nowhere else.
        {
            const vc::vc_image img = noise_image(13, 7, 1, 29U);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 3, k_policy);
            const vc::vc_image back = collapse(lap, k_policy);
            expect(back.width() == 13 && back.height() == 7,
                   "collapse of a 13x7 image keeps 13x7");
            expect(mae(back, img) < 1e-4F,
                   "collapse is exact on ODD, NON-SQUARE geometry");

            const vc::vc_image rgb = noise_image(13, 7, 3, 31U);
            expect(mae(collapse(laplacian_pyramid(rgb, 3, k_policy), k_policy),
                       rgb) < 1e-4F,
                   "collapse is exact on 3 channels");
        }

        // ---- 12. levels == 1, the degenerate pyramid -----------------------
        //
        // No bands at all, and `top` IS the source. The header states the
        // invariant as bands.size() + 1 == levels, so this is the case where
        // bands is empty -- an implementation that always built at least one
        // band would fail here.
        {
            const vc::vc_image img = noise_image(8, 8, 1, 37U);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 1, k_policy);
            expect(lap.bands.empty(), "1 level gives NO bands");
            expect(rel_diff(lap.top, img) <= 1e-6F,
                   "1 level: top IS the source");
            expect(mae(collapse(lap, k_policy), img) < 1e-6F,
                   "1 level: collapse returns the source untouched");

            const vc_pyramid g = gaussian_pyramid(img, 1, k_policy);
            expect(g.size() == 1 && rel_diff(g[0], img) <= 1e-6F,
                   "gaussian_pyramid level 0 is the source, unblurred");
        }

        // ---- 13. the two pyramids agree level by level ---------------------
        //
        // bands[l] must have Gaussian level l's geometry, and top must have
        // level (levels-1)'s. Checked on a non-square image so a transposed
        // level cannot hide.
        {
            const vc::vc_image img = noise_image(16, 12, 1, 41U);
            const vc_pyramid g = gaussian_pyramid(img, 4, k_policy);
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, 4, k_policy);

            expect(g.size() == 4, "gaussian_pyramid returns `levels` images");
            expect(g[0].width() == 16 && g[0].height() == 12 &&
                       g[1].width() == 8 && g[1].height() == 6 &&
                       g[2].width() == 4 && g[2].height() == 3 &&
                       g[3].width() == 2 && g[3].height() == 2,
                   "16x12 levels: 16x12, 8x6, 4x3, 2x2 (ceil on the odd 3)");

            bool geometry_matches = (lap.bands.size() == 3);
            for (std::size_t l = 0; geometry_matches && l < lap.bands.size();
                 ++l) {
                geometry_matches = lap.bands[l].width() == g[l].width() &&
                                   lap.bands[l].height() == g[l].height();
            }
            expect(geometry_matches,
                   "each band matches its Gaussian level's geometry");
            expect(lap.top.width() == g[3].width() &&
                       lap.top.height() == g[3].height(),
                   "top matches the coarsest Gaussian level");
        }

        // ---- 14. the rejections, each PAIRED with an acceptance ------------
        //
        // A function that threw for every input would satisfy the rejections on
        // its own, which is why each one is paired with an acceptance.
        {
            const vc::vc_image img = noise_image(8, 8, 1, 43U);

            expect(rejects([&] { (void)laplacian_pyramid(img, 0, k_policy); }),
                   "levels == 0 is rejected");
            expect(rejects([&] { (void)gaussian_pyramid(img, 0, k_policy); }),
                   "levels == 0 is rejected for the Gaussian pyramid too");
            expect(rejects([&] { (void)gaussian_pyramid(img, 99, k_policy); }),
                   "more levels than the geometry supports is rejected");
            expect(accepts([&] {
                       (void)gaussian_pyramid(img, max_pyramid_levels(8, 8),
                                              k_policy);
                   }),
                   "exactly max_pyramid_levels is ACCEPTED");
            expect(accepts([&] { (void)gaussian_pyramid(img, 2, k_policy); }),
                   "an ordinary level count is ACCEPTED");

            vc::vc_image_writer u8{8, 8, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();
            expect(
                rejects([&] { (void)gaussian_pyramid(not_f32, 2, k_policy); }),
                "a non-f32 source is rejected");
        }

        // ---- 15. COLLAPSE UNDER ALL FOUR EDGE POLICIES --------------------
        //
        // Every other check in this file uses `clamp`. a = (a-b)+b holds under
        // any policy, because expand() appears on both sides and cancels
        // whatever it did.
        //
        // `zero` is included deliberately even though it does NOT preserve a
        // flat image (group 4 asserts that only for clamp): a darkening border
        // is still reconstructed exactly, because the band records it.
        {
            const vc::vc_image img = noise_image(32, 24, 1, 101U);
            bool all_exact = true;
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::zero,
                  vc_edge_policy::reflect, vc_edge_policy::wrap}) {
                all_exact =
                    all_exact &&
                    mae(collapse(laplacian_pyramid(img, 4, p), p), img) < 1e-4F;
            }
            expect(all_exact,
                   "collapse is exact under clamp, zero, reflect AND wrap");
        }

        // ---- 16. MAXIMUM DEPTH -- does the error compound per level? -------
        //
        // The natural worry with 8 or 9 nested expand/subtract rounds. It does
        // not compound: measured 1.16e-10 at depth 7 and 6.09e-11 at depth 9,
        // slightly smaller deeper because those levels hold fewer values.
        {
            const vc::vc_image img = noise_image(64, 64, 1, 103U);
            const std::size_t deepest = max_pyramid_levels(64, 64);
            expect(deepest == 7, "64x64 supports 7 levels");
            const vc_laplacian_pyramid lap =
                laplacian_pyramid(img, deepest, k_policy);
            expect(lap.bands.size() == deepest - 1,
                   "max depth gives levels-1 bands");
            expect(mae(collapse(lap, k_policy), img) < 1e-4F,
                   "collapse is exact at MAXIMUM depth -- no compounding");
        }

        // ---- 17. DEGENERATE geometry: one axis that never halves ----------
        //
        // A 1xN image reduces 1x16 -> 1x8 -> ... -> 1x1: the WIDTH stays 1 at
        // every level, so expand() is asked for a target of 1 from a source of
        // 1 on that axis. Legal -- 2n-1 = 1 when n = 1 -- and REQUIRED, not a
        // loophole in the validation.
        {
            bool all_exact = true;
            for (const auto wh :
                 std::vector<std::pair<vc::image_dim, vc::image_dim>>{
                     {1, 16}, {16, 1}, {1, 1}, {2, 9}}) {
                const vc::vc_image img =
                    noise_image(wh.first, wh.second, 1, 107U);
                const std::size_t lv = max_pyramid_levels(wh.first, wh.second);
                all_exact = all_exact &&
                            mae(collapse(laplacian_pyramid(img, lv, k_policy),
                                         k_policy),
                                img) < 1e-4F;
            }
            expect(all_exact, "collapse is exact on 1xN, Nx1, 1x1 and 2x9");
            expect(max_pyramid_levels(1, 16) == 5,
                   "1x16 supports 5 levels (16,8,4,2,1) despite width 1");
        }

        // ---- 18. A MALFORMED pyramid is REJECTED, not read out of bounds ---
        //
        // vc_laplacian_pyramid's members are PUBLIC, deliberately -- band-wise
        // editing is the point of the Laplacian form (group 6b does exactly
        // that), so they cannot be sealed behind a validating constructor the
        // way grid2d and vc_image_info are. The invariant has to be checked
        // where it is used.
        //
        // Unchecked, a channel-count mismatch is a heap-buffer-overflow rather
        // than a throw: expand() produces `current`'s channel count while the
        // add loop strides by the BAND's.
        {
            vc_laplacian_pyramid bad =
                laplacian_pyramid(noise_image(8, 8, 3, 109U), 2, k_policy);
            bad.top = noise_image(4, 4, 1, 111U); // right geometry, 1 channel
            expect(
                rejects([&] { (void)collapse(bad, k_policy); }),
                "a channel-count mismatch between bands and top is rejected");

            vc_laplacian_pyramid wrong_geometry =
                laplacian_pyramid(noise_image(8, 8, 1, 113U), 2, k_policy);
            wrong_geometry.top =
                noise_image(3, 3, 1, 127U); // 8 is not 2*3 or 2*3-1
            expect(rejects([&] { (void)collapse(wrong_geometry, k_policy); }),
                   "a broken geometry chain is rejected");
        }

        // ---- 19. THE f32 GATE AT levels == 1 ------------------------------
        //
        // The dtype requirement used to be inherited from convolve_separable,
        // which left a hole: at levels == 1 no reduce() runs, so nothing
        // blurred and nothing rejected. A u8 image came back as level 0 of a
        // "Gaussian pyramid", to fail later in whatever first asked for
        // buf_f32. Group 14's f32 check passed only because it uses 2 levels.
        //
        // Now checked explicitly at every public entry point, so the level
        // count cannot change the answer.
        {
            vc::vc_image_writer u8{8, 8, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();
            expect(
                rejects([&] { (void)gaussian_pyramid(not_f32, 1, k_policy); }),
                "non-f32 is rejected at levels == 1, not just levels > 1");
            expect(
                rejects([&] { (void)laplacian_pyramid(not_f32, 1, k_policy); }),
                "non-f32 is rejected by laplacian_pyramid at levels == 1");
            expect(rejects([&] { (void)reduce(not_f32, k_policy); }),
                   "non-f32 is rejected by reduce directly");
            expect(rejects([&] { (void)expand(not_f32, 16, 16, k_policy); }),
                   "non-f32 is rejected by expand directly");
        }

        // ---- 20. THE ABSOLUTE MAE BOUND IS 0..1-SPECIFIC -------------------
        //
        // Section C states collapse as MAE < 1e-4, and float error scales with
        // magnitude, so that bound is a statement about 0..1 data and not about
        // the algorithm. Measured on 64x64, 5 levels:
        //
        //     values 0..1        MAE 1.02e-10
        //     values 0..255      MAE 4.40e-07
        //     values 0..65535    MAE 1.14e-04     <- fails 1e-4, CORRECTLY
        //     values 0..1e7      MAE 1.68e-02
        //
        // So the absolute criterion would reject correct code on 16-bit-range
        // input. Section F.3 already says tolerances belong relative to signal
        // RMS; this is that rule applied to the one criterion stated otherwise.
        // Every other check in this file feeds 0..1 data, where the two agree.
        {
            bool relative_holds = true;
            for (const float amp : {1.0F, 255.0F, 65535.0F, 1e7F}) {
                const vc::vc_image img = noise_image(64, 64, 1, 131U, amp);
                relative_holds =
                    relative_holds &&
                    rel_mae(
                        collapse(laplacian_pyramid(img, 5, k_policy), k_policy),
                        img) < 1e-6F;
            }
            expect(relative_holds,
                   "collapse is exact RELATIVE to RMS over 1e0 .. 1e7");
        }

        // ---- 21. EXTREME ASPECT -- one axis bottoms out many levels early ---
        //
        // 1024x2 reaches height 1 after one reduce and then keeps halving the
        // width for nine more levels, so most of the pyramid is a single row.
        // Measured exact: 1024x2 and 2x1024 at 11 levels, 512x1 at 10.
        {
            bool all_exact = true;
            for (const auto wh :
                 std::vector<std::pair<vc::image_dim, vc::image_dim>>{
                     {1024, 2}, {2, 1024}, {512, 1}}) {
                const vc::vc_image img =
                    noise_image(wh.first, wh.second, 1, 137U);
                const std::size_t lv = max_pyramid_levels(wh.first, wh.second);
                all_exact = all_exact &&
                            mae(collapse(laplacian_pyramid(img, lv, k_policy),
                                         k_policy),
                                img) < 1e-4F;
            }
            expect(all_exact, "collapse is exact on 1024x2, 2x1024 and 512x1");
            expect(max_pyramid_levels(1024, 2) == 11,
                   "1024x2 supports 11 levels -- driven by the 1024");
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "12_pyramid: " << passed << " / " << total
                  << " checks passed before the exception\n";
        std::cout << "12_pyramid: FAIL\n";
        return 1;
    }

    std::cout << "12_pyramid: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "12_pyramid: FAIL\n";
        return 1;
    }
    std::cout << "12_pyramid: OK\n";
    return 0;
}
