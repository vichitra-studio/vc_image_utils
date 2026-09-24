// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <complex>
#include <span>
#include <vector>

#include "vc/core/vc_scalar.h" // vc::real32 -- a LEAF header, no image machinery

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
// The week-6 files split on the same line: fft1d joins this one here, while
// fft2d and spectrum_viz go to pixelops because they do take a vc_image.
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
//     complex<double> accumulator   round-trip max|err|   4.7e-10
//     complex<float>  accumulator   round-trip max|err|   1.8e-06
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
// matters concretely at week 6: fft2d transforms an image row by row, and
// with the buffer planar a row IS a contiguous slice -- a view over it costs
// nothing, where a const-vector& parameter would force a copy per row.
// (Columns are strided and must be gathered either way.)
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
// no power-of-two requirement. fft1d (week 6) is the fast path and validates
// against this one; this stays as the reference precisely because it is the
// literal definition.
//
// Output is the same length as the input. Empty in, empty out.
[[nodiscard]] complex_signal dft1d(complex_view x);

// Forward DFT of REAL samples -- identical to dft1d(to_signal(x)), provided
// because image data is real and the two-call form is ceremony at every call
// site.
//
// This is an ADDITION, not a replacement. fft2d's column pass genuinely needs
// the complex overload above: a 2-D transform is a row pass then a column
// pass, and the row pass already produced complex values for the column pass
// to consume. dft1d must also stay type-compatible with idft1d, or the round
// trip idft1d(dft1d(x)) would not compose.
//
// The cost of routing real data through the complex path is real and
// accepted: roughly 2x the storage and 2x the arithmetic, since half the
// imaginary parts are known-zero and get multiplied anyway, plus to_signal's
// copy. This is the O(N^2) REFERENCE that fft1d is validated against, not the
// fast path. The optimisation has a standard shape -- exploit Hermitian
// symmetry to compute only N/2+1 bins for half the work, which is what Smith's
// Ch 8 real DFT is and what FFTW calls r2c -- and it belongs at P4, where
// FFTW replaces this.
[[nodiscard]] complex_signal dft1d(real_view x);

// Inverse DFT, O(N^2). Same sum with the sign flipped and a 1/N applied.
//
// Empty in, empty out -- which is also why the 1/N cannot divide by zero.
[[nodiscard]] complex_signal idft1d(complex_view spectrum);

} // namespace vc::math
