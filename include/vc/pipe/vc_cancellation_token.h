// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace vc::pipe {

// The shared mutable cell a source and its tokens all alias. Every token a
// source hands out holds a COPY of the same shared_ptr — copying a shared_ptr
// bumps its refcount, it does not clone the atomic<bool> it points at — so
// vc_cancellation_source::cancel() flips ONE object and every outstanding
// token/context/pipe_context copy derived from it (however many by-value
// copies deep) observes the flip the next time it calls cancelled(). Nothing
// ever rebinds a token to a different flag after construction; propagation
// works entirely through this aliasing, not through mutating a token itself.
using cancellation_flag = std::shared_ptr<std::atomic<bool>>;

// Cooperative cancellation for a pipeline run. A `vc_cancellation_source`
// owns the flag and flips it; a `vc_cancellation_token` is a cheap, copyable
// observer the runner checks BETWEEN stages (and, [LATER], at checkpoints
// inside long-running stages). The motivating case is interactive re-render:
// a drag cancels the in-flight render and restarts it.
//
// The flag is held behind a cancellation_flag (shared_ptr<atomic<bool>>), so
// a caller cancelling from another thread and the runner observing the token
// race only on the atomic — safe by construction. This is fully-written
// plumbing; the only rep is the between-stage check the user writes in
// vc_pipeline::run() (see the guiding comment there).
class vc_cancellation_token {
  public:
    // Never-cancelled default: a run() call that omits a token binds this and
    // never observes cancellation (the null flag reads as "not cancelled").
    vc_cancellation_token() = default;

    // Observe the shared flag. Cheap and const — the runner may call it once per
    // stage. A default-constructed token (null flag) is always "not cancelled".
    bool cancelled() const noexcept {
        return flag_ && flag_->load();
    }

  private:
    // Only a source can bind a live flag to a token; callers get tokens through
    // vc_cancellation_source::token(), never by constructing one with a flag.
    friend class vc_cancellation_source;
    explicit vc_cancellation_token(cancellation_flag flag)
        : flag_(std::move(flag)) {
    }

    cancellation_flag flag_;
};

// The write end: owns the flag, hands out observing tokens, and flips the flag
// on cancel(). Held by whoever controls a render (the UI/CLI driver); its tokens
// travel into run().
class vc_cancellation_source {
  public:
    vc_cancellation_source() : flag_(std::make_shared<std::atomic<bool>>(false)) {
    }

    // A fresh observer bound to this source's flag. All tokens from one source
    // observe the same flag, so one cancel() is seen by every outstanding token.
    vc_cancellation_token token() const noexcept {
        return vc_cancellation_token{flag_};
    }

    // Request cancellation. Idempotent; safe to call from another thread.
    void cancel() noexcept {
        flag_->store(true);
    }

  private:
    cancellation_flag flag_;
};

} // namespace vc::pipe
