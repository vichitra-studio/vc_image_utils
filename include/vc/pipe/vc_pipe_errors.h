// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>

#include "vc/pipe/vc_render_context.h"

namespace vc::pipe {

// Pipe-layer error helpers: both functions construct and throw a core
// vc::vc_exception, but need vc::pipe::vc_render_context (throw_if_cancelled)
// or are only ever called from vc_pipeline::run() (throw_pipe_run_error), so
// they belong to the pipe layer, not core. The dependency direction here is
// pipe -> core (this header includes vc_render_context.h and the .cpp
// includes vc_error_code.h/vc_exception.h), which is correct — core must
// never depend on pipe (see vc_error_code.h, which used to forward-declare
// vc::pipe::vc_render_context before these two functions moved out).

// Throws vc::vc_exception(user_cancelled) if run_context has been cancelled.
void throw_if_cancelled(const vc_render_context& run_context);

// Throws vc::vc_exception(invalid_argument) with a "vc_pipeline::run() "
// prefix — the single error-raising path shared by vc_pipeline::run()'s
// several failure checks.
[[noreturn]] void throw_pipe_run_error(const std::string& message);

} // namespace vc::pipe
