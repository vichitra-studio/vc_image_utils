// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::pipe {

// image -> scalar (an ANALYZER). Produces the mean brightness of the input
// image as a `double` on a NON-IMAGE output slot.
//
// This stage is why the generic type-erased design exists. A pure
// image->image interface (docs/pipe_design.md Sec 2.1) cannot express it: its
// output is not an image. Wiring it exercises a heterogeneous packet (double,
// not vc_image) travelling a real type contract — the exact case the plain
// `apply(image) -> image` shape rejects. declare() and process() are
// TODO(you).
class vc_mean_brightness_stage : public i_pipe {
  public:
    explicit vc_mean_brightness_stage(stage_name name);

    struct slots {
        static constexpr slot<vc::vc_image> image{"image"};
        static constexpr slot<double> mean{"mean"}; // <-- non-image payload
    };

    const char* kind() const override;
    void declare(vc_pipe_contract& contract) const override;
    void process(vc_pipe_context& context) const override;
};

} // namespace vc::pipe
