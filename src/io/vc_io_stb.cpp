// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/io/vc_io.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

// TODO(you): you will need this once read()/write() call ->as<T>() on a
// pixel buffer — vc_types.h (included via vc_io.h -> vc_image.h) only
// forward-declares vc_pixel_buffer.
// #include "vc/vc_pixel_buffer.h"

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
    (void)config; // no fields yet — see vc_io_types.h

    // TODO(you): implement.
    //   1. int w = 0, h = 0, channels_in_file = 0;
    //      unsigned char* data = stbi_load(p.c_str(), &w, &h,
    //      &channels_in_file, 0); Always pass desired_channels = 0 — never
    //      force a conversion (Sec 7.4).
    //   2. if (!data) throw vc::vc_exception(vc::vc_error_code::decode_error,
    //         "stb_image_reader::read: " + p + ": " + stbi_failure_reason());
    //   3. Construct vc::vc_image img(w, h, channels_in_file).
    //   4. Copy data into img.mutable_pixels(), dividing each byte by 255.0f.
    //      Do NOT use stbi_loadf — it silently linearises (applies gamma).
    //      We work in encoded 8-bit space here.
    //   5. stbi_image_free(data) — always, even though vc_image owns its
    //      own buffer; stb's buffer is separate and must be freed here.
    //   6. return img;

    throw vc::vc_exception(vc::vc_error_code::decode_error,
                           "stb_image_reader::read: not yet implemented (" + p +
                               ")");
}

void stb_image_writer::write(const path& p,
                             const vc::vc_image& image,
                             const write_config& config) {
    (void)image;
    (void)config; // TODO(you): dispatch on config.format once more than PNG is
                  // supported

    // TODO(you): implement.
    //   1. auto buf = image.pixels(); — read-only access, see Sec 4.2.
    //   2. Build a temporary std::vector<unsigned char> of buf->size(),
    //      converting each float back with: value * 255.0f + 0.5f, then
    //      cast to uint8_t. The +0.5f rounds instead of truncating.
    //   3. int stride = static_cast<int>(image.width() * image.channels());
    //   4. int ok = stbi_write_png(p.c_str(), static_cast<int>(image.width()),
    //         static_cast<int>(image.height()),
    //         static_cast<int>(image.channels()), converted.data(), stride);
    //   5. if (!ok) throw vc::vc_exception(vc::vc_error_code::encode_error,
    //         "stb_image_writer::write: failed to write " + p);

    throw vc::vc_exception(vc::vc_error_code::encode_error,
                           "stb_image_writer::write: not yet implemented (" +
                               p + ")");
}

} // namespace vc::io
