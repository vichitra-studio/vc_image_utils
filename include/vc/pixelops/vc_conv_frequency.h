// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_image.h"
#include "vc/math/vc_grid2d.h" // grid2d -- dimensions that cannot be wrong
#include "vc/pixelops/vc_convolve.h"    // vc_kernel
#include "vc/pixelops/vc_edge_policy.h" // vc_edge_policy

namespace vc::pixelops {

// Convolution by multiplying spectra -- the convolution theorem, executable.
//
// ---- WHAT THIS IS FOR, AND WHAT IT IS NOT FOR ----
//
// This is the SAME OPERATION as convolve(), reached by a different route, and
// the point is that the two must agree. It is not a faster convolve(): see the
// cost note at the bottom, where the honest answer is that with an O(N^2)
// dft1d underneath it is always slower, and even with a real FFT it only wins
// for kernels larger than roughly 7x7.
//
// It exists because (a) agreement between the two paths is the only real test
// of the convolution theorem, and (b) frequency-domain filtering is the shape
// that P9a-ii's Wiener deconvolution needs -- that one has no spatial form at
// all, because its filter is defined as a ratio per frequency.
//
// ---- ONLY TWO EDGE POLICIES ARE EXPRESSIBLE. THIS IS THE LESSON ----
//
//     wrap     supported -- and it is what the DFT does NATURALLY
//     zero     supported -- by padding, which is a statement about the signal
//     clamp    REJECTED
//     reflect  REJECTED
//
// The rejections are not missing features. The DFT's basis functions are
// periodic -- bin k and bin k-N are the same pattern -- so a spectrum can only
// ever describe a signal that tiles the plane. "Replicate the edge pixel" and
// "mirror about the edge" are not things a periodic signal can do. There is no
// spectrum whose inverse transform clamps.
//
// So convolve_frequency(src, k, clamp) throws rather than approximating. A
// function that silently gave you wrap when you asked for clamp would be the
// worst possible outcome here, because the difference shows up only in a few
// border pixels -- exactly the kind of error week 5's edge-policy work was
// about not shipping.
//
// Corollary worth holding: comparing convolve(clamp) against ANY frequency
// result will differ at the borders, always, and neither side is wrong.
//
// ---- WRAP IS THE EXACT CROSS-CHECK ----
//
// vc_edge_policy.h already names this: wrap makes the convolution matrix
// circulant, and circulant matrices are diagonalised by the DFT. So
//
//     convolve_frequency(src, k, wrap)  ==  convolve(src, k, wrap)
//
// to float tolerance, EVERYWHERE INCLUDING THE BORDERS -- because both paths
// make the same assumption about what lies past the edge. If those two
// disagree, something is broken; "the boundaries differ" is not available as
// an explanation. That is the check this file exists to make possible.
//
// ---- THE EXTENT, AND WHY N+M-1 ----
//
//     wrap   transform at the source extent. No padding.
//     zero   transform at (W + kw - 1) x (H + kh - 1).
//
// The padding is not a patch applied to make the DFT behave. The DFT sees the
// signal TILED, and under wrap the kernel at one edge reaches into the next
// copy -- which is the honest convolution of a periodic signal, and is the
// right answer when the signal really is periodic (a tiling texture, a 360
// degree panorama, week 7's Poisson solve, which depends on it).
//
// For a finite image you wanted the LINEAR convolution instead, and you say so
// by surrounding the signal with zeros so the periodic copies cannot touch.
// The gap has to be as wide as the kernel reaches, which is kw-1 along x:
//
//     L  >=  W  +  (kw - 1)
//            ^      ^^^^^^^
//         signal   the kernel's reach -- the gap the copies need
//
// Hence N+M-1, which is a buffer width rather than a formula to memorise.
// Larger is merely wasteful; smaller is contaminated.
//
// NOTE this library does NOT need a power-of-two extent. Smith pads his
// example to 512 because a radix-2 FFT demands it; dft1d/dft2d work at any
// size (06_dft tests N=3 and N=5 deliberately), so the extent is exactly
// N+M-1 and nothing is rounded up. One constraint this project does not carry.
//
// ---- KERNEL PLACEMENT IS WHERE THIS GOES WRONG ----
//
// convolve() centres the kernel: taps run -radius..+radius. A DFT-domain
// multiply implements sum_m h[m] . x[(n-m) mod L] with h indexed 0..L-1. To
// make those the same operation, place tap (kx, ky) at
//
//     plane index  ( ((kx % L_x) + L_x) % L_x ,  ((ky % L_y) + L_y) % L_y )
//
// i.e. the centre tap at (0,0) and negative offsets wrapped to the far end.
// (The double modulo for the same reason vc_edge_policy's wrap documents it:
// C++ % returns a negative remainder for negative operands.)
//
// Done that way the output needs NO CROP OFFSET -- the answer for pixel (x, y)
// is at plane index (x, y), and the zero-padded case is just the top-left
// W x H of the padded result.
//
// The alternative -- placing the kernel causally at indices 0..kw-1 -- also
// works, but then every output is shifted by (radius_x, radius_y) and the crop
// must start there. Getting that wrong produces a CORRECT-LOOKING IMAGE
// SHIFTED BY A FEW PIXELS, which no magnitude check can see. It is the failure
// mode section F.1(b)'s phase-ramp criterion exists to catch, and it is why
// the centred-and-wrapped placement is the one documented here.
//
// (notes/waves_and_signals.txt section 5d works its hand case with CAUSAL
// indexing and gets [2,2,1,1]. The centred convention here gives [2,1,1,2] on
// the same input -- the same answer shifted by one. Both are right for their
// own convention; the example in 11_conv_theorem asserts the centred one,
// because that is what convolve() does.)
//
// ---- NO EXTRA SCALING ----
//
// dft2d applies nothing on the forward pass and idft2d applies 1/(W*H) on the
// inverse, so the pair already composes to the identity. Do NOT add the 1/MN
// that Szeliski's Eq 3.60 puts on the forward transform: with idft2d's own
// factor that divides twice, and the result comes back W*H times too small --
// at 256x256 that is 65536x, which reads as a black image rather than as a
// subtle error. Three normalisation conventions are in play across this
// project (this library's, FFTW's, Szeliski's); see the cross-phase contract
// note in the P2 phase doc.
//
// ---- THE INVERSE MUST COME BACK REAL, AND THAT IS A FREE ALARM ----
//
// A real image convolved with a real kernel is real. The spectra involved are
// twin-symmetric, their product is twin-symmetric, and so the inverse has
// zero imaginary part up to float noise -- 06_dft check 8b is exactly this
// property, with a deliberately broken twin as its control.
//
// So the implementation must CHECK max|imag| before taking the real part, not
// just call .real() and move on. Calling .real() blind throws away the
// evidence that something upstream was not twin-symmetric, which is the
// classic way a frequency-domain filter ships quietly wrong. The threshold is
// relative to the result's RMS, per section F.3 -- an absolute tolerance that
// passes on a dark image fails on a bright one for entirely correct code.
//
// ---- COST, MEASURED ----
//
// 128x128 image, 5x5 kernel, five runs averaged, wrap policy:
//
//     spatial convolve()          0.0035 s release   0.0169 s debug
//     convolve_frequency()        0.0638 s release   0.3900 s debug
//     ratio                       18.2x              23.1x
//
// So this path is roughly 20x slower, and slower at every size this library
// will use. That is the expected outcome: it does three O(N^2) transforms
// where the spatial path does W*H*kw*kh multiplies.
//
// AN EARLIER VERSION OF THIS NOTE SAID ~60x, derived from an operation count
// (410k against 25M) rather than a clock. It was wrong by a factor of three,
// because operation counts do not transfer between different inner loops: the
// spatial path resolves an edge policy per tap, while the DFT's inner loop is
// a tight multiply-add. Recorded because the mistake is reusable.
//
// UNVERIFIED, and suspect for the same reason: with a real FFT the crossover
// is somewhere around kernel area > 3*log2(W*H), about 42 at 128x128, so
// roughly 7x7. That estimate rests on the same operation-counting that just
// proved wrong by 3x, and there is no FFT here to measure it against. Treat it
// as an order of magnitude, not a number.
//
// What is NOT in doubt: small kernels belong in the spatial domain, and the
// frequency domain is for filters DEFINED in frequency (P9a-ii's Wiener) or
// for kernels large enough to pay for the transforms.

// The extent the frequency path will transform at, for this source and policy.
//
// Separate from convolve_frequency() so the N+M-1 arithmetic is testable on
// its own -- it is one line and it is where an off-by-one would live.
//
// Throws invalid_argument for clamp or reflect, for the reason above.
[[nodiscard]] vc::math::grid2d frequency_extent(const vc::vc_image& src,
                                                const vc_kernel& kernel,
                                                vc_edge_policy policy);

// Convolve via the frequency domain. Output geometry, channel count and dtype
// follow the source, exactly as convolve() does; each channel is transformed
// independently. Input must be f32.
//
// Throws invalid_argument for clamp or reflect, and if the inverse transform
// comes back with a non-negligible imaginary part (which would mean a bug in
// this function, not bad input -- see the alarm note above).
[[nodiscard]] vc::vc_image convolve_frequency(const vc::vc_image& src,
                                              const vc_kernel& kernel,
                                              vc_edge_policy policy);

} // namespace vc::pixelops
