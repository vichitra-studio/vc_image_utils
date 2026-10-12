// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/pixelops/vc_convolve.h"    // vc_kernel, convolve
#include "vc/pixelops/vc_edge_policy.h" // vc_edge_policy

namespace vc::pixelops {

// The discrete Laplacian: the 5-point stencil.
//
// A separate header rather than another factory in vc_convolve.h, because this
// is not a filter alongside box and Gaussian -- it is the FORWARD OPERATOR that
// poisson_fft and poisson_iter invert. Those two will sit beside this file, and
// keeping the general convolution machinery clear of one specific application
// is worth one more header.
//
// ---- WHAT IT IS, FROM THE DIFFERENCE UP ----
//
// The first difference of a row asks how much it changed:  f(x+1) - f(x).
// Run that twice and you get the difference of the differences:
//
//     [ f(x+1) - f(x) ]  -  [ f(x) - f(x-1) ]  =  f(x+1) - 2 f(x) + f(x-1)
//
// The -2 is not a chosen weight. f(x) appears in BOTH differences -- once as
// the left end of the right-hand one, once as the right end of the left-hand
// one -- so it is subtracted twice. As a kernel that is [1, -2, 1].
//
// Do the same down a column, pad both to a common 3x3 grid (a zero weight
// contributes nothing) and ADD. Addition is legal because convolution is
// linear: convolve(f,A) + convolve(f,B) == convolve(f, A+B).
//
//        0  0  0        0  1  0        0  1  0
//        1 -2  1   +    0 -2  0   =    1 -4  1
//        0  0  0        0  1  0        0  1  0
//
// The centre is -2 + -2 = -4: the pixel is hit once by each axis.
//
// NOTE THIS IS NOT SEPARABILITY. A separable kernel is the OUTER PRODUCT of
// two 1-D passes -- apply one, then the other. Here the two passes are SUMMED.
// [1,-2,1] outer [1,-2,1] would be a 3x3 with +4 at the centre and 1s in the
// corners, which is a different operator. The Laplacian does not factor, so
// convolve_separable() is not an option for it.
//
// "Stencil" is the numerical-PDE word for kernel. Nothing new is meant by it,
// and "5-point" merely counts the non-zero weights.
//
// ---- WHAT IT MEASURES: BEND, NOT SLOPE ----
//
// Rearranged, the stencil is
//
//     (four neighbours) - 4 (centre)   ==   4 * [ (neighbour mean) - centre ]
//
// so it asks whether a pixel sits BELOW or ABOVE the average of its
// surroundings. Three rows, by hand:
//
//     flat   5  5  5  5  5   ->   0  0  0
//     ramp   2  4  6  8 10   ->   0  0  0      <- the one that matters
//     bowl   4  1  0  1  4   ->   2  2  2
//
// The ramp climbs steadily and the operator reports NOTHING. Compare the FIRST
// difference on that same ramp, which is 2,2,2,2 and fires at every pixel:
//
//     first difference    "is it sloping?"
//     second difference   "is the slope CHANGING?"
//
// A smooth brightness gradient across a photo -- a lit wall, lens vignetting --
// has slope everywhere and bend nowhere, and this operator ignores it entirely.
// That is the property every later use depends on.
//
// The bowl row is x^2 at x = -2..2, and its second difference is EXACTLY 2 with
// no error term, which is why the acceptance gate below is an equality rather
// than a tolerance.
//
// ---- SIGN CONVENTION ----
//
// Centre -4. This is the mathematics convention: at a local MAXIMUM the result
// is NEGATIVE, because the neighbours are below the centre. A bright isolated
// pixel comes back as -4, not +4.
//
// Image-processing sources that use the Laplacian for SHARPENING routinely
// negate it ([[0,-1,0],[-1,4,-1],[0,-1,0]]) so that bright features stay
// bright and the result can be added straight back to the image. Both are
// called "the Laplacian". Nothing here detects which one a given source meant;
// this file declares the maths convention and the tests pin it.
//
// ---- THE OUTPUT IS SIGNED, AND THE KERNEL SUMS TO ZERO ----
//
// make_laplacian() is THE FIRST KERNEL ANYWHERE IN THIS REPOSITORY WHOSE WEIGHTS
// SUM TO ZERO -- checked by grepping every vc_kernel construction in src/,
// examples/ and tests/, not assumed. make_box, make_gaussian, make_gaussian_1d_*
// and the pyramid's Burt kernel all sum to 1, because they preserve average
// brightness; 05_convolve's deliberately lopsided test kernel sums to 6.
//
// A derivative does the opposite of preserving the average: it ANNIHILATES it.
// vc_kernel::sum() exists for exactly this assertion, and the two families want
// OPPOSITE tests -- sum() == 1 there, sum() == 0 here.
//
// Consequences, both of which the acceptance checks state:
//
//   - The output takes NEGATIVE values. Anything that clamps it at 0 destroys
//     half the answer, and on a smooth image would still look plausible.
//   - Under `wrap`, the sum of every output sample is EXACTLY zero, for any
//     input. Circular convolution totals multiply: sum(x * h) = sum(x).sum(h),
//     and sum(h) is 0. S7 depends on this; what it implies there is S7's to
//     say, not this header's.
//
// ---- BOUNDARIES ----
//
// Only the INTERIOR is meaningful. An edge pixel's stencil reaches for
// neighbours that do not exist, and vc_edge_policy decides what it gets --
// invented data either way. The gates below are therefore asserted on interior
// pixels, and the boundary is asserted only where it is exactly hand-checkable.
//
// On a CONSTANT image of value k, `zero` is exactly computable and the other
// three are exactly 0 everywhere:
//
//     interior   4 of 4 neighbours in range   4k - 4k  =    0
//     edge       3 of 4                       3k - 4k  =   -k
//     corner     2 of 4                       2k - 4k  =  -2k
//
// which is the same DC loss vc_convolve.h describes for the box blur's dark
// border, in its cleanest possible form.
//
// ---- NOT THE PYRAMID'S "LAPLACIAN" ----
//
// vc_pyramid.h has vc_laplacian_pyramid and laplacian_pyramid(). Those bands
// are a difference of Gaussians and are NOT this operator applied at each
// level. Szeliski calls the pyramid's name a misnomer himself; the name
// collision is inherited from the literature, not introduced here.
//
// ---- TWO THINGS DELIBERATELY ABSENT ----
//
// No 9-point (8-neighbour) variant. [[1,1,1],[1,-8,1],[1,1,1]] is the usual
// alternative. It also sums to zero, but it is a DIFFERENT operator: on x^2+y^2
// it gives 12 rather than 4, so it approximates three times the Laplacian and
// needs a 1/3 factor to be comparable at all. (It is often reached for on
// isotropy grounds -- that the diagonals make it less direction-dependent -- but
// that claim is NOT verified here and is left unasserted rather than repeated.)
// One stencil, derived and tested, is what the solvers need; a second would
// double the surface with no caller.
//
// No grid-spacing parameter. The continuous operator on a grid of spacing h
// carries a factor 1/h^2. Pixels are one unit apart, h = 1, and a parameter
// that is always 1 is a parameter that is always wrong somewhere.

// The 5-point stencil as a kernel. 3x3, weights {0,1,0, 1,-4,1, 0,1,0}.
//
// Symmetric, so reversed() returns the same weights and convolve() and
// correlate() agree exactly on it -- which also means a TRANSPOSED stencil is
// not a possible bug here, since the stencil is its own transpose. No test can
// catch that, and none needs to.
[[nodiscard]] vc_kernel make_laplacian();

// Apply it. Equivalent to convolve(src, make_laplacian(), policy) and
// implemented as exactly that -- no second code path to drift.
//
// Output geometry, channel count and dtype follow the source; each channel is
// filtered independently; f32 input only. All of that is convolve()'s contract,
// inherited rather than restated, including the rejection of a non-f32 source.
[[nodiscard]] vc::vc_image laplacian(const vc::vc_image& src,
                                     vc_edge_policy policy);

} // namespace vc::pixelops
