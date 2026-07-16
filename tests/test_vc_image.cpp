// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <string>

#include "vc/io/vc_io.h"
#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"
#include "vc/vc_pixel_buffer.h"

#ifndef VC_TEST_DATA_DIR
#error "VC_TEST_DATA_DIR must be defined by CMake (see CMakeLists.txt)"
#endif
#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

TEST_CASE("vc_image: zeros() validates and allocates") {
    vc::vc_image img = vc::vc_image::zeros(4, 2, 3);
    CHECK(img.width() == 4);
    CHECK(img.height() == 2);
    CHECK(img.channels() == 3);
    CHECK(img.pixel_count() == 4 * 2 * 3);
    REQUIRE(img.pixels() != nullptr);
    CHECK(img.pixels()->size() == img.pixel_count());
}

TEST_CASE("vc_image: zeros() rejects invalid dimensions") {
    CHECK_THROWS_AS(vc::vc_image::zeros(0, 2, 3), vc::vc_exception);
    CHECK_THROWS_AS(vc::vc_image::zeros(4, 0, 3), vc::vc_exception);
    CHECK_THROWS_AS(vc::vc_image::zeros(4, 2, 0), vc::vc_exception);
}

TEST_CASE("vc_error_code: round-trips through to_int/to_error_code") {
    CHECK(vc::to_error_code(vc::to_int(vc::vc_error_code::decode_error)) ==
          vc::vc_error_code::decode_error);
    CHECK(vc::to_string(vc::vc_error_code::file_not_found) == "file_not_found");
}

TEST_CASE("stb round-trip: JPEG in, PNG out, dimensions and pixels match") {
    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;

    const vc::io::path input =
        std::string(VC_TEST_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const vc::io::path output =
        std::string(VC_TEST_OUTPUT_DIR) + "/roundtrip_test_output.png";

    vc::vc_image img = reader.read(input);
    CHECK(img.channels() == 3);
    CHECK(img.width() > 0);
    CHECK(img.height() > 0);

    writer.write(output, img);

    vc::vc_image roundtripped = reader.read(output);
    CHECK(roundtripped.width() == img.width());
    CHECK(roundtripped.height() == img.height());
    CHECK(roundtripped.channels() == img.channels());

    REQUIRE(img.pixels() != nullptr);
    REQUIRE(roundtripped.pixels() != nullptr);
    const auto& original = *img.pixels();
    const auto& reconstructed = *roundtripped.pixels();
    REQUIRE(original.size() == reconstructed.size());

    const auto original_pixels = original.as<vc::buf_f32>();
    const auto reconstructed_pixels = reconstructed.as<vc::buf_f32>();

    // PNG is lossless, so JPEG-decoded values written to PNG and read back
    // should match the original decode almost exactly — a 1/255 tolerance
    // accounts for float rounding, not any re-compression loss.
    bool all_close = true;
    for (std::size_t i = 0; i < original_pixels.size(); ++i) {
        if (std::fabs(original_pixels[i] - reconstructed_pixels[i]) >
            (1.0f / 255.0f)) {
            all_close = false;
            break;
        }
    }
    CHECK(all_close);
}
