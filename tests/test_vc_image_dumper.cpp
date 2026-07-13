// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "doctest/doctest.h"

#include <filesystem>
#include <optional>
#include <string>

#include "vc/utils/vc_image_dumper.h"
#include "vc/vc_image.h"

#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

TEST_CASE("vc_image_dumper: set_enabled/enabled round-trip") {
    vc::utils::debug::set_enabled(true);
    CHECK(vc::utils::debug::enabled() == true);
    vc::utils::debug::set_enabled(false);
    CHECK(vc::utils::debug::enabled() == false);
}

TEST_CASE("vc_image_dumper: set_output_dir/output_dir round-trip") {
    const vc::io::path dir = std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test";
    vc::utils::debug::set_output_dir(dir);
    CHECK(vc::utils::debug::output_dir() == dir);
}

TEST_CASE("vc_image_dumper: dump() is a no-op when disabled") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_disabled";
    std::filesystem::remove_all(dir);
    vc::utils::debug::set_output_dir(dir);
    vc::utils::debug::set_enabled(false);

    vc::vc_image img(4, 2, 3);
    CHECK_NOTHROW(vc::utils::debug::dump("test", img));

    CHECK(std::filesystem::exists(dir) == false);
}

TEST_CASE("vc_image_dumper: dump() never throws, even on internal failure") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_enabled";
    std::filesystem::remove_all(dir);
    vc::utils::debug::set_output_dir(dir);
    vc::utils::debug::set_enabled(true);

    vc::vc_image img(4, 2, 3);
    CHECK_NOTHROW(vc::utils::debug::dump("test", img));

    vc::utils::debug::set_enabled(false);
}

TEST_CASE("vc_image_dumper: dump() writes a numbered file when enabled") {
    // Currently red: requires both vc::utils::debug::write_dump() and
    // vc::io::stb_image_writer::write() to be implemented.
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_writes";
    std::filesystem::remove_all(dir);
    vc::utils::debug::set_output_dir(dir);
    vc::utils::debug::set_enabled(true);

    vc::vc_image img(4, 2, 3);
    vc::utils::debug::dump("resize", img);

    CHECK(std::filesystem::exists(dir + "/000_resize.png"));

    vc::utils::debug::set_enabled(false);
}

TEST_CASE(
    "vc_image_dumper::dump_builder: deferred callable is not invoked when "
    "disabled") {
    vc::utils::debug::set_enabled(false);
    vc::utils::debug::dump_builder builder;

    bool invoked = false;
    vc::utils::debug::dump("test",
                           builder([&]() -> std::optional<vc::vc_image> {
                               invoked = true;
                               return vc::vc_image(4, 2, 3);
                           }));

    CHECK(invoked == false);
}

TEST_CASE("vc_image_dumper::dump_builder: deferred callable is invoked when "
          "enabled") {
    vc::utils::debug::set_enabled(true);
    vc::utils::debug::dump_builder builder;

    bool invoked = false;
    vc::utils::debug::dump("test",
                           builder([&]() -> std::optional<vc::vc_image> {
                               invoked = true;
                               return vc::vc_image(4, 2, 3);
                           }));

    CHECK(invoked == true);
    vc::utils::debug::set_enabled(false);
}

TEST_CASE(
    "vc_image_dumper::dump_builder: nullopt from the callable writes nothing") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_nullopt";
    std::filesystem::remove_all(dir);
    vc::utils::debug::set_output_dir(dir);
    vc::utils::debug::set_enabled(true);
    vc::utils::debug::dump_builder builder;

    vc::utils::debug::dump(
        "test",
        builder([&]() -> std::optional<vc::vc_image> { return std::nullopt; }));

    CHECK(std::filesystem::exists(dir) == false);
    vc::utils::debug::set_enabled(false);
}

TEST_CASE("vc_image_dumper::dump_builder: tag_enabled(false) suppresses this "
          "instance's messages") {
    vc::utils::debug::set_enabled(true);
    vc::utils::debug::dump_builder builder(false);

    bool invoked = false;
    vc::utils::debug::dump("test",
                           builder([&]() -> std::optional<vc::vc_image> {
                               invoked = true;
                               return vc::vc_image(4, 2, 3);
                           }));

    CHECK(invoked == false);
    vc::utils::debug::set_enabled(false);
}
