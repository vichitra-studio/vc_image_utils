<!-- Copyright (c) 2026 Shantanu Agarwal -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# vc::pipe walkthrough

Two self-contained HTML pages that explain the `vc::pipe` framework visually.
Open either file directly in a browser (no build step, no assets).

- **`pipe_schematic.html`** — the system schematic: the parts (`i_pipe`,
  `slot<T>`, `stage_port`, `vc_pipe_packet`, `vc_pipe_contract`,
  `vc_pipe_context`, `vc_pipeline`), how they relate, and the two-phase
  *assemble → validate → run* wiring on a `grayscale → mean_brightness` chain.
- **`pipe_mechanics.html`** — the implementation mechanics: the compile-time
  `slot<T>` → erased `slot_decl` boundary, each subsystem's internals, a full
  data-structure trace of a run, and correct current-API construction/run usage.

Both track the settled design in [`../pipe_design.md`](../pipe_design.md) §12.
The code samples are illustrative (e.g. `vc_blend_stage` is hypothetical); the
compiled reference is `src/pipe/stages/vc_passthrough_stage.cpp` and
`tests/test_vc_pipe.cpp`.
