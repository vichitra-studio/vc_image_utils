// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "vc/edit/vc_memory_image_meta.h"
#include "vc/io/vc_io_stb.h"
#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"
#include "vc/vc_image_meta.h"
#include "vc/vc_image_writer.h"
#include "vc/vc_pixel_buffer.h"

#ifndef VC_TEST_DATA_DIR
#error "VC_TEST_DATA_DIR must be defined by CMake (see CMakeLists.txt)"
#endif
#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

TEST_CASE("vc_image: zeros() validates and allocates") {
    vc::vc_image img = vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
    CHECK(img.width() == 4);
    CHECK(img.height() == 2);
    CHECK(img.channels() == 3);
    CHECK(img.pixel_count() == 4 * 2 * 3);
    REQUIRE(img.pixels() != nullptr);
    CHECK(img.pixels()->size() == img.pixel_count());
}

TEST_CASE("vc_image: zeros() rejects invalid dimensions") {
    // zeros() is [[nodiscard]] (it has no other effect); CHECK_THROWS_AS
    // expands to a bare-statement call, so the result is explicitly
    // discarded here — the test cares only that construction throws.
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(0, 2, 3),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(4, 0, 3),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(4, 2, 0),
                    vc::vc_exception);
}

// vc_image_info composes shared_ptr<const vc::i_image_meta>; a mask
// is an image whose composed metadata is null (the default), and
// vc_image_writer::set_metadata() is the one write path onto it before
// seal(). This is written plumbing (not a rep), so it is GREEN.
TEST_CASE("vc_image_info: composed metadata is null by default") {
    const vc::vc_image_info meta{4, 2, 3};
    CHECK(meta.metadata() == nullptr);
}

TEST_CASE(
    "vc_image_writer/vc_image_info: set_metadata is visible through seal()") {
    auto md = std::make_shared<vc::edit::vc_memory_image_meta>();
    md->set("iso", vc::vc_metadata_value{std::string{"400"}});

    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    CHECK(writer.meta().metadata() == nullptr); // null until set

    writer.set_metadata(md);
    CHECK(writer.meta().metadata() == md);

    vc::vc_image img = std::move(writer).seal();
    REQUIRE(img.meta().metadata() != nullptr);
    CHECK(img.meta().metadata() == md);
    const auto iso = img.meta().metadata()->get("iso");
    REQUIRE(iso.has_value());
    CHECK(iso->get<std::string>() == "400");
}

TEST_CASE("vc_image_writer: with_pixels() mutation survives seal()") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    writer.with_pixels<vc::buf_f32>([](std::span<vc::buf_f32> px) {
        for (std::size_t i = 0; i < px.size(); ++i) {
            px[i] = static_cast<float>(i);
        }
    });

    vc::vc_image img = std::move(writer).seal();
    REQUIRE(img.pixels() != nullptr);
    const auto sealed = img.pixels()->as<vc::buf_f32>();
    REQUIRE(sealed.size() == img.pixel_count());
    for (std::size_t i = 0; i < sealed.size(); ++i) {
        CHECK(sealed[i] == static_cast<float>(i));
    }
}

TEST_CASE("vc_image_writer: with_pixels() works with an explicitly-typed"
          " span parameter") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{1.0f}};
    writer.with_pixels<vc::buf_f32>(
        [](std::span<vc::buf_f32> px) { px[0] = 42.0f; });

    vc::vc_image img = std::move(writer).seal();
    CHECK(img.pixels()->as<vc::buf_f32>()[0] == 42.0f);
}

TEST_CASE("vc_image_writer: with_pixels() works with a generic auto"
          " parameter") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{1.0f}};
    writer.with_pixels<vc::buf_f32>([](auto px) { px[0] = 42.0f; });

    vc::vc_image img = std::move(writer).seal();
    CHECK(img.pixels()->as<vc::buf_f32>()[0] == 42.0f);
}

TEST_CASE("vc_image_writer: with_pixels() span size equals the writer's"
          " element count") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    writer.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px) {
        CHECK(px.size() == writer.pixel_count());
        CHECK(px.size() == 4 * 2 * 3);
    });
}

TEST_CASE("vc_image_writer: with_pixels() throws on a dtype that does not"
          " match the buffer, same contract as pixels()/at()") {
    // The buffer was allocated as buf_f32 (see the fill value below); asking
    // with_pixels() for buf_u8 must fail the same way pixels<u8>() and
    // at<u8>() already do — both forward straight to
    // vc_pixel_buffer::as<T>(), which throws vc::vc_exception when the
    // requested T doesn't match the stored dtype (vc_pixel_buffer.h).
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    try {
        writer.with_pixels<vc::buf_u8>([](std::span<vc::buf_u8>) {});
        FAIL("with_pixels() should have thrown on a dtype mismatch");
    } catch (const vc::vc_exception& e) {
        CHECK(e.code() == vc::vc_error_code::invalid_argument);
    }
}

TEST_CASE("vc_error_code: round-trips through to_int/to_error_code") {
    CHECK(vc::to_error_code(vc::to_int(vc::vc_error_code::decode_error)) ==
          vc::vc_error_code::decode_error);
    CHECK(vc::to_string(vc::vc_error_code::file_not_found) == "file_not_found");
    // pipe_invalid_topology is a newly split-out enumerator (was folded into
    // pipe_connection_mismatch) — both switches in vc_error_code.cpp are
    // exhaustive but neither has a `default` label, so the build (-Wall
    // /-Wextra, no -Werror) would only warn on a missing case, not fail. A
    // missing case in to_error_code() would still be caught loudly at
    // runtime — it falls past the switch into a throw — but a missing case
    // in to_string() fails silently, falling through to
    // "unknown_error_code" instead of throwing; pin both explicitly.
    CHECK(vc::to_error_code(
              vc::to_int(vc::vc_error_code::pipe_invalid_topology)) ==
          vc::vc_error_code::pipe_invalid_topology);
    CHECK(vc::to_string(vc::vc_error_code::pipe_invalid_topology) ==
          "pipe_invalid_topology");
}

TEST_CASE("stb round-trip: JPEG in, PNG out, dimensions and pixels match") {
    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;

    const vc::io::path input =
        std::string(VC_TEST_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const vc::io::path output =
        std::string(VC_TEST_OUTPUT_DIR) + "/roundtrip_test_output.png";

    // Explicit dtype: read()'s default is buf_u8 (vc_io_types.h), but this
    // test asserts against buf_f32 below — the request must match what it
    // checks, not rely on whatever the default happens to be.
    const vc::io::read_config f32_config{.dtype = vc::pixel_dtype::f32};

    vc::vc_image img = reader.read(input, f32_config);
    CHECK(img.channels() == 3);
    CHECK(img.width() > 0);
    CHECK(img.height() > 0);

    // Explicit format: write_config's default is JPEG (vc_io_types.h), but
    // this test's name and tolerance below assume a lossless PNG round-trip
    // — the request must match that, not rely on the default.
    const vc::io::write_config png_config{.format =
                                              vc::io::vc_image_format::png};
    writer.write(output, img, png_config);

    vc::vc_image roundtripped = reader.read(output, f32_config);
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
