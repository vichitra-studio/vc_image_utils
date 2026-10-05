// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <string_view>

namespace vc::utils {

using string = std::string;  // general-purpose text
using message = std::string; // human-readable error or log text

// A NON-OWNING reference to text the callee only reads. Added 2026-10-05 for
// the same reason the element-count alias exists: coding_guidelines.md Sec 3
// forbids raw
// primitives in public signatures, and std::string_view was used raw in 21
// places with no alias -- including vc_io_fs.h's four `caller` parameters,
// which is the idiom vc::math::grid2d now follows.
//
// Named to parallel `string` above, which likewise shadows its std:: spelling.
// Use it for a parameter that does not outlive the call; use `string` when the
// callee has to keep the text.
using string_view = std::string_view;

} // namespace vc::utils
