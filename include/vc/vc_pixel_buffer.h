// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"
#include "vc/vc_types.h"

namespace vc {

// Element types vc_pixel_buffer can store, named for their pixel role rather
// than used as bare float/uint8_t/uint16_t — grepping for buf_u8 finds every
// pixel-data use of the type, not every unrelated uint8_t in the codebase.
// Names mirror pixel_dtype's enumerators below (f32/u8/u16) by design.
using buf_f32 = float;
using buf_u8 = std::uint8_t;
using buf_u16 = std::uint16_t;

// The only element types vc_pixel_buffer can store. Constrains the
// constructor and as<T>() so a request for an unsupported type fails to
// compile instead of compiling and throwing at runtime.
//
// Kept in sync by hand with pixel_dtype and pixel_buffer below — adding a
// dtype means updating all three declarations (the static_asserts on
// pixel_buffer catch an enum/variant order mismatch, but not a missing or
// extra concept entry).
template <typename T>
concept vc_pixel_element = std::same_as<T, buf_f32> ||
                           std::same_as<T, buf_u8> || std::same_as<T, buf_u16>;

// Fixed-size, runtime-typed pixel storage. Composes a variant of vectors but
// exposes no resize()/clear()/push_back() on any of them — a vc_image's
// width*height*channels == size() invariant is enforced by construction, not
// caller discipline. See docs/coding_guidelines.md Sec 4.2.
//
// dtype is a runtime property (decided when a file is decoded), not a
// compile-time one, so this is one concrete class over a variant rather than
// a templated vc_pixel_buffer<T> — templating would force the type parameter
// onto vc_image and everything that touches it. See Sec 3.1.
class vc_pixel_buffer {
  public:
    // dtype is inferred from fill's type — no separate enum parameter that
    // could disagree with it.
    template <vc_pixel_element T>
    vc_pixel_buffer(std::size_t count, T fill)
        : data_(std::vector<T>(count, fill)) {
    }

    pixel_dtype dtype() const noexcept {
        return static_cast<pixel_dtype>(data_.index());
    }

    // Element count, independent of dtype.
    std::size_t size() const noexcept {
        return std::visit([](const auto& v) { return v.size(); }, data_);
    }

    // Typed access — the primary paradigm for reading/writing pixel data.
    // Not visit(): float and uint8_t pixel math have different range and
    // overflow semantics, so an imaging algorithm is normally written
    // against one concrete dtype as a precondition, not as logic meant to
    // work identically across dtypes. Returns a span, not vector<T>&, so a
    // caller gets element access but not resize()/clear() on the backing
    // storage — same invariant this class exists to enforce, one layer in.
    template <vc_pixel_element T> std::span<T> as() {
        if (!std::holds_alternative<std::vector<T>>(data_)) {
            throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                                   "vc_pixel_buffer::as<T>(): requested dtype "
                                   "does not match stored dtype");
        }
        return std::span<T>{std::get<std::vector<T>>(data_)};
    }
    template <vc_pixel_element T> std::span<const T> as() const {
        if (!std::holds_alternative<std::vector<T>>(data_)) {
            throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                                   "vc_pixel_buffer::as<T>(): requested dtype "
                                   "does not match stored dtype");
        }
        return std::span<const T>{std::get<std::vector<T>>(data_)};
    }

  private:
    // Named so the member declaration below reads as one type instead of a
    // repeated template instantiation every time this variant is mentioned.
    using pixel_buffer = std::variant<std::vector<buf_f32>,
                                      std::vector<buf_u8>,
                                      std::vector<buf_u16>>;

    // dtype() casts data_.index() straight to pixel_dtype (see comment above
    // the enum) — these pin that cast to the variant's actual alternative
    // order, so a reorder becomes a compile error instead of a silently wrong
    // dtype() result.
    static_assert(std::is_same_v<std::variant_alternative_t<0, pixel_buffer>,
                                 std::vector<buf_f32>>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, pixel_buffer>,
                                 std::vector<buf_u8>>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, pixel_buffer>,
                                 std::vector<buf_u16>>);

    pixel_buffer data_;
};

} // namespace vc
