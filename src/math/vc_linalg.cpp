// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/math/vc_linalg.h"

#include <cmath>
#include <cstddef>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::math {

vc_vec2 operator*(const vc_mat2& a, const vc_vec2& v) {
    return vc_vec2{.x = a.m[0][0] * v.x + a.m[0][1] * v.y,
                   .y = a.m[1][0] * v.x + a.m[1][1] * v.y};
}

vc_mat2 operator*(const vc_mat2& a, const vc_mat2& b) {
    vc_mat2 result{};
    result.m[0][0] = a.m[0][0] * b.m[0][0] + a.m[0][1] * b.m[1][0];
    result.m[0][1] = a.m[0][0] * b.m[0][1] + a.m[0][1] * b.m[1][1];
    result.m[1][0] = a.m[1][0] * b.m[0][0] + a.m[1][1] * b.m[1][0];
    result.m[1][1] = a.m[1][0] * b.m[0][1] + a.m[1][1] * b.m[1][1];
    return result;
}

float determinant(const vc_mat2& a) {
    return a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0];
}

vc_mat2 inverse(const vc_mat2& a) {
    const float det = determinant(a);
    // A tolerance, deliberately NOT `det == 0.0f`. A mathematically singular
    // matrix usually computes a determinant NEAR zero rather than at it: rows
    // (0.1, 0.2) and (0.3, 0.6) collapse the plane onto a line, yet the
    // determinant lands around 8.9e-10 because 0.1f*0.6f and 0.2f*0.3f round
    // differently. An exact comparison admits those and hands back an
    // "inverse" with entries near 1e9, which destroys whatever image the
    // transform is applied to instead of failing here, where the cause is.
    if (std::fabs(det) < singular_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::inverse(vc_mat2): matrix is singular");
    }
    const float inv_det = 1.0f / det;
    vc_mat2 result{};
    result.m[0][0] = a.m[1][1] * inv_det;
    result.m[0][1] = -a.m[0][1] * inv_det;
    result.m[1][0] = -a.m[1][0] * inv_det;
    result.m[1][1] = a.m[0][0] * inv_det;
    return result;
}

vc_vec3 operator*(const vc_mat3& a, const vc_vec3& v) {
    vc_vec3 result{};
    result.x = a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z;
    result.y = a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z;
    result.z = a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z;
    return result;
}

vc_mat3 operator*(const vc_mat3& a, const vc_mat3& b) {
    vc_mat3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            result.m[row][col] = a.m[row][0] * b.m[0][col] +
                                 a.m[row][1] * b.m[1][col] +
                                 a.m[row][2] * b.m[2][col];
        }
    }
    return result;
}

float determinant(const vc_mat3& a) {
    return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
           a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
           a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
}

vc_mat3 inverse(const vc_mat3& a) {
    const float det = determinant(a);
    // Tolerance rather than an exact zero, for the reason given in the 2x2
    // case above.
    if (std::fabs(det) < singular_tolerance) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc::math::inverse(vc_mat3): matrix is singular");
    }
    const float inv_det = 1.0f / det;
    vc_mat3 result{};
    result.m[0][0] = (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) * inv_det;
    result.m[0][1] = -(a.m[0][1] * a.m[2][2] - a.m[0][2] * a.m[2][1]) * inv_det;
    result.m[0][2] = (a.m[0][1] * a.m[1][2] - a.m[0][2] * a.m[1][1]) * inv_det;
    result.m[1][0] = -(a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) * inv_det;
    result.m[1][1] = (a.m[0][0] * a.m[2][2] - a.m[0][2] * a.m[2][0]) * inv_det;
    result.m[1][2] = -(a.m[0][0] * a.m[1][2] - a.m[0][2] * a.m[1][0]) * inv_det;
    result.m[2][0] = (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]) * inv_det;
    result.m[2][1] = -(a.m[0][0] * a.m[2][1] - a.m[0][1] * a.m[2][0]) * inv_det;
    result.m[2][2] = (a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0]) * inv_det;
    return result;
}

} // namespace vc::math
