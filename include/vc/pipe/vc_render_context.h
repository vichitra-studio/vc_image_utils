// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <utility>

#include "vc/pipe/vc_cancellation_token.h"

namespace vc::pipe {

// The CONTROL-ONLY host for a pipeline run. Cancellation lives here now; a
// progress-reporting seam is [LATER]. Deliberately does NOT hold the
// derived-data cache — control and cache are separate concerns, threaded
// independently into run() (the cache reaches a stage through
// vc_edit_session/build_pipeline, not through here). This SUPERSEDES the
// bare `vc_pipeline::run(inputs, const vc_cancellation_token&)` signature.
//
// A cheap, copyable VALUE (it wraps a vc_cancellation_token, itself a cheap
// copyable observer over a shared atomic flag — see vc_cancellation_token.h)
// so it can be copied into a vc_pipe_context per stage without ceremony.
class vc_render_context {
  public:
    // A default context wraps a never-cancelled token (a default
    // vc_cancellation_token has a null flag => always "not cancelled"), so
    // a run() call that omits a context behaves exactly as one that omitted
    // a bare token before this milestone.
    vc_render_context() = default;

    // Wrap an existing observer token (obtained from a
    // vc_cancellation_source held by whoever controls the render and may
    // request cancellation).
    explicit vc_render_context(vc_cancellation_token token)
        : token_(std::move(token)) {
    }

    // Read-only face exposed to the pipeline / a stage: has this run been
    // asked to stop? Delegates to the wrapped token. Cheap and const — may
    // be called once per stage (vc_pipeline::run()) or, now that the
    // context threads into vc_pipe_context, from inside a stage's process()
    // body too.
    bool cancelled() const noexcept {
        return token_.cancelled();
    }

    // [LATER] progress-reporting seam: a callback/handle a long stage could
    // report fractional progress through. Not built now — the host stays
    // control-only until a consumer of progress exists.

  private:
    vc_cancellation_token token_;
};

} // namespace vc::pipe
