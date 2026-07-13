// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "vc/utils/vc_info_builder.h"
#include "vc/utils/vc_strings.h"

namespace vc::utils::log {

// Severity, ascending — declaration order matters, should_log() compares
// levels with a plain >=.
enum class level : std::uint8_t {
    debug,   // verbose diagnostic detail — opt in per call site, see below
    info,    // normal operational messages
    warning, // recoverable anomaly, operation continued
    error,   // operation failed, caller should see it
};

// ---------------------------------------------------------------------
// Primitives — process-wide state, owned entirely by vc_log.cpp as
// translation-unit-local statics (never exposed as a global/extern, no
// class-based singleton: there's no behaviour to encapsulate, just two
// scalars). Single-threaded assumption: call the setters once at startup,
// before any logging happens on another thread — no synchronisation here.
// ---------------------------------------------------------------------

// TODO(you): implement in src/utils/vc_log.cpp.
void set_enabled(bool on);
bool enabled() noexcept;

void set_min_level(level lvl);
level min_level() noexcept;

// enabled() && lvl >= min_level(). Not used by log_builder (see below) —
// exposed so debug()/info()/warning()/error() can filter by level
// themselves.
bool should_log(level lvl) noexcept;

// ---------------------------------------------------------------------
// Public logging API — five plain functions, one shape: a tag and an
// already-resolved, possibly-empty message. A nullopt msg is silently
// discarded. These are NOT templates — building the message (including any
// deferred, only-pay-if-needed computation) is log_builder's job below —
// so their definitions live entirely in vc_log.cpp. Nothing analogous to a
// lower-level "emit" primitive needs to be declared in this header at all.
//
// debug()/info()/warning()/error() each check should_log() for their own
// level before writing — this is where level-based filtering actually
// happens (see log_builder's comment below for why it isn't done any
// earlier), and temp() checks enabled() the same way.
// ---------------------------------------------------------------------

void debug(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg);
void info(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg);
void warning(const vc::utils::string& tag,
             const std::optional<vc::utils::message>& msg);
void error(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg);

// Always emitted whenever enabled() is true — ignores log_builder's
// tag_enabled entirely. For short-lived, ad-hoc debugging; grep
// "vc::utils::log::temp(" before committing to catch stragglers.
void temp(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg);

// ---------------------------------------------------------------------
// log_builder — the one place a deferred message gets resolved, built on
// vc::utils::info_builder<vc::utils::message> (see vc_info_builder.h for
// operator()/resolve()). should_build() checks this instance's
// tag_enabled() and enabled() — nothing about level. Level isn't a
// condition the builder machinery understands at all; min_level filtering
// happens in debug()/info()/warning()/error() themselves, each re-checking
// should_log() for its own level regardless of how the message was built.
// That means a message that gets filtered out by level was still fully
// built first — a deliberate simplification, not an oversight: only
// tag_enabled and the master switch skip building the message early.
//
//   vc::utils::log::log_builder builder;
//   ...
//   vc::utils::log::debug("resize",
//       builder([&]() -> std::optional<vc::utils::message> {
//           return "...";
//       }));
// ---------------------------------------------------------------------

class log_builder : public vc::utils::info_builder<vc::utils::message> {
  public:
    using info_builder::info_builder;

    // temp()'s rules — only the global enabled() switch, ignoring
    // tag_enabled (matching vc::utils::log::temp() above) — so it can't
    // reuse operator()'s should_build() gate. Kept as a named method
    // rather than relying on operator() so the different semantics stay
    // visible at the call site.
    template <typename F>
    std::optional<vc::utils::message> temp(F&& make_message) const {
        if (!enabled()) {
            return std::nullopt;
        }
        return resolve(std::forward<F>(make_message));
    }

  private:
    bool should_build() const override {
        return tag_enabled() && vc::utils::log::enabled();
    }
};

} // namespace vc::utils::log
