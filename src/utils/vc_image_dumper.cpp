// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <iomanip>
#include <sstream>

#include "vc/io/vc_io.h"
#include "vc/utils/vc_image_dumper.h"

namespace vc::utils::debug {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below. Same
// single-threaded assumption as vc::utils::log.
bool g_enabled = false;
vc::io::path g_output_dir = "./vc_debug_dump";
std::size_t g_counter = 0;

// A failed write is NOT caught here — deliberately: an unexpected dump
// failure crashes the run, surfacing the problem immediately rather than
// continuing on a partially-broken debug session. Fix the underlying issue
// and re-run.
void write_dump(const vc::utils::string& label, const vc::vc_image& image) {
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << g_counter;
    vc::io::path filepath = g_output_dir / (oss.str() + "_" + label + ".png");
    // Incremented before the write, not after: the counter must move on
    // regardless of whether the write below succeeds, so a failed dump
    // never silently reuses the next filename.
    ++g_counter;
    vc::io::stb_image_writer image_writer{};
    image_writer.write(filepath, image);
}

} // namespace

void set_enabled(bool on) {
    g_enabled = on;
}

bool enabled() noexcept {
    return g_enabled;
}

void set_output_dir(const vc::io::path& dir) {
    g_output_dir = dir;
}

vc::io::path output_dir() noexcept {
    return g_output_dir;
}

void dump(const vc::utils::string& label,
          const std::optional<vc::vc_image>& image) {
    if (!image || !enabled())
        return;
    write_dump(label, *image);
}

} // namespace vc::utils::debug
