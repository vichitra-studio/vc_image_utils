// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 05 -- convolution, and the three conventions it will not tell you
// about.
//
// Example 04 ended on [-1, 1]: the derivative of a scanline is a subtraction
// of neighbours. That is already a convolution, with a two-tap kernel, applied
// in one dimension. This is the general machine.
//
//     g(i,j) = sum over taps of  f(i-k, j-l) . h(k,l)
//
// The operation is the easy part. What this example actually verifies is the
// three things the formula leaves undeclared, each of which fails SILENTLY --
// no crash, no NaN, just a picture that is subtly and confidently wrong:
//
//     the kernel's ORIGIN      wrong -> the whole image is shifted
//     the sign of the OFFSET   wrong -> asymmetric kernels mirror,
//                                       derivative kernels change sign
//     the BOUNDARY policy      wrong -> the border darkens or smears
//
// Every assertion below exists because some plausible wrong implementation
// passes without it. That framing is the P1 lesson applied up front rather
// than after the fact: after writing a test, ask what wrong implementation
// would still pass it.
//
// A worked example of exactly that, and the reason several tests here use a
// deliberately lopsided kernel: a symmetric box blur is blind to ALL THREE
// failures above. It blurs correctly with the kernel corner-anchored (just
// shifted), it is identical under convolution and correlation (reversing a
// symmetric kernel changes nothing), and on a uniform image three of the four
// boundary policies agree. A test built on a box blur can pass while every
// convention in the file is wrong.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/io/vc_io_stb.h"
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

// A single-channel image from row-major values, so the fixtures below read on
// the page the way they do in the notebook.
vc::vc_image
grey(vc::image_dim w, vc::image_dim h, const std::vector<float>& values) {
    vc::vc_image_writer out{w, h, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            out.at<vc::buf_f32>(x, y, 0) =
                values[static_cast<std::size_t>(y) * w + x];
        }
    }
    return std::move(out).seal();
}

float px(const vc::vc_image& img, vc::image_dim x, vc::image_dim y) {
    return img.at<vc::buf_f32>(x, y, 0);
}

// The 5x5 fixture, values 1..25 so every pixel is distinguishable and a
// hand-computed result is checkable by eye.
//
//      1   2   3   4   5
//      6   7   8   9  10
//     11  12  13  14  15
//     16  17  18  19  20
//     21  22  23  24  25
vc::vc_image ramp_5x5() {
    std::vector<float> v(25);
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<float>(i + 1);
    }
    return grey(5, 5, v);
}

// A deliberately LOPSIDED 3x3 kernel, centred:
//
//     h(-1,-1)  h(0,-1)  h(1,-1)         0  0  0
//     h(-1, 0)  h(0, 0)  h(1, 0)    =    0  1  2
//     h(-1, 1)  h(0, 1)  h(1, 1)         0  0  3
//
// Chosen so that reversing it produces a visibly different kernel (not merely
// a sign flip, as an antisymmetric derivative kernel would), which is what
// makes the convolution-vs-correlation tests below able to fail.
vc::pixelops::vc_kernel lopsided() {
    return vc::pixelops::vc_kernel{
        3, 3, std::vector<float>{0, 0, 0, 0, 1, 2, 0, 0, 3}};
}

} // namespace

int main() {
    using namespace vc::pixelops;
    const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
    bool ok = true;

    try {

        // -----------------------------------------------------------------
        // 1. The kernel type: odd dimensions, and the centre that follows
        // -----------------------------------------------------------------
        //
        // Even sizes are rejected at CONSTRUCTION, not tolerated and worked
        // around, because an even kernel has no middle tap -- only two candidates
        // half a pixel apart. Rejecting here is what lets radius_x() and at() be
        // total functions with no "if there is no centre" branch anywhere
        // downstream.
        {
            bool threw = false;
            try {
                (void)vc_kernel{2, 3, std::vector<float>(6, 0.0F)};
            } catch (const std::exception&) {
                threw = true;
            }
            ok =
                check(threw, "even kernel width is rejected at construction") &&
                ok;

            threw = false;
            try {
                (void)vc_kernel{3, 3, std::vector<float>(8, 0.0F)};
            } catch (const std::exception&) {
                threw = true;
            }
            ok = check(threw, "weights.size() must equal width * height") && ok;

            const vc_kernel k = lopsided();
            ok = check(k.radius_x() == 1 && k.radius_y() == 1,
                       "a 3x3 kernel has radius (1,1)") &&
                 ok;

            // The centre tap is at offset (0,0). If this reads 0 instead of 1, the
            // origin is being taken as the corner -- the shift bug, caught here
            // rather than as a mysteriously translated image later.
            ok = check(near(k.at(0, 0), 1.0F, 0.0F),
                       "at(0,0) is the CENTRE tap, not the corner") &&
                 ok;
            ok = check(near(k.at(1, 0), 2.0F, 0.0F) &&
                           near(k.at(1, 1), 3.0F, 0.0F),
                       "offsets are centre-relative") &&
                 ok;

            // A box sums to 1 (preserves average brightness). Accumulated in
            // double inside sum() -- at 21x21 that is 441 additions of small
            // numbers, which is the same argument that puts the DFT inner sum in
            // double.
            ok = check(near(make_box(3, 3).sum(), 1.0F, 1e-6F),
                       "a normalised box kernel sums to 1") &&
                 ok;
            ok = check(near(make_gaussian(2.0F, 0.8F).sum(), 1.0F, 1e-6F),
                       "a Gaussian is normalised by its SAMPLED sum, not the "
                       "analytic constant") &&
                 ok;
        }

        // -----------------------------------------------------------------
        // 2. reversed() -- the one operation that separates the two ops
        // -----------------------------------------------------------------
        {
            const vc_kernel k = lopsided();
            const vc_kernel r = k.reversed();

            // Rotating 180 degrees means out.at(kx,ky) == in.at(-kx,-ky).
            ok = check(near(r.at(-1, 0), 2.0F, 0.0F) &&
                           near(r.at(-1, -1), 3.0F, 0.0F) &&
                           near(r.at(0, 0), 1.0F, 0.0F),
                       "reversed() rotates the kernel 180 degrees") &&
                 ok;

            // Reversing twice is the identity -- cheap, and it catches a reverse
            // that only flips one axis, which a single-direction test would not.
            const vc_kernel rr = r.reversed();
            bool same = true;
            for (std::int64_t ky = -1; ky <= 1; ++ky) {
                for (std::int64_t kx = -1; kx <= 1; ++kx) {
                    same = same && near(rr.at(kx, ky), k.at(kx, ky), 0.0F);
                }
            }
            ok = check(same, "reversed() twice is the identity") && ok;
        }

        // -----------------------------------------------------------------
        // 3. The hand-computed 3x3 -- and the flip, predicted on paper
        // -----------------------------------------------------------------
        //
        // At the centre pixel (2,2) of the 1..25 ramp, only three taps are
        // non-zero. Worked by hand:
        //
        //   CONVOLUTION   g = sum f(2-kx, 2-ky) h(kx,ky)      offsets SUBTRACT
        //       h(0,0)=1 . f(2,2)=13  =  13
        //       h(1,0)=2 . f(1,2)=12  =  24
        //       h(1,1)=3 . f(1,1)= 7  =  21
        //                               ---
        //                                58
        //
        //   CORRELATION   g = sum f(2+kx, 2+ky) h(kx,ky)      offsets ADD
        //       h(0,0)=1 . f(2,2)=13  =  13
        //       h(1,0)=2 . f(3,2)=14  =  28
        //       h(1,1)=3 . f(3,3)=19  =  57
        //                               ---
        //                                98
        //
        // 58 against 98. The kernel reaches up-and-left for convolution and
        // down-and-right for correlation -- that is the prediction, and it is what
        // the sign of the offset physically means.
        {
            const vc::vc_image src = ramp_5x5();
            const vc_kernel k = lopsided();

            const vc::vc_image c = convolve(src, k, vc_edge_policy::zero);
            const vc::vc_image x = correlate(src, k, vc_edge_policy::zero);

            ok =
                check(
                    near(px(c, 2, 2), 58.0F, 1e-4F),
                    "convolution at (2,2) matches the hand computation (58)") &&
                ok;
            ok =
                check(
                    near(px(x, 2, 2), 98.0F, 1e-4F),
                    "correlation at (2,2) matches the hand computation (98)") &&
                ok;

            // The identity that makes correlate() a flip rather than a second
            // loop. If this fails, two implementations have drifted apart.
            const vc::vc_image via_flip =
                convolve(src, k.reversed(), vc_edge_policy::zero);
            bool identical = true;
            for (vc::image_dim y = 0; y < 5; ++y) {
                for (vc::image_dim xi = 0; xi < 5; ++xi) {
                    identical = identical &&
                                near(px(x, xi, y), px(via_flip, xi, y), 1e-5F);
                }
            }
            ok =
                check(
                    identical,
                    "correlate(f,h) == convolve(f, reversed(h)), everywhere") &&
                ok;
        }

        // -----------------------------------------------------------------
        // 3b. The 1-D hand case -- and the ANTISYMMETRIC kernel
        // -----------------------------------------------------------------
        //
        // [1, 2, 3] convolved with [1, 0, -1], the derivative kernel, zero
        // boundary. Worked by hand:
        //
        //     g(i) = sum f(i-k) h(k),   h(-1)=1, h(0)=0, h(1)=-1
        //
        //     g(0) = f(1).1 + f(0).0 + f(-1).(-1) =  2 + 0 -  0  =  2
        //     g(1) = f(2).1 + f(1).0 + f( 0).(-1) =  3 + 0 -  1  =  2
        //     g(2) = f(3).1 + f(2).0 + f( 1).(-1) =  0 + 0 -  2  = -2
        //
        //                                         ->  [ 2,  2, -2]
        //     correlation                         ->  [-2, -2,  2]
        //
        // EXACT NEGATIONS, and that is the whole point of choosing this
        // kernel. [1,0,-1] is antisymmetric -- h(-k) = -h(k) -- so reversing
        // it IS negating it. Section 3's lopsided kernel shows the two
        // operations produce different pictures; this one shows the case that
        // actually bites, because a sign flip on a gradient does not look
        // wrong, it just points every edge the other way.
        //
        // That is P3's Sobel, met a phase early: the operator is a smoothed
        // version of exactly this kernel, and getting the convention wrong
        // there inverts gradient direction for everything built on it.
        {
            const vc::vc_image row = grey(3, 1, {1, 2, 3});
            const vc_kernel deriv{3, 1, kernel_weights{1.0F, 0.0F, -1.0F}};

            const vc::vc_image c = convolve(row, deriv, vc_edge_policy::zero);
            ok = check(near(px(c, 0, 0), 2.0F, 1e-5F) &&
                           near(px(c, 1, 0), 2.0F, 1e-5F) &&
                           near(px(c, 2, 0), -2.0F, 1e-5F),
                       "1-D convolution [1,2,3]*[1,0,-1] matches the hand "
                       "computation [2, 2, -2]") &&
                 ok;

            const vc::vc_image x = correlate(row, deriv, vc_edge_policy::zero);
            ok = check(near(px(x, 0, 0), -2.0F, 1e-5F) &&
                           near(px(x, 1, 0), -2.0F, 1e-5F) &&
                           near(px(x, 2, 0), 2.0F, 1e-5F),
                       "correlation with an ANTISYMMETRIC kernel is the exact "
                       "negation of convolution") &&
                 ok;

            // Stated as the relationship rather than as two value tables, so
            // it keeps holding if the fixture ever changes.
            for (vc::image_dim i = 0; i < 3; ++i) {
                ok = check(near(px(c, i, 0), -px(x, i, 0), 1e-5F),
                           "antisymmetric: conv == -corr, pixel by pixel") &&
                     ok;
            }

            // A derivative kernel sums to zero -- it measures change, so a
            // constant field must map to nothing. Cheap, and it catches a
            // mis-entered kernel before it reaches an image.
            ok = check(near(deriv.sum(), 0.0F, 1e-6F),
                       "a derivative kernel sums to 0, not 1") &&
                 ok;
        }

        // -----------------------------------------------------------------
        // 4. The impulse test -- what a delta proves
        // -----------------------------------------------------------------
        //
        //     convolve(delta, h) = h              the kernel, in order
        //     correlate(delta, h) = h reversed    the kernel, mirrored
        //
        // Both follow from the sifting property: the delta kills every term of the
        // sum except one, and the sign of the offset decides which one survives.
        // This is also why "impulse response" is the name -- poke an LSI system
        // with a point and it hands you its kernel.
        //
        // The kernel here MUST be asymmetric. With a symmetric one, h and its
        // mirror are the same picture and this test passes under either operation
        // -- flip-blind, in exactly the way a magnitude-only spectrum check is
        // shift-blind.
        {
            std::vector<float> d(25, 0.0F);
            d[2 * 5 + 2] = 1.0F; // impulse at (2,2)
            const vc::vc_image delta = grey(5, 5, d);
            const vc_kernel k = lopsided();

            const vc::vc_image c = convolve(delta, k, vc_edge_policy::zero);
            ok =
                check(
                    near(px(c, 2, 2), 1.0F, 1e-5F) &&
                        near(px(c, 3, 2), 2.0F, 1e-5F) &&
                        near(px(c, 3, 3), 3.0F, 1e-5F),
                    "convolve(delta, h) stamps h un-mirrored at the impulse") &&
                ok;

            const vc::vc_image x = correlate(delta, k, vc_edge_policy::zero);
            ok = check(near(px(x, 2, 2), 1.0F, 1e-5F) &&
                           near(px(x, 1, 2), 2.0F, 1e-5F) &&
                           near(px(x, 1, 1), 3.0F, 1e-5F),
                       "correlate(delta, h) stamps h MIRRORED") &&
                 ok;
        }

        // -----------------------------------------------------------------
        // 5. Boundaries, part one: a uniform image
        // -----------------------------------------------------------------
        //
        // 5x5 of 100s, 3x3 box. At the corner, four taps land inside and five do
        // not, so:
        //
        //     zero     4 x 100 x 1/9  =  400/9  =  44.4     <- the dark border
        //     clamp    every tap reads a real 100           =  100
        //     reflect  every tap reads a real 100           =  100
        //     wrap     every tap reads a real 100           =  100
        //
        // The deficit IS the artefact. And note what this test can and cannot see:
        // on a uniform image the last three are indistinguishable, because every
        // redirection lands on the same value. That is what part two is for.
        {
            const vc::vc_image flat =
                grey(5, 5, std::vector<float>(25, 100.0F));
            const vc_kernel box = make_box(3, 3);

            ok = check(near(px(convolve(flat, box, vc_edge_policy::zero), 0, 0),
                            400.0F / 9.0F, 1e-3F),
                       "zero boundary loses 5/9 of the corner: 100 -> 44.4") &&
                 ok;

            for (const auto& [policy, name] :
                 {std::pair{vc_edge_policy::clamp,
                            "clamp preserves a uniform corner: expected 100"},
                  std::pair{vc_edge_policy::reflect,
                            "reflect preserves a uniform corner: expected 100"},
                  std::pair{vc_edge_policy::wrap,
                            "wrap preserves a uniform corner: expected 100"}}) {
                const vc::vc_image g = convolve(flat, box, policy);
                ok = check(near(px(g, 0, 0), 100.0F, 1e-3F), name) && ok;
            }

            // The interior is unaffected by any policy -- if this fails, the
            // boundary logic is leaking into pixels that have all nine neighbours.
            ok = check(near(px(convolve(flat, box, vc_edge_policy::zero), 2, 2),
                            100.0F, 1e-3F),
                       "the interior is untouched by the boundary policy") &&
                 ok;
        }

        // -----------------------------------------------------------------
        // 6. Boundaries, part two: a ramp tells the policies apart
        // -----------------------------------------------------------------
        //
        // Row [1 2 3 4 5], 1-D box [1/3 1/3 1/3], output at x = 0. The kernel
        // reaches for f(-1), which each policy answers differently:
        //
        //     zero     f(-1) = 0            (0 + 1 + 2)/3 = 1.000
        //     clamp    f(-1) = f(0) = 1     (1 + 1 + 2)/3 = 1.333
        //     reflect  f(-1) = f(1) = 2     (2 + 1 + 2)/3 = 1.667
        //     wrap     f(-1) = f(4) = 5     (5 + 1 + 2)/3 = 2.667
        //
        // Four distinct values. This is the test that can actually fail when a
        // policy is wired to the wrong branch.
        //
        // Note the reflect convention being pinned: mirror about the EDGE PIXEL,
        // so f(-1) = f(1) = 2 and the edge is not repeated. The other variant
        // mirrors about the half-pixel boundary, giving f(-1) = f(0) = 1 -- which
        // would make reflect and clamp agree here and this test unable to separate
        // them. (OpenCV ships both; this is its BORDER_REFLECT_101.)
        {
            const vc::vc_image row = grey(5, 1, {1, 2, 3, 4, 5});
            const vc_kernel box1d{
                3, 1, {1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F}};

            ok =
                check(near(px(convolve(row, box1d, vc_edge_policy::zero), 0, 0),
                           1.0F, 1e-4F),
                      "ramp, zero boundary -> 1.000") &&
                ok;
            ok = check(
                     near(px(convolve(row, box1d, vc_edge_policy::clamp), 0, 0),
                          4.0F / 3.0F, 1e-4F),
                     "ramp, clamp boundary -> 1.333") &&
                 ok;
            ok =
                check(near(px(convolve(row, box1d, vc_edge_policy::reflect), 0,
                              0),
                           5.0F / 3.0F, 1e-4F),
                      "ramp, reflect boundary -> 1.667 (mirrors about the edge "
                      "PIXEL, f(-1)=f(1))") &&
                ok;
            ok =
                check(near(px(convolve(row, box1d, vc_edge_policy::wrap), 0, 0),
                           8.0F / 3.0F, 1e-4F),
                      "ramp, wrap boundary -> 2.667") &&
                ok;
        }

        // -----------------------------------------------------------------
        // 7. Separability
        // -----------------------------------------------------------------
        //
        // THE WIDTHS DIFFER ON PURPOSE. With sigma_x == sigma_y the two 1-D
        // factors are identical, so applying them to the wrong axes gives the
        // right answer anyway and the test cannot see the swap. 2.0 against 0.8
        // makes a transposed pass fail loudly.
        //
        // Same class of blindness as a row-major/column-major sweep that passes
        // for both layouts -- which is a bug this codebase has already shipped
        // once, at P1.
        //
        // THE TOLERANCE IS RELATIVE, and that is not fussiness -- an absolute one
        // is wrong here and measurably so. The two paths do different numbers of
        // operations on the same data (91 multiply-adds against 20), so they
        // accumulate different rounding, and that difference scales with the
        // VALUES in the image. Measured, same kernel, float32:
        //
        //     5x5   values 1..25     max|full - sep| = 9.5e-06
        //     32x32 values 1..1024   max|full - sep| = 3.7e-04
        //     relative to signal max, both cases:      ~4e-07
        //
        // An absolute 1e-5 passes the first and fails the second by 37x, on
        // identical, correct code. The relative figure is stable across both. This
        // is the same trap the fft2d round-trip criterion carries in the phase
        // plan, met early and in miniature.
        //
        // The image is 24x24, not the 5x5 used above, because sigma_x = 2 makes a
        // 13-tap kernel and a kernel wider than the image tests almost nothing but
        // the boundary policy.
        {
            constexpr vc::image_dim side = 24;
            std::vector<float> v(static_cast<std::size_t>(side) * side);
            for (std::size_t i = 0; i < v.size(); ++i) {
                v[i] = static_cast<float>(i + 1);
            }
            const vc::vc_image src = grey(side, side, v);
            const float sx = 2.0F;
            const float sy = 0.8F;

            const vc::vc_image full =
                convolve(src, make_gaussian(sx, sy), vc_edge_policy::clamp);
            const vc::vc_image sep = convolve_separable(
                src, make_gaussian_1d_x(sx), make_gaussian_1d_y(sy),
                vc_edge_policy::clamp);

            float worst = 0.0F;
            float scale = 0.0F;
            for (vc::image_dim y = 0; y < side; ++y) {
                for (vc::image_dim x = 0; x < side; ++x) {
                    worst = std::max(worst,
                                     std::fabs(px(full, x, y) - px(sep, x, y)));
                    scale = std::max(scale, std::fabs(px(full, x, y)));
                }
            }
            ok = check(worst < 1e-5F * scale,
                       "separable path matches the full 2-D path, boundary "
                       "included (tolerance RELATIVE to signal magnitude)") &&
                 ok;

            // Passing the same kernel twice would silently blur one axis twice.
            bool threw = false;
            try {
                (void)convolve_separable(src, make_gaussian_1d_x(sx),
                                         make_gaussian_1d_x(sy),
                                         vc_edge_policy::clamp);
            } catch (const std::exception&) {
                threw = true;
            }
            ok = check(threw, "convolve_separable rejects two row kernels") &&
                 ok;

            // The whole argument for separability, as a number.
            const vc_kernel k2d = make_gaussian(sx, sy);
            const std::size_t direct =
                static_cast<std::size_t>(k2d.width()) * k2d.height();
            const std::size_t separable =
                static_cast<std::size_t>(k2d.width()) + k2d.height();
            std::cout << "  separability: " << direct
                      << " multiplies per pixel "
                      << "direct, " << separable << " separable\n";
            ok = check(separable < direct,
                       "the separable path is demonstrably fewer multiplies") &&
                 ok;
        }

        // -----------------------------------------------------------------
        // 8. Dump-and-inspect
        // -----------------------------------------------------------------
        //
        // The assertions above prove correctness. These exist to be LOOKED at --
        // the blur should be visibly softer, and the zero-boundary dump should
        // have the dark frame that section 5 measured as 44.4 at the corner.
        {
            // A synthetic fixture rather than a photo, so the example has no file
            // dependency: a bright square on a dark field, which makes both the
            // blur and the dark border obvious by eye.
            // A DARK square on a BRIGHT field, that way round on purpose. The
            // zero-boundary dump exists to show the border deficit, and zero
            // padding DARKENS -- so on a dark field it would take near-black
            // to slightly-more-near-black and demonstrate nothing. Against a
            // bright field the frame is unmistakable: 0.9 falls to about
            // 0.9 * 4/9 = 0.4 at the corner, the same 4/9 the uniform-image
            // assertion measures.
            constexpr vc::image_dim side = 64;
            std::vector<float> field(static_cast<std::size_t>(side) * side,
                                     0.9F);
            for (vc::image_dim y = 20; y < 44; ++y) {
                for (vc::image_dim x = 20; x < 44; ++x) {
                    field[static_cast<std::size_t>(y) * side + x] = 0.1F;
                }
            }
            const vc::vc_image square = grey(side, side, field);
            const vc_kernel g = make_gaussian(3.0F, 3.0F);

            vc::io::stb_image_writer writer;
            const vc::io::write_config png{.format =
                                               vc::io::vc_image_format::png};

            writer.write(out_dir + "/05_blur_clamp.png",
                         convolve(square, g, vc_edge_policy::clamp), png);
            writer.write(out_dir + "/05_blur_zero_dark_border.png",
                         convolve(square, g, vc_edge_policy::zero), png);

            // The flip, as a picture. With a lopsided kernel these two differ
            // visibly; with a Gaussian they would be byte-identical, which is
            // exactly why a symmetric kernel cannot test the flip.
            const vc_kernel k = lopsided();
            writer.write(out_dir + "/05_lopsided_convolved.png",
                         convolve(square, k, vc_edge_policy::clamp), png);
            writer.write(out_dir + "/05_lopsided_correlated.png",
                         correlate(square, k, vc_edge_policy::clamp), png);

            std::cout << "  wrote 4 dumps to " << out_dir << '\n';
        }

    } catch (const std::exception& e) {
        // Expected while the implementation is still stubbed: each run stops
        // at the first thing not yet written, which is also a serviceable
        // progress meter. A clean non-zero beats an abort -- examples/README
        // asks for a failing return, not a crash.
        std::cerr << "NOT YET IMPLEMENTED: " << e.what() << '\n';
        return 1;
    }

    std::cout << (ok ? "05_convolve: OK\n" : "05_convolve: FAILED\n");
    return ok ? 0 : 1;
}
