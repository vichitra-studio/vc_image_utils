// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <any>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

// Lives at vc/ (core), NOT vc/utils/: this box throws vc::vc_exception, and
// vc_error_code.h/vc_exception.h both include vc/utils/vc_strings.h, so
// vc::utils sits BELOW core in the dependency order. A throwing type placed
// in vc::utils would create a utils -> core -> utils cycle.
namespace vc {

// Shared type-erased single-value box behind vc::pipe::vc_pipe_packet and
// vc::vc_metadata_value: identical wrap/get<T>/has_value/type
// mechanism, member for member — the two only ever differed in the type
// name reported by get<T>()'s mismatch message. That name is now supplied
// by Tag::type_name, so each instantiation of this template is still its
// OWN distinct type (vc_any_box<TagA> and vc_any_box<TagB> do not convert to
// one another) — a metadata value must not be silently interchangeable with
// a pipe packet, even though they share one implementation.
template <typename Tag> class vc_any_box {
  public:
    vc_any_box() = default;

    // Wrap a value. The requires-clause stops this template from being
    // chosen in place of the copy/move constructors when T would deduce to
    // vc_any_box itself (the classic "greedy templated constructor" trap).
    template <typename T>
        requires(!std::is_same_v<std::decay_t<T>, vc_any_box>)
    explicit vc_any_box(T value) : value_(std::move(value)) {
    }

    // Unbox to T. Throws vc::vc_exception if the stored payload is not a T.
    template <typename T> const T& get() const {
        const T* held = std::any_cast<T>(&value_);
        if (held == nullptr) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                std::string(Tag::type_name) +
                    "::get<T>(): requested type does not match the stored "
                    "payload type");
        }
        return *held;
    }

    bool has_value() const noexcept {
        return value_.has_value();
    }
    const std::type_info& type() const noexcept {
        return value_.type();
    }

  private:
    std::any value_;
};

} // namespace vc
