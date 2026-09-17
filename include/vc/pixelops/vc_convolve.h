// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <vector>

#include "vc/core/vc_image.h"
#include "vc/core/vc_types.h"
#include "vc/pixelops/vc_edge_policy.h" // vc_edge_policy -- shared vocabulary

namespace vc::pixelops {

// Convolution: the linear shift-invariant filter.
//
// This is a NEIGHBOURHOOD operation, and it is the first one in this codebase.
// The four-way split P1 arrived at classifies operations by what the INTERFACE
// must provide, not by what the operation conceptually is:
//
//     range          g(x) = h(f(x))    acts on the VALUE     needs nothing special
//     domain         g(x) = f(h(x))    acts on the POSITION  needs a sampler,
//                                                            an edge policy,
//                                                            an output-size policy
//     neighbourhood  output depends on a pixel's NEIGHBOURS  needs a kernel
//                                                            and a boundary policy
//     reduction      image in, not-an-image out              needs a result type
//
// warp() occupied `domain` and discovered what that category wanted. This file
// occupies `neighbourhood` and answers the same question for it: a kernel and
// a boundary policy, and notably NO sampler and NO output-size policy. The
// output is always the same size as the input, because a filtered image is
// still a picture of the same scene.
//
// ---- WHY LINEAR SHIFT-INVARIANT, AND WHY THAT PHRASE ----
//
// Two properties, independent of each other, both required:
//
//     linear           L(a*f1 + b*f2) = a*L(f1) + b*L(f2)
//     shift-invariant  shift the input, and the output is the same picture,
//                      shifted the same way. The filter does not know WHERE
//                      in the image it is.
//
// An operator is a convolution IF AND ONLY IF it is both. That is a
// characterisation, not a property convolution happens to have, and it has a
// practical edge: given a black box known to be LSI, feed it a single impulse
// and the output IS its kernel. One measurement characterises the system
// completely. Hence "impulse response".
//
// Neither property implies the other, which is why both are named:
//
//     median filter        shift-invariant, NOT linear  -> no kernel exists
//     vignetting correct.  linear, NOT shift-invariant  -> no kernel exists
//
// As a matrix this is exact. Any linear operator on an image is SOME matrix
// (N^2 free parameters). Shift invariance is the extra constraint that makes
// that matrix Toeplitz -- constant along each diagonal, so the entry depends
// only on (i - j). Those 2N-1 remaining parameters ARE the kernel.
//
// One honest caveat, since this file is largely about boundaries: a
// same-size convolution is only strictly shift-invariant in the INTERIOR. Any
// boundary policy breaks it at the edges, because shifting the input brings in
// content the policy has to invent. The Toeplitz matrix above is exact for the
// infinite case; the finite one has its first and last rows modified, and
// `wrap` is the only policy that keeps the matrix a clean rotation of itself
// (circulant) rather than ragged. That is not an aside -- it is why the
// spatial and frequency paths can only be expected to agree under `wrap` or
// under matched zero-padding.
//
// ---- CONVOLUTION vs CORRELATION: THE FLIP ----
//
// The two differ by the sign of the offset, and by nothing else:
//
//     correlation   g(i,j) = sum  f(i+k, j+l) h(k,l)
//     convolution   g(i,j) = sum  f(i-k, j-l) h(k,l)
//
// Minus offsets step through the kernel backwards, which is the kernel rotated
// 180 degrees. So, exactly:
//
//     correlate(f, h)  ==  convolve(f, reversed(h))
//
// which is why correlate() below is a flip and a delegation rather than a
// second loop. One algorithm, one place for it to be wrong.
//
// For a SYMMETRIC kernel -- box, Gaussian, Laplacian -- reversal changes
// nothing and the two operations coincide exactly. That is why the confusion
// survives: the filters reached for most often cannot expose it. It shows up
// the moment a kernel is asymmetric, and for an ANTISYMMETRIC one (a
// derivative, h(-k) = -h(k)) reversal IS negation, so convolution and
// correlation differ by a pure sign -- every edge points the wrong way and
// nothing crashes.
//
// Three things break if correlation is used where convolution is required:
//
//   1. The convolution theorem. F(f * h) = F(f) . F(h) holds ONLY for
//      convolution; correlation picks up a conjugate. IFFT(FFT(f).FFT(h))
//      therefore computes CONVOLUTION, always -- the frequency domain does not
//      offer the choice. A spatial path that quietly correlates will disagree
//      with the frequency path on any asymmetric kernel, and the FFT will get
//      the blame.
//   2. Commutativity and associativity. Convolution has both; correlation has
//      neither. Kernel pre-composition and the separability argument below are
//      convolution's algebra, not correlation's.
//   3. The delta identity. convolve(delta, h) reproduces h; correlate(delta, h)
//      reproduces its mirror.
//
// Deep-learning frameworks compute correlation and call it convolution. That
// is harmless there, because the weights are learned and the network simply
// learns whatever orientation it needs -- but it means the word is overloaded
// across sources.
//
// ---- THE KERNEL'S ORIGIN IS A DECLARATION ----
//
// Nothing in the summation above says where the kernel's index (0,0) sits. The
// sum ranges over whatever index set the kernel has. Index a 3x3 kernel -1..1
// and h(0,0) is its centre; index it 0..2 and h(0,0) is its top-left corner.
// The arithmetic works either way. Szeliski writes equation 3.12 without ever
// stating the range, and Smith marks an origin on his PSF diagrams precisely
// because the formula will not tell you.
//
// This is the area convention again, one level up: WHERE A NUMBER SITS IN
// SPACE IS DECLARED, NOT DISCOVERED. And it fails the same quiet way -- a
// corner-anchored box blur still blurs correctly, it just moves the entire
// image by (w/2, h/2), which reads as "working".
//
// THIS FILE DECLARES: the kernel is CENTRED. h(0,0) is the middle tap, and
// vc_kernel below rejects even dimensions at construction so that "the middle
// tap" always exists rather than being one of two defensible guesses.
//
// Smith corner-anchors and is not wrong, because his axis is TIME: a causal
// filter cannot read samples that have not happened yet, so it must take only
// past ones and accept a delay of (M-1)/2 -- Ch 7's "linear phase". This axis
// is SPACE. An image has all its pixels at once, there is no future to be
// blind to, and the same delay would just be the picture visibly moving. A
// centred symmetric kernel is ZERO phase: no shift at all.
//
// ---- BOUNDARIES ----
//
// A kernel centred on an edge pixel reaches for neighbours that do not exist.
// There is no correct answer, only a decided one, so it is a parameter --
// vc_edge_policy, shared with warp() rather than re-declared here.
//
// The policy is consulted PER TAP. An out-of-range tap is redirected or
// zeroed; its in-range partners contribute normally.
//
// Note what `zero` costs. On a 3x3 box at a corner, four taps land inside and
// five do not, so the applied weights sum to 4/9 and a uniform 100 comes out
// as 44.4 -- that deficit IS the dark border everyone has seen. clamp, reflect
// and wrap all preserve a uniform value exactly, because every redirected tap
// still reads a real pixel.
//
// Which makes uniform images and ramps complementary tests: a uniform image
// catches `zero`'s DC loss and is blind to the other three; a ramp makes all
// four read different values and so can tell them apart.
//
// ---- SEPARABILITY ----
//
// If a 2-D kernel is the outer product of two 1-D kernels, k = v * h^T, then
// convolving by v then by h gives the same answer as the full 2-D pass, for
// 2K multiplies per pixel instead of K*K. At K=7 that is 14 against 49.
//
// A Gaussian separates because its exponent is a SUM:
//
//     exp(-(x^2/2sx^2 + y^2/2sy^2)) = exp(-x^2/2sx^2) . exp(-y^2/2sy^2)
//
// and exp(a+b) = exp(a).exp(b). That still holds when sx != sy, so an
// elliptical Gaussian is separable -- but ROTATING one puts a cross-term xy in
// the exponent, which cannot be split into (function of x).(function of y).
// Separability means axis-aligned.
//
// This file only ever CONSTRUCTS separable kernels from their known 1-D
// factors. DETECTING whether an arbitrary matrix is separable is a rank-1 test
// and needs SVD, which is not built here (P3), and auto-detection is
// explicitly out of scope for this phase.

// A tap's offset from the kernel's CENTRE. Signed, and that is the whole point
// of naming it: a kernel offset runs -radius..+radius, so negative values are
// the normal case rather than an error case. Written as an unsigned type
// (size_t is the tempting one) a loop from -radius wraps to about 1.8e19 and
// the body never executes -- legal, silent, and the compiler cannot warn,
// because unsigned wraparound is defined behaviour. Follows the same naming
// habit as vc::image_dim and vc::channel_count.
using kernel_offset = std::int64_t;

// A kernel's weights, row-major. Named partly for readability at the
// constructor -- which takes it by value and moves -- and partly to mark the
// seam: these are plain `float`, deliberately NOT the vc::buf_f32 that pixel
// storage uses. Kernel weights and pixel elements are different things and are
// free to stay different types.
using kernel_weights = std::vector<float>;

// A 2-D convolution kernel: weights, plus the declaration that its centre tap
// is the origin.
//
// Deliberately NOT a vc_image. An image carries a dtype, a channel count and a
// metadata slot, none of which a 3x3 of weights has any use for, and giving a
// kernel an image's interface would invite it to be dumped, warped and
// round-tripped. Different thing, different type.
//
// Dimensions must be ODD and non-zero. That is what makes centre() exist as a
// fact rather than a convention argument -- see the origin discussion above.
// An even kernel has no middle tap, only two defensible candidates half a
// pixel apart, which is the same half-pixel that haunts the area convention.
class vc_kernel {
  public:
    // Throws invalid_argument if width or height is even or zero, or if
    // weights.size() != width * height.
    vc_kernel(vc::image_dim width,
              vc::image_dim height,
              kernel_weights weights);

    [[nodiscard]] vc::image_dim width() const noexcept {
        return width_;
    }
    [[nodiscard]] vc::image_dim height() const noexcept {
        return height_;
    }

    // Offset from the centre tap to the edge of support, in each axis. A 3x3
    // has radius (1,1) and taps at offsets -1..+1; a 1x5 has radius (0,2).
    // Guaranteed to be exact because the dimensions are odd.
    //
    // Returns kernel_offset, not image_dim, and the distinction is the §3.3b
    // rule applied rather than an inconsistency: a radius is not an EXTENT, it
    // is the largest legal tap OFFSET. It is only ever used as a bound on a
    // signed loop variable or added to one, so typing it unsigned would force
    // a cast at every use -- and casts sprinkled through offset arithmetic are
    // exactly how a negative value becomes a huge positive one.
    [[nodiscard]] kernel_offset radius_x() const noexcept {
        return static_cast<kernel_offset>(width_ / 2);
    }
    [[nodiscard]] kernel_offset radius_y() const noexcept {
        return static_cast<kernel_offset>(height_ / 2);
    }

    // Weight at an offset from the CENTRE. kx runs -radius_x()..+radius_x().
    // Throws if the offset is outside the kernel's support -- a finite kernel
    // is mathematically zero outside it, but reaching there from a loop is a
    // bug in the loop, not a query worth answering.
    [[nodiscard]] float at(kernel_offset kx, kernel_offset ky) const;

    // Row-major weights as given, for callers that want to walk them directly.
    [[nodiscard]] const kernel_weights& weights() const noexcept {
        return weights_;
    }

    // The kernel rotated 180 degrees. This is the ENTIRE difference between
    // convolution and correlation, isolated as one named operation so it can
    // be tested on its own rather than living inside a sign in a loop.
    [[nodiscard]] vc_kernel reversed() const;

    // Sum of all weights. A filter that preserves average brightness sums to
    // 1; a derivative filter sums to 0. Worth asserting in tests -- it catches
    // a mis-built kernel before it reaches an image.
    [[nodiscard]] float sum() const noexcept;

  private:
    vc::image_dim width_;
    vc::image_dim height_;
    kernel_weights weights_;
};

// ---- kernel factories -------------------------------------------------------
//
// Constructed from known 1-D factors, never detected. See the separability
// discussion above.

// Normalised box blur: every weight 1/(w*h). Odd dimensions.
[[nodiscard]] vc_kernel make_box(vc::image_dim width, vc::image_dim height);

// Sampled Gaussian, normalised so the weights sum to 1.
//
// sigma_x and sigma_y are INDEPENDENT on purpose. With sigma_x == sigma_y the
// two 1-D factors are identical, and a separable implementation that applies
// them to the wrong axes gives the right answer anyway -- the test cannot see
// the swap. Different widths make a transposed pass fail loudly. It is the
// same class of blindness as a row-major/column-major sweep that passes for
// both layouts.
//
// The radius is derived from sigma (a Gaussian is never truly finite, so the
// tail is truncated where it stops mattering) and forced odd.
[[nodiscard]] vc_kernel make_gaussian(float sigma_x, float sigma_y);

// The two 1-D factors of the Gaussian above, for the separable path. A row
// (width x 1) and a column (1 x height) respectively.
//
// Two names rather than one, even though the WEIGHTS are identical and only
// the orientation differs. The shape carries information the value cannot:
// convolve_separable() below rejects a row where a column belongs, which is
// what catches passing the same factor twice -- a mistake that would otherwise
// blur one axis twice and leave the other untouched, silently. Collapse these
// into a single make_gaussian_1d() and that check becomes unexpressible.
//
// The duplication that DID matter -- the tap computation -- is shared: both of
// these and make_gaussian() above are built from one private gaussian_taps()
// helper. Which also makes make_gaussian(sx, sy) exactly the outer product of
// make_gaussian_1d_x(sx) and make_gaussian_1d_y(sy), rather than approximately
// so: the 2-D normalising sum factorises exactly, S_2d = Sx * Sy, so the full
// and separable paths agree by construction and not by luck. The separability
// test asserts precisely that.
[[nodiscard]] vc_kernel make_gaussian_1d_x(float sigma);
[[nodiscard]] vc_kernel make_gaussian_1d_y(float sigma);

// ---- the operations ---------------------------------------------------------

// Convolve. g(i,j) = sum over taps of f(i-k, j-l) . h(k,l), with the kernel
// centred on (i,j) and out-of-range taps resolved by `policy`.
//
// Output geometry, channel count and dtype all follow the source: a
// neighbourhood operation filters a picture, it does not reshape or
// reinterpret it. Each channel is filtered independently. Input must be f32.
[[nodiscard]] vc::vc_image convolve(const vc::vc_image& src,
                                    const vc_kernel& kernel,
                                    vc_edge_policy policy);

// Correlate. Identical except for the sign of the tap offsets, which is
// exactly convolution with the kernel rotated 180 degrees -- so this IS that,
// rather than a parallel implementation that could drift from it.
//
// Provided because correlation is the operation that answers "does this
// neighbourhood look like that template", which is the literal core of block
// matching. Convolution is the one that models what a lens did to a scene.
[[nodiscard]] vc::vc_image correlate(const vc::vc_image& src,
                                     const vc_kernel& kernel,
                                     vc_edge_policy policy);

// Convolve by two 1-D passes: `kernel_x` along rows, then `kernel_y` down
// columns. Equivalent to convolve() with their outer product, at 2K multiplies
// per pixel instead of K*K.
//
// The caller supplies the factors; nothing here tests whether a given 2-D
// kernel is separable (see above -- that needs SVD, and P3 owns it).
//
// kernel_x must be 1 tap tall, kernel_y must be 1 tap wide. Enforced, because
// passing the same kernel twice is the obvious slip and it would silently
// blur twice in one axis.
//
// The two passes agree with the full 2-D path at the boundary too, for every
// policy, and the reason is worth stating because it is easy to assume
// otherwise: all four policies resolve x and y INDEPENDENTLY. Composing two
// per-axis redirections gives the same source pixel the full path's single
// 2-D redirection does, so the boundary is not a special case here.
//
// The one place it can break is a kernel wider than the image, where a
// reflected index reflects back out the far side and needs a second bounce.
// Not reachable with the small kernels this phase builds, but the reason
// radius-vs-dimension is worth a thought before assuming a single fold-back
// is enough.
[[nodiscard]] vc::vc_image convolve_separable(const vc::vc_image& src,
                                              const vc_kernel& kernel_x,
                                              const vc_kernel& kernel_y,
                                              vc_edge_policy policy);

} // namespace vc::pixelops
