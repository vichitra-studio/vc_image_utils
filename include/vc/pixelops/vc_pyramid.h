// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/pixelops/vc_edge_policy.h"

namespace vc::pixelops {

// Gaussian and Laplacian image pyramids.
//
// Full derivation, the hand-worked 1-D case, the measured band table and the
// pyramid-versus-DFT comparison are in curriculum/notes/pyramids.txt. This
// header carries the contract and the traps.
//
// ---- TWO PYRAMIDS, TWO JOBS ----
//
//     GAUSSIAN     the same image at several blurs   -> coarse-to-fine SPEED
//     LAPLACIAN    the image split into BANDS        -> band-wise EDITING
//
// Not two views of one idea. Block 2's MTB alignment and Lucas-Kanade run on the
// first; Mertens exposure fusion collapses over the second.
//
// ---- THE FOUR OPERATIONS ----
//
//     G0      = the image
//     G(l+1)  = reduce(Gl)                  blur, then keep every other pixel
//     Ll      = Gl - expand(G(l+1))         what the round trip LOST
//     Gl      = Ll + expand(G(l+1))         and back again
//
// ---- WHY COLLAPSE IS EXACT, AND WHAT THAT RULES OUT ----
//
// Ll is DEFINED as Gl - expand(G(l+1)), so adding expand(G(l+1)) returns Gl. It
// is a = (a - b) + b, with no approximation anywhere.
//
// Which means THE KERNEL CHOICE CANNOT BREAK COLLAPSE. Any blur at all satisfies
// a = (a-b)+b. sigma decides whether the pyramid is GOOD -- how much aliasing
// survives the subsample -- never whether it is INVERTIBLE. So a failing collapse
// test is never "wrong sigma"; it is an indexing or sizing bug. Do not go looking
// in the wrong place.
//
// ---- expand() TAKES ITS TARGET SIZE, AND MUST ----
//
//     reduce a 5-wide row   ->  indices 0, 2, 4   ->  3 wide
//     expand a 3-wide row   ->  5 wide? or 6?     BOTH are plausible
//
// A 3-wide level could have come from a 5 or a 6, so expand() cannot infer its
// output size by doubling. Without the target, Ll = Gl - expand(G(l+1))
// subtracts mismatched images at every odd level. Same class of decision as
// fft1d's non-power-of-two contract: stated here, and tested on an odd image.
//
// ---- THE KERNEL IS FIXED: BURT AND ADELSON'S [1,4,6,4,1]/16 ----
//
// Not a sigma parameter. Deliberately, and for two measured reasons.
//
// (1) ITS TAPS SPLIT EXACTLY EVENLY between the taps an even-parity output
//     reads and those an odd-parity output reads -- the two groups expand()'s
//     stencils are built from:
//
//       Burt      even taps (-2,0,+2)  (1+6+1)/16 = 0.5000
//                 odd  taps (-1,+1)    (4+4)/16   = 0.5000      EQUAL
//
//       sigma=1   even  0.0545+0.4026+0.0545 = 0.5116
//       truncated odd   0.2442+0.2442        = 0.4884           NOT equal
//
//     That equality is why BOTH of expand()'s stencils can divide by the same
//     8, and why there is one scale factor rather than two. A kernel that split
//     0.5116/0.4884 needs its own unequal pair, and with a single divisor
//     ripples: x2 gives 1.0232 against 0.9768, so +/-2.32% in 1-D. The two
//     separable passes MULTIPLY, 1.0232^2 = 1.0469 against 0.9768^2 = 0.9542,
//     so about TWICE that in 2-D -- +/-4.64%, where Burt leaves exactly none.
//     Measured on a flat 0.5 image, interior: Burt 0.500000 flat, sigma=1
//     0.477 .. 0.523. (Like for like, 2-D is twice 1-D, not four times: the
//     earlier "1.2%" was the weight's deviation in percentage points, which is
//     not the same quantity as the output's.)
//
// (2) SIGMA HAS EXACTLY ONE CORRECT VALUE for a 2x reduction, so the parameter
//     was a knob with one setting. Larger loses detail for nothing; smaller
//     leaves aliasing. Exposing it invited a silent regression -- set 1.5 and
//     the ripple above returns with no test failing, because the kernel choice
//     cannot break collapse (see below). Burt's variance is exactly 1, so this
//     IS the sigma=1 Gaussian to within truncation; the parameter bought
//     nothing and risked something.
//
// It also matches the literature, which matters when comparing against a
// reference implementation at the merge block.
//
// ---- expand() USES PER-PARITY STENCILS, NOT ZERO-INSERT-THEN-BLUR ----
//
// CHANGED 2026-11-06, from mask normalisation. The formulations this replaced
// are in curriculum/notes/pyramids.txt -- the mask and the constant-x4 border
// numbers in section 9, padding in 12b -- and 9a is where the three are compared
// and this one decided. It supersedes them; it does not merely win on taste.
//
// The textbook statement is "insert zeros, blur, multiply by 4" (x2 per
// separable pass, because only one input in four is real). Insert the zeros and
// then ask WHICH TAPS CAN POSSIBLY CONTRIBUTE:
//
//     centred on an EVEN output   sample hole sample hole sample   -> 1 _ 6 _ 1
//     centred on an ODD  output   hole sample hole sample hole     -> _ 4 _ 4 _
//
// Two or three of the five multiplies are times zero at EVERY position. Cross
// them out and the operator collapses to two one-line stencils that read the
// COARSE array directly, with j = i/2:
//
//     i EVEN  ->  ( c[j-1] + 6*c[j] + c[j+1] ) / 8
//     i ODD   ->  ( c[j]            + c[j+1] ) / 2
//
// Not an approximation and not a different filter -- the identical sum with the
// zero terms deleted. Hand-check it: coarse [10, 20, 30] to a 5-wide target
// under reflect gives 12.5, 15, 20, 25, 27.5, and zero-insert-then-blur gives
// the same five numbers. 12_pyramid.cpp group 6c pins them.
//
// WHAT FOLLOWS FROM IT, in the order it matters:
//
// (1) THERE IS NO SCALE FACTOR. Both numerators weigh 8 rather than 16, and
//     that 8 IS the x2: the weight the dropped taps used to consume is simply
//     absent from the divisor. The 2-D x4 is the two passes' two eights. So the
//     constant cannot be forgotten, mis-set, or applied twice -- which matters
//     more than it sounds, because collapse runs the same expand on both sides
//     of a = (a-b)+b and CANNOT SEE A WRONG ONE.
//
// (2) THERE ARE NO HOLES, SO THE EDGE POLICY CANNOT LAND ON THE WRONG PARITY.
//     The policy now applies to the COARSE grid -- to this operation's own
//     input, which is what reduce() does and what the enum is written for. Every
//     value an out-of-range read returns is a real sample at a real position.
//     Measured: a flat image is exact everywhere, border included, under clamp,
//     reflect AND wrap, at even and odd targets alike.
//
// (3) IT SAYS WHAT EXPAND DOES. An odd output is the plain AVERAGE of its two
//     coarse neighbours; an even output keeps mostly its own value. That is
//     legible in a way "stuff zeros and blur" is not.
//
// (4) IT IS CHEAPER, by about 4x. Per output pixel, per pass: the stencils do
//     3 or 2 multiply-adds where the mask formulation did 5 for the numerator
//     AND 5 for the mask, plus a divide. The peak buffer count drops from five
//     target-sized images (samples, mask, two blurs, quotient) to two, and the
//     wider of those two is half the target area.
//
//     (A FUSED 2-D form is cheaper again -- four stencils, one per (x,y) parity
//     pair, 9/6/6/4 taps, no intermediate buffer at all. That is what
//     darktable's local laplacian writes. Not done here: separable keeps the
//     weights in ONE place, which is the trade vc_edge_policy.h already makes
//     for resolve_index.)
//
// ONE BEHAVIOUR CHANGE, AND IT IS DELIBERATE: `zero` no longer preserves a flat
// image. 3x3 -> 5x5 reads (0 + 6 + 1)/8 = 7/8 per axis and 49/64 = 0.765625 at a
// corner. Mask normalisation CANCELLED `zero` away -- numerator and denominator
// lost the same taps -- which made expand the one operation in this library
// where vc_edge_policy.h's documented fading border silently did not happen.
// It now behaves here as it does everywhere else. Pinned in group 6d.
//
// WHAT THIS DOES NOT BUY YOU, so the kernel note above still earns its place:
// independence from the kernel. Both stencils dividing by the same 8 IS Burt's
// 0.5000/0.5000 split, cashed in. A kernel splitting 0.5116/0.4884 needs its own
// unequal PAIR of divisors, and with a single one still ripples +/-2.32% in 1-D
// and about twice that in 2-D. Parity is a property of the stencils; the even
// split is a property of the kernel. Two hardcoded divisors are legitimate only
// because the kernel is fixed -- the same bargain the section above struck.
//
// ---- THE THREE FORMULATIONS THIS REPLACED, (A) AND (B) BOTH CORRECT ----
//
// Recorded because the measurements are real and the reasoning is reusable.
// Sections 9 and 12b of the note have them in full; 9a compares them.
//
// (A) MASK NORMALISATION. Insert zeros, blur, and divide per pixel by the same
//     blur of a 1/0 mask -- the weight that actually landed on real data. It is
//     the general case, and these stencils are it SOLVED IN CLOSED FORM: the
//     blurred mask has only a handful of distinct values, one per
//     (parity, distance-from-edge) class, and in the interior it is exactly 0.5,
//     so "divide by the mask" IS "multiply by 2". Its one real advantage is
//     kernel independence -- it removes the parity ripple for ANY kernel, which
//     two fixed divisors cannot. That is a freedom this library deliberately
//     does not want. Cost: a second blur and a divide.
//
// (B) PAD THE SOURCE, then the constant x4 is exact -- measured 0.50000000 for
//     Burt. This is what Mertens's reference exposure fusion and darktable's
//     local laplacian both do. Rejected here for pad-then-crop index
//     arithmetic: a crop offset per level per axis, and every bug this project
//     has shipped has been index arithmetic (the N/2+1 leak, the causal kernel
//     placement in conv_frequency, the planar-vs-interleaved stride, the
//     colliding taps, the span-vs-extent overflow).
//
// (C) CONSTANT x2 / x4 ALONE, which is what both of the above are fixing.
//     Measured, flat 0.5 expanded 16x16 -> 32x32:
//
//       constant x4, Burt      interior exactly 0.500000   border 0.125 .. 1.125
//       constant x4, sigma=1   interior 0.477 .. 0.523     border 0.119 .. 1.142
//
//     Wrong at every border under clamp for every kernel, and wrong in the
//     interior for any kernel whose taps do not split evenly. The note derives
//     both numbers by hand.
//
// COST NOTE, HONESTLY LABELLED: the timings below were measured on the MASK
// implementation at 1024x1024, release, and have NOT been re-run. They are kept
// because the comparison they correct for is the instructive part.
//
//     one 7-tap blur                 0.0581 s
//     gaussian pyramid (5-tap)       0.0461 s   0.79x raw, 1.11x tap-adjusted
//     laplacian, naive 2-D mask      0.1411 s   2.43x raw, 3.40x tap-adjusted
//
// The raw 0.79x reads as "a whole pyramid is cheaper than one blur", which is
// only true because the blurs are not the same width: make_gaussian_1d_x(1.0) is
// SEVEN taps where Burt's is five, so one blur with it does 1.4x the per-pixel
// work. Worth stating, because that is the shape of comparison that produces a
// wrong conclusion.
//
// ---- THE VARIANT TO AVOID, IN SZELISKI'S OWN WORDS ----
//
//     Ll = Gl - expand(reduce(Gl))    self-inverting          <- this one
//     Ll = Gl - blur(Gl)              NOT self-inverting
//
// Of the second: "This variant has less aliasing, since it avoids one
// downsampling and upsampling round-trip, but it is NOT SELF-INVERTING, since the
// Laplacian images are no longer adequate to reproduce the original image."
// Implement it by accident and collapse fails looking like an arithmetic bug.
//
// ---- STORAGE ----
//
// A laplacian_pyramid keeps the band levels plus ONE coarsest Gaussian. It does
// not keep G0..G(n-1) -- those are intermediates, recoverable from the bands. The
// top must be kept, because there is no band beyond it to reconstruct from.
//
// Total storage is 4/3 of the image, not less: 1 + 1/4 + 1/16 + ... Each level is
// a QUARTER of the AREA (both axes halve), which is why it is 4/3 and not 2. So
// this is an OVERCOMPLETE representation and not a compression scheme; the 33%
// redundancy is what buys independent access to each scale.
//
// THE BAND LEVELS ARE SIGNED. The 1-D hand case gives L0 = [-1, 2, 1, -2]. They
// are f32 and can be negative; only `top` is ordinary brightness. Dumping a band
// needs an offset.
//
// ---- EDGE POLICY ----
//
// Every operation here takes one explicitly rather than defaulting, because the
// choice is visible in the output and week 5 recorded what happens when a
// boundary decision is implicit. `clamp` is the sane choice for a photograph and
// preserves a flat image; `zero` does NOT preserve a flat image (vc_edge_policy.h
// records the 3x3-box corner reading 44.4 from a flat 100), so a pyramid built
// with it darkens at every border on every level.

// A Gaussian pyramid: level 0 is the source size, each later level half of it.
// Always at least one level.
using vc_pyramid = std::vector<vc::vc_image>;

// A Laplacian pyramid: the band-pass levels, coarsest-Gaussian last and separate.
//
// INVARIANT: total levels == bands.size() + 1. bands[l] has the same geometry as
// Gaussian level l, and `top` has the geometry of Gaussian level bands.size().
// A single-level pyramid is legal and has NO bands -- `top` is then the source.
struct vc_laplacian_pyramid {
    vc_pyramid bands; // signed; bands[0] is the finest octave
    vc::vc_image top; // the coarsest Gaussian, kept as-is
};

// How many levels this geometry supports: halving until BOTH sides reach 1.
// A 256x256 image gives 9 (256,128,64,32,16,8,4,2,1). Always at least 1.
//
// This is an upper bound, not a recommendation -- a level narrower than about 8
// pixels holds almost nothing useful.
[[nodiscard]] std::size_t max_pyramid_levels(vc::image_dim width,
                                             vc::image_dim height);

// Blur with the fixed separable binomial, then keep every other pixel.
// Output is ceil(w/2) x ceil(h/2), so a 5-wide row reduces to 3 (indices 0,2,4).
[[nodiscard]] vc::vc_image reduce(const vc::vc_image& src,
                                  vc_edge_policy policy);
// Upsample to target_width x target_height with the two per-parity stencils --
// see the long note above. No zeros are inserted, there is no mask, and there is
// no scale factor to pass: the x2 per axis lives inside the stencils' divisors.
//
// THE EDGE POLICY APPLIES TO THE COARSE GRID, i.e. to `src`, the same as
// reduce(). A flat image comes back exactly flat under clamp, reflect and wrap;
// under `zero` it fades at the border, 49/64 at a corner, exactly as `zero` does
// in every other neighbourhood operation.
//
// Throws invalid_argument unless the target is reachable: each target dimension
// must be src's doubled, or one less (so both 5 and 6 are legal from 3, and
// nothing else is).
[[nodiscard]] vc::vc_image expand(const vc::vc_image& src,
                                  vc::image_dim target_width,
                                  vc::image_dim target_height,
                                  vc_edge_policy policy);
// `levels` Gaussian levels, [0] being the source itself (copied, not blurred).
// Throws if levels is 0 or exceeds max_pyramid_levels().
[[nodiscard]] vc_pyramid gaussian_pyramid(const vc::vc_image& src,
                                          std::size_t levels,
                                          vc_edge_policy policy);
// `levels` total, so levels-1 bands plus the top. Same bounds as above.
[[nodiscard]] vc_laplacian_pyramid laplacian_pyramid(const vc::vc_image& src,
                                                     std::size_t levels,
                                                     vc_edge_policy policy);
// Rebuild the source. MUST be given the same policy the pyramid was built with
// -- expand() appears on both sides of a = (a-b)+b, so a mismatch breaks the
// identity and collapse stops being exact.
//
// The policy is the ONLY thing that can be mismatched here. That is why the
// kernel is fixed rather than a parameter.
[[nodiscard]] vc::vc_image collapse(const vc_laplacian_pyramid& pyramid,
                                    vc_edge_policy policy);

} // namespace vc::pixelops
