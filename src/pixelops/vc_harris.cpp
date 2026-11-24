// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_harris.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/pixelops/vc_convolve.h"
#include "vc/pixelops/vc_gradients.h"

// ============================================================================
// REVIEW RECORD -- defects found after the first green-ish run, and why each
// one is instructive. The implementations are the learner's.
// ============================================================================
//
// DEFECT 1 -- corner_response COMPUTED THE CHANNEL COLLAPSE AND THEN IGNORED
// IT. `m_collapsed` was built and every read below it still went to m.a/m.b/
// m.c, with the writer sized from m.a.channels(). So the collapse was dead
// code and an RGB tensor came out of corner_response with three channels,
// which corner_peaks then rejected -- a DEAD END for colour, arriving as a
// throw from the wrong function. THE COMPILER CANNOT WARN: vc_structure_tensor
// holds vc_images with non-trivial destructors, so constructing one counts as
// a side effect and -Wunused-variable stays quiet.
//
// The half-fix is worse than the bug and worth naming. Sizing the writer at
// 1 channel while still READING m.a indexes a 3-channel interleaved buffer as
// if it were 1-channel, so it walks channels where it means to walk pixels.
// That stays in bounds, so AddressSanitizer is silent and the answer is just
// wrong. All four references move together or none of them do.
//
// DEFECT 2 -- collapse_channels DIVIDED BY A HARD-CODED 3.0F with the channel
// count already in scope on the line above. Correct for RGB, 4/3 too large
// for RGBA, and invisible to every fixture in the suite because they are all
// 1- or 3-channel. Same shape as the escape that MUT=23 found: a constant
// that happens to agree with the test data.
//
// DEFECT 3 -- std::invalid_argument AT SIX THROW SITES across three
// functions, where the library's
// convention is vc_exception{vc_error_code::invalid_argument}. It cost 11
// gates, because the suite's rejects() helper counts only the latter. The
// interesting part is that structure_tensor and corner_peaks had it RIGHT, so
// this was inconsistency inside one file rather than a misreading -- which is
// exactly the kind of thing a shared validator prevents by construction.
//
// DEFECT 4 -- THE THREE-WAY GEOMETRY CHECK WAS WRITTEN TWICE, verbatim, in
// collapse_channels and corner_response. require_tensor_triple() below is now
// the only copy, and folding it also fixed defect 3 in one place instead of
// three. It checks dtype EXPLICITLY rather than letting with_pixels<buf_f32>
// throw from vc_pixel_buffer::as<T>() -- that would raise the right error
// code with the wrong function's name on it, which is DEFECT 2 of
// vc_gradients.cpp arriving from a new direction.
//
// NOT A DEFECT, AND DELIBERATELY LEFT ALONE: corner_peaks allocates a full
// width x height float image to carry what is really a predicate, then scans
// it. The semantics are correct -- `> threshold` and then `> 0.0F` agree with
// the contract, and the "exactly ON the threshold is not a peak" gate passes
// -- and the only fragility is that the zeroed buffer conflates "below
// threshold" with "response is zero", which stays safe because a negative
// threshold is rejected. It is a memory point, not a correctness one, and
// rewriting working code during a review of it is how regressions arrive.
//
// DEFECT 5 -- corner_peaks CARRIED A DEAD BOUNDS CHECK on the 3x3 neighbour
// sweep. The loops run x in [1, width-2] and dx in [-1, 1], so a neighbour is
// always inside the image; the guard could never fire, and the int casts it
// needed produced two -Wsign-conversion warnings. Deleting the check removed
// the casts and the warnings together.

namespace vc::pixelops {

namespace {

// THE ONE VALIDATOR FOR A HAND-BUILT TENSOR TRIPLE. vc_structure_tensor is an
// aggregate, so a/b/c arrive with no guarantee they came from one
// structure_tensor() call -- see the contract in vc_harris.h. Dtype is checked
// EXPLICITLY and SEPARATELY from agreement, because three images that agree
// with each other and are all u8 pass an agreement test and are still wrong.
void require_tensor_triple(const vc_structure_tensor& m, const char* who) {
    const std::string prefix = std::string(who) + ": ";

    if (m.a.width() != m.b.width() || m.a.width() != m.c.width() ||
        m.a.height() != m.b.height() || m.a.height() != m.c.height() ||
        m.a.channels() != m.b.channels() || m.a.channels() != m.c.channels()) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               prefix + "a, b and c must agree in dimensions "
                                        "and channel count");
    }

    if (m.a.pixels()->dtype() != vc::pixel_dtype::f32 ||
        m.b.pixels()->dtype() != vc::pixel_dtype::f32 ||
        m.c.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               prefix +
                                   "a, b and c must all be of type buf_f32");
    }
}

vc_structure_tensor tensor_from_gradients(const vc::vc_image& dx,
                                          const vc::vc_image& dy,
                                          float window_sigma,
                                          vc_edge_policy policy) {
    if (dx.width() != dy.width() || dx.height() != dy.height() ||
        dx.channels() != dy.channels() ||
        dx.pixels()->dtype() != dy.pixels()->dtype()) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "structure_tensor: dx and dy must have the "
                               "same dimensions, channels, and dtype");
    }

    if (dx.pixels()->dtype() != vc::pixel_dtype::f32 ||
        dy.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "structure_tensor: dx and dy must be of type buf_f32");
    }

    vc_image_writer A_(dx.width(), dx.height(), dx.channels(),
                       vc::buf_f32{0.0F});
    vc_image_writer B_(dx.width(), dx.height(), dx.channels(),
                       vc::buf_f32{0.0F});
    vc_image_writer C_(dx.width(), dx.height(), dx.channels(),
                       vc::buf_f32{0.0F});
    dx.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_dx) {
        dy.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_dy) {
            A_.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px_A) {
                B_.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px_B) {
                    C_.with_pixels<vc::buf_f32>(
                        [&](std::span<vc::buf_f32> px_C) {
                            for (std::size_t i = 0; i < px_A.size(); ++i) {
                                const vc::float32 dx_i = px_dx[i];
                                const vc::float32 dy_i = px_dy[i];
                                px_A[i] = dx_i * dx_i;
                                px_B[i] = dy_i * dy_i;
                                px_C[i] = dx_i * dy_i;
                            }
                        });
                });
            });
        });
    });

    // TODO (one copy; the duplicate in structure_tensor was removed, and the
    // question is booked in docs/follow_up_p4.txt): a gaussian_blur() helper
    // in vc_convolve that takes a sigma and wraps these two factory calls
    // plus convolve_separable. Four call sites in src/ today, which is the
    // bar operator* set for earning a name. Extracting the Gaussian into its
    // own file is the larger version of the same question.
    vc_kernel window_kernel_x = make_gaussian_1d_x(window_sigma);
    vc_kernel window_kernel_y = make_gaussian_1d_y(window_sigma);

    vc_image A = convolve_separable(std::move(A_).seal(), window_kernel_x,
                                    window_kernel_y, policy);
    vc_image B = convolve_separable(std::move(B_).seal(), window_kernel_x,
                                    window_kernel_y, policy);
    vc_image C = convolve_separable(std::move(C_).seal(), window_kernel_x,
                                    window_kernel_y, policy);
    return {.a = std::move(A), .b = std::move(B), .c = std::move(C)};
}

// THE COLOUR STRUCTURE TENSOR: one M per pixel for a vector-valued image,
// as the MEAN of the per-channel tensors. sum_c M_c is J^T J for the C x 2
// Jacobian (Di Zenzo 1986) -- the honest generalisation, not a merge policy --
// and the mean is that sum over C.
//
// MEAN RATHER THAN SUM, and the reason is testable. Mean is sum/C, a uniform
// scaling of M, so det and tr^2 both scale by 1/C^2 and R scales by 1/C^2
// UNIFORMLY: the ranking of pixels is identical either way. What the mean buys
// is that a grey image stored as C identical channels gives exactly the same R
// as the same image stored as one channel. With the sum it would be C^2 times
// larger -- 9x for RGB -- and corner_peaks takes an ABSOLUTE threshold, so a
// threshold tuned on greyscale would silently find nothing on colour.
//
// WHY THE REDUCTION HAPPENS HERE AND NOT ON R. R = det - k*tr^2 is NONLINEAR
// in M, so sum(R(M_c)) != R(sum(M_c)). det is the term that measures whether
// the summands span two different directions: at an edge in one channel M is
// rank 1 and det is exactly 0, so every channel reports "not a corner", and
// adding those verdicts cannot recover the fact that their null directions
// differed. A red horizontal edge crossing a green vertical edge IS a corner,
// and only tensor-level summation sees it. Same shape as trap 1, one level up:
// the nonlinear step goes last.
//
// It also commutes with the integration window -- summation and convolution
// are both linear -- so collapsing before the window would give the same
// answer for a third of the windowing cost. Not done here, because
// structure_tensor() owns the window. See docs/follow_up_p4.txt.
[[nodiscard]] vc_structure_tensor
collapse_channels(const vc_structure_tensor& m) {
    require_tensor_triple(m, "collapse_channels");

    if (m.a.channels() == 1) {
        return m;
    }

    vc_image_writer A_(m.a.width(), m.a.height(), 1, vc::buf_f32{0.0F});
    vc_image_writer B_(m.b.width(), m.b.height(), 1, vc::buf_f32{0.0F});
    vc_image_writer C_(m.c.width(), m.c.height(), 1, vc::buf_f32{0.0F});

    //TODO: what a fuckin ugly lookin code
    m.a.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_A) {
        m.b.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> px_B) {
            m.c.with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> px_C) {
                    A_.with_pixels<vc::buf_f32>(
                        [&](std::span<vc::buf_f32> px_A_) {
                            B_.with_pixels<vc::buf_f32>(
                                [&](std::span<vc::buf_f32> px_B_) {
                                    C_.with_pixels<vc::buf_f32>(
                                        [&](std::span<vc::buf_f32> px_C_) {
                                            for (std::size_t i = 0;
                                                 i < px_A_.size(); ++i) {
                                                const std::size_t ch =
                                                    m.a.channels();
                                                float sum_A = 0.0F;
                                                float sum_B = 0.0F;
                                                float sum_C = 0.0F;
                                                for (std::size_t c = 0; c < ch;
                                                     ++c) {
                                                    sum_A += px_A[i * ch + c];
                                                    sum_B += px_B[i * ch + c];
                                                    sum_C += px_C[i * ch + c];
                                                }
                                                // DIVIDE BY ch, NOT BY 3:
                                                // a hard-coded 3 is right
                                                // for RGB and 4/3 too large
                                                // for RGBA, and no 1- or
                                                // 3-channel fixture can
                                                // tell the difference.
                                                const auto denom =
                                                    static_cast<float>(ch);
                                                px_A_[i] = sum_A / denom;
                                                px_B_[i] = sum_B / denom;
                                                px_C_[i] = sum_C / denom;
                                            }
                                        });
                                });
                        });
                });
        });
    });

    return {.a = std::move(A_).seal(),
            .b = std::move(B_).seal(),
            .c = std::move(C_).seal()};
}

} // namespace

vc_structure_tensor structure_tensor(const vc::vc_image& src,
                                     float sigma_d,
                                     float window_sigma,
                                     vc_edge_policy policy) {
    if (src.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "structure_tensor: src must be of type buf_f32");
    }

    if (!std::isfinite(sigma_d) || sigma_d <= 0.0F) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "structure_tensor: sigma_d must be finite and > 0");
    }

    if (!std::isfinite(window_sigma) || window_sigma <= 0.0F) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "structure_tensor: window_sigma must be finite and > 0");
    }

    vc_kernel blur_x = make_gaussian_1d_x(sigma_d);
    vc_kernel blur_y = make_gaussian_1d_y(sigma_d);
    vc_kernel deriv_x = make_sobel_derivative_1d_x();
    vc_kernel deriv_y = make_sobel_derivative_1d_y();

    vc_image blurred = convolve_separable(src, blur_x, blur_y, policy);
    vc_image dx = convolve(blurred, deriv_x, policy);
    vc_image dy = convolve(blurred, deriv_y, policy);

    return tensor_from_gradients(dx, dy, window_sigma, policy);
}

vc::vc_image corner_response(const vc_structure_tensor& m, float k) {
    if (k <= 0.0F || k >= 0.25F || !std::isfinite(k)) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "corner_response: k must be finite and in (0, 0.25)");
    }

    require_tensor_triple(m, "corner_response");

    // R IS ALWAYS SINGLE-CHANNEL, because a corner is a property of the scene
    // and not of a colour plane. collapse_channels() is the identity on a
    // 1-channel tensor, so this one line covers both cases.
    //
    // EVERY READ BELOW MUST COME FROM m_collapsed, NOT m. Mixing them is the
    // defect-1 half-fix: a 1-channel writer fed from a 3-channel interleaved
    // buffer walks channels where it means to walk pixels, stays in bounds,
    // and is silently wrong.
    const vc_structure_tensor m_collapsed = collapse_channels(m);

    vc_image_writer R(m_collapsed.a.width(), m_collapsed.a.height(),
                      m_collapsed.a.channels(), vc::buf_f32{0.0F});
    m_collapsed.a.with_pixels<vc::buf_f32>(
        [&](std::span<const vc::buf_f32> px_A) {
            m_collapsed.b.with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> px_B) {
                    m_collapsed.c.with_pixels<vc::buf_f32>(
                        [&](std::span<const vc::buf_f32> px_C) {
                            R.with_pixels<vc::buf_f32>(
                                [&](std::span<vc::buf_f32> px_R) {
                                    for (std::size_t i = 0; i < px_R.size();
                                         ++i) {
                                        const vc::float32 A_i = px_A[i];
                                        const vc::float32 B_i = px_B[i];
                                        const vc::float32 C_i = px_C[i];
                                        const vc::float32 det =
                                            A_i * B_i - C_i * C_i;
                                        const vc::float32 trace = A_i + B_i;
                                        px_R[i] = det - k * trace * trace;
                                    }
                                });
                        });
                });
        });

    return std::move(R).seal();
}

std::vector<vc_corner> corner_peaks(const vc::vc_image& response,
                                    float threshold) {

    if (response.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "corner_peaks: response must be of type buf_f32");
    }

    if (response.channels() != 1) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "corner_peaks: response must be single-channel");
    }

    if (!std::isfinite(threshold) || threshold < 0.0F) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "corner_peaks: threshold must be finite");
    }

    vc_image_writer response_filtered(response.width(), response.height(), 1,
                                      vc::buf_f32{0.0F});
    response.with_pixels<vc::buf_f32>(
        [&](std::span<const vc::buf_f32> px_response) {
            response_filtered.with_pixels<vc::buf_f32>(
                [&](std::span<vc::buf_f32> px_filtered) {
                    for (std::size_t i = 0; i < px_response.size(); ++i) {
                        if (px_response[i] > threshold) {
                            px_filtered[i] = px_response[i];
                        }
                    }
                });
        });

    std::vector<vc_corner> corners;
    response_filtered.with_pixels<vc::buf_f32>(
        [&](std::span<const vc::buf_f32> px_filtered) {
            for (vc::image_dim y = 1; y + 1 < response_filtered.height(); ++y) {
                for (vc::image_dim x = 1; x + 1 < response_filtered.width();
                     ++x) {
                    const std::size_t idx =
                        response_filtered.meta().index(x, y, 0);
                    const float center_value = px_filtered[idx];
                    if (center_value > 0.0F) {
                        bool is_peak = true;
                        // THE NEIGHBOUR SWEEP CARRIES NO SIGNED OFFSETS AND
                        // NO BOUNDS CHECK, and both of those are deliberate.
                        // The x/y loops run [1, w-2] and [1, h-2], so x-1 never
                        // underflows and x+1 never passes the last column: a
                        // neighbour is always inside the image. Written with
                        // `int dx = -1` offsets instead, every index() argument
                        // needs a cast back to the unsigned image_dim and the
                        // file carries two -Wsign-conversion warnings; written
                        // over the neighbour COORDINATES there are no casts and
                        // no mixed signedness anywhere. The bounds check that
                        // used to sit here could never fire.
                        for (vc::image_dim ny = y - 1; ny <= y + 1 && is_peak;
                             ++ny) {
                            for (vc::image_dim nx = x - 1;
                                 nx <= x + 1 && is_peak; ++nx) {
                                if (nx == x && ny == y) {
                                    continue;
                                }
                                const std::size_t neighbor_idx =
                                    response_filtered.meta().index(nx, ny, 0);
                                if (px_filtered[neighbor_idx] >= center_value) {
                                    is_peak = false;
                                }
                            }
                        }
                        if (is_peak) {
                            corners.push_back({x, y, center_value});
                        }
                    }
                }
            }
        });
    return corners;
}

} // namespace vc::pixelops
