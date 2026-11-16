// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/pixelops/vc_convolve.h" // vc_kernel, convolve, convolve_separable
#include "vc/pixelops/vc_edge_policy.h" // vc_edge_policy

namespace vc::pixelops {

// The image gradient: the FIRST derivative, in two directions, as a Sobel pair.
//
// vc_laplacian.h is the SECOND derivative and says "is the slope changing".
// This file says "is it sloping, and which way". Same family -- both are a
// derivative welded to a blur (Szeliski 3.2.3) -- different order, and the
// difference is not cosmetic: a brightness ramp across a lit wall reads ZERO
// to the Laplacian and reads the SAME NON-ZERO EVERYWHERE here. Neither is
// the better answer; they answer different questions.
//
// Derivations, the hand-worked numbers and the three conventions below are in
// curriculum/notes/gradients.txt (X3, sections 1-16). This header carries the
// contract, the conventions and the traps.
//
// ============================================================================
// THE KERNEL IS AN OUTER PRODUCT, AND THAT IS WHERE IT COMES FROM
// ============================================================================
//
// Sobel is not a magic 3x3 of {1,2,1,0,0,0,-1,-2,-1}. It is a DERIVATIVE in
// one axis times a SMOOTH in the other:
//
//     derivative   [-1  0  +1] / 2        the central difference
//     smooth       [ 1  2   1] / 4        the binomial/tent weights
//
//     sobel_x  =  smooth DOWN THE COLUMN  *  derivative ACROSS THE ROW
//     sobel_y  =  derivative DOWN THE COLUMN  *  smooth ACROSS THE ROW
//
// and that star is literal: vc_convolve.h's operator*(column, row) builds the
// outer product, COLUMN FIRST, because (N x 1)(1 x M) = (N x M) is ordinary
// matrix multiplication. Reverse the operands and it throws.
//
// Read that twice, because the axes cross: gradient_x differentiates along x
// and SMOOTHS along y. The smoothing is perpendicular to the direction being
// measured, and it is there to average out noise along the edge the operator
// is trying to find -- noise that is uncorrelated between neighbouring rows,
// while the edge is the same in all of them.
//
// TWO CONSEQUENCES, BOTH USED BELOW:
//
//   1. IT IS SEPARABLE. 6 multiplies per pixel instead of 9, and more to the
//      point, the four 1-D factories below are the honest statement of what
//      the operator is. make_sobel_x() is their outer product EXACTLY -- the
//      weights are dyadic (eighths), so the two paths agree bit for bit and
//      not merely to tolerance. The acceptance suite asserts that.
//
//   2. THE MIDDLE COLUMN OF sobel_x IS ALL ZEROS, and the middle ROW of
//      sobel_y is all zeros, because the derivative factor has a zero in the
//      middle. That is a permanent sanity check: if the centre tap of a
//      derivative kernel is ever non-zero, the kernel is wrong. A centred
//      difference does not look at the pixel it is standing on.
//
// ============================================================================
// CONVENTION 1 -- NORMALISATION. THE KERNEL IS SCALED BY 1/8, NOT LEFT RAW
// ============================================================================
//
// The unnormalised Sobel reports a step of height h as 4h, because the
// positive half of the smoothing factor weighs 1+2+1 = 4. That is a slope
// estimate multiplied by an arbitrary constant, and an arbitrary constant in
// a measurement is a unit nobody wrote down.
//
// So the factors carry their divisors: 1/2 on the derivative, 1/4 on the
// smooth, and 1/2 * 1/4 = 1/8 on the 2-D kernel. Szeliski's figure 3.14
// prints both, and they are consistent.
//
// NEITHER DIVISOR IS ARBITRARY:
//
//     /4   makes the smoothing a true weighted average -- the weights sum to
//          1, so a flat region keeps its brightness. (vc_convolve.h makes the
//          same demand of make_box and make_gaussian.)
//
//     /2   is the `2h` of the central difference, (f(x+1) - f(x-1)) / 2h, at
//          h = 1. It is the SAME formula as 04_numderiv's, not a cousin of it.
//
// What this buys, concretely: on a clean vertical step of height 100, this
// operator returns EXACTLY 50 -- which is (100 - 0)/2, the central difference
// across the step, in brightness units per pixel. A number with a meaning.
//
// A grid-spacing parameter is deliberately absent for the reason stated in
// vc_laplacian.h: pixels are one unit apart, and a parameter that is always 1
// is a parameter that is always wrong somewhere.
//
// ============================================================================
// CONVENTION 2 -- THE FLIP. THESE KERNELS ARE STORED ROTATED 180 DEGREES
// ============================================================================
//
// THE SINGLE MOST DANGEROUS LINE IN THIS FILE. The derivative factor is
// ANTISYMMETRIC, so rotating it is NEGATING it -- and vc_convolve.h spells out
// what that costs: every edge points the wrong way and nothing crashes. A
// blur cannot expose this mistake; a derivative is the first kernel in this
// repository that can.
//
// What is wanted:
//
//     gradient_x > 0   where the image gets BRIGHTER TO THE RIGHT
//     gradient_y > 0   where the image gets BRIGHTER GOING DOWN THE SCREEN
//
// Textbook figures (Szeliski fig 3.14, Harris & Stephens' (-1,0,1)) print the
// kernel in the layout that produces that under CORRELATION. This file goes
// through convolve(), which is correlation with the kernel reversed, so the
// WEIGHTS ARE STORED ALREADY REVERSED:
//
//     make_sobel_x()           make_sobel_y()
//      1/8   0  -1/8            1/8   1/4   1/8
//      1/4   0  -1/4              0     0     0
//      1/8   0  -1/8           -1/8  -1/4  -1/8
//
// Note the printed +1 is on the LEFT in sobel_x and on TOP in sobel_y, which
// reads backwards and IS backwards -- that is the point. Convolve flips it
// back. Why convolve rather than correlate(): convolution is the one with
// commutativity, associativity and the convolution theorem (vc_convolve.h),
// so kernels composed with these later -- blur-then-differentiate -- behave.
//
// THE CHEAPEST CHECK OF THIS, AND IT IS WORTH REMEMBERING: a 3x3 patch that
// is 0 0 100 in every row must give gradient_x = +50. If it gives -50 the
// flip is wrong. Nothing else about the operator needs to be understood to
// run that check.
//
// ============================================================================
// CONVENTION 3 -- THE ANGLE. POSITIVE ANGLES ROTATE CLOCKWISE ON SCREEN
// ============================================================================
//
// gradient_direction() returns atan2(dy, dx), in RADIANS, with NO correction
// applied. Measured from the +x axis (rightwards), and:
//
//     POSITIVE ANGLES ROTATE CLOCKWISE ON SCREEN, NOT COUNTER-CLOCKWISE,
//     BECAUSE THE ROW INDEX GROWS DOWNWARD.
//
// That sentence is the convention, and it is stated rather than fixed. Graph
// paper has y increasing upward; an image buffer has it increasing downward,
// and flipping an axis mirrors every angle. So +pi/2 points DOWN the screen,
// not up.
//
// Worked, because the number looks wrong the first time: a patch whose bright
// corner is UPPER RIGHT gives dx = +37.5 and dy = -37.5, so atan2 returns
// -45 degrees -- which on graph paper means down-and-right. It is not wrong.
// dx > 0 says brightness rises rightward; dy < 0 says brightness rises as the
// row index FALLS, i.e. upward. Right and up: the upper-right corner, where
// the bright region actually is.
//
// THE ALTERNATIVE WAS CONSIDERED AND REJECTED: storing atan2(-dy, dx) would
// make angles read counter-clockwise and the number would "look right". It
// also buries a negation inside a function, and a buried negation is a thing
// you forget -- then compose with, then debug for an afternoon. Document the
// axis instead of fighting it.
//
// DIRECTION, NOT ORIENTATION, AND THE NAME IS DELIBERATE. These differ:
//
//     DIRECTION    the full 360 degrees. -45 and +135 are DIFFERENT: one is
//                  dark-to-bright, the other bright-to-dark.
//     ORIENTATION  the undirected line, modulo 180. Both of the above are
//                  "the 45-degree diagonal".
//
// This function returns the first. Edge-linking literature wants the second
// and calls it "orientation", which is why the words are pinned here rather
// than used loosely. The hue dump uses the full 360 so the difference is
// visible.
//
// THE RANGE IS (-pi, +pi] FOR GRADIENT IMAGES, AND THAT QUALIFIER IS NOT
// PEDANTRY. atan2 itself returns the CLOSED range [-pi, +pi]: atan2(-0.0f,
// -1.0f) is -pi exactly, not +pi, because negative zero is a different float
// from zero and atan2 reads its sign. The half-open range holds for anything
// gradient_x/gradient_y produced, because convolve() accumulates into an
// accumulator initialised to +0.0F and +0 + (-0) is +0 under round-to-nearest,
// so a convolution output can never BE negative zero. It does not hold for an
// arbitrary pair of f32 images, which these signatures also accept. If you
// ever hand-build a dy containing -0.0f and pair it with a negative dx, expect
// -pi.
//
// UNDEFINED WHERE THE MAGNITUDE IS ZERO. atan2(0,0) is 0 by IEEE, and
// atan2(+0,-0) is pi -- a value that carries no information but looks like it
// does. On a flat image this function returns numbers that must not be read.
// Threshold on magnitude first. The acceptance suite asserts nothing about
// direction where magnitude is 0, for exactly this reason.
//
// ============================================================================
// WHAT SOBEL GETS WRONG, STATED SO IT IS NOT DISCOVERED AS A BUG
// ============================================================================
//
// IT OVER-REPORTS DIAGONAL EDGES BY ABOUT 6 PERCENT. A 45-degree step of the
// same height as a vertical one comes back at EXACTLY 3*sqrt(2)/4 = 1.0607
// times the vertical answer. The operator is not isotropic: a 3x3 grid has
// no way to be, since the diagonal neighbours are sqrt(2) away and the
// axis-aligned ones are 1. This is asserted as an exact ratio in the
// acceptance suite rather than hidden behind a tolerance.
//
// THE SCALE DIAL IS WELDED. There is no sigma parameter. The smoothing factor
// [1,2,1]/4 is the smallest non-trivial blur there is, so this operator sees
// a 3-pixel neighbourhood and nothing larger -- it cannot be asked for "only
// the big structural edges, ignore the brickwork".
//
// THE DIAL COMES BACK FOR FREE AND MUST NOT BE BUILT HERE: blur first with
// make_gaussian(sigma), then call this. Differentiating a blurred image IS
// the derivative-of-Gaussian at that sigma, by associativity (vc_convolve.h).
// Spelled as a composition instead of a parameter, with both pieces already
// built.
//
// NOT AN EDGE DETECTOR. A thresholded magnitude map gives THICK edges -- a
// ridge several pixels wide, because the kernel is 3 wide and real edges are
// blurred -- and a single global threshold drops a real boundary out wherever
// contrast falls. Thinning the ridge (non-maximum suppression) and tracking a
// curve through weak stretches (hysteresis) are what Canny adds, and neither
// is built here. Szeliski 7.2.1-7.2.2. Documented as a limitation, not a
// defect.
//
// WHAT HYSTERESIS ACTUALLY NEEDS, stated carefully because an earlier version
// of this comment got it wrong: CONNECTEDNESS, which a pixel map already has.
// It is not a second filter -- a weak pixel is kept on the strength of its
// NEIGHBOURS rather than its own value, so the decision is non-local. The
// standard algorithm is a flood fill: mark everything above the high
// threshold, then keep every pixel above the LOW threshold that is 8-connected
// to one of those. A stack and a visited array over the grid; no edgel, no
// chain, no curve. Szeliski presents it in 7.2.2 because THAT SECTION'S
// framing is curves, and "presented alongside linking" is not "requires
// linking".
//
// ============================================================================
// DECLARATIONS
// ============================================================================

// ---- the four 1-D factors ---------------------------------------------------
//
// Four names rather than two, following make_gaussian_1d_x / _y: the SHAPE
// carries information the weights cannot. convolve_separable() requires its
// first argument to be 1 tap tall and its second to be 1 tap wide, so passing
// the x-factor where the y-factor belongs is rejected at the call rather than
// silently differentiating the same axis twice.
//
// The derivative factors are ANTISYMMETRIC and are stored REVERSED (see
// convention 2): weights {+1/2, 0, -1/2}, which prints backwards on purpose.
// The smooth factors are symmetric, so reversal is invisible to them -- which
// is precisely why the derivative is the one that must be got right.

// Row, 3 wide x 1 tall. Weights {+0.5, 0, -0.5}. sum() == 0.
[[nodiscard]] vc_kernel make_sobel_derivative_1d_x();

// Column, 1 wide x 3 tall. Weights {+0.5, 0, -0.5}. sum() == 0.
[[nodiscard]] vc_kernel make_sobel_derivative_1d_y();

// Row, 3 wide x 1 tall. Weights {0.25, 0.5, 0.25}. sum() == 1.
[[nodiscard]] vc_kernel make_sobel_smooth_1d_x();

// Column, 1 wide x 3 tall. Weights {0.25, 0.5, 0.25}. sum() == 1.
[[nodiscard]] vc_kernel make_sobel_smooth_1d_y();

// ---- the two 2-D kernels ----------------------------------------------------
//
// Each is EXACTLY the outer product of its two factors -- the weights are
// eighths, so this is an identity and not an approximation.
//
//     make_sobel_x()  ==  make_sobel_smooth_1d_y() * make_sobel_derivative_1d_x()
//     make_sobel_y()  ==  make_sobel_derivative_1d_y() * make_sobel_smooth_1d_x()
//
// THE TWO APIS THAT TAKE THESE FACTORS WANT THEM IN MIRROR-IMAGE ORDER, and
// that is the one thing to read twice here:
//
//     operator*(COLUMN, ROW)                 -- column first
//     convolve_separable(src, ROW, COLUMN)   -- row first
//
// so the SAME operator is spelled two ways, with the factors swapped:
//
//     make_sobel_y()                 ==  derivative_1d_y * smooth_1d_x
//     convolve_separable for sobel_y ==  (smooth_1d_x, derivative_1d_y)
//
// Neither order can be got wrong silently -- operator* rejects the reverse by
// ALGEBRA, since a row on the left is a 1x1 inner product, and
// convolve_separable rejects it by SHAPE. Both diagnostics are good. But a
// caught mistake is still a mistake made, and writing make_sobel_y in
// convolve_separable's order is the obvious way to make it: that is exactly
// what happened on the first implementation pass.
//
// -- and these are the DEFINITIONS, not facts about them. Built as products,
// the full and separable paths agree BY CONSTRUCTION; the nine literal weight
// checks in the acceptance suite stay anyway, as an independent pin on the
// result rather than a guard against hand-maintained drift.
//
// Both sum to exactly 0: a derivative annihilates the average. make_laplacian()
// was the first kernel in this repository with that property; these are the
// second and third.

// 3x3. Positive weights on the LEFT (see convention 2). Middle column zeros.
[[nodiscard]] vc_kernel make_sobel_x();

// 3x3. Positive weights on TOP (see convention 2). Middle row zeros.
[[nodiscard]] vc_kernel make_sobel_y();

// ---- the operations ---------------------------------------------------------
//
// Output geometry, channel count and dtype follow the source; each channel is
// filtered independently; f32 input only. That is convolve()'s contract,
// inherited rather than restated, including the rejection of a non-f32 source.
//
// IMPLEMENTATION IS FREE between convolve(src, make_sobel_x(), policy) and
// convolve_separable(src, derivative_x, smooth_y, policy). The acceptance
// suite pins BOTH to the same numbers bit for bit, so either is correct and
// the choice is about cost, not behaviour.

// d/dx. Positive where the image gets brighter TO THE RIGHT.
[[nodiscard]] vc::vc_image gradient_x(const vc::vc_image& src,
                                      vc_edge_policy policy);

// d/dy. Positive where the image gets brighter GOING DOWN THE SCREEN.
[[nodiscard]] vc::vc_image gradient_y(const vc::vc_image& src,
                                      vc_edge_policy policy);

// sqrt(dx^2 + dy^2), per pixel, per channel. Always >= 0.
//
// Takes the two gradient images rather than the source, because the caller
// almost always wants dx and dy as well and recomputing them here would
// either double the work or force a four-output function. The cost is that
// the two arguments can disagree: mismatched dimensions, channel counts or
// dtypes are rejected with invalid_argument rather than read past.
//
// NOT LINEAR, and worth remembering before composing it with anything: the
// magnitude of a sum is not the sum of magnitudes. No kernel computes this.
[[nodiscard]] vc::vc_image gradient_magnitude(const vc::vc_image& dx,
                                              const vc::vc_image& dy);

// atan2(dy, dx), radians. In (-pi, +pi] for dx/dy that gradient_x/_y produced;
// see convention 3 for why an arbitrary f32 pair can also give exactly -pi.
// CLOCKWISE POSITIVE, and UNDEFINED where the magnitude is zero.
//
// Same argument-pairing contract and the same rejections as above.
[[nodiscard]] vc::vc_image gradient_direction(const vc::vc_image& dx,
                                              const vc::vc_image& dy);

// ---- thresholding, which is all `edges` is --------------------------------
//
// Takes a MAGNITUDE map and returns a binary map: 1.0F where the magnitude is
// above the threshold, 0.0F elsewhere. Geometry, channel count and dtype
// follow the input; each channel is thresholded independently.
//
// It takes the magnitude rather than the source for the same reason
// gradient_magnitude takes dx and dy: the caller already has it, and
// recomputing here would either double the work or hide a second set of
// gradient calls inside something that reads like a comparison.
//
// STRICTLY GREATER, NOT GREATER-OR-EQUAL, and the reason is the degenerate
// case rather than taste. A flat region has magnitude EXACTLY 0. With >=, a
// threshold of 0 would mark every flat pixel as an edge -- the whole image
// lights up, and the one threshold a caller might reach for first is the one
// that behaves worst. With >, a threshold of 0 means "any non-zero gradient",
// which is what it reads like. A pixel sitting exactly ON the threshold is
// therefore OFF, and the acceptance suite pins that.
//
// THE OUTPUT IS f32 CARRYING 0.0F AND 1.0F, not u8. It stays in the pipeline:
// it can be multiplied by another map, convolved, or dumped by the same
// writer as everything else, with no dtype conversion at the seam.
//
// THE THRESHOLD MUST BE FINITE AND >= 0. Magnitude is never negative, so a
// negative threshold marks EVERY pixel including the flat ones, and a NaN
// threshold makes every comparison false and marks NONE -- both are silent,
// and both are a mistake at the call site rather than a thing to honour.
// Rejected with invalid_argument.
//
// THIS IS NOT AN EDGE DETECTOR IN CANNY'S SENSE, and the limitation is the
// deliverable, not an apology -- see "WHAT SOBEL GETS WRONG" above and
// curriculum/notes/gradients.txt section 23. Two consequences are VISIBLE in
// the dumps 18_edges.cpp writes, which is why they are written:
//
//   THE EDGES COME OUT THICK. The ridge in the magnitude map is several
//   pixels wide -- the kernel is 3 wide and real edges are blurred -- and
//   thresholding keeps the whole ridge, not its crest. That is what
//   non-maximum suppression would thin, and the suite ASSERTS the thickness
//   rather than merely noting it.
//
//   ONE GLOBAL THRESHOLD IS FRAGILE. Where a single physical boundary runs
//   through falling contrast, one threshold keeps part of it and drops the
//   rest, breaking a closed contour into pieces. That is what hysteresis's
//   second threshold would carry through: the faint arc is CONNECTED to the
//   bright one, and a per-pixel threshold has no way to care. Fixing it needs
//   connectedness, not a new filter -- see the note above.
[[nodiscard]] vc::vc_image edge_map(const vc::vc_image& magnitude,
                                    float threshold);

} // namespace vc::pixelops
