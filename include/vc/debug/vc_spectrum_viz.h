// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/math/vc_dft.h"     // vc_complex_view
#include "vc/math/vc_grid2d.h"  // grid2d -- dimensions that cannot be wrong

namespace vc::debug {

// Turn a 2-D spectrum into pictures you can open.
//
// ---- WHY THIS IS IN debug, NOT pixelops AND NOT math ----
//
// Two questions, in order, and an earlier draft only asked the second.
//
// FIRST: is this pipeline machinery at all? No. Nothing in a processing
// pipeline calls this -- it exists so a human can look at a spectrum and
// decide whether a filter did what was intended. That is what vc::debug is
// for, and vc_image_dumper is the precedent: encode an image so somebody can
// open it, never part of the result.
//
// pixelops holds operations the pipeline RUNS -- warp, convolve, edge policy.
// spectrum_viz is not one of those, and filing it there would have made
// pixelops the place where anything image-shaped lands.
//
// SECOND, had it been pipeline machinery: 2.1's math/pixelops rule is phrased
// about INPUTS -- "does it take a vc_image? no -> math" -- and would not have
// settled it either way, because this takes complex samples and RETURNS
// vc_images. That reading still holds and is now in 2.1: naming vc_image in
// EITHER direction forces the vc_image.h dependency that vc_scalar.h and
// vc_grid2d.h exist to keep out of vc::math. It just is not the question that
// decides this file.
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
    // Set to 0 when the median is indistinguishable from zero, which is NOT a
    // small ratio -- it is no ratio at all. Not a rare corner: any sparse
    // synthetic spectrum has it. A pure cosine on an 8x4 grid puts energy in 2
    // bins out of 32.
    //
    // The test is RELATIVE -- median > max * 1e-6 -- and an earlier version
    // testing `> 0` was wrong in a way worth recording. "Exactly zero" does
    // not survive floating point: a flat field's non-DC bins come back around
    // 1e-27 rather than 0, the guard never fired, and the atlas printed a
    // DC:median of 4.2e30 for a uniform grey square. Meaningless, and it did
    // not LOOK meaningless.
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
// emit BLACK MAGNITUDE images rather than dividing by it, with every
// diagnostic 0 (and linear_zero_fraction therefore 1.0, since every bin is
// lost).
//
// The PHASE image is NOT black in that case, and an earlier draft of this
// comment said "three all-zero images", which was wrong. atan2(0, 0) is 0 by
// definition, and 0 maps to the middle of the range -- so a spectrum of
// nothing has a phase image that is uniformly 0.5, mid-grey. That is correct
// and not worth special-casing: phase is meaningless where the magnitude is
// zero, which is the whole of Nuisance 4.
[[nodiscard]] spectrum_dump spectrum_viz(vc::math::vc_complex_view spectrum,
                                         vc::math::grid2d extent);

} // namespace vc::debug
