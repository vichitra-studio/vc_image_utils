// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// The config a sample blur stage reads — a plain typed params struct, held
// by the stage and read directly in process() (`params_.radius` — zero
// indirection, no variant bag). These are a TOY algorithm's knobs, hardcoded
// by the caller at construction — NOT derived from an edit session (see the
// header comment on vc_sample_blur_stage below).
struct vc_sample_blur_params {
    double radius = 1.0;   // blur radius, in source pixels
    bool normalize = true; // divide by the kernel weight (energy-preserving)
};

// A SAMPLE stage (image -> image): a box blur, the worked reference for a
// stage that OWNS params and reads a pixel neighbourhood. Lives in
// tests/samples/, not the library — a worked example, not a production
// filter. Its params are hardcoded by whoever constructs it (see
// vc_sample_blur_params above) — deliberately no session-derived builder
// (contrast the production vc_stage_registry session-aware registration
// path, which a real edit-aware stage would use instead).
//
// KNOWN, DELIBERATE SIMPLIFICATION: process() seals a fresh image and does
// NOT carry the source's composed metadata across
// (out.set_metadata(src.meta().metadata())), so the output's metadata is
// null. Per vc_image_info.h that reads as "this image is a mask", which a
// blurred photo is not — a PRODUCTION image->image stage should propagate it.
// Left as-is here to keep the sample focused on the kernel; do not copy this
// part of the pattern. (vc_passthrough_stage does not have the issue: it
// republishes the same vc_image, metadata included.)
class vc_sample_blur_stage : public i_pipe {
  public:
    // Params are resolved BEFORE construction and captured here: the
    // stage is stateless per run, so process() needs no param argument.
    // Throws vc::vc_exception if params.radius is not a positive, non-NaN
    // value — radius is fully known at this point (it never depends on a
    // run-time input), so it is validated HERE rather than deferred to
    // validate_inputs()/process() time.
    vc_sample_blur_stage(stage_name name, vc_sample_blur_params params);

    // This stage's typed slots — the ONE place its ports are named and typed.
    struct slots {
        static constexpr slot<vc::vc_image> in{"in"};
        static constexpr slot<vc::vc_image> out{"out"};
    };

    const char* kind() const override;
    std::size_t params_hash() const override;
    void declare(vc_pipe_contract& contract) const override;

    // The resolved config this instance was built with (read in process()).
    const vc_sample_blur_params& params() const noexcept {
        return params_;
    }

  private:
    void validate_inputs(const vc_pipe_context& context) const override;
    void do_process(vc_pipe_context& context) const override;

    vc_sample_blur_params params_;
};

} // namespace vc::pipe
