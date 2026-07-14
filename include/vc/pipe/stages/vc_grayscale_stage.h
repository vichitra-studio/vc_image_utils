// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// image -> image. Collapses an RGB image to a single-channel luminance image.
// A representative P1-era Transform stage. declare() and process() are
// TODO(you) — follow vc_passthrough_stage as the pattern.
class vc_grayscale_stage : public i_pipe {
  public:
    explicit vc_grayscale_stage(stage_name name);

    struct slots {
        static constexpr slot<vc::vc_image> rgb{"rgb"};
        static constexpr slot<vc::vc_image> grey{"grey"};
    };

    const char* kind() const override;
    void declare(vc_pipe_contract& contract) const override;
    void process(vc_pipe_context& context) const override;
};

} // namespace vc::pipe
