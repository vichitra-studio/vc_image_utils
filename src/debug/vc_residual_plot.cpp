// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/debug/vc_residual_plot.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace vc::debug {

namespace {

// ---- LAYOUT ----
//
// Margins are in pixels and fixed, not fractions of the canvas. A fraction
// would shrink the y-axis labels' gutter on a small plot and the labels are a
// fixed pixel size, so they would collide. The 64x64 minimum the header rejects
// below is what guarantees these leave a non-empty plot area.
constexpr vc::image_dim k_margin_left = 62;
constexpr vc::image_dim k_margin_right = 12;
constexpr vc::image_dim k_margin_top = 12;
constexpr vc::image_dim k_margin_bottom = 26;

// 5x7 cell plus one column of spacing.
constexpr int k_glyph_w = 5;
constexpr int k_glyph_h = 7;
constexpr int k_glyph_advance = 6;

struct rgb {
    float r;
    float g;
    float b;
};

constexpr rgb k_background{0.98F, 0.98F, 0.97F};
constexpr rgb k_grid{0.86F, 0.86F, 0.85F};
constexpr rgb k_axis{0.30F, 0.30F, 0.32F};
constexpr rgb k_text{0.18F, 0.18F, 0.20F};

// Eight hues, chosen to stay distinguishable when the PNG is viewed small.
// Series beyond the eighth wrap, which is why the legend carries the label as
// well as the swatch -- colour alone stops identifying a curve at nine.
constexpr std::array<rgb, 8> k_palette{{{0.85F, 0.20F, 0.20F},
                                        {0.20F, 0.45F, 0.90F},
                                        {0.15F, 0.62F, 0.30F},
                                        {0.92F, 0.60F, 0.10F},
                                        {0.58F, 0.30F, 0.80F},
                                        {0.10F, 0.68F, 0.70F},
                                        {0.80F, 0.38F, 0.60F},
                                        {0.48F, 0.48F, 0.18F}}};

// ---- A 5x7 UPPERCASE FONT ----
//
// Hand-encoded rather than linked, because the alternative is a font dependency
// for a debug artifact. Each glyph is seven rows; the low five bits of each row
// are the columns, left to right.
//
// UPPERCASE ONLY, and labels are upper-cased when drawn. A lowercase set would
// double the table for a plot nobody reads typographically. Characters outside
// the table render as blank, which is why the legend is a swatch PLUS a label
// rather than a label alone.
struct glyph {
    char code;
    std::array<std::uint8_t, k_glyph_h> rows;
};

constexpr std::array<glyph, 42> k_font{{
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'-', {0, 0, 0, 0x1F, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 0x0C, 0x0C}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {':', {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
}};

// ---- A CANVAS THAT CLIPS ----
//
// Every draw below goes through put(), which DISCARDS out-of-range writes
// rather than clamping them to the edge. Clamping would smear a line that ran
// off the plot into a solid border and make a bug look like a design choice;
// dropping it leaves a visible gap.
class canvas {
  public:
    canvas(vc::image_dim w, vc::image_dim h)
        : w_{w}, h_{h}, img_{w, h, 3, vc::buf_f32{0.0F}} {
    }

    void put(int x, int y, const rgb& c) {
        if (x < 0 || y < 0 || x >= static_cast<int>(w_) ||
            y >= static_cast<int>(h_)) {
            return;
        }
        const auto ux = static_cast<vc::image_dim>(x);
        const auto uy = static_cast<vc::image_dim>(y);
        img_.at<vc::buf_f32>(ux, uy, 0) = static_cast<vc::buf_f32>(c.r);
        img_.at<vc::buf_f32>(ux, uy, 1) = static_cast<vc::buf_f32>(c.g);
        img_.at<vc::buf_f32>(ux, uy, 2) = static_cast<vc::buf_f32>(c.b);
    }

    void fill(const rgb& c) {
        for (vc::image_dim y = 0; y < h_; ++y) {
            for (vc::image_dim x = 0; x < w_; ++x) {
                put(static_cast<int>(x), static_cast<int>(y), c);
            }
        }
    }

    void hline(int x0, int x1, int y, const rgb& c) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) {
            put(x, y, c);
        }
    }

    void vline(int x, int y0, int y1, const rgb& c) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) {
            put(x, y, c);
        }
    }

    // Bresenham, thickened by one pixel vertically. A one-pixel line across 900
    // columns and six series is genuinely hard to follow in a browser; two is
    // legible and still thin enough that crossings read as crossings.
    void line(int x0, int y0, int x1, int y1, const rgb& c) {
        int dx = std::abs(x1 - x0);
        int dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            put(x0, y0, c);
            put(x0, y0 + 1, c);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void text(int x, int y, const std::string& s, const rgb& c) {
        int pen = x;
        for (const char raw : s) {
            const char ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(raw)));
            const auto it =
                std::find_if(k_font.begin(), k_font.end(),
                             [ch](const glyph& g) { return g.code == ch; });
            if (it != k_font.end()) {
                for (int row = 0; row < k_glyph_h; ++row) {
                    for (int col = 0; col < k_glyph_w; ++col) {
                        const auto bit = static_cast<std::uint8_t>(
                            1U << static_cast<unsigned>(k_glyph_w - 1 - col));
                        if ((it->rows[static_cast<std::size_t>(row)] & bit) !=
                            0) {
                            put(pen + col, y + row, c);
                        }
                    }
                }
            }
            pen += k_glyph_advance;
        }
    }

    vc::vc_image release() {
        return std::move(img_).seal();
    }

  private:
    vc::image_dim w_;
    vc::image_dim h_;
    vc::vc_image_writer img_;
};

[[noreturn]] void reject(const std::string& why) {
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "residual_plot: " + why);
}

// Decade labels as "1E-6" rather than "0.000001": at eight decades the decimal
// form is both unreadable and wider than the left margin.
std::string decade_label(int exponent) {
    return "1E" + std::to_string(exponent);
}

// A linear axis tick. std::to_string on a float gives "0.750000", which is
// eleven glyphs where the left margin fits ten, so the ticks would overlap each
// other down the axis.
//
// Two decimals inside [0.01, 1000) and E-notation outside it. The documented
// linear use is a relative residual running 0..1, where two decimals is exactly
// right; the fallback is so a caller plotting something else gets a label
// rather than "0.00" six times.
std::string linear_label(double v) {
    std::array<char, 24> buf{};
    const double a = std::abs(v);
    if (a != 0.0 && (a < 0.01 || a >= 1000.0)) {
        (void)std::snprintf(buf.data(), buf.size(), "%.0E", v);
    } else {
        (void)std::snprintf(buf.data(), buf.size(), "%.2f", v);
    }
    return std::string{buf.data()};
}

} // namespace

vc::vc_image residual_plot(std::span<const residual_series> series,
                           const residual_plot_config& config) {
    // ---- VALIDATION, BEFORE ANY ALLOCATION ----
    //
    // Order matters only in that the cheapest and most likely caller mistakes
    // come first. Each message names the offending series by index, because
    // "a series with no values" is useless when six were passed.
    if (series.empty()) {
        reject("`series` is empty -- a blank plot is indistinguishable from a "
               "solver that produced nothing");
    }
    if (config.width < 64 || config.height < 64) {
        reject("canvas is " + std::to_string(config.width) + "x" +
               std::to_string(config.height) +
               "; below 64 on either axis the axes and legend do not fit and "
               "the result reads as a bug in the solver");
    }
    if (config.log_scale && !(config.floor > 0.0F)) {
        reject("floor must be > 0 under a log axis, but is " +
               std::to_string(config.floor));
    }
    for (std::size_t s = 0; s < series.size(); ++s) {
        if (series[s].values.empty()) {
            reject("series " + std::to_string(s) + " (\"" + series[s].label +
                   "\") has no values; a real run always has at least "
                   "residual[c][0]");
        }
        for (const auto v : series[s].values) {
            if (!std::isfinite(v)) {
                reject("series " + std::to_string(s) + " (\"" +
                       series[s].label +
                       "\") contains a non-finite value; that means the solver "
                       "diverged or divided by zero, and skipping the point "
                       "would hide the one run worth looking at");
            }
        }
    }

    // ---- RANGES ----
    //
    // X spans the LONGEST series. Shorter ones are drawn short, not extended:
    // channels converge at different sweeps, and red_black on a checkerboard
    // finishes in one, so inventing a trailing value would draw a flat line
    // where the run had simply stopped.
    std::size_t longest = 0;
    float top_value = config.floor;
    for (const auto& s : series) {
        longest = std::max(longest, s.values.size());
        for (const auto v : s.values) {
            top_value = std::max(top_value, v);
        }
    }

    const auto floor_v = std::max(config.floor, 0.0F);
    // A run from a zero start tops out at exactly 1.0, so this is normally
    // 1.0. The guard is for a caller plotting something else entirely.
    if (!(top_value > floor_v)) {
        top_value = floor_v * 10.0F;
    }

    const double lo = config.log_scale
                          ? std::log10(static_cast<double>(floor_v))
                          : static_cast<double>(floor_v);
    const double hi = config.log_scale
                          ? std::log10(static_cast<double>(top_value))
                          : static_cast<double>(top_value);
    const double span = (hi > lo) ? (hi - lo) : 1.0;

    const int left = static_cast<int>(k_margin_left);
    const int right = static_cast<int>(config.width - k_margin_right);
    const int top = static_cast<int>(k_margin_top);
    const int bottom = static_cast<int>(config.height - k_margin_bottom);

    // CLAMPED, NOT DROPPED. An all-zero channel reports exactly 0.0, which
    // poisson_iter documents and which has no place on a log axis. A line
    // pinned to the floor is visibly degenerate; a missing line is an
    // invisible failure.
    const auto to_y = [&](float v) {
        const auto clamped =
            static_cast<double>(std::clamp(v, floor_v, top_value));
        const double mapped = config.log_scale ? std::log10(clamped) : clamped;
        const double t = (mapped - lo) / span;
        return static_cast<int>(
            std::lround(static_cast<double>(bottom) -
                        (t * static_cast<double>(bottom - top))));
    };

    const auto to_x = [&](std::size_t i) {
        if (longest <= 1) {
            return left;
        }
        const double t =
            static_cast<double>(i) / static_cast<double>(longest - 1);
        return left + static_cast<int>(
                          std::lround(t * static_cast<double>(right - left)));
    };

    // ---- DRAW ----
    canvas c{config.width, config.height};
    c.fill(k_background);

    // ---- Y GRIDLINES AND LABELS, IN BOTH MODES ----
    //
    // ** A FIRST VERSION DREW THESE ONLY UNDER log_scale, SO A LINEAR PLOT CAME
    // OUT WITH AN ENTIRELY UNLABELLED Y AXIS. ** Nothing failed: the flag is
    // documented in the header as being there "so the choice is testable rather
    // than asserted", and no check exercised it, so the one mode whose whole
    // purpose is to be compared against the other rendered without a scale to
    // compare on.
    if (config.log_scale) {
        const int first = static_cast<int>(std::floor(lo));
        const int last = static_cast<int>(std::ceil(hi));
        for (int e = first; e <= last; ++e) {
            const auto v = static_cast<float>(std::pow(10.0, e));
            if (v < floor_v || v > top_value) {
                continue;
            }
            const int y = to_y(v);
            c.hline(left, right, y, k_grid);
            c.text(4, y - (k_glyph_h / 2), decade_label(e), k_text);
        }
    } else {
        // Five evenly spaced ticks. Not a "nice number" algorithm: the range is
        // whatever the data spans, and inventing rounded bounds would move the
        // top of the axis away from the 1.0 that a run from zero actually
        // starts at.
        constexpr int k_linear_ticks = 5;
        for (int t = 0; t <= k_linear_ticks; ++t) {
            const double frac =
                static_cast<double>(t) / static_cast<double>(k_linear_ticks);
            const double v = lo + (frac * (hi - lo));
            const int y = to_y(static_cast<float>(v));
            c.hline(left, right, y, k_grid);
            c.text(4, y - (k_glyph_h / 2), linear_label(v), k_text);
        }
    }

    c.hline(left, right, bottom, k_axis);
    c.vline(left, top, bottom, k_axis);

    // X ticks at the ends and the middle. More would need rotated labels to
    // avoid overlapping at four digits, and the exact sweep numbers are in the
    // suite's stdout; the axis only has to convey the SHAPE.
    for (const double frac : {0.0, 0.5, 1.0}) {
        const auto i = static_cast<std::size_t>(
            std::lround(frac * static_cast<double>(longest - 1)));
        const int x = to_x(i);
        c.vline(x, bottom, bottom + 3, k_axis);
        c.text(x - 8, bottom + 7, std::to_string(i), k_text);
    }
    c.text(((left + right) / 2) - 15, bottom + 15, "SWEEP", k_text);

    for (std::size_t s = 0; s < series.size(); ++s) {
        const auto& colour = k_palette[s % k_palette.size()];
        const auto& v = series[s].values;
        for (std::size_t i = 1; i < v.size(); ++i) {
            c.line(to_x(i - 1), to_y(v[i - 1]), to_x(i), to_y(v[i]), colour);
        }
        // A single-point series draws nothing above. Mark it, so "converged
        // before the first sweep" is visible rather than absent.
        if (v.size() == 1) {
            const int x = to_x(0);
            const int y = to_y(v[0]);
            for (int d = -2; d <= 2; ++d) {
                c.put(x + d, y, colour);
                c.put(x, y + d, colour);
            }
        }
    }

    // ---- LEGEND, LAST AND ON AN OPAQUE PANEL ----
    //
    // DRAWN AFTER THE CURVES, not interleaved with them. A first version drew
    // each legend row inside the series loop, so every curve plotted afterwards
    // ran straight over it -- and the worst offender was the one that matters
    // most, the flat jacobi stall at 1.0, which crosses the top-right corner
    // exactly where a legend wants to sit.
    //
    // The panel is sized from the LONGEST label rather than a fixed 150px. A
    // fixed width either wastes space or lets "CHECKERBOARD/GAUSS_SEIDEL" run
    // off the canvas, which is what the first version did.
    {
        std::size_t longest_label = 0;
        for (const auto& s : series) {
            longest_label = std::max(longest_label, s.label.size());
        }
        const int text_w = static_cast<int>(longest_label) * k_glyph_advance;
        const int panel_w = 15 + text_w + 8;
        const int row_h = k_glyph_h + 4;
        const int panel_h = (static_cast<int>(series.size()) * row_h) + 6;
        const int px = std::max(left + 2, right - panel_w - 2);
        const int py = top + 2;

        for (int y = py; y < py + panel_h; ++y) {
            c.hline(px, px + panel_w, y, k_background);
        }
        c.hline(px, px + panel_w, py, k_grid);
        c.hline(px, px + panel_w, py + panel_h - 1, k_grid);
        c.vline(px, py, py + panel_h - 1, k_grid);
        c.vline(px + panel_w, py, py + panel_h - 1, k_grid);

        for (std::size_t s = 0; s < series.size(); ++s) {
            const auto& colour = k_palette[s % k_palette.size()];
            const int ly = py + 4 + (static_cast<int>(s) * row_h);
            for (int dy = 0; dy < 4; ++dy) {
                c.hline(px + 4, px + 12, ly + dy + 1, colour);
            }
            c.text(px + 17, ly, series[s].label, k_text);
        }
    }

    return c.release();
}

} // namespace vc::debug
