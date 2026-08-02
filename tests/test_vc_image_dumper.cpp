// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "doctest/doctest.h"

#include <filesystem>
#include <optional>
#include <string>

#include "vc/core/vc_image.h"
#include "vc/debug/vc_image_dumper.h"

#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

TEST_CASE("vc_image_dumper: set_enabled/enabled round-trip") {
    vc::debug::set_enabled(true);
    CHECK(vc::debug::enabled() == true);
    vc::debug::set_enabled(false);
    CHECK(vc::debug::enabled() == false);
}

TEST_CASE("vc_image_dumper: set_output_dir/output_dir round-trip") {
    const vc::io::path dir = std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test";
    vc::debug::set_output_dir(dir);
    CHECK(vc::debug::output_dir() == dir);
}

TEST_CASE("vc_image_dumper: dump() is a no-op when disabled") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_disabled";
    std::filesystem::remove_all(dir);
    vc::debug::set_output_dir(dir);
    vc::debug::set_enabled(false);

    vc::vc_image img = vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
    CHECK_NOTHROW(vc::debug::dump("test", img));

    CHECK(std::filesystem::exists(dir) == false);
}

TEST_CASE("vc_image_dumper: dump() never throws, even on internal failure") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_enabled";
    std::filesystem::remove_all(dir);
    vc::debug::set_output_dir(dir);
    vc::debug::set_enabled(true);

    vc::vc_image img = vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
    CHECK_NOTHROW(vc::debug::dump("test", img));

    vc::debug::set_enabled(false);
}

TEST_CASE("vc_image_dumper: dump() writes a numbered file when enabled") {
    // Green: exercises both vc::debug::write_dump() and
    // vc::io::stb_image_writer::write(), both implemented.
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_writes";
    std::filesystem::remove_all(dir);
    vc::debug::set_output_dir(dir);
    vc::debug::set_enabled(true);

    vc::vc_image img = vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
    vc::debug::dump("resize", img);

    // The counter is process-wide and globally-incrementing, not reset per
    // test/directory (vc_image_dumper.h) — so its value here depends on how
    // many earlier test cases in this run already dumped something. Check
    // for the right SUFFIX, not a specific NNN prefix.
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().string().ends_with("_resize.png")) {
            found = true;
            break;
        }
    }
    CHECK(found);

    vc::debug::set_enabled(false);
}

TEST_CASE(
    "vc_image_dumper::dump_image_builder: deferred callable is not invoked "
    "when disabled") {
    vc::debug::set_enabled(false);
    vc::debug::dump_image_builder builder;

    bool invoked = false;
    vc::debug::dump("test", builder([&]() -> std::optional<vc::vc_image> {
                        invoked = true;
                        return vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
                    }));

    CHECK(invoked == false);
}

TEST_CASE("vc_image_dumper::dump_image_builder: deferred callable is invoked "
          "when enabled") {
    vc::debug::set_enabled(true);
    vc::debug::dump_image_builder builder;

    bool invoked = false;
    vc::debug::dump("test", builder([&]() -> std::optional<vc::vc_image> {
                        invoked = true;
                        return vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
                    }));

    CHECK(invoked == true);
    vc::debug::set_enabled(false);
}

TEST_CASE(
    "vc_image_dumper::dump_image_builder: nullopt from the callable writes "
    "nothing") {
    const vc::io::path dir =
        std::string(VC_TEST_OUTPUT_DIR) + "/dumper_test_nullopt";
    std::filesystem::remove_all(dir);
    vc::debug::set_output_dir(dir);
    vc::debug::set_enabled(true);
    vc::debug::dump_image_builder builder;

    vc::debug::dump("test", builder([&]() -> std::optional<vc::vc_image> {
                        return std::nullopt;
                    }));

    CHECK(std::filesystem::exists(dir) == false);
    vc::debug::set_enabled(false);
}

TEST_CASE("vc_image_dumper::dump_image_builder: tag_enabled(false) suppresses "
          "this instance's messages") {
    vc::debug::set_enabled(true);
    vc::debug::dump_image_builder builder(false);

    bool invoked = false;
    vc::debug::dump("test", builder([&]() -> std::optional<vc::vc_image> {
                        invoked = true;
                        return vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
                    }));

    CHECK(invoked == false);
    vc::debug::set_enabled(false);
}
