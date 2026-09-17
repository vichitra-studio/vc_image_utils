// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <span>

#include "vc/core/vc_image.h"
#include "vc/core/vc_types.h"

namespace vc::pixelops {

// What it means to read a pixel at a coordinate the image does not have.
//
// Every operation that looks at a pixel's NEIGHBOURS eventually asks for one
// that is not there -- bilinear sampling near an edge, a convolution kernel
// centred on the first column, a pyramid REDUCE at a corner. There is no
// correct answer, only a decided one, which is why this is a parameter and not
// a hidden constant.
//
// This file exists because that question is the SAME question for all of them.
// The enum began life inside vc_warp.h, which made vc_convolve.h include the
// warp header to get at it -- convolution does not depend on warping, it
// depends on a vocabulary that warping happened to declare first. Every future
// neighbourhood operation (Sobel at P3, demosaic at P5, denoise at P9b) would
// have inherited that wrong edge in the dependency graph.
//
// ---- THE AXES RESOLVE INDEPENDENTLY ----
//
// All four policies answer x and y separately, and that is load-bearing rather
// than incidental: it is exactly why a separable two-pass convolution matches
// a full 2-D one AT THE BOUNDARY and not merely in the interior. Composing two
// per-axis redirections lands on the same pixel that one 2-D redirection does.
// A policy that mixed the axes -- "if EITHER index is out of range, return 0"
// -- would break separability, quietly, and only at the borders.
//
// So the implementation resolves one axis at a time, via a private helper in
// the .cpp. That helper exists for one reason -- the same fold arithmetic runs
// on both axes, and writing it inline would mean two copies of `reflect`'s
// off-by-one-prone expression, which drift the first time one is fixed.
//
// It is NOT part of this interface, and the reason is worth recording because
// the first cut of this file exported it:
//
//     resolve_index(std::int64_t i, vc::image_dim n, vc_edge_policy)
//
// Two adjacent numeric parameters, neither naming an axis. Pass height() where
// width() belongs and it compiles, runs, and produces a wrong picture on any
// non-square image -- and a 1-D table test never holds a width and a height at
// once, so it is blind to precisely that.
//
// Being honest about what privacy bought: NOT immunity. fetch() still pairs x
// with width() and y with height() on two adjacent lines, and that pairing can
// still be got wrong. What changed is the blast radius -- from every present
// and future caller of a public function, down to two lines in one function,
// with a 7x3 fixture in the tests that can actually see the mistake. The
// encapsulation narrows the risk; the test is what catches it.
//
// The 1-D coverage is not lost either: a 5x1 image IS the x-axis table and a
// 1x5 image IS the y-axis table, so the same cases are still checked, through
// the interface that actually ships.

enum class vc_edge_policy : std::uint8_t {
    clamp, // clamp the coordinate to the image, replicating the edge pixel

    zero, // an out-of-range READ contributes 0, so a border fades rather than
          // ending hard. Applied PER TAP, never as one verdict on a whole
          // sample: at the right-hand edge x collapses while y may still have
          // both its neighbours and must still interpolate. A single upfront
          // bounds gate discards the y blend along with the x one, which
          // silently point-samples the entire last row and column.
          //
          // Note this is the one policy that does NOT preserve a uniform
          // image. A 3x3 box at a corner has four taps inside and five out, so
          // the applied weights sum to 4/9 and a flat 100 reads back as 44.4.
          // That deficit is the dark border everyone has seen.

    reflect, // mirror about the EDGE PIXEL: f(-1) = f(1), f(last+1) =
             // f(last-1). The edge pixel itself is NOT repeated. The other
             // convention mirrors about the half-pixel boundary instead --
             // f(-1) = f(0) -- which gives the edge pixel double weight and
             // makes reflect indistinguishable from clamp on a one-tap
             // overhang. (OpenCV ships both; this is its BORDER_REFLECT_101,
             // and its default for filtering.)
             //
             // Assumes ONE fold-back suffices, i.e. the reach never exceeds
             // the image dimension. True for a 2x2 bilinear window and for
             // every kernel this curriculum builds, but not true in general --
             // a 13-tap kernel on a 5-wide image reflects straight back out
             // the far side. The implementation must decide what happens
             // there -- loop the fold, or reject -- rather than leave it to
             // whatever the arithmetic happens to do.

    wrap, // modular: the image tiles the plane. f(-1) = f(last), f(last+1) =
          // f(0). Note C++'s % returns a NEGATIVE remainder for negative
          // operands, so the index must be normalised twice:
          // ((i % n) + n) % n.
          //
          // This is the policy that makes the convolution matrix CIRCULANT --
          // every row a rotation of the one above, with no ragged ends. That
          // matters because circulant matrices are diagonalised by the DFT,
          // which is the convolution theorem (P2 week 6) and the Poisson solve
          // (week 7). It is the only policy under which the spatial and
          // frequency paths compute the same thing, so it is the one that
          // makes the two cross-checkable.
};

// Read one element of `src` at a possibly-out-of-range integer coordinate,
// with the policy applied.
//
// This is the ONLY place a coordinate becomes a read, which is what makes the
// out-of-range problem structural rather than something to remember: no path
// reaches vc_image_info::index() without the policy having been consulted
// first.
//
// `px` is the caller's already-checked pixel span -- the dtype is verified
// once per operation via with_pixels<T>() rather than once per element. At a
// 3x3 kernel that is nine redundant checks per pixel per channel avoided; at
// 13x13, a hundred and sixty-eight.
//
// Shared by warp() (four neighbours around a fractional coordinate) and
// convolve() (a kernel footprint). Those callers differ in what they do with
// the values; this function does not know or care which is asking.
[[nodiscard]] float fetch(std::span<const vc::buf_f32> px,
                          const vc::vc_image& src,
                          std::int64_t x,
                          std::int64_t y,
                          vc::channel_count ch,
                          vc_edge_policy policy);

} // namespace vc::pixelops
