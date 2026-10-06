// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 08 — look at a spectrum.
//
// 07 established that dft2d is correct. This one turns its output into
// pictures, which is the first time the frequency domain stops being a list of
// numbers.
//
// The deliverable is THREE PNGs from one spectrum:
//
//     08_spectrum_log.png      log(1 + |F|), normalised
//     08_spectrum_linear.png   |F|, normalised, no log
//     08_spectrum_phase.png    atan2(Im, Re)
//
// Open the first two side by side. That comparison is the entire argument for
// the log and it settles faster than any explanation.
//
// WHAT IS ASSERTED HERE, AND WHAT IS ONLY PRINTED
//
// Asserted: facts with closed forms -- the exact index mapping of the shift,
// where DC lands, where a known cosine's bright pair lands, and that the phase
// spans more than atan() could produce.
//
// Printed: DC:median and the linear-display zero fraction. Those depend on the
// image, so there is no right answer to assert -- and a threshold copied from
// somewhere else is worse than no threshold. Read them off your own picture.
//
// The off-centre impulse phase RAMP is already asserted in 07 and is not
// repeated.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/io/vc_io_stb.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"
#include "vc/pixelops/vc_spectrum_viz.h"

namespace {

int passed = 0;
int total = 0;

void expect(bool condition, const char* what) {
    ++total;
    if (condition) {
        ++passed;
    } else {
        std::cerr << "FAILED: " << what << '\n';
    }
}

using vc::math::complex_signal;
using vc::math::grid2d;
using vc::math::vc_complex;

constexpr float k_pi = std::numbers::pi_v<float>;
constexpr float k_two_pi = 2.0F * k_pi;

// A plane whose every element is DISTINCT, so a shift that loses or duplicates
// an element cannot pass by coincidence.
complex_signal numbered(std::uint32_t w, std::uint32_t h) {
    complex_signal plane(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < plane.size(); ++i) {
        plane[i] = vc_complex{static_cast<float>(i + 1), 0.0F};
    }
    return plane;
}

float px(const vc::vc_image& img, vc::image_dim x, vc::image_dim y) {
    return img.at<vc::buf_f32>(x, y, 0);
}

} // namespace

int main() {
    try {
        // ---- 1. the shift's exact index mapping -------------------------
        //
        // out[(x + W/2) % W, (y + H/2) % H] = in[x, y], integer division.
        // Checked on 5x3 -- ODD on both axes, where the rounding direction is
        // a real choice -- and on 4x2, where it is not.
        for (auto [w, h] : {std::pair<std::uint32_t, std::uint32_t>{5, 3},
                            std::pair<std::uint32_t, std::uint32_t>{4, 2}}) {
            const complex_signal in = numbered(w, h);
            const complex_signal out = vc::math::fftshift2d(
                in, grid2d::checked(vc::math::complex_view{in}, w, h,
                                    "08_spectrum_viz"));

            bool mapped = out.size() == in.size();
            for (std::uint32_t y = 0; y < h && mapped; ++y) {
                for (std::uint32_t x = 0; x < w && mapped; ++x) {
                    const std::size_t src = static_cast<std::size_t>(y) * w + x;
                    const std::size_t dst =
                        static_cast<std::size_t>((y + h / 2) % h) * w +
                        ((x + w / 2) % w);
                    mapped = out[dst] == in[src];
                }
            }
            expect(mapped, w == 5 ? "fftshift2d index map, 5x3 (odd)"
                                  : "fftshift2d index map, 4x2 (even)");
        }

        // ---- 2. DC lands in the middle ----------------------------------
        //
        // The point of the whole operation, asserted directly: mark bin 0 and
        // find it at (W/2, H/2) afterwards, with nothing else non-zero.
        {
            constexpr std::uint32_t w = 5;
            constexpr std::uint32_t h = 3;
            complex_signal in(static_cast<std::size_t>(w) * h,
                              vc_complex{0.0F, 0.0F});
            in[0] = vc_complex{1.0F, 0.0F}; // the DC bin, marked

            const complex_signal out = vc::math::fftshift2d(
                in, grid2d::checked(vc::math::complex_view{in}, w, h,
                                    "08_spectrum_viz"));

            const std::size_t centre =
                static_cast<std::size_t>(h / 2) * w + (w / 2);
            bool only_centre = out.size() == in.size() &&
                               out[centre] == vc_complex{1.0F, 0.0F};
            for (std::size_t i = 0; i < out.size() && only_centre; ++i) {
                if (i != centre) {
                    only_centre = out[i] == vc_complex{0.0F, 0.0F};
                }
            }
            expect(only_centre, "fftshift2d puts DC at (W/2, H/2)");
        }

        // ---- 3. shifting twice: identity at even N, NOT at odd ----------
        //
        // Two shifts move by 2*(N/2): exactly N when N is even, N-1 when it is
        // odd. Both halves are asserted because getting the rounding backwards
        // would break one and not the other.
        {
            auto twice = [](std::uint32_t w, std::uint32_t h) {
                const complex_signal in = numbered(w, h);
                const grid2d g = grid2d::checked(vc::math::complex_view{in}, w,
                                                 h, "08_spectrum_viz");
                return std::pair{in, vc::math::fftshift2d(
                                         vc::math::fftshift2d(in, g), g)};
            };
            const auto [even_in, even_out] = twice(4, 2);
            const auto [odd_in, odd_out] = twice(5, 3);
            expect(even_out == even_in,
                   "shifting twice is the identity at even N");
            expect(odd_out != odd_in,
                   "shifting twice is NOT the identity at odd N");
        }

        // ---- 4. a known cosine lands at centre +- (kx, ky) --------------
        //
        // NON-SQUARE and varying along x only, which is what makes this catch
        // a transposed axis: 8 wide by 4 high, two cycles across x.
        //
        //   the y-sum kills every ky except 0, so energy sits at (2,0) and
        //   (6,0) with magnitude H*W/2 = 16 each, and DC is ZERO because a
        //   whole number of cycles has zero mean.
        //
        //   after the shift by (4, 2):   (2,0) -> (6,2)
        //                                (6,0) -> (2,2)
        //                                (0,0) -> (4,2)   <- the centre, dark
        {
            constexpr std::uint32_t w = 8;
            constexpr std::uint32_t h = 4;
            constexpr std::uint32_t kx = 2;

            complex_signal plane(static_cast<std::size_t>(w) * h);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const float v = std::cos(k_two_pi * static_cast<float>(kx) *
                                             static_cast<float>(x) /
                                             static_cast<float>(w));
                    plane[static_cast<std::size_t>(y) * w + x] =
                        vc_complex{v, 0.0F};
                }
            }
            const grid2d g = grid2d::checked(vc::math::complex_view{plane}, w,
                                             h, "08_spectrum_viz");
            const vc::pixelops::spectrum_dump dump =
                vc::pixelops::spectrum_viz(vc::math::dft2d(plane, g), g);

            const vc::image_dim cx = w / 2;
            const vc::image_dim cy = h / 2;
            const vc::vc_image& mag = dump.linear_magnitude;

            expect(mag.width() == w && mag.height() == h &&
                       mag.channels() == 1,
                   "spectrum_viz keeps the extent, one channel");

            // Both members of the pair are the maximum, so both read 1.0 after
            // normalising by the max.
            expect(px(mag, cx + kx, cy) > 0.99F && px(mag, cx - kx, cy) > 0.99F,
                   "cosine's bright pair sits at centre +- (kx, 0)");

            // DC is zero here, so the centre is DARK. A transposed shift would
            // put a bright spot here instead.
            expect(px(mag, cx, cy) < 0.01F,
                   "centre is dark: a whole number of cycles has zero mean");

            // Everything off the kx axis is zero. This is the half that
            // actually catches a swapped pair of axes.
            bool elsewhere_dark = true;
            for (vc::image_dim y = 0; y < h && elsewhere_dark; ++y) {
                for (vc::image_dim x = 0; x < w && elsewhere_dark; ++x) {
                    const bool is_pair = (y == cy) && (x == cx + kx || x == cx - kx);
                    if (!is_pair) {
                        elsewhere_dark = px(mag, x, y) < 0.01F;
                    }
                }
            }
            expect(elsewhere_dark, "no energy anywhere but the pair");

            // f32 pixels must be 0..1 -- the stb write path multiplies by 255.
            bool in_unit_range = true;
            for (vc::image_dim y = 0; y < h && in_unit_range; ++y) {
                for (vc::image_dim x = 0; x < w && in_unit_range; ++x) {
                    in_unit_range = px(dump.log_magnitude, x, y) >= 0.0F &&
                                    px(dump.log_magnitude, x, y) <= 1.0F &&
                                    px(dump.phase, x, y) >= 0.0F &&
                                    px(dump.phase, x, y) <= 1.0F;
                }
            }
            expect(in_unit_range, "all three images are in 0.0 .. 1.0");
        }

        // ---- 5. the atan trap, as an assertion --------------------------
        //
        // Smith's Nuisance 3: atan(im/re) collapses two quadrants, and the
        // symptom is that the phase never passes +-1.5708. Here is that
        // symptom turned into a test.
        //
        // An impulse at x=1 on an 8-wide row gives phases -45k degrees, which
        // after wrapping span -180 .. +135. Mapped to 0..1 that is a RANGE of
        // 0.875. atan() would cap the phases at +-90, giving a range of 0.5.
        // The threshold of 0.6 sits between with room on both sides.
        {
            constexpr std::uint32_t w = 8;
            constexpr std::uint32_t h = 1;
            complex_signal plane(w, vc_complex{0.0F, 0.0F});
            plane[1] = vc_complex{1.0F, 0.0F};

            const grid2d g = grid2d::checked(vc::math::complex_view{plane}, w,
                                             h, "08_spectrum_viz");
            const vc::pixelops::spectrum_dump dump =
                vc::pixelops::spectrum_viz(vc::math::dft2d(plane, g), g);

            float lo = 2.0F;
            float hi = -1.0F;
            for (vc::image_dim x = 0; x < w; ++x) {
                lo = std::min(lo, px(dump.phase, x, 0));
                hi = std::max(hi, px(dump.phase, x, 0));
            }
            std::cout << "  [atan2 check] phase range " << lo << " .. " << hi
                      << "  (atan would give 0.25 .. 0.75)\n";
            expect(hi - lo > 0.6F,
                   "phase spans more than atan() can produce (use atan2)");
        }

        // ---- 6. a real image: PRINT the diagnostics, write the PNGs ------
        //
        // 128x128 from the centre, one channel. Sized from the timing table in
        // the P2 doc -- 128 squared costs about 0.14 s in a debug build, which
        // is fine under ctest; 512 squared costs about 8 s, which is not.
        {
            const vc::io::path input =
                std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
            vc::io::stb_image_reader reader;
            vc::io::stb_image_writer writer;

            const vc::vc_image src = reader.read(
                input, vc::io::read_config{.dtype = vc::pixel_dtype::f32});

            const vc::image_dim side =
                std::min<vc::image_dim>(128, std::min(src.width(), src.height()));
            const vc::image_dim x0 = (src.width() - side) / 2;
            const vc::image_dim y0 = (src.height() - side) / 2;

            complex_signal plane(static_cast<std::size_t>(side) * side);
            for (vc::image_dim y = 0; y < side; ++y) {
                for (vc::image_dim x = 0; x < side; ++x) {
                    plane[static_cast<std::size_t>(y) * side + x] =
                        vc_complex{src.at<vc::buf_f32>(x0 + x, y0 + y, 0),
                                   0.0F};
                }
            }

            const grid2d g = grid2d::checked(vc::math::complex_view{plane},
                                             side, side, "08_spectrum_viz");
            const vc::pixelops::spectrum_dump dump =
                vc::pixelops::spectrum_viz(vc::math::dft2d(plane, g), g);

            std::cout << "\n  " << side << " x " << side
                      << " centre crop, channel 0\n"
                      << "    DC magnitude          " << dump.dc_magnitude
                      << "\n    median magnitude      "
                      << dump.median_magnitude
                      << "\n    DC : median           " << dump.dc_to_median
                      << "\n    linear bins at grey 0 "
                      << (dump.linear_zero_fraction * 100.0F) << " %\n";

            const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
            const auto png = vc::io::write_config{
                .format = vc::io::vc_image_format::png};
            writer.write(out_dir + "/08_spectrum_log.png", dump.log_magnitude,
                         png);
            writer.write(out_dir + "/08_spectrum_linear.png",
                         dump.linear_magnitude, png);
            writer.write(out_dir + "/08_spectrum_phase.png", dump.phase, png);

            std::cout << "\n  wrote, and the first two are the point:\n"
                      << "    " << out_dir << "/08_spectrum_log.png\n"
                      << "    " << out_dir << "/08_spectrum_linear.png\n"
                      << "    " << out_dir << "/08_spectrum_phase.png\n"
                      << "\n  OPEN THE LOG AND LINEAR ONES SIDE BY SIDE.\n";

            expect(dump.log_magnitude.width() == side &&
                       dump.phase.width() == side,
                   "real image: all three dumps keep the crop size");
        }

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        return 1;
    }

    std::cout << "\n08_spectrum_viz: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "08_spectrum_viz: FAIL\n";
        return 1;
    }
    std::cout << "08_spectrum_viz: OK\n";
    return 0;
}
