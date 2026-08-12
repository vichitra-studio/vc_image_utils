// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <any>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

// Lives at vc/ (core), NOT vc/utils/: this box throws vc::vc_exception, and
// vc_error_code.h/vc_exception.h both include vc/utils/vc_strings.h, so
// vc::utils sits BELOW core in the dependency order. A throwing type placed
// in vc::utils would create a utils -> core -> utils cycle.
namespace vc {

// The identities a vc_any can carry — the CLOSED set of type-erased boxes in
// this system. The tag is what makes two boxes over one implementation into
// two unrelated types (see vc_any below), and nothing else.
//
// Deliberately closed, and deliberately here. `pipe_packet` means core names a
// concept from the layer above it, which is the one cost: adding a box kind
// means editing this core header, and the vc::pipe entry cannot be added by
// vc::pipe itself. Accepted because the alternative — a per-layer tag plus a
// traits specialization to carry each name — is materially more machinery for
// a two-entry vocabulary that has no reason to grow, and because an enumerator
// is a NAME, not a dependency: nothing in core includes or needs anything from
// vc::pipe. If this list ever reaches the point where downstream layers want
// to add their own kinds, the move is a declared-never-defined traits template
// (the vc_stage_params<StageT> idiom in vc/edit/vc_stage_params.h), which
// reopens the set without changing a single call site.
enum class vc_any_tag : std::uint8_t {
    meta_value,
    pipe_packet,
};

// A vc_any tag: a value OF the enum above, checked by type rather than by
// enumerating the members.
//
// Deliberately NOT a list of the valid enumerators. That shape —
// vc_pixel_element_req's (vc/vc_pixel_buffer.h), which spells out every
// allowed alternative — is right there because pixel_dtype, its variant and
// its concept are three independent declarations that can drift; here the
// enum IS the list, so restating it would create the only drift hazard in
// sight rather than guarding one. Range is enforced instead by
// vc_any_tag_name() below, which has no fallback: an unnamed tag stops vc_any
// at instantiation (see the static_assert in the class) and stops a direct
// caller with an exception.
template <typename TagT>
concept vc_any_tag_req = std::same_as<TagT, vc_any_tag>;

// The alias name a box reports for itself in get<T>()'s mismatch message —
// what the caller actually wrote (vc_pipe_packet), not the shared underlying
// template.
//
// THROWS on an unnamed tag rather than returning a placeholder. A scoped enum
// with a fixed underlying type admits every value in that type's range, not
// just the named ones, so `static_cast<vc_any_tag>(99)` is a legal argument —
// and a fallback string would let it through with silently degraded
// diagnostics, which is the failure mode this codebase keeps finding. Not
// noexcept for that reason.
//
// Being constexpr is what makes the throw a COMPILE-time guard and not merely
// a runtime one: a throw is not a constant expression, so vc_any's opening
// static_assert cannot be evaluated for an unnamed tag and the class never
// instantiates. See tests/test_vc_pipe.cpp for both halves.
//
// No `default:` label on purpose — -Wswitch then flags a newly added
// enumerator here, which is what keeps this function total as the enum grows.
constexpr std::string_view vc_any_tag_name(vc_any_tag tag) {
    switch (tag) {
    case vc_any_tag::meta_value:
        return "vc_metadata_value";
    case vc_any_tag::pipe_packet:
        return "vc_pipe_packet";
    }
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           "vc_any_tag_name(): not a named vc_any_tag value: " +
                               std::to_string(static_cast<int>(tag)));
}

// Shared type-erased single-value box behind vc::pipe::vc_pipe_packet and
// vc::vc_metadata_value: identical wrap/get<T>/has_value/type mechanism,
// member for member — the two only ever differed in the type name reported by
// get<T>()'s mismatch message, which now comes from the tag. Each
// instantiation is still its OWN distinct type: two different template
// arguments give two unrelated specializations, so a metadata value is not
// silently interchangeable with a pipe packet even though they share one
// implementation. tests/test_vc_pipe.cpp asserts exactly that.
//
// The parameter is `auto` + a concept rather than the equivalent
// `template <vc_any_tag Tag>`, which would enforce the same thing through the
// language. Spelling it as a concept costs one line and buys a named,
// greppable, separately assertable predicate — the diagnostic reads
// "constraints not satisfied" and names vc_any_tag_req, instead of a bare
// conversion error.
template <auto Tag>
    requires vc_any_tag_req<decltype(Tag)>
class vc_any {
    // The RANGE check, and the reason vc_any_tag_name() throws instead of
    // returning a placeholder: a throw is not a constant expression, so an
    // unnamed tag makes this condition non-constant and vc_any<that tag> fails
    // to compile, naming the instantiation. The concept on the parameter
    // cannot do this job — it constrains the tag's TYPE, and every value in
    // vc_any_tag's underlying range has that type.
    //
    // It must be a static_assert and not merely kTagName's initializer below:
    // implicitly instantiating a class template instantiates the DECLARATIONS
    // of its static data members, not their initializers, so kTagName alone is
    // never evaluated for a specialization nobody reads it from — verified by
    // a probe TU that compiled clean until this assert was added.
    static_assert(!vc_any_tag_name(Tag).empty(),
                  "vc_any: Tag is not a named vc_any_tag value");

  public:
    vc_any() = default;

    // Wrap a value. The requires-clause stops this template from being
    // chosen in place of the copy/move constructors when T would deduce to
    // vc_any itself (the classic "greedy templated constructor" trap).
    template <typename T>
        requires(!std::is_same_v<std::decay_t<T>, vc_any>)
    explicit vc_any(T value) : value_(std::move(value)) {
    }

    // Unbox to T, read-only. Throws vc::vc_exception if the stored payload is
    // not a T.
    template <typename T> const T& get() const {
        const T* held = std::any_cast<T>(&value_);
        if (held == nullptr) {
            throw_type_mismatch();
        }
        return *held;
    }

    // Unbox to T, mutable — updates the stored value IN PLACE (e.g.
    // `box.get<Matrix>()(i, j) = x;`), rather than requiring a caller to copy
    // the whole T out, modify the copy, and wrap+assign a brand-new box over
    // it. For a payload where a copy is expensive (a matrix, a large
    // buffer), that round trip is a real, avoidable cost — this overload
    // avoids it the same way vc_pixel_buffer::as<T>() (vc_pixel_buffer.h)
    // pairs a mutable and a const accessor rather than offering only one.
    // Same mismatch contract as the const overload above.
    template <typename T> T& get() {
        T* held = std::any_cast<T>(&value_);
        if (held == nullptr) {
            throw_type_mismatch();
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
    // Shared by both get<T>() overloads above — neither's mismatch message
    // depends on T, so unlike vc_pixel_buffer's throw_on_mismatch<T>() (which
    // needs T to check holds_alternative<vector<T>>), this needs no template
    // parameter of its own. static because it reads only kTagName.
    //
    // Kept IN-CLASS, unlike vc_image_info::throw_out_of_range()
    // (vc_image_info.h), which outlines its twin into a .cpp to keep the
    // throw's machinery out of every including TU. That argument applies here
    // too — it is simply outranked: vc_any is a class template, so this member
    // cannot be outlined without inventing a non-template helper, and
    // vc_any.h's whole point is being header-only (docs/coding_guidelines.md
    // §9 records it as deliberately .cpp-free). One extra TU-local throw path
    // is the cheaper of the two costs.
    [[noreturn]] static void throw_type_mismatch() {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            std::string(kTagName) +
                "::get<T>(): requested type does not match the stored "
                "payload type");
    }

    // Resolved once, so get<T>()'s message costs no switch at run time. The
    // static_assert at the top of the class, not this, is what makes an
    // unnamed tag a compile error.
    static constexpr std::string_view kTagName = vc_any_tag_name(Tag);

    std::any value_;
};

} // namespace vc
