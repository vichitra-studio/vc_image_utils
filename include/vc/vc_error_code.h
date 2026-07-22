// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <string>

#include "vc/utils/vc_strings.h"

namespace vc {

namespace pipe {
class vc_render_context;
}

// Underlying type fixed to uint8_t: this enum is only ever compared and
// converted (to_int/to_error_code), never used in arithmetic — no
// promotion-warning risk, so the smaller type is free (see channel_count
// in vc_types.h for the opposite case, where arithmetic makes uint32_t
// the right choice).
enum class vc_error_code : std::uint8_t {
    file_not_found,   // path does not exist or process lacks read permission
    invalid_format,   // file header is not a recognised image format
    decode_error,     // format recognised but data is corrupt or unsupported
                      // variant
    encode_error,     // output could not be written (bad path, disk full,
                      // permissions)
    invalid_argument, // caller passed logically invalid data (zero dimensions,
                      // null buffer)
    stage_not_found,  // a stage name in a connection was not found in the graph
    stage_already_added, // vc_pipeline::add() was called twice with the same
                         // stage name
    slot_not_found,      // a slot name in a connection was not declared by the
                         // stage
    slot_already_declared,    // a stage's declare() called add_input_slot (or
                              // add_output_slot) twice for the same slot name
    pipe_connection_mismatch, // a connection's upstream output type does not
                              // match the downstream input type
    pipe_input_already_connected, // two connections both target the same
                                  // downstream (stage, slot) input port
    user_cancelled,               // the run() was cancelled by the user (via
                                  // vc_render_context::cancelled())
};

[[nodiscard]] int to_int(vc_error_code code) noexcept;
[[nodiscard]] vc_error_code
to_error_code(int value); // throws vc_exception if value is out of range

[[nodiscard]] vc::utils::string to_string(vc_error_code code) noexcept;

void throw_if_cancelled(const vc::pipe::vc_render_context& run_context);

[[noreturn]] void throw_pipe_run_error(const std::string& message);

} // namespace vc
