// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 09 — an atlas of known patterns and their spectra.
//
// 08 proved spectrum_viz is correct. This one is a TRAINING SET: sixteen
// patterns whose spectra can be predicted before you look, dumped side by
// side so you can check yourself against the prediction.
//
// Each entry writes two PNGs into examples_out/:
//
//     09_<name>.png            the pattern
//     09_<name>_spectrum.png   its log-magnitude spectrum, DC centred
//
// and prints the expected reading plus the measured DC:median. Read the
// prediction, look at the picture, and only then read on.
//
// ---- THE THREE READING RULES THIS SET TEACHES ----
//
// 1. ORIENTATION. The bright pair lies along the direction in which
//    brightness CHANGES, which is PERPENDICULAR to the stripes themselves.
//    Vertical stripes give a horizontal pair. Most people guess this
//    backwards once.
//
// 2. SCALE. Distance from the centre is frequency. Coarse structure sits
//    near the middle, fine detail far out.
//
// 3. PERIODICITY vs TEXTURE. A repeating pattern gives DISCRETE DOTS. Noise
//    and texture give a HAZE. That difference is how you spot sensor
//    banding, moire, and screen-door artefacts at a glance.
//
// ---- WHAT THIS SET CANNOT TEACH, SAID OUT LOUD ----
//
// Reading what a PHOTOGRAPH IS from its magnitude spectrum. That is not a
// skill anyone has, and practice does not produce it: magnitude says how
// much change at each scale, and PHASE says where it is -- so identity lives
// in the phase, and the phase image is grain. Entries 02 and 03 below prove
// it directly: two different images, pixel-for-pixel identical spectra.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/debug/vc_spectrum_viz.h"
#include "vc/io/vc_io_stb.h"
#include "vc/math/vc_dft.h"
#include "vc/math/vc_grid2d.h"
#include "vc/pixelops/vc_convolve.h"

namespace {

constexpr vc::image_dim k_n = 128; // every pattern is 128 x 128
constexpr float k_two_pi = 6.28318530717958647692F;

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

// Build a single-channel 128x128 image from f(x, y). Values are 0..1, which
// is what the stb f32 write path expects -- it multiplies by 255.
vc::vc_image make(const std::function<float(vc::image_dim, vc::image_dim)>& f) {
    vc::vc_image_writer out{k_n, k_n, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < k_n; ++y) {
        for (vc::image_dim x = 0; x < k_n; ++x) {
            out.at<vc::buf_f32>(x, y, 0) = std::clamp(f(x, y), 0.0F, 1.0F);
        }
    }
    return std::move(out).seal();
}

float fx(vc::image_dim v) {
    return static_cast<float>(v);
}

// A grating of frequency (kx, ky) cycles across the window, lifted to 0..1 so
// it is a legal image rather than a signed field.
std::function<float(vc::image_dim, vc::image_dim)> grating(float kx, float ky) {
    return [kx, ky](vc::image_dim x, vc::image_dim y) {
        return 0.5F + 0.5F * std::cos(k_two_pi *
                                      (kx * fx(x) + ky * fx(y)) / fx(k_n));
    };
}

struct entry {
    const char* name;
    vc::vc_image image;
    const char* expect_to_see;
};

} // namespace

int main() {
    try {
        const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
        const auto png =
            vc::io::write_config{.format = vc::io::vc_image_format::png};
        vc::io::stb_image_writer writer;

        constexpr vc::image_dim c = k_n / 2;

        // ---- the photo, and the same photo blurred ----------------------
        vc::io::stb_image_reader reader;
        const vc::vc_image src = reader.read(
            std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg",
            vc::io::read_config{.dtype = vc::pixel_dtype::f32});
        const vc::image_dim x0 = (src.width() - k_n) / 2;
        const vc::image_dim y0 = (src.height() - k_n) / 2;
        const vc::vc_image photo =
            make([&](vc::image_dim x, vc::image_dim y) {
                return src.at<vc::buf_f32>(x0 + x, y0 + y, 0);
            });
        const vc::vc_image photo_blurred = vc::pixelops::convolve(
            photo, vc::pixelops::make_gaussian(3.0F, 3.0F),
            vc::pixelops::vc_edge_policy::clamp);

        std::mt19937 rng{7};
        std::uniform_real_distribution<float> uniform{0.0F, 1.0F};
        const vc::vc_image noise =
            make([&](vc::image_dim, vc::image_dim) { return uniform(rng); });

        // ---- the atlas --------------------------------------------------
        //
        // Every "expect to see" below was verified against a reference
        // transform before being written down, which is why the diagonal
        // entry says TWO dots and not four.
        std::vector<entry> atlas;
        atlas.push_back({"01_flat",
                         make([](vc::image_dim, vc::image_dim) { return 0.5F; }),
                         "ONE dot dead centre, nothing else. All the energy is "
                         "DC because there is no change anywhere."});
        atlas.push_back({"02_impulse_centre",
                         make([](vc::image_dim x, vc::image_dim y) {
                             return (x == c && y == c) ? 1.0F : 0.0F;
                         }),
                         "ENTIRELY WHITE -- a perfectly flat magnitude. Every "
                         "frequency present in equal amount. The opposite "
                         "extreme from 01."});
        atlas.push_back({"03_impulse_offcentre",
                         make([](vc::image_dim x, vc::image_dim y) {
                             return (x == 20 && y == 37) ? 1.0F : 0.0F;
                         }),
                         "IDENTICAL to 02, pixel for pixel. Moving the dot "
                         "changed nothing in the magnitude -- the position "
                         "went into the PHASE. This is the shift theorem, "
                         "visible."});
        atlas.push_back({"04_stripes_vertical_k4", make(grating(4.0F, 0.0F)),
                         "TWO dots on the HORIZONTAL axis, 4 px either side of "
                         "centre. Vertical stripes -> horizontal pair: the "
                         "pair follows the direction of CHANGE."});
        atlas.push_back({"05_stripes_vertical_k24", make(grating(24.0F, 0.0F)),
                         "The same two dots, now 24 px out. Six times finer "
                         "stripes, six times further from centre. Distance IS "
                         "frequency."});
        atlas.push_back({"06_stripes_horizontal_k4", make(grating(0.0F, 4.0F)),
                         "TWO dots on the VERTICAL axis. Same pattern rotated "
                         "90 degrees, same rotation in the spectrum."});
        atlas.push_back({"07_stripes_diagonal", make(grating(4.0F, 4.0F)),
                         "TWO dots on ONE diagonal, at (+4,+4) and (-4,-4) -- "
                         "NOT four. A single grating is one frequency and its "
                         "mirror, whatever its orientation."});
        atlas.push_back({"08_stripes_off_bin", make(grating(4.5F, 0.0F)),
                         "A LINE RIGHT ACROSS THE FRAME instead of two dots. "
                         "Compare 04, which is the same grating at k=4 and "
                         "shows three clean dots. The 4.5 version does not "
                         "complete a whole number of cycles across the window, "
                         "so it matches NO basis function exactly and leaves a "
                         "little in every one of them, brightest near 4 and 5 "
                         "and tailing off slowly. This is LEAKAGE, and it is "
                         "why windowing exists. (An earlier draft said 'energy "
                         "across 4, 5 and 6' -- the actual spread is the whole "
                         "axis.)"});
        atlas.push_back({"09_checkerboard_8",
                         make([](vc::image_dim x, vc::image_dim y) {
                             return ((x / 8 + y / 8) % 2 == 0) ? 0.0F : 1.0F;
                         }),
                         "A regular LATTICE of dots filling the frame. The "
                         "four brightest sit at (+-8, +-8) -- squares of 8 px "
                         "give a period of 16, and 128/16 = 8 -- and the rest "
                         "are harmonics marching outward on a grid. The "
                         "harmonics exist because a checkerboard has SHARP "
                         "edges; a smoothly-shaded one would show only the "
                         "four. (An earlier draft of this line said 'four dots "
                         "plus a couple of harmonics' and badly undersold what "
                         "is actually there.)"});
        atlas.push_back({"10_edge_vertical",
                         make([](vc::image_dim x, vc::image_dim) {
                             return x < c ? 0.0F : 1.0F;
                         }),
                         "A DOTTED line along the horizontal axis, brightest "
                         "near the centre and fading outward. Dotted, not "
                         "solid: half black and half white across the window "
                         "is a square wave, and a square wave has only ODD "
                         "harmonics -- bins 1, 3, 5 light up and 2, 4, 6 are "
                         "empty. One sharp edge needs every (odd) frequency in "
                         "the direction it changes. THIS is where the cross in "
                         "a natural photo's spectrum comes from."});
        atlas.push_back({"11_rectangle",
                         make([](vc::image_dim x, vc::image_dim y) {
                             const int ix = static_cast<int>(x) -
                                            static_cast<int>(c);
                             const int iy = static_cast<int>(y) -
                                            static_cast<int>(c);
                             return (std::abs(ix) < 16 && std::abs(iy) < 8)
                                        ? 1.0F
                                        : 0.0F;
                         }),
                         "A GRID of ringing lobes filling the frame, "
                         "brightest at the centre -- a sinc in each direction, "
                         "multiplied. Not just a cross: the lobes tile the "
                         "whole plane. The box is 32 wide and 16 tall, so its "
                         "lobes repeat every 128/32 = 4 bins horizontally and "
                         "every 128/16 = 8 vertically -- WIDER in space means "
                         "lobes PACKED CLOSER in frequency. Compare 13: the "
                         "ringing is what a hard-edged shape costs you."});
        atlas.push_back({"12_disc",
                         make([](vc::image_dim x, vc::image_dim y) {
                             const float dx = fx(x) - fx(c);
                             const float dy = fx(y) - fx(c);
                             return (dx * dx + dy * dy) < 400.0F ? 1.0F : 0.0F;
                         }),
                         "CONCENTRIC RINGS. The round version of 11 -- no "
                         "preferred direction, so the sinc lobes become rings."});
        atlas.push_back({"13_gaussian_blob",
                         make([](vc::image_dim x, vc::image_dim y) {
                             const float dx = fx(x) - fx(c);
                             const float dy = fx(y) - fx(c);
                             return std::exp(-(dx * dx + dy * dy) /
                                             (2.0F * 64.0F));
                         }),
                         "ANOTHER BLOB, concentrated at the centre, with NO "
                         "rings. A Gaussian transforms to a Gaussian -- and "
                         "the absence of ringing is exactly why a Gaussian "
                         "blur does not halo where a box blur does."});
        atlas.push_back({"14_noise", noise,
                         "A UNIFORM HAZE with no structure at all. Every "
                         "frequency present in roughly equal amount, which is "
                         "what makes noise broadband. Compare with 09: "
                         "repeating -> DOTS, random -> HAZE."});
        atlas.push_back({"15_photo", photo,
                         "Energy crowded near the CENTRE (photographs are "
                         "mostly broad, smooth structure) with a bright CROSS "
                         "on the axes -- which is entry 10, several times "
                         "over: the wall edge, the floor lines, and the seam "
                         "where the DFT's tiling makes the left edge abut the "
                         "right."});
        atlas.push_back({"16_photo_blurred", photo_blurred,
                         "The SAME picture with the outer region visibly "
                         "DARKENED. The blur removed the fine detail, and "
                         "'fine detail' is 'far from centre'. A low-pass "
                         "filter, watched rather than described."});

        std::cout << "\n  pattern                   DC:median   bins lost on a "
                     "LINEAR dump\n"
                  << "  ---------------------------------------------------------"
                     "------\n";

        for (const entry& e : atlas) {
            vc::math::vc_complex_signal plane(
                static_cast<std::size_t>(k_n) * k_n);
            for (vc::image_dim y = 0; y < k_n; ++y) {
                for (vc::image_dim x = 0; x < k_n; ++x) {
                    plane[static_cast<std::size_t>(y) * k_n + x] =
                        vc::math::vc_complex{e.image.at<vc::buf_f32>(x, y, 0),
                                             0.0F};
                }
            }
            const vc::math::grid2d g = vc::math::grid2d::checked(
                vc::math::vc_complex_view{plane}, k_n, k_n, "09_spectrum_atlas");
            const vc::debug::spectrum_dump d =
                vc::debug::spectrum_viz(vc::math::dft2d(plane, g), g);

            writer.write(out_dir + "/09_" + e.name + ".png", e.image, png);
            writer.write(out_dir + "/09_" + e.name + "_spectrum.png",
                         d.log_magnitude, png);

            std::cout << "  " << std::left << std::setw(26) << e.name
                      << std::right << std::setw(10) << std::fixed
                      << std::setprecision(1) << d.dc_to_median << "   "
                      << std::setw(5) << std::setprecision(1)
                      << (d.linear_zero_fraction * 100.0F) << " %\n";

            expect(d.log_magnitude.width() == k_n &&
                       d.log_magnitude.height() == k_n,
                   "every atlas entry dumps a full-size spectrum");
        }

        std::cout << "\n  WHAT TO EXPECT, entry by entry — read BEFORE looking"
                     "\n  ----------------------------------------------------\n";
        for (const entry& e : atlas) {
            std::cout << "\n  " << e.name << "\n      " << e.expect_to_see
                      << '\n';
        }

        std::cout << "\n  " << atlas.size() * 2 << " PNGs in " << out_dir
                  << "\n  Start with 02 and 03: two different pictures, one "
                     "identical spectrum.\n";

    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "\n09_spectrum_atlas: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "09_spectrum_atlas: FAIL\n";
        return 1;
    }
    std::cout << "09_spectrum_atlas: OK\n";
    return 0;
}
