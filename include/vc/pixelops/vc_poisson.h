// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/math/vc_grid2d.h" // grid2d, grid_dim

#include <cstdint>
#include <vector>

namespace vc::pixelops {

// Solving grad^2 phi = f for phi, by division in the frequency domain.
//
// vc_laplacian.h builds the FORWARD operator: image in, second differences out.
// This file is the undo.
//
//     laplacian     image  ->  f        plug in numbers, read off the answer
//     poisson_fft   f      ->  image    the same equation, backwards
//
// The equation is one English sentence: THE IMAGE I WANT IS THE ONE WHOSE SECOND
// DIFFERENCES COME OUT AS f. phi is the unknown, f is what was handed over.
// "Poisson" is only a name -- Simeon Poisson met the equation in electrostatics
// and nothing about the name carries information.
//
// Reasoning in full: curriculum/notes/poisson.txt.
//
// ---- WHY A SOLVER EXISTS AT ALL ----
//
// Four samples, wrap, f = [-10, 4, 2, 4]. "Find phi" written out is
//
//     phi1 - 2.phi0 + phi3  =  -10
//     phi2 - 2.phi1 + phi0  =    4
//     phi3 - 2.phi2 + phi1  =    2
//     phi0 - 2.phi3 + phi2  =    4
//
// Four equations, four unknowns, and EVERY EQUATION TOUCHES THREE OF THEM. Not
// one can be solved alone. At 256x256 that is 65,536 simultaneous equations.
//
// ---- WHY THE TRANSFORM UNTANGLES THEM ----
//
// A linear filter cannot change a wave's frequency. The output is a weighted sum
// of SHIFTED COPIES of the input; a shifted wave is the same wave started at a
// different angle; and same-rate waves at different angles add up to a same-rate
// wave. (That last step is the one doing the work. For the symmetric kernels
// here it is visible rather than asserted: the shifted copies pair up and their
// sideways parts cancel, leaving no shift at all -- poisson.txt 5c.) So all a
// filter can do to each frequency is SCALE it: one number per frequency.
// Therefore
//
//     laplacian(phi) = f      becomes      scale . PHI[k,l] = F[k,l]
//
// which is one equation in one unknown, per bin. Divide. THE TANGLE WAS IN THE
// COORDINATES, NOT IN THE PROBLEM.
//
//     transform f  ->  divide each bin by its scale  ->  inverse transform
//
// Three lines, and dft2d/idft2d are two of them.
//
// ---- THE SCALE, AND WHERE IT COMES FROM ----
//
// Not a formula to look up. A pixel's two neighbours on a wave average to that
// pixel times cos(w), where w is how far round the circle one pixel takes you.
// The two outer taps of [1,-2,1] therefore contribute 2.centre.cos(w) and the
// middle tap -2.centre, so the scale is -2 + 2.cos(w). Add the two axes (the
// same addition that built the 3x3 stencil from two 1-D kernels) and
//
//     scale(k,l) = -4 + 2.cos(2.pi.k / width) + 2.cos(2.pi.l / height)
//
// It is REAL, with no rotation, because the stencil is symmetric -- a symmetric
// kernel's mirrored taps cancel sideways and only scale.
//
// ** DO NOT USE THE CONTINUOUS FORM -(u^2 + v^2). ** The phrase "the deliberate
// teaching trap" is P2's, from its capability table in section B -- NOT from the
// master overview, which an earlier draft of this header said. The two planning
// documents cite each other for it, so the attribution was circular; section B
// is where it actually originates. It earns the name either way:
//
//     w = 90 deg    discrete -2      continuous -2.47
//     w = 180 deg   discrete -4      continuous -9.87
//
// Off by a factor of 2.5 at the highest frequency, which is exactly where edges
// live. It looks right on smooth test fields and degrades silently on anything
// with an edge. 14_poisson group 2 transforms the real stencil and compares bin
// by bin, so the trap cannot be stepped into unnoticed.
//
// ---- THE BINS BESIDE DC ARE NEARLY A HOLE TOO ----
//
// For a small angle cos w is close to 1 - w*w/2, so the scale is close to -w*w.
// At bin 1 of a row of width N that is about -(2.pi/N)^2, which shrinks fast:
//
//     width  64    bin 1 scale about -0.0096    dividing amplifies ~104x
//     width 256    bin 1 scale about -0.0006    dividing amplifies ~1660x
//
// So the SLOWEST non-flat waves are the least accurately recovered, and any float
// error already in those bins is multiplied up with them. That is correct
// behaviour, not a defect -- a broad smooth swell genuinely has tiny second
// differences -- but it means a 4x4 test grid, whose only scales are 0, -2, -4
// and -8, never exercises the solver's hardest case. 14_poisson group 15 runs at
// 64x32 for exactly this, and that is the size a tolerance should be measured
// against.
//
// Note -w*w IS the continuous form. "The continuous version agrees at low
// frequency" and "the discrete scale is tiny near DC" are one fact seen twice.
//
// ---- BIN 0 IS ZERO, AND THAT IS NOT AN EDGE CASE ----
//
// scale(0,0) = -4 + 2 + 2 = 0 exactly. Dividing by it is undefined, and the
// reason is the fact vc_laplacian.h already states: THE WEIGHTS SUM TO ZERO, so
// adding a constant to every pixel cancels --
//
//     (x[n+1] + c) - 2(x[n] + c) + (x[n-1] + c)      the c's give c - 2c + c = 0
//
// -- and under wrap laplacian returns byte-for-byte the same array. The level is
// destroyed by the forward operator.
//
// SAY "UNDER WRAP". Measured, adding 7 to a 5x4 field: wrap, clamp and reflect
// all leave the output unchanged, because every tap still lands on a real pixel.
// Under `zero` the missing taps contribute 0 rather than c, so the cancellation
// is short -- an edge shifts by -c, a corner by -2c, and the worst change is 14.
// That is laplacian.txt 8b's table, and an earlier draft of this header stated
// the claim with no policy attached.
//
// NO ALGORITHM RECOVERS THE LEVEL -- not "none found yet", but cannot exist,
// because f is IDENTICAL for every constant.
//
// What that does and does not cost:
//
//     every edge, every texture, every gradient, exactly        RECOVERED
//     the overall brightness level                             NEVER
//
// And the natural objection -- "surely everything depends on the level, since
// the rest are deviations from it" -- is wrong, and the circle-sum fact says
// why. ADDING A CONSTANT CHANGES BIN 0 AND NOTHING ELSE: for every other bin the
// wave's samples are evenly spaced points on a circle and sum to zero, so the
// constant contributes nothing to them. The mean sits BESIDE the deviations, not
// underneath them.
//
// Hence two obligations, both tested:
//
//   - CHOOSE the level. This solver returns the ZERO-MEAN solution: bin 0 is not
//     divided, so it stays at whatever f put there, which the precondition below
//     forces to be zero. Legitimate precisely because nothing is being
//     discarded; there was never anything there.
//
//     AN EARLIER DRAFT SAID "sets bin 0 to zero", which over-specified. Given
//     that f has passed the zero-mean check, F[0,0] is ALREADY zero, so setting
//     it and skipping it are the same operation -- provably, not just in
//     practice, since no admissible f can tell them apart. Mutation testing
//     confirms it: skipping bin 0 rather than zeroing it leaves all 59 checks in
//     14_poisson green. What is load-bearing is only that bin 0 IS NOT DIVIDED;
//     dividing it drops the suite to 43/59 and fills the image with NaN.
//
//   - REJECT an f whose mean is not zero. The forward operator UNDER WRAP always
//     outputs zero mean, so an f that does not is one no image could have
//     produced through the problem this solver poses, and there is
//     no phi to return. Say so rather than returning something quietly wrong.
//     Note f = laplacian(phi, wrap) satisfies this BY CONSTRUCTION, so the
//     round-trip gate can never trip this check -- testing it needs a hand-built
//     f that violates it, which 14_poisson group 7 supplies.
//
//     ** AND A REAL GRADIENT-DOMAIN CALLER WILL TRIP IT. ** An f assembled over a
//     MASKED REGION -- which is what the Block-7 seam does -- has no reason to
//     sum to zero, because nothing constrains it to. That is not a bug in the
//     caller and not a bug here: the periodic problem genuinely has no solution
//     for such an f. The caller subtracts the mean first, which is the standard
//     projection onto the solvable set, and loses nothing it could have kept
//     (bin 0 was going to be discarded regardless). Rejecting rather than
//     silently subtracting is the deliberate choice, so that the caller decides
//     knowingly instead of being handed a shifted answer.
//
// ---- THIS SOLVES THE PERIODIC PROBLEM AND NOTHING ELSE ----
//
// The scales above describe the stencil applied with WRAP: the sample before
// index 0 is the last sample. That is the only boundary the DFT can express
// (waves_and_signals 5d), and it is why THERE IS NO vc_edge_policy PARAMETER
// HERE. A policy argument would advertise a choice this function does not have.
//
// Operationally, for every caller and every test:
//
//     BUILD f WITH laplacian(phi, vc_edge_policy::wrap)
//
// A forward pass under clamp or zero produces the f of a DIFFERENT problem. The
// round trip then misses at the border while looking correct in the interior,
// which reads as a solver bug and is not one. S8's cross-validation hits the
// same wall one session later.
//
// ** AND THE ZERO-MEAN CHECK ABOVE DOES NOT GUARD AGAINST THIS. ** The obvious
// hope is that a wrongly-built f would fail the precondition. It does not.
// Measured with laplacian() on a 5x4 field that is neither constant nor
// symmetric:
//
//     wrap      sum of output =    0
//     clamp     sum of output =    0        <-- ALSO ZERO
//     zero      sum of output = -206.5
//     reflect   sum of output =  -19.5
//
// clamp sums to zero too. So a clamp-built f is ACCEPTED, solved, and returned
// -- on an 8x4 test field, wrong by 0.66 of phi's RMS, with no exception and no
// NaN. The wrap-built f on that same field recovers to 5.6e-07.
//
// There is no defence inside this function. Building f with wrap is the whole
// of it. 14_poisson group 16 pins the failure so it stays visible.
//
// When a pinned boundary IS needed -- the Block-7 masked-merge seam holds the
// border at the destination's pixels, which is what makes the seam vanish --
// an ITERATIVE solver is what can express it, because "hold this pixel" is just
// "skip it in the loop".
//
// BUT poisson_iter AS BUILT AT S8 DOES NOT DO THAT EITHER. It is wrap-only, the
// same periodic problem as this function, on purpose: the two must solve the
// IDENTICAL problem for the cross-validation to mean anything. The mask and the
// pinned border arrive at Block-7. An earlier version of this line said pinned
// boundaries were "poisson_iter's job (S8)", which promised a capability S8
// does not ship.
//
// ---- WHAT THIS IS FOR ----
//
// Gradient-domain image processing: edit the DIFFERENCES between neighbouring
// pixels, then recover an image. Recovering the image IS solving this equation.
//
//     seamless paste   set f = laplacian(source) inside the region and pin the
//                      border to the destination. The answer carries the
//                      source's texture and the destination's brightness.
//     tone mapping     shrink the large entries of f (harsh contrast), keep the
//                      small ones (detail), solve.
//
// Both MANUFACTURE an f that no image produced and ask for the nearest image
// that could have. This is PREQ-4: built once here, reused at P9a (diffuse.c)
// and at the Block-7 seam, never re-derived.

// ======================================================================
// THE SAME EQUATION, SOLVED BY SWEEPING INSTEAD
// ======================================================================
//
// Reasoning in full: curriculum/notes/poisson.txt. Short version below.
//
// ---- WHY A SECOND SOLVER, HONESTLY ----
//
// Not because this one is too slow. Because poisson_fft CANNOT BE TOLD ANYTHING
// ABOUT A BOUNDARY. Its waves wrap; there is no input for "hold this pixel at
// this value", and gradient-domain compositing is exactly that -- the border
// pinned to the destination's pixels, the interior solved. An iterative solver
// expresses it in one line (`if fixed, continue`), because it touches one pixel
// at a time.
//
// ** THAT IS NOT WHAT S8 BUILDS. ** poisson_iter is WRAP-ONLY, deliberately, and
// solves the same periodic problem poisson_fft does. The point this week is that
// two independent methods must agree; a solver aimed at a different problem
// could not be cross-checked against anything. Masks arrive at Block-7, additive
// to this signature.
//
// ---- THE UPDATE IS ONE REARRANGEMENT ----
//
//     phi(x+1,y) + phi(x-1,y) + phi(x,y+1) + phi(x,y-1) - 4.phi(x,y) = f(x,y)
//
//     ->   phi(x,y) = [ the four neighbours  -  f(x,y) ] / 4
//
// Circular -- phi defined by phi -- which is the whole idea: guess, apply it
// everywhere, repeat. Each pass makes every pixel consistent with whatever its
// neighbours currently say, and information crawls outward one pixel per sweep.
//
// JACOBI reads all four neighbours from the PREVIOUS sweep (two buffers, swap at
// the end). GAUSS-SEIDEL reads and writes ONE buffer in place, so some
// neighbours are already this sweep's values. That is the entire difference.
//
// ** THIS IS THE 2-D UPDATE AT EVERY GRID SIZE, INCLUDING ONE ROW. ** On a 4x1
// image under wrap the up and down neighbours are the pixel itself, so the
// update reads (L + R + 2.centre - f)/4 -- NOT the 1-D (L + R - f)/2. Both have
// the SAME fixed point (set them equal and the centre terms cancel to
// 2.phi = L + R - f), so the answer is unchanged, but the convergence RATE
// differs and any hand-worked 1-D iteration sequence will not reproduce. An
// earlier draft of P2's plan row derived the stall below from the 1-D update and
// published hand numbers that this code does not produce.
//
// ---- HOW FAST, AND THE ONE MODE THAT NEVER MOVES ----
//
// Subtract the exact equation from the update. f appears in both and CANCELS,
// leaving: the new error at a pixel is the AVERAGE OF ITS FOUR NEIGHBOURS'
// ERRORS. The errors evolve on their own, with no reference to f at all.
//
// A wave's neighbours average to that wave times cos(w) -- the same fact that
// gave laplacian_scale its cos above. So an error made of a single wave is
// multiplied EVERY SWEEP by
//
//     factor(k,l) = [ cos(2.pi.k/width) + cos(2.pi.l/height) ] / 2
//
// which is strictly inside +-1 for every mode except two:
//
//     (0, 0)                  the flat mode          factor +1   never decays
//     (width/2, height/2)     the full checkerboard  factor -1   flips forever
//
// THE JOINT MODE ONLY. (width/2, 0) has factor (-1 + 1)/2 = 0 and dies in a
// SINGLE sweep, as does (0, height/2); and the joint mode exists only when both
// dimensions are even. A one-row image has no height/2 bin, so it cannot stall.
//
// The flat mode is the DC indeterminacy this file already describes, and is
// removed by subtracting the mean. THE CHECKERBOARD IS A GENUINE STALL and
// 15_poisson_iter pins it exactly: on a pure checkerboard phi, Jacobi's relative
// residual is 1.0 forever -- the iterate oscillates between 2.P and 0 while the
// error holds magnitude |A| and the checkerboard's own scale is -8.
//
// NOTHING HERE IS ASSERTED ABOUT GAUSS-SEIDEL AND THE CHECKERBOARD. Its
// iteration is not symmetric, the argument above does not transfer, and it has
// not been verified. The tests assert the Jacobi plateau and stay silent on GS.
//
// ---- WHAT THAT COSTS IN SWEEPS ----
//
// Runtime is set by the SLOWEST-decaying useful mode, which is the one nearest
// DC -- not by the stall:
//
//     width 64    mode (1,0)   factor 0.9976    ~4,800 sweeps to shrink 1e-5
//     width 16    mode (1,0)   factor 0.9619    ~300 sweeps
//
// So cross-validation belongs on a SMALL grid. Two traps that read as solver
// bugs and are not:
//
//   - A SMALL RESIDUAL IS NOT A SMALL ERROR. error ~ residual / |smallest
//     scale|. At width 64 the smallest non-zero scale is about 0.0096, so a
//     residual of 1e-5 permits a phi error near 1e-3.
//   - f32 HAS A FLOOR of roughly eps x (largest scale / smallest scale), about
//     830 x eps at width 64. Below it, more sweeps buy nothing. This is the same
//     size-dependence poisson_fft's tolerance block measures, reached by a
//     different route.
//
// ---- TWO THINGS THE ITERATION MUST DO THAT THE FORMULA DOES NOT SAY ----
//
// SUBTRACT THE MEAN AFTER EVERY SWEEP. Jacobi provably preserves it (summing the
// update over all pixels gives sum(phi) - sum(f)/4, and sum(f) is zero by the
// precondition), so for Jacobi this is a no-op. GAUSS-SEIDEL DRIFTS IN DC and
// will converge to the right shape at the wrong level. Subtracting costs nothing
// and changes nothing else, because the stencil is blind to constants.
//
// TWO THINGS THE CONTRACT DELIBERATELY DOES NOT CONSTRAIN, so that an
// implementer does not waste time deciding. Whether the residual is measured
// before or after the mean subtraction within a sweep DOES NOT MATTER -- it is
// blind to a constant either way. Nor does whether the channel loop sits inside
// or outside the sweep loop, since the equation does not couple channels.
//
// THE RESIDUAL IS BLIND TO A CONSTANT OFFSET, for the same reason:
// r = f - laplacian(phi) is identical for phi and phi + c. SO A RUN CAN CONVERGE
// PERFECTLY AND STILL SIT A CONSTANT AWAY from poisson_fft's answer. Any
// comparison between the two solvers must zero-mean both sides first.
//
// ---- TRAVERSAL ORDER IS PART OF THE CONTRACT ----
//
// Gauss-Seidel's answer depends on the order pixels are visited, so the order is
// FIXED HERE AND TESTED: in place, raster order, y outer and x inner, ascending.
// Under wrap this means a pixel's LEFT and UP neighbours are this sweep's values
// and its RIGHT and DOWN are the previous sweep's -- EXCEPT in the last column,
// whose right neighbour wraps to column 0 and is therefore already fresh, and
// the last row likewise. "Left and up are fresh" is the rule in the interior,
// not the definition; the seam is where an implementation differs from a naive
// reading, and 15_poisson_iter's hand case exercises it.

// The number the 5-point stencil multiplies bin (k, l) by.
//
// Deliberately public rather than a detail of poisson_fft, for two reasons: the
// hand-checkable values live here (at 4x4, scale(0,0)=0, scale(1,0)=-2,
// scale(2,0)=-4, scale(2,2)=-8), and S8 will want the same numbers to explain
// why its iteration converges at the rate it does.
//
// Returns a float, not a complex: the stencil is symmetric, so there is no
// rotation to represent. If this ever needs to be complex, the kernel stopped
// being symmetric and more than this function is wrong.
//
// extent gives the transform's dimensions; k and l are bin indices and must be
// inside it.
[[nodiscard]] float laplacian_scale(vc::math::grid_dim k,
                                    vc::math::grid_dim l,
                                    const vc::math::grid2d& extent);

// Solve laplacian(phi) == f for phi, with WRAP boundaries, returning the
// ZERO-MEAN solution.
//
// f must be f32, and EVERY CHANNEL must have zero mean (to within float noise
// scaled by that channel's own RMS); both are checked and both throw
// vc_error_code::invalid_argument.
//
// PER CHANNEL, NOT OVER THE WHOLE BUFFER, and the distinction is not pedantic.
// Each channel is solved independently and each one's bin 0 is set to zero on its
// own, so each needs the precondition on its own. A whole-image sum accepts a
// two-channel f whose channels have means +5 and -5 and returns a quietly wrong
// answer for both. The first implementation written against an earlier draft of
// this line made exactly that mistake and passed every other check in
// 14_poisson; group 19 now pins it.
//
// Output geometry, channel count and dtype follow f. Each channel is solved
// independently -- the equation does not couple them.
[[nodiscard]] vc::vc_image poisson_fft(const vc::vc_image& f);

// Which sweep poisson_iter runs. Both solve the same equation; they differ only
// in which copy of the data they read.
//
// Switched over with NO `default` label, so that adding red_black at S8b makes
// every switch a compiler warning instead of a silent fall-through -- the same
// discipline frequency_extent uses for vc_edge_policy. red_black is NOT declared
// yet for exactly that reason: an enumerator with no implementation is a stub
// that throws, and a test asserting it throws passes vacuously.
enum class poisson_sweep : std::uint8_t {
    jacobi,       // read all four neighbours from the PREVIOUS sweep
    gauss_seidel, // read and write one buffer in place, raster order
};

// A sweep count, named rather than a bare uint32_t so that a call site reading
// poisson_iter(f, sweep, 500, 1e-6F) cannot silently swap the two trailing
// arguments past the type system. docs/follow_up_p4.txt lists bare primitives
// as a standing debt; this is one place not to add to it.
using sweep_count = std::uint32_t;

// What a run produced.
//
// NOT DEFAULT-CONSTRUCTIBLE, because vc_image is not. Accumulate the history in
// a local vector and aggregate-initialise this once phi exists; writing
// `poisson_iter_report out;` at the top of the function does not compile.
struct poisson_iter_report {
    // The solution, ZERO-MEAN PER CHANNEL. Geometry, channel count and dtype
    // follow f.
    vc::vc_image phi;

    // The relative residual ||f - laplacian(phi, wrap)|| / ||f||, both as RMS,
    // recorded once per sweep. This is the curve S8b plots.
    //
    // INDEXING: residual[0] is the INITIAL value, BEFORE any sweep; residual[i]
    // is the value after sweep i. So size() == sweeps_actually_run + 1, which is
    // max_sweeps + 1 if it never converged and less if it stopped early.
    //
    // residual[0] IS 1.0 and that is a free check: from a zero start,
    // r = f - laplacian(0) = f, so the ratio is ||f||/||f||. Exactly 1.0 in
    // arithmetic, and to within an ulp in practice depending on whether the two
    // norms are computed by the same code path -- 15_poisson_iter allows 1e-6
    // rather than demanding bit equality.
    //
    // THE ONE EXCEPTION IS AN ALL-ZERO f, where the clamped denominator makes it
    // 0.0 rather than 1.0 -- correctly, since there is no residual to report and
    // phi = 0 already solves it. 15_poisson_iter pins both.
    //
    // ONE NUMBER PER SWEEP, AND IT IS THE WORST CHANNEL, NOT THE COMBINED RMS.
    // A single RMS over all channels hides a channel whose own f is small and
    // whose answer is entirely wrong -- which is precisely the bug group 19 of
    // 14_poisson exists to catch, in the mean check, one function over.
    //
    // THE DENOMINATOR IS CLAMPED by the same RMS floor poisson_fft uses, so an
    // all-zero f gives 0 rather than 0/0, converges immediately, and returns
    // phi = 0, which is the correct answer for it.
    std::vector<float> residual;

    // True only when EVERY channel's relative residual reached tolerance. A run
    // that exhausted max_sweeps reports false; it does not throw, because
    // "did not converge in this budget" is information the caller asked for by
    // supplying a budget.
    //
    // ** A SMALL RESIDUAL IS NOT A SMALL ERROR. ** error ~ residual / |smallest
    // scale|, and the header above tabulates how small that gets. Do not read
    // converged == true as "phi is accurate to tolerance".
    bool converged;
};

// Solve laplacian(phi) == f for phi by sweeping, with WRAP boundaries, returning
// the ZERO-MEAN solution -- the same problem and the same answer as
// poisson_fft, by an unrelated method, so the two cross-validate.
//
// f must be f32 and EVERY CHANNEL must have zero mean, checked exactly as
// poisson_fft checks them and sharing its implementation so the two tolerances
// cannot drift apart. Both throw vc_error_code::invalid_argument, and the
// message names poisson_iter rather than poisson_fft.
//
// max_sweeps must be at least 1; zero throws rather than returning a zero image
// that would quietly fail a gate. tolerance must be a finite value >= 0, tested
// as !(tolerance >= 0) so that NaN is rejected rather than compared away. A
// tolerance of exactly 0 is legal and simply never converges, which is a
// reasonable way to ask for exactly max_sweeps sweeps.
//
// The initial guess is zero everywhere. Not a parameter: nothing this week needs
// another, and adding one later is additive. The hand checks in 15_poisson_iter
// depend on it, so changing it is a contract change.
[[nodiscard]] poisson_iter_report poisson_iter(const vc::vc_image& f,
                                               poisson_sweep sweep,
                                               sweep_count max_sweeps,
                                               float tolerance);

} // namespace vc::pixelops
