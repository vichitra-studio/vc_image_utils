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
// (1) ITS TAPS SPLIT EXACTLY EVENLY between the positions that land on samples
//     and those that land on the zeros expand() inserts:
//
//       Burt      even taps (-2,0,+2)  (1+6+1)/16 = 0.5000
//                 odd  taps (-1,+1)    (4+4)/16   = 0.5000      EQUAL
//
//       sigma=1   even  0.0545+0.4026+0.0545 = 0.5116
//       truncated odd   0.2442+0.2442        = 0.4884           off by 1.2%
//
//     In 2-D the two separable passes MULTIPLY -- 0.5116^2 x 4 = 1.0469 against
//     0.4884^2 x 4 = 0.9542 -- so a truncated Gaussian leaves a +/-4.64%
//     checkerboard in expand() where Burt leaves exactly none. Measured on a
//     flat 0.5 image, interior: Burt 0.500000 flat, sigma=1 0.477 .. 0.523.
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
// ---- expand() MUST NORMALISE, NOT MULTIPLY BY A CONSTANT ----
//
// The textbook statement is "upsample, blur, multiply by 4" (x2 per separable
// pass, because only one input in four is real). MEASURED, that is wrong at
// every border, for every kernel INCLUDING BURT'S. Do this instead:
//
//     numerator   = blur(upsampled samples)
//     denominator = blur(a MASK, 1 where a real sample was placed, 0 elsewhere)
//     out         = numerator / denominator
//
// WHY: A HOLE IS MISSING DATA, NOT DARK DATA. Strip the kernel away -- you want
// the average around a pixel, two neighbours exist holding 10, three positions
// are holes:
//
//     treat holes as ZERO, divide by 5    (10+10+0+0+0)/5 =  4    WRONG
//     divide by what EXISTS               (10+10)/2       = 10    right
//
// The upsampled grid is that situation at every other position. A blur has no
// concept of a hole: a zero contributes nothing to the numerator while still
// consuming the weight you assumed it would carry. The constant is an
// ASSUMPTION ("a quarter of the weight always lands on real data"); the mask is
// a MEASUREMENT of how much actually did.
//
// vc_edge_policy.h already documents this exact bug for `zero`: "A 3x3 box at a
// corner has four taps inside and five out, so the applied weights sum to 4/9
// and a flat 100 reads back as 44.4." expand() has it IN THE INTERIOR, not only
// at the edges, because the holes are everywhere.
//
// Measured, flat 0.5 expanded 16x16 -> 32x32, whole image including borders:
//
//     constant x4,   Burt       interior exactly 0.500000   border 0.125 .. 1.125
//     constant x4,   sigma=1    interior 0.477 .. 0.523     border 0.119 .. 1.142
//     NORMALISED,    either     0.50000000 everywhere
//
// WHERE 1.125 AND 0.125 COME FROM. Upsampled flat c is [c, 0, c, 0, ...]. An
// interior even position reads c,0,c,0,c -> 8c/16 = 0.5c, as intended. At
// position 0, clamp makes the two taps reaching left read up[0] = c instead of
// zeros -> (1+4+6+0+1)c/16 = 12c/16 = 0.75c, which is 1.5x too much; two
// separable passes make a corner 1.5 x 1.5 = 2.25, and 2.25 x 0.5 = 1.125. The
// opposite corner is the mirror: where the last index is ODD it holds a zero,
// the clamped taps read zeros where samples belonged, 4c/16 = 0.25c, and
// 0.5 x 0.5 x 0.5 = 0.125. One cause, opposite parity, 225% and 25%.
//
// THE MASK IS NEARLY FREE, which is what makes this affordable:
//
//   IT IS AN OUTER PRODUCT. The mask is 1 exactly where x is even AND y is
//   even, so it is separable, and blurring a separable thing separably keeps it
//   separable: den(x,y) = dx(x) . dy(y). Verified, max|den_2d - dx (x) dy| =
//   0.000e+00. Storage W+H, not W*H; cost two 1-D convolutions.
//
//   IT DEPENDS ONLY ON GEOMETRY -- not the image, not the channel, not the
//   frame. Compute once per level SHAPE and reuse across channels and across
//   every frame of a bracket.
//
// Multiply-adds per ORIGINAL pixel, separable 5-tap at 10 per blur, level areas
// summing to 4/3:
//
//     gaussian pyramid                        13.3
//     + laplacian                             26.7
//     + mask, naive 2-D per expand            40.0
//     + mask, outer product and cached        26.7
//
// MEASURED against one full-image blur at 1024x1024, release, and the table
// holds to within about 15%. The comparison needs a correction to be fair:
// make_gaussian_1d_x(1.0) is SEVEN taps where Burt's is five, so a single blur
// with it does 1.4x the per-pixel work.
//
//     one 7-tap blur                 0.0581 s
//     gaussian pyramid (5-tap)       0.0461 s   0.79x raw, 1.11x tap-adjusted
//                                               (table predicts 1.33x)
//     laplacian, naive 2-D mask      0.1411 s   2.43x raw, 3.40x tap-adjusted
//                                               (table predicts 4.00x)
//
// The raw 0.79x reads as "a whole pyramid is cheaper than one blur", which is
// only true because the blurs are not the same width -- worth stating, because
// that is the shape of comparison that produces a wrong conclusion.
//
// ---- THE REJECTED ALTERNATIVE, AND WHY ----
//
// Padding the source before upsampling also fixes the border, and then the
// constant x4 is exact -- measured 0.50000000 for Burt. It is cheaper. It was
// rejected because pad-then-crop is MORE INDEX ARITHMETIC: a crop offset per
// level per axis.
//
// That is an evidence-based choice, not an aesthetic one. Every bug this project
// has actually shipped has been index arithmetic -- the N/2+1 leak, the causal
// kernel placement in conv_frequency, the planar-vs-interleaved stride, the
// colliding taps, the span-vs-extent overflow. The mask trades a division for a
// whole category of off-by-one, and the division is free.
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
// Upsample to target_width x target_height by inserting zeros, blur with the
// same fixed kernel, and NORMALISE per output pixel by the blurred mask -- see
// the long note above. Do NOT multiply by a constant 4; that is wrong at every
// border, for every kernel including Burt's.
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
