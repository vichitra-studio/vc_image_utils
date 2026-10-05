// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "vc/core/vc_scalar.h" // vc::real32 -- a LEAF header, no image machinery
#include "vc/math/vc_grid2d.h" // grid2d -- dimensions that cannot be wrong

namespace vc::math {

// The Discrete Fourier Transform: a change of basis.
//
// Everything before this file describes an image by its SAMPLES -- what is the
// value at this pixel. The DFT describes the same data by its COMPONENTS --
// how much of each scale is present, and where each one sits. No information
// is added or lost; the same numbers are written in a different coordinate
// system, and idft1d() writes them back.
//
// This lives in vc/math rather than vc/pixelops because nothing in its
// signature is an image: sequences in, sequences out. That is the same shape
// as vc_linalg -- maths that image code USES, not maths that operates ON
// images. It also fits none of the four categories pixelops is organised by
// (range / domain / neighbourhood / reduction), all of which are statements
// about how an operation touches pixels.
//
// The week-6 files split on the same line -- but NOT where this comment first
// guessed. It used to read "fft2d and spectrum_viz go to pixelops because they
// do take a vc_image". Corrected 2026-10-04, once the signature existed:
// dft2d takes a complex 2-D buffer and RETURNS a complex one, so no vc_image
// appears anywhere in it and the 2.1 rule keeps it here. spectrum_viz does go
// to pixelops -- it takes a vc_image and writes a PNG.
//
// That split is load-bearing rather than tidy. A 2-D spectrum is complex and
// a vc_image is real, so a dft2d phrased in terms of vc_image would have to
// throw the imaginary part away somewhere. Keeping the transform in vc::math
// pushes that discard out to the pixelops boundary, where it is one visible
// line instead of a hidden one -- and 06_dft check 8b is the reason to care:
// a non-zero imaginary part is the alarm that says a filter was not
// twin-symmetric, and calling .real() destroys the evidence.
//
// ---- WHY SINUSOIDS, AND WHY THIS IS NOT ARBITRARY ----
//
// Feed a complex exponential through any convolution:
//
//     y[n] = sum over m of h[m] . x[n-m],  with x[n] = e^(+i.w.n)
//          = sum over m of h[m] . e^(+i.w.n) . e^(-i.w.m)
//          = e^(+i.w.n) . ( sum over m of h[m] . e^(-i.w.m) )
//            ----------     -----------------------------
//            the input          a single complex number
//
// The output is the input back again, scaled. Nothing else has that property
// -- a square wave through a blur is no longer a square wave. Sinusoids are
// not chosen for convenience; they are the only signals a linear
// shift-invariant filter cannot reshape. That parenthesised sum IS the
// forward transform below, applied to the kernel, and it is why a convolution
// becomes one multiply per frequency.
//
// A caution about that derivation, since it is easy to over-read: the minus
// on H is not forced by it. It appeared because the probe was e^(+i.w.n).
// Probe with e^(-i.w.n) instead and the eigenvalue comes out with a PLUS.
// Perfectly symmetric -- the sign was inherited from a choice made one line
// earlier, not derived.
//
// ---- THE SIGN CONVENTION -- THE WHOLE REASON THIS COMMENT EXISTS ----
//
//     forward:   X[k] = sum over n of  x[n] . e^(-2.pi.i.k.n/N)
//     inverse:   x[n] = (1/N) . sum over k of  X[k] . e^(+2.pi.i.k.n/N)
//
// MINUS on the forward transform. Geometrically that is clockwise winding:
// as n increases the angle -2.pi.k.n/N grows more negative.
//
// NOTHING REQUIRES THIS SIGN. Flipping it conjugates the entire spectrum and
// every magnitude-based property survives: round-trip, Parseval, and -- this
// one surprises people -- the CONVOLUTION THEOREM all hold under either
// convention (verified numerically, max error ~7e-15 both ways). It is a
// convention, not a derivation.
//
// Two things the minus buys, and they are the whole justification:
//
//   1. THE PHASE YOU READ BACK IS THE PHASE YOU PUT IN. Feed in
//      M.cos(2.pi.k0.n/N + phi) and this convention reports +phi at bin k0;
//      the other reports -phi. Neither is wrong, but one of them makes you
//      negate every phase you ever look at.
//
//   2. EVERYONE ELSE USES IT -- numpy, FFTW, MATLAB, and Smith's Ch 8. That
//      is worth more than the first point here, because P4 swaps this
//      implementation for FFTW, whose forward transform is also e^(-i...).
//      A conjugated spectrum inverts every phase downstream at that swap,
//      and since magnitudes are untouched, NOTHING FAILS LOUDLY.
//
// So it is pinned by a test rather than by this paragraph:
//
//     x = [1, 2, 3, 4]   ->   X[1] = -2 + 2i     under the convention above
//                             X[1] = -2 - 2i     under the other one
//
// A symmetric real input CANNOT see this. [1,1,1,1] and [1,0,1,0] are
// real-symmetric, so a consistent flip cancels in the round-trip and is
// invisible to any magnitude-based check. The asymmetric case above is the
// only one of the obvious four that can fail.
//
// This is a CROSS-PHASE CONTRACT. P4 swaps this implementation for FFTW,
// whose forward transform is also e^(-i...); the two must agree or every
// phase downstream inverts.
//
// Smith's Ch 8 already uses this convention without saying so. His real-DFT
// analysis equation carries a minus on the imaginary part -- ImX[k] = -sum
// x[i].sin(...) -- and that minus exists precisely so his ReX + i.ImX lines
// up with e^(-i...).
//
// ---- NORMALISATION -- 1/N ON THE INVERSE, NOTHING ON THE FORWARD ----
//
// With the scaling above:
//
//     round trip:   idft1d(dft1d(x)) == x
//     Parseval:     sum |x[n]|^2  ==  (1/N) . sum |X[k]|^2
//
// THIS IS NOT SMITH'S EQUATION 8-3, and the difference is worth stating
// because it is the easiest thing to import by accident. Smith divides by
// N/2, with N at k=0 and k=N/2. That factor is real and derivable -- the
// correlation of a basis function with itself is N times the average of
// cos^2, which is N/2, except at k=0 and k=N/2 where the cosine never takes
// an intermediate value so the average is 1 -- but it belongs to the REAL
// DFT's synthesis, where the output is two arrays of N/2+1 cosine and sine
// amplitudes. This is the COMPLEX DFT: N complex in, N complex out, and the
// only factor anywhere is the 1/N on the inverse. Do not mix them.
//
// ---- ACCUMULATE IN DOUBLE ----
//
// Storage is float, to match vc_image's buf_f32. The inner sum is accumulated
// in double and narrowed once at the end.
//
// The reason is N. This is an O(N^2) transform, so every output bin is a sum
// of N products, and float carries only about 7 decimal digits.
//
// MEASURED at N=1024 (06_dft's signal, float32 storage in both cases):
//
//     complex<double> accumulator   round-trip max|err|   7.45e-09
//     complex<float>  accumulator   round-trip max|err|   1.8e-06
//
// CORRECTED 2026-10-05: these lines read 4.7e-10 for the double accumulator,
// here and in 06_dft.cpp and in the P2 phase doc. Re-measured against the
// current build on 06_dft's own N=1024 signal it is 7.45e-09 -- SIXTEEN TIMES
// larger, and 7.45e-09 is the figure notes/buildlog_p2_week5.txt recorded at
// the time, so three of the four places that quoted it were wrong and the
// build log was right. Where 4.7e-10 came from is unknown; it was not
// reproduced. The consequence is in the margin, below.
//
// A factor of ~3800. Note this is smaller than the worst-case bound N*eps
// would suggest (~1.2e-4) because the per-term errors partially cancel --
// quote the measurement, not the bound.
//
// A four-point hand case cannot see any of this: at N=4 both accumulators
// are exact. Same blindness as testing a convolution with a symmetric
// kernel, and it is why 06_dft carries a large-N round trip at all.

// One complex sample, at the library's declared precision (vc_scalar.h).
//
// real32 and NOT buf_f32, although they are the same type: buf_f32 exists to
// be greppable as PIXEL DATA, and a spectrum coefficient is not pixel data.
// Reusing it here would break the one property that alias is for. The
// precision they share is declared once, in vc_scalar.h, so they cannot drift.
//
// Storage precision is a separate decision from ACCUMULATOR precision -- the
// inner sum runs in double regardless. See the note above.
using vc_complex = std::complex<vc::float32>;

// A sequence of samples, OWNED. The same type serves as a signal and as a
// spectrum, because the transform is a change of basis and not a change of
// kind.
using complex_signal = std::vector<vc_complex>;

// The same sequence, BORROWED. Every entry point below follows one rule:
//
//     TAKE A VIEW, RETURN STORAGE.
//
// A caller should not have to own a std::vector to be transformed. It also
// matters concretely at dft2d below: the row pass transforms an image row by
// row, and with the buffer planar a row IS a contiguous slice -- a view over
// it costs nothing, where a const-vector& parameter would force a copy per
// row. (Columns are strided and must be gathered either way.) That asymmetry
// is also why dft2d pins ROWS FIRST: the cheap pass goes first.
//
// This matches vc_image::with_pixels, which already hands out a
// std::span<const vc::buf_f32> rather than the underlying vector.
//
// complex_signal converts to complex_view implicitly, so chaining still reads
// the same: idft1d(dft1d(x)). The usual span caveat applies -- a view does not
// extend a temporary's lifetime, so bind a result to a complex_signal, never
// to a complex_view.
using complex_view = std::span<const vc_complex>;
using real_view = std::span<const vc::float32>;

// Lift real samples into a complex signal, imaginary parts zero. Convenience
// for callers whose data is a scanline rather than a spectrum.
[[nodiscard]] complex_signal to_signal(real_view real_samples);

// ---- the transforms ---------------------------------------------------------

// Forward DFT, O(N^2). Direct evaluation of the sum above -- no factorisation,
// NO POWER-OF-TWO REQUIREMENT; any length works, including odd and prime.
//
// There is no fast path in this library. An earlier version of this comment
// said "fft1d (week 6) is the fast path and validates against this one" --
// fft1d was DROPPED from week 6 (see the P2 phase doc), so that promise is
// withdrawn. FFTW takes the role at P4, behind an own fft() wrapper. This
// stays the reference because it is the literal definition.
//
// Output is the same length as the input. Empty in, empty out.
[[nodiscard]] complex_signal dft1d(complex_view x);

// Forward DFT of REAL samples -- identical to dft1d(to_signal(x)), provided
// because image data is real and the two-call form is ceremony at every call
// site.
//
// This is an ADDITION, not a replacement. dft2d's column pass genuinely needs
// the complex overload above: a 2-D transform is a row pass then a column
// pass, and the row pass already produced complex values for the column pass
// to consume. dft1d must also stay type-compatible with idft1d, or the round
// trip idft1d(dft1d(x)) would not compose.
//
// The cost of routing real data through the complex path is real and
// accepted: roughly 2x the storage and 2x the arithmetic, since half the
// imaginary parts are known-zero and get multiplied anyway, plus to_signal's
// copy. That cost is accepted because this is the only implementation there
// is, and correctness is what it is for. The optimisation has a standard
// shape -- exploit Hermitian
// symmetry to compute only N/2+1 bins for half the work, which is what Smith's
// Ch 8 real DFT is and what FFTW calls r2c -- and it belongs at P4, where
// FFTW replaces this.
[[nodiscard]] complex_signal dft1d(real_view x);

// Inverse DFT, O(N^2). Same sum with the sign flipped and a 1/N applied.
//
// Empty in, empty out -- which is also why the 1/N cannot divide by zero.
[[nodiscard]] complex_signal idft1d(complex_view spectrum);

// ---- the 2-D transforms -----------------------------------------------------
//
// An image is 2-D, and the 2-D transform is TWO PASSES OF THE 1-D ONE. That is
// not an optimisation bolted on afterwards; it falls out of the definition in
// one step. Szeliski's Eq 3.60 is
//
//     H(kx,ky) = sum over x, sum over y of  h(x,y) . e^(-2.pi.i.(kx.x/M + ky.y/N))
//
// and the exponent is a SUM, so the exponential FACTORS:
//
//     e^(-2.pi.i.(kx.x/M + ky.y/N))  =  e^(-2.pi.i.kx.x/M) . e^(-2.pi.i.ky.y/N)
//
// The SECOND factor has no x in it, so it is a constant as far as the inner
// x-sum is concerned and COMES OUT OF IT -- leaving the x-sum as a bracket:
//
//     H(kx,ky) = sum over y of  e^(-2.pi.i.ky.y/N) . [ sum over x of h(x,y) . e^(-2.pi.i.kx.x/M) ]
//                                                     -------------------------------------------
//                                                     a 1-D DFT ALONG ROW y, evaluated at kx
//
// (It is the y-FACTOR that moves out, not the x-sum. An earlier draft of this
// comment said "the x-sum can be pulled outside", which contradicts the line
// directly beneath it -- the x-sum is the thing that stays innermost.)
//
// Do that bracket for every row -- that is PASS 1 -- and you are left with
// G(kx, y). Then the outer sum over y, at fixed kx, is a 1-D DFT down column
// kx of G. That is PASS 2. Two passes of the 1-D transform, nothing else.
//
// The order is interchangeable: nothing above forced the x-sum inside rather
// than the y-sum, so pulling the x-factor out instead gives columns-then-rows
// and the identical answer.
//
// This is the separable-Gaussian argument from week 5, one level up. A
// separable KERNEL factors into (horizontal) x (vertical), so you convolve
// rows then columns. A 2-D BASIS FUNCTION factors the same way, so you
// transform rows then columns. Same structural fact, different object.
//
// ---- WHAT SEPARABILITY IS WORTH, WITH NO FFT INVOLVED ----
//
//     direct      (M.N)^2         every output bin touches every input sample
//     separated   M.N.(M + N)     M row transforms + N column transforms
//
//     ratio = M.N / (M + N)   ->   at 256 x 256:  65536/512 = 128x
//
// That 128x is why this is usable on real images at all without fft1d, which
// was dropped from week 6 (see the P2 phase doc). Dropping it gave up a log
// factor; it never touched this one.
//
// ---- LAYOUT ----
//
// Row-major, as the pixel buffers are:  index(x, y) = y * width + x.
// Output indexes the same way:          index(kx, ky) = ky * width + kx.
//
// Dimensions travel as parameters rather than in a wrapper type, because the
// only thing either function needs to know about shape is where a row ends.
//
// PASS ORDER IS DOCUMENTED, NOT JUST CHOSEN: rows first, then columns. The
// answer is the same either way, but spectrum_viz's reader should not have to
// guess, and an intermediate dump is unreadable without knowing.
//
// ---- SCALING: ADD NOTHING. THERE ARE NOW THREE CONVENTIONS IN PLAY ----
//
//     vc_dft.h (here)   forward: nothing     inverse: 1/N per call
//     FFTW              forward: nothing     inverse: nothing (caller divides)
//     Szeliski Eq 3.60  forward: 1/(M.N)     inverse: unstated
//
// dft2d is two unscaled dft1d passes, so it applies NOTHING. idft2d is two
// idft1d passes, and each contributes its own 1/N -- 1/width on the row pass
// and 1/height on the column pass -- so the round trip recovers 1/(M.N)
// automatically and exactly.
//
// DO NOT COPY EQ 3.60'S 1/(M.N) INTO THE FORWARD PASS. If you do, and then
// invert with idft2d, you divide twice: the round trip comes back M.N times
// too small. At 256x256 that is 65536x, which reads as a black image rather
// than as a subtle error -- so it is at least a loud bug. The quiet version is
// worse: a forward-only comparison against someone else's spectrum silently
// disagrees by a constant factor, and magnitudes-only checks cannot see a
// uniform scale at all. Parseval cannot either; it scales right along with it.
//
// ---- NON-SQUARE INPUT IS NOT AN EDGE CASE, IT IS THE TEST ----
//
// On a square image a transposed pass, a swapped width/height, and a stride
// computed from the wrong dimension are ALL INVISIBLE -- the output is the
// transpose of the right answer, which for many test inputs is the right
// answer. The same shape of blindness as week 5's symmetric kernel hiding the
// convolution-vs-correlation flip, and week 5's [1,1,1,1] hiding the sign
// convention. 07_dft2d therefore checks 4-wide-by-2-high AND 2-wide-by-4-high
// against an independent direct implementation of the definition above.
//
// ---- CONTRACT: THERE IS NOTHING TO VALIDATE HERE ----
//
// The dimensions arrive as a vc::math::grid2d, which cannot exist unless they
// are both non-zero and their product equals the buffer's length. So these two
// functions perform no checks of their own and have no argument-validation
// failure mode to document. See vc_grid2d.h for why that is a TYPE rather than
// a helper, and for why uint32 dimensions make the overflow hazard
// unrepresentable instead of merely guarded.
//
// An earlier version of this comment specified those checks in detail,
// including the overflow-safe divide-not-multiply form and a worked
// counterexample (width = 2^63 + 4 with height = 2 multiplies to exactly 8).
// The implementation written against it had NO VALIDATION AT ALL, and ASan
// caught a heap-buffer-overflow on the first malformed input. The
// documentation was correct and did not transmit. That is the argument for
// grid2d in one sentence, and it is why the checks moved into a constructor
// instead of into a longer comment.
//
// ANY DIMENSIONS, not just powers of two -- dft1d has no such restriction and
// neither does this. Odd, prime and coprime sides all work.
//
// Note the asymmetry with dft1d, which keeps "empty in, empty out": grid2d
// refuses a zero width or height, so there is no empty 2-D case to reach. A
// 1-D transform takes no dimension argument, so an empty input is unambiguous
// -- a zero-length signal has a zero-length spectrum. A 2-D transform does
// take dimensions, and a zero in either is ill-formed rather than empty; there
// is no sensible answer to "the DFT of a 0-by-7 image". vc_grid2d.h argues
// this at length, and vc_image_writer::validated() has refused zero-sized
// images since P1, so this is the library's existing position, not a new one.
[[nodiscard]] complex_signal dft2d(complex_view plane, grid2d extent);

// Inverse 2-D DFT. Two idft1d passes, so the 1/(width*height) arrives for
// free -- see the scaling note above.
//
// Returns complex, NOT real, and deliberately. For a spectrum that came from
// real samples the imaginary part is zero to rounding, and for one that did
// not, a complex result is the correct answer. Discarding it here would throw
// away the only signal that says a frequency-domain filter was not
// twin-symmetric -- which is what 06_dft check 8b exists to assert, and what
// conv_theorem will need in week 6.
[[nodiscard]] complex_signal idft2d(complex_view spectrum, grid2d extent);

} // namespace vc::math
