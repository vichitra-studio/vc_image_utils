// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 11_conv_theorem -- acceptance checks for convolve_frequency().
//
// P2 section C makes this one of the phase's two GRADED deliverables, and
// names three things it owes:
//
//     spatial convolve  vs  frequency multiply on the same kernel
//     zero-padding
//     the circular wraparound DEMONSTRATED, not merely avoided
//
// All three are below. The demonstration is check group 6: it does not merely
// prove that padding fixes the wraparound, it MEASURES the contamination so
// the number is on the record.
//
// ---- WHAT EACH GROUP CAN AND CANNOT CATCH ----
//
// 1-2   policy rejection. The DFT cannot express clamp or reflect, so asking
//       must throw rather than quietly substitute wrap -- a substitution that
//       would differ only in a few border pixels.
//
// 3     frequency_extent arithmetic. Deliberately includes a 1-tap kernel
//       (where N+M-1 == N, so padding is a no-op) and a kernel wider than the
//       image, which is legal and makes the extent more than double.
//
// 4     THE HAND CASE. 4x1 image, 3x1 kernel, both policies, every output
//       value checkable on paper. This is the only group that can catch a
//       kernel placed causally instead of centred, because it is the only one
//       whose expected values are known independently of convolve().
//
// 5     the exact cross-check against convolve(wrap). Catches scaling,
//       transposition and sign. CANNOT catch a shift of exactly (rx, ry) if
//       convolve() had the same shift -- which is why group 4 exists.
//
// 6     the wraparound demonstration, measured.
//
// 7     zero-padded agreement with convolve(zero).
//
// 8     a flat image through a sum-to-1 kernel under wrap stays flat.
//       Catches a normalisation error that a random image hides in noise.
//       NOT asserted for zero, because zero does not preserve a uniform
//       image -- vc_edge_policy.h documents that corner reading 44.4.
//
// 9     separable agreement, and multi-channel independence.
//
// Tolerances are RELATIVE TO SIGNAL RMS per section F.3. An absolute
// tolerance that passes on a dark image fails on a bright one for entirely
// correct code.

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
#include "vc/math/vc_grid2d.h"
#include "vc/pixelops/vc_conv_frequency.h"
#include "vc/pixelops/vc_convolve.h"
#include "vc/pixelops/vc_edge_policy.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

bool near(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

// Root mean square of an image's samples -- the scale every tolerance below
// is stated against.
float rms(const vc::vc_image& img) {
    double acc = 0.0;
    std::size_t n = 0;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const vc::buf_f32 v : px) {
            acc += static_cast<double>(v) * static_cast<double>(v);
        }
        n = px.size();
    });
    if (n == 0) {
        return 0.0F;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

// Largest |a - b| over two images. Infinity on any geometry mismatch rather
// than indexing off the end of the smaller -- a wrong output size is a
// failure, not undefined behaviour. (Same guard as 06_dft's max_abs_diff,
// which was added after exactly that bug.)
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

// Relative to the reference's RMS, so a tolerance means the same thing on a
// dark image and a bright one.
float rel_diff(const vc::vc_image& got, const vc::vc_image& want) {
    const float scale = std::max(rms(want), 1e-12F);
    return max_abs_diff(got, want) / scale;
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

float pixel(const vc::vc_image& img, vc::image_dim x, vc::image_dim y) {
    return img.at<vc::buf_f32>(x, y, 0);
}

// A deterministic pseudo-random image. A fixed sequence, not <random>, so a
// failure is reproducible from the source alone.
vc::vc_image noise_image(vc::image_dim w,
                         vc::image_dim h,
                         vc::channel_count ch,
                         std::uint32_t seed) {
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(w) * h * ch);
    std::uint32_t s = seed;
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h * ch; ++i) {
        s = s * 1664525U + 1013904223U;
        v.push_back(static_cast<float>(s >> 8U) /
                    static_cast<float>(1U << 24U));
    }
    return make_image(w, h, ch, v);
}

// True only for a vc_exception carrying invalid_argument. NOT "something
// threw" -- that version passed against the unimplemented stub, which also
// threw, so the rejection checks were green before any rejection existed.
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

// The other half of the contract, and the half that makes the first half mean
// something: the two policies the DFT CAN express must be accepted. Without
// these, a function that threw for every policy would pass the rejections.
bool accepts(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    using vc::math::grid2d;
    using vc::pixelops::convolve;
    using vc::pixelops::convolve_frequency;
    using vc::pixelops::convolve_separable;
    using vc::pixelops::frequency_extent;
    using vc::pixelops::make_gaussian;
    using vc::pixelops::make_gaussian_1d_x;
    using vc::pixelops::make_gaussian_1d_y;
    using vc::pixelops::vc_edge_policy;
    using vc::pixelops::vc_kernel;

    int passed = 0;
    int total = 0;
    auto expect = [&](bool ok, const char* what) {
        ++total;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // The 1-D-shaped case the hand arithmetic uses throughout: impulses
        // at both ends, so the wraparound has something to contaminate with.
        const vc::vc_image row = make_image(4, 1, 1, {1.0F, 0.0F, 0.0F, 1.0F});
        const vc_kernel box3x1{3, 1, {1.0F, 1.0F, 1.0F}};

        // ---- 1. clamp and reflect are REJECTED, not approximated ---------
        //
        // The DFT's basis functions are periodic, so a spectrum can only
        // describe a signal that tiles the plane. There is no spectrum whose
        // inverse transform replicates an edge pixel or mirrors about it.
        // Substituting wrap would differ only in a few border pixels, which
        // is the hardest kind of wrong to notice.
        {
            expect(rejects([&] {
                       (void)convolve_frequency(row, box3x1,
                                                vc_edge_policy::clamp);
                   }),
                   "convolve_frequency rejects clamp");
            expect(rejects([&] {
                       (void)convolve_frequency(row, box3x1,
                                                vc_edge_policy::reflect);
                   }),
                   "convolve_frequency rejects reflect");
            expect(rejects([&] {
                       (void)frequency_extent(row, box3x1,
                                              vc_edge_policy::clamp);
                   }),
                   "frequency_extent rejects clamp");
            expect(rejects([&] {
                       (void)frequency_extent(row, box3x1,
                                              vc_edge_policy::reflect);
                   }),
                   "frequency_extent rejects reflect");

            // And the two it must ACCEPT. These fail while the bodies are
            // stubs, which is the point -- the four rejections above are
            // satisfiable by a function that refuses everything.
            expect(accepts([&] {
                       (void)frequency_extent(row, box3x1,
                                              vc_edge_policy::wrap);
                   }),
                   "frequency_extent ACCEPTS wrap");
            expect(accepts([&] {
                       (void)frequency_extent(row, box3x1,
                                              vc_edge_policy::zero);
                   }),
                   "frequency_extent ACCEPTS zero");
        }

        // ---- 2. the extent under wrap is the source extent ---------------
        {
            const grid2d e = frequency_extent(row, box3x1,
                                              vc_edge_policy::wrap);
            expect(e.width() == 4 && e.height() == 1,
                   "wrap extent == source extent (no padding)");
        }

        // ---- 3. the extent under zero is W+kw-1 by H+kh-1 ----------------
        //
        // Three cases, chosen for what they can catch:
        //   box3x1 on 4x1  -> 6x1   the ordinary case
        //   1x1 kernel     -> 4x1   N+M-1 == N, so padding is a NO-OP. An
        //                           implementation that always padded by a
        //                           constant fails here.
        //   9x9 on 4x4     -> 12x12 kernel WIDER than the image, which is
        //                           legal; the extent more than doubles.
        {
            const grid2d e1 = frequency_extent(row, box3x1,
                                               vc_edge_policy::zero);
            expect(e1.width() == 6 && e1.height() == 1,
                   "zero extent: 4x1 with 3x1 kernel -> 6x1");

            const vc_kernel one{1, 1, {1.0F}};
            const grid2d e2 = frequency_extent(row, one,
                                               vc_edge_policy::zero);
            expect(e2.width() == 4 && e2.height() == 1,
                   "zero extent: a 1-tap kernel needs NO padding");

            const vc::vc_image sq = noise_image(4, 4, 1, 7U);
            const vc_kernel big{9, 9, std::vector<float>(81, 1.0F / 81.0F)};
            const grid2d e3 = frequency_extent(sq, big, vc_edge_policy::zero);
            expect(e3.width() == 12 && e3.height() == 12,
                   "zero extent: kernel wider than the image is legal");
        }

        // ---- 4. THE HAND CASE -- the only group with independent values ---
        //
        // x = [1, 0, 0, 1], h = [1, 1, 1] CENTRED, so taps sit at offsets
        // -1, 0, +1. convolve() computes g(i) = sum_kx h(kx).f(i-kx).
        //
        // WRAP, with f read modulo 4:
        //   g(0) = f(1) + f(0) + f(-1 -> 3) = 0 + 1 + 1 = 2
        //   g(1) = f(2) + f(1) + f(0)       = 0 + 0 + 1 = 1
        //   g(2) = f(3) + f(2) + f(1)       = 1 + 0 + 0 = 1
        //   g(3) = f(4 -> 0) + f(3) + f(2)  = 1 + 1 + 0 = 2
        //                                     -> [2, 1, 1, 2]
        //
        // ZERO, with out-of-range reading 0:
        //   g(0) = f(1) + f(0) + 0          = 0 + 1 + 0 = 1
        //   g(1) = f(2) + f(1) + f(0)       = 0 + 0 + 1 = 1
        //   g(2) = f(3) + f(2) + f(1)       = 1 + 0 + 0 = 1
        //   g(3) = 0 + f(3) + f(2)          = 0 + 1 + 0 = 1
        //                                     -> [1, 1, 1, 1]
        //
        // NOTE the wrap answer is [2,1,1,2] and NOT the [2,2,1,1] in
        // notes/waves_and_signals.txt section 5d. That note indexes the
        // kernel CAUSALLY (taps at 0,1,2); convolve() centres it. Same
        // answer shifted by one, both correct for their own convention --
        // and this check pins the one convolve() actually uses.
        //
        // This is the group that catches a causally-placed kernel. Group 5
        // cannot: if convolve_frequency shifted everything by (rx, ry), it
        // would still differ from convolve() -- but if BOTH were somehow
        // shifted, only independently-known values would notice.
        {
            const vc::vc_image w =
                convolve_frequency(row, box3x1, vc_edge_policy::wrap);
            expect(w.width() == 4 && w.height() == 1 && w.channels() == 1,
                   "hand case: wrap output geometry follows the source");
            expect(near(pixel(w, 0, 0), 2.0F, 1e-5F) &&
                       near(pixel(w, 1, 0), 1.0F, 1e-5F) &&
                       near(pixel(w, 2, 0), 1.0F, 1e-5F) &&
                       near(pixel(w, 3, 0), 2.0F, 1e-5F),
                   "hand case: wrap gives [2,1,1,2] (centred kernel)");

            const vc::vc_image z =
                convolve_frequency(row, box3x1, vc_edge_policy::zero);
            expect(z.width() == 4 && z.height() == 1,
                   "hand case: zero output is CROPPED back to source size");
            expect(near(pixel(z, 0, 0), 1.0F, 1e-5F) &&
                       near(pixel(z, 1, 0), 1.0F, 1e-5F) &&
                       near(pixel(z, 2, 0), 1.0F, 1e-5F) &&
                       near(pixel(z, 3, 0), 1.0F, 1e-5F),
                   "hand case: zero gives [1,1,1,1]");
        }

        // ---- 5. THE EXACT CROSS-CHECK: wrap must match convolve(wrap) ----
        //
        // Both paths assume the image tiles the plane, so they must agree
        // EVERYWHERE INCLUDING THE BORDERS. "The boundaries differ" is not
        // available as an explanation here -- that is the whole point of
        // using wrap for this comparison.
        //
        // An asymmetric kernel, deliberately. A symmetric one cannot see a
        // transposed pass or a 180-degree kernel flip, which is the lesson
        // week 5 recorded when conv2d's convolution-vs-correlation sign went
        // undetected behind a symmetric test kernel.
        {
            const vc::vc_image img = noise_image(32, 24, 1, 11U);
            const vc_kernel asym{3, 3,
                                 {0.0F, 0.1F, 0.2F,
                                  0.3F, 0.4F, 0.0F,
                                  0.0F, 0.0F, 0.5F}};

            const vc::vc_image spatial =
                convolve(img, asym, vc_edge_policy::wrap);
            const vc::vc_image freq =
                convolve_frequency(img, asym, vc_edge_policy::wrap);

            expect(rel_diff(freq, spatial) <= 1e-5F,
                   "wrap: frequency == spatial to 1e-5 of RMS, borders too");
        }

        // ---- 6. THE WRAPAROUND, DEMONSTRATED AND MEASURED ----------------
        //
        // Section C requires this shown, not merely avoided.
        //
        // An 8x1 row that is bright ONLY at the right-hand end, blurred by a
        // 3-tap box of weight 1/3. Under wrap, pixel 0 reads f(-1) = f(7) =
        // 1, so it comes out at 1/3 -- brightness teleported from the far
        // side of the image. Under zero it reads 0 and stays dark.
        //
        //   wrap: g(0) = (f(1) + f(0) + f(7)) / 3 = (0 + 0 + 1) / 3 = 1/3
        //   zero: g(0) = (f(1) + f(0) +    0) / 3 =              0
        //
        // So the contamination is EXACTLY 1/3, by hand, and the assertion
        // pins that value rather than just "they differ".
        {
            const vc::vc_image edge =
                make_image(8, 1, 1,
                           {0.0F, 0.0F, 0.0F, 0.0F,
                            0.0F, 0.0F, 0.0F, 1.0F});
            const vc_kernel box{3, 1,
                                {1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F}};

            const vc::vc_image w =
                convolve_frequency(edge, box, vc_edge_policy::wrap);
            const vc::vc_image z =
                convolve_frequency(edge, box, vc_edge_policy::zero);

            expect(near(pixel(w, 0, 0), 1.0F / 3.0F, 1e-5F),
                   "wraparound: the LEFT edge is lit by the RIGHT edge (1/3)");
            expect(near(pixel(z, 0, 0), 0.0F, 1e-5F),
                   "padding removes it: the left edge stays dark");
            expect(std::fabs(pixel(w, 0, 0) - pixel(z, 0, 0)) > 0.3F,
                   "the contamination is LARGE, not a rounding difference");

            // And the interior is untouched by the choice -- the wraparound
            // is a border effect, which is exactly why it is easy to ship.
            expect(near(pixel(w, 3, 0), pixel(z, 3, 0), 1e-5F),
                   "wraparound touches only the border, not the interior");
        }

        // ---- 7. zero-padded agreement with convolve(zero) ----------------
        {
            const vc::vc_image img = noise_image(32, 24, 1, 23U);
            const vc_kernel asym{3, 3,
                                 {0.0F, 0.1F, 0.2F,
                                  0.3F, 0.4F, 0.0F,
                                  0.0F, 0.0F, 0.5F}};

            const vc::vc_image spatial =
                convolve(img, asym, vc_edge_policy::zero);
            const vc::vc_image freq =
                convolve_frequency(img, asym, vc_edge_policy::zero);

            expect(rel_diff(freq, spatial) <= 1e-5F,
                   "zero: frequency == spatial to 1e-5 of RMS");
        }

        // ---- 8. a sum-to-1 kernel under wrap preserves a FLAT image ------
        //
        // Every tap reads the same value c, and the weights sum to 1, so the
        // output is c exactly. A scaling error of any kind shows here as a
        // clean offset instead of hiding inside the noise of group 5.
        //
        // Asserted for wrap ONLY. zero does NOT preserve a uniform image --
        // vc_edge_policy.h records the 3x3-box corner reading 44.4 from a
        // flat 100 -- so the same check under zero would be asserting a bug.
        {
            const vc::vc_image flat =
                make_image(16, 16, 1, std::vector<float>(256, 0.37F));
            const vc_kernel box{3, 3, std::vector<float>(9, 1.0F / 9.0F)};

            const vc::vc_image w =
                convolve_frequency(flat, box, vc_edge_policy::wrap);
            expect(rel_diff(w, flat) <= 1e-5F,
                   "wrap: a sum-to-1 kernel leaves a flat image flat");
        }

        // ---- 9. separable agreement, and channel independence ------------
        //
        // The separable path is week 5's, and it reaches the same answer by a
        // third route. Agreement between all three is stronger than between
        // any two.
        //
        // Multi-channel because convolve_frequency transforms each channel
        // separately, which is where a stride mistake lives: a 3-channel
        // image whose channels get mixed still looks plausible.
        {
            const vc::vc_image img = noise_image(32, 32, 3, 31U);
            const float sigma = 1.4F;

            const vc::vc_image sep =
                convolve_separable(img, make_gaussian_1d_x(sigma),
                                   make_gaussian_1d_y(sigma),
                                   vc_edge_policy::wrap);
            const vc::vc_image freq =
                convolve_frequency(img, make_gaussian(sigma, sigma),
                                   vc_edge_policy::wrap);

            expect(freq.channels() == 3,
                   "channel count follows the source");
            expect(rel_diff(freq, sep) <= 1e-5F,
                   "wrap: frequency == SEPARABLE spatial, 3 channels");
        }

        // ---- 10. A KERNEL WIDER THAN THE IMAGE, which collides under wrap
        //
        // Added after a review found this silently wrong. Under wrap the extent
        // IS the source extent, so a kernel wider than the image maps two or
        // more taps into the same plane slot and their weights must SUM. A
        // 5-tap kernel on a 4-wide ring sends offset -2 and offset +2 both to
        // slot 2; assigning instead of accumulating lets the later write win
        // and drops the other tap.
        //
        // Weights are powers of two so every contribution is identifiable in
        // the result -- 17 can only be 1+16, which is exactly the collision.
        //
        // x = [1,0,0,0], h = {1,2,4,8,16} at offsets -2..+2. Since x is an
        // impulse at 0, the output IS the kernel plane, so these four numbers
        // read it directly:
        //
        //   WRAP   y[0] = h( 0)          = 4
        //          y[1] = h(+1)          = 8
        //          y[2] = h(-2) + h(+2)  = 1 + 16 = 17   <- THE COLLISION
        //          y[3] = h(-1)          = 2
        //
        //   ZERO   extent 4+5-1 = 8, so no two offsets share a slot
        //          y = [4, 8, 16, 0]
        {
            const vc::vc_image impulse =
                make_image(4, 1, 1, {1.0F, 0.0F, 0.0F, 0.0F});
            const vc_kernel k5{5, 1, {1.0F, 2.0F, 4.0F, 8.0F, 16.0F}};

            const vc::vc_image w =
                convolve_frequency(impulse, k5, vc_edge_policy::wrap);
            expect(near(pixel(w, 0, 0), 4.0F, 1e-4F) &&
                       near(pixel(w, 1, 0), 8.0F, 1e-4F) &&
                       near(pixel(w, 2, 0), 17.0F, 1e-4F) &&
                       near(pixel(w, 3, 0), 2.0F, 1e-4F),
                   "wide kernel, wrap: colliding taps SUM to 17, not 16");

            const vc::vc_image z =
                convolve_frequency(impulse, k5, vc_edge_policy::zero);
            expect(near(pixel(z, 0, 0), 4.0F, 1e-4F) &&
                       near(pixel(z, 1, 0), 8.0F, 1e-4F) &&
                       near(pixel(z, 2, 0), 16.0F, 1e-4F) &&
                       near(pixel(z, 3, 0), 0.0F, 1e-4F),
                   "wide kernel, zero: padded wide enough, no collision");

            // And against the spatial path, which accumulates per tap and so
            // was right all along.
            expect(rel_diff(w, convolve(impulse, k5, vc_edge_policy::wrap)) <=
                       1e-5F,
                   "wide kernel, wrap: matches convolve(wrap)");
            expect(rel_diff(z, convolve(impulse, k5, vc_edge_policy::zero)) <=
                       1e-5F,
                   "wide kernel, zero: matches convolve(zero)");
        }

        // ---- 11. the 1x1 identity kernel -------------------------------------
        //
        // The degenerate case, and the one where zero and wrap must AGREE:
        // N+M-1 = N+1-1 = N, so the zero policy pads by nothing and the two
        // paths are the same computation. A padding implementation that added a
        // constant margin rather than kw-1 would differ here.
        {
            const vc::vc_image img = noise_image(8, 5, 1, 97U);
            const vc_kernel scale3{1, 1, {3.0F}};

            const vc::vc_image w =
                convolve_frequency(img, scale3, vc_edge_policy::wrap);
            const vc::vc_image z =
                convolve_frequency(img, scale3, vc_edge_policy::zero);

            expect(rel_diff(w, convolve(img, scale3, vc_edge_policy::wrap)) <=
                       1e-5F,
                   "1x1 kernel: wrap just scales the image");
            expect(rel_diff(z, w) <= 1e-5F,
                   "1x1 kernel: zero and wrap agree, because kw-1 == 0");
        }

        // ---- 12. ODD dimensions, both axes ---------------------------------
        //
        // Every other image in this file is even on both axes, so an off-by-one
        // that only shows on an odd extent would pass the whole file. 5x3 with a
        // 3x3 kernel gives odd source dims AND odd padded dims (7x5).
        {
            const vc::vc_image img = noise_image(5, 3, 1, 61U);
            const vc_kernel asym{3, 3,
                                 {0.0F, 0.1F, 0.2F,
                                  0.3F, 0.4F, 0.0F,
                                  0.0F, 0.0F, 0.5F}};

            expect(frequency_extent(img, asym, vc_edge_policy::zero).width() ==
                       7 &&
                   frequency_extent(img, asym, vc_edge_policy::zero).height() ==
                       5,
                   "odd dims: 5x3 with 3x3 -> 7x5");
            expect(rel_diff(convolve_frequency(img, asym, vc_edge_policy::wrap),
                            convolve(img, asym, vc_edge_policy::wrap)) <= 1e-5F,
                   "odd dims: wrap matches convolve(wrap)");
            expect(rel_diff(convolve_frequency(img, asym, vc_edge_policy::zero),
                            convolve(img, asym, vc_edge_policy::zero)) <= 1e-5F,
                   "odd dims: zero matches convolve(zero)");
        }

        // ---- 13. collisions in BOTH axes, and more than two per slot ------
        //
        // Group 10 is 1-D and collides exactly one pair. A kernel wider than
        // the image in BOTH axes collides on both, and a kernel more than twice
        // the image collides THREE taps into one slot: 9x9 on a 4-wide ring
        // sends offsets -4, 0 and +4 all to slot 0.
        //
        // Normalised weights deliberately. With raw weights 1..81 the outputs
        // reach ~1500 and float error is proportionally visible -- measured
        // 2.5e-07 relative, which is correct but only 40x inside the
        // tolerance. Normalising keeps the margin at ~100x so this check fails
        // for a real reason or not at all.
        {
            const vc::vc_image img = noise_image(4, 4, 1, 5U);
            const vc_kernel k9{9, 9, std::vector<float>(81, 1.0F / 81.0F)};

            expect(rel_diff(convolve_frequency(img, k9, vc_edge_policy::wrap),
                            convolve(img, k9, vc_edge_policy::wrap)) <= 1e-5F,
                   "9x9 on 4x4 wrap: THREE taps per slot, both axes");
            expect(rel_diff(convolve_frequency(img, k9, vc_edge_policy::zero),
                            convolve(img, k9, vc_edge_policy::zero)) <= 1e-5F,
                   "9x9 on 4x4 zero: extent 12x12, so no collision at all");
        }

        // ---- 14. the smallest possible image -------------------------------
        //
        // grid2d refuses a zero dimension, so 1x1 is the floor. Worth pinning
        // because it exercises dft1d at N=1 on both axes, where the transform
        // is the identity and every loop bound is degenerate.
        {
            const vc::vc_image one = make_image(1, 1, 1, {0.25F});
            const vc_kernel twice{1, 1, {2.0F}};
            const vc::vc_image doubled =
                convolve_frequency(one, twice, vc_edge_policy::wrap);
            expect(doubled.width() == 1 && doubled.height() == 1 &&
                       near(pixel(doubled, 0, 0), 0.5F, 1e-5F),
                   "1x1 image with a 1x1 kernel scales and keeps its shape");

            // A 1x1 image with a 3x3 kernel under zero: only the centre tap
            // sees the pixel, so the answer is pixel x centre weight.
            const vc_kernel box{3, 3, std::vector<float>(9, 1.0F / 9.0F)};
            const vc::vc_image blurred =
                convolve_frequency(one, box, vc_edge_policy::zero);
            expect(near(pixel(blurred, 0, 0), 0.25F / 9.0F, 1e-6F),
                   "1x1 image, 3x3 kernel, zero: only the centre tap lands");
        }

        // ---- 15. a non-f32 source is REJECTED ------------------------------
        //
        // The header says f32 only and the code checks it, but nothing
        // exercised the check. An untested throw is an untested branch.
        {
            vc::vc_image_writer u8{4, 4, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();
            const vc_kernel box{3, 3, std::vector<float>(9, 1.0F / 9.0F)};
            expect(rejects([&] {
                       (void)convolve_frequency(not_f32, box,
                                                vc_edge_policy::wrap);
                   }),
                   "a non-f32 source is rejected");
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "11_conv_theorem: " << passed << " / " << total
                  << " checks passed before the exception\n";
        std::cout << "11_conv_theorem: FAIL\n";
        return 1;
    }

    std::cout << "11_conv_theorem: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "11_conv_theorem: FAIL\n";
        return 1;
    }
    std::cout << "11_conv_theorem: OK\n";
    return 0;
}
