// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <memory>
#include <span>

#include "vc/io/vc_io_fs.h"
#include "vc/io/vc_io_stb.h"

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"

// This is the only translation unit that includes these headers, so it's
// also the only place the *_IMPLEMENTATION macros can be defined — no risk
// of a second definition elsewhere. Warnings from the vendored stb headers
// are suppressed via the SYSTEM include path in CMakeLists.txt.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace vc::io {

vc::vc_image stb_image_reader::read(const path& p, const read_config& config) {
    // Checked before stbi_load() so a missing/unreadable input reports
    // file_not_found — distinct from decode_error below, which now
    // genuinely means "the file exists but stb couldn't decode it".
    require_file_exists(p, "stb_image_reader::read");

    int w = 0, h = 0, channels_in_file = 0;

    // OWNING from the moment stb hands the buffer over, because everything
    // between here and the return can throw: vc_image_writer's constructor
    // validates the geometry and then allocates the pixel buffer (std::bad_alloc
    // on a large decode), and the switch below throws outright on a dtype it
    // does not recognise — read_config::dtype is a plain field with no
    // validation, so a caller can reach that branch with one bad argument. A
    // bare `unsigned char*` freed at the bottom of the function leaks the entire
    // decoded image down every one of those paths. Measured at 3.2 MB per throw
    // on the 3-channel JPEG fixture before this was a unique_ptr.
    //
    // Nothing caught it: no test made read() throw after stbi_load() succeeded,
    // and LeakSanitizer does not run on macOS/arm64 ("detect_leaks is not
    // supported on this platform"), so the debug-sanitizers preset is blind to
    // leaks by construction. The regression test is in tests/test_vc_image.cpp.
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data{
        stbi_load(p.c_str(), &w, &h, &channels_in_file, 0), &stbi_image_free};

    auto uw = static_cast<vc::image_dim>(w);
    auto uh = static_cast<vc::image_dim>(h);
    auto channels = static_cast<vc::channel_count>(channels_in_file);

    if (!data) {
        throw vc::vc_exception(vc::vc_error_code::decode_error,
                               "stb_image_reader::read: " + p.string() + ": " +
                                   stbi_failure_reason());
    }

    vc_image_writer writer = [&]() {
        switch (config.dtype) {
        case vc::pixel_dtype::f32: {
            auto temp_writer =
                vc_image_writer(uw, uh, channels, vc::buf_f32{0.0f});
            temp_writer.with_pixels<vc::buf_f32>(
                [&](std::span<vc::buf_f32> pix) {
                    std::transform(data.get(), data.get() + pix.size(),
                                   pix.begin(), [](unsigned char v) {
                                       return static_cast<vc::buf_f32>(v) /
                                              255.0f;
                                   });
                });
            return temp_writer;
        }
        case vc::pixel_dtype::u8: {
            auto temp_writer = vc_image_writer(uw, uh, channels, vc::buf_u8{0});
            temp_writer.with_pixels<vc::buf_u8>([&](std::span<vc::buf_u8> pix) {
                std::copy(data.get(), data.get() + pix.size(), pix.begin());
            });
            return temp_writer;
        }
        case vc::pixel_dtype::u16: {
            auto temp_writer =
                vc_image_writer(uw, uh, channels, vc::buf_u16{0});
            temp_writer.with_pixels<vc::buf_u16>(
                [&](std::span<vc::buf_u16> pix) {
                    std::transform(data.get(), data.get() + pix.size(),
                                   pix.begin(), [](unsigned char v) {
                                       return static_cast<vc::buf_u16>(
                                           (static_cast<unsigned>(v) << 8) | v);
                                   });
                });
            return temp_writer;
        }
        default:
            throw vc::vc_exception(vc::vc_error_code::decode_error,
                                   "stb_image_reader::read: unsupported dtype");
        }
    }();

    return std::move(writer).seal();
}

void stb_image_writer::write(const path& p,
                             const vc::vc_image& image,
                             const write_config& config) {
    // Auto-create the target's parent directory if it doesn't exist yet,
    // rather than making every caller ensure it exists first — a no-op if
    // it already exists (ensure_directory()'s own semantics).
    // vc::debug::write_dump() relies on this: it no longer creates
    // its own output directory, since this already covers it.
    ensure_parent_directory(p);

    auto buf = image.pixels();
    std::vector<unsigned char> data(buf->size());
    switch (buf->dtype()) {
    case vc::pixel_dtype::f32: {
        std::transform(buf->as<vc::buf_f32>().begin(),
                       buf->as<vc::buf_f32>().end(), data.begin(),
                       [](vc::buf_f32 v) {
                           return static_cast<unsigned char>(
                               std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
                       });
        break;
    }
    case vc::pixel_dtype::u8: {
        std::copy(buf->as<vc::buf_u8>().begin(), buf->as<vc::buf_u8>().end(),
                  data.begin());
        break;
    }
    case vc::pixel_dtype::u16: {
        std::transform(
            buf->as<vc::buf_u16>().begin(), buf->as<vc::buf_u16>().end(),
            data.begin(),
            [](vc::buf_u16 v) { return static_cast<unsigned char>(v >> 8); });
        break;
    }
    }

    int result = 0;
    switch (config.format) {
    case vc::io::vc_image_format::png: {
        int stride = static_cast<int>(image.width() * image.channels());
        result = stbi_write_png(p.c_str(), static_cast<int>(image.width()),
                                static_cast<int>(image.height()),
                                static_cast<int>(image.channels()), data.data(),
                                stride);
        break;
    }
    case vc::io::vc_image_format::jpeg: {
        result = stbi_write_jpg(p.c_str(), static_cast<int>(image.width()),
                                static_cast<int>(image.height()),
                                static_cast<int>(image.channels()), data.data(),
                                config.jpeg_quality);
        break;
    }
    default:
        throw vc::vc_exception(vc::vc_error_code::encode_error,
                               "stb_image_writer::write: unsupported format");
    }

    if (!result) {
        throw vc::vc_exception(vc::vc_error_code::encode_error,
                               "stb_image_writer::write: failed to write " +
                                   p.string());
    }
}

} // namespace vc::io
