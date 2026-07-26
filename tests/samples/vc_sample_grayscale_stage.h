// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// A SAMPLE stage (image -> image): exercises the pipe framework's
// heterogeneous-type contract with a real, self-contained luminance kernel.
// Lives in tests/samples/, not the library — it is a worked example for
// pipe-framework mechanics, not a production filter, and deliberately has no
// dependency on vc_edit_session/vc_edit_document: its kernel has no knobs.
//
// KNOWN, DELIBERATE SIMPLIFICATION: like vc_sample_blur_stage, process()
// seals a fresh image without carrying the source's composed metadata, so the
// output's metadata is null. Here the right answer is genuinely ambiguous —
// a 1-channel luminance plane may legitimately BE a mask (which is exactly
// what null metadata denotes, per vc_image_info.h) — so a production version
// must decide deliberately rather than inherit this by accident.
class vc_sample_grayscale_stage : public i_pipe {
  public:
    explicit vc_sample_grayscale_stage(stage_name name);

    // This stage's typed slots — the ONE place its ports are named and typed.
    struct slots {
        static constexpr slot<vc::vc_image> rgb{"rgb"};
        static constexpr slot<vc::vc_image> grey{"grey"};
    };

    const char* kind() const override;
    std::size_t params_hash() const override;
    void declare(vc_pipe_contract& contract) const override;

  private:
    void validate_inputs(const vc_pipe_context& context) const override;
    void do_process(vc_pipe_context& context) const override;
};

} // namespace vc::pipe
