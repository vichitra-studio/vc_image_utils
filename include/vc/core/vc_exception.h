// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <exception>
#include <memory>

#include "vc/core/vc_error_code.h"
#include "vc/utils/vc_strings.h"

namespace vc {

class vc_exception : public std::exception {
  public:
    vc_exception(vc_error_code code, vc::utils::message message);

    // Explicitly defaulted (rather than left implicit) so the non-throwing
    // guarantee is checked by the compiler, not just true by accident of the
    // current member list: copying is an enum copy plus a shared_ptr refcount
    // bump, never an allocation. This matters because an in-flight exception
    // can be copied during stack unwinding or when captured into a
    // std::exception_ptr — a bad_alloc there is std::terminate, not a
    // catchable error. If a future change adds a by-value std::string (or
    // anything else with a throwing copy) back onto this class, this
    // declaration turns that into a compile error here instead of a
    // terminate() at some unrelated call site later.
    //
    // Deliberately no matching move constructor: declaring this copy ctor
    // already suppresses the implicitly-generated move ctor/move-assignment,
    // and that suppression is wanted, not incidental — a defaulted move
    // would leave message_ null in the moved-from object, and what() derefs
    // message_ unconditionally (UB on a moved-from what() call). Falling
    // back to the (equally cheap, refcount-bump) copy ctor at every would-be
    // move site removes that null state from existing entirely, at
    // essentially no cost.
    vc_exception(const vc_exception&) noexcept = default;

    vc_error_code code() const noexcept {
        return code_;
    }
    const char* what() const noexcept override {
        return message_->c_str();
    }

  private:
    vc_error_code code_;
    // shared_ptr<const T>, not a by-value string: see the copy-ctor comment
    // above — this is what makes that copy non-allocating.
    std::shared_ptr<const vc::utils::message> message_;
};

} // namespace vc
