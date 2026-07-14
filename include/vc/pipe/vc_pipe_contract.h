// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <typeindex>
#include <typeinfo>
#include <vector>

#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// Collects the slot declarations ONE stage makes inside i_pipe::declare(): its
// own input and output slots, each a {name, payload-type} pair, and nothing
// else. A contract describes only what a stage consumes and produces — it has
// NO knowledge of what that stage is wired to. Wiring is graph-global topology
// that only exists once stages are assembled, so it lives on vc_pipeline, not
// here (docs/pipe_design.md Sec 6, Sec 12.2). The pipeline reads contracts back
// at assembly time to type-check every connection before any pixels flow.
//
// This is the "type layer" of the contract, and the only layer live now. The
// "data layer" — matching a vc_image_spec (planar/linear/float32, etc.) — is
// designed in Sec 5 but deliberately NOT built yet; it needs vc_image to first
// carry a spec ([LATER]). When it lands, add_*_slot grows a fluent return so a
// declaration can chain `.matching(spec)`; today it returns void.
class vc_pipe_contract {
  public:
    // Declare a slot. The name AND the payload type both come from the typed
    // descriptor (Sec 12.2) — there is no separate set_type step. Lowers the
    // descriptor to a private slot_decl; the compile-time type is erased to a
    // std::type_index here, at the authoring edge.
    template <typename T> void add_input_slot(slot<T> s) {
        inputs_.push_back(slot_decl{.name = slot_name{s.name},
                                    .type = std::type_index(typeid(T))});
    }

    template <typename T> void add_output_slot(slot<T> s) {
        outputs_.push_back(slot_decl{.name = slot_name{s.name},
                                     .type = std::type_index(typeid(T))});
    }

    // Type lookup by name. Returns the slot's declared payload type, or THROWS
    // vc::vc_exception if no such slot was declared — this project handles
    // errors with exceptions, not an optional the caller must branch on. Its
    // one caller is validate(), which compares output_slot_type(out) against
    // input_slot_type(in): a missing slot surfaces as the throw, so validate()
    // needs no not-found branch — only the real type comparison.
    std::type_index input_slot_type(const slot_name& name) const;
    std::type_index output_slot_type(const slot_name& name) const;

    // The declared slot NAMES, in declaration order. The map-variant run()
    // computes the graph's OPEN slots by subtracting the connected ports from
    // these — it needs names only, never the erased types (Sec 12.2). Also the
    // seam the CLI's introspection command will read.
    std::vector<slot_name> input_slot_names() const;
    std::vector<slot_name> output_slot_names() const;

  private:
    // The lowered, erased form of a slot<T> — name + payload type_index.
    // Private and never escapes: nothing outside the contract legitimately
    // names this type (Sec 12.1). The outside world sees only names and type
    // lookups. Direction is implicit in which vector holds the decl.
    struct slot_decl {
        slot_name name;
        std::type_index type;
    };

    // Return a slot's declared type by name, or throw if absent. `direction`
    // ("input" / "output") is only for the exception message.
    static std::type_index require_type(const std::vector<slot_decl>& slots,
                                        const slot_name& name,
                                        const char* direction);
    static std::vector<slot_name> names(const std::vector<slot_decl>& slots);

    std::vector<slot_decl> inputs_;
    std::vector<slot_decl> outputs_;
};

} // namespace vc::pipe
