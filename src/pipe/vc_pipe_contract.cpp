// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipe_contract.h"

#include <string>

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::pipe {

std::type_index
vc_pipe_contract::require_type(const std::vector<slot_decl>& slots,
                               const slot_name& name,
                               const char* direction) {
    for (const slot_decl& slot : slots) {
        if (slot.name == name) {
            return slot.type;
        }
    }
    throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                           std::string{"vc_pipe_contract: no "} + direction +
                               " slot named '" + name + "' was declared");
}

std::vector<slot_name>
vc_pipe_contract::names(const std::vector<slot_decl>& slots) {
    std::vector<slot_name> out;
    out.reserve(slots.size());
    for (const slot_decl& slot : slots) {
        out.push_back(slot.name);
    }
    return out;
}

std::type_index vc_pipe_contract::input_slot_type(const slot_name& name) const {
    return require_type(inputs_, name, "input");
}

std::type_index
vc_pipe_contract::output_slot_type(const slot_name& name) const {
    return require_type(outputs_, name, "output");
}

std::vector<slot_name> vc_pipe_contract::input_slot_names() const {
    return names(inputs_);
}

std::vector<slot_name> vc_pipe_contract::output_slot_names() const {
    return names(outputs_);
}

} // namespace vc::pipe
