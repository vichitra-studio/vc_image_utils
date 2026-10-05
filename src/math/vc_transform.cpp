// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_transform.h"
#include "vc/core/vc_scalar.h" // vc::float32 -- leaf header

// rotate() uses std::cos/std::sin. Included directly rather than relied on
// transitively — it arrives today through another header, and that is exactly
// the kind of dependency an unrelated refactor upstream silently breaks.
#include <cmath>

namespace vc::math {

vc_mat3 translate(vc::float32 tx, vc::float32 ty) {
    vc_mat3 result = vc_mat3::identity();
    result.m[0][2] = tx;
    result.m[1][2] = ty;
    return result;
}

vc_mat3 scale(vc::float32 sx, vc::float32 sy) {
    vc_mat3 result = vc_mat3::identity();
    result.m[0][0] = sx;
    result.m[1][1] = sy;
    return result;
}

vc_mat3 rotate(vc::float32 radians) {
    vc_mat3 result = vc_mat3::identity();
    result.m[0][0] = std::cos(radians);
    result.m[0][1] = -std::sin(radians);
    result.m[1][0] = std::sin(radians);
    result.m[1][1] = std::cos(radians);
    return result;
}

vc_mat3 rotate_about(vc::float32 cx, vc::float32 cy, vc::float32 radians) {
    return translate(cx, cy) * (rotate(radians) * translate(-cx, -cy));
}

vc_mat3
scale_about(vc::float32 cx, vc::float32 cy, vc::float32 sx, vc::float32 sy) {
    return translate(cx, cy) * (scale(sx, sy) * translate(-cx, -cy));
}

vc_vec2 transform_point(const vc_mat3& m, const vc_vec2& p) {
    vc_vec3 embedded{.x = p.x, .y = p.y, .z = 1.0F};
    vc_vec3 transformed = m * embedded;
    return vc_vec2{.x = transformed.x, .y = transformed.y};
}

} // namespace vc::math
