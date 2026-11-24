// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <vector>

#include "vc/core/vc_image.h"
#include "vc/pixelops/vc_edge_policy.h" // vc_edge_policy

namespace vc::pixelops {

// The Harris corner detector: the structure tensor, its response, and peaks.
//
// Harris & Stephens, "A Combined Corner and Edge Detector", AVC 1988.
//
// WHERE THE DERIVATION IS, AND WHY IT IS NOT HERE. The route from the
// sum-of-squared-differences of a shifted patch to the three sums below is
// curriculum/notes/corners.txt section 7.4, and why two large eigenvalues mean
// a corner is sections 15-18 of the same file. THIS HEADER DELIBERATELY DOES
// NOT REPEAT EITHER. It carries the contract, the conventions and the traps;
// the reasoning lives in the note, and a third copy in a header is a third
// place for it to drift.
//
// WHAT THE PAPER STATES, which is what this file implements:
//
//     A = X^2 (x) w        X, Y are the image gradients
//     B = Y^2 (x) w        w is a GAUSSIAN window (the paper's correction #2
//     C = (XY) (x) w         to Moravec, not a modern embellishment)
//
//     Tr(M)  = A + B                   Det(M) = AB - C^2
//     R = Det - k Tr^2
//
//     R > 0 in the corner region, R < 0 in the edge regions, small in the flat
//     region. The flat region is pinned by Tr falling below a threshold, NOT
//     by R -- R is small-but-POSITIVE there. Two tests, not one.
//
// THE PAPER STATES NO VALUE FOR k. Checked against the rendered pages 4 and 5,
// not the text layer, which this scan garbles. Choosing k is the caller's, and
// there is no default here for that reason.
//
// ============================================================================
// TRAP 1 -- SQUARE FIRST, THEN BLUR. NEVER BLUR THEN SQUARE
// ============================================================================
//
// THE SINGLE MOST DANGEROUS LINE IN THIS FILE, and it compiles and runs either
// way. The window is applied to the PRODUCTS of gradients:
//
//     A = blur(X * X)          CORRECT
//     A = blur(X) * blur(X)    WRONG, and quiet
//
// Those are different quantities: sum(w X^2) versus (sum(w X))^2. The second
// is the square of an AVERAGE, and averaging a gradient field is exactly what
// destroys a corner -- a corner has gradients pointing opposite ways, so they
// CANCEL in the average and the patch reads as flat. The error does not throw,
// does not NaN, and produces a plausible-looking response map with the corners
// missing. 20_harris.cpp gates it.
//
// ============================================================================
// TRAP 2 -- THERE ARE TWO GAUSSIANS, DOING TWO DIFFERENT JOBS
// ============================================================================
//
// And this function only owns the second one:
//
//     PRE-SMOOTHING the IMAGE, before gradients are taken, so that the
//       tangent-plane approximation underneath the whole method is a fair
//       description of the pixels (corners.txt section 9). The 1988 paper does
//       not discuss it; every working implementation does it.
//
//     THE WINDOW w, applied to the gradient PRODUCTS, so that E has no
//       preferred direction and the response has a findable summit rather than
//       a plateau (corners.txt sections 7.5 and 20). This is `window_sigma`.
//
// AND THIS FUNCTION OWNS BOTH OF THEM. An earlier version of this header took
// gradients and argued that pre-smoothing should stay a composition the caller
// writes, on the precedent of vc_gradients.h refusing a sigma. THAT ARGUMENT
// WAS WRONG, and the line it got wrong is worth keeping:
//
//     A PRIMITIVE COMPOSES. A NAMED ALGORITHM OWNS ITS PARAMETERS.
//
// gradient_x is a primitive -- one operation, and composing a blur in front of
// it is natural and visible. Harris is a named algorithm out of a paper, with
// published tuned scales; its parameters are part of its identity, the way
// Canny owns its sigma rather than demanding pre-blurred input. Handing
// sigma_d to the caller did not make it visible, it made it UNCHECKABLE: the
// scale that half the literature's tuning is about sat outside the contract,
// so no gate could reach it and no rejection could cover it.
//
// THE LITERATURE NAMES BOTH, AND SETS THEM DIFFERENTLY. corners.txt section
// 23 left "is the first Gaussian needed, and does the gradient operator
// already carry it" open. Schmid, Mohr and Bauckhage (2000) answer the first
// half: measuring REPEATABILITY -- how often a keypoint found in one image
// reappears within ~1.5 px of the right place in a transformed one -- they
// report the Gaussian-derivative Harris works best at
//
//     sigma_d = 1   the DERIVATIVE scale: the pre-smoothing above
//     sigma_i = 2   the INTEGRATION scale: `window_sigma` here
//
// So both are wanted, and THE WINDOW IS THE WIDER OF THE TWO. That ordering
// has a mechanism behind it and not just a measurement: the gradient kernel
// has already mixed neighbouring pixels, so an integration window narrower
// than the derivative's support averages samples that are not independent and
// adds no information. THE RATIO IS NOT A LAW -- one study, planar transforms,
// repeatability as the only score -- and `window_sigma` still has no default
// here, for the reason given above.
//
// WHY THE DERIVATIVE'S OWN GAUSSIAN SMOOTHS IN BOTH DIRECTIONS -- A DIFFERENT
// QUESTION FROM THE ONE ABOVE. Inside the FIRST Gaussian alone there are two
// blurs, doing unrelated jobs. A derivative across the row factors into
// "smooth down the column" times "difference across the row", and:
//
//     DOWN THE COLUMN, perpendicular to the differencing, the blur averages
//       rows that all report the same transition. Noise is independent row to
//       row and cancels; a straight edge is constant down the column and
//       survives untouched. Free accuracy -- and the one Sobel already has.
//
//     ACROSS THE ROW, along the differencing, the blur is NOT noise reduction.
//       It sets the WIDTH OVER WHICH "CHANGE" IS MEASURED. A bare (-1, 0, 1)
//       measures change across two pixels, and two pixels is where sensor
//       noise lives, so signal and noise come back in the same band. Blur
//       first and the difference is taken between two AVERAGES: a lone spike
//       is divided down against its own zero neighbours on both sides, while
//       a real step has one high side already and barely moves.
//
// THE PERPENDICULAR BLUR CANNOT DO THE SECOND JOB -- a spike one pixel wide
// across the row is still one pixel wide after averaging columns. So the
// direction Sobel smooths is the one that was already free, and the direction
// it leaves bare is the one that selects scale. corners.txt section 24 carries
// the spike-against-step numbers.
//
// CORNERS PAY FOR A NOISY DERIVATIVE TWICE, WHERE EDGES PAY ONCE. M is built
// from PRODUCTS, and zero-mean noise in a gradient becomes a POSITIVE bias
// once squared, because the average of a square is never zero. Derivative
// noise does not average out of A and B -- it accumulates there, lifting the
// tensor in flat regions as well as real ones. That is the mechanism behind
// sigma_d mattering for THIS operator, rather than an empirical preference.
//
// AND ONE HONEST LIMIT ON "FREE": the perpendicular blur is free for a
// STRAIGHT edge, where the signal is constant along it. A corner is by
// definition where an edge stops being straight, so there that blur rounds the
// very thing being located. AT A CORNER BOTH BLURS COST LOCALISATION, which is
// the real reason sigma_d stays small while sigma_i is allowed to grow.
//
// WHAT COMPOSING ACTUALLY GIVES YOU, PRECISELY -- the second half of section
// 23's question. gradient_x is Sobel, which factors as [1,2,1] smoothing down
// the column times [-1,0,1] differencing across the row (vc_gradients.h says
// so). Blur at sigma and then call gradient_x, and in the DIFFERENCING
// direction you get exactly a derivative-of-Gaussian at sigma, by the
// associativity above. But PERPENDICULAR to it you get sigma AND THEN Sobel's
// own [1,2,1], whose variance is 0.5. The effective scales are sigma across
// and sqrt(sigma^2 + 0.5) down -- 1.0 against 1.22 at sigma = 1, anisotropic
// by about a fifth, shrinking as sigma grows past Sobel's own ~0.71. So the
// operator carries PART of the first Gaussian, in one direction only, at a
// scale you cannot set.
//
// A clean isotropic derivative-of-Gaussian now needs no hand-built kernel at
// all: make_gaussian_derivative_1d_x/y(sigma) in vc_convolve.h fuse the blur
// and the difference exactly, and were added for this. The hand-check that
// the fusion is exact: [1,2,1] (x) [-1,0,1] = [-1,-2,0,2,1], antisymmetric
// and summing to zero.
//
// IF YOU DO BUILD IT BY HAND, the factor along the DIFFERENCING direction of
// a single convolve_separable() call must be the GAUSSIAN CONVOLVED WITH THE
// DIFFERENCE, not a bare central difference -- a bare difference there is
// precisely the operator this trap just described, missing the
// scale-selecting blur. A bare central difference is correct only once the
// blur has already been applied in an EARLIER pass; the construction section
// further down sets out both routes and why they coincide. DO NOT reach for a
// sampled analytic Gaussian derivative as a third option -- it is a
// measurably different operator, and the suite-coverage note below has the
// number. Whether the
// anisotropy matters for corner LOCATION is worth measuring rather than
// assuming -- the three signs are robust to it, which group 1b of
// examples/20_harris.cpp now gates; sub-pixel position need not be.
//
// ============================================================================
// TRAP 3 -- THIS OPERATOR IS BLIND TO THE GRADIENT SIGN. DO NOT "FIX" IT
// ============================================================================
//
// Harris writes X = I (x) (-1,0,1) ~= dI/dx without saying whether (x) is
// convolution or correlation, and under convolution the kernel is reversed, so
// that expression is MINUS dI/dx. The paper is silent and nothing in it
// depends on the answer:
//
//     EVERY ENTRY OF M IS A PRODUCT OF TWO GRADIENTS, so a global sign flip
//     CANCELS. (-X)(-X) = X^2, and (-X)(-Y) = XY.
//
// So passing dx and dy with the opposite sign convention changes nothing here.
// gradient_x/gradient_y DO assert a signed value and must keep doing so --
// that asymmetry is deliberate, and the acceptance suite for this file gates
// no gradient sign, because a gate that cannot fail is worth nothing.
//
// ============================================================================
// TRAP 4 -- ONLY THE SIGN OF R SURVIVES A CONTRAST CHANGE
// ============================================================================
//
// Scaling the image by p scales every gradient by p, so A, B and C scale by
// p^2, and R by p^4. Dividing R by A^2 leaves only the RATIO of the two
// eigenvalues, so:
//
//     THE SIGN of R           depends only on that ratio   -> contrast-proof
//     THE MAGNITUDE of R      scales as p^4                -> not
//     ANY ABSOLUTE THRESHOLD  on R or on Tr                -> not
//
// A faint patch at 10x contrast scores what a real corner scores at normal
// contrast; the arithmetic is corners.txt section 19. Harris names adaptive
// thresholding as unfinished work in his last sentence, and this file does not
// finish it: corner_peaks() takes an ABSOLUTE threshold.
//
// A RELATIVE THRESHOLD IS THE CALLER'S TO COMPUTE -- scan the response for its
// maximum and pass a fraction of it, or sort and keep the top N. Both are two
// lines at the call site. Neither is chosen here, because choosing would bury
// a policy decision inside something that reads like a comparison, and the
// policy is exactly what the caller is supposed to be reasoning about.
//
// ============================================================================
// TRAP 5 -- ROTATION MOVES THE MAGNITUDE, NOT THE LOCATION
// ============================================================================
//
// A, B and C individually change when the image rotates; Tr and Det do not,
// which is why the response is built from those two (corners.txt section 16).
// But that invariance is exact only for the CONTINUOUS operator. A discrete
// gradient kernel is not isotropic -- Sobel over-reports a 45-degree step by
// exactly 3*sqrt(2)/4 (vc_gradients.h) -- and rotating an image resamples and
// blurs it. So:
//
//     CORNER LOCATIONS are stable under rotation, to within a pixel.
//     RESPONSE MAGNITUDES DRIFT by a few percent, and that is CORRECT.
//
// Reading the drift as a bug is the trap. Nothing in the acceptance suite
// asserts a magnitude across a rotation, deliberately.
//
// ============================================================================
// TRAP 6 -- NON-MAXIMUM SUPPRESSION MUST NOT WRITE INTO WHAT IT IS READING
// ============================================================================
//
// corner_peaks() compares each pixel against its eight neighbours. If an
// implementation SUPPRESSES AS IT SCANS -- zeroing a loser in the same buffer
// it is still reading -- then later pixels are compared against zeros instead
// of the real values, and the result depends on the scan order.
//
// Concretely, on a strictly decreasing ramp there are no interior peaks at
// all, but an in-place row-major sweep invents one every other pixel: having
// zeroed the larger left neighbour, the next pixel now beats it. The suite
// gates this with a ramp, because an isolated-blob fixture cannot see it.
//
// Read from the response and write to a separate output. The order of the
// threshold and the suppression does NOT matter -- any neighbour that could
// outscore a pixel above the threshold is itself above the threshold -- so
// thresholding first is a pure saving, not a behaviour change.
//
// ============================================================================
// WHAT IS DELIBERATELY NOT HERE
// ============================================================================
//
// THE ENTIRE EDGE HALF OF THE PAPER. R's negative lobe is computed and is
// asserted in the suite, because the sign is half of what the response means.
// But the thinning rule, hysteresis, the 5-level classification, junction
// completion and the edge-vertex graph are not built. Harris's title promises
// a combined detector; this is the corner half of it.
//
// NO SUBPIXEL REFINEMENT. Peaks land on integer pixel centres. A real corner
// between two pixels gives a near-tie, and the strict > below picks one of
// them arbitrarily rather than interpolating.
//
// NO SCALE SELECTION. Two scales, sigma_d and window_sigma, both chosen by
// the caller and both held FIXED for a call. The response is not
// scale-invariant: nothing here searches over scale or picks an extremum
// across one, which is what Harris-Laplace and the SIFT family add. Same
// welded-dial limitation vc_gradients.h documents, and for the same reason --
// the fix composes, by calling this over a geometric ladder of sigma. That
// the scales are ARGUMENTS rather than constants is what makes that possible
// at all; see the construction section below.
//
// ============================================================================
// DECLARATIONS
// ============================================================================

// The three windowed gradient-product sums, as three images.
//
// One struct rather than three returns because they always travel together and
// are only meaningful as a set: each is one entry of the same 2x2 M, per pixel.
// Named for the paper's A, B, C rather than for what they contain, so the
// correspondence to page 3 is readable at the call site.
struct vc_structure_tensor {
    vc::vc_image a; // X^2 (x) w
    vc::vc_image b; // Y^2 (x) w
    vc::vc_image c; // (XY) (x) w   -- the off-diagonal; see trap 1
};

// A detected corner. Integer pixel coordinates (see "no subpixel" above) and
// the response value that got it selected, carried so the caller can sort,
// rank or re-threshold without a second pass over the map.
struct vc_corner {
    vc::image_dim x = 0;
    vc::image_dim y = 0;
    vc::float32 response = 0.0F;
};

// Build M from a SOURCE IMAGE. See trap 1 (square then blur) and trap 2 (the
// two Gaussians -- and why BOTH of them are arguments here).
//
// TAKES AN IMAGE, NOT A GRADIENT PAIR, AND OWNS BOTH SCALES.
//
//     sigma_d       the DERIVATIVE scale. This function differentiates `src`
//                   itself, with an ISOTROPIC derivative-of-Gaussian at
//                   sigma_d.
//     window_sigma  the INTEGRATION scale, applied to the gradient products.
//
// Geometry, channel count and dtype follow `src`, f32 only, each channel
// independent -- convolve()'s contract, inherited.
//
// WHY THE GRADIENTS ARE NOT THE CALLER'S ANY MORE. Two images of gradients
// have a PROVENANCE the function cannot inspect: nothing in a pair of f32
// buffers says they came from the same operator at the same scale. Sobel for
// x against a central difference for y type-checks, passes every shape and
// dtype test, and yields a tensor that means nothing -- and no contract,
// however carefully written, can reject it. Taking `src` deletes that whole
// class of caller error instead of documenting it. It also brings sigma_d
// inside the contract, where the acceptance suite can gate it at all.
//
// THE DERIVATIVE MUST BE ISOTROPIC, which is a requirement and not a
// preference. The one thing that DOES NOT SATISFY IT is pre-blurring at
// sigma_d and then calling gradient_x or gradient_y, because those are
// SOBEL: smooth_1d * derivative_1d, so they drag in a second smoothing along
// the PERPENDICULAR axis only, at a scale you cannot set. Trap 2 has the
// arithmetic and the measured anisotropy. That anisotropy is the entire
// reason this function owns sigma_d rather than trusting a gradient pair
// from the caller.
//
// ---- HOW TO BUILD IT, AND THE TWO ROUTES THAT BOTH WORK ----------------
//
// EACH GRADIENT TAKES TWO 1-D FACTORS: one that DIFFERENCES and one that
// only BLURS, on opposite axes. A gradient image is a 2-D operator and it
// must smooth by the same sigma_d along both axes, or dx and dy disagree.
// A derivative factor used alone smooths along its own axis and NOT AT ALL
// along the other; the plain Gaussian supplies the missing direction.
//
// THE x/y IN THESE NAMES IS THE AXIS THE TAPS RUN ALONG, NOT THE GRADIENT
// THEY COMPUTE. Every separable call takes exactly one _x factor (a ROW,
// 1 tap tall) and one _y factor (a COLUMN, 1 tap wide) -- the shapes are
// enforced, so a column cannot be passed where a row belongs. What differs
// between dx and dy is only WHICH of the two carries the difference:
//
//     dx:  derivative along x  +  plain Gaussian along y
//     dy:  plain Gaussian along x  +  derivative along y
//
// make_sobel_x() and make_sobel_y() in vc_gradients.cpp are this exact
// pairing with a 3-tap Gaussian in place of a real one. Two lines each, and
// the structure carries over unchanged. Mind the ARGUMENT ORDER while
// reading them: operator* takes (COLUMN, ROW) and convolve_separable()
// takes (src, ROW, COLUMN) -- mirror images, which is DEFECT 1 at the top
// of that file. Both reject the swap, one by algebra and one by shape, so a
// slip throws rather than silently transposing.
//
// ROUTE A -- COMPOSED. make_gaussian_derivative_1d_x/y(sigma_d) in
// vc_convolve.h fuse blur and difference into one exact kernel; pair each
// with the plain make_gaussian_1d_* on the other axis. The two factors come
// out DIFFERENT LENGTHS, and that is correct: the derivative taps are the
// Gaussian convolved with a 3-tap difference, so they run 2 taps longer.
//
// ROUTE B -- BLUR ONCE, THEN DIFFERENCE. Blur `src` at sigma_d with the two
// plain Gaussian factors, then take BOTH gradients off that one blurred
// image with the bare central difference -- make_sobel_derivative_1d_x/y()
// in vc_gradients.h, which are k_central_difference_taps with no smoothing
// attached. This is NOT the forbidden route: the trap is gradient_x(), which
// is the difference TIMES a smoothing, not the difference alone.
//
// THE TWO ROUTES PRODUCE THE SAME 2-D OPERATOR, not merely similar ones.
// Convolution is associative, so (I (x) G) (x) D == I (x) (G (x) D), and the
// composed operator regroups: (g(x) (x) d) * g(y) == [g(x) * g(y)] (x) d(x),
// which reads "isotropic Gaussian, then difference along x" either way.
// Check it by hand at Sobel size with g = [1,2,1]/4 and d = [0.5,0,-0.5]:
// g (x) d = [0.5, 1, 0, -1, -0.5]/4, and the centre tap of the column g is
// 2/4 = 1/2, so either grouping puts [0.5, 1, 0, -1, -0.5]/8 on the
// operator's centre row. (This said /16 until it was checked against
// make_sobel_x(), whose centre row is [0.25, 0, -0.25] -- exactly half of
// [0.5, 0, -0.5], not a quarter. The two routes always agreed; the stated
// number was wrong.)
//
// ROUTE B IS CHEAPER, because one blur serves both gradients:
//
//     B:  7 + 7 + 3 + 3     = 20 taps/pixel at sigma_d = 1
//     A:  (9 + 7) + (7 + 9) = 32 taps/pixel
//
// Route A's advantage is that the operator is a single INSPECTABLE OBJECT --
// print its taps, check they sum to 0 and are antisymmetric -- where B's
// correctness is a property of a call sequence. Either is acceptable.
//
// THE ONE REAL DIFFERENCE IS AT THE BORDER. B applies `policy` twice, and
// clamping does not commute with convolution, so the routes agree EXACTLY
// in the interior and differ within the support of the edge. Harris does not
// care -- corner_peaks skips borders, and the suite's rotated fixture keeps a
// 12-px margin -- but the two will not match pixel-for-pixel to the frame.
//
// SIGN CONVENTION: take the taps from k_central_difference_taps rather than
// writing a literal, and do not reverse it. TRAP 3 is why this is a note and
// not a gate -- R is blind to the gradient sign, so the suite cannot and does
// not check it. The one thing trap 3 does not say: the sign does survive in
// the tensor's EIGENVECTORS, which are the corner's dominant orientation, so
// a reversed convention is invisible now and wrong later.
//
// HOW MUCH OF THAT THE SUITE CAN ACTUALLY CHECK, PRECISELY. Not the general
// claim: "this derivative is isotropic" has no on-axis test. The obvious one
// -- A on a vertical edge against B on a horizontal one -- is BLIND, because
// sobel_y is sobel_x transposed and an isotropic pre-smoothing is unchanged by
// transposing, so the two agree EXACTLY even for the wrong operator. Anything
// built from a symmetric axis-aligned fixture has the same hole.
//
// NOR DOES THE ROTATED PEAK COUNT, at the scales this suite runs at. A
// pre-blur-then-gradient_x implementation returns the SAME 24 interior peaks
// against the same 20 expected junctions as a correct one, so no tightening
// of that gate can help. The reason is that the anisotropy is in the
// DERIVATIVE stage while what the response sees is the derivative and the
// window together, roughly sqrt(sigma_d^2 + sigma_i^2):
//
//     sigma_i = 1:  sqrt(1 + 1) = 1.41  vs  sqrt(1.5 + 1) = 1.58   +12%
//     sigma_i = 2:  sqrt(1 + 4) = 2.24  vs  sqrt(1.5 + 4) = 2.35    +5%
//
// A wide integration window DILUTES it. At sigma_i = 1 the count gate did
// catch it; but sigma_i = 1 is not a usable configuration -- with
// sigma_i == sigma_d the junction response collapses into a four-fold ring
// (see k_window_sigma in examples/20_harris.cpp), the count inflates 4x on
// both fixtures, and a correct implementation fails its own gate. So that
// gate worked only at a scale where nothing else did.
//
// WHAT DOES CATCH IT IS A REFERENCE COMPARISON, and group 1c is it. The suite
// builds A, B and C out of the fused make_gaussian_derivative_1d_* kernels
// and requires the implementation's tensor to match IN THE INTERIOR. This is
// the header's own stance -- pin the result, not the route -- and it is not
// circular, because group 0 pins those kernels arithmetically first (taps
// summing to zero, antisymmetric, unit-ramp slope exactly 1, and the fused
// kernel equal to the Gaussian composed with the difference through
// convolve()). Measured worst interior difference, relative to max |A|:
//
//     route A (fused DoG) vs route B      2.9e-07   <- both legal routes
//     pre-blur + gradient_x (the trap)    6.6e-02   <- 200,000x larger
//
// The tolerance is 1e-4: 300x above the legal disagreement, 600x below the
// trap. Interior only, because the two routes apply `policy` a different
// number of times and clamping does not commute with convolution.
//
// THIS CLAIM HAS NOW FLIPPED THREE TIMES, which is worth recording as a
// caution about reasoning from one measurement. The transpose argument said
// blind; a count measurement at sigma_i = 1 said caught; fixing the ring
// defect raised sigma_i to 2 and said blind again. Only the reference
// comparison is robust to the scales, because it compares against a
// construction instead of against a consequence.
//
// AND ONE ALTERNATIVE CONSTRUCTION IS NOW EXCLUDED, which narrows the clause
// above. Sampling the analytic derivative of a continuous Gaussian at integer
// offsets is NOT the same operator as the sampled Gaussian convolved with the
// difference -- measured at 1.6e-01 on the same fixture, FURTHER off than the
// trap this gate exists to catch. An earlier revision of this trap offered it
// as an alternative; it is not one. Use the library kernels, or compose the
// Gaussian with the difference yourself.
//
// window_sigma > sigma_d IS A QUALITY REQUIREMENT, NOT A REJECTION, and the
// difference matters. A caller who passes window_sigma <= sigma_d gets a
// mathematically well-defined tensor that is a BAD DETECTOR, which is a
// tuning mistake and not an invalid argument -- so this function computes it
// and says nothing. Here is what they get, and it is worth hand-checking
// because it is the reason the integration stage exists at all.
//
// Take the limit, window_sigma -> 0: no integration, so A, B and C are just
// Ix^2, Iy^2 and Ix*Iy at a single pixel. Then
//
//     Det = Ix^2 * Iy^2 - (Ix*Iy)^2 = 0
//
// EXACTLY, at every pixel, for every image. M is the outer product of one
// gradient vector with itself, which is rank 1, and a rank-1 2x2 matrix has
// a zero determinant. So R = 0 - k*Tr^2 is NEGATIVE EVERYWHERE and no corner
// can ever score positive. A corner is not a property of one pixel's
// gradient; it is the property of a NEIGHBOURHOOD containing gradients in
// two different directions, and window_sigma is what makes the function look
// at a neighbourhood. Shrink it toward sigma_d and the two stages' supports
// merge -- effective support is sqrt(sigma_d^2 + window_sigma^2) -- the
// window stops adding any averaging the derivative had not already done, and
// Det slides back toward that exact zero. Corners stop separating from
// edges continuously, with no error and no threshold to catch it.
//
// WHY BOTH SCALES ARE PARAMETERS AND NOT CONSTANTS, given that the
// literature publishes tuned values. SIGMA IS MEASURED IN PIXELS, and a
// pixel is not a unit of the scene. The published pair is tuned for the
// resolution it was measured at: the same integration window that sits
// inside one square of a coarse checkerboard straddles three corners of a
// fine one, and the tensor at each junction is then contaminated by its
// neighbours. Halve an image and every feature in it halves, so the scales
// that found those features have to halve too. Scale-space detectors go
// further and sweep sigma deliberately over a geometric ladder, which a
// baked-in value makes impossible. A tuned value is a DEFAULT at a call
// site, carried with its citation, never a constant inside the operator --
// and a constant is also untestable, because the cheapest proof that this
// function respects a scale at all is to change the scale and watch the
// output move.
//
// REJECTS, with invalid_argument: a `src` that is not f32; a sigma_d or a
// window_sigma that is not finite and > 0. BOTH scales, both ends, and
// +INFINITY is one of the four cases -- it is not covered by a `> 0` test.
//
// VALIDATE HERE ANYWAY, EVEN THOUGH THE KERNEL BUILDERS NOW AGREE.
// make_gaussian_1d_* and make_gaussian_derivative_1d_* reject all four of 0,
// negative, NaN and +INFINITY as invalid_argument themselves, so leaving the
// scales unchecked here would raise the right error class today -- by
// accident. It would be the FIRST kernel built that decided it, which makes
// the error class depend on the order of the stages, and a later refactor
// that reorders them changes the exception with no visible cause. (It was
// worse than that before: +INFINITY used to escape the `> 0` test, size a
// vector from an infinite radius, and surface as length_error, which is not
// an invalid_argument at all.) A named algorithm checks its own arguments
// before it does any work.
[[nodiscard]] vc_structure_tensor structure_tensor(const vc::vc_image& src,
                                                   float sigma_d,
                                                   float window_sigma,
                                                   vc_edge_policy policy);

// R = Det - k Tr^2, per pixel. Signed: see the three signs above.
//
// THE RESPONSE IS ALWAYS SINGLE-CHANNEL, WHATEVER WENT IN, and that is a
// change: it used to follow the input's channel count. A corner is a property
// of the SCENE, not of a colour plane, so a per-plane R is not a corner
// measure -- and vc_corner has no channel field precisely because there is
// nothing for it to mean.
//
// So a multi-channel tensor is COLLAPSED here first, to the MEAN of the
// per-channel tensors. sum_c M_c is J^T J for the C x 2 Jacobian (Di Zenzo
// 1986), the honest generalisation for a vector-valued image, and the mean is
// that sum over C -- which keeps R numerically comparable with the
// single-channel case, so an absolute threshold tuned on greyscale still
// means the same thing on colour.
//
// THE COLLAPSE HAPPENS AT THE TENSOR AND NOT AT R, because R is NONLINEAR in
// M: sum(R(M_c)) != R(sum(M_c)). At an edge in one channel M is rank 1 and
// det is exactly 0, so every channel reports "not a corner"; adding those
// verdicts cannot recover the fact that their null directions differed, and
// det is the term that measures exactly that. A red horizontal edge crossing
// a green vertical edge IS a corner, and only tensor-level summation sees it.
// Same shape as trap 1, one level up: the nonlinear step goes last.
//
// IMPLEMENTATION IS FREE between writing (ab - c*c) - k*(a+b)*(a+b) directly
// and assembling a vc::math::vc_mat2 per pixel to call determinant() and
// trace() on. The suite pins the result, not the route. The per-pixel mat2 is
// the more readable of the two and costs a construction per pixel; the direct
// form is what a tight loop wants. Both are correct.
//
// REJECTS, with invalid_argument: a, b, c disagreeing in dimensions, channel
// count or dtype; ANY OF THEM NOT f32 -- "disagreeing in dtype" does not cover
// three images that agree with each other and are all u8, which is why this is
// spelled out separately; a k that is not finite, or outside (0, 0.25)
// EXCLUSIVE.
//
// A vc_structure_tensor IS AN AGGREGATE AND CAN BE HAND-BUILT, so the three
// images arrive with no guarantee they came from one structure_tensor() call.
// Walking A's extent and reading C happens to throw when C is the smaller,
// because vc_image_info::index() bounds-checks -- but reverse the sizes and
// nothing is out of range and nothing rejects. The same asymmetry applies to
// dx/dy above, in the opposite direction for channel counts than for
// dimensions, so do not reason about which of these is "free": validate all of
// them up front.
//
// WHY THAT UPPER BOUND IS A REJECTION AND NOT A WARNING: at k = 0.25 a
// perfectly round bowl -- the most corner-like thing there is -- gives R
// exactly 0, and above it R is negative everywhere. So k >= 0.25 is not a
// strict detector, it is a detector that finds nothing, and silently. The
// lower bound is exclusive for the mirror reason: at k = 0 every edge scores
// R = 0 or above and the negative lobe disappears.
[[nodiscard]] vc::vc_image corner_response(const vc_structure_tensor& m,
                                           float k);

// Threshold, then keep 8-way local maxima. See trap 6.
//
// STRICTLY GREATER on both comparisons, matching edge_map(): a pixel exactly
// ON the threshold is out, and a pixel merely EQUAL to its largest neighbour
// is not a peak. The second half means an exact plateau yields NOTHING rather
// than all of it -- which is the right failure for a box window, whose flat
// response has no summit to find (corners.txt section 20).
//
// Border pixels, which have fewer than eight neighbours, are NEVER peaks. The
// response there is built from padded data and is not trustworthy; excluding
// them is cheaper to reason about than deciding what a missing neighbour
// compares as.
//
// AN IMAGE WITH NO INTERIOR PIXEL -- any width or height below 3 -- therefore
// yields an EMPTY result, and that is not an error. Worth stating because
// vc::image_dim is unsigned: an interior loop written `x <= width() - 2`
// underflows to 4294967295 at width 1 and reads until something throws, while
// `x < width() - 1` is correct at every size. (No equivalent claim is made
// here for structure_tensor or corner_response -- their behaviour on
// degenerate sizes is whatever convolve() does, and this file does not gate
// it.)
//
// SINGLE-CHANNEL ONLY, AND A MULTI-CHANNEL RESPONSE IS REJECTED. A corner is
// a position, not a per-channel quantity, so returning several overlapping
// coordinate lists for an RGB response would invite the caller to merge them
// -- a policy decision, not a detection one.
//
// THIS USED TO READ CHANNEL 0 AND SAY NOTHING, which was worse than either
// option: on an RGB response it returned the corners of the RED plane and
// discarded the rest, with nothing in the return value admitting it. That is
// a silent wrong answer dressed as a deferred decision. The reduction belongs
// at the tensor, where corner_response now performs it, which makes a
// multi-channel response unreachable through the normal pipeline -- but
// vc_structure_tensor is an aggregate and a response can be hand-built, so
// the rejection is still load-bearing.
//
// Returns corners in ROW-MAJOR ORDER, not sorted by response. Sorting is one
// line at the call site and the scan order is the cheaper guarantee to keep.
//
// REJECTS, with invalid_argument: a non-f32 response; a response with more
// than one channel; a threshold that is not finite, or negative. NOT FINITE
// INCLUDES +/-INFINITY, not only NaN: a guard
// written as a bare self-comparison catches NaN and lets +Inf through, where it
// then silently selects nothing at all.
[[nodiscard]] std::vector<vc_corner> corner_peaks(const vc::vc_image& response,
                                                  float threshold);

} // namespace vc::pixelops
