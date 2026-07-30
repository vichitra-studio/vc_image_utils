// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <iomanip>
#include <sstream>

#include "vc/debug/vc_image_dumper.h"
#include "vc/io/vc_io_stb.h"

namespace vc::debug {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below (the output
// directory through output_dir_storage() immediately after — see its own
// comment for why that one isn't a plain variable like these two). Same
// single-threaded assumption as vc::utils::log.
bool g_enabled = false;
std::size_t g_counter = 0;

// The output directory's storage, function-local rather than a plain
// TU-static: std::filesystem::path's converting constructor is not
// unconditionally noexcept (it can throw, e.g. on allocation failure), and a
// plain global's initializer runs during static initialization — before
// main(), where nothing exists to catch it, so the process would terminate
// with no useful diagnostic (bugprone-throwing-static-initialization). A
// function-local static defers construction to first call, by which point
// write_dump()'s and set_output_dir()'s callers are inside ordinary,
// catchable control flow — output_dir() is the one caller that stays
// noexcept (existing accessor behaviour, preserved as-is), so a first call
// arriving through THAT path still terminates on construction failure, same
// as it already could on the pre-existing return-by-value copy. Still the
// same process-wide, TU-owned state as g_enabled/g_counter above, just
// constructed lazily instead of eagerly — all three functions read and
// write through this one function, so they still all observe the same
// single value.
vc::io::path& output_dir_storage() {
    static vc::io::path dir = "./vc_debug_dump";
    return dir;
}

// A failed write is NOT caught here — deliberately: an unexpected dump
// failure crashes the run, surfacing the problem immediately rather than
// continuing on a partially-broken debug session. Fix the underlying issue
// and re-run.
void write_dump(const vc::utils::string& label, const vc::vc_image& image) {
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << g_counter;
    vc::io::path filepath =
        output_dir_storage() / (oss.str() + "_" + label + ".png");
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
    output_dir_storage() = dir;
}

vc::io::path output_dir() noexcept {
    return output_dir_storage();
}

void dump(const vc::utils::string& label,
          const std::optional<vc::vc_image>& image) {
    if (!image || !enabled())
        return;
    write_dump(label, *image);
}

} // namespace vc::debug
