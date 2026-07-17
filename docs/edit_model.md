# Edit Model & Non-Destructive Editing — vc_image_utils

> Companion to `docs/pipe_design.md`. That doc settles the **execution** substrate
> (`vc::pipe`: stages, slots, packets, contracts, the runner). *This* doc settles the
> **editing** model above it: how an edit is represented, stored, rendered, and
> reproduced — and records the studies, decision levers, and scenario stress-tests
> behind those choices.
>
> **Status tags** (mirroring `pipe_design.md`): **[NOW]** built/settled for P1–P4
> scope · **[LATER]** deferred by design, seam present · **[OPEN]** genuine undecided
> question · **[CALL]** an author's provisional choice, easily overruled.
>
> **Provenance discipline.** §15 (Reference studies) reports findings from research
> agents (2026-07 session) with their sources and their own confidence caveats —
> read them as "studied," not "personally byte-verified." Claims about *our* code
> (`vc_image`, `vc::pipe`) are verified against the tree. Third-party product
> specifics that could not be independently verified are marked **[unverified]**.
>
> **Learning-build split (§17):** Claude scaffolds interfaces/plumbing; the user
> writes the rep logic. Nothing here is built yet — this captures the converged
> design so P4 starts from a spec.

---

## Table of contents

**Part I — The design**
0. TL;DR · 1. Core model · 2. Three kinds of data · 3. `edit_session` aggregate ·
4. Edit settings (document) · 5. Derived data (cache + store) · 6. Metadata ·
7. Storage interfaces · 8. Render-pipeline relationship · 9. Reproducibility &
portability · 10. Undo/redo · 11. Coordinate spaces & geometry

**Part II — Evidence & rationale**
12. NOW vs LATER · 13. Open questions · 14. Linkage & learning-build ·
15. Reference studies (the five engines) · 16. Decision levers · 17. Scenarios &
scalability · 18. The schema/serialization mechanism · 19. Glossary

---

# PART I — THE DESIGN

## 0. TL;DR

- An edit is **not baked pixels** — it is a **serializable description** applied to an
  **immutable source**. Pixels are produced by *rendering* on demand; the original is
  never mutated (`vc_image` is uniformly read-only via `vc_image_writer`/`seal()`).
- Image state is **not one blob**. It splits into **three kinds** (§2) with different
  formats, durability, and interop needs, **unified for access** by one per-image
  aggregate, **`edit_session`** (§3) — this is what resolves the "state is scattered
  everywhere" friction.
- The render **pipeline is disposable**, rebuilt from the edit state on demand by
  `build_pipeline`. Expensive results survive in a **persistent derived store**, not in
  the pipeline.
- Stages are **pure** (`output = f(inputs, params)`) and **never mutate inputs**. That
  one invariant licenses caching, buffer reuse, and (later) parallelism — all in the
  pipeline layer, invisible to stages.

## 1. Core model

```
   UI / CLI / preset  ── read & write directly ──►  edit_session  (one per image)
                                                        │  composes the 3 kinds (§2)
                                                        ▼
                                    build_pipeline(edit_session, render_request)
                                                        │  cheap; rebuilt on demand
                                                        ▼
                                                 vc_pipeline  (vc::pipe)
                                                        │  run()
                                                        ▼
                                                 vc_image  (immutable pixels)
```

Two invariants:

1. **Immutable source, rendered on demand.** The source `vc_image` is read-only and
   cheaply shared (`shared_ptr<const vc_pixel_buffer>`; see `vc_types.h:14`). Every
   render produces *new* pixels; the original is never touched.
2. **Edit state is the source of truth; the pipeline is disposable.** Editing mutates
   only the edit state. Rendering only reads it. `vc_pipeline` is a throwaway recipe
   rebuilt from one instant of that state — never where edits live.

**Why the pipeline is disposable yet work isn't repeated:** the *recipe* (pipeline
object) is cheap to rebuild; the expensive *results* live in a persistent, content-
hashed store that survives rebuilds (§5). darktable works the same way — the pixelpipe
is re-walked every render, but its cache persists (§15.3).

## 2. The three kinds of data — the central taxonomy

The most load-bearing decision in this doc. Sort every "datum about an image" by one
question — *can it be regenerated identically, reliably, cheaply?*

| Kind | Examples | Reproducible? | Home | Interop matters? |
|---|---|---|---|---|
| **A. Edit settings** (intent) | exposure, deghost strength, crop, parametric mask (brush/curve/feather) | it *is* input | **edit document** (JSON, §4) | **no** — app-specific |
| **B. Image metadata** | EXIF (ISO/shutter/GPS), color matrices, IPTC/copyright, ratings ⭐, flags, keywords | authored or read-from-file | **metadata** subsystem (EXIF/IPTC/XMP, §6) | **yes** — LR/dt/Bridge read these |
| **C-repro.** Derived, reproducible | alignment/homography, algorithmic masks, feature points, embeddings (if deterministic), rasterized parametric masks | **yes** (deterministic) | **derived store** (§5) — regenerable | n/a |
| **C-nonrepro.** Derived, non-reproducible | AI/ML object masks, non-deterministic embeddings | **no** (model drift / non-determinism) | **persisted with the edit** — behaves like intent | n/a |

The taxonomy drives everything downstream:
- **A** is app-specific → plain JSON, no XMP (§4.4).
- **B** is where standards/interop genuinely live → a `metadata` interface (§6).
- **C-repro** is a *performance* store: loss ⇒ recompute, not data loss. **Persistent,
  not casually evicted; core on device** (re-aligning a burst per deghost tweak is
  unacceptable).
- **C-nonrepro** can't be regenerated identically, so it behaves like intent and
  **must travel with the edit** (§9).

## 3. The per-image aggregate — `edit_session` [NOW, thin]

Storage is deliberately split (A/B/C have different formats, durability, interop —
§16 lever L9). That split is correct but leaves image state *scattered*, and much
depends on the *collection*. The resolution is **not** to merge the stores — it is one
domain entity that **composes** them:

```cpp
class edit_session {                 // everything about ONE image
    vc_image        source_;         // immutable source
    edit_document   edits_;          // Kind A  (+ Kind C-nonrepro bundled here)
    metadata        meta_;           // Kind B  (interface, §6)
    derived_store&  derived_;        // Kind C-repro handle (§5)
  public:
    edit_document&  edits();
    metadata&       meta();
    // build_pipeline reads NARROW SLICES from here (§4.1)
};
```

**Scattered storage, unified access.** This is Lightroom's catalog entry / darktable's
"image" concept **[unverified as to internal structure]**. It is: what **undo/redo**
snapshots (§10), what **portability** bundles (§9), what the **UI** binds to, and what
`build_pipeline` reads slices from (§8). Whenever "many things depend on the
collection," they depend on `edit_session` — not on reaching into stores individually.
**[CALL]** name `edit_session` (alternatives: `image_document`, `image_project`).

## 4. Edit settings (Kind A) — the document [NOW]

### 4.1 User-facing, not stage-facing
The document holds **user-facing settings**, organized by tool (like RawTherapee
`ProcParams`, §15.2) — **not** stage param structs. Stages receive a **narrow slice**,
never the whole document.

```cpp
struct capture_settings  { int bracket_count = 5; double ev_spacing = 2.0; };  // cross-cutting
struct exposure_settings { bool enabled = true; double ev = 0.0; double black = 0.0; };
struct edit_document {
    int version = 1;                 // doc_version — reserved for migration (§9)
    capture_settings  capture;       // ONE home for a value many stages read
    exposure_settings exposure;      // one member per user-facing tool
    // ...
};
```

**Lever (why narrow slices — §16 L3/L4):** giving stages the whole document reproduces
RawTherapee's whole-`ProcParams` coupling (§15.2), forces coarse caching (a stage that
reads everything must be invalidated by any change), and blocks isolated
test/reuse. A narrow slice keeps the cache key precise (§5.1), decouples the stage,
and makes its dependencies visible. Cross-cutting values (`bracket_count`) live **once**
in the document and are read by whichever stages need them — no per-stage argument
threading.

### 4.2 `build_pipeline` — the translation layer [NOW, hand-written]
`build_pipeline(edit_session, render_request) → vc_pipeline` is the single place that
speaks both vocabularies (user settings ↔ machine graph). It decides which stages
exist (including **paramless plumbing stages** a tool depends on — e.g. `deghost` ⇒
`align` + `compute_mask`), how many (`bracket_count` ⇒ N aligns), wires them, and
derives each stage's config from the relevant settings. **Structural/dependency
knowledge lives here, imperatively** — this is RawTherapee's `ImProcCoordinator`
(§15.2), legitimate for a fixed pipeline. A stage cannot decide how many of itself
exist, so this assembly step is irreducible regardless of registry (§8). Moving the
graph to *data* is [LATER].

### 4.3 Params on a stage
A stage owns a **plain typed `params` struct** (no variant bag), built at image-load
and read directly in the hot path (`params_.exposure` — zero indirection). `i_pipe`
is **untouched**: params are resolved *before* construction, at the factory boundary
the `kind()` seam already anticipates (`i_pipe.h:27-29`). A stage's params can be
**empty** and absent from the document (a plumbing stage like `align` has no user
knobs — it exists because `deghost` depends on it).

### 4.4 Serialization [NOW: hand-written; LATER: schema-driven — see §18]
- **Format: JSON.** Human-readable, diffable, nests (masks = arrays of strokes),
  versionable. Purpose is **reliable representation + import/export + own-ecosystem
  consumption** — *not* cross-app edit interop, which is impossible anyway (every
  editor's develop settings are private; darktable's history is unreadable by LR and
  vice-versa).
- **Borrow the codec** (`nlohmann/json`, MIT — permissive-clean for the paid app).
  **Hand-roll the mapping** (which fields, ranges, versioning) — the domain value no
  library provides.
- **Not XMP for edits** (§16 L6): XMP's interop applies to *standard metadata* (Kind B),
  not app-specific edits; RDF/XML complexity + GPL tooling (exiv2) is cost without
  payoff. darktable's hex-blob-in-XMP is the cautionary case (§15.2).
- **Binary in JSON**: possible via base64 (reliable), but reserve for *small* binary
  (a hash, a tiny mask). Large blobs (mask pixels, embeddings) go to the derived store
  (Kind C), never JSON.
- **[LATER] schema-driven serialization** (§18): declare each field once as a
  descriptor; one generic `save`/`load`/`describe` walks it, so save/load/UI/CLI/
  cache-key all derive from a single source (escapes RawTherapee's per-field
  duplication, §15.2). Adopted **when hand-written boilerplate hurts**, not before.

## 5. Derived data (Kind C) — cache + persistent store [LATER; seam NOW]

Three tiers, cut by "reliably reproducible?" (§2):
- **Transient in-memory cache** — cheap intermediates; evictable freely.
- **Persistent derived store** (content-hash-keyed "db": files or SQLite blobs) —
  reproducible-but-expensive (alignment, features). **Reliable, not casually evicted;
  core on device.** Reconstructible if lost (not source of truth).
- **Bundled with the edit** — non-reproducible derived (AI masks): persisted as data,
  travels with the document (§9).

### 5.1 Caching model
Cache key = `hash(own param slice + input hash)`; the input hash transitively encodes
all upstream (darktable's chained hash, §15.3). This makes cross-stage **data
dependencies** correct — change exposure ⇒ its output changes ⇒ color's *input hash*
changes ⇒ color recomputes (it is processing different pixels) — while keeping
same-level param changes precise (change color ⇒ exposure's slice/input unchanged ⇒
exposure stays cached). **Purity is what makes the cache correct**: same
(inputs, params) ⇒ same output, always. A params-*only* key would return stale
downstream pixels — the input-hash term is mandatory (this closed the "metaphysical
dependency" gap raised in review). Coarse (recompute-suffix, RawTherapee-style) suits
the batch merge; fine per-stage (darktable-style) suits the interactive editor — same
pure stages, product-tuned schedule.

### 5.2 Taps & injections — a `run()` extension [LATER; design NOW]
The pipeline needs keyed read/write of **any** slot, not just open ones:
- **inject** a stored value onto a slot ⇒ *skip* its producer (this is how the derived
  store integrates: cache hit ⇒ inject alignment, skip re-align);
- **tap** (harvest) an intermediate ⇒ persistence, overlays, dump-and-inspect.

One mechanism, four uses (cache, overlays, persistence, debugging). Additive to the
map-variant `run()` (add `taps` + `injections`); stages stay pure and oblivious (they
read their input slot regardless of whether the value was computed or injected).

### 5.3 Derived data is uniform
Masks, warps, feature points, embeddings are all "expensive non-pixel data derived
from inputs+params." They ride the **same slot/packet rails** as pixels — `vc_pipe_
packet` is `std::any`, so a slot carries a `vector<vc_image>`, a homography, a
`vector<feature_point>`, or an embedding tensor identically. Make expensive/shared
derived data a **first-class stage output** (wired via slots), which makes it
independently cacheable, tappable, and dump-inspectable. Compute inline only when
cheap and single-consumer.

## 6. Image metadata (Kind B) — behind an interface [LATER; seam NOW]

Ratings, flags, keywords, EXIF, color matrices, IPTC/copyright, GPS. Unlike edits,
**standards and interop matter here.**

```cpp
struct metadata {
    virtual std::optional<value> get(field) const = 0;   // read EXIF/IPTC/XMP
    virtual void set(field, value) = 0;                  // rating, copyright, keyword
};
// implementations: exiv2_metadata (desktop), dng_sdk_metadata (on-device)
```

- **Licensing (load-bearing):** exiv2 is **GPL → desktop only** (per the project
  licensing memo); on-device (paid app) writes metadata into the output DNG via the
  **DNG SDK** (permissive). The interface is what lets the same calling code run either
  backend — and here **both interface criteria hold** (2 real implementations + a
  polymorphic caller), unlike the Kind A/C case (§7).
- **Source of truth** = the app's model; XMP is a *sync target* for interop (LR-style
  **[unverified]**), not the only home.
- **MVP scope:** EXIF passthrough + copyright + color matrices into the output DNG.
  Full catalog metadata (ratings/flags/keywords) is editor-era; the interface
  accommodates it so we are not bottled up.

## 7. Storage interfaces [LATER; shape settled]

"Program to an interface" applies — at the **storage engine**, not the data role
(§16 L10). The substitutability test: an interface earns its place only when there are
≥2 real implementations *and* a caller uses them interchangeably. Kind A vs Kind C
**fail the second criterion** (no caller is polymorphic over "settings vs blobs"; they
have opposite miss- and durability-semantics), so they are **not** one caller-facing
interface. What they *share* is the storage engine beneath:

- **`kv_store`** — shared low-level `put(key, bytes)` / `get(key) → optional<bytes>`.
  Backends: `file_store`, `sqlite_store`, `memory_store` (tests). Both roles build on it.
- **`doc_writer`/`doc_reader`** — *typed* adapter over `kv_store` for Kind A; the schema
  save/load program against this; hides misses behind defaults (total get).
- **`derived_store`** — *content-hash* adapter over `kv_store` for Kind C; **exposes**
  misses (⇒ recompute). **Separate instance, different durability** (may-evict vs the
  document's never-evict) — the one distinction to preserve.

`edit_session` (§3) composes these; it is a facade (has-a), not a shared interface (is-a).

## 8. Relationship to the render pipeline

- **Stack compiles to graph** (§16 L1). The user arranges a linear *stack* of tools
  (the edit doc); `build_pipeline` compiles it into the wired `vc::pipe` *graph* (which
  may branch / fan-in). The general DAG stays available for HDR fan-in (§17.1).
- **Construction registry [NOW, small]** — `kind → factory(params)`; fulfils the
  `i_pipe::kind()` seam; removes hardcoded `new`-chains; enables CLI introspection.
  Graph **assembly** stays in code (§4.2). **Wiring is always explicit** — no studied
  engine infers the graph from types (GEGL/vkdt/darktable all wire explicitly); a
  registry lets wiring be *data* but never removes the need to *state* it. Data-driven
  graph template is [LATER] and even then dynamic structure (variable stage counts,
  conditionals) stays code.
- **Buffers** — pipeline-owned; stages are pure and produce their output (private
  scratch is their own; they **never** mutate inputs). Point-ops may run in place, area
  ops need a distinct buffer — but that is the *pipeline's* schedule choice driven by
  **liveness**, not a stage declaration. In three of four studied engines the pipeline/
  framework owns output buffers (§15.3). Pooling / liveness-aliasing (vkdt-style) is
  [LATER]; v1 lets each stage allocate (fine at the memory budget). In-place safety is
  a **global** property (only the pipeline knows if a buffer is dead), which is why it
  cannot be a stage-local decision.
- **Ordering** — canonical, defined in code; the doc does **not** store order.
  Configurable parts of the pipeline are *params* (data, in the doc); *structure* is
  stable (code). Stage-declared order priority (darktable's `iop_order`, §15.4) is
  [LATER] — added when a stage exists where order has a *correct* answer (demosaic/color).
- **Display / view transform** — executed *by* the pipeline as a **terminal stage**
  (scene→display), but its config comes from the **output target** (monitor profile for
  preview, output profile for export), **not** the edit doc. `build_pipeline` appends it
  per render destination. This keeps "edit" (in the doc) and "view transform" (from the
  target) cleanly separate while both run as stages (§16 L18).
- **Cancellation [NOW]** — `run()` takes a cooperative **cancellation token**, checked
  between stages and at checkpoints inside long stages. Interactive re-render (drag ⇒
  cancel in-flight ⇒ restart) needs it.
- **Concurrency [LATER]** — pure stages enable parallel execution; the executor/
  threading/tiling is built later without touching stage semantics (Halide's algorithm/
  schedule split validates this, §15.5).
- **Error model** — `vc_exception` for **fatal** (OOM, corrupt data, contract
  violation) ⇒ abort; the pipeline **wraps** with stage context (kind/name/slot) +
  a `vc_error_code` category. **Valid partial outcomes** (a bracket won't align ⇒ merge
  the rest) are **robust stage domain logic, not errors** — the design scales without a
  partial-failure framework because robustness is per-stage. Per-stage "bypass-on-error
  ⇒ pass-through" policy is [LATER] (editor-era; darktable disables a failed module).
- **Resolution awareness / preview [LATER; design NOW]** — the render *request* carries
  `{target resolution / pyramid level, ROI}`; it is **not** source metadata (the source
  image's `vc_image_meta` describes *its* geometry; a preview is a different image).
  Stages must be resolution-aware, and **spatial params must scale with level** (a 5 px
  blur at full-res ≈ 1.25 px at 1/4 preview) so preview matches export. Scale-awareness
  is the crux of preview↔export parity (§17.6).

## 9. Reproducibility & portability [design NOW]

"Transfer image + edit ⇒ same net render" requires three things met deliberately:

1. **All inputs travel.** Transfer bundle = **source + JSON doc + non-reproducible
   derived** (AI masks embedded/bundled — never left in the local cache). Reproducible
   derived regenerates on the target.
2. **Same engine / process version.** Add an **engine/process version** axis to the
   document (beyond doc/module versions); the target selects a matching render path or
   upgrades knowingly (LR's "process version" / dt module versions **[unverified]**).
3. **Deterministic computation.** IEEE 754 basic ops (`+ − × ÷ sqrt fma`) are correctly
   rounded and deterministic — matching computations *do* match across machines — but
   **"same source" ≠ "same computation"**: FMA contraction, reassociation / reduction
   order, and non-bit-specified transcendentals (`pow`/`exp`/`sin`) diverge by default.
   So determinism is **engineered, not free**: no `-ffast-math`, pinned FMA,
   deterministic reduction order, **own/portable transcendentals**. Feature-extraction
   *thresholds* can amplify a 1-ULP difference into a different feature set ⇒ different
   alignment — keep decisions robust. **CPU↔GPU parity is the hard boundary** (relevant
   to the Halide→Vulkan path). **[OPEN]** target **visually-identical** (achievable,
   LR/dt-level) vs **bit-exact** (a serious cross-platform commitment); and whether the
   desktop tool must reproduce the phone merge exactly.

## 10. Undo/redo [LATER; buildable NOW]

Snapshot the **intent** held by `edit_session` — edit doc + metadata + non-reproducible
derived (= the transfer bundle minus source). Reproducible derived is *not* snapshotted
(it regenerates). Full-snapshot vs command/delta granularity is deferred to the editor
era. This is buildable on the existing design — no new machinery, just snapshots of the
aggregate's intent.

## 11. Coordinate spaces & geometry [OPEN; MVP-adjacent via alignment]

Everything user-authored in pixel coordinates (masks, spot edits, local adjustments) is
stored in a **canonical reference frame** ( **[CALL]** original-image pixels *or*
normalized [0,1] — not yet pinned). Geometry ops (crop/rotate/resize; alignment warp)
carry a **transform** — a *param* if user-set (crop/rotate → Kind A), *derived* if
computed (alignment → Kind C). Masks/edits **follow geometry** by mapping through that
transform. The UI edits in *display* space and converts back to the reference frame at
its boundary using the transform; display coordinates are **never** stored. Full
treatment deferred (P7/editor), but the merge's alignment warp already puts coordinate
transforms in play, so the reference-frame discipline starts now.

---

# PART II — EVIDENCE & RATIONALE

## 12. NOW vs LATER

**[NOW]** immutable `vc_image` (done); `edit_document` (typed, user-facing) +
narrow-slice stage params; hand-written JSON serialization (`nlohmann/json`);
`build_pipeline` (hand-written assembly); construction `kind()` registry; `edit_session`
aggregate (thin); cancellation token on `run()`; reserved version fields
(doc/module/engine); fatal-error model with stage-context wrapping; reference-frame
discipline for stored coordinates.

**[LATER]** persistent derived store + tiers; taps/injections `run()` extension;
metadata subsystem; `kv_store` + adapters; schema-driven serialization; buffer pooling
/ liveness aliasing; chained-hash caching; data-driven graph template;
concurrency/parallel executor; resolution-aware preview/pyramid + scale-aware params;
masks-follow-geometry (full); stage-declared order priority; per-stage bypass-on-error;
undo/redo mechanics; `vc_image_spec` semantic axes; region-scoped invalidation.

## 13. Open questions

1. **[OPEN]** Determinism target (visual vs bit-exact; desktop-reproduces-phone?) — §9.
2. **[OPEN]** Fan-in mechanism (list-valued slot vs `collect` stage) — deferred to P4;
   seam confirmed present (§17.1). Also `pipe_design.md §11 #1`.
3. **[OPEN]** Multi-instance representation in the document (count+one-config vs
   `vector` field) — deferred to P4 (§17.2).
4. **[OPEN]** Non-reproducible-derived storage location (embedded base64 vs bundled
   sibling).
5. **[OPEN]** Metadata master persistence location & catalog scope (editor-era).
6. **[OPEN]** Coordinate-space/geometry full model, and the reference-frame choice — §11.
7. **[OPEN]** `edit_session` final name.
8. **[PARKED]** Derived-store eviction/GC/invalidation policy; exact cache-key hashing;
   tap/injection API shape; schema-adoption trigger; preview↔export parity details;
   `vc_image_spec` field set (`pipe_design.md §11 #4`).

## 14. Linkage & learning-build split

`pipe_design.md` owns the **execution substrate**; this doc owns the **editing model**.
They meet at `build_pipeline` (edit state → `vc_pipeline`) and at three amendments this
doc requests to the `pipe_design.md` `run()` contract, to be reconciled there when built:
**(a)** a cancellation token, **(b)** `taps` + `injections` (read/write any slot),
**(c)** a resolution/ROI render request.

**Learning-build split:** Claude scaffolds interfaces and framework plumbing
(`edit_session`, `kv_store`/adapters, `metadata` interface, registry, the serialization
framework); the user writes the rep logic (settings→stage derivations in
`build_pipeline`, stage `params`/`process` bodies, validation).

## 15. Reference studies — the five engines (2026-07 session)

> **Method & confidence.** Findings gathered by research agents from primary source and
> official docs. Some file/function specifics were obtained via summarizing fetches
> (flagged by the agents as "shape-accurate, not byte-verbatim"); vkdt's caching
> description was partly inferred from structure. Treat concrete names as strong leads,
> not personally byte-verified facts. Sources consolidated in §15.6.

### 15.1 The serialization spectrum — the core lesson
Two bad poles, and a shared escape hatch:
- **RawTherapee pole** — readable `.pp3` (Glib KeyFile INI), total per-key control, but
  **~5900 lines of hand-written `save()`/`load()`** and a parallel `ParamsEdited` bool
  struct kept in sync by hand; a param's name/default/range/mapping/edited-flag live in
  3–5 disconnected places.
- **darktable pole** — generic serialization, *zero* per-module glue, but the wire form
  is `memcpy`'d struct memory → **hex-encoded blob in XMP**, layout-brittle (reordering
  two floats breaks old files; forces a `module_version` bump + `legacy_params` branch).
- **The escape (GEGL, vkdt; and what darktable's introspection would enable if not
  binary):** a **declarative per-parameter descriptor** as the single source of truth —
  from it, serialize + deserialize + default + range + UI + CLI are all *generated*, and
  output stays readable. All five agents independently recommended this for us. It is
  our §18 mechanism, and it is the reason named-field JSON collapses most of darktable's
  `legacy_params` cascade into "unknown field ⇒ default."

### 15.2 Params & serialization, per engine
- **RawTherapee** — `ProcParams` = one master struct of ~30 per-*tool* sub-structs
  (user-facing); defaults in constructors + a global `setDefaults()`; no schema (plain
  fields). `.pp3` via Glib KeyFile; `ppVersion` integer (reported `353`, 2025-07-22)
  distinct from app version; back-compat via inline `if (ppVersion < N)` migrations
  scattered through `load()`. `ParamsEdited` tracks "was this field set" (powers partial
  presets/paste/batch). `ImProcCoordinator` reads across `ProcParams` to drive processing
  stages that are **not** 1:1 with tools. **We adopt** the user-facing per-tool document;
  **we fix** the whole-`ProcParams` coupling (narrow slices) and the hand-written
  duplication (§18); **we defer** `ParamsEdited` (§17.4).
- **darktable** — `dt_iop_params_t` per module; `DT_MODULE_INTROSPECTION(version, type)`
  + `$MIN/$MAX/$DEFAULT/$DESCRIPTION` field comments; a build-time Perl pass
  (`tools/introspection/parser.pl`) generates an introspection table
  (`dt_introspection_field_t`, offset-based). `legacy_params()` migrates old param blobs
  version-to-version. History serialized to XMP (`darktable:history`) as gzip+hex blob.
  **We adopt** introspection-as-metadata + version-keyed migration + the (operation,
  instance) stable key; **we avoid** hex blobs (named JSON) and the C-preprocessor
  codegen (type-safe C++ descriptors instead).
- **GEGL** — every op is a GObject class; every param a **`GParamSpec`** (name, GType,
  default, min/max, nick/blurb) queryable via `gegl_operation_list_properties` — one
  reflection layer drives serialize + UI + CLI, *no per-op code*. Authoring via
  `gegl-op.h` "chant" X-macros; `ui_meta` string k/v for extensibility. Serializes to
  readable XML and a terse one-line "chain". **No versioning** (a named gap they told us
  to close). **We adopt** the reflection-first principle (rebuilt as plain C++ `PropDesc`
  without GObject); **we add** explicit `doc_version` + migration from day one.
- **vkdt** — module *class* = a directory of declarative files: `params`
  (`name:type:count:default`), `params.ui` (widget + ranges, **separate** from data),
  `connectors` (typed named ports). Graph is a line-oriented token `.cfg`
  (`module:` / `connect:` / `param:`). Order is **derived** by topological sort, not
  stored. Module→node expansion (authoring graph vs execution graph). **We adopt** the
  data-schema-vs-UI split and the authoring-vs-execution-graph idea. **vkdt's own
  self-critique we heed:** put min/max in the *data* schema too (not UI-only), else
  headless validation can't clamp.

### 15.3 Buffer & cache models — the comparison
| | Owns buffers | Stage sees | In-place | Cross-graph reuse | Cache |
|---|---|---|---|---|---|
| **darktable** | pixelpipe cache | separate `ivoid`/`ovoid`; allocates neither | only colorspace (then invalidates) | LRU cache-line pool (→2 = ping-pong for export) | **per-module chained hash** `key(N)=hash(key(N−1),op,inst,params,roi)` → recompute-from-here-down |
| **GEGL** | framework (tiled per-node) | in/out `GeglBuffer`+`roi` | **yes for point ops** (`want_in_place`, `gegl_can_do_inplace_processing`, single consumer); area never | none (sparse tiles + global LRU + swap) | per-node + **region-scoped** invalidation (`get_invalidated_by_change`) |
| **vkdt** | offset allocator over one GPU heap | its WRITE connector's output | via aliasing, not input-overwrite | **liveness aliasing** (`nid_last_ref` → free-to-pool → offset reclaimed; `s_conn_protected` pins) | none (flag-driven full re-record; relies on GPU speed) |
| **RawTherapee** | ~5–7 persistent `Imagefloat*` | mutates the working image | **yes for scalar tools**; separate at neighborhood boundaries | the few big buffers are reused (fixed) | coarse `todo`-bitmask suffix recompute (`updatePreviewImage`) |

Lessons extracted: **(1)** three of four have the *pipeline* own output buffers (only
RT, a fixed pipeline, mutates persistent buffers). **(2)** in-place is always
**category-gated** (point vs area) where it exists. **(3)** darktable confirms the
purity→caching mechanism verbatim (*"only works because modules are pure functions of
(params, input, roi)"*). **(4)** vkdt confirms **liveness-based** reuse is a real,
cheap, single-pass technique — the literal answer to "what if a buffer is needed
later?" (its `nid_last_ref` keeps it alive until its last reader). **(5)** the "right"
buffer/cache strategy differs by **cost model**: darktable optimizes *compute* (fine
cache), vkdt optimizes *memory* (aliasing, no cache), RT optimizes *simplicity*, GEGL
optimizes *locality* (tiles/ROI). Our pure-stage contract supports all four schedules;
we pick per product.

### 15.4 Ordering
darktable separates **history/interaction order** from **`iop_order`** (execution
position), joined by the `(operation, instance)` stable key, with a canonical default
(e.g. `V50`) overridable to `CUSTOM`. We adopt: canonical order in code now; the doc
does not store order; stage-declared priority [LATER].

### 15.5 Halide — algorithm/schedule (transferable lesson)
Halide splits the *algorithm* (`Func`/`Var`, pure "what") from the *schedule* (tiling,
vectorize, parallel, `compute_at` — "how"), with the invariant "same bits regardless of
schedule." This validates our two-layer design: **stage = algorithm (pure `process`)**,
**pipeline = schedule (buffers, caching, later parallelism/tiling)**. Purity is exactly
what lets scheduling/caching be layered on afterward without touching stage semantics.
Caveats we do **not** import: Halide binds params at *compile* time (`GeneratorParam`) —
our editor needs runtime-mutable params; and good schedules are hard/manual — we keep
"how" internal and default-good.

### 15.6 Sources (as reported by the agents)
- **vkdt** — `github.com/hanatos/vkdt` `src/pipe/{graph.h,module.h,connector.h,graph-io.c,graph.c,graph-run-nodes-allocate.h}`, `modules/readme.md`, `modules/i-raw/params`, `doc/core.md`.
- **GEGL** — `gegl.org/{documentation,gegl-chain.html,operation-api.html,gegl-operation.h.html}`, `developer.gimp.org/api/gegl`, GObject `GParamSpec` docs.
- **Halide** — `halide-lang.org`, tutorials 5/8/15; Ragan-Kelley et al., "Decoupling Algorithms from Schedules," PLDI 2013 / CACM 2018.
- **darktable** — `github.com/darktable-org/darktable` `src/develop/{imageop.h,pixelpipe_hb.c,pixelpipe_cache.c}`, `src/common/{introspection.h,iop_order.h,history.c,exif.cc}`, `tools/introspection/parser.pl`, `iop/exposure.c`; DeepWiki.
- **RawTherapee** — `github.com/Beep6581/RawTherapee` `rtengine/{procparams.h,procparams.cc,improccoordinator.h,improccoordinator.cc,refreshmap.cc}`, `rtgui/{paramsedited.h,ppversion.h}`; RawPedia.

## 16. Decision levers

For each fork: the choice, the lever that tipped it, and the status.

| # | Fork | Decision | Lever (what tips it) |
|---|---|---|---|
| L1 | edit-stack vs user-built graph | **stack → compiles to graph** | fixed-pipeline product; DAG deferred for fan-in; matches dt/vkdt |
| L2 | interaction order vs processing order | **canonical in code; doc stores no order** | order carries color-science correctness; configurable parts are *params*, structure is stable |
| L3 | document = stage params vs user settings | **user settings; stages get narrow slices** | RT whole-`ProcParams` coupling; cache precision; testability |
| L4 | params: variant bag vs typed struct | **typed struct, narrow slice** | hot-path directness; avoid stringly-typed; codebase consistency (`slot<T>`) |
| L5 | serialization: hand-written vs schema-driven | **hand-written now, schema later** | few stages now; surface friction; schema is the end-state (§18) |
| L6 | format: JSON vs XMP vs INI | **JSON (nlohmann)** | interop matters only for metadata; XMP RDF + GPL cost; edits are app-specific |
| L7 | buffers: in-place vs new vs pooled | **pipeline-owned, pure stages, pooling later** | purity enables it; memory-budget headroom; in-place safety is *global* |
| L8 | cache key: params-only vs +input-hash | **params slice + input hash (chained)** | correctness — params-only returns stale downstream ("metaphysical dependency") |
| L9 | Kind A/C: unified vs separate stores | **separate stores, unified access (`edit_session`)** | different durability/format/interop; false-abstraction test |
| L10 | interface: one store vs role-specific | **shared `kv_store`; typed + blob adapters** | substitutability test; the byte-KV is the real shared seam |
| L11 | assembly: hardcoded vs registry vs data | **construction registry now; code assembly; data-graph later** | wiring is always explicit; dynamic structure resists pure data |
| L12 | determinism: automatic vs engineered | **engineered** | IEEE ops deterministic, but source ≠ computation (FMA/reassoc/transcendentals) |
| L13 | immutability: convention vs type-guarantee | **type-guarantee (`vc_image_writer`/`seal`)** | greenfield C++ affords it; stronger than all four studied engines |
| L14 | metadata: in edit-doc vs subsystem | **separate subsystem behind interface** | standards/interop matter for metadata; avoid bottling up |
| L15 | "was-set" tracking | **defer** | full doc handles single-image; sparsity/presets are later |
| L16 | non-reproducible derived: cache vs persist | **persist with the edit** | can't regenerate identically (model drift) ⇒ behaves like intent |
| L17 | error model | **exceptions for fatal; degradation as stage logic** | robustness is per-stage; no partial-failure framework needed |
| L18 | display transform: edit vs view | **view — terminal stage, output-target config** | it is *how you view*, not *what you edited* |

## 17. Scenarios & scalability

Each scenario: does the design handle it, by what mechanism, does it scale, what's
deferred.

### 17.1 Fan-in — HDR merge (N brackets → 1) [scales; build at P4]
The product's core shape. The current runner is **linear**, but the design allows
fan-in *additively*: (a) `std::any` packets already carry a `vector<vc_image>`;
(b) `connections_` is a plain vector — nothing restricts one `to`-port to a single
connection; (c) insertion order already gives producers-before-consumer, which is the
merge's shape (no topo sort needed for it). **Missing = additive:** a list-valued-slot /
`collect` mechanism + `run()` gathering. `pipe_design.md` already flags this as
[LATER]/[OPEN #1] and states "the interface does not preclude a DAG." **Verdict:**
scales; build against the real merge at P4.

### 17.2 Multi-instance (N aligns, 2 blurs) [scales; build at P4]
Execution already supports it (`i_pipe::name()` per-instance; the pipe design names "N
align stages" explicitly). The homogeneous case needs *no* document multiplicity — one
`align_settings` + `bracket_count`, and `build_pipeline` instantiates N. Heterogeneous
instances become a `vector<settings>` field, handled by the container serializer (§18).
**Verdict:** scales; document representation deferred to P4.

### 17.3 Custom config types (matrix, curve, structured mask) [scales]
Cost is **per distinct type, written once, reused everywhere** (O(types), not O(uses)):
a per-type serializer (a `mat3` writes as 9 numbers) and/or a nested sub-schema (a
curve = a struct with its own schema; recursion), plus a container serializer for
variable-length (`vector<brush_stroke>`). This is the cereal/boost.serialization pattern
— proven to scale. The complexity concentrates in **one write-once generic serializer**
(dispatch across scalar/custom/nested/container); per-stage code stays trivial. **Does
not** hold opaque bulk (embeddings, raster pixels) — those are Kind C blobs, correctly
not in the schema/document.

### 17.4 Masks — parametric / rasterized / AI [scales; two homes]
- **Parametric mask** (brush strokes, curve nodes, feather) = **Kind A intent** →
  stored *as description* in the document (small), **not** as pixels. The rasterized
  pixel mask = Kind C-repro, computed on render and cached.
- **AI/ML object mask** = **Kind C-nonrepro** → persisted as data with the edit
  (regeneration would differ — model drift), like Lightroom **[unverified]**.
- **"Was-set" (`ParamsEdited`)** — needed only for *partial-overlay* presets (a preset
  that sets only some params atop existing edits). Deferred; when built, sparsity (a
  document that stores only set keys) gives it for free without RT's parallel bool struct.

### 17.5 Deghost re-render / caching [scales; the merge is not one-shot]
Adjusting deghosting post-merge is a re-render. The chained-hash cache (§5.1) makes it
cheap: deghost's params change ⇒ deghost-down recomputes, but **alignment/features
(upstream, unchanged) are a cache hit** — no re-align. On device this needs the
*persistent* derived store (§5), not a throwaway cache — hence "core, not optional." If
the burst is closed and reopened, a disk-backed derived store avoids re-aligning
(reproducible-but-precious).

### 17.6 Preview / pyramid / preview-export parity [design now, build later]
The render request carries `{level/resolution, ROI}`. The hard part is **scale-aware
params**: spatial params must scale with resolution so the downsampled preview matches
the full-res export. This is a property of how params are *interpreted*, designed in now
(spatial params carry units / scale with the render level), built later.

### 17.7 Portability / reproducibility [see §9]
Scales *if* the three requirements are met deliberately: bundle non-reproducible derived;
pin engine/process version; engineer determinism. The design has homes for all three
(bundle = transfer unit; version = doc field; determinism = algorithm discipline).

### 17.8 Overlays [scales]
Image-affecting overlays (gradient, texture, baked mask-visualization) = **stages**,
conditionally inserted by `build_pipeline`. UI-only overlays (cursor, guides,
live mask preview) = **display layer** reading a tapped mask output. The user may
request mask overlays *baked into* the render — that is simply a conditional compositing
stage fed the mask via a wire/tap.

### 17.9 Derived data as first-class output (warps, features, embeddings) [scales]
All ride the `std::any` packet on typed slots (§5.3); expensive/shared ones become
stage outputs → independently cacheable, tappable, persistable, inspectable. Uniform
with pixels; no special-casing.

## 18. The schema / serialization mechanism [LATER — the end-state]

Captures the §15.1 escape hatch as a concrete C++ design. Declare each field **once**;
generate save/load/describe. Understood and endorsed; adopted when hand-written
per-field serialization (the [NOW] baseline) starts to hurt.

```cpp
// One schema entry: templated on the owning struct C and the field type T,
// because a pointer-to-member has type `T C::*`.
template <typename C, typename T>
struct field {
    std::string_view name;      // serialization key / UI label
    T C::*           member;    // pointer-to-member: reach the field generically (p.*member)
    std::optional<T> min, max;  // range metadata — no home in the plain struct itself
};
template <typename C, typename T> field(std::string_view, T C::*, T, T) -> field<C,T>;
template <typename C, typename T> field(std::string_view, T C::*)       -> field<C,T>;

struct exposure_params { double exposure = 0.0; double black = 0.0; bool auto_exp = false; };
inline constexpr auto exposure_schema = std::tuple{      // tuple: heterogeneous field types
    field{"exposure", &exposure_params::exposure, -18.0, 18.0},
    field{"black",    &exposure_params::black,     -1.0,  1.0},
    field{"auto_exp", &exposure_params::auto_exp},        // unranged
};

// ONE generic save, for every stage that has a schema:
template <typename Params, typename Schema>
void save(const Params& p, const Schema& schema, doc_writer& out) {
    std::apply([&](auto... f) {                          // apply: tuple -> separate args
        (( out.set(f.name, p.*(f.member)) ), ...);       // fold: one out.set() per field
    }, schema);
}
```

Why each template feature is present (feature follows problem):
- **`template<typename T>`** — the field *type* varies (double/bool/int).
- **second param `C`** — the owning struct varies (`exposure_params`/`deghost_params`).
- **`T C::*` (pointer-to-member)** — reach a field *by name* generically; `p.*member`
  reads the field out of a specific object. Not a template feature itself — an older C++
  facility; `&C::m` describes "the m slot, in general," `p.*(&C::m)` == `p.m`.
- **deduction guide** — stop spelling `field<exposure_params,double>` by hand.
- **`std::tuple`** — hold descriptors of *different* types in one list (a `vector` is
  homogeneous; a tuple is a fixed heterogeneous bundle).
- **`std::apply` + parameter pack + fold** — *loop* over the tuple at compile time. The
  variadic lambda parameter `auto... f` **gathers** the args into a pack; the fold
  `(expr, ...)` **spreads** it, emitting one `out.set(...)` per field. `std::apply`
  bridges tuple → separate args; the pack is created by the parameter declaration, not a
  conversion. The fold *is* the hand-written save body the compiler writes for you.
- **concept** (e.g. `param_value`) — constrain `T` to real param types with clear
  errors (mirrors the existing `vc_pixel_element` concept).

Custom types (§17.3): broaden to "types with a `write_value`/`read_value`" (per-type
serializer) + nested sub-schemas + container serializers. Opaque bulk stays Kind C.
Note the schema carries **more than serialization** (ranges, UI hints, cache-key
material), which is why a pure serialization library (e.g. cereal) wouldn't replace it.

## 19. Glossary

- **Non-destructive editing** — original preserved; result computed from
  original + a description of operations.
- **edit document** — the saveable, structured record of Kind-A edit settings.
- **`edit_session`** — the per-image aggregate composing source + edit doc + metadata +
  derived-store handle (§3).
- **sidecar** — a companion file stored next to the image, holding the edit document.
- **serialize/deserialize** — object ↔ savable bytes; **JSON** is the text encoding.
- **`kv_store` / `doc_writer` / `derived_store`** — shared byte store + typed adapter
  (Kind A) + content-hash adapter (Kind C) (§7).
- **narrow slice** — the small, per-stage subset of config a stage depends on (vs the
  whole document).
- **purity** — a stage's output depends only on (inputs, params); no hidden state, no
  input mutation. Enables caching/pooling/parallelism.
- **chained hash** — cache key `hash(own params + input hash)`; encodes upstream (§5.1).
- **point op / area op** — output pixel depends on the same input pixel (in-place safe)
  vs a neighborhood (needs a distinct buffer).
- **liveness** — analysis of when a buffer is dead (no future reader) so it can be
  reused; only the pipeline can know it (it is graph-global).
- **tap / injection** — read an intermediate slot / write a value onto a slot to skip
  its producer (§5.2).
- **pointer-to-member (`T C::*`)** — a typed handle to a struct field, applied with
  `.*` (§18).
- **fold expression / parameter pack** — a compile-time "loop" that emits one statement
  per element of a variadic pack (§18).
- **transfer bundle** — source + JSON doc + non-reproducible derived (§9).
- **process/engine version** — the render-path version recorded so an edit reproduces.
- **scale-aware param** — a spatial param that scales with render resolution so preview
  matches export (§17.6).
```