# Edit Model — Scaffolding Plan (Phase 4)

> **[HISTORICAL RECORD]** This plan has been executed and superseded. Names,
> signatures, and rationale below may predate later renames/revisions (e.g. the
> `vc_`/`i_` interface-prefix convention, the exiv2 exclusion widening from
> "desktop-only" to "every platform"). For the current, authoritative design, see
> `docs/edit_model.md`; for the current code, see `include/vc/edit/`.
>
> **What this is.** A precise, durable scaffolding plan for standing up the
> non-destructive **edit model** (`docs/edit_model.md`) on top of the settled
> **execution substrate** (`docs/pipe_design.md`, `vc::pipe`). It names every new
> type/file, its exact intended interface, what is scaffolded vs left `TODO(you)`,
> and which `edit_model.md` section governs it.
>
> **This plan writes no code except itself.** It is the input to a separate
> implementation agent. It is a *learning-build* (§14/§17): Claude scaffolds
> interfaces + plumbing + `TODO(you)` shells; the user writes the rep logic.
>
> **Status tags** mirror the design docs: **[NOW]** built/settled · **[LATER]**
> deferred, seam only · **[OPEN]** genuinely undecided · **[CALL]** provisional.
> The quality bar applies to **scaffolding quality**, not to the deferred reps:
> `TODO(you)` shells, `[LATER]` seams, and red-by-design tests are **the plan**,
> not defects, and must be preserved — never "fixed" by writing rep logic.

---

## 1. Build baseline (the "before" state — verified)

Recorded so "no regressions" is checkable after implementation.

- **Compiles clean**: lib + app + tests.
- **doctest**: **54 test cases, 32 passed, 22 failed; 105 assertions, 74 passed,
  31 failed.**
- All 22 failures are **intentional unimplemented `TODO(you)` reps** (throw "not
  yet implemented" / return empty) — NOT regressions. Failing cases:
  `vc_image` zeros() validates+allocates / rejects invalid dimensions; stb
  round-trip JPEG→PNG; `vc_grayscale_stage` declares; `vc_mean_brightness_stage`
  declares; `vc_pipeline` run() (3 cases) + validate() rejects type-mismatch;
  `vc_image_dumper` (4); `vc_log` (7); `vc_perf` (2).

### 1.1 Expected post-scaffold totals (baseline hygiene)

The scaffold **adds new red-by-design tests** (§6 below) and does **not** touch
any existing red rep. After scaffolding, the *existing* 22 failures must remain
exactly 22 (any change there is a regression). The **new** file
`tests/test_vc_edit.cpp` adds green primitives plus red-by-design specs; its red
count is the human's edit-model worklist. Concretely, the scaffold should ship:

- **Green primitives** (pass immediately — fully-written plumbing, 6): `edit_document`
  defaults; `edit_session` composition/accessors; `memory_kv_store` put/get;
  `cancellation_token` cancel/observe; `render_request` defaults; `stage_registry`
  register→has→create round-trip (plumbing — see §4.6, [review: scope S3]).
- **Red-by-design specs** (fail until the user writes the rep, 4): `build_pipeline`
  assembles a pipeline from a session (~1); `doc_writer`/`doc_reader` round-trip
  (~1); `derived_store` miss-then-hit (~1); `run()` observes a pre-cancelled token
  (~1). *(The registry `create` was previously a 5th red spec; it is plumbing, not
  a rep, so it moves to green — [review: scope S3].)*

**Record the exact new totals when the scaffold lands** (this doc's §1 should be
updated with the measured numbers), so intended-red is distinguishable from an
actual regression. The invariant to check: *existing 22 failures unchanged; every
new failure traces to a named `TODO(you)` in `tests/test_vc_edit.cpp`.*

> **MEASURED (scaffold landed, 2026-07-17).** Post-scaffold doctest totals:
> **64 test cases, 38 passed, 26 failed; 138 assertions, 104 passed, 34 failed.**
> Delta from the pre-scaffold baseline (+10 cases, +6 passed, +4 failed): 6 new
> GREEN primitives (`edit_document`/`render_request`/`edit_session`/`memory_kv_store`/
> `cancellation_source`+token/`stage_registry` round-trip) and 4 new RED-by-design
> specs. The original 22 failures are **unchanged** (verified by name-diff, none
> flipped). The 4 new reds each trace to one named `TODO(you)`:
> `build_pipeline` (throwing shell) · `doc_writer`/`doc_reader` `set`/`get` ·
> `derived_store` `lookup`/`store` · `vc_pipeline::run()` cancellation check.
> Build is clean for lib + app + tests. (The `VC_BUILD_BENCHMARKS` build currently
> **fails** to compile in the pre-existing, unrelated `bench/vc_benchmark_pipe.cpp`
> — `vc_pipe_context` API drift: `has_output`/`set_input` no longer members — which
> is independent of the edit-model scaffold and out of its scope.) The only compiler
> warnings are the pre-existing `vc_log`/`vc_image_dumper`/`vc_perf` TODO-rep
> warnings — the edit-model scaffold adds none.

> **Line references are approximate — re-grep, don't trust them [review:
> doc-fidelity D-3 / code-recon].** The tree is uncommitted and has drifted since
> this plan was written. Verified stale: the three `run()` **call** sites are at
> `tests/test_vc_pipe.cpp:267,294,314` (not the 254/275/298 this doc cites — those
> are the `TEST_CASE` declaration lines), and `run()`'s signature is at
> `vc_pipeline.h:89` (not :88). Verified still-correct: `i_pipe.h:54` (`kind()`),
> `test_vc_image.cpp:39` (error round-trip), `CMakeLists.txt:80/123`. The
> implementer must re-locate every cited `file:line` at implementation time; the
> *substance* of each citation was checked and holds. **Snippet `#include` blocks
> are elided** throughout §3–§5 (as the real headers add them): each shown class
> needs the obvious standard headers — `<atomic>`/`<memory>` (cancellation),
> `<unordered_map>`/`<vector>`/`<cstddef>` (`memory_kv_store`), `<optional>`,
> `<functional>` (registry), `<cstdint>` (`render_request::roi`), `<string>` — plus
> the `vc/...` headers each type names. Add them when materializing each file.

---

## 2. Scope & principles

- **Learning-build split (§14, §17).** Claude scaffolds interfaces + framework
  plumbing (`edit_session`, `kv_store`/adapters, `metadata` interface, registry,
  `cancellation_token`, the serialization seam). The **user writes the rep logic**:
  `build_pipeline` (settings→stage derivations, §14), stage `params`/`process`
  bodies (§14), `validate()`/`run()` bodies (§14; already scaffolded-with-TODO in
  `vc::pipe`), **and the adapter bodies** `doc_writer`/`doc_reader`/`derived_store`.
  *Attribution note (**[review: doc-fidelity D-1]**):* §14 nominally lists the
  adapters on Claude's side ("`kv_store`/adapters"). This plan scaffolds their
  *interfaces + ctors* (Claude) but defers their *bodies* to the user — justified
  not by §14 but by **§4.4** (the mapping "which fields, ranges, versioning" is the
  hand-rolled domain value no library provides) and **§5.1** (the content-hash
  cache key is domain logic). The fully-mechanical `memory_kv_store` backend stays
  Claude-written plumbing, consistent with §14.
- **Scaffold-all-then-fill.** Stand up every seam first; fill reps after. Matches
  how `vc::pipe` already ships (green primitives + red-by-design reps on disk).
- **Breadth-of-seams / deferred-depth.** Every §12 **[NOW]** item gets an
  interface/shell; every §12 **[LATER]** item stays a seam/TODO — no
  implementation of derived-store eviction, chained-hash cache, buffer pooling,
  metadata backends, concurrency, resolution-aware scaling, taps/injections.
  `[OPEN]` items (§13) get thin placeholders that do not bake a shape a later
  decision reverses. *(Exception, 2026-07-17: **schema-driven serialization**
  (§18) was pulled forward from [LATER] by user request — the `vc::params`
  framework + a worked `vc_blur_stage` now ship. See edit_model.md §18 status
  note. Its persistence wiring through `doc_writer` still awaits that rep.)*
- **Spine-first.** Stand up the end-to-end path with one trivial stage before any
  periphery, so the whole model is exercised end-to-end early (§7 ordering).
- **Match existing idioms.** `vc::pipe` conventions are the template: `slot<T>`,
  `stage_port`, typed descriptors, `vc_pipe_packet` (`std::any`),
  `vc_exception`/`vc_error_code`, the `vc_pixel_element` concept, the
  `vc_image_writer`→`seal()` construction path, header forward-decls +
  `TODO(you)` bodies, `// Copyright … SPDX AGPL-3.0-only` headers, and the
  `struct slots { static constexpr slot<T> … }` pattern.

**Namespace & layout [CALL].** New edit-model types live in namespace **`vc::edit`**
under **`include/vc/edit/**`** + **`src/edit/**`**, mirroring the `vc::pipe` /
`include/vc/pipe` layout. The one exception is `cancellation_token`, which is an
*execution-substrate* concern and belongs in `vc::pipe` /
`include/vc/pipe/**` (it amends `vc_pipeline::run()`; see §5).

---

## 3. The spine (stand up FIRST, one trivial stage)

End-to-end path from `edit_model.md` §1:

```
edit_session ──► build_pipeline(edit_session, render_request) ──► vc_pipeline.run() ──► vc_image
```

The **trivial stage** for the spine is the existing, fully-implemented
`vc::pipe::vc_passthrough_stage` (`include/vc/pipe/stages/vc_passthrough_stage.h`)
— image→image, already green. `build_pipeline`'s body (the rep) assembles a
one-stage pipeline containing it, so the moment the user writes `build_pipeline`
+ the pipe-layer `run()`/`validate()` reps, the spine renders end-to-end. No new
stage is invented for the spine.

New spine files (5 headers, 2 cpp), each interface traced to a section:

### 3.1 `edit_document` — Kind A settings (§4.1) — [NOW, data; no rep]
`include/vc/edit/vc_edit_document.h` (header-only; plain data — no `.cpp`).

```cpp
namespace vc::edit {

// One HOME for a value many stages read (§4.1 cross-cutting). Plain struct.
struct capture_settings  { int bracket_count = 5; double ev_spacing = 2.0; };

// One member per user-facing tool. A stage receives a NARROW SLICE of the
// document, never the whole thing (§4.1, §16 L3/L4).
struct exposure_settings { bool enabled = true; double ev = 0.0; double black = 0.0; };

// The saveable, structured record of Kind-A edit settings (§4, §19). Typed,
// user-facing, organised by tool (RawTherapee ProcParams shape, minus the
// whole-struct coupling — §15.2). version = doc_version, reserved for migration
// (§9); engine/process version live on render_engine_version (§3.7), not here.
struct edit_document {
    int               version = 1;   // doc_version (§9) — reserved, unused now
    capture_settings  capture;       // cross-cutting (bracket_count etc.)
    exposure_settings exposure;      // one member per tool; grows additively
    // more tool slices added as stages arrive; hand-written JSON later (§4).
};

} // namespace vc::edit
```
Scaffolded: the structs + defaults. `TODO(you)`: none here — it is data; reps are
the *derivations* in `build_pipeline` that read these slices.

### 3.2 `render_request` — resolution/ROI seam (§8 "resolution awareness") — [LATER; design NOW]
`include/vc/edit/vc_render_request.h` (header-only).

```cpp
namespace vc::edit {

// The render REQUEST carries {target resolution / pyramid level, ROI} (§8). It
// is NOT source metadata — the source's vc_image_meta describes ITS geometry; a
// preview is a different image. Fields are DESIGNED now but INERT: stages are not
// resolution-aware yet and spatial params do not scale with level — that
// (scale-aware params, §17.6) is [LATER]. Consumed by build_pipeline, never by
// run() (see §5, amendment c).
struct render_request {
    // Pyramid level: 0 = full resolution (the default → preview == export).
    int level = 0;
    // Region of interest, in the canonical reference frame (§11). All-zero =
    // whole image. [OPEN] reference frame not yet pinned (§11, §13.6) — kept as
    // a plain rectangle placeholder that a later decision can reshape.
    struct roi { std::uint32_t x = 0, y = 0, width = 0, height = 0; } region;
};

} // namespace vc::edit
```
Scaffolded: the struct + defaults (full-res, whole-image). `TODO(you)`: none;
nothing reads `level`/`region` yet — the seam is inert by design.

### 3.3 `edit_session` — the per-image aggregate (§3) — [NOW, thin]
`include/vc/edit/vc_edit_session.h` + `src/edit/vc_edit_session.cpp`.

**Thin spine form** (source + edits only). The §3 sketch also composes `metadata`
and `derived_store`; those members are **added in the periphery step** when their
seam types exist (§4), because a thin spine must not carry a reference to a type
that does not yet exist. See **Risk R1** for the §3-vs-§6 member-shape correction.

```cpp
#include "vc/edit/vc_edit_document.h"   // held BY VALUE below ⇒ needs the full
                                        // definition, not a forward declaration
                                        // (an incomplete-type value member is
                                        // ill-formed). [review: code-recon C1]

namespace vc::edit {

class metadata;               // fwd — held by handle (unique_ptr), §4.5
class derived_store;          // fwd — held by handle (pointer), §4.4

// Everything about ONE image (§3). Scattered storage (A/B/C have different
// formats, durability, interop — §16 L9), UNIFIED for access here. This is what
// undo/redo snapshots (§10), portability bundles (§9), the UI binds to, and
// build_pipeline reads NARROW SLICES from (§4.1, §8). A facade (has-a), not a
// shared interface (is-a) (§7). [CALL] name — alternatives image_document /
// image_project (§13.7).
class edit_session {
  public:
    edit_session(vc_image source, edit_document edits);

    const vc_image&      source() const noexcept;   // immutable source (§1 inv.1)
    edit_document&       edits() noexcept;           // Kind A (+ C-nonrepro later)
    const edit_document& edits() const noexcept;

    // ── added in the PERIPHERY step (§4), not the thin spine (see R1) ──
    // Both Kind-B metadata and the Kind-C derived-store handle are attached
    // AFTER construction via setters (NOT ctor params), so the thin 2-arg ctor
    // stays valid through every milestone. The owning unique_ptr<metadata> makes
    // the aggregate MOVE-ONLY (see R1 / §10 note below). [review: scope S1]
    // metadata*       meta() noexcept;         // Kind B seam (§6); may be null
    // void            set_metadata(std::unique_ptr<metadata>) noexcept;
    // derived_store*  derived() noexcept;      // Kind C-repro handle (§5, §7); may be null
    // void            set_derived_store(derived_store*) noexcept;
    // ~edit_session(); edit_session(edit_session&&); operator=(edit_session&&);
    //   (destructor + move ops declared here, =default in the .cpp where metadata
    //    is complete — required for a unique_ptr over a forward-declared type.)

  private:
    vc_image      source_;   // immutable; shallow-shared via shared_ptr buffer
    edit_document edits_;    // by value; the source of truth for editing (§1)
    // std::unique_ptr<metadata> meta_;              // §4.5 / R1 — nullable handle
    // derived_store*            derived_ = nullptr; // §4.4 — nullable, setter-injected
};

} // namespace vc::edit
```
Scaffolded fully (composition + accessors are plumbing, not reps). `TODO(you)`:
none in the thin form.

### 3.4 `build_pipeline` — the translation layer (§4.2) — [NOW, hand-written; body is the user's rep]
`include/vc/edit/vc_build_pipeline.h` + `src/edit/vc_build_pipeline.cpp`.

```cpp
namespace vc::edit {

// The single place that speaks BOTH vocabularies: user settings <-> machine
// graph (§4.2). Reads NARROW SLICES from the session (§4.1), decides which
// stages exist (incl. paramless plumbing stages a tool depends on — e.g.
// deghost ⇒ align + compute_mask), how many (bracket_count ⇒ N aligns), wires
// them, and derives each stage's config. Structural/dependency knowledge lives
// HERE, imperatively (RawTherapee ImProcCoordinator, §15.2) — legitimate for a
// fixed pipeline; a stage cannot decide how many of itself exist. Cheap; rebuilt
// on demand (§1 inv.2). The pipeline is DISPOSABLE — never where edits live.
//
// TODO(you): the assembly is a learning rep. For the SPINE, assemble a single
// vc::pipe::vc_passthrough_stage and return the pipeline (add + connect nothing;
// its in/out become the graph's open input/output). Later reps read
// session.edits() slices and build real stages. `request` selects the render
// destination (§8): resolution/ROI (inert now, §3.2) and the terminal display
// transform (appended per output target — [LATER], §8).
vc::pipe::vc_pipeline build_pipeline(const edit_session& session,
                                     const render_request& request);

} // namespace vc::edit
```
Scaffolded: the signature + a `TODO(you)` body. **Prefer a throwing shell**
(`throw vc::vc_exception(... "build_pipeline not yet implemented")`) over
`return {}`: `vc_pipeline` exposes no public stage accessor, so a build_pipeline
spec test can only observe its result *through* `run()` (itself an unimplemented
rep). An empty-return shell entangles the failure with `run()`'s; a throwing
shell isolates it to `build_pipeline`, keeping "every new failure traces to one
named `TODO(you)`" clean.

### 3.5 Driving the spine to a `vc_image`
The spine terminates by the caller running the pipeline and harvesting the open
output — the existing `vc::pipe::vc_pipeline::run()` map contract
(`docs/pipe_design.md §12.2`): inject the source on the open input, `run()`,
read the open output packet as a `vc_image`. **No new `render()` wrapper is
mandated.** A convenience `vc_image render(const edit_session&, const
render_request&)` (build_pipeline → run → extract) is **[OPEN]** and left out of
the spine: it must pick "the" output image out of a multi-output map, an
assumption best deferred (see **Risk R3**). If added later it is a `TODO(you)`
rep, not plumbing.

### 3.6 `cancellation_token` — run() amendment (a) (§8 "Cancellation") — [NOW]
See §5.1. Lives in `vc::pipe` (`include/vc/pipe/vc_cancellation_token.h`), not
`vc::edit`, because it amends the pipe runner.

### 3.7 Version fields (§9) — [NOW, reserved]
- `edit_document::version` (doc_version) — already on the struct (§3.1).
- **Engine/process version** — a separate axis from doc/module versions (§9 req 2).
  Scaffold a header-only constant `include/vc/edit/vc_engine_version.h`:
  ```cpp
  namespace vc::edit {
  // The render-path version recorded so an edit reproduces (§9 req 2, §19). A
  // target selects a matching render path or upgrades knowingly. Reserved;
  // no migration logic now ([LATER]).
  inline constexpr int render_engine_version = 1;
  }
  ```
  Module-version (per-stage) is deferred to [LATER] (darktable `module_version`,
  §15.2) — see **Risk R8**: §12 [NOW] lists it, but it has no home until a stage
  params struct exists (a user rep, not yet scaffolded).

---

## 4. The periphery (after the spine)

Each item: file(s), interface, scaffolded-vs-`TODO(you)`, governing section.

### 4.1 Storage engine — `kv_store` (§7) — [LATER; seam+test-backend NOW]; file+sqlite [LATER]
> **Tag note [review: doc-fidelity D-4]:** §12 lists "`kv_store` + adapters" under
> **[LATER]** and §7 is "[LATER; shape settled]". Scaffolding the interface + a
> memory test-backend now is the doc-sanctioned "seam NOW" pattern (mirrors §6/§7's
> own "[LATER; seam NOW]" phrasing and §14's authorization to scaffold
> `kv_store`/adapters) — it is not a promotion of a [LATER] line to [NOW].
`include/vc/edit/vc_kv_store.h` + `src/edit/vc_kv_store.cpp`.

"Program to an interface" applies at the **storage engine**, not the data role
(§7, §16 L10). The shared low-level byte store both Kind A and Kind C build on.

```cpp
namespace vc::edit {

using kv_bytes = std::vector<std::byte>;  // opaque payload

// Shared low-level byte store (§7). A MISS is expected control flow, so get()
// returns optional — NOT the pipe layer's throw idiom (a missing key is not an
// error here; derived_store surfaces misses to trigger recompute — §7).
class i_kv_store {
  public:
    virtual ~i_kv_store() = default;
    virtual void                    put(const std::string& key, kv_bytes value) = 0;
    virtual std::optional<kv_bytes> get(const std::string& key) const = 0;
};

// The tests backend (§7: memory_store). Fully implemented — a std::unordered_map
// wrapper is plumbing, not a rep. file_store / sqlite_store are [LATER].
class memory_kv_store : public i_kv_store {
  public:
    void                    put(const std::string& key, kv_bytes value) override;
    std::optional<kv_bytes> get(const std::string& key) const override;
  private:
    std::unordered_map<std::string, kv_bytes> map_;
};

} // namespace vc::edit
```
Scaffolded: interface + fully-written `memory_kv_store`. `TODO(you)`: none
(memory backend is plumbing); `file_store`/`sqlite_store` are **[LATER]** —
not created now.

> **Decision D1 (get → optional, not throw).** Preserved from §7 deliberately.
> `doc_writer` **hides** misses behind defaults (total get); `derived_store`
> **exposes** them (⇒ recompute). That miss-semantics split is the one distinction
> §7 says to keep, so `i_kv_store::get` must be `optional`, not throwing.

### 4.2 `doc_writer` / `doc_reader` — typed Kind-A adapter (§7, §4.4) — interface [NOW]; reps [TODO]
`include/vc/edit/vc_doc_store.h` + `src/edit/vc_doc_store.cpp`.

```cpp
namespace vc::edit {

// TYPED adapter over i_kv_store for Kind A (§7). The [LATER] schema save/load
// (§18) programs against THIS. Hides misses behind defaults (total get) — a
// missing key yields the field's default, never an error (contrast
// derived_store). The value vocabulary starts minimal (the scalar param types
// edit_document uses) and broadens per §17.3 when custom types appear.
class doc_writer {
  public:
    explicit doc_writer(i_kv_store& store);
    // TODO(you): serialise `value` under `key`. Hand-rolled mapping now (§4.4);
    // nlohmann/json codec deferred — see Decision D2.
    void set(const std::string& key, double value);      // representative scalar
    void set(const std::string& key, int value);
    void set(const std::string& key, bool value);
  private:
    i_kv_store& store_;
};

class doc_reader {
  public:
    explicit doc_reader(const i_kv_store& store);
    // TODO(you): total get — return the stored value or `fallback` on a miss.
    double get(const std::string& key, double fallback) const;
    int    get(const std::string& key, int fallback) const;
    bool   get(const std::string& key, bool fallback) const;
  private:
    const i_kv_store& store_;
};

} // namespace vc::edit
```
Scaffolded: interfaces + ctors. `TODO(you)`: the `set`/`get` bodies (the
serialization rep). Governed by §7 + §4.4.

> **Decision D2 (JSON codec — vendor NOW). RESOLVED 2026-07-17: vendor now** (user
> sign-off). `edit_model.md` §12 [NOW] names "hand-written JSON serialization
> (`nlohmann/json`)", and §4.4 says *borrow the codec (`nlohmann/json`, MIT),
> hand-roll the mapping*. `nlohmann/json` was NOT vendored (`third_party/` held only
> `doctest`, `nanobench`, `stb`). **Resolution:** vendor `nlohmann/json` (MIT — clean
> for the paid app) under `third_party/nlohmann/json.hpp` as a **`SYSTEM PRIVATE`
> include**, the same pattern as `stb`/`nanobench`, and add its MIT entry to
> `THIRD_PARTY_LICENSES.md`. **Scope stays intact:** vendoring only makes the codec
> *available* — the `doc_writer`/`doc_reader` **bodies remain `TODO(you)`** (the
> hand-rolled mapping is the user's rep, §4.4). The scaffold ships the seam + the
> dependency wired; the user fills the mapping against `nlohmann::json`.

### 4.3 `derived_store` — content-hash Kind-C adapter (§7, §5) — interface [NOW]; reps/tiers [TODO/LATER]
`include/vc/edit/vc_derived_store.h` + `src/edit/vc_derived_store.cpp`.

```cpp
namespace vc::edit {

// CONTENT-HASH adapter over i_kv_store for Kind C (§7). Separate instance,
// different durability (may-evict vs the document's never-evict) — the one
// distinction to preserve (§7). EXPOSES misses (⇒ recompute), the opposite of
// doc_reader. This is a PERFORMANCE store: loss ⇒ recompute, not data loss (§2).
class derived_store {
  public:
    explicit derived_store(i_kv_store& store);
    // TODO(you): look up by CONTENT HASH (§5.1 cache key = hash(own param slice
    // + input hash)). A hit returns the bytes; a MISS returns nullopt so the
    // caller recomputes. Store bytes under put().
    std::optional<kv_bytes> lookup(const std::string& content_hash) const;
    void                    store(const std::string& content_hash, kv_bytes value);
  private:
    i_kv_store& store_;
};

} // namespace vc::edit
```
Scaffolded: interface + ctor. `TODO(you)`: `lookup`/`store` bodies.
**[LATER] (do NOT build):** the three tiers (transient cache / persistent store /
bundled-with-edit), eviction/GC/invalidation policy (§13.8), the actual
chained-hash keying (§5.1), and taps/injections integration (§5.2). Only the
adapter shape is scaffolded.

### 4.4 Wire the stores into `edit_session`
Once §4.1–4.3 exist, extend `edit_session` (§3.3) to compose them:
- `derived_store* derived_ = nullptr;` — a **nullable handle, setter-injected**
  (`set_derived_store(derived_store*)`), **not** a ctor param and **not** a
  reference. Rationale (**[review: scope S1]**): a reference member must be
  initialized in every ctor's init-list, which would force the thin 2-arg
  `edit_session(vc_image, edit_document)` ctor (§3.3) to be dropped the moment the
  member appears — breaking the M2 green primitive + `build_pipeline` red spec
  that construct with 2 args, i.e. a non-compiling milestone. A nullable handle
  keeps the 2-arg ctor valid, makes "injected" real (a reference cannot be
  attached post-construction), matches §3.3's own commented pointer form, and
  matches §3's "separate durability, injected handle" intent — the store outlives
  the session, so the session merely references it. (`std::unique_ptr` would give
  ownership but is unwanted here; a non-owning `*` is correct. **[CALL]** non-owning
  pointer.) *Note:* this handle does **not** by itself determine copyability — the
  Kind-B `metadata` handle (§4.5) is an owning `unique_ptr`, which is what makes the
  whole aggregate **move-only** (see the §10 reconciliation in R1).
- Access via `derived_store* derived() noexcept;` (may return null).
Scaffolded: the member + accessor + setter. No rep. The metadata handle (§4.5) is
attached the same way (`set_metadata`, M6).

### 4.5 `metadata` — Kind B interface (§6) — seam only [LATER]
`include/vc/edit/vc_metadata.h` (interface; **no backend impl now**).

```cpp
namespace vc::edit {

// Kind B — EXIF/IPTC/XMP, ratings, flags, keywords, color matrices, GPS (§6).
// Unlike edits, STANDARDS and INTEROP matter here. Behind an interface because
// BOTH criteria hold (§7): two real backends (exiv2_metadata desktop,
// dng_sdk_metadata on-device) + a polymorphic caller. Licensing is load-bearing:
// exiv2 is GPL ⇒ desktop only; on-device writes via the permissive DNG SDK (§6).
//
// SEAM ONLY. No backend is built now ([LATER]). `field`/`value` are placeholder
// vocabularies — kept deliberately thin so the editor-era decision on the full
// field set (§13.5) is not pre-baked.
class metadata {
  public:
    virtual ~metadata() = default;
    using field = std::string;               // placeholder key vocabulary
    using value = std::string;               // placeholder value vocabulary
    virtual std::optional<value> get(const field& f) const = 0;
    virtual void                 set(const field& f, value v)  = 0;
};

} // namespace vc::edit
```
Scaffolded: the abstract interface. `TODO(you)`/[LATER]: `exiv2_metadata`,
`dng_sdk_metadata` — **not created now**. If `edit_session` must hold a metadata
member during scaffold, hold it as `std::unique_ptr<metadata>` (default null) —
see **Risk R1**.

### 4.6 Construction `kind()` registry (§8 "Construction registry") — [NOW, small]
`include/vc/edit/vc_stage_registry.h` + `src/edit/vc_stage_registry.cpp`.

Fulfils the `i_pipe::kind()` seam (`include/vc/pipe/i_pipe.h:54`); removes
hardcoded `new`-chains; enables CLI introspection (§8). Graph **assembly** stays
in `build_pipeline` code (§4.2) — the registry only maps a `kind` string to a
factory that builds one stage; it never infers the graph.

```cpp
namespace vc::edit {

// kind -> factory(name). NOTE [review: doc-fidelity D-2]: §8 specifies
// `factory(params)`. This plan SIMPLIFIES to `factory(name)` because no stage
// `params` struct exists yet (params structs are user reps — tied to R8); the
// only registerable stages today are paramless (verified: vc_passthrough_stage
// etc.). Broaden to carry resolved params when the first params struct lands.
// A factory builds ONE stage instance given its per-instance name (i_pipe::name,
// pipe_design §12.2). Wiring stays explicit in build_pipeline — a registry lets
// wiring be data but never removes the need to STATE it (§8, §16 L11).
// Data-driven graph template is [LATER].
class stage_registry {
  public:
    using factory = std::function<std::unique_ptr<vc::pipe::i_pipe>(vc::pipe::stage_name)>;

    void register_kind(const char* kind, factory make);   // plumbing
    // Plumbing (map-lookup-or-throw — structurally identical to has()): look up
    // `kind`, invoke its factory, or throw vc::vc_exception on an unknown kind.
    // [review: scope S3] — this is NOT a rep, so the scaffold WRITES it. The
    // red-by-design spec that used to hang on it moves onto a genuine rep
    // (build_pipeline), see §6.3.
    std::unique_ptr<vc::pipe::i_pipe> create(const std::string& kind,
                                             vc::pipe::stage_name name) const;
    bool has(const std::string& kind) const noexcept;     // plumbing

  private:
    std::unordered_map<std::string, factory> factories_;
};

} // namespace vc::edit
```
Scaffolded fully as plumbing: `register_kind`, `has`, **and `create`** (all
map operations, no domain logic). `TODO(you)`: none in the registry itself — the
*stage `kind()` registrations* it is populated with (which stages exist) are
assembled in `build_pipeline`, the user's rep. A registry round-trip
(register → has → create the passthrough kind) is therefore a **green** primitive
(§6.3), not a red spec.

### 4.7 Error-model wrapping (§8 "Error model") — [NOW, seam in run()/build_pipeline]
The pipeline **wraps** a fatal stage exception with **stage context**
(kind/name/slot) + a `vc_error_code` category (§8, §16 L17). Valid partial
outcomes (a bracket won't align ⇒ merge the rest) are **stage domain logic, not
errors** — no partial-failure framework (§8).

- **Where:** this is behavior *inside* `vc::pipe::vc_pipeline::run()` (already a
  `TODO(you)` rep) — when a stage's `process()` throws, catch/rethrow wrapped with
  the stage's `name()`/`kind()`. It is therefore a **rep the user writes in
  `run()`**, guided by a comment; no new type is required.
- **Error code — Decision D3 (check, don't default-extend).** A dedicated
  category (e.g. `vc_error_code::pipeline_error`) would ripple to **three**
  switches: `to_int` (safe), `to_error_code` (throws on unlisted values — the new
  case is mandatory or its round-trip throws), and `to_string` (needs the case).
  **Verified:** no existing test pins the enum's cardinality/range (only a
  `decode_error` round-trip + a `file_not_found` to_string, `tests/test_vc_image.cpp:39`).
  So adding an enumerator is *safe if all three switches are updated together*.
  **Recommendation:** for the scaffold, **reuse `vc_error_code::invalid_argument`**
  for the wrapping message (minimal, zero ripple); add a dedicated
  `pipeline_error` enumerator only when the wrapping rep is fleshed out and a
  distinct category earns its place. **[OPEN — needs sign-off]** which of the two.

---

## 5. The three `run()` amendments — reconciled against on-disk tests

`edit_model.md` §14 requests three amendments to the `pipe_design.md` `run()`
contract. **Verified fact:** all three existing `run()` tests
(`tests/test_vc_pipe.cpp:254,275,298`) call `run(std::move(inputs))` with **one
argument**, and `run()`'s body is already `TODO(you)` (red-by-design). The
current signature (`include/vc/pipe/vc_pipeline.h:88`) is:

```cpp
std::unordered_map<stage_port, vc_pipe_packet>
run(std::unordered_map<stage_port, vc_pipe_packet> inputs) const;
```

**Headline: none of the three amendments conflicts with an on-disk test.** Only
(a) changes the signature now, and it does so additively via a trailing default.

### 5.1 Amendment (a) — cancellation token — [NOW] — the one signature change
- **New type** `include/vc/pipe/vc_cancellation_token.h` (namespace `vc::pipe`):
  a cooperative token, checked **between stages** in `run()` (§8). Small, concrete,
  fully scaffolded (plumbing — no rep):
  ```cpp
  namespace vc::pipe {
  // Cooperative cancellation (§8 edit_model). A source flips the flag; the runner
  // observes it BETWEEN stages (and, [LATER], at checkpoints inside long stages).
  // Interactive re-render (drag ⇒ cancel in-flight ⇒ restart) needs it. Shared
  // flag so a caller on another thread can cancel a run in progress.
  class cancellation_token {
    public:
      cancellation_token() = default;               // never-cancelled default
      bool cancelled() const noexcept { return flag_ && flag_->load(); }
    private:
      friend class cancellation_source;
      explicit cancellation_token(std::shared_ptr<std::atomic<bool>> f)
          : flag_(std::move(f)) {}
      std::shared_ptr<std::atomic<bool>> flag_;
  };
  class cancellation_source {
    public:
      cancellation_source() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
      cancellation_token token() const noexcept { return cancellation_token{flag_}; }
      void cancel() noexcept { flag_->store(true); }
    private:
      std::shared_ptr<std::atomic<bool>> flag_;
  };
  } // namespace vc::pipe
  ```
- **Signature change (additive, trailing default):**
  ```cpp
  std::unordered_map<stage_port, vc_pipe_packet>
  run(std::unordered_map<stage_port, vc_pipe_packet> inputs,
      const cancellation_token& cancel = {}) const;
  ```
- **Why it does not break tests:** the three `run(std::move(inputs))` calls bind
  `inputs`; `cancel` defaults to a never-cancelled token. They remain red-by-design
  for the *body* reason (run() is TODO), not the signature.
- **What is scaffolded vs rep:** the token type is fully written (plumbing); the
  **between-stage check** in `run()`'s body is the **user's rep** (a comment
  directs: "check `cancel.cancelled()` before each stage; on cancel, stop and
  throw/return per the chosen policy").
- **NOT touched now:** `i_pipe::process()` and `vc_pipe_context` do **not** take a
  token. §8's "checkpoints inside long stages" needs the token *inside*
  `process()`, but **no long stage exists**, so that is **[LATER]**; threading it
  through `process()` now would ripple to the passthrough worked-reference and
  every stage test for zero present benefit.
- **Bench note:** `bench/vc_benchmark_pipeline.cpp` (built only under
  `VC_BUILD_BENCHMARKS`) may call `run()`; the trailing default keeps it
  compiling. The implementer should grep the bench sources and confirm.

### 5.2 Amendment (b) — taps & injections — [LATER; design NOW] — no signature change now
- **Design (recorded, not built):** keyed read/write of **any** slot, not just
  open ones — *inject* a stored value onto a slot ⇒ skip its producer (cache-hit
  path for `derived_store`); *tap* an intermediate ⇒ persistence/overlays/debug
  (§5.2). One mechanism, four uses.
- **Reconciliation:** additive to the map-variant `run()` when built (add `taps`
  + `injections` parameters/options); stages stay pure and oblivious. **No change
  to `run()` now** — adding inert parameters would be shape-baking against an
  `[OPEN]` API (§13.8 "tap/injection API shape"). The seam is the existing
  `stage_port`-keyed map design; nothing to scaffold beyond this note.

### 5.3 Amendment (c) — resolution/ROI render request — [LATER; design NOW] — NOT a run() param
- **Cross-section tension, flagged (as the task asks):** §14 lists the render
  request under "three amendments this doc requests to the `run()` contract", but
  §1's pipeline diagram and §8 ("Resolution awareness") put `{level, ROI}` on the
  **render request consumed by `build_pipeline`**, not on `run()`. **The scaffold
  follows §8/§1:** `render_request` (§3.2) is a `build_pipeline` input; `run()` is
  **unchanged** by (c). Scale-aware param interpretation is **[LATER]** (§17.6).
  This is the one genuine doc-internal inconsistency; resolving it toward
  `build_pipeline` avoids putting an inert `{level,ROI}` onto `run()`'s hot
  signature. **[OPEN — noted]**; no on-disk test is affected either way.

---

## 6. CMake / test wiring

> **Wire INCREMENTALLY, per milestone — not all at the end [review: scope S2].**
> Each new `.cpp` and the `tests/test_vc_edit.cpp` entry is added to CMake **as its
> milestone lands** (§7), so every milestone builds and its green/red tests
> actually run — this is what makes spine-first incremental verification (§2, §7)
> real and lets "verify no regression before adding new surface" gate each step.
> The lists below are the *cumulative end-state*; the final milestone only does the
> baseline **recount**, not first-time wiring. (`tests/test_vc_edit.cpp` is added at
> the first milestone that ships a test — M1 — and grows in place thereafter.)

### 6.1 New source files → `VC_LIB_SOURCES` (cumulative)
Add to the `set(VC_LIB_SOURCES …)` list (CMakeLists.txt:80), so they build into
**both** `vc_image_utils_lib` and the benchmark copy `vc_bench_lib` (shared list —
they cannot drift, per the existing comment):
```
src/edit/vc_edit_session.cpp
src/edit/vc_build_pipeline.cpp
src/edit/vc_kv_store.cpp
src/edit/vc_doc_store.cpp
src/edit/vc_derived_store.cpp
src/edit/vc_stage_registry.cpp
```
Header-only files add **no** source entry: `vc_edit_document.h`,
`vc_render_request.h`, `vc_engine_version.h`, `vc_metadata.h`,
`vc/pipe/vc_cancellation_token.h`. (`cancellation_token` is header-only above; if
the implementer moves any inline body to a `.cpp`, add it here.)

### 6.2 New test file → tests target
Add to `add_executable(vc_image_utils_tests …)` (CMakeLists.txt:123):
```
tests/test_vc_edit.cpp
```
Follow the existing multi-TU doctest rule: **no** `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`
in this file (`tests/test_vc_image.cpp` defines the single `main`; see the header
comment in `tests/test_vc_pipe.cpp:4`).

### 6.3 New red-by-design tests (the human's worklist)
Ship `tests/test_vc_edit.cpp` with the split from §1.1 — **green primitives** for
the fully-written plumbing, **red-by-design specs** for every `TODO(you)` rep,
matching the exact convention already in `tests/test_vc_pipe.cpp` (a GREEN block
that pins primitives + a SPEC block that fails loudly until the rep is written).
Red specs to include (4 — one per rep):
- `build_pipeline` returns a runnable pipeline from a one-stage session (RED — body TODO).
- `doc_writer`/`doc_reader` round-trip a scalar through a `memory_kv_store` (RED — set/get TODO).
- `derived_store` lookup misses, then hits after store (RED — lookup/store TODO).
- `run(inputs, token)` with a **pre-cancelled** token stops/throws (RED — run() body TODO).
Green primitives to include (6): `edit_document` defaults; `edit_session` accessors;
`memory_kv_store` put/get; `cancellation_source::cancel()` observed by its token;
`render_request` defaults; `stage_registry` register→has→create round-trip
(plumbing — [review: scope S3]; the passthrough `kind()` registration under test is
trivial, so this passes immediately).

**No benchmark files are added** — the edit model has no perf rep yet, and the
bench targets are off by default.

---

## 7. Ordering / milestones (spine-first, for the implementation agent)

Each milestone **wires its own new files into CMake and builds green before the
next** (§6). "Verify" below = rebuild + run the suite + confirm the existing 22
failures are unchanged and every new failure traces to a named `TODO(you)`.

1. **M0 — cancellation seam.** Add `vc/pipe/vc_cancellation_token.h`; amend
   `vc_pipeline::run()` with the trailing defaulted token (§5.1). Header-only, no
   CMake source change. Verify all existing tests still compile (one-arg calls) and
   the pipe suite's red/green split is unchanged. *(Smallest, touches existing code
   — do it first and verify no regression before adding new surface.)*
2. **M1 — spine data types.** `vc_edit_document.h`, `vc_render_request.h`,
   `vc_engine_version.h` (header-only data). **Create `tests/test_vc_edit.cpp` and
   wire it into the tests target now** (§6.2); add green primitive tests
   (`edit_document`/`render_request` defaults). Verify.
3. **M2 — spine aggregate + translation.** `edit_session` (thin: source + edits;
   `#include` the document header per C1) + `build_pipeline` signature with a
   throwing `TODO(you)` body. **Wire `vc_edit_session.cpp` + `vc_build_pipeline.cpp`
   into `VC_LIB_SOURCES`** (§6.1). Green: `edit_session` accessors. Red spec:
   build_pipeline from a one-passthrough session. Closes the end-to-end spine
   (edit_session → build_pipeline → run → vc_image). Verify.
4. **M3 — storage engine.** `i_kv_store` + `memory_kv_store` (fully written);
   **wire `vc_kv_store.cpp`**. Green: put/get. Verify.
5. **M4 — typed adapters.** `doc_writer`/`doc_reader` and `derived_store`
   (interfaces + ctors; `TODO(you)` bodies); **wire `vc_doc_store.cpp` +
   `vc_derived_store.cpp`**. Red specs. Then extend `edit_session` to hold the
   nullable `derived_store*` handle + `set_derived_store` setter (§4.4, S1) — the
   2-arg ctor is unchanged, so M2's green primitive + red spec still compile.
   Verify.
6. **M5 — registry.** `stage_registry` (all plumbing incl. `create`, §4.6/S3);
   **wire `vc_stage_registry.cpp`**. Green: register→has→create round-trip with the
   passthrough `kind()`. Verify.
7. **M6 — metadata seam.** `metadata` interface only (§4.5, header-only); add the
   nullable `std::unique_ptr<metadata>` member + `set_metadata` setter per R1. No
   backend. Verify.
8. **M7 — baseline recount (no first-time wiring).** All files are already wired
   (M1–M6). Rebuild; **record the measured doctest totals + refreshed line
   references in §1 / §1.1** (per D-3); confirm existing 22 failures unchanged and
   every new failure traces to a named `TODO(you)`.

Error-model wrapping (§4.7) is folded into the `run()` rep the user writes; no
milestone of its own.

---

## 8. Risks / open decisions (called out, not silently assumed)

- **R1 — `edit_session` member shapes (doc inconsistency + compile bug; RESOLVED
  in the scaffold).** Three corrections to §3's member sketch, all so the aggregate
  actually compiles:
  1. **`metadata` (§3-vs-§6).** §3 sketches `metadata meta_;` (a **value** member),
     but §6 defines `metadata` as a **pure-virtual interface** — an abstract type
     cannot be a value member. **Correction:** `std::unique_ptr<metadata>` (owning
     handle, default null), attached via `set_metadata` (M6).
  2. **`derived_store` [review: scope S1].** §3 sketches `derived_store& derived_;`
     (a reference); §4.4 originally made it a ctor param. A reference member forces
     init in every ctor, which would kill the thin 2-arg ctor at M4 (a non-compiling
     milestone) and can't be "injected" post-construction. **Correction:** a
     nullable **non-owning handle** `derived_store* = nullptr`, setter-injected
     (§4.4). Keeps the 2-arg ctor stable.
  3. **`edit_document` [review: code-recon C1].** Held **by value**, so
     `vc_edit_session.h` must **`#include`** the document header, not forward-declare
     it (an incomplete-type value member is ill-formed). §3.3 fixed.

  Flagged here rather than silently changed so a reviewer diffing against §3's
  sketch reads these as deliberate corrections, not errors.

  **§10 copyability reconciliation (RESOLVED 2026-07-17, user sign-off — supersedes
  the earlier "keeps the aggregate copyable" wording in R1/§3.3/§4.4).** Correction
  1's owning `unique_ptr<metadata>` makes `edit_session` **move-only** — a `unique_ptr`
  is non-copyable, so the aggregate cannot be copied. The earlier plan claim that the
  aggregate "stays copyable for §10 undo/redo snapshots" was internally inconsistent
  with the mandated `unique_ptr` and is **withdrawn**. The scaffold ships `edit_session`
  as cleanly **movable** (user `~edit_session()` + `= default` move ops declared in the
  header, defined in the `.cpp` where `metadata` is complete — required over a
  forward-declared type). This costs nothing now: §10 is `[LATER]`, no test copies a
  session, and §10's own framing snapshots the **serialized intent** (edit doc +
  metadata + non-repro derived, §10), which needs no C++ copy. **Decision:** if an
  *in-memory* snapshot path is later wanted, add a virtual **`metadata::clone()`**
  (`[LATER]` seam already marked in `vc_metadata.h`) so the handle — and thus the
  aggregate — can be deep-copied on demand; it lands with the first real metadata
  backend, where a concrete clone body exists to write.
- **R2 — JSON codec vendor timing (Decision D2). RESOLVED: vendor now.** Vendor
  `nlohmann/json.hpp` (MIT) under `third_party/nlohmann/`, `SYSTEM PRIVATE` include,
  + `THIRD_PARTY_LICENSES.md` entry, as part of M4. Adapter *bodies* stay
  `TODO(you)`; only the dependency is wired.
- **R3 — `render()` convenience wrapper ([OPEN]).** The spine deliberately omits a
  `vc_image render(session, request)` helper because extracting "the" output image
  from `run()`'s multi-output map bakes an assumption about which open output is
  the render result. Left as a future `TODO(you)` rep, not plumbing. If the user
  wants a single-call spine capstone, this needs the extraction convention pinned.
- **R4 — error-code category (Decision D3, [OPEN]).** Reuse `invalid_argument` for
  the stage-context wrapping now, or add a dedicated `pipeline_error` enumerator
  (safe — no test pins cardinality — but ripples to three switches). Needs a pick.
- **R5 — resolution/ROI placement (§5.3, [OPEN, doc-internal]).** §14 groups the
  render request under "run() amendments"; §8/§1 put it on `build_pipeline`. Plan
  follows §8/§1 (render_request → build_pipeline; run() unchanged). Flagged as the
  one cross-section tension; no test is affected. Confirm the §8 reading is
  intended.
- **R6 — `edit_session` final name ([OPEN], §13.7).** `[CALL]` in the doc;
  alternatives `image_document` / `image_project`. Scaffold uses `edit_session`;
  a rename later is mechanical but touches every new edit-model file — worth
  confirming before M2.
- **R7 — reference-frame / ROI coordinate space ([OPEN], §11, §13.6).**
  `render_request::roi` uses a plain pixel rectangle placeholder. The canonical
  reference frame (original-image pixels vs normalized [0,1]) is unpinned;
  `roi`'s shape must not be treated as settled — it is a thin placeholder that a
  later §11 decision can reshape.
- **R8 — module-version reserved field deferred (§12-vs-§3.7 deviation; needs
  sign-off).** §12 [NOW] lists reserved version fields as **doc/module/engine**.
  The plan reserves doc (`edit_document::version`) and engine
  (`render_engine_version`) but **defers module-version to [LATER]** — because a
  per-stage `module_version` has no home until a stage *params* struct exists, and
  that struct is a **user rep not yet scaffolded** (you cannot reserve a field on a
  nonexistent struct). This is a considered deferral, not an omission; flagged as a
  risk (parallel to R2's JSON-vendor [NOW]-deviation) so the sequencing gets an
  explicit yes rather than a buried aside. Confirm: module-version lands when the
  first stage params struct is written.
- **R9 — reference-frame discipline for stored coordinates deferred (§12 [NOW],
  silently) [review: scope S4].** §12 [NOW] lists "reference-frame discipline for
  stored coordinates" and §11 says it "starts now." The plan's only coordinate
  touch is `render_request::roi` (§3.2) — but that is *render-request* geometry,
  explicitly "NOT source metadata," a **different** frame from §11's
  authored/**stored** coordinates (masks, spot edits in the document). There is no
  home for stored-coordinate discipline yet because **no coordinate-carrying
  setting exists in `edit_document`** (the first is crop/mask — a later tool
  slice). This is a defensible deferral (parallel to R8), but it is the one §12
  [NOW] item deferred without its own flag, so it is recorded here: the
  reference-frame choice (original-image pixels vs normalized [0,1], §11/§13.6) is
  pinned, and the discipline lands, when the first coordinate-carrying setting is
  added. No scaffold surface is needed now.
```