// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_types.h"

namespace vc::pipe {

// The ONLY thing a stage's declare() is handed — a narrow, write-only VIEW over
// a vc_pipe_contract the framework owns. It forwards the two typed add_*_slot
// calls and exposes nothing else, so this is the whole author-facing contract
// surface: a stage names and types its ports through slot<T> and never reaches
// the contract's name-keyed query methods (input_slot_type, input_slot_names,
// ...), which are framework-internal and used only by vc_pipeline::validate()/
// run() (docs/pipe_design.md Sec 12.5).
//
// No ownership: the referenced contract outlives the builder. The pipeline
// creates a contract, wraps it in a builder for the declare() call, then
// queries the now-populated contract. Non-copyable so the reference cannot be
// smuggled out of declare().
class contract_builder {
  public:
    explicit contract_builder(vc_pipe_contract& contract) noexcept
        : contract_(contract) {
    }

    contract_builder(const contract_builder&) = delete;
    contract_builder& operator=(const contract_builder&) = delete;

    template <typename T> void add_input_slot(slot<T> s) {
        contract_.add_input_slot(s);
    }

    template <typename T> void add_output_slot(slot<T> s) {
        contract_.add_output_slot(s);
    }

  private:
    vc_pipe_contract& contract_;
};

} // namespace vc::pipe
