// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// A SAMPLE stage: image -> a NON-image (double) output — the heterogeneous
// type contract the pipe framework exists to support. Lives in
// tests/samples/, not the library — a worked example, not a production
// filter; no dependency on vc_edit_session/vc_edit_document.
class vc_sample_mean_brightness_stage : public i_pipe {
  public:
    explicit vc_sample_mean_brightness_stage(stage_name name);

    // This stage's typed slots — the ONE place its ports are named and typed.
    struct slots {
        static constexpr slot<vc::vc_image> image{"image"};
        static constexpr slot<double> mean{"mean"};
    };

    const char* kind() const override;
    std::size_t params_hash() const override;
    void declare(vc_pipe_contract& contract) const override;

  private:
    void validate_inputs(const vc_pipe_context& context) const override;
    void do_process(vc_pipe_context& context) const override;
};

} // namespace vc::pipe
