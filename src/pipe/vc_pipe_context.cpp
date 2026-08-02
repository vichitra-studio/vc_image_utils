// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipe_context.h"

#include <utility>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"

namespace vc::pipe {

// Private lowering targets — reachable only through the typed
// get_input(slot<T>) / set_output(slot<T>) methods. The runner never calls
// these: it hands inputs in through the constructor and pulls outputs out
// through take_outputs().

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

void vc_pipe_context::set_output(const slot_name& name, vc_pipe_packet packet) {
    outputs_.insert_or_assign(name, std::move(packet));
}

} // namespace vc::pipe
