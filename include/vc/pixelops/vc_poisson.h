// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/math/vc_grid2d.h" // grid2d, grid_dim

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
// that is poisson_iter's job (S8), not this one.
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

} // namespace vc::pixelops
