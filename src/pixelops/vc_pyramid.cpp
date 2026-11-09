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
// reduce() IS THE ONLY CALLER. expand() no longer convolves at all -- it carries
// the same taps pre-collapsed into the two per-parity stencils below, which is
// where the 0.5000/0.5000 split above is cashed in. Change these weights and
// expand_tap()'s must change with them; they are the same kernel twice.
const vc_kernel& burt_x() {
    static const vc_kernel k{
        5,
        1,
        {1.0F / 16.0F, 4.0F / 16.0F, 6.0F / 16.0F, 4.0F / 16.0F, 1.0F / 16.0F}};
    return k;
}

const vc_kernel& burt_y() {
    static const vc_kernel k{
        1,
        5,
        {1.0F / 16.0F, 4.0F / 16.0F, 6.0F / 16.0F, 4.0F / 16.0F, 1.0F / 16.0F}};
    return k;
}

// THE TWO 1-D PER-PARITY STENCILS. One copy, shared by both axes, for the same
// reason vc_edge_policy keeps a single resolve_index for x and y: written inline
// twice, the weights drift the first time one side is touched.
//
// `sample(k)` reads COARSE index k with the edge policy applied. `i` indexes the
// EXPANDED axis, and j = i/2 is the coarse index it sits on or between.
//
//     i EVEN  ->  ( c[j-1] + 6*c[j] + c[j+1] ) / 8
//     i ODD   ->  ( c[j]            + c[j+1] ) / 2
//
// WHERE THEY COME FROM -- they are not a different filter. Insert zeros, blur
// with [1,4,6,4,1]/16, and ask which taps can contribute. Centred on an EVEN
// output the pattern under the kernel is sample hole sample hole sample, so the
// two weight-4 taps multiply zeros. Centred on an ODD output it is hole sample
// hole sample hole, so the 1, 6, 1 taps do. Two or three of the five multiplies
// are guaranteed to be times zero at EVERY position. These are the same sums
// with those terms crossed out.
//
// BOTH NUMERATORS WEIGH 8, NOT 16, AND THAT 8 IS THE x2. The weight the dropped
// taps used to consume is simply absent from the divisor -- which is precisely
// what a constant scale factor was there to compensate for. So no scale factor
// is passed, stored, or available to get wrong, and the 2-D x4 is the two
// passes' two eights. The header records why that matters: collapse runs the
// same expand on both sides of a = (a-b)+b, so it cannot see a wrong one.
//
// AND THEY SAY WHAT EXPAND DOES, which "stuff zeros and blur" does not. An odd
// output is the plain AVERAGE of its two coarse neighbours. An even output keeps
// mostly its own coarse value, nudged by the two beside it.
//
// THE TRAP: swap the two branches and a flat image is still exactly flat, both
// stencils weighing 8, so neither the flatness check nor collapse can see it.
// Only the pinned ramp values in 12_pyramid.cpp group 6c can.
template <typename Sample>
float expand_tap(const Sample& sample, std::int64_t i) {
    const std::int64_t j = i / 2;
    if ((i & 1) == 0) {
        return (sample(j - 1) + 6.0F * sample(j) + sample(j + 1)) * 0.125F;
    }
    return (sample(j) + sample(j + 1)) * 0.5F;
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
//   3. expand              -- validate the target is 2n or 2n-1 per axis, then
//                             two passes of expand_tap(), X then Y, reading the
//                             COARSE grid through fetch(). No zero insertion, no
//                             mask, no scale factor. Collapse will NOT catch a
//                             wrong expand, so group 6c pins actual values.
//   4. gaussian_pyramid    -- level 0 is the source, then reduce repeatedly.
//   5. laplacian_pyramid   -- bands[l] = G[l] - expand(G[l+1] -> G[l]'s size),
//                             top = G[levels-1].
//   6. collapse            -- from `top` upward: G = bands[l] + expand(G).
//
// The header's traps, restated as the two most likely failures: expand() needs
// its target size passed in (it cannot double, because 3 could come from 5 or
// 6), and ANY systematically wrong expand passes collapse, because the same
// wrong expand runs on both sides of a = (a-b)+b. Under the per-parity stencils
// the live version of that second trap is swapping the even and odd branches --
// which a flat image also cannot see, since both weigh 8.

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

    const std::size_t nch = src.channels();
    const std::size_t bw = blurred.width();
    const std::size_t rw = reduced.width();
    const vc::image_dim rh = reduced.height();

    blurred.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> in) {
        reduced.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> out) {
            for (vc::image_dim y = 0; y < rh; ++y) {
                const std::size_t orow = static_cast<std::size_t>(y) * rw * nch;
                const std::size_t irow =
                    static_cast<std::size_t>(y) * 2U * bw * nch;
                for (vc::image_dim x = 0; x < rw; ++x) {
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
    // 32 bits and wraps for a width near 2^31.
    validate_source(src);
    const std::uint64_t doubled_w = std::uint64_t{src.width()} * 2U;
    const std::uint64_t doubled_h = std::uint64_t{src.height()} * 2U;
    if ((doubled_w != target_width && doubled_w - 1U != target_width) ||
        (doubled_h != target_height && doubled_h - 1U != target_height)) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "expand: target size must be double or double "
                               "minus one of source size");
    }

    // TWO SEPARABLE PASSES, the same shape as reduce() and convolve_separable,
    // and for the load-bearing reason vc_edge_policy.h records: the policy
    // resolves each axis INDEPENDENTLY, so composing a per-axis expansion lands
    // where one 2-D expansion would AT THE BORDER and not merely in the
    // interior. The alternative is four 2-D stencils, one per (x,y) parity pair
    // -- which is what darktable's local laplacian writes, slightly cheaper,
    // and four places for the same weights to drift apart.
    //
    // THE POLICY NOW APPLIES TO THE COARSE GRID -- to this operation's own
    // INPUT, which is what reduce() already does and what the enum is written
    // for. Every value an out-of-range read returns is a real sample at a real
    // position. There is no interleaved-zero grid for a policy to land on the
    // wrong parity of, because after expand_tap()'s derivation there are no
    // zeros to insert.
    const std::size_t nch = src.channels();

    // ---- PASS 1: X ONLY.  src -> target_width x src.height() ----
    vc_image_writer wide(target_width, src.height(), src.channels(),
                         vc::buf_f32{0.0F});
    const std::size_t ww = target_width;
    const vc::image_dim sh = src.height();
    src.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> in) {
        wide.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> out) {
            for (vc::image_dim y = 0; y < sh; ++y) {
                const std::size_t orow = static_cast<std::size_t>(y) * ww * nch;
                for (vc::image_dim x = 0; x < target_width; ++x) {
                    const std::size_t o =
                        orow + static_cast<std::size_t>(x) * nch;
                    for (std::size_t c = 0; c < nch; ++c) {
                        const auto ch = static_cast<vc::channel_count>(c);
                        // y is always in range here, so the policy only ever
                        // fires on the axis this pass is expanding.
                        out[o + c] = expand_tap(
                            [&](std::int64_t k) {
                                return fetch(in, src, k, y, ch, policy);
                            },
                            x);
                    }
                }
            }
        });
    });
    const vc::vc_image wide_image = std::move(wide).seal();

    // ---- PASS 2: Y ONLY.  that -> target_width x target_height ----
    //
    // Two buffers live at the peak, and the wider one is half the target area.
    // The mask formulation this replaced held five target-sized buffers at
    // once: samples, mask, both blurs, and the quotient.
    vc_image_writer tall(target_width, target_height, src.channels(),
                         vc::buf_f32{0.0F});
    wide_image.with_pixels<vc::buf_f32>([&](std::span<const vc::buf_f32> in) {
        tall.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> out) {
            for (vc::image_dim y = 0; y < target_height; ++y) {
                const std::size_t orow = static_cast<std::size_t>(y) * ww * nch;
                for (vc::image_dim x = 0; x < target_width; ++x) {
                    const std::size_t o =
                        orow + static_cast<std::size_t>(x) * nch;
                    for (std::size_t c = 0; c < nch; ++c) {
                        const auto ch = static_cast<vc::channel_count>(c);
                        out[o + c] = expand_tap(
                            [&](std::int64_t k) {
                                return fetch(in, wide_image, x, k, ch, policy);
                            },
                            y);
                    }
                }
            }
        });
    });

    return std::move(tall).seal();
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
