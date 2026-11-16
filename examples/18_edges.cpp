// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only
//
// 18_edges -- S1. Threshold a gradient-magnitude map. That is the whole toy.
//
// WHAT THIS EXAMPLE IS REALLY FOR, because the function is four lines and
// would not otherwise earn a file: TWO OF ITS DUMPS ARE THE EVIDENCE FOR A
// LIMITATION THAT IS OTHERWISE JUST PROSE. P3's section B asks `edges` for a
// "documented limitation (no NMS/hysteresis)", and a limitation you have been
// TOLD about is a sentence, while one you have SEEN is a reason. So:
//
//   IMAGE A, high contrast, uniform.  The ring survives all the way round --
//   and it is SEVERAL PIXELS THICK. Group 6 asserts the thickness rather
//   than remarking on it, because that ridge is precisely what non-maximum
//   suppression exists to thin.
//
//   IMAGE B, the SAME disc over a background whose brightness RAMPS. The
//   boundary is physically identical all the way round; only the contrast
//   across it falls. One global threshold keeps the high-contrast side and
//   DROPS THE REST, cutting a closed contour open. That is the picture
//   hysteresis's second threshold answers: the faint arc is CONNECTED to the
//   bright one, and a per-pixel rule cannot use that. What hysteresis needs
//   is CONNECTEDNESS -- a flood fill from the strong pixels through the weak
//   ones -- not a chain data structure. See vc_gradients.h.
//
// Every number in image B is hand-checkable and is derived below rather than
// fitted to the output.
//
// WHAT IS NOT TESTED, deliberately: the exact magnitude on a DISCRETISED
// circle's rim. A curved rim presents every orientation at once, and Sobel is
// anisotropic by about 6% between 0 and 45 degrees (see 17_gradients group
// 7), so a per-pixel equality there would be pinning the aliasing of a
// rasterised circle. The exact assertions all run on flat or ramped images
// where the answer is a number; the disc is used for STRUCTURAL claims (is
// the ring closed, is it broken, how thick is it) and for the pictures.

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/io/vc_io_stb.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_gradients.h"

namespace {

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
    for (vc::channel_count c = 0; c < ch; ++c) {
        for (vc::image_dim y = 0; y < h; ++y) {
            for (vc::image_dim x = 0; x < w; ++x) {
                out.at<vc::buf_f32>(x, y, c) =
                    f(static_cast<int>(x), static_cast<int>(y)) +
                    static_cast<float>(c);
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

// Count ON pixels whose x falls in [x0, x1). The band, not a single pixel,
// because a rasterised circle's rim wanders by a pixel and a point probe
// would be pinning that wander rather than the phenomenon.
int count_on_in_band(const vc::vc_image& e,
                     vc::image_dim x0,
                     vc::image_dim x1) {
    int n = 0;
    for (vc::image_dim y = 0; y < e.height(); ++y) {
        for (vc::image_dim x = x0; x < x1; ++x) {
            if (pixel(e, x, y) == 1.0F) {
                ++n;
            }
        }
    }
    return n;
}

// The longest run of consecutive ON pixels along one row. This is the ridge
// thickness, and it is the NMS motivation expressed as a number.
int longest_run_in_row(const vc::vc_image& e, vc::image_dim y) {
    int best = 0;
    int run = 0;
    for (vc::image_dim x = 0; x < e.width(); ++x) {
        run = (pixel(e, x, y) == 1.0F) ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

constexpr vc::image_dim k_n = 192; // dump image side
constexpr float k_radius = 60.0F;  // disc radius, pixels
constexpr float k_centre = 95.5F;  // so the disc is centred on 192
constexpr float k_edge = 6.0F;     // rim ramp width, pixels

// THE THRESHOLD IS DERIVED, NOT FITTED. A rim that ramps over k_edge pixels
// has slope 1/k_edge, so a FULL-CONTRAST rim (step of 1.0) peaks at
// 1/6 = 0.1667. Half of that is the threshold: comfortably below the
// full-contrast ring and comfortably above the ramp background of image B.
constexpr float k_threshold = 0.5F / k_edge; // 0.08333...

// Image B's background spans 0 -> k_bg_span left to right.
constexpr float k_bg_span = 0.85F;

float disc_coverage(int x, int y) {
    const float fx = static_cast<float>(x) - k_centre;
    const float fy = static_cast<float>(y) - k_centre;
    const float r = std::sqrt((fx * fx) + (fy * fy));
    return std::clamp((k_radius - r) / k_edge, 0.0F, 1.0F);
}

vc::vc_image threshold_of(const vc::vc_image& src, float t) {
    using namespace vc::pixelops;
    const vc::vc_image gx = gradient_x(src, vc_edge_policy::clamp);
    const vc::vc_image gy = gradient_y(src, vc_edge_policy::clamp);
    return edge_map(gradient_magnitude(gx, gy), t);
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
        // ---- 1. geometry, dtype and the binary contract -------------------
        {
            const vc::vc_image m = from_rule(
                7, 5, [](int x, int) { return static_cast<float>(x) / 10.0F; });
            const vc::vc_image e = edge_map(m, 0.25F);
            expect(e.width() == 7 && e.height() == 5 && e.channels() == 1,
                   "edge_map: geometry and channel count follow the input");
            expect(accepts([&] { (void)e.at<vc::buf_f32>(0, 0, 0); }),
                   "edge_map: the output is f32");

            bool binary = true;
            for (vc::image_dim y = 0; y < e.height(); ++y) {
                for (vc::image_dim x = 0; x < e.width(); ++x) {
                    const float v = pixel(e, x, y);
                    if (v != 0.0F && v != 1.0F) {
                        binary = false;
                    }
                }
            }
            expect(binary, "edge_map: every output value is EXACTLY 0.0 or "
                           "1.0, nothing between");
        }

        // ---- 2. the comparison is STRICTLY GREATER ------------------------
        {
            // Values {0, 0.25, 0.5} against a threshold of 0.25, so the
            // middle pixel sits EXACTLY ON it and must come out OFF.
            const vc::vc_image m =
                from_rule(3, 1, [](int x, int) { return 0.25F * x; });
            const vc::vc_image e = edge_map(m, 0.25F);
            expect(pixel(e, 0, 0) == 0.0F, "below the threshold is OFF");
            expect(pixel(e, 1, 0) == 0.0F,
                   "EXACTLY AT the threshold is OFF -- the comparison is "
                   "strictly greater (see the header)");
            expect(pixel(e, 2, 0) == 1.0F, "above the threshold is ON");

            // The degenerate case the convention was chosen for: a flat
            // image has magnitude exactly 0, and threshold 0 must leave it
            // dark rather than lighting the whole frame.
            const vc::vc_image flat =
                from_rule(5, 5, [](int, int) { return 0.0F; });
            const vc::vc_image none = edge_map(flat, 0.0F);
            expect(count_on_in_band(none, 0, 5) == 0,
                   "threshold 0 on a FLAT map marks nothing -- the reason the "
                   "comparison is > and not >=");
        }

        // ---- 3. raising the threshold can only REMOVE pixels --------------
        {
            const vc::vc_image m =
                from_rule(5, 1, [](int x, int) { return 0.1F * (x + 1); });
            const vc::vc_image lo = edge_map(m, 0.15F);
            const vc::vc_image hi = edge_map(m, 0.35F);
            expect(count_on_in_band(lo, 0, 5) == 4,
                   "threshold 0.15 on {0.1 .. 0.5} leaves 4 on");
            expect(count_on_in_band(hi, 0, 5) == 2,
                   "threshold 0.35 on {0.1 .. 0.5} leaves 2 on");

            bool subset = true;
            for (vc::image_dim x = 0; x < 5; ++x) {
                if (pixel(hi, x, 0) == 1.0F && pixel(lo, x, 0) != 1.0F) {
                    subset = false;
                }
            }
            expect(subset, "the high-threshold set is a SUBSET of the "
                           "low-threshold set -- monotone, so a flipped "
                           "comparison cannot hide");
        }

        // ---- 4. channels are thresholded independently --------------------
        {
            // from_rule adds the channel index, so channel c holds c + x/10.
            const vc::vc_image m = from_rule(
                4, 1, [](int x, int) { return static_cast<float>(x) / 10.0F; },
                3);
            const vc::vc_image e = edge_map(m, 0.95F);
            expect(pixel(e, 0, 0, 0) == 0.0F && pixel(e, 3, 0, 0) == 0.0F,
                   "channel 0 (0.0 .. 0.3) is entirely below 0.95");
            expect(pixel(e, 0, 0, 1) == 1.0F,
                   "channel 1 (1.0 .. 1.3) is entirely above 0.95");
            expect(pixel(e, 0, 0, 2) == 1.0F,
                   "channel 2 (2.0 .. 2.3) is entirely above 0.95");
            expect(e.channels() == 3, "three channels in, three out");
        }

        // ---- 5. rejections, PAIRED with acceptances -----------------------
        {
            const vc::vc_image m =
                from_rule(4, 4, [](int, int) { return 0.5F; });
            vc::vc_image_writer u8{4, 4, 1, vc::buf_u8{0}};
            const vc::vc_image not_f32 = std::move(u8).seal();

            expect(rejects([&] { (void)edge_map(not_f32, 0.5F); }),
                   "edge_map rejects a non-f32 magnitude");
            expect(rejects([&] { (void)edge_map(m, -0.1F); }),
                   "edge_map rejects a NEGATIVE threshold -- magnitude is "
                   "never negative, so it would mark every pixel");
            expect(rejects([&] {
                       (void)edge_map(m,
                                      std::numeric_limits<float>::quiet_NaN());
                   }),
                   "edge_map rejects a NaN threshold -- every comparison "
                   "would be false and it would mark none");
            expect(rejects([&] {
                       (void)edge_map(m,
                                      std::numeric_limits<float>::infinity());
                   }),
                   "edge_map rejects an infinite threshold");
            expect(accepts([&] { (void)edge_map(m, 0.0F); }),
                   "edge_map ACCEPTS a threshold of 0 (so the rejection is "
                   "not vacuous)");
            expect(accepts([&] { (void)edge_map(m, 0.5F); }),
                   "edge_map ACCEPTS an ordinary threshold");
        }

        // ---- 6. IMAGE A -- high contrast. The ring closes, and it is THICK -
        {
            const vc::vc_image disc = from_rule(
                k_n, k_n, [](int x, int y) { return disc_coverage(x, y); });
            const vc::vc_image e = threshold_of(disc, k_threshold);

            // The rim's slope is 1/k_edge and the threshold is half of it, so
            // every part of the ring clears it regardless of orientation --
            // the 6% anisotropy is nowhere near a factor of two.
            const vc::image_dim q = k_n / 4;
            expect(count_on_in_band(e, 0, q) > 0 &&
                       count_on_in_band(e, k_n - q, k_n) > 0,
                   "image A: the ring survives on BOTH sides -- a closed "
                   "contour, which is what uniform contrast buys");

            expect(pixel(e, k_n / 2, k_n / 2) == 0.0F,
                   "image A: the disc INTERIOR is off (flat, so magnitude 0)");
            expect(pixel(e, 1, 1) == 0.0F,
                   "image A: the far background is off");

            // THE LIMITATION, ASSERTED. The rim ramps over k_edge = 6 pixels,
            // so the magnitude ridge is about that wide and thresholding
            // keeps all of it.
            //
            // AND THE RIDGE HERE IS A PLATEAU, NOT A PEAK -- worth knowing
            // before reading too much into it. disc_coverage() is LINEAR in
            // the radius, and the derivative of a linear ramp is CONSTANT, so
            // the top of this ridge is flat: measured along row 95, four
            // consecutive pixels sit at 0.16665x, agreeing to five decimals
            // and differing only in float noise.
            //
            // WHICH MEANS NMS WOULD NOT CLEANLY THIN *THIS* IMAGE. Under a
            // strict >, a plateau has no winner, and whichever pixel survives
            // is decided by rounding in the sixth decimal. A real edge is a
            // SMOOTHED step rather than a linear one, and its derivative is a
            // genuine bump with a single maximum -- that is the shape NMS is
            // built for.
            //
            // So this image honestly demonstrates THICKNESS, which is what is
            // asserted below. It does not demonstrate thinnability, and the
            // suite does not claim it does.
            const int thickness = longest_run_in_row(e, k_n / 2);
            expect(thickness >= 3,
                   "image A: the edge is a RIDGE at least 3 pixels thick, not "
                   "a line -- this is the no-NMS limitation, measured");
            expect(thickness <= static_cast<int>(k_edge) + 3,
                   "image A: ... and no thicker than the rim ramp plus the "
                   "kernel's reach, so the thickness has a known cause");
        }

        // ---- 7. IMAGE B -- falling contrast cuts the SAME contour open -----
        {
            // Same disc. The only change is a background that ramps, so the
            // STEP ACROSS THE RIM shrinks from left to right while the
            // boundary itself is unchanged.
            //
            //   background at x:   k_bg_span * x / (k_n - 1)
            //   step at the rim:   1 - background
            //   magnitude:         step / k_edge
            //
            //   left rim,  x ~ 36:  bg ~ 0.158 -> step 0.842 -> mag ~ 0.140
            //   right rim, x ~ 155: bg ~ 0.692 -> step 0.308 -> mag ~ 0.051
            //   threshold                                        0.083
            //
            // So the left rim clears the threshold comfortably and the right
            // rim misses it comfortably. The ramp ITSELF has slope
            // k_bg_span / (k_n - 1) = 0.0045, which is 18x below the
            // threshold -- so the background contributes nothing and the only
            // thing on trial is the ring.
            //
            // THE TWO RIM MAGNITUDES ABOVE ARE APPROXIMATE. They are worked at
            // the NOMINAL rim pixel, and the actual peak sits a pixel or so
            // away where the background differs slightly. The margins were
            // chosen wide precisely so that does not matter; the figures are
            // an estimate of the right size, not a prediction to the third
            // decimal. The 0.0045 is exact -- vc_gradients.h's contract is
            // that a ramp of slope a reports magnitude a.
            const vc::vc_image ramped = from_rule(k_n, k_n, [](int x, int y) {
                const float bg = k_bg_span * static_cast<float>(x) /
                                 static_cast<float>(k_n - 1);
                const float t = disc_coverage(x, y);
                return bg + t * (1.0F - bg);
            });
            const vc::vc_image e = threshold_of(ramped, k_threshold);

            const vc::image_dim q = k_n / 4;
            const int left = count_on_in_band(e, 0, q);
            const int right = count_on_in_band(e, k_n - q, k_n);

            expect(left > 0,
                   "image B: the HIGH-contrast side of the ring survives");
            expect(right == 0,
                   "image B: the LOW-contrast side of the SAME physical "
                   "boundary is GONE -- one global threshold cut a closed "
                   "contour open. This is the hysteresis motivation, and it "
                   "is a picture, not a claim");

            // And the background ramp registered nothing, which is what makes
            // the previous assertion about the ring rather than the backdrop.
            expect(pixel(e, 5, 5) == 0.0F && pixel(e, k_n - 5, 5) == 0.0F,
                   "image B: the background RAMP itself is below threshold at "
                   "both ends, so it is not what is being measured");

            // Lowering the threshold brings the lost arc back. That is the
            // other half of the fragility: there is no single right answer,
            // only a trade, which is exactly why Canny uses two numbers.
            const vc::vc_image low = threshold_of(ramped, k_threshold / 2.0F);
            expect(count_on_in_band(low, k_n - q, k_n) > 0,
                   "image B: HALVING the threshold recovers the lost arc -- "
                   "so the boundary was always there and the threshold, not "
                   "the image, decided");
        }

        // ---- 8. the dumps -------------------------------------------------
        {
            const auto png =
                vc::io::write_config{.format = vc::io::vc_image_format::png};
            vc::io::stb_image_writer writer;

            const vc::vc_image disc = from_rule(
                k_n, k_n, [](int x, int y) { return disc_coverage(x, y); });
            const vc::vc_image ramped = from_rule(k_n, k_n, [](int x, int y) {
                const float bg = k_bg_span * static_cast<float>(x) /
                                 static_cast<float>(k_n - 1);
                const float t = disc_coverage(x, y);
                return bg + t * (1.0F - bg);
            });

            writer.write(out_dir + "/18_source_uniform.png", disc, png);
            writer.write(out_dir + "/18_source_ramped.png", ramped, png);
            writer.write(out_dir + "/18_edges_uniform.png",
                         threshold_of(disc, k_threshold), png);
            writer.write(out_dir + "/18_edges_ramped.png",
                         threshold_of(ramped, k_threshold), png);

            std::cout
                << "\n  wrote 18_source_uniform.png, 18_source_ramped.png,\n"
                << "        18_edges_uniform.png, 18_edges_ramped.png\n"
                << "\n  WHAT TO LOOK AT, in this order:\n"
                << "    18_edges_uniform  the ring is CLOSED -- and look how\n"
                << "                      THICK it is. That band is what\n"
                << "                      non-maximum suppression thins to a\n"
                << "                      one-pixel crest. It is not a bug;\n"
                << "                      it is the operator being honest\n"
                << "                      about a blurred edge.\n"
                << "    18_edges_ramped   the SAME ring, over a background\n"
                << "                      that brightens left to right. The\n"
                << "                      right-hand arc is simply MISSING.\n"
                << "                      Nothing about the boundary changed;\n"
                << "                      only the contrast across it did.\n"
                << "                      One number cannot serve both ends,\n"
                << "                      and that is why Canny carries two.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "18_edges: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "18_edges: FAIL\n";
        return 1;
    }

    std::cout << "18_edges: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "18_edges: FAIL\n";
        return 1;
    }
    std::cout << "18_edges: OK\n";
    return 0;
}
