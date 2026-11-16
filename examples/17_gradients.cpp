// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 17_gradients -- acceptance checks for the Sobel gradient pair.
//
// P3 section B's S0. Reasoning, the three conventions and the limitations are
// in include/vc/pixelops/vc_gradients.h; the hand-derivations the numbers here
// come from are in curriculum/notes/gradients.txt, sections 10-13 (X3 boxes
// 1-6).
//
// ---- WHERE EACH NUMBER CAME FROM, BECAUSE IT MATTERS WHO DERIVED IT --------
//
// Groups 3, 7 and 8 assert numbers worked on PAPER BEFORE ANY CODE EXISTED --
// that is the whole point of X3, and it is why a wrong implementation cannot
// quietly redefine what "right" means here. The attribution is recorded
// honestly in the note's header: boxes 1, 3 and the sign choice in box 6 were
// the learner's; BOXES 2, 4 AND 5 WERE WORKED BY THE SCAFFOLDER ON REQUEST.
//
// GROUP 3's +50 IS BOX 2's NUMBER, AND THE PLAN'S FREE MITIGATION FOR IT --
// re-derive it INDEPENDENTLY before the test goes green -- IS NO LONGER
// AVAILABLE, by decision: scaffolding was requested ahead of the
// re-derivation, and the answer is now printed in this file, in the header
// and in the stub. What remains possible is a CHECK, not an independent
// derivation -- and that check WAS DONE on 2026-11-16. The record of what it
// was: work the patch yourself and confirm you land on 50 by both
// routes. Weaker, honestly weaker, and recorded as such rather than counted
// as met.
//
// ---- EXACT EQUALITY WHERE THE ARITHMETIC IS EXACT, TOLERANCE WHERE NOT -----
//
// Every weight here is a dyadic fraction -- eighths, quarters, halves -- and
// every test input is a small integer. Products and sums of those are exact
// f32 in any summation order, so groups 1-3, 5, 6, 9, 10 and 11 are
// EQUALITIES. A tolerance there would be a place for a real error to hide.
//
// The three ANGLE and MAGNITUDE checks -- group 4's +pi/2, group 7's
// sqrt(2) ratio and group 8's -pi/4 -- are the only ones with a tolerance,
// because atan2 and sqrt are not exact. It is tight: 1e-6 relative. Note
// that group 4's dx/dy assertions beside it are still equalities; only the
// angle is not.
//
// ---- WHAT EACH GROUP CAN AND CANNOT CATCH ---------------------------------
//
// 1   the six kernels themselves. Weights, sums, the outer-product identity,
//     and the zero middle column/row. Cheap, fails first, so later failures
//     are not noise from one root cause. Catches a mis-built kernel but says
//     NOTHING about whether gradient_x() then applies it correctly.
//
// 1b  operator* ON ITS OWN, with a 5x3 of distinct primes. Non-square and
//     asymmetric because a 3x3 Sobel kernel is neither, and so cannot see a
//     width/height swap or a transposed loop inside the product. Plus the
//     order rejections: ROW * COLUMN throws rather than transposing.
//
// 2   THE IMPULSE, and it is the only complete check of the operator AS
//     APPLIED. convolve(delta, h) reproduces h in place, so this reads all
//     nine weights back out of a real call -- centring, policy wiring and
//     flip included. Groups 3-8 are consequences of it, not independent
//     detectors.
//     ONE REAL BLIND SPOT, FOUND BY RUNNING IT AND NOT BY ASSERTING IT: this
//     group compares the output against make_sobel_x()'s OWN weights, so a
//     WRONG KERNEL passes -- both sides move together. It detects a wrong
//     APPLICATION. Group 1 is the one that detects a wrong kernel. Neither
//     alone is enough, which is why both are here.
//
// 3   BOX 2. A vertical step of height 100 -> dx = +50 EXACTLY, dy = 0. Three
//     separate things ride on that one number:
//       the SIGN      +50 not -50 is the flip (convention 2)
//       the SCALE     50 not 400 is the /8 (convention 1)
//       the AXIS      dy = 0 is the cross-axis, and a transposed
//                     implementation fails it loudly
//
// 4   THE MIRROR. A horizontal step -> dy = +50, dx = 0, and direction =
//     +pi/2. Group 3 alone cannot see a swapped pair; this one can, and the
//     +pi/2 is the direct statement that POSITIVE ANGLES POINT DOWN THE
//     SCREEN (convention 3).
//
// 5   flat -> exactly zero in both. Pins sum(w) == 0 as APPLIED. Note that
//     direction is NOT asserted here: atan2(0,0) is 0 and atan2(+0,-0) is pi,
//     values that carry no information but look like they do.
//
// 6   a PLANE, f = 3x - 5y -> dx == 3 and dy == -5 exactly, in the interior.
//     This is the strongest single gate in the file: the coefficients are
//     distinct, asymmetric and opposite in sign, so it pins sign, scale AND
//     axis assignment simultaneously. f = x - y would not -- a transpose plus
//     a sign flip lands back on itself.
//
// 7   BOX 4, the 45-degree over-report. magnitude(diagonal) / magnitude
//     (vertical) == 3*sqrt(2)/4 exactly. Swept over the three defensible
//     values for the AMBIGUOUS on-diagonal pixels (0, 50, 100) because the
//     answer must not depend on that judgement call -- the dx weights at
//     (0,0),(1,1),(2,2) are -1,0,+1 and sum to zero, so the constant cancels.
//     THAT CANCELLATION ONLY HOLDS ON A DIAGONAL PIXEL, and the sub-check
//     shows the adjacent centre moving with d, which is why the sampling
//     point is pinned rather than incidental.
//
// 8   BOX 5. direction on that same patch == -pi/4. The arithmetic is
//     trivial; the CONVENTION is the exercise.
//
// 9   both implementation paths, bit for bit, under all four policies:
//     gradient_x() == convolve(full 3x3) == convolve_separable(the factors).
//     Dyadic weights make this an equality rather than a tolerance. It leaves
//     the implementation choice open while pinning the behaviour closed.
//
// 10  geometry: odd, non-square, 1-wide and 1-tall; and three channels with
//     three DIFFERENT expected answers, so a channel-index slip cannot land
//     on the right value by accident.
//
// 11  rejections PAIRED WITH ACCEPTANCES -- unpaired, a function that threw
//     for everything would pass. Plus the pairing contract on magnitude and
//     direction, which is the one genuinely new failure mode in this file:
//     two images that must agree in shape.
//
// 12  the dumps. Not a check -- the pictures are the point, and what to look
//     for is printed beside each.
//
// ---- MEASURED AGAINST THREE DELIBERATE BREAKS, NOT ASSUMED TO WORK -------
//
// Before this suite was handed over it was run against a reference
// implementation (78/78) and then against three broken ones, to check that
// the gates can actually fail rather than merely pass:
//
//     kernels NOT reversed     16 checks fail, incl. BOX 2, BOX 5 and the
//     (the flip bug)           plane. Group 2 PASSES -- see its blind spot
//                              above. This is the bug vc_convolve.h warns
//                              "nothing crashes" about, and it is loud here.
//
//     weights unnormalised     16 fail. Group 7's RATIO survives, correctly:
//     (the /8 missing)         a ratio of two magnitudes cannot see a common
//                              scale factor. Group 3's +50 is what catches it.
//
//     axes swapped             24 fail -- the widest net of the three, and
//     (gradient_x uses          group 2 is among them. Group 7's ratio fails
//      sobel_y)                 too, but for a reason worth naming: its
//                               DENOMINATOR goes to zero. A vertical step is
//                               invisible to sobel_y, so with both outputs
//                               computed from it the vertical magnitude is 0
//                               and the ratio is a division by zero. Group 9
//                               is the clean detector here -- eight checks,
//                               one per policy per path.
//
// NOT TESTED, deliberately: anything about direction where the magnitude is
// zero. The header declares it undefined there and a test would pin an
// accident of IEEE sign-of-zero.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/io/vc_io_stb.h"
#include "vc/pixelops/vc_convolve.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_gradients.h"

namespace {

constexpr float k_pi = 3.14159265358979323846F;

bool check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

vc::vc_image from_rule(vc::image_dim w,
                       vc::image_dim h,
                       const std::function<float(int, int)>& f,
                       vc::channel_count ch = 1) {
    vc::vc_image_writer out{w, h, ch, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            for (vc::channel_count c = 0; c < ch; ++c) {
                out.at<vc::buf_f32>(x, y, c) =
                    f(static_cast<int>(x), static_cast<int>(y)) *
                    static_cast<float>(c + 1);
            }
        }
    }
    return std::move(out).seal();
}

float pixel(const vc::vc_image& img,
            vc::image_dim x,
            vc::image_dim y,
            vc::channel_count c = 0) {
    return img.at<vc::buf_f32>(x, y, c);
}

bool near_rel(float got, float want, float tol) {
    const float scale = std::max(1.0F, std::fabs(want));
    return std::fabs(got - want) <= tol * scale;
}

// Exact, on purpose. See the note at the top.
bool interior_all_exactly(const vc::vc_image& img, float want) {
    for (vc::image_dim y = 1; y + 1 < img.height(); ++y) {
        for (vc::image_dim x = 1; x + 1 < img.width(); ++x) {
            if (pixel(img, x, y) != want) {
                std::cerr << "  at (" << x << ',' << y << ") got "
                          << pixel(img, x, y) << " want " << want << '\n';
                return false;
            }
        }
    }
    return true;
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

const char* policy_name(vc::pixelops::vc_edge_policy p) {
    switch (p) {
    case vc::pixelops::vc_edge_policy::clamp:
        return "clamp";
    case vc::pixelops::vc_edge_policy::zero:
        return "zero";
    case vc::pixelops::vc_edge_policy::reflect:
        return "reflect";
    case vc::pixelops::vc_edge_policy::wrap:
        return "wrap";
    }
    return "?";
}

// The diagonal test image of box 4, as a RULE so the expectation can be a rule
// too. Bright above-right of the diagonal, dark below-left, and `d` is the
// value given to the ambiguous pixels ON the diagonal -- the whole point being
// that the answer must not depend on it.
vc::vc_image diagonal_patch(float d) {
    return from_rule(5, 5, [d](int x, int y) {
        if (x > y) {
            return 100.0F;
        }
        if (x < y) {
            return 0.0F;
        }
        return d;
    });
}

} // namespace

int main() {
    using namespace vc::pixelops;

    const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);

    int passed = 0;
    int total_checks = 0;
    auto expect = [&](bool ok, const std::string& what) {
        ++total_checks;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // ---- 1. the six kernels -------------------------------------------
        {
            const vc_kernel dx1 = make_sobel_derivative_1d_x();
            const vc_kernel dy1 = make_sobel_derivative_1d_y();
            const vc_kernel sx1 = make_sobel_smooth_1d_x();
            const vc_kernel sy1 = make_sobel_smooth_1d_y();

            expect(dx1.width() == 3 && dx1.height() == 1,
                   "derivative_1d_x is a ROW, 3x1");
            expect(dy1.width() == 1 && dy1.height() == 3,
                   "derivative_1d_y is a COLUMN, 1x3");
            expect(sx1.width() == 3 && sx1.height() == 1,
                   "smooth_1d_x is a ROW, 3x1");
            expect(sy1.width() == 1 && sy1.height() == 3,
                   "smooth_1d_y is a COLUMN, 1x3");

            const std::vector<float> want_d{0.5F, 0.0F, -0.5F};
            const std::vector<float> want_s{0.25F, 0.5F, 0.25F};
            expect(dx1.weights() == want_d && dy1.weights() == want_d,
                   "derivative factors are {+0.5, 0, -0.5} -- STORED REVERSED, "
                   "positive first");
            expect(sx1.weights() == want_s && sy1.weights() == want_s,
                   "smooth factors are {0.25, 0.5, 0.25}");

            expect(dx1.sum() == 0.0F && dy1.sum() == 0.0F,
                   "a derivative factor sums to EXACTLY zero");
            expect(sx1.sum() == 1.0F && sy1.sum() == 1.0F,
                   "a smoothing factor sums to EXACTLY one (a true weighted "
                   "average)");

            // Antisymmetric, so reversing IS negating. This is the property
            // that makes the flip dangerous and the Laplacian safe.
            expect(dx1.reversed().weights() !=
                       make_sobel_derivative_1d_x().weights(),
                   "reversing a derivative factor CHANGES it (antisymmetric)");
            expect(sx1.reversed().weights() == sx1.weights(),
                   "reversing a smoothing factor does nothing (symmetric)");

            const vc_kernel kx = make_sobel_x();
            const vc_kernel ky = make_sobel_y();
            expect(kx.width() == 3 && kx.height() == 3 && ky.width() == 3 &&
                       ky.height() == 3,
                   "both 2-D kernels are 3x3");

            const std::vector<float> want_kx{0.125F, 0.0F, -0.125F,
                                             0.25F,  0.0F, -0.25F,
                                             0.125F, 0.0F, -0.125F};
            const std::vector<float> want_ky{0.125F,  0.25F,  0.125F,
                                             0.0F,    0.0F,   0.0F,
                                             -0.125F, -0.25F, -0.125F};
            expect(kx.weights() == want_kx,
                   "sobel_x weights are exactly +1/8 +1/4 +1/8 on the LEFT");
            expect(ky.weights() == want_ky,
                   "sobel_y weights are exactly +1/8 +1/4 +1/8 on TOP");

            expect(kx.sum() == 0.0F && ky.sum() == 0.0F,
                   "both 2-D kernels sum to EXACTLY zero");

            // The permanent sanity check: a centred difference does not look
            // at the pixel it is standing on.
            expect(kx.at(0, -1) == 0.0F && kx.at(0, 0) == 0.0F &&
                       kx.at(0, 1) == 0.0F,
                   "sobel_x middle COLUMN is all zeros");
            expect(ky.at(-1, 0) == 0.0F && ky.at(0, 0) == 0.0F &&
                       ky.at(1, 0) == 0.0F,
                   "sobel_y middle ROW is all zeros");

            // The outer-product identity, EXACTLY -- the weights are eighths.
            //
            // These nine literal checks above are KEPT even though
            // make_sobel_x() is now built as a product. Once it is derived
            // rather than written out, they stop being a restatement of the
            // source and become the INDEPENDENT PIN on the answer -- the only
            // thing in the file that would notice if operator* and the 1-D
            // factors were both wrong in compensating ways.
            bool outer_x = true;
            bool outer_y = true;
            for (kernel_offset ky_o = -1; ky_o <= 1; ++ky_o) {
                for (kernel_offset kx_o = -1; kx_o <= 1; ++kx_o) {
                    outer_x = outer_x && (kx.at(kx_o, ky_o) ==
                                          dx1.at(kx_o, 0) * sy1.at(0, ky_o));
                    outer_y = outer_y && (ky.at(kx_o, ky_o) ==
                                          sx1.at(kx_o, 0) * dy1.at(0, ky_o));
                }
            }
            expect(outer_x, "sobel_x IS smooth_y * derivative_x, exactly");
            expect(outer_y, "sobel_y IS derivative_y * smooth_x, exactly");

            // And the operator itself, applied: COLUMN * ROW.
            bool via_op_x = (sy1 * dx1).weights() == kx.weights();
            bool via_op_y = (dy1 * sx1).weights() == ky.weights();
            expect(via_op_x && via_op_y,
                   "make_sobel_x/_y ARE the operator* of their factors, bit "
                   "for bit");
        }

        // ---- 1b. operator*, on its own ------------------------------------
        {
            // NON-SQUARE AND ASYMMETRIC ON PURPOSE. A 3x3 Sobel kernel cannot
            // see a width/height swap inside the operator -- it is square --
            // and symmetric weights could not see a transpose either. 5 wide
            // by 3 tall, with distinct primes, fails loudly on both.
            const vc_kernel col{1, 3, {2.0F, 3.0F, 5.0F}};
            const vc_kernel row{5, 1, {7.0F, 11.0F, 13.0F, 17.0F, 19.0F}};
            const vc_kernel k = col * row;

            expect(k.width() == 5 && k.height() == 3,
                   "COLUMN(1x3) * ROW(5x1) is 5 wide by 3 tall -- (Nx1)(1xM)");

            bool exact = true;
            for (std::size_t j = 0; j < 3; ++j) {
                for (std::size_t i = 0; i < 5; ++i) {
                    exact = exact && (k.weights()[(j * 5) + i] ==
                                      col.weights()[j] * row.weights()[i]);
                }
            }
            expect(exact, "every weight is column[j] * row[i], exactly");

            // 2*7 = 14 is the top-left and 5*19 = 95 the bottom-right. A
            // transposed loop would put 95 somewhere it does not belong, and
            // these two numbers are hand-checkable.
            expect(k.weights().front() == 14.0F && k.weights().back() == 95.0F,
                   "top-left is 2*7 = 14 and bottom-right is 5*19 = 95");

            // The order is the whole safety property. ROW * COLUMN is a 1x1
            // inner product -- never what a caller wants -- so it throws
            // rather than transposing silently.
            expect(rejects([&] { (void)(row * col); }),
                   "ROW * COLUMN is REJECTED (the argument-order slip)");
            expect(rejects([&] { (void)(col * col); }),
                   "COLUMN * COLUMN is rejected (right operand not 1 tall)");
            expect(rejects([&] { (void)(row * row); }),
                   "ROW * ROW is rejected (left operand not 1 wide)");
            expect(rejects([&] { (void)(make_sobel_x() * row); }),
                   "a 2-D kernel on the left is rejected");
            expect(accepts([&] { (void)(col * row); }),
                   "COLUMN * ROW is ACCEPTED (so the rejections are not "
                   "vacuous)");

            // Degenerate but legal: a 1x1 left operand scales a row.
            const vc_kernel one{1, 1, {4.0F}};
            const vc_kernel scaled_row = one * row;
            expect(scaled_row.width() == 5 && scaled_row.height() == 1 &&
                       scaled_row.weights()[2] == 52.0F,
                   "1x1 * ROW scales the row (4 * 13 = 52)");
        }

        // ---- 2. the impulse: reads all nine weights back out --------------
        {
            const vc::vc_image delta = from_rule(5, 5, [](int x, int y) {
                return (x == 2 && y == 2) ? 1.0F : 0.0F;
            });
            const vc::vc_image gx = gradient_x(delta, vc_edge_policy::zero);
            const vc::vc_image gy = gradient_y(delta, vc_edge_policy::zero);

            expect(gx.width() == 5 && gx.height() == 5 && gx.channels() == 1,
                   "impulse response keeps the source geometry");

            // convolve(delta, h) reproduces h, centred where the impulse was.
            // So the expected picture for gradient_x is the STORED layout:
            //
            //     +1/8   0  -1/8
            //     +1/4   0  -1/4
            //     +1/8   0  -1/8
            //
            // The + column sits to the LEFT of the dot, and that is correct
            // rather than backwards: the pixel immediately left of a bright
            // dot sees brightness rising to ITS right, so its dx is positive.
            const vc_kernel kx = make_sobel_x();
            const vc_kernel ky = make_sobel_y();
            bool exact_x = true;
            bool exact_y = true;
            for (vc::image_dim y = 0; y < 5; ++y) {
                for (vc::image_dim x = 0; x < 5; ++x) {
                    const auto ox = static_cast<kernel_offset>(x) - 2;
                    const auto oy = static_cast<kernel_offset>(y) - 2;
                    const bool in =
                        (ox >= -1 && ox <= 1 && oy >= -1 && oy <= 1);
                    const float wx = in ? kx.at(ox, oy) : 0.0F;
                    const float wy = in ? ky.at(ox, oy) : 0.0F;
                    exact_x = exact_x && (pixel(gx, x, y) == wx);
                    exact_y = exact_y && (pixel(gy, x, y) == wy);
                }
            }
            expect(exact_x, "gradient_x of an impulse reproduces sobel_x in "
                            "place, all nine weights");
            expect(exact_y, "gradient_y of an impulse reproduces sobel_y in "
                            "place, all nine weights");
        }

        // ---- 3. BOX 2: the vertical step ----------------------------------
        {
            // 5x5 so the sampled pixel is interior and no policy can reach it.
            // Around (2,2) the columns read 0, 0, 100 -- box 2's patch.
            const vc::vc_image step = from_rule(
                5, 5, [](int x, int) { return x >= 3 ? 100.0F : 0.0F; });

            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::zero,
                  vc_edge_policy::reflect, vc_edge_policy::wrap}) {
                const vc::vc_image gx = gradient_x(step, p);
                const vc::vc_image gy = gradient_y(step, p);
                // +50 == (100 - 0)/2: the central difference across the step,
                // in brightness per pixel. NOT 400 (unnormalised) and NOT -50
                // (unflipped).
                expect(pixel(gx, 2, 2) == 50.0F,
                       std::string("BOX 2: a step of height 100 gives dx = "
                                   "EXACTLY +50 under ") +
                           policy_name(p));
                expect(pixel(gy, 2, 2) == 0.0F,
                       std::string("BOX 2: a vertical step is INVISIBLE to dy "
                                   "under ") +
                           policy_name(p));
            }
        }

        // ---- 4. THE MIRROR: the horizontal step ---------------------------
        {
            const vc::vc_image step = from_rule(
                5, 5, [](int, int y) { return y >= 3 ? 100.0F : 0.0F; });
            const vc::vc_image gx = gradient_x(step, vc_edge_policy::clamp);
            const vc::vc_image gy = gradient_y(step, vc_edge_policy::clamp);

            expect(pixel(gy, 2, 2) == 50.0F,
                   "MIRROR: dark above, bright below gives dy = EXACTLY +50");
            expect(pixel(gx, 2, 2) == 0.0F,
                   "MIRROR: a horizontal step is INVISIBLE to dx");

            // The direct statement of convention 3. Brightness rises as the
            // row index rises, i.e. DOWN the screen, and the angle is +90.
            const vc::vc_image dir = gradient_direction(gx, gy);
            expect(near_rel(pixel(dir, 2, 2), k_pi / 2.0F, 1e-6F),
                   "MIRROR: direction is +pi/2 -- POSITIVE ANGLES POINT DOWN "
                   "THE SCREEN");
        }

        // ---- 5. flat -> exactly zero --------------------------------------
        {
            for (const float k : {0.0F, 1.0F, 255.0F}) {
                const vc::vc_image flat =
                    from_rule(7, 5, [k](int, int) { return k; });
                const vc::vc_image gx = gradient_x(flat, vc_edge_policy::clamp);
                const vc::vc_image gy = gradient_y(flat, vc_edge_policy::clamp);
                expect(interior_all_exactly(gx, 0.0F) &&
                           interior_all_exactly(gy, 0.0F),
                       std::string("flat image of ") + std::to_string(k) +
                           " gives EXACTLY zero in both");
                const vc::vc_image mag = gradient_magnitude(gx, gy);
                expect(interior_all_exactly(mag, 0.0F),
                       "flat image gives EXACTLY zero magnitude");
                // Direction deliberately NOT asserted. See the header.
            }
        }

        // ---- 6. a plane: f = 3x - 5y --------------------------------------
        {
            // The coefficients are distinct, asymmetric and opposite in sign
            // on purpose. f = x - y could not tell a transpose from a sign
            // flip, the same blindness make_gaussian's independent sigmas
            // exist to defeat.
            const vc::vc_image plane = from_rule(7, 5, [](int x, int y) {
                return (3.0F * static_cast<float>(x)) -
                       (5.0F * static_cast<float>(y));
            });
            const vc::vc_image gx = gradient_x(plane, vc_edge_policy::clamp);
            const vc::vc_image gy = gradient_y(plane, vc_edge_policy::clamp);
            expect(interior_all_exactly(gx, 3.0F),
                   "f = 3x - 5y gives dx == EXACTLY 3 everywhere interior");
            expect(interior_all_exactly(gy, -5.0F),
                   "f = 3x - 5y gives dy == EXACTLY -5 everywhere interior");
        }

        // ---- 7. BOX 4: the 45-degree over-report --------------------------
        {
            const vc::vc_image vstep = from_rule(
                5, 5, [](int x, int) { return x >= 3 ? 100.0F : 0.0F; });
            const vc::vc_image vmag =
                gradient_magnitude(gradient_x(vstep, vc_edge_policy::clamp),
                                   gradient_y(vstep, vc_edge_policy::clamp));
            const float vertical = pixel(vmag, 2, 2);

            const float want_ratio = 3.0F * std::sqrt(2.0F) / 4.0F;

            // Swept over the three defensible values for the ambiguous
            // on-diagonal pixels. The dx weights sitting on the diagonal are
            // -1/8, 0, +1/8 and sum to zero, so a constant added there
            // cancels -- the answer must not move.
            for (const float d : {0.0F, 50.0F, 100.0F}) {
                const vc::vc_image img = diagonal_patch(d);
                const vc::vc_image gx = gradient_x(img, vc_edge_policy::clamp);
                const vc::vc_image gy = gradient_y(img, vc_edge_policy::clamp);
                const vc::vc_image mag = gradient_magnitude(gx, gy);

                // (2,2) is ON the diagonal. That is load-bearing, not
                // incidental -- see the sub-check below.
                expect(pixel(gx, 2, 2) == 37.5F && pixel(gy, 2, 2) == -37.5F,
                       std::string("BOX 4: on-diagonal dx/dy are exactly "
                                   "+37.5 / -37.5, independent of d = ") +
                           std::to_string(d));
                expect(near_rel(pixel(mag, 2, 2) / vertical, want_ratio, 1e-6F),
                       std::string("BOX 4: magnitude(45) / magnitude(0) == "
                                   "3*sqrt(2)/4 at d = ") +
                           std::to_string(d));
            }

            // AND THE REASON THE SAMPLING POINT IS PINNED. One pixel across,
            // the ambiguous cells fall under different weights and the answer
            // DOES move with d. If this ever stops being true, the claim
            // above has quietly become luck.
            const vc::vc_image a = diagonal_patch(0.0F);
            const vc::vc_image b = diagonal_patch(100.0F);
            const float off_a =
                pixel(gradient_x(a, vc_edge_policy::clamp), 3, 2);
            const float off_b =
                pixel(gradient_x(b, vc_edge_policy::clamp), 3, 2);
            expect(off_a != off_b,
                   "the OFF-diagonal centre DOES depend on the ambiguous "
                   "pixels -- which is why the test samples on-diagonal");
        }

        // ---- 8. BOX 5: the angle ------------------------------------------
        {
            const vc::vc_image img = diagonal_patch(0.0F);
            const vc::vc_image gx = gradient_x(img, vc_edge_policy::clamp);
            const vc::vc_image gy = gradient_y(img, vc_edge_policy::clamp);
            const vc::vc_image dir = gradient_direction(gx, gy);

            // dx = +37.5 (brighter rightward), dy = -37.5 (brighter as the
            // row index FALLS, i.e. upward). The gradient points at the
            // upper-right corner, which is where the bright region is -- and
            // atan2 reports that as -45 BECAUSE ROWS GROW DOWNWARD.
            expect(near_rel(pixel(dir, 2, 2), -k_pi / 4.0F, 1e-6F),
                   "BOX 5: direction on the diagonal patch is EXACTLY -pi/4");
        }

        // ---- 9. both implementation paths, bit for bit --------------------
        {
            const vc::vc_image structured = from_rule(9, 7, [](int x, int y) {
                return static_cast<float>(((5 * x) + (3 * y)) % 11);
            });
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::zero,
                  vc_edge_policy::reflect, vc_edge_policy::wrap}) {
                const vc::vc_image gx = gradient_x(structured, p);
                const vc::vc_image full =
                    convolve(structured, make_sobel_x(), p);
                const vc::vc_image sep =
                    convolve_separable(structured, make_sobel_derivative_1d_x(),
                                       make_sobel_smooth_1d_y(), p);
                bool same_full = true;
                bool same_sep = true;
                for (vc::image_dim y = 0; y < gx.height(); ++y) {
                    for (vc::image_dim x = 0; x < gx.width(); ++x) {
                        same_full =
                            same_full && (pixel(gx, x, y) == pixel(full, x, y));
                        same_sep =
                            same_sep && (pixel(gx, x, y) == pixel(sep, x, y));
                    }
                }
                expect(same_full,
                       std::string("gradient_x == convolve(sobel_x) bit for "
                                   "bit under ") +
                           policy_name(p));
                expect(same_sep,
                       std::string("gradient_x == convolve_separable(factors) "
                                   "bit for bit under ") +
                           policy_name(p));

                const vc::vc_image gy = gradient_y(structured, p);
                const vc::vc_image sepy =
                    convolve_separable(structured, make_sobel_smooth_1d_x(),
                                       make_sobel_derivative_1d_y(), p);
                bool same_y = true;
                for (vc::image_dim y = 0; y < gy.height(); ++y) {
                    for (vc::image_dim x = 0; x < gy.width(); ++x) {
                        same_y =
                            same_y && (pixel(gy, x, y) == pixel(sepy, x, y));
                    }
                }
                expect(same_y,
                       std::string("gradient_y == convolve_separable(factors) "
                                   "bit for bit under ") +
                           policy_name(p));
            }
        }

        // ---- 10. geometry and channels ------------------------------------
        {
            for (const auto dims :
                 std::vector<std::pair<vc::image_dim, vc::image_dim>>{
                     {1, 1}, {1, 7}, {7, 1}, {4, 9}, {9, 4}, {5, 5}}) {
                const vc::vc_image img =
                    from_rule(dims.first, dims.second, [](int x, int y) {
                        return static_cast<float>(x + (2 * y));
                    });
                const vc::vc_image gx =
                    gradient_x(img, vc_edge_policy::reflect);
                expect(gx.width() == dims.first && gx.height() == dims.second &&
                           gx.channels() == 1,
                       std::string("geometry survives ") +
                           std::to_string(dims.first) + "x" +
                           std::to_string(dims.second));
            }

            // from_rule multiplies channel c by (c+1), so a plane of 3x - 5y
            // becomes 1x, 2x and 3x that -- three DIFFERENT right answers.
            const vc::vc_image three = from_rule(
                7, 5,
                [](int x, int y) {
                    return (3.0F * static_cast<float>(x)) -
                           (5.0F * static_cast<float>(y));
                },
                3);
            const vc::vc_image gx = gradient_x(three, vc_edge_policy::clamp);
            const vc::vc_image gy = gradient_y(three, vc_edge_policy::clamp);
            expect(gx.channels() == 3, "three channels in, three out");
            bool ok = true;
            for (vc::channel_count c = 0; c < 3; ++c) {
                const auto k = static_cast<float>(c + 1);
                for (vc::image_dim y = 1; y + 1 < 5; ++y) {
                    for (vc::image_dim x = 1; x + 1 < 7; ++x) {
                        ok = ok && (pixel(gx, x, y, c) == 3.0F * k);
                        ok = ok && (pixel(gy, x, y, c) == -5.0F * k);
                    }
                }
            }
            expect(ok, "each channel filtered independently: dx = 3, 6, 9 and "
                       "dy = -5, -10, -15");
        }

        // ---- 11. rejections, PAIRED with acceptances ----------------------
        {
            vc::vc_image_writer u8{4, 4, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();
            expect(rejects([&] {
                       (void)gradient_x(not_f32, vc_edge_policy::clamp);
                   }),
                   "gradient_x rejects a non-f32 source");
            const vc::vc_image f32 =
                from_rule(4, 4, [](int, int) { return 1.0F; });
            expect(
                accepts([&] { (void)gradient_x(f32, vc_edge_policy::clamp); }),
                "gradient_x ACCEPTS an f32 source (so the rejection is not "
                "vacuous)");

            // The pairing contract: the one genuinely new failure mode here.
            const vc::vc_image a =
                from_rule(5, 5, [](int, int) { return 1.0F; });
            const vc::vc_image wrong_w =
                from_rule(6, 5, [](int, int) { return 1.0F; });
            const vc::vc_image wrong_h =
                from_rule(5, 6, [](int, int) { return 1.0F; });
            const vc::vc_image wrong_c =
                from_rule(5, 5, [](int, int) { return 1.0F; }, 3);

            // EVERY MISMATCH IS PROBED IN BOTH ARGUMENT ORDERS, and that is
            // not symmetry for its own sake. The three bad images above are
            // all LARGER than `a`, so with `a` FIRST the output is sized from
            // the smaller image and the over-long partner is merely read
            // short -- an implementation with no validation returns wrong
            // numbers and stays inside its buffers. Put the LARGER image
            // FIRST and the same implementation reads PAST THE END of the
            // shorter one: a heap-buffer overflow, which is what
            // AddressSanitizer caught on the first implementation pass while
            // this group, probing only one direction, reported four clean
            // failures and no memory error.
            //
            // So one order tests the contract and the other tests memory
            // safety. They are different tests and the suite needs both.
            //
            // AND BOTH FUNCTIONS GET THE FULL SET. The header promises
            // gradient_direction "the same rejections as above"; this group
            // used to give it one probe out of four, which is how a promise
            // becomes decoration.
            struct bad_pair {
                const vc::vc_image* dx;
                const vc::vc_image* dy;
                const char* what;
            };
            const std::vector<bad_pair> bad_pairs = {
                {&a, &wrong_w, "mismatched WIDTH, partner larger"},
                {&wrong_w, &a, "mismatched WIDTH, partner SMALLER"},
                {&a, &wrong_h, "mismatched HEIGHT, partner larger"},
                {&wrong_h, &a, "mismatched HEIGHT, partner SMALLER"},
                {&a, &wrong_c, "mismatched CHANNEL COUNT, partner larger"},
                {&wrong_c, &a, "mismatched CHANNEL COUNT, partner SMALLER"},
                {&a, &not_f32, "a non-f32 partner, second"},
                {&not_f32, &a, "a non-f32 partner, FIRST"},
            };
            for (const bad_pair& bp : bad_pairs) {
                expect(
                    rejects([&] { (void)gradient_magnitude(*bp.dx, *bp.dy); }),
                    std::string("gradient_magnitude rejects ") + bp.what);
                expect(
                    rejects([&] { (void)gradient_direction(*bp.dx, *bp.dy); }),
                    std::string("gradient_direction rejects ") + bp.what);
            }
            expect(accepts([&] { (void)gradient_magnitude(a, a); }),
                   "gradient_magnitude ACCEPTS a matching pair");
            expect(accepts([&] { (void)gradient_direction(a, a); }),
                   "gradient_direction ACCEPTS a matching pair");

            // Magnitude is never negative, and 3-4-5 is exact in f32.
            const vc::vc_image three_img =
                from_rule(3, 3, [](int, int) { return 3.0F; });
            const vc::vc_image four_img =
                from_rule(3, 3, [](int, int) { return -4.0F; });
            const vc::vc_image m = gradient_magnitude(three_img, four_img);
            expect(pixel(m, 1, 1) == 5.0F,
                   "magnitude(3, -4) == EXACTLY 5 -- and is never negative");
        }

        // ---- 12. the dumps ------------------------------------------------
        {
            // A soft-edged disc on 0..1. Chosen because it presents EVERY
            // direction at once, which is what the hue map is for, and
            // because it reproduces Szeliski figure 3.14's visual check.
            constexpr vc::image_dim n = 192;
            constexpr float k_radius = 60.0F;
            // The rim ramps from 0 to 1 over this many pixels, so ITS SLOPE
            // IS 1 / k_edge, and that is the largest gradient magnitude the
            // picture can contain. Every scale factor below is derived from
            // this number rather than fitted to the output.
            constexpr float k_edge = 6.0F;
            const vc::vc_image disc = from_rule(n, n, [](int x, int y) {
                const float fx = static_cast<float>(x) - 95.5F;
                const float fy = static_cast<float>(y) - 95.5F;
                const float r = std::sqrt((fx * fx) + (fy * fy));
                return std::clamp((k_radius - r) / k_edge, 0.0F, 1.0F);
            });

            const vc::vc_image gx = gradient_x(disc, vc_edge_policy::clamp);
            const vc::vc_image gy = gradient_y(disc, vc_edge_policy::clamp);
            const vc::vc_image mag = gradient_magnitude(gx, gy);
            const vc::vc_image dir = gradient_direction(gx, gy);

            // THE OFFSET AND SCALE ARE DERIVED, NOT CHOSEN. dx and dy are
            // signed, and the writer clamps to 0..1 -- so without a grey
            // offset every negative value becomes black and HALF THE ANSWER
            // IS DESTROYED while the picture still looks plausible.
            //
            // The positive weights of sobel_x sum to 1/2, so on input in
            // [0,1] the output lies in [-1/2, +1/2] and a bare 0.5 + dx would
            // already not clip. It would also be nearly invisible: this
            // picture's actual peak is set by the RIM'S SLOPE, 1/k_edge, far
            // below that worst case. So the gain below is k_edge.
            // The gain is k_edge, not a number chosen by looking: the rim's
            // slope is 1/k_edge, so k_edge * dx lands in [-1, +1] and
            // 0.5 + that lands in [0, 1] with the extremes exactly at black
            // and white. Nothing clips and nothing is wasted.
            auto grey_offset = [](const vc::vc_image& src) {
                vc::vc_image_writer o{src.width(), src.height(), 1,
                                      vc::buf_f32{0.0F}};
                for (vc::image_dim y = 0; y < src.height(); ++y) {
                    for (vc::image_dim x = 0; x < src.width(); ++x) {
                        o.at<vc::buf_f32>(x, y, 0) =
                            0.5F + (0.5F * k_edge * pixel(src, x, y));
                    }
                }
                return std::move(o).seal();
            };

            auto scaled = [](const vc::vc_image& src) {
                vc::vc_image_writer o{src.width(), src.height(), 1,
                                      vc::buf_f32{0.0F}};
                for (vc::image_dim y = 0; y < src.height(); ++y) {
                    for (vc::image_dim x = 0; x < src.width(); ++x) {
                        o.at<vc::buf_f32>(x, y, 0) = k_edge * pixel(src, x, y);
                    }
                }
                return std::move(o).seal();
            };

            // Hue = direction over the FULL 360 degrees, so dark-to-bright and
            // bright-to-dark come out as opposite colours rather than the
            // same one. Value = magnitude, so flat areas are black and the
            // hue is only shown where it means something.
            vc::vc_image_writer hue{n, n, 3, vc::buf_f32{0.0F}};
            for (vc::image_dim y = 0; y < n; ++y) {
                for (vc::image_dim x = 0; x < n; ++x) {
                    const float h =
                        (pixel(dir, x, y) + k_pi) / (2.0F * k_pi) * 6.0F;
                    const float v = std::min(1.0F, k_edge * pixel(mag, x, y));
                    const auto sector = static_cast<int>(h) % 6;
                    const float f = h - std::floor(h);
                    const float q = v * (1.0F - f);
                    const float t = v * f;
                    float r = 0.0F;
                    float g = 0.0F;
                    float b = 0.0F;
                    switch (sector) {
                    case 0:
                        r = v;
                        g = t;
                        break;
                    case 1:
                        r = q;
                        g = v;
                        break;
                    case 2:
                        g = v;
                        b = t;
                        break;
                    case 3:
                        g = q;
                        b = v;
                        break;
                    case 4:
                        r = t;
                        b = v;
                        break;
                    default:
                        r = v;
                        b = q;
                        break;
                    }
                    hue.at<vc::buf_f32>(x, y, 0) = r;
                    hue.at<vc::buf_f32>(x, y, 1) = g;
                    hue.at<vc::buf_f32>(x, y, 2) = b;
                }
            }

            const auto png =
                vc::io::write_config{.format = vc::io::vc_image_format::png};
            vc::io::stb_image_writer writer;
            writer.write(out_dir + "/17_disc.png", disc, png);
            writer.write(out_dir + "/17_dx.png", grey_offset(gx), png);
            writer.write(out_dir + "/17_dy.png", grey_offset(gy), png);
            writer.write(out_dir + "/17_magnitude.png", scaled(mag), png);
            writer.write(out_dir + "/17_direction_hue.png",
                         std::move(hue).seal(), png);

            std::cout
                << "\n  wrote 5 PNGs to " << out_dir << "\n"
                << "    17_dx            the rim, STRONGEST AT LEFT AND "
                   "RIGHT and fading to invisible\n"
                << "                     at the very top and bottom, where the "
                   "rim runs horizontally\n"
                << "                     and nothing changes left-to-right. "
                   "Mid-grey is zero.\n"
                << "                     LEFT edge BRIGHT (going right you "
                   "enter the disc, so\n"
                << "                     brightness RISES rightward, dx > 0); "
                   "RIGHT edge DARK.\n"
                << "                     That asymmetry IS the sign "
                   "convention. A symmetric-looking\n"
                << "                     ring means the flip is wrong.\n"
                << "    17_dy            the same picture rotated 90 degrees: "
                   "strongest at TOP and\n"
                << "                     BOTTOM, invisible at the sides. TOP "
                   "edge BRIGHT (going DOWN\n"
                << "                     you enter the disc), BOTTOM edge "
                   "DARK.\n"
                << "    17_magnitude     the complete rim, all the way round, "
                   "and no sign left.\n"
                << "    17_direction_hue one full colour wheel around the rim, "
                   "black inside and out\n"
                << "                     where the magnitude is zero and the "
                   "angle means nothing.\n"
                << "                     Opposite sides of the disc are "
                   "OPPOSITE colours, which is\n"
                << "                     the 360-degree direction doing work a "
                   "180-degree\n"
                << "                     orientation could not.\n\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "17_gradients: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "17_gradients: FAIL\n";
        return 1;
    }

    std::cout << "17_gradients: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "17_gradients: FAIL\n";
        return 1;
    }
    std::cout << "17_gradients: OK\n";
    return 0;
}
