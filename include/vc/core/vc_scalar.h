// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

namespace vc {

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

} // namespace vc
