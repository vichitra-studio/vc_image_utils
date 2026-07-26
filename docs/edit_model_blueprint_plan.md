# Blueprint collation — every change from this conversation

> **[HISTORICAL RECORD]** This plan has been executed and superseded. For the current,
> authoritative design (including revisions made after this plan was written, e.g. the
> exiv2 exclusion widened from "desktop-only" to "every platform"), see
> `docs/edit_model.md`.

Purpose: a complete, de-duplicated inventory of everything decided in this session, so
nothing is dropped. Categorized as: **[DOC]** design to record in `docs/edit_model.md`,
**[NOW]** buildable scaffold, **[LATER]** documented seam (not built), **[REP]** learning-build
body the user writes, **[SUPERSEDES]** revises code already on disk, **[?]** open question.

Legend for confidence: every "current code" claim below was grounded against the tree today.

---

## PART A — Already on disk (this session), and what now supersedes it

Landed + verified earlier this session (build clean, tests 70/43/27):
- **Phase 1** — `vc_` prefix on all new-infra types; `vc_edit_session` ctor-injection; params
  `schema()` static member + `vc_param_struct` concept + `save/load` read `Params::schema()`;
  Kind-B `vc_metadata` interface + `vc_memory_metadata` backend; doc-citation removal.
- **Phase 2** — alias sweep: `vc_kv_bytes→kv_bytes`; added `stage_ptr`, `metadata_handle`,
  `content_hash`, `doc_key`; convention: classes `vc_`-prefixed, aliases no prefix.

**The design evolution now SUPERSEDES several of those. Explicit supersessions:**
1. **[SUPERSEDES]** `vc_edit_session` ctor `(source, edits, metadata_handle, vc_derived_store*)`
   → session now **owns** two derived stores + an edited-metadata overlay; the raw
   `vc_derived_store*` is gone. New member set (Part C-1).
2. **[SUPERSEDES]** single `vc_derived_store` (over `i_kv_store&`) → `i_derived_store`
   interface + two impls `vc_evictable_cache` + `vc_persistent_store` (Part C-2).
3. **[SUPERSEDES]** `vc_blur_stage::from_session` static shell → free builder function in the
   **edit** layer, supplied at registration; the static is REMOVED from the stage (Part C-4).
   → ⚠️ **Itself superseded (2026-07-26).** The builder-supplied-at-registration step was
   removed too. The session → params translation is now a **type-keyed trait**,
   `vc::edit::vc_stage_params<StageT>::from_session()` (`include/vc/edit/vc_stage_params.h`),
   looked up by `register_stage<StageT>(kind)` — which no longer takes a builder argument.
   Rationale: a builder argument could not *enforce* that a stage has a translation, and let
   one stage type acquire a different mapping at every call site.
4. **[SUPERSEDES]** `vc_pipeline::run(inputs, const vc_cancellation_token& = {})` → takes the
   run context instead (Part C-3).
5. **Still valid, unchanged:** `stage_ptr`, `content_hash` (now the cache key), `doc_key`,
   the `vc_param_struct` concept, `vc_metadata` interface + `vc_memory_metadata`.
6. **[?]** `metadata_handle` alias (`unique_ptr<vc_metadata>`): the session's overlay is
   *editable* (mutable) while the image-composed metadata is `shared_ptr<const vc_metadata>`
   — two handle types. Resolve which alias(es) survive (Part F, Q).
7. **`capture_settings` / `exposure_settings`** (questioned earlier, never closed): **DECIDED —
   keep as the worked edit-document slice example; no change.** Recorded here so it's not
   silently forgotten.

---

## PART B — Design decisions to record in docs/edit_model.md  [DOC]

The blueprint of record. None of these are "build now" unless also listed in Part C.

1. **Stage = pure function of slots (data) + params (config).** Sees no session, runner, or
   cache. Everything it needs is pushed in at build time.
2. **`build_pipeline` is the sole session-reader.** It wires slots + derives params; the
   stage only ever sees narrow slotted inputs.
3. **Session owns all durable state:** immutable `source`, `edits`, edited-metadata overlay,
   `vc_persistent_store`, `vc_evictable_cache`.
4. **Metadata stays separate from `vc_image_meta`** (forced by: EXIF orientation makes
   "display width" ≠ buffer width; exiv2 is GPL and must not enter the core value type).
   `vc_image_meta` **composes** `shared_ptr<const vc_metadata>` (physical dims always live
   in `vc_image_meta`). Backend is permissive + per-platform (Apple Image I/O / Android
   ExifInterface / desktop parser / DNG SDK), behind the interface, instantiated by the
   loader — **never exiv2**. Metadata rides the image → reaches stages via typed slots;
   a mask is an image whose composed metadata is null. Two layers: captured (immutable, on
   image) vs edited overlay (mutable, session), merged on export. `vc_metadata::field`/`value`
   stay `std::string` PLACEHOLDERS — typing them is [LATER] (pre-bakes the field-set decision).
5. **Image writer composes a ready `vc_image_meta`; it never parses EXIF** (SoC). Metadata
   construction is the loader's/backend's job, handed in.
6. **Two derived stores behind one `i_derived_store` interface:** `vc_evictable_cache`
   (reproducible, content-hash key, may evict, NOT saved) + `vc_persistent_store`
   (non-reproducible, id key, pinned, SAVED/bundled). Separate objects — differ in BOTH
   eviction AND serialization lifecycle. Access is source-agnostic (one interface + slots);
   a consumer can't tell which store produced a mask.
7. **Runner memoization:** stages are pure and never touch the cache. A cheap **chained-hash
   pre-pass at run start** computes each node's `hash(params-via-schema + input-hashes)`,
   decides hit/miss, and detects upstream changes automatically (a changed upstream hash
   propagates downstream). Then only misses run — the runner runs a missing input's
   **producer** (the upstream stage on that connection) on demand. Params supply the cache
   **key** (via `schema()`), never the cache store.
8. **Run context (host):** control-only — cancellation now, progress [LATER]. Does NOT hold
   the cache. Threaded `run() → …`.
9. **Cancellation:** keep the value-semantic `token`/`source` split (read = `token`, write =
   `source`), justified by least-privilege + CQS; wrap it in the run context. Do NOT switch to
   base/derived inheritance (loses cheap copyable tokens, adds a virtual call).
10. **Registry-erased construction:** `register_stage<StageT>(kind, builder)` captures the
    concrete type + its edit-layer params builder + ctor into a uniform
    `create(kind, name, session) -> stage_ptr`. No casts; concept-enforced at registration.
    → ⚠️ **Amended (2026-07-26):** the `builder` argument was dropped; the translation is
    looked up from the `vc_stage_params<StageT>` trait. Everything else here still holds.
    Params stay concrete (Design A); the `vc_param_struct` concept is the standardization,
    NOT a runtime params base class. The **schema** is the type-erased view for generic
    consumers (UI, serialization).
11. **Reader/writer standardized as CONCEPTS** (`vc_reader`/`vc_writer`), not one inherited
    interface. Two reader flavors: total-get (settings: miss→default) vs miss-exposing
    (cache: miss→optional). Keep `vc_doc_reader`/`vc_doc_writer` SPLIT (read-view over a
    `const` store / write-view over a mutable store) — do NOT merge.
12. **Two idioms, applied by driver:** (A) immutable-value + builder where *stable shared
    identity* is the driver (images, captured metadata, cached derived); (B) read/write
    capability split where *least-privilege/CQS* is the driver. Never smear immutability onto
    genuinely mutable state (edits). Standardize each idiom's expression.
13. **Disposable pipeline:** stage objects rebuilt cheaply per render; the expensive part
    (pixels) is skipped by the cache, not by persisting the graph. Expensive-construction
    stages (shader/LUT) → engine-level resource cache keyed by `(kind, params-hash)` [LATER].
14. **Serialization / portability:** source stays pristine. A JSON **sidecar** holds edits +
    metadata overlay + **ids** referencing derived data. A **companion blob store** beside the
    sidecar holds the persistent-derived bytes (NOT base64'd inline). **Export = one
    self-contained bundle** (edits + metadata + derived blobs). The evictable cache is never
    saved (optional private warm-start mirror only).
15. **Render-failure consistency:** edits are the source of truth; the rendered image is
    disposable. A failed/cancelled render never mutates edits; the preview is left stale
    (tracked by edit-state hash) and re-rendered; the cache retains every stage that
    succeeded so a retry recomputes only from the failure point.
16. **Naming / aliasing rule:** classes/structs/enums/concepts carry `vc_`; type aliases are
    lowercase concept names, no prefix. Anything that *means something in the system* gets a
    named type (reuse the vocabulary: `vc::utils::message`, `path`, `slot_name`, …, or add an
    alias at its seam); only incidental primitives (loop indices, transient bools) stay raw.
    **Weak `using` aliases only** — strong wrapper types were rejected as too much machinery
    for the goal; aliases are transparent to the type system (readability/refactorability, not
    type-safety).
17. **Test doubles never ship in library code:** pure test spies live under a dedicated test
    support area; an *in-memory backend* that is a legitimate runtime state (ephemeral/unsaved
    sessions) may stay in the lib. (Decision behind Part C-8.)

---

## PART C — Buildable-now scaffold  [NOW]  (my proposed subset — gated on Q4)

Cheap, low-risk structural pieces that don't require the [LATER] machinery. Each keeps the
learning-build split (interfaces + plumbing + throwing `TODO(you)` shells; user writes reps).

1. **`vc_edit_session` re-shape** [SUPERSEDES A-1]:
   ```
   source_ (immutable) | edits_ | metadata overlay | vc_persistent_store | vc_evictable_cache
   ```
   All owned. Ctor takes them (exact signature = Q). Getters inline. Move-only if it owns a
   `unique_ptr` handle.
   - **[?]** exact ctor arg list + whether stores are constructed by the session or injected.

2. **`i_derived_store` + two impls** [SUPERSEDES A-2]:
   - `i_derived_store` — the uniform contract (get/set-shaped; verbs = Q).
   - `vc_evictable_cache` — reproducible; in-memory now; eviction policy [LATER].
   - `vc_persistent_store` — non-reproducible; in-memory now; disk/bundle IO [LATER].
   - Bodies are [REP]/throwing shells where they'd otherwise need the real store logic.

3. **`vc_render_context` host** [SUPERSEDES A-4]:
   - Holds the cancellation `token` (read-only face exposed to the pipeline); progress seam
     [LATER]. Does NOT hold the cache.
   - `vc_pipeline::run(inputs, const vc_render_context&)` replaces the bare-token signature.
   - **[?]** does `vc_pipe_context`/`process()` receive the run context now (for future in-stage
     cancellation checkpoints), or stays runner-only until a long stage exists?
   - Alias the internal `shared_ptr<std::atomic<bool>>` (the token/source flag) — or drop it if
     the host makes it unnecessary (the user flagged the missing typedef).

4. **Registry `from_session` erasure** [SUPERSEDES A-3]:
   - `register_stage<StageT>(kind, builder)` + `create(kind, name, session)`, concept-enforced.
   - Move `blur_params_from_session` to a free function in the **edit** layer as a [REP]
     throwing shell; REMOVE `vc_blur_stage::from_session` static.
   - ⚠️ **Superseded (2026-07-26):** LANDED, then replaced. The builder argument is gone;
     `register_stage<StageT>(kind)` reads `vc_stage_params<StageT>::from_session(session)`
     instead, enforced by the `vc_stage_params_req` concept. No free builder function exists.

5. **`vc_reader`/`vc_writer` concepts** — in a shared header; existing adapters made to
   conform; two reader flavors documented.

6. **`vc_image_meta` metadata composition:**
   - Add `shared_ptr<const vc_metadata>` member + accessor (fits the existing "add a
     descriptor field here" pattern). `vc_image_writer` gains a way to set it before `seal()`.
   - **[?]** build this now (touches the core image type) or keep [DOC] only for now?

7. **Naming finalization** (Q2): apply the ratified renames repo-wide.

8. **Test-infra separation** [decision behind B-17]: move pure test spies (e.g.
   `recording_store` in `test_vc_param_schema.cpp`, and any other test-only doubles) into a
   dedicated `tests/support/` area; **keep** `vc_memory_kv_store` / `vc_memory_metadata` in the
   library as runtime backends. Verify tests still green after the move.

---

## PART D — [LATER] seams (documented, NOT built now)

- Chained-hash memoization + the run-start hash pre-pass (runner incremental eval).
- Real derived-store backends: eviction/GC policy; disk + bundle IO.
- Metadata backends (Apple Image I/O / Android ExifInterface / desktop parser / DNG SDK).
- Sidecar read/write + export bundle packaging.
- Progress-reporting seam on the run context.
- Engine-level resource cache for expensive-construction stages.
- Edited-metadata-overlay ↔ captured-metadata merge-on-export.
- Scale-aware params, ROI/resolution, taps/injections (pre-existing [LATER]).

---

## PART E — Learning-build reps (user writes bodies)  [REP]

Existing (retype/relocate as noted): `build_pipeline`; `vc_doc_writer::set`/`vc_doc_reader::get`;
`i_derived_store` impls' get/set; `vc_pipeline::run()` cancellation check; `vc_blur_stage::process()`.
New/moved: `blur_params_from_session` (edit-layer builder). No rep body written by agents.

> ⚠️ **Stale (2026-07-26).** Of this list: `vc_pipeline::run()` (incl. the cancellation check) is
> implemented; `vc_blur_stage::process()` moved to `tests/samples/vc_sample_blur_stage.cpp` with
> its kernel written; `blur_params_from_session` was never created — the builder mechanism it
> belonged to was replaced by the `vc_stage_params<StageT>` trait. Still open reps:
> `build_pipeline`, the doc writer/reader set/get, and the two edit-table stores' get/set.

---

## PART F — Open questions / ambiguities  [?]  (need your call)

**Q1 (GATES Q3–Q5). Output & scope of THIS task** — (a) write the blueprint into
`docs/edit_model.md` + this change-plan only (no code yet); (b) that PLUS scaffold the Part-C
[NOW] subset via the agent review-fix loop; (c) other. If (a), Q3–Q5 are deferred until we
build; if (b), answer them too.

**Q1 — DECIDED:** document the blueprint into `docs/edit_model.md` (no code changes yet);
keep this collation as the change-plan. Build the Part-C subset in a later pass. Q3–Q5 deferred
until we build.

**Q2 — DECIDED (naming, to RECORD in the doc; applied to code only when we build):**
- `vc_field → vc_param_field`.
- The low-level byte-store family → **`vc_data*`**: `vc_kv_store → vc_data_store`,
  `vc_memory_kv_store → vc_memory_data_store`, `kv_bytes → data_bytes` (alias, no prefix).
- Store access verbs → **`get`/`set` everywhere** (the `optional`/fallback return already
  carries the miss semantics; the total-get vs miss-exposing distinction stays in
  comments/return types).

**Q3 (only if building now). Build-now subset** — which Part-C items: session re-shape +
two-store interface + run context + registry erasure + reader/writer concepts + test-infra
move, and separately the `vc_image_meta` metadata composition (touches the core image type)?

**Q4 (only if building now). Session ctor** — two stores **constructed by** the session (owned
by value) or **injected** (for test/mock substitution)? Metadata overlay a
`unique_ptr<vc_metadata>` or a concrete mutable type?

**Q5 (only if building now). run-context reach** — does `process()`/`vc_pipe_context` get the
run context now, or stay runner-only until a long-running stage needs in-process checkpoints?
