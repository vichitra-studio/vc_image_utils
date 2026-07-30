// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>
#include <type_traits>
#include <utility>

namespace vc::utils {

// Shared base for vc::utils::log::log_info_builder and vc::debug::
// dump_image_builder — the one place a deferred callable (F&&) gets resolved
// into std::optional<T>: the info a derived class builds, gated by
// should_build(), a subsystem-specific condition each derived class
// defines. Every override should fold in this instance's own
// tag_enabled() (see log_info_builder/dump_image_builder) — the base doesn't
// enforce that itself, since should_build() has to stay a simple, opaque
// predicate to remain virtual (see below).
//
// dump_image_builder's inheritance from this base spans a package boundary
// deliberately: vc::debug is a top-level namespace with its own
// include/vc/debug and src/debug directories (docs/coding_guidelines.md
// Sec 2.1), while this base stays put in vc::utils. That split is fine —
// this base enforces SHAPE (the deferred-resolution mechanics below), not
// namespace co-location, and log_info_builder already inherits across the
// vc::utils/vc::utils::log boundary the same way. Do not "fix" this by
// dragging log_info_builder_base into vc::debug to sit next to its
// derived class.
//
// vc::utils::perf::scoped_timer does NOT use this — it only ever reports
// once, at destruction, using an enabled() answer already captured at
// construction, so it has no repeated, reusable decision to make the way
// log_info_builder/dump_image_builder do. See vc_perf.h's comment for why.
//
// operator() is a template (F is deduced per call site) and templates
// cannot be virtual — that's a hard C++ rule, not a design choice: virtual
// dispatch needs a fixed-size vtable, but a template is an open-ended
// family of functions the compiler can't enumerate in advance. should_build()
// has no arguments and isn't a template, so it CAN be virtual — that split
// is what makes this shareable at all: operator() is defined once, here,
// non-virtual, and calls the virtual should_build() to decide whether to
// proceed.
template <typename T> class log_info_builder_base {
  public:
    explicit log_info_builder_base(bool tag_enabled = true)
        : tag_enabled_(tag_enabled) {
    }
    virtual ~log_info_builder_base() = default;

    void set_tag_enabled(bool on) noexcept {
        tag_enabled_ = on;
    }
    bool tag_enabled() const noexcept {
        return tag_enabled_;
    }

    // F is deduced per call:
    //   - Pass a value directly (anything that constructs a T): cheap call
    //     sites, built unconditionally by the caller.
    //   - Pass a callable (usually a lambda) returning std::optional<T>:
    //     only invoked if should_build(), so expensive work is skipped
    //     entirely when filtered out. Returning std::nullopt from the
    //     callable skips this specific call for a reason this log_info_builder_base
    //     has no way to know about on its own.
    template <typename F> std::optional<T> operator()(F&& make_value) const {
        if (!should_build()) {
            return std::nullopt;
        }
        return resolve(std::forward<F>(make_value));
    }

  protected:
    // Pure virtual, no arguments — each derived class defines what
    // "should I proceed" means for its own subsystem.
    virtual bool should_build() const = 0;

    // Exposed to derived classes (not just used internally by operator())
    // so a derived class can implement a call with different gating rules
    // than should_build() — e.g. log_info_builder::temp(), which bypasses
    // tag_enabled() entirely and uses a different condition.
    template <typename F> static std::optional<T> resolve(F&& make_value) {
        if constexpr (std::is_invocable_v<F>) {
            static_assert(
                std::is_same_v<std::invoke_result_t<F>, std::optional<T>>,
                "deferred callable must return std::optional<T>");
            return make_value();
        } else {
            static_assert(std::is_constructible_v<T, F>,
                          "must be a callable, or something convertible to T");
            return T(std::forward<F>(make_value));
        }
    }

  private:
    bool tag_enabled_;
};

} // namespace vc::utils
