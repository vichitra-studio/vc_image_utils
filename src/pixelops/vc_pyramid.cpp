// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pixelops/vc_pyramid.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/pixelops/vc_convolve.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vc::pixelops {

namespace {

// Burt and Adelson's [1,4,6,4,1]/16, in both orientations because
// convolve_separable rejects a row where a column belongs -- which is what
// catches passing the same factor twice.
//
// PRIVATE, not a parameter and not public surface. The header has the two
// measured reasons: its taps split exactly 0.5000/0.5000 so expand() leaves no
// parity ripple, and sigma had exactly one correct value for a 2x reduction so
// the knob could only ever be turned wrong.
//
// Variance = (1.4 + 4.1 + 6.0 + 4.1 + 1.4)/16 = 16/16 = 1, so this IS the
// sigma=1 Gaussian to within truncation.
//
// [[maybe_unused]] only while the bodies below are stubs -- reduce() and
// expand() are the two callers, and the attribute comes off with the stubs.
[[maybe_unused]] const vc_kernel& burt_x() {
    static const vc_kernel k{
        5,
        1,
        {1.0F / 16.0F, 4.0F / 16.0F, 6.0F / 16.0F, 4.0F / 16.0F, 1.0F / 16.0F}};
    return k;
}

[[maybe_unused]] const vc_kernel& burt_y() {
    static const vc_kernel k{
        1,
        5,
        {1.0F / 16.0F, 4.0F / 16.0F, 6.0F / 16.0F, 4.0F / 16.0F, 1.0F / 16.0F}};
    return k;
}

// The f32 requirement, checked HERE rather than inherited from
// convolve_separable. Relying on that side effect left a hole: at levels == 1
// no reduce() runs, so nothing blurred and nothing rejected -- a u8 image came
// back as level 0 of a "Gaussian pyramid", to fail later in whatever first
// asked for buf_f32.
void validate_source(const vc::vc_image& src) {
    if (src.pixels()->dtype() != vc::pixel_dtype::f32) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "pyramid: src must be f32");
    }
}

void validate_levels(std::size_t levels, const vc::vc_image& src) {
    if (levels == 0) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "gaussian_pyramid: levels must be positive");
    }

    auto max_allowed_levels = max_pyramid_levels(src.width(), src.height());
    if (levels > max_allowed_levels) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "gaussian_pyramid: requested levels exceed max allowed");
    }
}

// A vc_laplacian_pyramid arrives from the CALLER, and its members are public --
// deliberately, because band-wise editing is the whole point of the Laplacian
// form, so they cannot be sealed behind a validating constructor the way
// grid2d and vc_image_info are. The invariant therefore has to be checked at
// the point of use.
//
// A channel-count mismatch between bands and top is otherwise a
// heap-buffer-overflow rather than a throw: expand() produces `current`'s
// channel count while the add loop strides by the BAND's, so it reads past the
// expanded buffer.
//
// The geometry chain is already caught by expand()'s own target validation; it
// is checked here as well so the diagnostic names the PYRAMID rather than
// complaining about an argument the caller never passed.
void validate_pyramid(const vc_laplacian_pyramid& pyramid) {
    const vc::channel_count channels = pyramid.top.channels();

    for (std::size_t i = 0; i < pyramid.bands.size(); ++i) {
        if (pyramid.bands[i].channels() != channels) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "collapse: band " + std::to_string(i) + " has " +
                    std::to_string(pyramid.bands[i].channels()) +
                    " channels but the top has " + std::to_string(channels) +
                    "; every level of one pyramid must agree");
        }
    }

    // Walk the chain coarse-to-fine: each band must be a legal expand target
    // from the level below it, i.e. 2n or 2n-1 per axis.
    for (std::size_t i = pyramid.bands.size(); i > 0; --i) {
        const vc::vc_image& finer = pyramid.bands[i - 1];
        const vc::image_dim cw = (i == pyramid.bands.size())
                                     ? pyramid.top.width()
                                     : pyramid.bands[i].width();
        const vc::image_dim chh = (i == pyramid.bands.size())
                                      ? pyramid.top.height()
                                      : pyramid.bands[i].height();
        const std::uint64_t dw = std::uint64_t{cw} * 2U;
        const std::uint64_t dh = std::uint64_t{chh} * 2U;
        if ((dw != finer.width() && dw - 1U != finer.width()) ||
            (dh != finer.height() && dh - 1U != finer.height())) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "collapse: band " + std::to_string(i - 1) + " is " +
                    std::to_string(finer.width()) + "x" +
                    std::to_string(finer.height()) +
                    ", which is not reachable by expanding the " +
                    std::to_string(cw) + "x" + std::to_string(chh) +
                    " level below it");
        }
    }
}

} // namespace

// ---- RED. The five bodies are yours. ---------------------------------------
//
// 12_pyramid.cpp holds the acceptance checks. Suggested order, because each one
// unlocks the next:
//
//   1. max_pyramid_levels  -- pure arithmetic, no image. Checks 1-2.
//   2. reduce              -- convolve_separable with burt_x()/burt_y() above,
//                             then keep even indices. Output
//                             ceil(w/2) x ceil(h/2).
//   3. expand              -- validate the target is 2n or 2n-1 per axis; write
//                             src into every other pixel of a target-sized
//                             image, zeros elsewhere; ALSO build a 1/0 mask the
//                             same way; convolve_separable BOTH; divide.
//                             Do not multiply by a constant 4 -- the header has
//                             the measurements. Collapse will NOT catch this.
//   4. gaussian_pyramid    -- level 0 is the source, then reduce repeatedly.
//   5. laplacian_pyramid   -- bands[l] = G[l] - expand(G[l+1] -> G[l]'s size),
//                             top = G[levels-1].
//   6. collapse            -- from `top` upward: G = bands[l] + expand(G).
//
// The header's traps, restated as the two most likely failures: expand() needs
// its target size passed in (it cannot double, because 3 could come from 5 or
// 6), and a missing x4 passes collapse while halving every level, because the
// same wrong expand runs on both sides of a = (a-b)+b.

std::size_t max_pyramid_levels(vc::image_dim width, vc::image_dim height) {
    std::size_t levels = 1;
    while (width > 1 || height > 1) {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        ++levels;
    }
    return levels;
}

vc::vc_image reduce(const vc::vc_image& src, vc_edge_policy policy) {
    validate_source(src);
    const auto& kernel_x = burt_x();
    const auto& kernel_y = burt_y();
    vc::vc_image blurred = convolve_separable(src, kernel_x, kernel_y, policy);
    vc::vc_image_writer reduced((src.width() + 1) / 2, (src.height() + 1) / 2,
                                src.channels(), vc::buf_f32{0.0F});
    // ---- THE PIXEL-LOOP IDIOM USED THROUGHOUT THIS FILE ----
    //
    // with_pixels<T>() on both sides, with the row offset hoisted into a
    // std::size_t once per row.
    //
    // with_pixels rather than at<>() because vc_image.h names it the preferred
    // accessor for a loop: at<>() calls as<T>() per element, which re-checks the
    // variant and materialises a fresh span every time.
    //
    // Hoisted because vc_image is INTERLEAVED, so the offset is
    // (y*width + x)*channels + ch -- and written inline that is image_dim
    // (uint32_t) arithmetic, which wraps for a large image. Widening once per
    // row fixes that and also lifts both multiplies out of the innermost loop,
    // leaving a single add.
    const std::size_t nch = src.channels();
    const std::size_t rw = reduced.width();
    const std::size_t bw = blurred.width();
    const vc::image_dim rh = reduced.height();
    blurred.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> in) {
        reduced.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> out) {
            for (vc::image_dim y = 0; y < rh; ++y) {
                const std::size_t orow = static_cast<std::size_t>(y) * rw * nch;
                const std::size_t irow =
                    static_cast<std::size_t>(y) * 2U * bw * nch;
                for (vc::image_dim x = 0; x < reduced.width(); ++x) {
                    const std::size_t o =
                        orow + static_cast<std::size_t>(x) * nch;
                    const std::size_t i =
                        irow + static_cast<std::size_t>(x) * 2U * nch;
                    for (std::size_t c = 0; c < nch; ++c) {
                        out[o + c] = in[i + c];
                    }
                }
            }
        });
    });
    return std::move(reduced).seal();
}

vc::vc_image expand(const vc::vc_image& src,
                    vc::image_dim target_width,
                    vc::image_dim target_height,
                    vc_edge_policy policy) {
    // Widened. src.width() is image_dim (uint32_t), so `* 2` was evaluated in
    // 32 bits and wraps for a width near 2^31 -- the same class as the index
    // arithmetic below, and the last site of it in this file.
    validate_source(src);
    const std::uint64_t doubled_w = std::uint64_t{src.width()} * 2U;
    const std::uint64_t doubled_h = std::uint64_t{src.height()} * 2U;
    if ((doubled_w != target_width && doubled_w - 1U != target_width) ||
        (doubled_h != target_height && doubled_h - 1U != target_height)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "expand: target size must be double or double "
                               "minus one of source size");
    }

    const auto& kernel_x = burt_x();
    const auto& kernel_y = burt_y();

    vc_image_writer expanded(target_width, target_height, src.channels(),
                             vc::buf_f32{0.0F});
    vc_image_writer mask(target_width, target_height, 1, vc::buf_f32{0.0F});
    // SCATTER AND MASK IN ONE LOOP. They mark the SAME positions, so separate
    // loops can disagree about which -- keeping them together makes that
    // impossible, leaving only the arithmetic for one of them to get wrong.
    //
    // No bounds guard on the writes: the validation above admits only 2n and
    // 2n-1, and the tight case is 2n-1, where the last sample lands at 2n-2 =
    // target-1, exactly the final slot. If that validation were ever loosened,
    // a guard that silently skipped would drop a sample instead of failing.
    const std::size_t nch = src.channels();
    const std::size_t tw = target_width;
    const std::size_t sw = src.width();
    const vc::image_dim sh = src.height();
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> in) {
        expanded.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> out) {
            mask.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> msk) {
                for (vc::image_dim y = 0; y < sh; ++y) {
                    const std::size_t irow =
                        static_cast<std::size_t>(y) * sw * nch;
                    const std::size_t orow =
                        static_cast<std::size_t>(y) * 2U * tw * nch;
                    const std::size_t mrow =
                        static_cast<std::size_t>(y) * 2U * tw;
                    for (vc::image_dim x = 0; x < src.width(); ++x) {
                        const std::size_t i =
                            irow + static_cast<std::size_t>(x) * nch;
                        const std::size_t o =
                            orow + static_cast<std::size_t>(x) * 2U * nch;
                        msk[mrow + static_cast<std::size_t>(x) * 2U] = 1.0F;
                        for (std::size_t c = 0; c < nch; ++c) {
                            out[o + c] = in[i + c];
                        }
                    }
                }
            });
        });
    });
    // Five target-sized buffers are live here: samples, mask, both blurs, and
    // the result. The mask pair is the reducible part -- the blurred mask is an
    // OUTER PRODUCT of two 1-D arrays and depends only on geometry, so it could
    // be W+H instead of W*H and cached per level shape. Not done; see the
    // header's cost note.
    auto const_expanded = std::move(expanded).seal();
    auto blurred_expanded =
        convolve_separable(const_expanded, kernel_x, kernel_y, policy);
    auto const_mask = std::move(mask).seal();
    auto blurred_mask =
        convolve_separable(const_mask, kernel_x, kernel_y, policy);

    vc_image_writer final_expanded(target_width, target_height, src.channels(),
                                   vc::buf_f32{0.0F});
    blurred_expanded.with_pixels<vc::buf_f32>(
        [&](std::span<const vc::buf_f32> num) {
            blurred_mask.with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> den) {
                    final_expanded.with_pixels<vc::buf_f32>(
                        [&](std::span<vc::buf_f32> out) {
                            for (vc::image_dim y = 0; y < target_height; ++y) {
                                const std::size_t row =
                                    static_cast<std::size_t>(y) * tw * nch;
                                const std::size_t mrow =
                                    static_cast<std::size_t>(y) * tw;
                                for (vc::image_dim x = 0; x < target_width;
                                     ++x) {
                                    // The guard stays, epsilon-based, returning 0: no real sample
                                    // influenced this pixel, so the numerator is zero too. Without
                                    // the epsilon a denormal denominator would divide and amplify.
                                    const float weight =
                                        den[mrow + static_cast<std::size_t>(x)];
                                    const std::size_t o =
                                        row + static_cast<std::size_t>(x) * nch;
                                    for (std::size_t c = 0; c < nch; ++c) {
                                        out[o + c] = weight > 1e-12F
                                                         ? num[o + c] / weight
                                                         : 0.0F;
                                    }
                                }
                            }
                        });
                });
        });

    return std::move(final_expanded).seal();
}

vc_pyramid gaussian_pyramid(const vc::vc_image& src,
                            std::size_t levels,
                            vc_edge_policy policy) {
    validate_source(src);
    validate_levels(levels, src);

    vc_pyramid pyramid;
    for (std::size_t i = 0; i < levels; ++i) {
        if (i == 0) {
            pyramid.push_back(src);
        } else {
            vc::vc_image reduced_image = reduce(pyramid.back(), policy);
            pyramid.push_back(reduced_image);
        }
    }
    return pyramid;
}

vc_laplacian_pyramid laplacian_pyramid(const vc::vc_image& src,
                                       std::size_t levels,
                                       vc_edge_policy policy) {
    validate_source(src);
    validate_levels(levels, src);
    // Builds the whole Gaussian pyramid first. It could stream -- only two
    // adjacent levels are ever needed at once -- at the cost of interleaving
    // the two constructions. Kept separate because the two pyramids are
    // separately useful and gaussian_pyramid is already tested on its own.
    vc_pyramid gaussian = gaussian_pyramid(src, levels, policy);
    vc_pyramid bands;
    for (std::size_t i = 0; i < levels - 1; ++i) {
        vc::vc_image expanded = expand(gaussian[i + 1], gaussian[i].width(),
                                       gaussian[i].height(), policy);
        vc_image_writer band(gaussian[i].width(), gaussian[i].height(),
                             gaussian[i].channels(), vc::buf_f32{0.0F});
        // Same geometry on all three, so ONE offset serves all of them.
        const std::size_t nch = gaussian[i].channels();
        const std::size_t w = gaussian[i].width();
        const vc::image_dim gh = gaussian[i].height();
        const vc::image_dim gw = gaussian[i].width();
        gaussian[i].with_pixels<vc::buf_f32>(
            [&](std::span<const vc::buf_f32> g) {
                expanded.with_pixels<vc::buf_f32>(
                    [&](std::span<const vc::buf_f32> e) {
                        band.with_pixels<vc::buf_f32>(
                            [&](std::span<vc::buf_f32> out) {
                                for (vc::image_dim y = 0; y < gh; ++y) {
                                    const std::size_t row =
                                        static_cast<std::size_t>(y) * w * nch;
                                    for (vc::image_dim x = 0; x < gw; ++x) {
                                        const std::size_t o =
                                            row +
                                            static_cast<std::size_t>(x) * nch;
                                        for (std::size_t c = 0; c < nch; ++c) {
                                            out[o + c] = g[o + c] - e[o + c];
                                        }
                                    }
                                }
                            });
                    });
            });
        bands.push_back(std::move(band).seal());
    }
    return {.bands = std::move(bands), .top = std::move(gaussian.back())};
}

vc::vc_image collapse(const vc_laplacian_pyramid& pyramid,
                      vc_edge_policy policy) {
    validate_pyramid(pyramid);
    vc_image current = pyramid.top;
    for (std::size_t i = pyramid.bands.size(); i > 0; --i) {
        vc::vc_image expanded = expand(current, pyramid.bands[i - 1].width(),
                                       pyramid.bands[i - 1].height(), policy);
        vc_image_writer collapsed(
            pyramid.bands[i - 1].width(), pyramid.bands[i - 1].height(),
            pyramid.bands[i - 1].channels(), vc::buf_f32{0.0F});
        const vc::vc_image& band = pyramid.bands[i - 1];
        const std::size_t nch = band.channels();
        const std::size_t w = band.width();
        const vc::image_dim bh = band.height();
        const vc::image_dim bw = band.width();
        band.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> b) {
            expanded.with_pixels<vc::buf_f32>(
                [&](std::span<const vc::buf_f32> e) {
                    collapsed.with_pixels<vc::buf_f32>(
                        [&](std::span<vc::buf_f32> out) {
                            for (vc::image_dim y = 0; y < bh; ++y) {
                                const std::size_t row =
                                    static_cast<std::size_t>(y) * w * nch;
                                for (vc::image_dim x = 0; x < bw; ++x) {
                                    const std::size_t o =
                                        row + static_cast<std::size_t>(x) * nch;
                                    for (std::size_t c = 0; c < nch; ++c) {
                                        out[o + c] = b[o + c] + e[o + c];
                                    }
                                }
                            }
                        });
                });
        });
        // Move-assignment frees the previous buffer before taking the new
        // one, so each round holds two levels and not the whole chain.
        current = std::move(collapsed).seal();
    }
    return current;
}

} // namespace vc::pixelops
