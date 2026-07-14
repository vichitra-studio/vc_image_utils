// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipe_context.h"

#include <utility>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::pipe {

const vc_pipe_packet& vc_pipe_context::get_input(const slot_name& name) const {
    const auto it = inputs_.find(name);
    if (it == inputs_.end()) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_pipe_context::get_input(): no packet bound to input slot '" +
                name + "'");
    }
    return it->second;
}

void vc_pipe_context::set_input(const slot_name& name, vc_pipe_packet packet) {
    inputs_.insert_or_assign(name, std::move(packet));
}

void vc_pipe_context::set_output(const slot_name& name, vc_pipe_packet packet) {
    outputs_.insert_or_assign(name, std::move(packet));
}

const vc_pipe_packet& vc_pipe_context::get_output(const slot_name& name) const {
    const auto it = outputs_.find(name);
    if (it == outputs_.end()) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "vc_pipe_context::get_output(): no packet produced on output slot "
            "'" +
                name + "'");
    }
    return it->second;
}

bool vc_pipe_context::has_output(const slot_name& name) const noexcept {
    return outputs_.find(name) != outputs_.end();
}

} // namespace vc::pipe
