// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "doctest/doctest.h"

#include "vc/core/vc_exception.h"
#include "vc/core/vc_pixel_buffer.h"

TEST_CASE("vc_pixel_buffer: float construction reports size and dtype") {
    vc::vc_pixel_buffer buf(5, vc::buf_f32{1.0f});
    CHECK(buf.size() == 5);
    CHECK(buf.dtype() == vc::pixel_dtype::f32);
}

TEST_CASE("vc_pixel_buffer: uint8_t construction reports size and dtype") {
    vc::vc_pixel_buffer buf(4, vc::buf_u8{200});
    CHECK(buf.size() == 4);
    CHECK(buf.dtype() == vc::pixel_dtype::u8);
}

TEST_CASE("vc_pixel_buffer: uint16_t construction reports size and dtype") {
    vc::vc_pixel_buffer buf(3, vc::buf_u16{40000});
    CHECK(buf.size() == 3);
    CHECK(buf.dtype() == vc::pixel_dtype::u16);
}

TEST_CASE(
    "vc_pixel_buffer: as<T>() returns the fill value for the matching dtype") {
    vc::vc_pixel_buffer buf(3, vc::buf_f32{0.5f});
    auto span = buf.as<vc::buf_f32>();
    REQUIRE(span.size() == 3);
    CHECK(span[0] == 0.5f);
}

TEST_CASE("vc_pixel_buffer: as<T>() mutates the backing storage") {
    vc::vc_pixel_buffer buf(3, vc::buf_u8{0});
    buf.as<vc::buf_u8>()[1] = 42;
    CHECK(buf.as<vc::buf_u8>()[1] == 42);
}

TEST_CASE("vc_pixel_buffer: as<T>() throws on a dtype mismatch") {
    vc::vc_pixel_buffer buf(3, vc::buf_f32{1.0f});
    CHECK_THROWS_AS(buf.as<vc::buf_u8>(), vc::vc_exception);
}
