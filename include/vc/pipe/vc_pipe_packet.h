// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/vc_any_box.h"

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
// Backed by vc::vc_any_box (include/vc/vc_any_box.h), the mechanism shared
// with vc::vc_metadata_value; get<T>()'s mismatch message names this
// type via the tag below. In a graph that cleared assembly-time validation
// (Sec 6) get<T>() never throws, because the connected types were already
// checked — but it is a runtime cast, not a compile-time guarantee on its
// own.
struct vc_pipe_packet_tag {
    static constexpr const char* type_name = "vc_pipe_packet";
};
using vc_pipe_packet = vc::vc_any_box<vc_pipe_packet_tag>;

} // namespace vc::pipe
