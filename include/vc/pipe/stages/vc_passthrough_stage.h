// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// The simplest possible pipe: copies its input image to its output unchanged
// (image -> image). Fully implemented as the WORKED REFERENCE that every other
// stage in this directory follows — read vc_passthrough_stage.cpp to see how
// declare() states a contract and process() reads/writes through typed slots.
class vc_passthrough_stage : public i_pipe {
  public:
    explicit vc_passthrough_stage(stage_name name);

    // This stage's typed slots — the ONE place its ports are named and typed.
    // Wiring and access refer to these ({stage, slots::out} ports,
    // ctx.get_input(slots::in)); nobody hand-types "in"/"out" (Sec 12.1).
    struct slots {
        static constexpr slot<vc::vc_image> in{"in"};
        static constexpr slot<vc::vc_image> out{"out"};
    };

    const char* kind() const override;
    void declare(vc_pipe_contract& contract) const override;

  private:
    void validate_inputs(const vc_pipe_context& context) const override;
    void do_process(vc_pipe_context& context) const override;
};

} // namespace vc::pipe
