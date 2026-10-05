// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef> // std::size_t -- the only include this leaf may ever need

namespace vc {

// The library's LEAF PRIMITIVE ALIASES, declared ONCE each.
//
// Originally this header held only the real-number precision. `element_count`
// joined it on 2026-10-05 because coding_guidelines.md Sec 3 forbids raw
// primitives in public API signatures and names `std::size_t` explicitly, and
// there was no alias for it anywhere in the library -- 66 raw uses. It belongs
// HERE rather than in vc_types.h because vc::math must be able to name a
// buffer length without including image vocabulary; see the leaf invariant
// below, which is the whole reason this file exists.
//
// ---- the precision ----
//
// The library's real-number precision, declared ONCE.
//
// Everything that stores a real quantity -- pixel samples, geometry, spectrum
// coefficients -- is this type. Changing it here changes it everywhere, which
// is the entire point: before this header existed the precision was written
// as a bare `float` in vc_pixel_buffer.h, vc_linalg.h and vc_dft.h
// independently, and nothing connected them. Three places that had to agree
// and no mechanism that made them.
//
// "real" as opposed to COMPLEX, not as opposed to integer -- the contrast that
// matters once vc_dft.h exists and needs std::complex<real32>.
//
// ---- WHY THIS IS A LEAF, AND MUST STAY ONE ----
//
// This header includes nothing and will include nothing. That is what lets
// vc::math include it without acquiring a dependency on image machinery --
// the invariant in coding_guidelines.md 2.1 that keeps vc_linalg's tests pure
// arithmetic. A scalar typedef is not image machinery; a pixel buffer is. If
// anything image-shaped is ever added below this line, the layering argument
// that put vc_dft.h in vc::math stops holding.
//
// ---- WHY buf_f32 IS STILL A SEPARATE NAME ----
//
// vc_pixel_buffer.h defines `buf_f32 = real32`, and that alias is NOT
// redundant. Its stated job (see the comment there) is to be greppable: a
// search for buf_f32 finds every use of this precision AS PIXEL DATA, and
// nothing else. A spectrum coefficient is the same precision but not pixel
// data, so vc_dft.h spells it real32 rather than buf_f32 deliberately. Same
// type, different role, and the two names keep the roles separable while this
// header keeps the type single.
using float32 = float;

// ---- counts ----
//
// How many elements a buffer, span or container holds, and the type an index
// into one is compared against. This is std::size_t and can never be anything
// else -- it is what .size() returns and what sizeof yields -- so unlike
// float32 the alias is not here to make the type CHANGEABLE. It is here to
// satisfy Sec 3's "every primitive has a named alias that encodes its semantic
// role", and to give the eventual retrofit of the other raw uses one target
// instead of several.
//
// NOT for dimensions. A width or a height is vc::image_dim (vc_types.h) or
// vc::math::grid_dim (vc_grid2d.h), both uint32_t, deliberately narrower so
// their PRODUCT cannot overflow a size_t. Using element_count for an extent
// would hand that hazard back -- see the overflow arithmetic in vc_grid2d.h.
using element_count = std::size_t;

} // namespace vc
