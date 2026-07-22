// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/pipe/vc_pipe_contract.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

namespace vc::pipe {

bool vc_pipe_contract::has_slot(const std::vector<slot_decl>& slots,
                                const slot_name& name) {
    for (const slot_decl& existing : slots) {
        if (existing.name == name) {
            return true;
        }
    }
    return false;
}

std::optional<std::type_index>
vc_pipe_contract::find_slot_type(const std::vector<slot_decl>& slots,
                                 const slot_name& name) {
    for (const slot_decl& slot : slots) {
        if (slot.name == name) {
            return slot.type;
        }
    }
    return std::nullopt;
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
    if (auto type = find_slot_type(inputs_, name)) {
        return *type;
    }
    throw vc::vc_exception(vc::vc_error_code::slot_not_found,
                           "vc_pipe_contract: no input slot named '" + name +
                               "' was declared");
}

std::type_index
vc_pipe_contract::output_slot_type(const slot_name& name) const {
    if (auto type = find_slot_type(outputs_, name)) {
        return *type;
    }
    throw vc::vc_exception(vc::vc_error_code::slot_not_found,
                           "vc_pipe_contract: no output slot named '" + name +
                               "' was declared");
}

std::vector<slot_name> vc_pipe_contract::input_slot_names() const {
    return names(inputs_);
}

std::vector<slot_name> vc_pipe_contract::output_slot_names() const {
    return names(outputs_);
}

} // namespace vc::pipe
