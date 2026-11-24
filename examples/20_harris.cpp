// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// S3 -- the Harris corner detector, and its acceptance suite.
//
// THE IMPLEMENTATION IS A STUB UNTIL THE LEARNER WRITES IT. That is not a
// defect. This file PROBES for the stub and exits 0 with a notice, so that
// ctest stays green rather than going permanently red -- P3 section H.5.10
// records what a permanently red ctest costs: it teaches you to ignore a red
// ctest. Once the bodies exist, every gate below runs.
//
// WHAT THIS CHECKS, AND WHY EACH GATE EXISTS. The contract lives in
// vc_harris.h. Each group here exists because some SPECIFIC wrong
// implementation would otherwise pass, and the comment says which one. A gate
// that cannot fail is worth nothing however green it is -- P3 section H.3,
// which is week 9's lesson after S0's dtype validation passed vacuously.
//
// ============================================================================
// WHICH OF THESE GATES CAN ACTUALLY FAIL
// ============================================================================
//
// Writing a gate is not the same as proving it bites. The gates below hold for
// every implementation that reads a pixel at all, so passing them is evidence
// of nothing -- they are kept as executable statements of the contract, beside
// the gates that do bite, and they are not counted as verification.
//
// CANNOT FAIL, because something upstream already enforces it:
//   the four non-f32 gates     vc_image::at<T>() routes through as<T>(), which
//                              re-checks the stored dtype on every typed read,
//                              so any read at all rejects a u8 input
//   window_sigma 0, negative   make_gaussian_1d_* rejects these itself. It
//   and NaN                    does NOT reject +Inf cleanly, which is why the
//                              +Inf gate DOES bite -- see that gate's comment
//   one half of each mismatch  vc_image_info::index() bounds-checks, so for
//   pair (dims and channels,   ANY loop order one orientation of each pair
//   dx/dy and a/b/c)           throws without any validation being written.
//                              Which half that is depends on which extent the
//                              implementation walks, which is why both are
//                              here; see group 4's comment
//   TINY: 2x2                  a width of 2 has no interior column, so neither
//                              a correct loop nor an underflowing one reads
//                              anything. Kept as the boundary companion to
//                              1x1, which does bite
//
// ASSERTS THE FIXTURE, NOT THE IMPLEMENTATION:
//   ROTATED: contains interior junctions -- computed from fixture geometry
//
// WOULD CATCH A GROSS ERROR, BUT NOTHING SUBTLE:
//   DIAGONAL: Det ~= 0 and |C| comparable to A, FLAT and SCALE: |R|
//   negligible in a flat interior (every window_sigma), SPIKE: exactly one
//   peak and the reported response, PEAKS: both found, MULTI and TENSOR
//   geometry/channel count. These document how the fixtures are built. Treat
//   them as documentation, not as evidence.
//
// A NOTE ON WHY THE SWEEPS NEEDED COMPANION GATES. Sweeping a parameter and
// checking signs proves the signs are ROBUST to it. It does not prove the
// parameter is USED -- the two claims are different, and a sweep over an
// invariant can only ever show the first. Mutation testing caught both cases
// here: versions that discarded the caller's k, and the caller's
// window_sigma, passed every sign gate in this file. The two "the argument is
// not ignored" gates exist for that reason, and any future sweep added to
// this file needs the same companion.
// ============================================================================
//
// WHAT IS DELIBERATELY NOT GATED, so the absences are not read as oversights:
//
//   THE GRADIENT SIGN. Every entry of M is a product of two gradients, so a
//     global flip cancels (vc_harris.h trap 3, and P3 section F.2a). There is
//     no signed value here to assert. gradient_x/gradient_y DO assert one;
//     this operator genuinely cannot see the difference.
//
//   C's SIGN. Same reason -- C enters R only as C^2.
//
//   RESPONSE MAGNITUDES ACROSS A ROTATION. They drift a few percent, and that
//     is correct (trap 5). Group 5 asserts LOCATIONS and nothing else.
//
//   THE EXACT NUMBER OF PEAKS PER JUNCTION. A step-edge checkerboard puts its
//     junctions BETWEEN pixel centres, so the response peak is a 2x2 near-tie
//     that strict > can resolve either way depending on float ordering. Group
//     5 therefore gates "at least one peak near each junction" plus a loose
//     global ceiling, not an exact count -- a count gate would be flaky for
//     reasons that have nothing to do with correctness.
//
//   ANY HAND-COMPUTED VALUE OF A, B OR C. Those are the learner's to work, and
//     P3 section H.5.10's standing lesson for this phase is not to work them
//     here. Every gate below is a SIGN, a REJECTION, or a COUNT -- decided by
//     the fixture's construction, never by arithmetic done on the learner's
//     behalf.
//
// THE k SWEEP, WHICH IS THE POINT OF GROUPS 1 AND 2. The learner picks k and
// the threshold by reasoning -- that is the spec'd task. So no gate may depend
// on which value they pick: every sign gate runs across a BAND of k spanning
// the usable range, and passes for all of it. The learner's own choice appears
// only in group 6, which reports and fails nothing.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/io/vc_io_stb.h"
#include "vc/pixelops/vc_edge_policy.h"
#include "vc/pixelops/vc_gradients.h"
#include "vc/pixelops/vc_harris.h"

namespace {

using vc::pixelops::vc_edge_policy;

int g_passed = 0;
int g_skipped = 0;
int g_failed = 0;

bool check(bool condition, const std::string& what) {
    if (condition) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

void report(const std::string& what) {
    std::cout << "  . " << what << '\n';
}

vc::vc_image from_rule(vc::image_dim w,
                       vc::image_dim h,
                       const std::function<float(int, int)>& f) {
    vc::vc_image_writer out{w, h, 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < h; ++y) {
        for (vc::image_dim x = 0; x < w; ++x) {
            out.at<vc::buf_f32>(x, y, 0) =
                f(static_cast<int>(x), static_cast<int>(y));
        }
    }
    return std::move(out).seal();
}

vc::vc_image from_rule_ch(vc::image_dim w,
                          vc::image_dim h,
                          vc::channel_count ch,
                          const std::function<float(int, int)>& f) {
    vc::vc_image_writer out{w, h, ch, vc::buf_f32{0.0F}};
    for (vc::channel_count c = 0; c < ch; ++c) {
        for (vc::image_dim y = 0; y < h; ++y) {
            for (vc::image_dim x = 0; x < w; ++x) {
                out.at<vc::buf_f32>(x, y, c) =
                    f(static_cast<int>(x), static_cast<int>(y));
            }
        }
    }
    return std::move(out).seal();
}

vc::vc_image from_rule_c(vc::image_dim w,
                         vc::image_dim h,
                         vc::channel_count ch,
                         const std::function<float(int, int, int)>& f) {
    vc::vc_image_writer out{w, h, ch, vc::buf_f32{0.0F}};
    for (vc::channel_count c = 0; c < ch; ++c) {
        for (vc::image_dim y = 0; y < h; ++y) {
            for (vc::image_dim x = 0; x < w; ++x) {
                out.at<vc::buf_f32>(x, y, c) =
                    f(static_cast<int>(x), static_cast<int>(y),
                      static_cast<int>(c));
            }
        }
    }
    return std::move(out).seal();
}

float pixel_c(const vc::vc_image& img,
              vc::image_dim x,
              vc::image_dim y,
              vc::channel_count c) {
    return img.at<vc::buf_f32>(x, y, c);
}

float pixel(const vc::vc_image& img, vc::image_dim x, vc::image_dim y) {
    return img.at<vc::buf_f32>(x, y, 0);
}

// A STUB'S THROW IS NOT AN ANSWER TO ANY QUESTION. Both local handlers below
// catch std::exception -- and that is also what an unwritten function throws.
// Without this test, "you have not written this yet" is reported as "your
// validation is wrong" on every rejection gate at once, and the learner goes
// hunting for a defect that does not exist. Rethrowing instead lets
// run_group() file it where it belongs, as a skip.
bool is_unwritten(const std::exception& e) {
    return std::string(e.what()).find("is NOT IMPLEMENTED") !=
           std::string::npos;
}

bool rejects(const std::function<void()>& f) {
    try {
        f();
    } catch (const vc::vc_exception& e) {
        return e.code() == vc::vc_error_code::invalid_argument;
    } catch (const std::exception& e) {
        if (is_unwritten(e)) {
            throw;
        }
        return false; // a non-vc_exception throw is never a rejection
    }
    return false;
}

// ---- the fixtures ---------------------------------------------------------
//
// THE GEOMETRY IS THE TEST. Every expected sign below is decided by how these
// are CONSTRUCTED, not by evaluating the operator and believing the answer.

// GEOMETRY, AND WHY IT GREW. Both of these were 96 and 16, sized when
// sigma_d was 0 and the only smoothing was the window at 1.0. The operator now
// reaches 10 px from any sample point -- the derivative's 4 (3 for the
// Gaussian at sigma_d = 1, 1 for the difference) plus the window's
// ceil(3 * sigma_i) = 6 -- and a 16 px square gave a band centre only 8 px of
// clearance to the nearest transition. So NO POINT IN A 16 px CHECKERBOARD IS
// FLAT with respect to this operator, and group 1's FLAT gate was detecting
// exactly that: Tr at the band centre came to 0.78 against 470 at the
// junction, a ratio of 1.7e-3 against a 1e-3 bound.
//
// Measured, at sigma_d = 1 and sigma_i = 2, Tr(flat) / Tr(corner):
//
//     k_square = 16, clearance  8 px  ->  1.7e-03     over the bound
//     k_square = 24, clearance 12 px  ->  0           exactly zero
//
// 24 px squares give 12 px of clearance, which clears the 10 px reach, so the
// bound did not have to move and the flat interior is EXACTLY zero rather
// than merely small.
//
// k_dim GREW WITH IT so group 5's rotated census stays worth counting. That
// census walks a 5x5 block of pattern-space junctions, rotates them into the
// frame, and keeps those at least k_rot_margin inside it: at 144 that leaves
// 20 of the 25. At 96 with 24 px squares it leaves 9, and the count gate
// would be resting on single digits.
constexpr vc::image_dim k_dim = 144;
constexpr int k_square = 24; // checkerboard square, in pixels

// THE THREE SAMPLE POINTS EVERY SIGN GATE USES, DERIVED RATHER THAN SPELLED.
// Transitions sit at t == k_square/2 (mod k_square) and band centres at
// t == 0 (mod k_square), so these followed k_square silently when it was 16
// and had to be found by grep when it changed. Written as expressions they
// cannot drift again.
constexpr vc::image_dim k_t = // a transition: on the junction/edge
    static_cast<vc::image_dim>((k_square * 2) + (k_square / 2));
constexpr vc::image_dim k_c = // a band centre: flat interior
    static_cast<vc::image_dim>(k_square * 3);
// THE INTEGRATION SCALE every fixture uses when it is not sweeping it. 2.0,
// which is Schmid, Mohr and Bauckhage's figure, and it is also the only value
// here that is NOT free to choose.
//
// IT WAS 1.0, AND THAT WAS A DEFECT -- the third one that widening sigma_d
// from 0 to 1 exposed, after MULTI's exact zero and ROTATED's region
// mismatch. With sigma_i == sigma_d the integration window adds no averaging
// the derivative had not already done, which vc_harris.h's own
// "window_sigma > sigma_d" paragraph describes as a BAD DETECTOR, and the
// checkerboard showed precisely the predicted failure: R at the junction
// pixel became a LOCAL MINIMUM with four equal maxima at the diagonal
// neighbours. Those four are 2 px apart, so strict non-max suppression
// legitimately keeps all of them, and group 6 reported 144 peaks for 36
// junctions -- a correct implementation scoring 4x on a misconfigured
// fixture. Measured across sigma_i, holding sigma_d = 1, ON THE OLD 96/16
// BOARD -- which is where the defect was found, and which had 36 junctions
// just as the 144/24 board does:
//
//     sigma_i   peaks   on the junction   on the (+-1,+-1) ring
//       1.0      144           0                  144
//       1.5       36          36                    0
//       2.0       36          36                    0
//       3.0       36          36                    0
//
// RAISING IT MOVES EVERY OTHER FIXTURE, which is why the sweep lives in group
// 1b rather than here: total reach is now the derivative's 4 px (3 for the
// Gaussian at sigma_d = 1, 1 for the difference) plus the window's
// ceil(3 * sigma_i) = 6, so 10 px from any sample point. The flat-interior
// gates have 8 px of clearance to the nearest transition and their tolerances
// were set at sigma_i = 1; k_rot_margin = 12 still clears 10 with room.
constexpr float k_window_sigma = 2.0F;
// THE DERIVATIVE SCALE, which structure_tensor() now owns -- it takes the
// source image, builds its own isotropic derivative-of-Gaussian, and so has a
// sigma_d argument that the gradient-pair version could not have had. Schmid,
// Mohr and Bauckhage report 1 for this one, so unlike k_window_sigma above
// this value IS the literature's, and it is still not a default: the header
// declines to give either scale one.
//
// IT IS NOT FREE OF THE FIXTURES EITHER. Total smoothing is now the derivative
// and the window combined -- roughly sqrt(sigma_d^2 + window_sigma^2) for two
// Gaussians -- so raising this reaches further from every sample point, and the
// flat-interior gates below have 12 px of clearance to the nearest transition
// against a 10 px reach. Group 1b sweeps the pair for exactly that reason,
// and k_square was raised from 16 to 24 to buy that clearance.
constexpr float k_sigma_d = 1.0F;
// Group 5's border margin, in pixels: how far inside the frame a junction has
// to be before the response there is built from real data rather than clamp
// padding. Named because TWO gates depend on it agreeing -- the junction
// census and the peak count -- and they silently disagreed while one of them
// used a literal.
constexpr vc::image_dim k_rot_margin = 12;

// THE k every fixture uses when it is not sweeping k. 0.06 is the value in
// common use and the one Szeliski quotes for alpha; the 1988 paper gives none,
// which is why vc_harris.h declines to supply a default. Named rather than
// repeated ten times so that its provenance is recorded and so that it reads
// as what it is -- a FIXTURE constant. Choosing k for real use is the
// learner's job and lives in group 6.
constexpr float k_fixture_k = 0.06F;

// Axis-aligned checkerboard. Squares k_square across; X-junctions sit on the
// boundary between pixels (k_square - 1) and k_square, and their multiples.
// +1 / -1 bands with a ZERO-VALUED TRANSITION PIXEL between them, placed at
// t == k_square/2 (mod k_square). Two properties, both load-bearing:
//
//   THE TRANSITION IS ON A PIXEL CENTRE, not between two pixels. A hard step
//     puts the junction halfway between pixels, so the response peak is an
//     EXACT 2x2 tie by symmetry -- and strict > then finds NOTHING at all. The
//     grey transition pixel makes the response symmetric ABOUT a pixel
//     instead, which is what lets a unique maximum exist there.
//
//     IT DOES NOT BY ITSELF MAKE THAT PIXEL THE MAXIMUM, and this comment
//     claimed it did -- "decided by construction rather than by tolerance".
//     Symmetry about a pixel permits a single central peak OR a symmetric
//     RING of peaks around it, and which it is depends on the scales. At
//     sigma_i == sigma_d the junction pixel is a local MINIMUM with four
//     equal maxima on its diagonal neighbours; at sigma_i > sigma_d it is
//     the unique maximum. Necessary, not sufficient. See k_window_sigma.
//
//   THE OFFSET KEEPS TRANSITIONS OFF THE IMAGE BORDER. With transitions at
//     multiples of k_square, the pattern's own edge lands on row 0 and column
//     0, and clamp padding then replicates it into a genuine T-junction one
//     pixel in -- 21 false corners, at 42% of the true peak height. Offsetting
//     by half a square puts flat interior against the border instead.
// Written over a CONTINUOUS coordinate so the rotated fixture in group 5 can
// use exactly the same pattern. On integer arguments it is the obvious
// alternating +1 / -1, with a single zero-valued pixel at each transition.
double band_sign(double t) {
    const double u = t + (k_square * 8) - (k_square / 2.0); // kept positive
    const double f = u / k_square;
    const double n = std::round(f);
    const double d = (f - n) * k_square; // pixels from it
    const double band = (d >= 0.0) ? n : n - 1.0;
    const double sign = (std::fmod(band, 2.0) == 0.0) ? -1.0 : +1.0;
    return sign * std::min(1.0, std::abs(d)); // 1-px ramp
}

vc::vc_image checkerboard() {
    return from_rule(k_dim, k_dim, [](int x, int y) {
        return 50.0F +
               (50.0F * static_cast<float>(band_sign(x) * band_sign(y)));
    });
}

// A single straight DIAGONAL step: I depends on (x + y) alone.
//
// THIS FIXTURE IS THE ONE THAT TESTS C, AND THE CHECKERBOARD CANNOT. An
// axis-aligned checkerboard has C ~= 0 almost everywhere, so an implementation
// that drops C entirely, or forgets to square it in Det, passes every
// checkerboard gate. Here:
//
//   I(x,y) = f(x + y)  =>  Ix == Iy at EVERY pixel, by construction
//   so A == B == C, hence Det = AB - C^2 == 0 and M is RANK ONE
//   so R = -k Tr^2 < 0 for every k > 0:  A STRAIGHT EDGE, which it is.
//
// Drop C and Det becomes AB = A^2, giving R = A^2 (1 - 4k) > 0 for k < 0.25 --
// the diagonal edge is reported as a CORNER. That is corners.txt section 8's
// demonstration, turned into a gate.
vc::vc_image diagonal_step() {
    return from_rule(k_dim, k_dim, [](int x, int y) {
        return (x + y >= static_cast<int>(k_dim) / 2) ? 100.0F : 0.0F;
    });
}

// A strictly decreasing ramp, used AS a response map (not as an image).
//
// THIS IS THE ONLY FIXTURE THAT CAN SEE AN IN-PLACE SUPPRESSION (trap 6). It
// has NO interior local maximum: every pixel is smaller than its left
// neighbour. An implementation that zeroes losers in the buffer it is still
// reading invents a peak every other pixel, because the larger neighbour it
// should be losing to has already been overwritten with 0.
//
// An isolated-blob fixture cannot catch this -- the bug needs a monotone run
// to express itself -- which is why this is built by hand rather than produced
// by the detector.
// THE Y-STEP MUST BE MUCH LARGER THAN THE X-STEP, and that is not cosmetic.
// With a gentle y-slope the bug hides: the pixel below-left has not been
// scanned yet, still holds its original larger value, and correctly suppresses
// the candidate -- so an in-place implementation passes. Making each row far
// smaller than the one above it means the only larger neighbours are the ones
// ALREADY overwritten, which is exactly the condition the bug needs.
vc::vc_image decreasing_ramp() {
    return from_rule(k_dim, k_dim, [](int x, int y) {
        return 1.0e6F - (static_cast<float>(y) * 1000.0F) -
               static_cast<float>(x);
    });
}

// A flat plateau well above any threshold.
//
// EVERY INTERIOR PIXEL TIES WITH ITS NEIGHBOURS, so under the strict > that
// vc_harris.h specifies there is NO peak here at all -- a plateau has no
// summit, which is the box-window failure corners.txt section 20 describes.
// Relaxing the comparison to accept ties turns the whole plateau into peaks.
// Nothing else in this suite contains a tie, so without this fixture that
// clause of the contract is unpinned.
vc::vc_image plateau() {
    return from_rule(k_dim, k_dim, [](int x, int y) {
        const bool inside = x >= 30 && x <= 60 && y >= 30 && y <= 60;
        return inside ? 500.0F : 1.0F;
    });
}

struct tensor_at {
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
};

tensor_at tensor_sample(const vc::pixelops::vc_structure_tensor& m,
                        vc::image_dim x,
                        vc::image_dim y) {
    return {pixel(m.a, x, y), pixel(m.b, x, y), pixel(m.c, x, y)};
}

// THE FIXTURE PATH: gradients straight from the source, with NO pre-smoothing.
// That is a fixture choice and not a prescription. Trap 2 of vc_harris.h
// covers the pre-smoothing Gaussian (sigma_d) and why applying one is the
// caller's composition rather than this function's argument -- so it sits
// outside structure_tensor's contract, which is what this file tests. Group 1b
// sweeps the INTEGRATION scale, which IS structure_tensor's own parameter; the
// derivative scale is not swept here because it belongs to the caller.
// THE FIXTURE PATH. One call now: structure_tensor() owns the derivative, so
// there is no gradient stage here to get wrong, and no way for these fixtures
// to differ from what the function does internally. The earlier version of
// this helper called gradient_x/gradient_y and passed the pair in -- which
// meant every fixture in this file silently used sigma_d = 0, the one setting
// the header calls incomplete, and nothing could notice.
vc::pixelops::vc_structure_tensor tensor_of(const vc::vc_image& src) {
    return vc::pixelops::structure_tensor(src, k_sigma_d, k_window_sigma,
                                          vc_edge_policy::clamp);
}

// The k band every sign gate must hold across. Spans the usable range stated
// in vc_harris.h -- open at 0, open at 0.25 -- so no gate can be flipped by
// the learner's choice.
const std::vector<float> k_band = {0.01F, 0.04F, 0.06F, 0.10F, 0.20F, 0.24F};

} // namespace

int main() {
    // UNBUFFERED. ctest captures stdout through a pipe, where std::cout is
    // fully buffered rather than line buffered, so if an implementation throws
    // and terminate() aborts the run, every group header printed so far is
    // still sitting in the buffer and is LOST. The learner then sees a failed
    // test with no indication of where it got to. Flushing per write costs
    // nothing at this scale and keeps the last group printed meaningful.
    std::cout << std::unitbuf;
    // ---- stub probe --------------------------------------------------------
    // PROBE ALL THREE, not just the first. The state this file sees for most
    // of the build is PARTIALLY implemented -- structure_tensor written, the
    // other two still stubs -- and a one-function probe falls straight through
    // to the gates in that state. There the unimplemented function's
    // logic_error either escapes main uncaught or gets swallowed by rejects()
    // and reported as a failed REJECT gate. Both read as "your code is wrong"
    // when the truth is "you have not written this one yet", so name it.
    {
        const vc::vc_image tiny =
            from_rule(8, 8, [](int, int) { return 0.0F; });
        // Valid input: the three agree and are f32, so this reaches the body
        // rather than tripping a validation check.
        const vc::pixelops::vc_structure_tensor flat_m{tiny, tiny, tiny};
        std::vector<std::string> missing;
        std::vector<std::string> broken;
        const auto probe = [&missing, &broken](const std::function<void()>& f) {
            try {
                f();
            } catch (const std::exception& e) {
                // MATCH THE SENTINEL TEXT, NOT THE EXCEPTION TYPE, and this is
                // not a style preference. std::out_of_range,
                // std::invalid_argument, std::length_error and
                // std::domain_error ALL DERIVE FROM std::logic_error. Trap 6
                // tells the learner to write the suppression into a separate
                // output; a std::vector<float> scratch buffer indexed with
                // .at() and an off-by-one throws std::out_of_range on this very
                // 8x8 probe. Catching the TYPE would file that as "not written
                // yet", exit 0, and leave ctest GREEN on a genuinely broken
                // implementation -- which is the one outcome this file exists
                // to prevent.
                const std::string what = e.what();
                if (what.find("is NOT IMPLEMENTED") != std::string::npos) {
                    missing.emplace_back(what);
                } else {
                    // A REAL THROW ON VALID INPUT, AND IT CANNOT BE LEFT TO
                    // THE GATES. Earlier this branch discarded the exception
                    // and relied on the gates hitting it again -- but when
                    // `missing` is non-empty this function returns
                    // EXIT_SUCCESS and NO GATE EVER RUNS. So in the partial
                    // state (one function written, one still a stub) a genuine
                    // bug in the written one was seen here, dropped, and ctest
                    // stayed green. The 8x8 input below is valid by
                    // construction, so any throw on it that is not the
                    // sentinel is a defect and has to fail the run on its own
                    // account.
                    broken.emplace_back(what);
                }
            }
        };
        probe([&] { (void)tensor_of(tiny); });
        probe(
            [&] { (void)vc::pixelops::corner_response(flat_m, k_fixture_k); });
        probe([&] { (void)vc::pixelops::corner_peaks(tiny, 0.0F); });
        // CHECKED FIRST, AND INDEPENDENTLY OF `missing`. A broken function
        // alongside an unwritten one is still broken.
        if (!broken.empty()) {
            std::cout << "20_harris: " << broken.size()
                      << " function(s) THREW on valid 8x8 input. That is a "
                         "BUG, not an unwritten stub:\n";
            for (const std::string& w : broken) {
                std::cout << "  " << w << '\n';
            }
            // Print the unwritten ones too before failing: one bug and one
            // stub is the likeliest partial state, and hiding the stub list
            // makes the bug look like the only thing outstanding.
            for (const std::string& w : missing) {
                std::cout << "  (still unwritten) " << w << '\n';
            }
            return EXIT_FAILURE;
        }
        // STRUCTURE_TENSOR IS THE ONE THAT HAS TO EXIST BEFORE ANYTHING
        // RUNS. Every fixture below IS a tensor, and they are built at main
        // scope before the first group, so if that function is still a stub
        // the construction throws where no group handler can catch it. The
        // other two are different: a gate that needs them can be skipped on
        // its own, which is what run_group() does. So a PARTIAL
        // implementation -- the normal state for most of this build -- still
        // gets real feedback on the parts that exist, instead of the silence
        // that an all-or-nothing probe gives.
        const bool have_tensor = std::none_of(
            missing.begin(), missing.end(), [](const std::string& w) {
                return w.find("structure_tensor") != std::string::npos;
            });
        if (!missing.empty()) {
            std::cout << "20_harris: " << missing.size()
                      << " of 3 functions NOT IMPLEMENTED:\n";
            for (const std::string& w : missing) {
                std::cout << "  " << w << '\n';
            }
            if (!have_tensor) {
                std::cout << "  structure_tensor is one of them, and every "
                             "fixture is a tensor -- 0 gates can run.\n";
                return EXIT_SUCCESS;
            }
            std::cout << "  gates needing an unwritten function will be "
                         "SKIPPED; all others still run.\n";
        }
    }

    std::cout << "20_harris: implementation present -- running gates.\n";

    const vc::vc_image board = checkerboard();
    const vc::pixelops::vc_structure_tensor m_board = tensor_of(board);

    // ---- gate groups: a missing dependency SKIPS, anything else FAILS ----
    // THE DISTINCTION IS THE WHOLE POINT. A gate that cannot run because the
    // function it calls is still a stub has not found a defect, and reporting
    // that as a failure teaches the learner to ignore red output. A gate that
    // throws anything ELSE on valid input HAS found something, and must fail
    // the run with a readable message rather than escape main and abort it --
    // terminate() loses the group headers and says nothing about where it got
    // to. So: the sentinel counts as a skip, every other exception counts as
    // a failure, and neither is silent.
    const auto run_group = [](const char* name,
                              const std::function<void()>& body) {
        try {
            body();
        } catch (const std::exception& e) {
            const std::string what = e.what();
            if (what.find("is NOT IMPLEMENTED") != std::string::npos) {
                ++g_skipped;
                std::cout << "  group " << name
                          << ": SKIPPED from here -- needs a function that is "
                             "not written yet\n";
                return;
            }
            ++g_failed;
            std::cout << "  group " << name << ": THREW on valid input -- "
                      << what << '\n';
        }
    };

    // ---- group 1: the three signs, on three hand-identifiable pixels -------
    //
    // P3 section F.2(e)'s decision, verbatim: "R > 0 at a square corner, R < 0
    // on a straight edge between corners, R ~= 0 (and tr below threshold) in
    // the interior of a square." The heatmap stays a dump-and-inspect
    // artefact; THE SIGN IS THE TEST.
    //
    // THIS GROUP ALSO GATES TRAP 1, and it is the only group that can. If the
    // ---- group 0: the kernels this slice added to the library -------------
    //
    // make_gaussian_derivative_1d_x/y() were added to vc_convolve for THIS
    // slice, so that route A of the construction section in vc_harris.h has a
    // correct fused derivative-of-Gaussian to use instead of asking the
    // implementer to build one. They have NO CALLER, because the
    // implementation chose route B -- and public API with no caller and no
    // test is exactly what the operator* note's "earns its place" bar exists
    // to refuse. Gated here rather than deleted, because route A stays
    // documented as acceptable and the kernels are correct; what was wrong was
    // that nothing in ctest said so.
    //
    // THESE BELONG WITH A CONVOLVE SUITE, not with Harris. Booked in
    // docs/follow_up_p4.txt; they live here because this is the file that
    // needed them.
    //
    // All four checks are ARITHMETIC ON THE TAPS, with no tolerance beyond
    // float's own:
    //   sum == 0        a derivative of a constant is zero, exactly
    //   antisymmetric   w[i] == -w[n-1-i], exactly
    //   width == n + 2  the Gaussian convolved with a 3-tap difference
    //   ramp slope 1    on f(x) = x the response is exactly 1, which pins the
    //                   1/2 normalisation AND the sign convention together
    run_group("0", [&] {
        std::cout << "group 0: the library kernels this slice added\n";

        for (const float sigma : {0.5F, 1.0F, 2.0F}) {
            const vc::pixelops::vc_kernel row =
                vc::pixelops::make_gaussian_derivative_1d_x(sigma);
            const vc::pixelops::vc_kernel col =
                vc::pixelops::make_gaussian_derivative_1d_y(sigma);
            const vc::pixelops::kernel_weights& w = row.weights();
            const std::string at = " (sigma = " + std::to_string(sigma) + ")";

            check(row.height() == 1 && col.width() == 1,
                  "DOG: _x is a row and _y is a column" + at);
            check(row.width() == col.height() && row.weights() == col.weights(),
                  "DOG: the two orientations carry identical taps" + at);
            check(row.width() ==
                      vc::pixelops::make_gaussian_1d_x(sigma).width() + 2,
                  "DOG: width is the Gaussian's plus 2" + at);
            check(row.width() % 2 == 1, "DOG: width is odd" + at);

            double sum = 0.0;
            double worst_anti = 0.0;
            double slope = 0.0;
            const auto n = w.size();
            const auto radius = static_cast<double>(n / 2);
            for (std::size_t i = 0; i < n; ++i) {
                sum += static_cast<double>(w[i]);
                worst_anti = std::max(
                    worst_anti, std::abs(static_cast<double>(w[i]) +
                                         static_cast<double>(w[n - 1 - i])));
                // convolve() flips, so the ramp f(x) = x is sampled at -(i-r)
                slope += static_cast<double>(w[i]) *
                         -(static_cast<double>(i) - radius);
            }
            check(std::abs(sum) < 1e-6,
                  "DOG: the taps sum to zero -- d/dx of a constant" + at);
            check(worst_anti < 1e-6, "DOG: the taps are antisymmetric" + at);
            check(std::abs(slope - 1.0) < 1e-5,
                  "DOG: a unit ramp reports a slope of exactly 1 -- pins both "
                  "the 1/2 normalisation and the sign" +
                      at);
        }

        // THE FUSION IS EXACT, and this gate COMPOSES it rather than
        // comparing widths, which is all it used to do while claiming more.
        // Every check above is satisfied by a BOX filter convolved with the
        // difference -- sum zero, antisymmetric, odd width, unit slope -- so
        // not one of them says the blur is GAUSSIAN. This puts an impulse
        // through convolve() twice: once with the fused kernel, once with the
        // plain Gaussian followed by the bare central difference. Those are
        // the two groupings of (I (x) G) (x) D == I (x) (G (x) D), so they
        // must agree to float precision, and that pins the fused taps to the
        // Gaussian's without reaching into the private tap array.
        check(
            vc::pixelops::make_gaussian_derivative_1d_x(1.0F).width() ==
                vc::pixelops::make_gaussian_1d_x(1.0F).width() +
                    vc::pixelops::make_sobel_derivative_1d_x().width() - 1,
            "DOG: width is exactly that of a full 1-D convolution, n + m - 1");

        for (const float sigma : {0.5F, 1.0F, 2.0F}) {
            vc::vc_image_writer imp{k_dim, k_dim, 1, vc::buf_f32{0.0F}};
            imp.at<vc::buf_f32>(k_dim / 2, k_dim / 2, 0) = 1000.0F;
            const vc::vc_image impulse = std::move(imp).seal();

            const vc::vc_image fused = vc::pixelops::convolve(
                impulse, vc::pixelops::make_gaussian_derivative_1d_x(sigma),
                vc_edge_policy::clamp);
            const vc::vc_image staged = vc::pixelops::convolve(
                vc::pixelops::convolve(impulse,
                                       vc::pixelops::make_gaussian_1d_x(sigma),
                                       vc_edge_policy::clamp),
                vc::pixelops::make_sobel_derivative_1d_x(),
                vc_edge_policy::clamp);
            float worst_f = 0.0F;
            float scale_f = 0.0F;
            for (vc::image_dim y = 0; y < k_dim; ++y) {
                for (vc::image_dim x = 0; x < k_dim; ++x) {
                    scale_f = std::max(scale_f, std::abs(pixel(staged, x, y)));
                    worst_f = std::max(worst_f, std::abs(pixel(fused, x, y) -
                                                         pixel(staged, x, y)));
                }
            }
            check(scale_f > 0.0F, "DOG: the staged reference is not zero");
            check(worst_f < scale_f * 1e-5F,
                  "DOG: the fused kernel IS the Gaussian convolved with the "
                  "central difference (sigma = " +
                      std::to_string(sigma) + ")");
        }
    });

    // implementation blurs the gradients and then squares them, a checkerboard
    // junction -- where gradients point in OPPOSITE directions -- averages to
    // nearly zero, so A, B and C all collapse and R is ~0 instead of positive.
    // An EDGE cannot catch it: there the gradients all point the same way and
    // nothing cancels, so both orders give a large A. The corner is the
    // discriminator.
    run_group("1", [&] {
        std::cout << "group 1: the three signs (swept over k)\n";

        // Transitions sit at t == k_square/2 (mod k_square); band centres at
        // t == 0 (mod k_square). See k_t and k_c beside the fixtures.
        constexpr vc::image_dim cx = k_t; // transition in BOTH axes: junction
        constexpr vc::image_dim cy = k_t;
        constexpr vc::image_dim ex = k_t; // transition in x only: a straight
        constexpr vc::image_dim ey = k_c; //   vertical edge, mid-band in y
        constexpr vc::image_dim fx = k_c; // band centre in both: flat interior
        constexpr vc::image_dim fy = k_c;

        const tensor_at t_flat = tensor_sample(m_board, fx, fy);
        const float tr_flat = t_flat.a + t_flat.b;
        const tensor_at t_corner = tensor_sample(m_board, cx, cy);
        const float tr_corner = t_corner.a + t_corner.b;

        // The flat test is on Tr, not on R -- the paper pins the flat region
        // with "Tr falling below some selected threshold", and R is small but
        // POSITIVE there, so an R-only test would call it a corner.
        check(tr_flat < tr_corner * 1e-3F,
              "FLAT: Tr in a square interior is negligible beside Tr at a "
              "corner");

        for (const float k : k_band) {
            const vc::vc_image r = vc::pixelops::corner_response(m_board, k);
            const std::string at = " (k = " + std::to_string(k) + ")";

            check(pixel(r, cx, cy) > 0.0F,
                  "CORNER: R > 0 at a square corner" + at);
            check(pixel(r, ex, ey) < 0.0F,
                  "EDGE: R < 0 on a straight edge between corners" + at);
            check(std::abs(pixel(r, fx, fy)) <
                      std::abs(pixel(r, cx, cy)) * 1e-3F,
                  "FLAT: |R| in a square interior is negligible" + at);
        }

        // k MUST ACTUALLY BE USED, and the sweep above cannot tell. Every gate
        // in it checks a SIGN, and the sign is the same at every k in the band
        // -- that is the whole point of sweeping, and it is also precisely why
        // sweeping is blind to an implementation that ignores the argument and
        // hard-codes a value. Mutation testing found this: a version that
        // hard-coded 0.06 and discarded the caller's k passed every other gate
        // in this file.
        //
        // R = Det - k Tr^2, and Tr is large at a corner, so two different k
        // give two different R by arithmetic. No tolerance to choose and
        // nothing to measure -- only a difference to require.
        check(pixel(vc::pixelops::corner_response(m_board, 0.01F), cx, cy) !=
                  pixel(vc::pixelops::corner_response(m_board, 0.20F), cx, cy),
              "CORNER: k changes R -- the argument is not ignored");
    });

    // ---- group 1b: the signs hold across the INTEGRATION scale ------------
    //
    // Group 1 sweeps k at one window_sigma. This sweeps window_sigma at one k,
    // because window_sigma is structure_tensor's OWN parameter and every other
    // gate in this file was validating it at exactly one value -- so "the signs
    // are right" meant "the signs are right at 1.0" and nothing more. The
    // header declines to give it a default; the least this file can do is show
    // the sign structure does not depend on the choice.
    //
    // sigma_i = 2 is deliberately the top of the range: it is what Schmid,
    // Mohr and Bauckhage report as best, so the value the learner is most
    // likely to pick is the one furthest from the value these fixtures were
    // built at.
    //
    // THE UPPER LIMIT IS A PROPERTY OF THE FIXTURE, NOT OF THE OPERATOR. The
    // flat sample sits at a band centre, 8 px from the nearest transition. At
    // window_sigma = 3 the window reaches about 3 sigma = 9 px and so pulls in
    // a real junction, and the flat gate stops being a statement about the
    // implementation. Do not widen this sweep without moving the flat sample
    // or growing the squares.
    run_group("1b", [&] {
        std::cout << "group 1b: the three signs (swept over window_sigma)\n";

        constexpr vc::image_dim cx = k_t;
        constexpr vc::image_dim cy = k_t;
        constexpr vc::image_dim ex = k_t;
        constexpr vc::image_dim ey = k_c;
        constexpr vc::image_dim fx = k_c;
        constexpr vc::image_dim fy = k_c;

        // BOTH SCALES, as a grid. Before structure_tensor() took the source
        // image this group could only sweep window_sigma, because sigma_d was
        // the caller's business and never entered the function -- so the
        // literature's own pair, sigma_d = 1 with sigma_i = 2, was a
        // combination this file could not even express. It can now.
        for (const float sd : {0.7F, 1.0F, 1.5F}) {
            for (const float si : {0.7F, 1.0F, 1.5F, 2.0F}) {
                const vc::pixelops::vc_structure_tensor m =
                    vc::pixelops::structure_tensor(board, sd, si,
                                                   vc_edge_policy::clamp);
                const vc::vc_image r =
                    vc::pixelops::corner_response(m, k_fixture_k);
                const std::string at =
                    " (sigma_d = " + std::to_string(sd) +
                    ", window_sigma = " + std::to_string(si) + ")";

                check(pixel(r, cx, cy) > 0.0F,
                      "SCALE: R > 0 at a square corner" + at);
                check(pixel(r, ex, ey) < 0.0F,
                      "SCALE: R < 0 on a straight edge" + at);
                check(std::abs(pixel(r, fx, fy)) <
                          std::abs(pixel(r, cx, cy)) * 1e-3F,
                      "SCALE: |R| negligible in a flat interior" + at);
            }
        }

        // sigma_d MUST ACTUALLY BE USED, by the same argument as the
        // window_sigma gate below and k's in group 1: the grid above checks
        // only SIGNS, and the signs hold at every scale, so a hard-coded
        // derivative width passes all of it. Two Gaussian derivatives of
        // different width applied to the same non-constant image give
        // different results -- arithmetic, so no tolerance.
        const auto a_at_corner = [&board](float sd) {
            return pixel(vc::pixelops::structure_tensor(
                             board, sd, k_window_sigma, vc_edge_policy::clamp)
                             .a,
                         k_t, k_t);
        };
        check(a_at_corner(0.7F) != a_at_corner(1.5F),
              "SCALE: sigma_d changes the tensor -- not ignored");

        // window_sigma MUST ACTUALLY BE USED -- the same blind spot as group
        // 1's k gate, and found the same way. A version that ignored this
        // argument and always integrated at 1.0 passed every sign gate above,
        // because the signs are correct at every scale. Robustness to a
        // parameter and use of a parameter are different claims, and a sweep
        // over signs only ever demonstrates the first.
        //
        // Two Gaussians of different width, applied to the same non-constant
        // image, give different results. Arithmetic again, so no tolerance.
        const auto a_at_edge = [&board](float si) {
            return pixel(vc::pixelops::structure_tensor(board, k_sigma_d, si,
                                                        vc_edge_policy::clamp)
                             .a,
                         k_t, k_c);
        };
        check(a_at_edge(0.7F) != a_at_edge(2.0F),
              "SCALE: window_sigma changes the tensor -- not ignored");
    });

    // ---- group 1c: the operator itself, against a reference construction --
    //
    // THIS IS THE ONLY GATE THAT SEES AN ANISOTROPIC DERIVATIVE, and it took
    // three attempts to find one. vc_harris.h requires the derivative stage to
    // be ISOTROPIC; the route it warns against -- pre-blur at sigma_d, then
    // call gradient_x/gradient_y, which are Sobel and so carry a second
    // [1,2,1] along the perpendicular axis only -- is the single most likely
    // wrong implementation, because it is the one that reuses what already
    // exists. Every other gate in this file is blind to it:
    //
    //   ON-AXIS TENSOR GATES ARE BLIND BY SYMMETRY. sobel_y is sobel_x
    //     transposed and an isotropic pre-blur commutes with transposition,
    //     so B[I^T] == A[I]^T EXACTLY even for the wrong operator. Any gate
    //     built from a symmetric axis-aligned fixture has this hole.
    //   THE ROTATED PEAK COUNT IS BLIND AT THESE SCALES. What the response
    //     sees is derivative and window together, ~sqrt(sd^2 + si^2): at
    //     si = 2 that is 2.24 against 2.35, about 5%, and group 5 returns the
    //     SAME 24 interior peaks either way. It did catch it at si = 1, where
    //     the anisotropy is 12% -- but si = 1 is the ring defect, so the gate
    //     only worked at a scale where nothing else did.
    //
    // WHAT WORKS IS A REFERENCE COMPARISON, which is also the one thing the
    // header already promised: "the suite pins the result, not the route".
    // Build A, B and C here out of the FUSED derivative-of-Gaussian kernels,
    // and require the implementation's tensor to match in the INTERIOR.
    //
    // NOT CIRCULAR: group 0 pins those kernels arithmetically and
    // independently -- taps summing to zero, antisymmetric, unit-ramp slope
    // exactly 1 -- so this gate rests on checked arithmetic, not on a second
    // copy of the implementation.
    //
    // INTERIOR ONLY, because the two routes apply `policy` a different number
    // of times and clamping does not commute with convolution. k_ref_margin
    // is the operator's reach plus one.
    //
    // THE TOLERANCE COMES FROM A FIVE-ORDER GAP, measured on this fixture as
    // the worst |difference| over the interior, relative to max |A| there:
    //
    //     route A (fused DoG) vs route B      2.9e-07   <- both legal
    //     pre-blur + gradient_x (the trap)    6.6e-02   <- 200,000x larger
    //     sampled analytic G' (see below)     1.6e-01
    //
    // 1e-4 sits 300x above the legal disagreement and 600x below the trap, so
    // it cannot fail a correct implementation on rounding and cannot pass the
    // wrong one.
    //
    // IT ALSO NARROWED THE CONTRACT, and that is a real finding rather than a
    // convenience. Trap 2 used to allow "a sampled Gaussian derivative" as an
    // alternative to the Gaussian convolved with the difference. Sampling the
    // analytic derivative of a continuous Gaussian at integer offsets is NOT
    // the same operator -- it is 1.6e-01 away, FURTHER than the trap this
    // gate exists to catch -- so that clause was wrong and has been removed.
    // A gate that rejects a construction the header permits would be the
    // suite contradicting the contract; the contract was the thing in error.
    run_group("1c", [&] {
        std::cout << "group 1c: the tensor against a reference construction\n";

        constexpr vc::image_dim k_ref_margin = 12;

        const auto ref = [&board](float sd, float si) {
            const vc::vc_image dx = vc::pixelops::convolve_separable(
                board, vc::pixelops::make_gaussian_derivative_1d_x(sd),
                vc::pixelops::make_gaussian_1d_y(sd), vc_edge_policy::clamp);
            const vc::vc_image dy = vc::pixelops::convolve_separable(
                board, vc::pixelops::make_gaussian_1d_x(sd),
                vc::pixelops::make_gaussian_derivative_1d_y(sd),
                vc_edge_policy::clamp);
            vc::vc_image_writer aw{k_dim, k_dim, 1, vc::buf_f32{0.0F}};
            vc::vc_image_writer bw{k_dim, k_dim, 1, vc::buf_f32{0.0F}};
            vc::vc_image_writer cw{k_dim, k_dim, 1, vc::buf_f32{0.0F}};
            for (vc::image_dim y = 0; y < k_dim; ++y) {
                for (vc::image_dim x = 0; x < k_dim; ++x) {
                    const float gx = pixel(dx, x, y);
                    const float gy = pixel(dy, x, y);
                    aw.at<vc::buf_f32>(x, y, 0) = gx * gx;
                    bw.at<vc::buf_f32>(x, y, 0) = gy * gy;
                    cw.at<vc::buf_f32>(x, y, 0) = gx * gy;
                }
            }
            const vc::pixelops::vc_kernel wx =
                vc::pixelops::make_gaussian_1d_x(si);
            const vc::pixelops::vc_kernel wy =
                vc::pixelops::make_gaussian_1d_y(si);
            return vc::pixelops::vc_structure_tensor{
                .a = vc::pixelops::convolve_separable(
                    std::move(aw).seal(), wx, wy, vc_edge_policy::clamp),
                .b = vc::pixelops::convolve_separable(
                    std::move(bw).seal(), wx, wy, vc_edge_policy::clamp),
                .c = vc::pixelops::convolve_separable(
                    std::move(cw).seal(), wx, wy, vc_edge_policy::clamp)};
        };

        // Swept, so the match cannot be an accident of one scale pair.
        for (const float sd : {0.8F, 1.0F}) {
            for (const float si : {1.5F, 2.0F}) {
                const vc::pixelops::vc_structure_tensor expect = ref(sd, si);
                const vc::pixelops::vc_structure_tensor got =
                    vc::pixelops::structure_tensor(board, sd, si,
                                                   vc_edge_policy::clamp);
                float worst = 0.0F;
                float scale = 0.0F;
                for (vc::image_dim y = k_ref_margin; y < k_dim - k_ref_margin;
                     ++y) {
                    for (vc::image_dim x = k_ref_margin;
                         x < k_dim - k_ref_margin; ++x) {
                        scale =
                            std::max(scale, std::abs(pixel(expect.a, x, y)));
                        worst = std::max({worst,
                                          std::abs(pixel(got.a, x, y) -
                                                   pixel(expect.a, x, y)),
                                          std::abs(pixel(got.b, x, y) -
                                                   pixel(expect.b, x, y)),
                                          std::abs(pixel(got.c, x, y) -
                                                   pixel(expect.c, x, y))});
                    }
                }
                check(scale > 0.0F,
                      "REF: the reference tensor is not identically zero");
                check(worst < scale * 1e-4F,
                      "REF: the tensor matches an isotropic "
                      "derivative-of-Gaussian in the interior (sigma_d = " +
                          std::to_string(sd) +
                          ", sigma_i = " + std::to_string(si) + ")");
            }
        }
    });

    // ---- group 2: the diagonal step -- C exists, and is squared ------------
    run_group("2", [&] {
        std::cout << "group 2: the diagonal step (C's existence)\n";

        const vc::vc_image diag = diagonal_step();
        const vc::pixelops::vc_structure_tensor m_diag = tensor_of(diag);

        constexpr vc::image_dim dx = k_dim / 4; // (24,24): on x+y == 48
        constexpr vc::image_dim dy = k_dim / 4;

        const tensor_at t = tensor_sample(m_diag, dx, dy);

        // C must be COMPUTED. On a diagonal edge |C| is the same size as A, so
        // an implementation that leaves C zero is visible here and nowhere on
        // the checkerboard. The SIGN is not gated -- C enters R only as C^2.
        check(std::abs(t.c) > std::abs(t.a) * 0.5F,
              "DIAGONAL: |C| is comparable to A on a 45-degree edge");

        // A == B == C by construction, so Det must vanish: M is rank one.
        const float det = (t.a * t.b) - (t.c * t.c);
        check(std::abs(det) < t.a * t.b * 1e-3F,
              "DIAGONAL: Det ~= 0 -- the tensor is rank one, as constructed");

        for (const float k : k_band) {
            const vc::vc_image r = vc::pixelops::corner_response(m_diag, k);
            check(pixel(r, dx, dy) < 0.0F,
                  "DIAGONAL: R < 0 -- a 45-degree edge is an EDGE, not a "
                  "corner (k = " +
                      std::to_string(k) + ")");
        }
    });

    // ---- group 3: non-maximum suppression on a monotone ramp --------------
    run_group("3", [&] {
        std::cout << "group 3: non-max suppression (trap 6)\n";

        const vc::vc_image ramp = decreasing_ramp();
        const std::vector<vc::pixelops::vc_corner> peaks =
            vc::pixelops::corner_peaks(ramp, 0.0F);

        check(peaks.empty(),
              "RAMP: a strictly decreasing map has NO peaks -- an in-place "
              "suppression invents them");

        // And a single planted maximum must be found, so the gate above is not
        // passed by a function that always returns nothing.
        const vc::vc_image spike = from_rule(k_dim, k_dim, [](int x, int y) {
            return (x == 40 && y == 50) ? 500.0F : 1.0F;
        });
        const std::vector<vc::pixelops::vc_corner> one =
            vc::pixelops::corner_peaks(spike, 100.0F);
        check(one.size() == 1, "SPIKE: exactly one peak is found");
        if (one.size() == 1) {
            check(one[0].x == 40 && one[0].y == 50,
                  "SPIKE: the peak is at the planted location");
            check(
                one[0].response == 500.0F,
                "SPIKE: the reported response is the value that was selected");
        }

        // Ties are NOT peaks -- see plateau() for why this fixture is needed.
        const std::vector<vc::pixelops::vc_corner> flat_top =
            vc::pixelops::corner_peaks(plateau(), 100.0F);
        check(flat_top.empty(),
              "PLATEAU: a flat maximum yields NO peaks -- strict >, so a tie "
              "is not a summit");
    });

    // ---- group 3b: images with no interior ---------------------------------
    //
    // vc::image_dim IS uint32_t, and that makes the obvious interior loop a
    // trap. Written `x <= width() - 2`, a width of 1 gives 4294967295 and the
    // loop runs until a read finally goes out of range; written
    // `x < width() - 1` it is correct at every size. Nothing else in this file
    // is smaller than 8x8, so nothing else can tell the two apart -- and
    // 13_laplacian already tests 1x1 while 03_warp tests 2x2, so this is a
    // habit this suite was missing rather than a new idea.
    //
    // A 2x1 FIXTURE IS DELIBERATELY ABSENT. There the OUTER loop is the one
    // that underflows, and because a width of 2 has no interior column the
    // inner loop never performs a read -- so nothing throws and the program
    // spins forever instead of failing. 1x1, 1x8 and 8x1 provoke the same bug
    // into a read that does throw. A 0-width image is absent too: the writer
    // rejects it, so there is nothing to pass in.
    run_group("3b", [&] {
        std::cout << "group 3b: images with no interior\n";
        const auto no_interior = [](vc::image_dim w, vc::image_dim h,
                                    const char* label) {
            bool threw = false;
            bool empty = false;
            try {
                empty =
                    vc::pixelops::corner_peaks(
                        from_rule(w, h, [](int, int) { return 5.0F; }), 1.0F)
                        .empty();
            } catch (const std::exception& e) {
                if (is_unwritten(e)) {
                    throw;
                }
                threw = true;
            }
            check(!threw && empty, std::string("TINY: ") + label +
                                       " returns empty without throwing");
        };
        no_interior(1, 1, "1x1");
        no_interior(1, 8, "1x8");
        no_interior(8, 1, "8x1");
        no_interior(2, 2, "2x2");

        // The smallest shape that HAS an interior pixel -- exactly one, at
        // (1,1). This is the only gate here that catches an interior loop
        // which stops one column or row short.
        const vc::vc_image three = from_rule(3, 3, [](int x, int y) {
            return (x == 1 && y == 1) ? 500.0F : 1.0F;
        });
        const std::vector<vc::pixelops::vc_corner> tp3 =
            vc::pixelops::corner_peaks(three, 100.0F);
        check(tp3.size() == 1 && tp3[0].x == 1 && tp3[0].y == 1,
              "TINY: 3x3 with a centre maximum gives one peak, at (1,1)");
    });

    // ---- group 4: rejections ----------------------------------------------
    //
    // Every one of these must throw vc_exception/invalid_argument. The stub
    // throws std::logic_error, which `rejects` deliberately does NOT count --
    // otherwise this whole group would pass against an empty implementation,
    // which is precisely the vacuous pass P3 section H.3 names.
    run_group("4", [&] {
        std::cout << "group 4: rejections\n";

        // SIX GATES USED TO LIVE HERE AND ARE GONE, BECAUSE THE BUG CLASS
        // THEY TESTED NO LONGER EXISTS. structure_tensor() took two gradient
        // images, which could disagree in width, height, channel count or
        // dtype; gates asserted each disagreement was rejected, in BOTH
        // orientations, because vc_image_info::index() rejects on the
        // learner's behalf for whichever operand the loop happens to walk.
        // The function now takes ONE image and derives both gradients itself,
        // so they agree by construction. These were not weak gates -- they
        // were load-bearing, and MUT=8 showed the asymmetry ran in opposite
        // directions for dimensions and for channel counts. They are gone
        // because a contract clause you no longer need beats a gate that
        // enforces one. THE a/b/c MISMATCH GATES BELOW REMAIN: corner_response
        // still takes three separate images, so that class is still live.
        //
        // BOTH ORIENTATIONS STILL MATTERS FOR THOSE, and for the same reason:
        // index() bounds-checks, so an implementation that validates nothing
        // still throws whenever it walks the larger operand -- which operand
        // that is depends on the loop order, the learner's choice and not this
        // file's, so a single orientation tests the accessor rather than the
        // implementation.

        // DTYPE. BE HONEST ABOUT THESE FOUR: they cannot fail. at<T>() routes
        // through as<T>(), which re-checks the stored dtype on every typed
        // read, so any implementation that reads a pixel at all rejects a u8
        // input whether or not it meant to. Trap 3 says a gate that cannot
        // fail is worth nothing, and these are the deliberate exception --
        // kept because they state the f32 requirement in executable form next
        // to the gates that do bite, and because the only way to pass them
        // while violating the contract is to bypass the typed accessor
        // entirely. They are documentation, and they are not counted as
        // evidence that anything was verified. They are also not the ONLY
        // gates here that cannot fail: a fixture self-check such as "ROTATED:
        // the fixture contains interior junctions" is computed from the
        // fixture's own geometry and no implementation can move it. Every such
        // gate carries its disposition in a comment beside it.
        vc::vc_image_writer u8w{k_dim, k_dim, 1, vc::buf_u8{0}};
        const vc::vc_image u8img = std::move(u8w).seal();
        check(rejects([&] {
                  (void)vc::pixelops::structure_tensor(
                      u8img, k_sigma_d, k_window_sigma, vc_edge_policy::clamp);
              }),
              "REJECT: non-f32 src");
        // BOTH SCALES, ALL FOUR BAD VALUES EACH -- and +Inf is the
        // interesting one. gaussian_taps() rejects 0, negative and NaN itself,
        // so those six sit in the ledger's "enforced upstream" bucket. +Inf
        // did NOT: it sailed through `!(sigma > 0)`, ceil(3 * inf) overflowed
        // the cast to an integer radius, and sizing the tap vector threw
        // std::length_error("vector") -- not a rejection, and naming nothing
        // the caller passed. A live P2 defect, found while writing these
        // gates and FIXED IN gaussian_taps() rather than worked around here,
        // so every contract in the library that says "not finite is rejected"
        // is now true. The +Inf pair stays as its regression gate.
        for (const auto& bad :
             std::initializer_list<std::pair<const char*, float>>{
                 {"== 0", 0.0F},
                 {"negative", -1.0F},
                 {"NaN", std::nanf("")},
                 {"+Inf", std::numeric_limits<float>::infinity()}}) {
            check(rejects([&] {
                      (void)vc::pixelops::structure_tensor(
                          board, bad.second, k_window_sigma,
                          vc_edge_policy::clamp);
                  }),
                  std::string("REJECT: sigma_d ") + bad.first);
            check(rejects([&] {
                      (void)vc::pixelops::structure_tensor(
                          board, k_sigma_d, bad.second, vc_edge_policy::clamp);
                  }),
                  std::string("REJECT: window_sigma ") + bad.first);
        }

        // k's bounds are EXCLUSIVE at both ends, and both ends are a detector
        // that finds nothing -- see vc_harris.h for why that is a rejection
        // rather than a warning.
        check(rejects(
                  [&] { (void)vc::pixelops::corner_response(m_board, 0.0F); }),
              "REJECT: k == 0 (the negative lobe disappears)");
        check(rejects(
                  [&] { (void)vc::pixelops::corner_response(m_board, 0.25F); }),
              "REJECT: k == 0.25 (even a round bowl scores exactly 0)");
        check(rejects(
                  [&] { (void)vc::pixelops::corner_response(m_board, 0.5F); }),
              "REJECT: k above 0.25");
        check(rejects(
                  [&] { (void)vc::pixelops::corner_response(m_board, -0.1F); }),
              "REJECT: negative k");
        check(rejects([&] {
                  (void)vc::pixelops::corner_response(m_board, std::nanf(""));
              }),
              "REJECT: NaN k");

        const vc::vc_image r =
            vc::pixelops::corner_response(m_board, k_fixture_k);

        // "NOT FINITE" MEANS INFINITY TOO, not only NaN. Gated separately
        // because the natural guard (a bare x != x, or an ordered comparison)
        // catches one and not the other: +Inf passes every >= 0 test and then
        // silently selects nothing at all.
        //
        // THE window_sigma ONE HERE IS LOAD-BEARING IN A WAY THE OTHER SIGMA
        // GATES ARE NOT: make_gaussian_1d_* rejects 0, negative and NaN on its
        // own, but an infinite sigma reaches it and throws std::length_error
        // rather than vc_exception. So structure_tensor has to reject +Inf
        // ITSELF, before composing, or the error that surfaces is the wrong
        // type from the wrong layer.
        check(rejects([&] {
                  (void)vc::pixelops::corner_response(
                      m_board, std::numeric_limits<float>::infinity());
              }),
              "REJECT: +Inf k");

        // CHANNEL COUNT, not just dimensions. An implementation that compares
        // width and height and stops there passes every other gate here.
        const vc::vc_image rgb =
            from_rule_ch(k_dim, k_dim, 3, [](int, int) { return 1.0F; });
        const vc::vc_image grey =
            from_rule(k_dim, k_dim, [](int, int) { return 1.0F; });

        // A STRUCTURE TENSOR CAN BE HAND-BUILT, so its three images are not
        // guaranteed to agree just because structure_tensor() made them agree.
        // Several clauses on this page need writing on purpose rather than
        // falling out of a natural implementation -- this one, dx/dy in the
        // orientation where no read goes out of range, the 1-then-3 channel
        // case, and every k and threshold bound. Mutation testing is what
        // separated those from the ones the accessor covers; reading the code
        // was not enough.
        check(rejects([&] {
                  const vc::pixelops::vc_structure_tensor mixed{m_board.a,
                                                                m_board.b, rgb};
                  (void)vc::pixelops::corner_response(mixed, k_fixture_k);
              }),
              "REJECT: a, b, c of different channel counts (1-ch A)");
        check(rejects([&] {
                  const vc::pixelops::vc_structure_tensor mixed{rgb, m_board.b,
                                                                m_board.c};
                  (void)vc::pixelops::corner_response(mixed, k_fixture_k);
              }),
              "REJECT: a, b, c of different channel counts (3-ch A)");
        // BOTH ORIENTATIONS, and that is the point. With a large A and a
        // small C, an implementation that validates nothing still throws --
        // it walks A's extent and runs off the end of C, and vc_image_info
        // ::index() turns that into invalid_argument for free. Reverse it and
        // the out-of-bounds read never happens, so the only thing that can
        // reject is a check written on purpose. Testing one orientation only
        // would let the accessor pass this gate on the implementation's
        // behalf.
        const vc::vc_image tiny =
            from_rule(8, 8, [](int, int) { return 1.0F; });
        check(rejects([&] {
                  const vc::pixelops::vc_structure_tensor mixed{
                      m_board.a, m_board.b, tiny};
                  (void)vc::pixelops::corner_response(mixed, k_fixture_k);
              }),
              "REJECT: a, b, c of different dimensions (large A, small C)");
        check(rejects([&] {
                  const vc::pixelops::vc_structure_tensor mixed{tiny, tiny,
                                                                m_board.c};
                  (void)vc::pixelops::corner_response(mixed, k_fixture_k);
              }),
              "REJECT: a, b, c of different dimensions (small A, large C)");

        // THREE AGREEING u8 IMAGES. A disagreement check cannot see these --
        // they agree perfectly, in dimensions, channels and dtype -- so f32
        // has to be asserted on its own rather than inferred from consistency.
        check(rejects([&] {
                  const vc::pixelops::vc_structure_tensor all_u8{u8img, u8img,
                                                                 u8img};
                  (void)vc::pixelops::corner_response(all_u8, k_fixture_k);
              }),
              "REJECT: a, b, c all non-f32 but agreeing with each other");

        // CORNER_PEAKS' OWN REJECTIONS COME LAST, AND THE ORDER IS THE POINT.
        // run_group() skips the REST of a group the moment it meets an
        // unwritten function, so a gate for a stubbed function placed in the
        // middle takes every later gate down with it. These four used to sit
        // above the a/b/c gates, which meant that while corner_peaks was a
        // stub -- the normal state while it is being written last -- the
        // corner_response mismatch gates never ran, and a real defect in
        // already-written code went unreported. GATES FOR ONE FUNCTION STAY
        // CONTIGUOUS, and the function most likely to be unwritten goes at the
        // bottom.
        check(rejects([&] { (void)vc::pixelops::corner_peaks(r, -1.0F); }),
              "REJECT: negative threshold");
        check(rejects(
                  [&] { (void)vc::pixelops::corner_peaks(r, std::nanf("")); }),
              "REJECT: NaN threshold");
        check(rejects([&] {
                  (void)vc::pixelops::corner_peaks(
                      r, std::numeric_limits<float>::infinity());
              }),
              "REJECT: +Inf threshold");
        check(rejects([&] { (void)vc::pixelops::corner_peaks(u8img, 1.0F); }),
              "REJECT: a non-f32 response");
        // A MULTI-CHANNEL RESPONSE IS NOW A REJECTION, AND IT USED TO BE A
        // SILENT ANSWER. corner_peaks read channel 0 and discarded the rest,
        // which on an RGB response meant returning the corners of the RED
        // plane with nothing in the return value saying so. vc_corner has no
        // channel field and should not -- a corner is a position -- so the
        // reduction belongs at the TENSOR, where corner_response now does it.
        // That makes a multi-channel response unreachable through the normal
        // pipeline, but vc_structure_tensor is an aggregate and a response
        // can be hand-built, so the rejection still has to exist.
        const vc::vc_image r3ch =
            from_rule_ch(k_dim, k_dim, 3, [](int, int) { return 1.0F; });
        check(rejects([&] { (void)vc::pixelops::corner_peaks(r3ch, 0.5F); }),
              "REJECT: a multi-channel response");
    });

    // ---- group 4b: properties the contract states and nothing else pins ----
    //
    // Each of these is a sentence in vc_harris.h. Before this group they were
    // all true of a correct implementation and asserted by nothing, which is
    // the same shape of hole as a contract clause with no test: the header
    // would have been documentation rather than specification.
    run_group("4b", [&] {
        std::cout << "group 4b: stated properties\n";

        // Geometry follows the input -- inherited from convolve(), but
        // inherited contracts still get broken by a wrong output allocation.
        check(m_board.a.width() == k_dim && m_board.a.height() == k_dim &&
                  m_board.b.width() == k_dim && m_board.b.height() == k_dim &&
                  m_board.c.width() == k_dim && m_board.c.height() == k_dim &&
                  m_board.a.channels() == 1 && m_board.b.channels() == 1 &&
                  m_board.c.channels() == 1,
              "TENSOR: A, B and C keep the source geometry (all three, both "
              "axes, channels)");

        // A and B are windowed sums of SQUARES, so neither can be negative
        // anywhere -- a Gaussian has no negative weights. This catches a sign
        // slip that the sign gates in groups 1 and 2 can survive, because
        // those read three pixels and this reads every one.
        float min_a = 0.0F;
        float min_b = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                min_a = std::min(min_a, pixel(m_board.a, x, y));
                min_b = std::min(min_b, pixel(m_board.b, x, y));
            }
        }
        check(min_a >= 0.0F && min_b >= 0.0F,
              "TENSOR: A and B are non-negative everywhere (sums of squares)");

        // THE BORDER AND ORDERING GATES USE PLANTED RESPONSE MAPS, not the
        // checkerboard, and the reason is measured rather than guessed: on the
        // checkerboard the response AT the border is exactly 0, because the
        // pattern is offset so that no transition touches the edge. A gate
        // phrased as "no returned peak sits on the border" therefore holds for
        // an implementation that scans the border too -- there is simply
        // nothing there to find. It could not fail, which by this file's own
        // standard (and trap 3's) makes it worth nothing. Plant the bait
        // instead.

        // The global maximum sits ON the border and well above the threshold.
        // Only the interior peak may come back.
        const vc::vc_image bait = from_rule(k_dim, k_dim, [](int x, int y) {
            if (x == 0 && y == 48) {
                return 1000.0F; // on the border, and the largest value here
            }
            if (x == 40 && y == 50) {
                return 500.0F; // the one legitimate peak
            }
            return 1.0F;
        });
        const std::vector<vc::pixelops::vc_corner> bp =
            vc::pixelops::corner_peaks(bait, 100.0F);
        check(bp.size() == 1,
              "PEAKS: a border maximum is NOT returned (exactly one peak)");
        check(bp.size() == 1 && bp[0].x == 40 && bp[0].y == 50,
              "PEAKS: the one peak is the interior one, not the border one");

        // TWO ISOLATED PEAKS, THE LATER ONE STRONGER. Sorting by response is
        // the standard Harris idiom (keep the top N), so it is the deviation
        // this gate has to be able to see -- and it can only see it if scan
        // order and response order DISAGREE, which they do here by
        // construction.
        const vc::vc_image pair = from_rule(k_dim, k_dim, [](int x, int y) {
            if (x == 20 && y == 20) {
                return 300.0F; // first in scan order, weaker
            }
            if (x == 60 && y == 70) {
                return 900.0F; // later in scan order, stronger
            }
            return 1.0F;
        });
        const std::vector<vc::pixelops::vc_corner> tp =
            vc::pixelops::corner_peaks(pair, 100.0F);
        check(tp.size() == 2, "PEAKS: both planted peaks are found");
        check(tp.size() == 2 && tp[0].x == 20 && tp[0].y == 20 &&
                  tp[1].x == 60 && tp[1].y == 70,
              "PEAKS: row-major order, NOT sorted by response");

        // EXACTLY ON THE THRESHOLD IS OUT. The header says strictly greater on
        // BOTH comparisons; the plateau fixture pins the neighbour half and
        // this pins the threshold half. Both values are exactly representable,
        // so "equal" here is exact and not a rounding accident.
        const vc::vc_image on_t = from_rule(k_dim, k_dim, [](int x, int y) {
            return (x == 40 && y == 50) ? 500.0F : 1.0F;
        });
        check(vc::pixelops::corner_peaks(on_t, 500.0F).empty(),
              "PEAKS: a value exactly ON the threshold is not a peak");
    });

    // ---- group 4c: more than one channel ----------------------------------
    //
    // "Geometry, channel count and dtype follow the inputs", "each channel
    // independent", "R per pixel, PER CHANNEL" and "channel 0 only" are four
    // separate promises in vc_harris.h, and before this group every gate in
    // this file ran on a ONE-CHANNEL image. All four were therefore unasserted,
    // and the slip they invite is a specific one: hardcoding channel 0 in the
    // at<>() calls, which is easy to do precisely because corner_peaks really
    // IS channel-0-only, so the habit is already in the file.
    //
    // The three channels are deliberately different KINDS of content rather
    // than different numbers, so the gates can be structural:
    //   channel 0  checkerboard   -> has corners, R > 0 somewhere
    //   channel 1  constant       -> zero gradients, so A = B = C = 0 exactly
    //   channel 2  vertical edge  -> dy = 0 everywhere, so R <= 0 everywhere
    //
    // NOTE WHAT IS *NOT* ASSERTED: that channel 0 of this run equals the
    // one-channel run pixel for pixel. Changing the channel count changes the
    // memory layout and therefore what the compiler vectorises and contracts,
    // and S2 (section H.5) already paid for that lesson once with an
    // FMA-contraction NaN. Structure survives that; bit equality need not.
    run_group("4c", [&] {
        std::cout << "group 4c: more than one channel\n";

        const vc::vc_image multi =
            from_rule_c(k_dim, k_dim, 3, [](int x, int y, int c) {
                if (c == 0) {
                    return 50.0F + (50.0F * static_cast<float>(band_sign(x) *
                                                               band_sign(y)));
                }
                if (c == 1) {
                    return 100.0F; // flat
                }
                return (x < 48) ? 0.0F : 100.0F; // vertical edge
            });
        const vc::pixelops::vc_structure_tensor m3 =
            vc::pixelops::structure_tensor(multi, k_sigma_d, k_window_sigma,
                                           vc_edge_policy::clamp);

        check(m3.a.channels() == 3 && m3.b.channels() == 3 &&
                  m3.c.channels() == 3,
              "MULTI: channel count follows the input (all three of A, B, C)");

        // Channel 1 is flat, so its gradients are zero and every windowed sum
        // of their products is zero. NOT EXACTLY, THOUGH, AND THAT CHANGED
        // WITH sigma_d. Sobel's derivative factor is {0.5, 0, -0.5}: two
        // nonzero taps against a constant, cancelling to exactly 0.0 in
        // float. The derivative-of-Gaussian at sigma_d = 1 is nine taps that
        // sum to zero analytically but accumulate in float, so a constant
        // channel leaves a rounding residue instead of a clean zero. The old
        // gate asserted `== 0.0F` and was passing on a property of the KERNEL
        // WIDTH, not of channel independence.
        //
        // THE BOUND IS FROM THE MECHANISM, NOT FROM A MEASUREMENT. A real
        // bleed from channel 0 would put a value of the same ORDER as that
        // channel's own A into this one -- a ratio near 1. Rounding puts in
        // about eps per gradient, squared by the product, so something like
        // 1e-9 of it. 1e-6 sits a million-fold below any real bleed and a
        // hundred-fold above the residue, so it fails the defect and passes
        // the arithmetic.
        float worst_flat = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                worst_flat =
                    std::max({worst_flat, std::abs(pixel_c(m3.a, x, y, 1)),
                              std::abs(pixel_c(m3.b, x, y, 1)),
                              std::abs(pixel_c(m3.c, x, y, 1))});
            }
        }
        check(worst_flat < std::abs(pixel_c(m3.a, k_t, k_t, 0)) * 1e-6F,
              "MULTI: a flat channel has A = B = C = 0 (channels are "
              "independent)");

        // THE RESPONSE IS SINGLE-CHANNEL WHATEVER WENT IN, and this gate
        // replaces one that asserted the opposite. Up to this review
        // corner_response kept the input's channel count, which made R a
        // per-colour-plane quantity -- and a corner is a property of the
        // SCENE, not of a plane. Worse, the old gate was passing because of a
        // defect: the implementation built the collapse and then read the
        // uncollapsed tensor anyway, so three channels came out, and
        // corner_peaks then rejected them. RGB was a dead end that announced
        // itself as a throw from the wrong function.
        const vc::vc_image r3 = vc::pixelops::corner_response(m3, k_fixture_k);
        check(r3.channels() == 1,
              "MULTI: the response is single-channel whatever went in");
        check(r3.width() == k_dim && r3.height() == k_dim,
              "MULTI: collapsing channels leaves the geometry alone");

        float max_r3 = -1.0e30F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                max_r3 = std::max(max_r3, pixel(r3, x, y));
            }
        }
        check(max_r3 > 0.0F,
              "MULTI: the collapsed response still finds the corners");

        // DUPLICATING A CHANNEL ADDS NO INFORMATION, SO IT MUST NOT CHANGE R.
        // This is the gate that pins the reduction to a MEAN rather than a
        // sum, and it is the only one that can: mean is sum/C, a uniform
        // scaling of M, so det and tr^2 both scale by 1/C^2 and the RANKING
        // of pixels is identical either way. Only the magnitude moves -- by
        // C^2, so 9x for RGB -- and since corner_peaks takes an ABSOLUTE
        // threshold, a sum would silently break any threshold tuned on
        // greyscale.
        //
        // SWEPT OVER CHANNEL COUNT, AND 2 AND 4 ARE THE POINT. The
        // implementation divided by a hard-coded 3.0F with the channel count
        // already in scope: correct for RGB, 4/3 too large for RGBA. Three
        // channels cannot see that, and neither can one. This is the MUT=23
        // lesson in a third place -- a constant that happens to agree with
        // the fixture is indistinguishable from a parameter until the fixture
        // changes.
        const vc::vc_image gray = from_rule(k_dim, k_dim, [](int x, int y) {
            return 50.0F +
                   (50.0F * static_cast<float>(band_sign(x) * band_sign(y)));
        });
        const vc::vc_image r_gray =
            vc::pixelops::corner_response(tensor_of(gray), k_fixture_k);
        float scale_gray = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                scale_gray =
                    std::max(scale_gray, std::abs(pixel(r_gray, x, y)));
            }
        }
        for (const vc::channel_count nch :
             {vc::channel_count{2}, vc::channel_count{3},
              vc::channel_count{4}}) {
            const vc::vc_image dup =
                from_rule_c(k_dim, k_dim, nch, [](int x, int y, int) {
                    return 50.0F + (50.0F * static_cast<float>(band_sign(x) *
                                                               band_sign(y)));
                });
            const vc::vc_image r_dup =
                vc::pixelops::corner_response(tensor_of(dup), k_fixture_k);
            float worst = 0.0F;
            for (vc::image_dim y = 0; y < k_dim; ++y) {
                for (vc::image_dim x = 0; x < k_dim; ++x) {
                    worst = std::max(worst, std::abs(pixel(r_dup, x, y) -
                                                     pixel(r_gray, x, y)));
                }
            }
            // SCALED TO r_gray, WHICH IS THE QUANTITY BEING COMPARED. This
            // read max_r3 and called itself "relative to the response's own
            // scale", which was wrong twice: max_r3 comes from the MIXED
            // three-channel fixture, whose checkerboard is one channel of
            // three, so it runs about a ninth of r_gray's peak and the
            // tolerance was ~9x tighter than advertised.
            //
            // What is being tolerated is a sum of C identical floats divided
            // by C. Exact for C = 2, both operations being powers of two; for
            // C = 3 and C = 4 the running sum passes through 3x, which is not
            // generally representable, so one rounding can survive. 1e-5 of
            // the peak is many orders above that and still three orders below
            // a /3-instead-of-/C error, which is 33% at C = 2.
            check(worst < scale_gray * 1e-5F,
                  "MULTI: " + std::to_string(nch) +
                      " identical channels give the same R as one");
        }

        // THE CROSSING CASE, WHICH IS WHY THE REDUCTION HAPPENS AT THE TENSOR
        // AND NOT AT R. A horizontal edge in one channel crossing a vertical
        // edge in another IS a corner in the colour image, and no per-channel
        // response can see it: each channel alone is a straight edge, so each
        // M is rank 1, each det is 0, and each R is NEGATIVE. Summing those
        // verdicts cannot recover the fact that their null directions
        // differed -- det is the term that MEASURES that, and it is created
        // by the summation. Idealised:
        //
        //   M_h = [[1,0],[0,0]]  det 0  tr 1  ->  R = -k
        //   M_v = [[0,0],[0,1]]  det 0  tr 1  ->  R = -k
        //   sum of R                               -0.12   "not a corner"
        //   R of sum = [[1,0],[0,1]] det 1 tr 2    +0.76   "corner"
        //
        // This gate also kills the defect-1 half-fix: an implementation that
        // sizes a 1-channel writer but still reads the uncollapsed tensor
        // walks channels where it means to walk pixels, stays in bounds, and
        // produces nothing like this.
        constexpr vc::image_dim k_mid = k_dim / 2;
        const vc::vc_image cross =
            from_rule_c(k_dim, k_dim, 2, [](int x, int y, int c) {
                if (c == 0) {
                    return (y < static_cast<int>(k_mid)) ? 0.0F : 100.0F;
                }
                return (x < static_cast<int>(k_mid)) ? 0.0F : 100.0F;
            });
        const vc::vc_image r_cross =
            vc::pixelops::corner_response(tensor_of(cross), k_fixture_k);
        check(pixel(r_cross, k_mid, k_mid) > 0.0F,
              "MULTI: a horizontal edge crossing a vertical one in ANOTHER "
              "channel is a corner");

        // And each channel ON ITS OWN is only an edge, so the positive
        // response above cannot have come from either plane separately. This
        // is the half of the claim that makes the gate above mean something.
        const vc::vc_image h_only = from_rule(k_dim, k_dim, [](int, int y) {
            return (y < static_cast<int>(k_mid)) ? 0.0F : 100.0F;
        });
        const vc::vc_image v_only = from_rule(k_dim, k_dim, [](int x, int) {
            return (x < static_cast<int>(k_mid)) ? 0.0F : 100.0F;
        });
        check(
            pixel(vc::pixelops::corner_response(tensor_of(h_only), k_fixture_k),
                  k_mid, k_mid) < 0.0F &&
                pixel(vc::pixelops::corner_response(tensor_of(v_only),
                                                    k_fixture_k),
                      k_mid, k_mid) < 0.0F,
            "MULTI: neither edge alone scores positive there");
    });

    // ---- group 5: orientation -- LOCATIONS ONLY ---------------------------
    //
    // The pattern is rotated ANALYTICALLY, in the rule that generates it,
    // rather than by resampling an existing image. Two reasons: resampling
    // would add its own blur and its own pixel-centre convention to a gate
    // that is about the OPERATOR, and the expected junction locations are then
    // known exactly instead of inherited from a warp.
    //
    // NOTHING ABOUT MAGNITUDE IS ASSERTED HERE. See trap 5.
    run_group("5", [&] {
        std::cout << "group 5: orientation (locations only)\n";

        constexpr double k_theta = 20.0 * 3.14159265358979323846 / 180.0;
        const double ct = std::cos(k_theta);
        const double st = std::sin(k_theta);
        const double mid = k_dim / 2.0;

        const vc::vc_image rotated =
            from_rule(k_dim, k_dim, [ct, st, mid](int x, int y) {
                const double ox = x - mid;
                const double oy = y - mid;
                const double px = (ox * ct) + (oy * st); // inverse rotation
                const double py = (-ox * st) + (oy * ct);
                // The SAME pattern as checkerboard(), sampled on rotated
                // coordinates -- not a resampling of the image above.
                return 50.0F +
                       (50.0F * static_cast<float>(band_sign(px + mid) *
                                                   band_sign(py + mid)));
            });

        const vc::pixelops::vc_structure_tensor m_rot = tensor_of(rotated);
        const vc::vc_image r_rot =
            vc::pixelops::corner_response(m_rot, k_fixture_k);

        // Threshold relatively, because an absolute one is not contrast-proof
        // and would not be portable between these two fixtures (trap 4).
        float max_r = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                max_r = std::max(max_r, pixel(r_rot, x, y));
            }
        }
        const std::vector<vc::pixelops::vc_corner> found =
            vc::pixelops::corner_peaks(r_rot, max_r * 0.05F);

        // Where the junctions LANDED: rotate the pattern-space junction
        // coordinates forward by the same angle.
        int matched = 0;
        int expected = 0;
        for (int jx = -2; jx <= 2; ++jx) {
            for (int jy = -2; jy <= 2; ++jy) {
                const double px = (jx * k_square) + (k_square / 2.0);
                const double py = (jy * k_square) + (k_square / 2.0);
                const double ix = (px * ct) - (py * st) + mid;
                const double iy = (px * st) + (py * ct) + mid;
                // 12 PX OF MARGIN, AND IT IS TIED TO BOTH SCALES NOW. The
                // response near an edge is built from clamp-padded data, so a
                // junction closer than the combined support of the derivative
                // kernel and the window Gaussian is not a fair test of the
                // operator. The derivative used to be Sobel, radius 1, whose
                // contamination stayed inside the 1 px border corner_peaks
                // already skips. It is now a derivative-of-Gaussian at
                // sigma_d, radius about 3*sigma_d + 1, so it reaches into the
                // interior -- which is what broke the peak-count gate below
                // until that gate was restricted to this same region.
                //
                // Two Gaussians in series combine as sqrt(sd^2 + si^2), so at
                // the pair this file uses the support is around 5 px and 12 is
                // still generous. RAISE EITHER SCALE AND THIS MARGIN HAS TO
                // RISE WITH IT, or the gate starts asserting on padded data.
                if (ix < k_rot_margin || ix > k_dim - k_rot_margin ||
                    iy < k_rot_margin || iy > k_dim - k_rot_margin) {
                    continue; // too near the border to be trustworthy
                }
                ++expected;
                bool hit = false;
                for (const auto& f : found) {
                    const double ddx = f.x - ix;
                    const double ddy = f.y - iy;
                    // 3 px, deliberately loose. The header says both forms
                    // of R are correct and they round differently in float, so
                    // a near-tie at a junction can legitimately put a peak one
                    // pixel over. A tolerance tight enough to catch drift is
                    // also tight enough to fail a correct implementation on
                    // rounding. Drift beyond 3 px fails THIS gate -- not the
                    // count gate below, which bounds how MANY peaks come back
                    // and measures nothing about where they are.
                    if ((ddx * ddx) + (ddy * ddy) <= 9.0) { // within 3 px
                        hit = true;
                        break;
                    }
                }
                if (hit) {
                    ++matched;
                }
            }
        }

        check(expected > 0, "ROTATED: the fixture contains interior junctions");
        check(matched == expected,
              "ROTATED: every interior junction has a peak within 3 px");
        // A CEILING, NOT AN EXACT COUNT -- but x2 now, not x4, and the
        // reason the old bound was x4 is worth keeping. It was justified as
        // absorbing "a 2x2 near-tie", and what it was actually absorbing was
        // a DEFECT: at the old k_window_sigma = 1.0 every junction produced
        // four equal maxima on its diagonal neighbours instead of one peak,
        // so a correct implementation scored exactly 4x and the bound had
        // been fitted to that. With sigma_i = 2.0 the fixture yields one peak
        // per junction and the correct implementation returns 24 interior
        // peaks against 20 expected junctions, so x2 clears it with room and
        // still fails by a wide margin any detector that fires along edges,
        // which would return hundreds.
        //
        // COUNTED IN THE SAME REGION THE JUNCTIONS WERE COUNTED IN, and that
        // is a correction. `expected` above is restricted to junctions at
        // least k_border px inside the frame, but this gate used to compare it
        // against peaks found ANYWHERE, including the clamp-contaminated band
        // along the edge -- interior junctions against whole-image peaks.
        // sigma_d = 0 hid the mismatch: Sobel's 3-tap support kept the
        // contamination inside the 1 px border that corner_peaks already
        // skips. A derivative-of-Gaussian at sigma_d = 1 is nine taps wide and
        // reaches about 5 px in, so the artefacts land in the interior and the
        // comparison fell apart. The operator did not get worse; the gate was
        // always comparing two different regions.
        std::size_t found_interior = 0;
        for (const vc::pixelops::vc_corner& f : found) {
            const auto dmin =
                std::min({f.x, f.y, k_dim - 1 - f.x, k_dim - 1 - f.y});
            if (dmin >= k_rot_margin) {
                ++found_interior;
            }
        }
        check(found_interior <= static_cast<std::size_t>(expected) * 2,
              "ROTATED: the detector is not firing along the edges");
    });

    // ---- group 6: THE LEARNER'S SLOT -- reports, gates nothing ------------
    //
    // Picking k and the threshold by reasoning is the spec'd task (P3 section
    // B), so this group must not have an opinion about the answer. It DUMPS
    // the heatmap and prints what the choice produced; the numbers are to be
    // read, not asserted. Deliberately not a gate, and deliberately not left
    // as an empty group that reddens ctest -- S2's group 6 was exactly that.
    run_group("6", [&] {
        std::cout << "group 6: the chosen k and threshold (report only)\n";

        constexpr float k_chosen = 0.06F; // <-- REPLACE with your own, and say
        constexpr float k_fraction = 0.05F; // <-- why, in a comment here.

        const vc::vc_image r = vc::pixelops::corner_response(m_board, k_chosen);
        float max_r = 0.0F;
        float min_r = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                max_r = std::max(max_r, pixel(r, x, y));
                min_r = std::min(min_r, pixel(r, x, y));
            }
        }
        const std::vector<vc::pixelops::vc_corner> found =
            vc::pixelops::corner_peaks(r, max_r * k_fraction);

        report("k = " + std::to_string(k_chosen) +
               ", threshold = " + std::to_string(k_fraction) + " x max(R)");
        report("R ranges [" + std::to_string(min_r) + ", " +
               std::to_string(max_r) + "]  (negative lobe = the edges)");
        report("corners found: " + std::to_string(found.size()) +
               "   (this checkerboard has 36 interior junctions)");

        // ---- the dumps ----------------------------------------------------
        //
        // TWO ARTEFACTS, because the acceptance criterion asks for
        // "troughs/peaks as expected" and that is two questions: WHERE the
        // detector fires (the marked corners) and WHAT the response surface
        // does between the firings (the heatmap).
        //
        // THE HEATMAP IS DIVERGING, NOT GRAYSCALE, and that is the whole
        // reason this is not a one-liner. R is positive at corners, NEGATIVE
        // along edges, and ~0 in flat regions -- so a map normalised to
        // [0, max] clamps the entire negative lobe to black, THE SAME BLACK
        // as the flat interior. The edge troughs are half of what the
        // criterion asks to see, and the obvious dump destroys exactly that
        // half. Scaled symmetrically about zero by max|R| instead: amber
        // above zero, blue below, mid-grey AT zero. Flat and edge are then
        // different colours rather than both being black.
        //
        // AND IT IS WRITTEN TWICE, LINEAR AND COMPRESSED, because the peak
        // dwarfs the trough (compare the printed range). On the linear map
        // the edges are a faint tint. The compressed map takes the SIGNED
        // CUBE ROOT, which lifts the trough into view at the price of no
        // longer being proportional to R. Both are kept: the linear one is
        // the honest picture of MAGNITUDE, the compressed one the legible
        // picture of STRUCTURE. Reading magnitude off the compressed map
        // would be the same class of error trap 4 warns about.
        const auto png =
            vc::io::write_config{.format = vc::io::vc_image_format::png};
        vc::io::stb_image_writer writer;
        const std::string out_dir = std::string(VC_EXAMPLES_OUTPUT_DIR);

        const float span = std::max(std::abs(max_r), std::abs(min_r));
        if (span <= 0.0F) {
            report("DUMP SKIPPED: the response is identically zero, so there "
                   "is no scale to normalise against");
            return;
        }

        // grey -> amber above zero, grey -> blue below. u is already in
        // [0, 1]; `compress` decides whether it is proportional or legible.
        const auto diverging = [span](const vc::vc_image& resp, bool compress) {
            return from_rule_c(
                k_dim, k_dim, 3, [&resp, span, compress](int x, int y, int c) {
                    const float t = pixel_c(resp, static_cast<vc::image_dim>(x),
                                            static_cast<vc::image_dim>(y),
                                            static_cast<vc::channel_count>(0)) /
                                    span;
                    float u = std::abs(t);
                    if (compress) {
                        u = std::cbrt(u);
                    }
                    const float hot[3] = {1.00F, 0.80F, 0.10F};
                    const float cold[3] = {0.10F, 0.35F, 0.95F};
                    const float* end = (t >= 0.0F) ? hot : cold;
                    return 0.5F + ((end[c] - 0.5F) * u);
                });
        };

        // A HOLLOW box, not a filled one: a filled marker hides the very
        // pixel whose position is the thing being checked.
        const auto marked =
            [](const vc::vc_image& src,
               const std::vector<vc::pixelops::vc_corner>& pts) {
                vc::vc_image_writer out{k_dim, k_dim, 3, vc::buf_f32{0.0F}};
                for (vc::image_dim y = 0; y < k_dim; ++y) {
                    for (vc::image_dim x = 0; x < k_dim; ++x) {
                        const float g = pixel(src, x, y) / 100.0F;
                        for (vc::channel_count c = 0; c < 3; ++c) {
                            out.at<vc::buf_f32>(x, y, c) = g;
                        }
                    }
                }
                // SIGNED offsets, deliberately. vc::image_dim is UNSIGNED, so
                // the obvious `for (vc::image_dim d = -k_r; d <= k_r; ++d)`
                // wraps -3 to ~4.29e9, fails the condition immediately, and
                // draws NOTHING -- silently, with no warning, because every
                // operand is the same unsigned type. That is the same defect
                // class as the one the review found in corner_peaks' neighbour
                // sweep, and it is worth the four extra casts to not repeat it.
                constexpr int k_r = 3;
                const int lim = static_cast<int>(k_dim);
                for (const vc::pixelops::vc_corner& pt : pts) {
                    const int cx = static_cast<int>(pt.x);
                    const int cy = static_cast<int>(pt.y);
                    for (int d = -k_r; d <= k_r; ++d) {
                        const int ring[4][2] = {{cx + d, cy - k_r},
                                                {cx + d, cy + k_r},
                                                {cx - k_r, cy + d},
                                                {cx + k_r, cy + d}};
                        for (const auto& q : ring) {
                            if (q[0] < 0 || q[0] >= lim || q[1] < 0 ||
                                q[1] >= lim) {
                                continue;
                            }
                            const auto qx = static_cast<vc::image_dim>(q[0]);
                            const auto qy = static_cast<vc::image_dim>(q[1]);
                            out.at<vc::buf_f32>(qx, qy, 0) = 1.0F;
                            out.at<vc::buf_f32>(qx, qy, 1) = 0.15F;
                            out.at<vc::buf_f32>(qx, qy, 2) = 0.15F;
                        }
                    }
                }
                return std::move(out).seal();
            };

        // The rotated board too, because "the same corners after a small
        // rotation" is the third acceptance criterion and group 5 gates it on
        // a COUNT. A count says 24 twice; it does not say they are the same
        // 24. The picture does.
        constexpr double k_theta = 20.0 * 3.14159265358979323846 / 180.0;
        const double ct = std::cos(k_theta);
        const double st = std::sin(k_theta);
        const double mid = k_dim / 2.0;
        const vc::vc_image rotated =
            from_rule(k_dim, k_dim, [ct, st, mid](int x, int y) {
                const double ox = x - mid;
                const double oy = y - mid;
                const double px = (ox * ct) + (oy * st);
                const double py = (-ox * st) + (oy * ct);
                return 50.0F +
                       (50.0F * static_cast<float>(band_sign(px + mid) *
                                                   band_sign(py + mid)));
            });
        const vc::vc_image r_rot =
            vc::pixelops::corner_response(tensor_of(rotated), k_chosen);
        float max_rot = 0.0F;
        for (vc::image_dim y = 0; y < k_dim; ++y) {
            for (vc::image_dim x = 0; x < k_dim; ++x) {
                max_rot = std::max(max_rot, pixel(r_rot, x, y));
            }
        }

        writer.write(out_dir + "/20_source_board.png",
                     from_rule(k_dim, k_dim,
                               [&board](int x, int y) {
                                   return pixel(board,
                                                static_cast<vc::image_dim>(x),
                                                static_cast<vc::image_dim>(y)) /
                                          100.0F;
                               }),
                     png);
        writer.write(out_dir + "/20_response_linear.png", diverging(r, false),
                     png);
        writer.write(out_dir + "/20_response_compressed.png",
                     diverging(r, true), png);
        writer.write(out_dir + "/20_corners_marked.png", marked(board, found),
                     png);
        writer.write(out_dir + "/20_corners_marked_rotated.png",
                     marked(rotated, vc::pixelops::corner_peaks(
                                         r_rot, max_rot * k_fraction)),
                     png);

        // ---- the numerical inspection -------------------------------------
        //
        // The criterion says the heatmap is for NUMERICAL inspection, so the
        // numbers go here rather than being left to the eye on a PNG.
        //
        // TWO ROWS, because one row cannot show all three regimes. The
        // fixture's transitions sit at k_t (mod k_square) and its band
        // centres at k_c, so:
        //
        //   y = k_t is a TRANSITION row -- it crosses a junction every
        //     k_square px, and between them it runs along a HORIZONTAL edge.
        //     Expect: large POSITIVE spikes at the junctions, negative
        //     between them.
        //   y = k_c is a BAND-CENTRE row -- it crosses a VERTICAL edge every
        //     k_square px, and between them it is FLAT interior.
        //     Expect: negative troughs at the crossings, ~0 between them.
        //
        // Between them that is corner, edge and flat, each at a known x, and
        // the sign alone distinguishes all three. Worth checking against the
        // PNGs rather than instead of them.
        //
        // AND THE FIRST VERSION OF THIS BLOCK WALKED THE WRONG ROW. It used
        // y = k_c and called it "the junction row". k_c is the band CENTRE;
        // the junctions are at k_t. The output was still correct -- an
        // edge/flat profile, troughs at the crossings -- which is exactly why
        // it was not obviously wrong: a plausible picture of the wrong thing.
        for (const auto& row : {std::pair<vc::image_dim, const char*>{
                                    k_t, "y = k_t  (junctions and horizontal "
                                         "edges)"},
                                std::pair<vc::image_dim, const char*>{
                                    k_c, "y = k_c  (vertical edges and flat "
                                         "interior)"}}) {
            std::cout << "\n  R along " << row.second << "\n";
            for (vc::image_dim x = k_t - k_square; x <= k_t + (2 * k_square);
                 x += k_square / 4) {
                const float v = pixel(r, x, row.first);
                const auto bar =
                    static_cast<std::size_t>(std::abs(v) / span * 32.0F);
                std::cout << "    x = " << (x < 100 ? " " : "") << x
                          << "  R = " << (v < 0.0F ? "" : " ") << v << "\t"
                          << std::string(bar, v < 0.0F ? '-' : '+') << "\n";
            }
        }

        std::cout << "\n  wrote 20_source_board.png, 20_response_linear.png,"
                  << "\n        20_response_compressed.png, "
                  << "20_corners_marked.png,"
                  << "\n        20_corners_marked_rotated.png  ->  " << out_dir
                  << "\n";
    });

    std::cout << "20_harris: " << g_passed << " / " << (g_passed + g_failed)
              << " checks passed";
    if (g_skipped > 0) {
        std::cout << "   (" << g_skipped
                  << " group(s) skipped -- unwritten dependency)";
    }
    std::cout << '\n';
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
