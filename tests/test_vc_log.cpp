// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "doctest/doctest.h"

#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "vc/utils/vc_log.h"

namespace {

// Redirects std::cerr into a string for the lifetime of this object, then
// restores it — lets tests assert on vc::log's actual output without a
// pluggable sink in the library itself.
class cerr_capture {
  public:
    cerr_capture() : old_buf_(std::cerr.rdbuf(buf_.rdbuf())) {
    }
    ~cerr_capture() {
        std::cerr.rdbuf(old_buf_);
    }
    cerr_capture(const cerr_capture&) = delete;
    cerr_capture& operator=(const cerr_capture&) = delete;

    std::string str() const {
        return buf_.str();
    }

  private:
    std::ostringstream buf_;
    std::streambuf* old_buf_;
};

} // namespace

TEST_CASE("vc_log: set_enabled/enabled round-trip") {
    vc::utils::log::set_enabled(false);
    CHECK(vc::utils::log::enabled() == false);
    vc::utils::log::set_enabled(true);
    CHECK(vc::utils::log::enabled() == true);
}

TEST_CASE("vc_log: set_min_level/min_level round-trip") {
    vc::utils::log::set_min_level(vc::utils::log::level::error);
    CHECK(vc::utils::log::min_level() == vc::utils::log::level::error);
    vc::utils::log::set_min_level(vc::utils::log::level::debug);
    CHECK(vc::utils::log::min_level() == vc::utils::log::level::debug);
    vc::utils::log::set_min_level(vc::utils::log::level::info);
}

TEST_CASE("vc_log: should_log respects enabled and min_level") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::warning);
    CHECK(vc::utils::log::should_log(vc::utils::log::level::debug) == false);
    CHECK(vc::utils::log::should_log(vc::utils::log::level::info) == false);
    CHECK(vc::utils::log::should_log(vc::utils::log::level::warning) == true);
    CHECK(vc::utils::log::should_log(vc::utils::log::level::error) == true);

    vc::utils::log::set_enabled(false);
    CHECK(vc::utils::log::should_log(vc::utils::log::level::error) == false);

    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::info);
}

TEST_CASE("vc_log::log_info_builder: deferred lambda is not invoked when "
          "globally disabled") {
    vc::utils::log::set_enabled(false);
    vc::utils::log::log_info_builder builder;

    bool invoked = false;
    vc::utils::log::debug("test",
                          builder([&]() -> std::optional<vc::utils::message> {
                              invoked = true;
                              return vc::utils::message("should not run");
                          }));

    CHECK(invoked == false);
    vc::utils::log::set_enabled(true);
}

TEST_CASE("vc_log::log_info_builder: deferred lambda is invoked when enabled") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::log_info_builder builder;

    bool invoked = false;
    vc::utils::log::debug("test",
                          builder([&]() -> std::optional<vc::utils::message> {
                              invoked = true;
                              return vc::utils::message("ran");
                          }));

    CHECK(invoked == true);
}

TEST_CASE("vc_log::log_info_builder: nullopt from the callable emits nothing") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::log_info_builder builder;

    cerr_capture capture;
    vc::utils::log::debug("test",
                          builder([&]() -> std::optional<vc::utils::message> {
                              return std::nullopt;
                          }));

    CHECK(capture.str().empty());
}

TEST_CASE("vc_log::log_info_builder: tag_enabled(false) suppresses this "
          "instance's messages") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::log_info_builder builder(false);

    bool invoked = false;
    vc::utils::log::debug("test",
                          builder([&]() -> std::optional<vc::utils::message> {
                              invoked = true;
                              return vc::utils::message("should not run");
                          }));

    CHECK(invoked == false);
}

TEST_CASE("vc_log: debug() still filters by level even though log_info_builder "
          "doesn't") {
    // log_info_builder's should_build() no longer checks level (only
    // tag_enabled + the global switch) — debug()/info()/warning()/error()
    // are where level filtering actually happens now, so a message built
    // via log_info_builder (or passed directly) still gets discarded if
    // it's below min_level.
    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::error);

    cerr_capture capture;
    vc::utils::log::debug("test", "should not be written");

    CHECK(capture.str().empty());
    vc::utils::log::set_min_level(vc::utils::log::level::info);
}

TEST_CASE("vc_log::log_info_builder: temp() form ignores tag_enabled, respects "
          "only the global switch") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::log_info_builder builder(false); // tag disabled

    bool invoked = false;
    vc::utils::log::temp(
        "scratch", builder.temp([&]() -> std::optional<vc::utils::message> {
            invoked = true;
            return vc::utils::message("ran");
        }));

    CHECK(invoked == true);
}

TEST_CASE("vc_log: plain message call writes tag and message to stderr") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::info);

    cerr_capture capture;
    vc::utils::log::info("resize", "kernel started");

    const std::string output = capture.str();
    CHECK(output.find("resize") != std::string::npos);
    CHECK(output.find("kernel started") != std::string::npos);
}

TEST_CASE("vc_log: temp() always fires regardless of min_level") {
    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::error);

    cerr_capture capture;
    vc::utils::log::temp("scratch", "always shown");

    CHECK(capture.str().find("always shown") != std::string::npos);
    vc::utils::log::set_min_level(vc::utils::log::level::info);
}

TEST_CASE("vc_log: temp() is silenced by the master enabled switch") {
    vc::utils::log::set_enabled(false);

    cerr_capture capture;
    vc::utils::log::temp("scratch", "should not appear");

    CHECK(capture.str().empty());
    vc::utils::log::set_enabled(true);
}
