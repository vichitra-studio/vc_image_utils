// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 13_laplacian -- acceptance checks for the discrete Laplacian (5-point stencil).
//
// P2 section C's S6: the stencil as a vc_kernel, applied through convolve().
// Reasoning and the sign convention are in include/vc/pixelops/vc_laplacian.h.
//
// ---- EVERY GATE HERE IS AN EXACT EQUALITY, NOT A TOLERANCE ----
//
// The weights are integers and every test input is a small integer (or an
// exactly representable fraction), so every expected result is an exact f32 in
// any summation order. There is no rounding to absorb and therefore no
// tolerance to justify -- which is the same reason X2's x^2+y^2 gate is an
// equality on paper. A tolerance here would be a place for a real error to hide.
//
// ---- WHAT EACH GROUP CAN AND CANNOT CATCH ----
//
// 1   the kernel itself. Dimensions, the nine weights, sum() == 0 exactly, and
//     reversed() == itself. Cheap, and it fails first when the weights are
//     wrong, so the later failures are not noise from one root cause.
//
// 2   THE IMPULSE, AND IT IS THE STRONGEST CHECK IN THE FILE. convolve(delta,h)
//     reproduces h, so this reads all nine weights back out of a real
//     convolution, in place. It catches every typo, the sign flip, any scale
//     error, the 8-neighbour substitute, and clamping-at-zero (the -4 has to
//     come back as -4). Groups 3-6 are then DEMONSTRATIONS of properties, not
//     independent detectors -- the honest framing, since each is weaker.
//
// 3   the three paper rows, reproduced exactly: flat 0, ramp 0, bowl 2. Run on
//     a 5x3 whose rows are identical, so the y second difference is 0 and what
//     is measured is purely the x one. The RAMP is the interesting one.
//
// 4   constant. Catches weights that do not sum to zero -- a wrong centre, a
//     missing arm. Blind to overall SIGN and SCALE (negate everything and a
//     constant still gives 0). Includes `zero`'s exact three-tier boundary.
//
// 5   plane. Catches an ASYMMETRIC kernel: a missing or offset arm survives
//     group 4 if the weights still total zero, but not a plane. Also blind to
//     sign and scale.
//
// 6   x^2 + y^2 -> 4, the X2 gate. The only one of 3-6 that sees SIGN (a
//     negated stencil gives -4) and SCALE (doubled gives 8, the 8-neighbour
//     stencil gives 12).
//
// 7   sum of all output == 0 exactly under `wrap`, on a deterministic integer
//     pattern. Circular convolution totals multiply: sum(x*h) = sum(x).sum(h),
//     and sum(h) = 0. S7 depends on this fact.
//
// 8   1x1, where EVERY pixel is a boundary pixel and nothing is interior.
//
// 9   three channels, filtered independently. A planar/interleaved mix-up
//     agrees with itself at one channel, which is how it survived week 6.
//
// 10  the rejection, PAIRED WITH AN ACCEPTANCE. Unpaired, a function that threw
//     for everything would pass it -- which is exactly what happened in week 6
//     against the throwing stub.
//
// NOT TESTED, deliberately: a transposed stencil. The 5-point stencil is its
// own transpose, so that is not a reachable bug and no input can distinguish it.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/pixelops/vc_convolve.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_laplacian.h"

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
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

// Built from a rule rather than a literal list, so the EXPECTATION below can be
// stated as a rule too and the two cannot drift.
vc::vc_image from_rule(vc::image_dim w,
                       vc::image_dim h,
                       const std::function<float(int, int)>& f) {
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(w) * h);
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            v.push_back(f(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    return make_image(w, h, 1, v);
}

float pixel(const vc::vc_image& img,
            vc::image_dim x,
            vc::image_dim y,
            vc::channel_count c = 0) {
    return img.at<vc::buf_f32>(x, y, c);
}

// Exact equality, on purpose. See the note at the top of this file.
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

// Accumulated in double so the TOTAL is exact for integer-valued samples; the
// per-pixel values are already exact f32.
double total(const vc::vc_image& img) {
    double acc = 0.0;
    img.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px) {
        for (const vc::buf_f32 v : px) {
            acc += static_cast<double>(v);
        }
    });
    return acc;
}

// A vc_exception carrying invalid_argument -- NOT merely "something threw".
// The stub throws std::logic_error precisely so this cannot pass before the
// real rejection exists.
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

// Looped assertions below run the same check under several policies. Without a
// name in the message a failure says only that one of them broke, which is the
// kind of diagnostic that turns a two-minute fix into a bisect.
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

} // namespace

int main() {
    using vc::pixelops::convolve;
    using vc::pixelops::laplacian;
    using vc::pixelops::make_laplacian;
    using vc::pixelops::vc_edge_policy;
    using vc::pixelops::vc_kernel;

    int passed = 0;
    int total_checks = 0;
    auto expect = [&](bool ok, const char* what) {
        ++total_checks;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // ---- 1. the kernel itself ------------------------------------------
        {
            const vc_kernel k = make_laplacian();
            expect(k.width() == 3 && k.height() == 3, "stencil is 3x3");

            const std::vector<float> want{0.0F, 1.0F, 0.0F, 1.0F, -4.0F,
                                          1.0F, 0.0F, 1.0F, 0.0F};
            bool weights_match = k.weights().size() == want.size();
            for (std::size_t i = 0; weights_match && i < want.size(); ++i) {
                weights_match = (k.weights()[i] == want[i]);
            }
            expect(weights_match, "weights are exactly {0,1,0, 1,-4,1, 0,1,0}");

            // Exactly zero, not nearly. A derivative annihilates the average,
            // and this is the FIRST factory here that does -- box and Gaussian
            // both sum to 1.
            expect(k.sum() == 0.0F, "weights sum to EXACTLY zero");

            // Symmetric, so convolve and correlate coincide on it.
            const vc_kernel rev = k.reversed();
            bool same = true;
            for (std::size_t i = 0; i < want.size(); ++i) {
                same = same && (rev.weights()[i] == k.weights()[i]);
            }
            expect(same, "the stencil is its own 180-degree rotation");
        }

        // ---- 2. the impulse: reads all nine weights back out ---------------
        {
            // A single 1 at the centre of a 5x5 of zeros. convolve(delta, h)
            // reproduces h, centred where the impulse was. `zero` because the
            // surround is zero anyway, so no policy can affect the answer.
            const vc::vc_image delta = from_rule(5, 5, [](int x, int y) {
                return (x == 2 && y == 2) ? 1.0F : 0.0F;
            });
            const vc::vc_image out = laplacian(delta, vc_edge_policy::zero);

            expect(out.width() == 5 && out.height() == 5 && out.channels() == 1,
                   "impulse response keeps the source geometry");

            // The expected picture, written out in full:
            //
            //     0  0  0  0  0
            //     0  0  1  0  0
            //     0  1 -4  1  0
            //     0  0  1  0  0
            //     0  0  0  0  0
            bool exact = true;
            for (vc::image_dim y = 0; y < 5; ++y) {
                for (vc::image_dim x = 0; x < 5; ++x) {
                    const int dx = static_cast<int>(x) - 2;
                    const int dy = static_cast<int>(y) - 2;
                    float want = 0.0F;
                    if (dx == 0 && dy == 0) {
                        want = -4.0F;
                    } else if (std::abs(dx) + std::abs(dy) == 1) {
                        want = 1.0F;
                    }
                    if (pixel(out, x, y) != want) {
                        std::cerr << "  impulse (" << x << ',' << y << ") got "
                                  << pixel(out, x, y) << " want " << want
                                  << '\n';
                        exact = false;
                    }
                }
            }
            expect(exact,
                   "impulse response IS the stencil, sign and scale included");
        }

        // ---- 3. the three paper rows: flat 0, ramp 0, bowl 2 ---------------
        {
            // 5 wide, 3 tall, every row identical. Constant down y, so the y
            // second difference is 0 and what survives is purely the x one --
            // which makes this the 1-D hand calculation, run through the 2-D
            // operator. Interior is x in 1..3 at y == 1.
            struct row_case {
                const char* name;
                std::vector<float> row;
                float want;
            };
            const std::vector<row_case> cases{
                {"flat row 5,5,5,5,5 gives 0", {5, 5, 5, 5, 5}, 0.0F},
                {"RAMP row 2,4,6,8,10 gives 0 -- bend, not slope",
                 {2, 4, 6, 8, 10},
                 0.0F},
                {"bowl row 4,1,0,1,4 gives 2", {4, 1, 0, 1, 4}, 2.0F},
            };

            for (const row_case& rc : cases) {
                const vc::vc_image src = from_rule(5, 3, [&](int x, int) {
                    return rc.row[static_cast<std::size_t>(x)];
                });
                const vc::vc_image out = laplacian(src, vc_edge_policy::clamp);
                expect(interior_all_exactly(out, rc.want), rc.name);
            }
        }

        // ---- 4. a constant image -------------------------------------------
        {
            const float k = 0.25F; // exactly representable
            const vc::vc_image src =
                from_rule(7, 5, [&](int, int) { return k; });

            // clamp, reflect and wrap all redirect an out-of-range tap onto a
            // REAL pixel, which on a constant image is k. So the result is 0
            // everywhere, boundary included.
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::reflect,
                  vc_edge_policy::wrap}) {
                const vc::vc_image out = laplacian(src, p);
                bool all_zero = true;
                for (vc::image_dim y = 0; y < out.height(); ++y) {
                    for (vc::image_dim x = 0; x < out.width(); ++x) {
                        all_zero = all_zero && (pixel(out, x, y) == 0.0F);
                    }
                }
                const std::string what =
                    std::string("constant gives exactly 0 EVERYWHERE under ") +
                    policy_name(p);
                expect(all_zero, what.c_str());
            }

            // `zero` invents a 0 outside, so the boundary loses weight in an
            // exactly computable way:
            //     interior  4 of 4 neighbours   4k - 4k =   0
            //     edge      3 of 4              3k - 4k =  -k
            //     corner    2 of 4              2k - 4k = -2k
            {
                const vc::vc_image out = laplacian(src, vc_edge_policy::zero);
                expect(interior_all_exactly(out, 0.0F),
                       "constant under `zero`: interior is still exactly 0");
                expect(pixel(out, 0, 0) == -2.0F * k &&
                           pixel(out, 6, 0) == -2.0F * k &&
                           pixel(out, 0, 4) == -2.0F * k &&
                           pixel(out, 6, 4) == -2.0F * k,
                       "constant under `zero`: every corner is exactly -2k");
                expect(pixel(out, 3, 0) == -k && pixel(out, 3, 4) == -k &&
                           pixel(out, 0, 2) == -k && pixel(out, 6, 2) == -k,
                       "constant under `zero`: every edge is exactly -k");
            }
        }

        // ---- 5. a plane: 3x + 5y -------------------------------------------
        {
            // Different coefficients per axis on purpose, so the two axes are
            // not interchangeable in the input even though the stencil is.
            const vc::vc_image src = from_rule(7, 7, [](int x, int y) {
                return (3.0F * static_cast<float>(x)) +
                       (5.0F * static_cast<float>(y));
            });
            const vc::vc_image out = laplacian(src, vc_edge_policy::clamp);
            expect(interior_all_exactly(out, 0.0F),
                   "a plane 3x+5y gives exactly 0 in the interior");
        }

        // ---- 6. THE X2 GATE: x^2 + y^2 -> 4 --------------------------------
        {
            // Hand-checked at (2,3): the neighbours are 18, 10, 20, 8 and the
            // centre is 13, so 18+10+20+8 - 4(13) = 56 - 52 = 4. The calculus
            // route gives 2 + 2 = 4. The second difference of x^2 is exactly 2
            // with no error term, so this is an EQUALITY and holds at every
            // interior pixel, not just the one checked on paper.
            const vc::vc_image src = from_rule(9, 9, [](int x, int y) {
                return static_cast<float>((x * x) + (y * y));
            });
            const vc::vc_image out = laplacian(src, vc_edge_policy::clamp);
            expect(interior_all_exactly(out, 4.0F),
                   "the X2 gate: Laplacian of x^2+y^2 is EXACTLY 4 everywhere "
                   "in the interior");
        }

        // ---- 7. under `wrap`, the output totals exactly zero ---------------
        {
            // A deterministic integer pattern, not noise, so the total is an
            // exact integer and a failure is reproducible from this source
            // alone. sum(x * h) = sum(x) . sum(h) for CIRCULAR convolution, and
            // sum(h) is 0, so the total must be 0 for any input whatsoever.
            const vc::vc_image src = from_rule(16, 12, [](int x, int y) {
                return static_cast<float>(((7 * x) + (13 * y)) % 17);
            });
            const vc::vc_image out = laplacian(src, vc_edge_policy::wrap);
            expect(total(out) == 0.0,
                   "under `wrap` the output sums to EXACTLY zero");

            // Under `zero` the total is NOT zero, and the amount is exactly
            // computable rather than merely "different". Count it the same way:
            // the centre tap always lands in range and contributes -4S. Each of
            // the four neighbour taps lands in range everywhere EXCEPT along
            // one border line, so the four together contribute 4S minus the
            // four border lines. The S cancels and what survives is
            //
            //     total = -( sum of column 0     + sum of column W-1
            //              + sum of row 0        + sum of row H-1 )
            //
            // with the four corners counted TWICE -- which is the -2k corner
            // against -k edge of group 4, now on a non-constant input.
            const vc::vc_image zeroed = laplacian(src, vc_edge_policy::zero);
            const auto rule = [](int x, int y) {
                return static_cast<double>(((7 * x) + (13 * y)) % 17);
            };
            double border = 0.0;
            for (int y = 0; y < 12; ++y) {
                border += rule(0, y) + rule(15, y);
            }
            for (int x = 0; x < 16; ++x) {
                border += rule(x, 0) + rule(x, 11);
            }
            expect(total(zeroed) == -border,
                   "under `zero` the total is EXACTLY minus the border sum "
                   "(corners counted twice)");
        }

        // ---- 8. a 1x1 image, where nothing is interior ---------------------
        {
            const vc::vc_image one =
                from_rule(1, 1, [](int, int) { return 3.0F; });
            // clamp/reflect/wrap: all four neighbours redirect onto the single
            // pixel itself, so 4(3) - 4(3) = 0.
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::reflect,
                  vc_edge_policy::wrap}) {
                const vc::vc_image out = laplacian(one, p);
                // reflect on a 1-wide image is a 3-tap kernel wider than
                // its source -- the double-bounce case vc_convolve.h flags as
                // "not reachable with the small kernels this phase builds".
                // It is reachable here, and it is checked rather than assumed.
                const std::string what =
                    std::string("1x1 gives exactly 0 under ") + policy_name(p);
                expect(out.width() == 1 && out.height() == 1 &&
                           pixel(out, 0, 0) == 0.0F,
                       what.c_str());
            }
            // `zero`: no neighbour is in range at all, so 0 - 4(3) = -12.
            const vc::vc_image out = laplacian(one, vc_edge_policy::zero);
            expect(pixel(out, 0, 0) == -12.0F,
                   "1x1 under `zero` is exactly -12");
        }

        // ---- 9. three channels, independently -------------------------------
        {
            // Channel 0 is a bowl in x (expect 2), channel 1 is a ramp
            // (expect 0), channel 2 is the bowl scaled by 10 (expect 20). The
            // three expected values are all DIFFERENT, so a channel-index slip
            // cannot land on the right answer by accident.
            const std::vector<float> row{4, 1, 0, 1, 4};
            std::vector<float> v;
            for (vc::image_dim y = 0; y < 3; ++y) {
                for (vc::image_dim x = 0; x < 5; ++x) {
                    const float b = row[x];
                    v.push_back(b);
                    v.push_back(2.0F * static_cast<float>(x));
                    v.push_back(10.0F * b);
                }
            }
            const vc::vc_image src = make_image(5, 3, 3, v);
            const vc::vc_image out = laplacian(src, vc_edge_policy::clamp);

            expect(out.channels() == 3, "three channels in, three out");
            bool ok = true;
            for (vc::image_dim x = 1; x < 4; ++x) {
                ok = ok && (pixel(out, x, 1, 0) == 2.0F);
                ok = ok && (pixel(out, x, 1, 1) == 0.0F);
                ok = ok && (pixel(out, x, 1, 2) == 20.0F);
            }
            expect(ok, "each channel filtered independently: bowl 2, ramp 0, "
                       "scaled bowl 20");
        }

        // ---- 10. rejection, PAIRED with acceptance -------------------------
        {
            vc::vc_image_writer u8{4, 4, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();
            expect(rejects([&] {
                       (void)laplacian(not_f32, vc_edge_policy::clamp);
                   }),
                   "a non-f32 source is rejected with invalid_argument");

            const vc::vc_image f32 =
                from_rule(4, 4, [](int, int) { return 1.0F; });
            expect(
                accepts([&] { (void)laplacian(f32, vc_edge_policy::clamp); }),
                "an f32 source is ACCEPTED (so the rejection above is not "
                "vacuous)");

            // laplacian() must BE convolve() with the stencil, not a parallel
            // implementation that could drift from it.
            const vc::vc_image structured = from_rule(9, 7, [](int x, int y) {
                return static_cast<float>(((5 * x) + (3 * y)) % 11);
            });
            for (const vc_edge_policy p :
                 {vc_edge_policy::clamp, vc_edge_policy::zero,
                  vc_edge_policy::reflect, vc_edge_policy::wrap}) {
                const vc::vc_image a = laplacian(structured, p);
                const vc::vc_image b =
                    convolve(structured, make_laplacian(), p);
                bool agrees = true;
                for (vc::image_dim y = 0; y < a.height(); ++y) {
                    for (vc::image_dim x = 0; x < a.width(); ++x) {
                        agrees = agrees && (pixel(a, x, y) == pixel(b, x, y));
                    }
                }
                const std::string what =
                    std::string(
                        "laplacian() == convolve(stencil) bit for bit under ") +
                    policy_name(p);
                expect(agrees, what.c_str());
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "13_laplacian: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "13_laplacian: FAIL\n";
        return 1;
    }

    std::cout << "13_laplacian: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "13_laplacian: FAIL\n";
        return 1;
    }
    std::cout << "13_laplacian: OK\n";
    return 0;
}
