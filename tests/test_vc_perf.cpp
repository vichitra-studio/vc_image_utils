// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "doctest/doctest.h"

#include <iostream>
#include <sstream>
#include <string>

#include "vc/utils/vc_log.h"
#include "vc/utils/vc_perf.h"

namespace {

// Redirects std::cerr into a string for the lifetime of this object, then
// restores it — lets tests assert on scoped_timer's actual output without
// a pluggable sink in the library itself.
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

TEST_CASE("vc_perf: set_enabled/enabled round-trip") {
    vc::utils::perf::set_enabled(false);
    CHECK(vc::utils::perf::enabled() == false);
    vc::utils::perf::set_enabled(true);
    CHECK(vc::utils::perf::enabled() == true);
}

TEST_CASE("vc_perf: scoped_timer reports nothing when perf is disabled") {
    vc::utils::perf::set_enabled(false);

    cerr_capture capture;
    {
        vc::utils::perf::scoped_timer timer("test", "step");
    }

    CHECK(capture.str().empty());
    vc::utils::perf::set_enabled(true);
}

TEST_CASE("vc_perf: scoped_timer reports tag and label when enabled") {
    // Green: exercises scoped_timer's constructor/destructor, both
    // implemented.
    vc::utils::perf::set_enabled(true);
    vc::utils::log::set_enabled(true);
    vc::utils::log::set_min_level(vc::utils::log::level::info);

    cerr_capture capture;
    {
        vc::utils::perf::scoped_timer timer("test", "step");
    }

    const std::string output = capture.str();
    CHECK(output.find("test") != std::string::npos);
    CHECK(output.find("step") != std::string::npos);
}
