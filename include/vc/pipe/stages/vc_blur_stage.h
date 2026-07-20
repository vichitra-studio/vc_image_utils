// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <tuple>

#include "vc/pipe/i_pipe.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/vc_param_schema.h"

namespace vc {
class vc_image; // named only in the typed slot descriptors below
} // namespace vc

namespace vc::edit {
class vc_edit_session; // only used by reference in from_session() below
} // namespace vc::edit

namespace vc::pipe {

// The config a blur stage reads — a plain typed params struct, held by the
// stage and read directly in process() (`params_.radius` — zero indirection,
// no variant bag). These are a TOY algorithm's knobs, NOT a user-facing edit
// setting: the point is a worked reference for giving ANY stage serializable
// params. (A user-facing knob would instead live in vc_edit_document.)
struct vc_blur_params {
    double radius = 1.0;     // blur radius / sigma, in source pixels
    bool normalize = true;   // divide by the kernel weight (energy-preserving)

    // The schema is a static member FUNCTION (not a data member): a static
    // data member initializer would need `&vc_blur_params::radius` while the
    // class is still mid-definition (incomplete), which is ill-formed; a
    // member-function BODY is only instantiated after the class is complete,
    // so it sees a fully-defined vc_blur_params.
    static constexpr auto schema() {
        return std::tuple{
            vc::params::vc_param_field{"radius", &vc_blur_params::radius, 0.0, 100.0},
            vc::params::vc_param_field{"normalize", &vc_blur_params::normalize},
        };
    }
};

// Pin the contract at compile time: vc_blur_params must satisfy
// vc::params::vc_param_struct (i.e. it has a schema()).
static_assert(vc::params::vc_param_struct<vc_blur_params>);

// A TOY blur stage (image -> image) — the WORKED REFERENCE for a stage that OWNS
// params. Its structure (ctor stores params, kind(), declare()) is written;
// process() — the actual blur kernel — is YOUR rep to implement and test (see the
// .cpp). Follows vc_passthrough_stage's shape exactly, plus a params member.
class vc_blur_stage : public i_pipe {
  public:
    // Params are resolved BEFORE construction and captured here: the
    // stage is stateless per run, so process() needs no param argument.
    vc_blur_stage(stage_name name, vc_blur_params params);

    // This stage's typed slots — the ONE place its ports are named and typed.
    struct slots {
        static constexpr slot<vc::vc_image> in{"in"};
        static constexpr slot<vc::vc_image> out{"out"};
    };

    const char* kind() const override;
    void declare(contract_builder& contract) const override;
    void process(vc_pipe_context& context) const override;

    // The resolved config this instance was built with (read in process()).
    const vc_blur_params& params() const noexcept {
        return params_;
    }

    // Derives this stage's params from an edit session — the builder
    // `vc_stage_registry::register_stage` wants for the "blur" kind (see
    // vc_stage_registry.h). Kept as a static on the stage itself, next to
    // the params it produces, rather than as a free function elsewhere.
    // Only declared here: `vc_edit_session` is forward-declared above and
    // its full definition is only needed in the .cpp that implements this.
    // TODO(you): read whatever slice of the session holds the blur's
    // user-facing knob(s) and return the resolved vc_blur_params. Throwing
    // shell for now — this is YOUR rep.
    static vc_blur_params from_session(const vc::edit::vc_edit_session& session);

  private:
    vc_blur_params params_;
};

} // namespace vc::pipe
