// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <exception>

#include "vc/utils/vc_strings.h"
#include "vc/vc_error_code.h"

namespace vc {

class vc_exception : public std::exception {
  public:
    vc_exception(vc_error_code code, vc::utils::message message);

    vc_error_code code() const noexcept {
        return code_;
    }
    const char* what() const noexcept override {
        return message_.c_str();
    }

  private:
    vc_error_code code_;
    vc::utils::message message_;
};

} // namespace vc
