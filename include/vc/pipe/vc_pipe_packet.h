// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <any>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::pipe {

// A type-erased payload that travels between pipes on a named slot. It holds
// any single value — an image, a transform matrix, a scalar, a point list —
// and remembers the concrete type, so a downstream stage can unbox it safely.
// See docs/pipe_design.md Sec 4.3.
//
// std::any (an OPEN set of payload types — third-party stages can introduce
// new ones) is chosen over a closed std::variant for extensibility, at the
// cost of a heap allocation + RTTI. That choice is [OPEN] in the design and
// revisited once the real payload set is known.
//
// This class is written in full — there is no learning rep in hand-writing an
// any_cast. The reps live in the stage process() bodies (vc_pipeline's own
// validate()/run() are done).
class vc_pipe_packet {
  public:
    vc_pipe_packet() = default;

    // Wrap a value. The requires-clause stops this template from being chosen
    // in place of the copy/move constructors when T would deduce to
    // vc_pipe_packet itself (the classic "greedy templated constructor" trap).
    template <typename T>
        requires(!std::is_same_v<std::decay_t<T>, vc_pipe_packet>)
    explicit vc_pipe_packet(T value) : value_(std::move(value)) {
    }

    // Unbox to T. Throws vc::vc_exception if the stored payload is not a T.
    // In a graph that cleared assembly-time validation (Sec 6) this never
    // throws, because the connected types were already checked — but it is a
    // runtime cast, not a compile-time guarantee on its own.
    template <typename T> const T& get() const {
        const T* held = std::any_cast<T>(&value_);
        if (held == nullptr) {
            throw vc::vc_exception(
                vc::vc_error_code::invalid_argument,
                "vc_pipe_packet::get<T>(): requested type does not match the "
                "stored payload type");
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

} // namespace vc::pipe
