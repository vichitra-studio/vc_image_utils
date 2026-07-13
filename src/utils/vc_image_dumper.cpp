// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/utils/vc_image_dumper.h"

// TODO(you): you will need these once write_dump() constructs a writer and
// reports failures. vc/vc_image.h is already pulled in transitively via
// vc_image_dumper.h.
// #include "vc/io/vc_io.h"
// #include "vc/utils/vc_log.h"

namespace vc::utils::debug {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below. Same
// single-threaded assumption as vc::utils::log.
bool g_enabled = false;
vc::io::path g_output_dir = "./vc_debug_dump";
std::size_t g_counter = 0;

// TODO(you): implement — the one place an already-resolved image actually
// gets written. Never declared in vc_image_dumper.h: nothing outside this
// file needs to call it directly, and since dump() below is no longer a
// template, there's no compile-time reason to expose it either.
//   1. #include <filesystem> — std::filesystem::create_directories(
//      output_dir()) before the first write; safe to call every time,
//      it's a no-op if the directory already exists.
//   2. Build the filename: zero-pad g_counter to 3 digits (#include
//      <iomanip>/<sstream>: std::setw(3) << std::setfill('0')), then
//      "<NNN>_<label>.png", joined onto output_dir(). Increment
//      g_counter regardless of whether the write below succeeds, so a
//      failed dump doesn't silently reuse a filename.
//   3. try { vc::io::stb_image_writer{}.write(path, image); }
//      catch (const vc::vc_exception& e) {
//          vc::utils::log::error("image_dumper", e.what());
//      }
//      Do not rethrow — see the header comment: a broken debug dump
//      must never crash the pipeline it's observing.
void write_dump(const vc::utils::string& label, const vc::vc_image& image) {
    (void)label;
    (void)image;
}

} // namespace

void set_enabled(bool on) {
    // TODO(you): g_enabled = on;
    (void)on;
}

bool enabled() noexcept {
    // TODO(you): return g_enabled;
    return false;
}

void set_output_dir(const vc::io::path& dir) {
    // TODO(you): g_output_dir = dir;
    (void)dir;
}

vc::io::path output_dir() noexcept {
    // TODO(you): return g_output_dir;
    return {};
}

void dump(const vc::utils::string& label,
          const std::optional<vc::vc_image>& image) {
    // TODO(you): if (!image) return;
    //            write_dump(label, *image);
    (void)label;
    (void)image;
}

} // namespace vc::utils::debug
