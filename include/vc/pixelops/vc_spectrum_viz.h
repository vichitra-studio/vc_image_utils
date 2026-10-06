// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/math/vc_dft.h"     // complex_view
#include "vc/math/vc_grid2d.h"  // grid2d -- dimensions that cannot be wrong

namespace vc::pixelops {

// Turn a 2-D spectrum into pictures you can open.
//
// ---- WHY THIS IS IN pixelops AND NOT math ----
//
// 2.1's rule is phrased about INPUTS -- "does it take a vc_image? no -> math".
// This is the first case the phrasing does not settle: spectrum_viz takes
// complex samples and RETURNS vc_images. Putting it in vc::math would make
// vc::math include vc_image.h, which is the exact dependency vc_scalar.h and
// vc_grid2d.h were created to avoid. So the rule is read as being about the
// dependency rather than the signature, and outputs count. Worth folding back
// into 2.1's wording.
//
// ---- WHAT THE THREE IMAGES ARE ----
//
// Single-channel f32, fftshift'd so DC is in the middle, all values in
// 0.0 .. 1.0. The 0..1 range is not a style choice: the stb f32 write path
// does `v * 255 + 0.5` clamped, so a pixel of 255.0F saturates to white and
// an entire spectrum written that way is a white rectangle.
//
//     log_magnitude      log(1 + |F|), divided by its own max
//     linear_magnitude   |F| divided by its own max, NO log
//     phase              atan2(Im, Re) mapped (-pi, pi] -> 0 .. 1, so 0.5
//                        means zero phase
//
// linear_magnitude exists to be LOOKED AT next to log_magnitude. It is the
// whole argument for the log, and it is cheaper to see than to be told.
//
// ---- THE TRAPS, ALL FOUR ALREADY KNOWN ----
//
// 1. std::atan2(im, re), never std::atan(im/re). atan collapses two
//    quadrants, and Smith's symptom is that the phase then never exceeds
//    +-1.5708. 08_spectrum_viz turns that symptom into an assertion.
//
// 2. Take the log of the RAW magnitudes, then normalise. Normalising first
//    puts the values near or below 1, where log(1+x) ~= x is nearly linear,
//    and the log does nothing -- a black rectangle that looks like the log
//    was never applied.
//
// 3. The phase image of a real photograph is MOSTLY NOISE, and that is
//    correct. Phase has no meaning where the magnitude is negligible, and in
//    a photograph almost every bin is negligible. Judge the magnitude image
//    by eye; judge phase only against a synthetic impulse.
//
// 4. Wrapping. Phase is only defined modulo 2.pi, so a true ramp shows hard
//    boundaries where it crosses +-pi. Not tearing. Do not try to unwrap a
//    2-D phase field -- in 2-D there are many paths between two bins and with
//    any noise they disagree.
struct spectrum_dump {
    vc::vc_image log_magnitude;
    vc::vc_image linear_magnitude;
    vc::vc_image phase;

    // These say whether the picture is TRUSTWORTHY, and the honest numbers
    // are whatever the caller's own image produces -- not a figure quoted
    // from somewhere.

    // |F| at bin 0 of the UNSHIFTED input. Same number before or after the
    // shift; said this way so there is no question which bin is meant.
    float dc_magnitude{0.0F};

    // The element at index count/2 of the magnitudes once SORTED ASCENDING --
    // the upper middle for an even count, with no averaging of the two middle
    // elements. Pinned because "the median" of an even-length list is
    // genuinely ambiguous and an undeclared edge case is how dft1d's
    // empty-input contract went wrong.
    //
    // ALL bins are included, DC among them.
    //
    // Median rather than mean: the mean is dragged up by DC and the few large
    // low-frequency bins, so it describes no actual bin.
    float median_magnitude{0.0F};

    // dc_magnitude / median_magnitude. If this is large, a linear display
    // cannot work -- the entire case for the log, measured rather than
    // asserted.
    //
    // ---- ZERO WHEN THE MEDIAN IS ZERO, AND THAT MEANS UNDEFINED ----
    //
    // Set to 0 when median_magnitude is 0, which is NOT a small ratio -- it
    // is no ratio at all. This is not a rare corner: any sparse synthetic
    // spectrum has it. A pure cosine on an 8x4 grid puts energy in 2 bins out
    // of 32, so 30 bins are exactly zero and the median is 0.
    //
    // So this number is only meaningful on a REAL image. That is also why the
    // example prints it there and asserts it only on a hand-built case whose
    // median is non-zero by construction.
    float dc_to_median{0.0F};

    // Fraction of bins that round to grey 0 in linear_magnitude -- the
    // companion number: how much of the picture a linear display throws away.
    //
    // Counted against what the PNG will ACTUALLY show, not against the float:
    // the stb path writes floor(v * 255 + 0.5), so a bin counts as lost when
    // v * 255 + 0.5 < 1, i.e. v < 1/510.
    float linear_zero_fraction{0.0F};
};

// `spectrum` is dft2d output in its NATURAL order -- unshifted. Shifting is
// this function's job, so a caller that shifts first gets DC back in the
// corner.
//
// ---- AN ALL-ZERO SPECTRUM IS LEGAL AND RETURNS BLACK ----
//
// The transform of a black image is all zeros, which is a legitimate input,
// not an error -- and throwing on legitimate input is exactly the mistake
// dft1d's empty-input contract made. So when the maximum magnitude is 0,
// emit three all-zero images rather than dividing by it, with every
// diagnostic 0 (and linear_zero_fraction therefore 1.0, since every bin is
// lost).
[[nodiscard]] spectrum_dump spectrum_viz(vc::math::complex_view spectrum,
                                         vc::math::grid2d extent);

} // namespace vc::pixelops
