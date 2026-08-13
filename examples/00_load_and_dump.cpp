// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Example 00 — load an image, address a pixel, write it back out.
//
// The smallest complete loop in the library: decode a file into a vc_image,
// read one pixel through the descriptor's index map, and encode the result to
// a PNG you can open. Everything later in this directory assumes this loop.
//
// The idea worth taking away is the index map. An image is stored as ONE flat
// buffer, not a 2-D array of pixels, and vc_image_info::index() is the
// function that turns a coordinate into a position in it:
//
//     index = (y * width + x) * channels + ch
//
// That layout is "interleaved, row-major": the three channels of one pixel sit
// next to each other, pixels run along x, and rows follow one another down y.

#include <cstdlib>
#include <iostream>
#include <string>

#include "vc/core/vc_image.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/io/vc_io_stb.h"

namespace {

// Examples check their own results so ctest can run them as smoke tests.
//
// Deliberately not <cassert>: assert() compiles out under NDEBUG, so a Release
// run would silently verify nothing while still reporting success.
bool check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

} // namespace

int main() {
    const vc::io::path input =
        std::string(VC_EXAMPLES_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const vc::io::path output =
        std::string(VC_EXAMPLES_OUTPUT_DIR) + "/00_load_and_dump.png";

    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;

    // u8 rather than f32: both stb paths are a straight copy for u8, so what
    // is written is byte-for-byte what was decoded. Reading as f32 would
    // divide by 255 on the way in and multiply back on the way out, which is
    // the right choice for arithmetic but adds a rounding step here.
    const vc::vc_image img =
        reader.read(input, vc::io::read_config{.dtype = vc::pixel_dtype::u8});

    std::cout << "loaded " << input.string() << '\n'
              << "  " << img.width() << " x " << img.height() << " x "
              << img.channels() << " channels\n"
              << "  " << img.pixel_count() << " elements in one flat buffer\n";

    const auto pixels = img.pixels()->as<vc::buf_u8>();

    // Address one pixel through the descriptor rather than by hand-computing
    // an offset. Centre of the image, so this stays in range whatever fixture
    // is used.
    const vc::image_dim x = img.width() / 2;
    const vc::image_dim y = img.height() / 2;
    std::cout << "  pixel (" << x << ", " << y << ") = ("
              << static_cast<unsigned>(pixels[img.meta().index(x, y, 0)])
              << ", "
              << static_cast<unsigned>(pixels[img.meta().index(x, y, 1)])
              << ", "
              << static_cast<unsigned>(pixels[img.meta().index(x, y, 2)])
              << ")\n";

    // The three channels of one pixel are ADJACENT — that is what interleaved
    // means, and it is visible directly in the offsets.
    std::cout << "  offsets of its R, G, B: " << img.meta().index(x, y, 0)
              << ", " << img.meta().index(x, y, 1) << ", "
              << img.meta().index(x, y, 2) << '\n';

    writer.write(output, img,
                 vc::io::write_config{.format = vc::io::vc_image_format::png});
    std::cout << "wrote " << output.string() << " — open it to inspect\n";

    bool ok = true;
    ok = check(img.channels() == 3, "fixture decodes to 3 channels") && ok;
    ok = check(img.width() > 0 && img.height() > 0, "geometry is non-empty") &&
         ok;
    ok = check(pixels.size() == img.pixel_count(),
               "buffer length matches width * height * channels") &&
         ok;
    // Channels adjacent, one step in x costs `channels` elements.
    ok = check(img.meta().index(x, y, 1) - img.meta().index(x, y, 0) == 1,
               "channels of a pixel are adjacent") &&
         ok;
    ok = check(img.meta().index(x + 1, y, 0) - img.meta().index(x, y, 0) ==
                   img.channels(),
               "one step in x costs `channels` elements") &&
         ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
