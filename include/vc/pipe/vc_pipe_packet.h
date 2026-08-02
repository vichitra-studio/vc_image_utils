// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/core/vc_any.h"

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
// Backed by vc::vc_any (include/vc/core/vc_any.h), the mechanism shared with
// vc::vc_metadata_value; the tag is what keeps the two INDEPENDENT types
// rather than one, and is what get<T>()'s mismatch message names. In a graph
// that cleared assembly-time validation (Sec 6) get<T>() never throws, because
// the connected types were already checked — but it is a runtime cast, not a
// compile-time guarantee on its own.
using vc_pipe_packet = vc::vc_any<vc::vc_any_tag::pipe_packet>;

} // namespace vc::pipe
