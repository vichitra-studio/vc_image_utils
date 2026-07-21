// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>

#include "vc/io/vc_io_types.h"
#include "vc/utils/vc_info_builder.h"
#include "vc/utils/vc_strings.h"
#include "vc/vc_image.h"

// Unlike vc_types.h's forward declaration of vc_image (sufficient there,
// since shared_ptr only needs a reference),
// vc::utils::info_builder<vc::vc_image> (see vc_info_builder.h) uses
// std::is_constructible_v<vc::vc_image, F> — a type trait, not a reference
// binding. That check doesn't depend on F, so it's evaluated the moment this
// header is parsed, not deferred to template instantiation — which means
// vc::vc_image must be complete right here, not just at whatever call site
// eventually instantiates the template.

namespace vc::utils::debug {

// ---------------------------------------------------------------------
// Process-wide state, owned entirely by vc_image_dumper.cpp as
// translation-unit-local statics — same ownership model as vc::utils::log.
// Single filtering dimension here (on/off), so unlike vc::utils::log
// there's no tag/level system to configure: you either want dumps for this
// run or you don't.
// ---------------------------------------------------------------------

// Off by default — unlike logging, a dump call has real I/O cost (encodes
// and writes a file), so it must be opt-in.
void set_enabled(bool on);
bool enabled() noexcept;

// Directory dumped images are written into. Created on first dump() if it
// doesn't exist yet.
void set_output_dir(const vc::io::path& dir);
vc::io::path output_dir() noexcept;

// Writes `image` to output_dir()/NNN_label.png if present, where NNN is a
// zero-padded, globally-incrementing counter (not per-label) — so files
// sort into true call order in a file browser, e.g. 000_decode.png,
// 001_resize.png, 002_resize.png, 003_tonemap.png, revealing the
// pipeline's actual sequence even when the same label repeats. A nullopt
// image is silently discarded.
//
// Not a template — building the image (including any deferred, only-pay-
// if-needed computation) is dump_builder's job below, so this function's
// definition lives entirely in vc_image_dumper.cpp. Nothing analogous to a
// lower-level "write" primitive needs to be declared in this header at
// all.
//
// Failures (bad output_dir, disk full, encode failure) are NOT caught —
// deliberately: an unexpected dump failure crashes the run, surfacing the
// problem immediately rather than continuing on a partially-broken debug
// session (see write_dump() in vc_image_dumper.cpp).
void dump(const vc::utils::string& label,
          const std::optional<vc::vc_image>& image);

// ---------------------------------------------------------------------
// dump_builder — the one place a deferred image gets resolved, built on
// vc::utils::info_builder<vc::vc_image> (see vc_info_builder.h for
// operator()/resolve()). should_build() checks this instance's tag_enabled()
// and vc::utils::debug::enabled() — the same shape as log_builder.
//
//   vc::utils::debug::dump_builder builder;
//   ...
//   vc::utils::debug::dump("resize",
//       builder([&]() -> std::optional<vc::vc_image> {
//           return build_visualisation(buffer);
//       }));
//
// F is deduced per call:
//   - Pass a vc::vc_image directly: cheap call sites, where you already
//     have the exact buffer you want to inspect in hand.
//   - Pass a callable (usually a lambda) returning
//     std::optional<vc::vc_image>: only invoked if enabled() and this
//     instance's own tag_enabled hold, so building an expensive debug
//     visualisation (e.g. false-colouring or tone-mapping a raw/HDR
//     buffer into something viewable) is skipped entirely when dumps are
//     off. Return std::nullopt to skip this specific call for a reason
//     dump_builder has no way to know about on its own.
// ---------------------------------------------------------------------

class dump_builder : public vc::utils::info_builder<vc::vc_image> {
  public:
    using info_builder::info_builder;

  private:
    bool should_build() const override {
        return tag_enabled() && vc::utils::debug::enabled();
    }
};

} // namespace vc::utils::debug
