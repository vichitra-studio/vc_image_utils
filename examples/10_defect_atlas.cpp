// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 10 — what defects look like in the frequency domain.
//
// 09 taught the alphabet on clean patterns. This one is the reason to learn
// it: six real artefacts, each with a signature you can recognise at a
// glance. This is what the spectrum is actually FOR in a pipeline.
//
// Each entry dumps the picture, its log-magnitude spectrum and its phase.
//
// EVERY SIGNATURE BELOW WAS MEASURED AGAINST A REFERENCE TRANSFORM BEFORE
// BEING WRITTEN DOWN. Two of my predictions were wrong on the way -- the
// staircase ones give NULLS where I expected peaks -- so nothing here is
// quoted from memory.
//
// ---- THE ONE RULE THAT EXPLAINS FOUR OF THE SIX ----
//
// From notes section 7e: A BLOCK OF L CONSECUTIVE PIXELS CONTRIBUTES EXACTLY
// ZERO AT BIN k WHENEVER k.L/N IS A WHOLE NUMBER. So any structure built out
// of blocks of width L shows NULLS every N/L bins -- and that runs backwards:
//
//     READ THE FIRST NULL, AND L = N / (first null).
//
// That is a real measurement technique, not a curiosity. It is how you
// recover a motion-blur length from a photograph you did not take.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
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

float fx(vc::image_dim v) {
    return static_cast<float>(v);
}

template <typename F>
vc::vc_image make(vc::image_dim n, F f) {
    vc::vc_image_writer out{n, n, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < n; ++y) {
        for (vc::image_dim x = 0; x < n; ++x) {
            out.at<vc::buf_f32>(x, y, 0) = std::clamp(f(x, y), 0.0F, 1.0F);
        }
    }
    return std::move(out).seal();
}

// Keep every other pixel. No pre-blur, which is the point of entry 01.
vc::vc_image decimate2(const vc::vc_image& src) {
    const vc::image_dim n = src.width() / 2;
    return make(n, [&](vc::image_dim x, vc::image_dim y) {
        return src.at<vc::buf_f32>(x * 2, y * 2, 0);
    });
}

struct entry {
    const char* name;
    vc::vc_image image;
    const char* signature;
};

} // namespace

int main() {
    try {
        const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);
        const auto png =
            vc::io::write_config{.format = vc::io::vc_image_format::png};
        vc::io::stb_image_writer writer;

        constexpr vc::image_dim n = 128;

        // A FINE grating: 50 cycles across 128 samples. Decimating by 2 leaves
        // 64 samples, whose highest representable frequency is 32 -- so 50
        // cannot survive, and comes back as 64 - 50 = 14.
        const vc::vc_image fine =
            make(n, [](vc::image_dim x, vc::image_dim) {
                return 0.5F + 0.5F * std::cos(k_two_pi * 50.0F * fx(x) / 128.0F);
            });
        const vc::vc_image preblurred = vc::pixelops::convolve(
            fine, vc::pixelops::make_gaussian(2.0F, 2.0F),
            vc::pixelops::vc_edge_policy::wrap);

        std::mt19937 rng{3};
        std::uniform_real_distribution<float> uniform{0.0F, 1.0F};
        const vc::vc_image rough =
            make(n, [&](vc::image_dim, vc::image_dim) { return uniform(rng); });
        const vc::vc_image photo = vc::pixelops::convolve(
            rough, vc::pixelops::make_gaussian(4.0F, 4.0F),
            vc::pixelops::vc_edge_policy::wrap);

        std::vector<entry> atlas;

        atlas.push_back(
            {"01_aliased", decimate2(fine),
             "TWO DOTS AT 14, NOT 50. The 50-cycle grating was thrown away and "
             "something that was never in the scene appeared in its place. "
             "64 - 50 = 14: the frequency FOLDED. That is aliasing, and the "
             "picture itself looks like a perfectly innocent coarse grating -- "
             "the spectrum is where you can tell it is a lie."});

        atlas.push_back(
            {"02_antialiased", decimate2(preblurred),
             "ALMOST NOTHING but the centre dot. The pre-blur removed the "
             "50-cycle content BEFORE decimating, so there was nothing left to "
             "fold. Correct: the fine detail is genuinely gone rather than "
             "disguised as something coarse. Measured peak energy after the "
             "pre-blur is about 1% of DC, against a full-strength pair in 01."});

        atlas.push_back(
            {"03_row_banding",
             make(n,
                  [&](vc::image_dim x, vc::image_dim y) {
                      return photo.at<vc::buf_f32>(x, y, 0) +
                             0.1F * std::cos(k_two_pi * 32.0F * fx(y) / 128.0F);
                  }),
             "A BRIGHT PAIR AT (0, +-32) sitting on top of the photo's own "
             "haze. Periodic sensor noise -- mains hum, readout interference, "
             "a rolling shutter beat. VERTICAL axis because the banding varies "
             "with y. This is the single most common thing anyone goes to a "
             "spectrum to find: a repeating defect gives DISCRETE DOTS where "
             "the photograph itself only ever gives haze, so it stands out "
             "even when it is invisible to the eye in the picture. Measured "
             "in the dump: that bin reads 190 against a local background of "
             "0 -- an isolated spike with nothing around it."});

        atlas.push_back(
            {"04_motion_blur",
             // Averaged by hand rather than through make_box: vc_kernel
             // requires ODD dimensions, and at N = 128 only an EVEN block
             // length gives nulls on exact integer bins (k.L/N whole needs L
             // to divide 128, whose divisors are all even but 1). So the
             // cleanest demonstration of the block rule is not expressible
             // through the kernel API.
             make(n,
                  [&](vc::image_dim x, vc::image_dim y) {
                      float sum = 0.0F;
                      for (vc::image_dim i = 0; i < 16; ++i) {
                          sum += photo.at<vc::buf_f32>((x + i) % n, y, 0);
                      }
                      return sum / 16.0F;
                  }),
             "DARK VERTICAL BANDS at kx = +-8, +-16, +-24 ... The blur is a "
             "box 16 px long, so by the BLOCK RULE it contributes zero "
             "whenever k.16/128 is whole, i.e. every 8 bins. Read it "
             "BACKWARDS: first null at 8 gives L = 128/8 = 16, which recovers "
             "the blur length from the picture alone. That is how PSF "
             "estimation starts.\n      HONEST LIMIT, measured: only the FIRST "
             "null is readable here. kx=8 reads 0 against a neighbour of 21, "
             "but at kx=16 and beyond both the null AND its neighbours read 0 "
             "-- a smooth photograph has almost no energy that far out, so "
             "there is nothing left for the blur to null. The technique needs "
             "an image with fine detail to bite on."});

        atlas.push_back(
            {"05_clipped",
             make(n,
                  [](vc::image_dim x, vc::image_dim) {
                      return std::min(0.5F + 0.4F * std::cos(k_two_pi * 8.0F *
                                                             fx(x) / 128.0F),
                                      0.8F);
                  }),
             "EXTRA DOTS AT 16, 24, 32, 40 that were not in the signal. The "
             "input is ONE pure tone at 8. Flattening its peaks at 0.8 made "
             "the wave non-sinusoidal, and the spectrum had to invent "
             "harmonics to describe the new shape. Clipping CREATES frequency "
             "content -- which is why a blown highlight is not just a lost "
             "highlight, and why it cannot be undone by scaling back down."});

        atlas.push_back(
            {"06_quantised_8_levels",
             make(n,
                  [](vc::image_dim x, vc::image_dim) {
                      return std::floor(fx(x) / 128.0F * 8.0F) / 8.0F;
                  }),
             "A COMB WITH NULLS every 8 bins. A smooth ramp crushed to 8 "
             "levels is a STAIRCASE, and each tread is a block 16 px wide: "
             "k.16/128 whole means nulls at 8, 16, 24 ... (I predicted peaks "
             "there and measured nulls -- the tread's own block rule wins.) "
             "This is posterisation, the banding you see in smooth skies, and "
             "the null spacing tells you the step width: 128/8 = 16."});

        std::cout << "\n  defect                    DC:median   bins lost on a "
                     "LINEAR dump\n"
                  << "  ---------------------------------------------------------"
                     "------\n";

        for (const entry& e : atlas) {
            const vc::image_dim w = e.image.width();
            vc::math::vc_complex_signal plane(static_cast<std::size_t>(w) * w);
            for (vc::image_dim y = 0; y < w; ++y) {
                for (vc::image_dim x = 0; x < w; ++x) {
                    plane[static_cast<std::size_t>(y) * w + x] =
                        vc::math::vc_complex{e.image.at<vc::buf_f32>(x, y, 0),
                                             0.0F};
                }
            }
            const vc::math::grid2d g = vc::math::grid2d::checked(
                vc::math::vc_complex_view{plane}, w, w, "10_defect_atlas");
            const vc::debug::spectrum_dump d =
                vc::debug::spectrum_viz(vc::math::dft2d(plane, g), g);

            writer.write(out_dir + "/10_" + e.name + ".png", e.image, png);
            writer.write(out_dir + "/10_" + e.name + "_spectrum.png",
                         d.log_magnitude, png);
            writer.write(out_dir + "/10_" + e.name + "_phase.png", d.phase, png);

            std::cout << "  " << std::left << std::setw(26) << e.name
                      << std::right << std::setw(10) << std::fixed
                      << std::setprecision(1) << d.dc_to_median << "   "
                      << std::setw(5) << std::setprecision(1)
                      << (d.linear_zero_fraction * 100.0F) << " %\n";

            expect(d.log_magnitude.width() == w,
                   "every defect entry dumps a full-size spectrum");
        }

        std::cout << "\n  WHAT TO LOOK FOR\n  ----------------\n";
        for (const entry& e : atlas) {
            std::cout << "\n  " << e.name << "\n      " << e.signature << '\n';
        }

        std::cout << "\n  " << atlas.size() * 3 << " PNGs in " << out_dir
                  << "\n  Compare 01 against 02 first: same scene, one "
                     "pre-blur, and the difference is a\n  frequency that was "
                     "never there.\n";

    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "\n10_defect_atlas: " << passed << " / " << total
              << " checks passed\n";
    if (passed != total) {
        std::cout << "10_defect_atlas: FAIL\n";
        return 1;
    }
    std::cout << "10_defect_atlas: OK\n";
    return 0;
}
