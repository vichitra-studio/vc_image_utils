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
  `build_pipeline`. Expensive results survive in the **derived stores** (§5), not in
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

`vc::edit::render_image(session, request)` is this whole chain — `build_pipeline`
then `run()` — collapsed into one callable (§8); `vc::edit::export_image` extends it
one step further, writing the result to a file.

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
is re-walked every render, but its cache persists (§15.3). **Rebuild is cheap by
construction:** stage objects are rebuilt fresh per render; only the expensive part
(pixels/derived data) is skipped, and it is skipped *by the cache*, not by keeping the
graph alive. The one exception, deferred: stages with genuinely expensive
*construction* (compiling a shader, loading a LUT) get an engine-level resource cache
keyed by `(kind, params-hash)` — **[LATER]**.

**Edits are truth; a failed render never corrupts them.** The edit state (§3) is the
one source of truth; the rendered image is disposable output. A render that fails or
is cancelled leaves the edit state untouched — it never partially applies. The
preview is simply left stale, tracked by a hash of the current edit state, and
re-rendered; because the derived-data cache (§5) retains every stage that *did*
succeed, a retry recomputes only from the point of failure onward, not from scratch.

**Two idioms, not one, and never blurred.** Two different immutability/mutability
idioms coexist by design, applied where each one's driver actually holds (full
treatment in §16 L19): **(A)** immutable-value + builder, where the driver is
*stable shared identity* — `vc_image`, captured metadata, cached derived data; **(B)**
a read/write capability split, where the driver is *least-privilege / CQS* — the
storage interfaces (§7), cancellation (§8). Edits themselves are genuinely mutable
state and are never smeared with either idiom's immutability.

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
- **C-repro** is a *performance* store: loss ⇒ recompute, not data loss. Lives in
  **`vc_cached_edits_table`** (§5, B6) — content-hash-keyed; **core on device** (re-aligning
  a burst per deghost tweak is unacceptable), even though the store *may* evict.
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

> **[SUPERSEDED 2026-07-18]** The sketch above (a single injected `derived_store&`)
> is superseded. **The session owns all durable state directly** — it does not
> reach out to a store handed in from outside: immutable `source_`, `edits_`, an
> **edited-metadata overlay** (mutable, session-owned — distinct from the image's
> own *captured* metadata, `shared_ptr<const i_image_meta>`, §6), a
> `vc_persistent_edits_table`, and a `vc_cached_edits_table` — the latter two behind one
> `i_edit_table` interface, replacing the old single `derived_store&` (§5, §7).
> Updated member set:
> ```cpp
> class edit_session {
>     vc_image             source_;        // immutable source
>     edit_document         edits_;        // Kind A (+ Kind C-nonrepro bundled here)
>     /* edited-metadata overlay */        // Kind B edited layer, mutable (§6)
>     vc_persistent_edits_table   persistent_;   // Kind C-nonrepro, pinned/saved (§5, §7)
>     vc_cached_edits_table    cache_;        // Kind C-repro, evictable, never saved (§5, §7)
> };
> ```
> **[DECIDED 2026-07-18, built — Part C-1]** The two derived stores are **injected**
> (not owned by value), as **two separate nullable non-owning pointers**
> (`vc_persistent_edits_table*`, `vc_cached_edits_table*`): they differ in both key scheme and
> serialization lifecycle and each outlives the session, so the session merely
> references them, and injection enables test/mock substitution. The metadata overlay
> is an **owning `unique_ptr<i_image_meta>`** (alias `image_metadata_handle`) — session-local
> mutable state, not an externally-owned backend — which makes the aggregate
> **move-only** (a snapshot copy would need a metadata `clone()`, a later rep). The
> built 5-arg ctor is `vc_edit_session(vc_image source, vc_edit_document edits,
> image_metadata_handle meta, vc_persistent_edits_table*, vc_cached_edits_table*)`, superseding the
> owned-by-value member sketch above. This also settles the metadata-handle question:
> one alias does **not** suffice — the image-composed (captured) metadata is a
> `shared_ptr<const i_image_meta>` (on `vc_image_info`) while the session's edited
> overlay is a `unique_ptr<i_image_meta>`; two distinct handle kinds.

> **[DECIDED 2026-07-20]** `meta` is now **mandatory**, matching `persistent`/`cache`:
> the ctor throws `vc::vc_exception` if `meta` is null, so `meta()` returns
> `i_image_meta&` (not a pointer) and stays `noexcept` — there is no "no backend
> attached" state to represent. This supersedes the "meta is NULLABLE" framing
> above; every caller must inject a real (even if trivial in-memory
> `vc_memory_image_meta`) backend.

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

`build_pipeline` is the **sole session-reader**: it is the only code that reads
`edit_session` state directly. It wires slots and derives each stage's params from
that state; a stage itself is never handed the session, only the narrow slotted
inputs `build_pipeline` chose for it (§4.2, §4.3, §8).

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

**Registry-erased construction [design NOW].** A stage is instantiated through the
registry, not `new`-chained directly: `register_stage<StageT>(kind, builder)`
captures, at registration, the concrete stage type together with its edit-layer
params-builder and constructor, behind one uniform call
`create(kind, name, session) -> stage_ptr`. No casts anywhere in the call path —
each stage's params type is fixed at compile time via the template itself, not
checked by an additional params-shape concept at registration (the schema
concept that once served this role, `vc_param_struct`, is superseded — §18).
This is what `build_pipeline` calls to get each stage it wires.

> **[REVERTED]** An intermediate shape moved each stage's `from_session` out to a
> free builder function living in the edit layer, supplied to `register_stage` at
> registration. That was reverted: `from_session` (e.g. `vc_blur_stage::from_session`)
> lives back on the stage as a static, sitting next to the params struct it
> produces. `vc_stage_registry::register_stage` accepts either shape identically —
> it only requires something invocable with `const vc_edit_session&`, so a pointer
> to a static member function (`&vc_blur_stage::from_session`) satisfies the same
> `vc_stage_builder_req` concept a free function would. The stage itself stays
> session-blind either way (§4.3) — `from_session` derives params from a
> session, but nothing in `process()` ever sees one.

### 4.3 Params on a stage
**A stage is a pure function of its slots (data) and its params (config)** — it sees
no session, no runner, no cache; everything it needs is pushed in at build time
(§8). A stage owns a **plain typed `params` struct** (no variant bag), built at
image-load and read directly in the hot path (`params_.exposure` — zero
indirection). `i_pipe` is **untouched**: params are resolved *before*
construction, at the factory boundary the `kind()` seam already anticipates
(`i_pipe.h:27-29`). A stage's params can be **empty** and absent from the document
(a plumbing stage like `align` has no user knobs — it exists because `deghost`
depends on it).

**Concrete params, not a runtime base class.** Params stay concrete per-stage
types (Design A), **not** a virtual/runtime params base class — a schema concept
once stood in for that standardization (§18, superseded). The one generic
consumer that materialized is a future runner computing a cache key, which goes
through `i_pipe::params_hash()` (§18's superseding note) instead; there is no
current generic UI/serialization consumer, and the stage's hot-path code never
sees `params_hash()` either.

### 4.4 Serialization [NOW: hand-written]
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
- **[SUPERSEDED 2026-07-25] schema-driven serialization**: a generic
  descriptor-based `save`/`load` mechanism (§18) was built, pulled forward from
  [LATER] by request, then removed after tracing its only real consumer
  (cache-keying) through in full — once `i_pipe::params_hash()` became a
  per-stage virtual method, it already gave the "uniform access across
  concrete types" the schema was meant to provide, and the schema's remaining
  contribution (saving a hand-written hash per params struct) didn't clear
  this codebase's own bar for keeping a mechanism. Hand-written JSON
  (`vc_edit_document_io.h`/`.cpp`, above) is the durable serialization path,
  not a stepping stone to something more generic (§18's superseding note).
- **Portability shape [design NOW; build LATER — see also §9]:** the source image
  stays pristine — never touched by serialization. What actually saves is a JSON
  **sidecar** holding the edit document + the edited-metadata overlay (§6) + **ids**
  that reference derived data, not the bytes themselves. A **companion blob store**
  sits beside the sidecar and holds the *persistent*-derived bytes those ids point
  at (§5) — **not** base64'd inline (base64 stays reserved for genuinely small
  binary, as above). **Export is one self-contained bundle**: sidecar + metadata +
  the referenced derived blobs, packaged together. The **evictable** cache (§5) is
  never part of this — it may optionally back a private warm-start mirror, but it
  is never saved or bundled. (This is the *portability* sense of "export" — a
  different, unrelated use of the word from `vc::edit::export_image`'s
  render-pixels-to-a-file sense, §8. The two share a name in this doc by accident
  of English, not by design; do not conflate them.)

## 5. Derived data (Kind C) — cache + persistent store [LATER; seam NOW]

> **[SUPERSEDED 2026-07-18]** The "three tiers" list below predates B6 and blurs
> the axis B6 uses to split the two derived stores: its middle tier describes a
> content-hash-keyed, "reproducible-but-expensive" persistent store — that is
> `vc_cached_edits_table`'s definition now, not `vc_persistent_edits_table`'s, which is
> **non-reproducible** and id-keyed (see the corrected paragraph below). Kept
> for narrative continuity only; the two-store split below is authoritative.

Three tiers, cut by "reliably reproducible?" (§2):
- **Transient in-memory cache** — cheap intermediates; evictable freely.
- **Persistent derived store** (content-hash-keyed "db": files or SQLite blobs) —
  reproducible-but-expensive (alignment, features). **Reliable, not casually evicted;
  core on device.** Reconstructible if lost (not source of truth).
- **Bundled with the edit** — non-reproducible derived (AI masks): persisted as data,
  travels with the document (§9).

**A reproducible cache and a non-reproducible persistent store are two concrete
stores behind one interface [design NOW; B6].** `vc_cached_edits_table`
(**reproducible**, content-hash keyed, may evict, never saved) and
`vc_persistent_edits_table` (**non-reproducible**, id-keyed, pinned, saved/bundled —
§4.4, §9) are **separate objects**, not one store with a policy flag: they
differ in **both** reproducibility/key-scheme **and** serialization lifecycle
(evictable vs. pinned/saved), and conflating them would force one object to
carry two independent axes of variance. Both sit behind one `i_edit_table`
interface (§7), so **access is source-agnostic** — a consumer reading through
the interface and slots cannot tell, and does not need to, whether the value
it got came from the cache or the persistent store.

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

**Runner memoization, mechanically [design NOW; B7].** Stages themselves are pure
and never touch the cache (§4.3) — memoization is entirely the runner's job. At the
start of a `run()`, a cheap **chained-hash pre-pass** walks the graph and computes
each node's `hash(params-via-i_pipe::params_hash() + input-hashes)` (§18's
superseding note), deciding hit/miss for every node before any pixels move; a
changed upstream hash automatically propagates downstream, so the pre-pass alone
detects everything that must recompute. Only the misses actually run. Running is
**demand-driven**: a miss on some slot causes the runner to run *that slot's
producer* — the upstream stage wired to it — which is the same mechanism §5.2's
tap/injection design already describes from the inject side (inject ⇒ skip the
producer; here, a genuine miss ⇒ *run* the producer). Params reach the cache only
**via `i_pipe::params_hash()`, a per-stage hand-written combine** (§18's
superseding note) — the params object itself is never stored *in* the cache; the
store holds outputs, keyed by hash. (Store access itself is spelled `get`/`set`
uniformly, per the Q2 naming decision — §7, §19.)

**[DECIDED 2026-07-25] A cache-table instance is never shared across two different source
images.** Each `vc_cached_edits_table`/`vc_persistent_edits_table` instance's
lifecycle is 1:1 with one editing session (§3's "each outlives the session"
language is about non-owning-reference safety, a related but distinct point).
This is *why* no source-image hash is needed anywhere in the cache key: the
table itself already scopes every key to one source image, so folding a
source-image hash into the key would be redundant.

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
cheap and single-consumer. This uniformity is exactly what makes the two-store
split behind `i_edit_table` (§5, B6) invisible to a consumer: a slot value is a
slot value regardless of whether `vc_cached_edits_table` or `vc_persistent_edits_table`
produced it.

## 6. Image metadata (Kind B) — behind an interface [LATER; seam NOW]

Ratings, flags, keywords, EXIF, color matrices, IPTC/copyright, GPS. Unlike edits,
**standards and interop matter here.**

```cpp
struct metadata {
    virtual std::optional<value> get(field) const = 0;   // read EXIF/IPTC/XMP
    virtual void set(field, value) = 0;                  // rating, copyright, keyword
};
// implementations: per-platform backends (Apple Image I/O / Android ExifInterface /
// desktop parser / DNG SDK) — NEVER exiv2
```

> **Revises the earlier exiv2-on-desktop choice (B4, 2026-07-18).** The line below
> previously read "exiv2 is GPL → desktop only," with `exiv2_metadata`/
> `dng_sdk_metadata` as the two implementations. That choice is now revised:
> exiv2 must **never** enter the core value type on *any* platform — the backend is
> **permissive and per-platform** everywhere (Apple Image I/O, Android
> ExifInterface, a desktop parser, and the DNG SDK), not split GPL-desktop /
> permissive-device. The "≥2 real implementations + a polymorphic caller"
> substitutability argument below still holds — it now rests on the per-platform
> backends rather than on exiv2 vs DNG SDK. **[Ambiguity — flagged, not resolved
> here]** the collation records this as a plain Part-B decision, not as one of the
> four explicit supersessions; it is recorded here as a B4-driven revision of this
> section's earlier content rather than as a fifth formal supersession.

- **Licensing (load-bearing):** the backend is **permissive, per-platform**, chosen
  and instantiated by the **loader**, behind the interface — Apple Image I/O
  on-platform, Android ExifInterface on-platform, a desktop parser, the DNG SDK for
  DNG output. exiv2 (GPL) is excluded everywhere, not just off-desktop. The
  interface is what lets the same calling code run any backend — and here **both
  interface criteria hold** (multiple real implementations + a polymorphic caller),
  unlike the Kind A/C case (§7).
- **Source of truth** = the app's model; XMP is a *sync target* for interop (LR-style
  **[unverified]**), not the only home.
- **MVP scope:** EXIF passthrough + copyright + color matrices into the output DNG.
  Full catalog metadata (ratings/flags/keywords) is editor-era; the interface
  accommodates it so we are not bottled up.
- **`field`/`value` stay `std::string` placeholders [LATER; B4].** The interface
  above is intentionally under-typed for now — typing `field`/`value` properly
  pre-bakes the full field-set decision (which EXIF/IPTC/XMP tags exist as named,
  typed fields), which is deferred; string placeholders keep the interface usable
  without committing to that set early.
- **Composition, not fusion, with the image [design NOW; B4].** Metadata stays a
  separate subsystem (L14, §16) but **rides the image**: `vc_image_info`
  **composes** a `shared_ptr<const i_image_meta>` rather than metadata and image
  geometry being fused into one type. Physical pixel dimensions always live on
  `vc_image_info` itself, never inferred from metadata — the reason is EXIF
  **orientation**: an EXIF-rotated image's *display* width is not its *buffer*
  width, so "dimensions" cannot be read off metadata alone without the
  orientation-application step in between. Because metadata rides the image, it
  reaches stages the same way pixels do — through **typed slots** (§5.3) — with no
  separate metadata-passing channel. A **mask is simply an image whose composed
  metadata is null** (a mask has no EXIF story of its own).
- **Two layers, merged on export [design NOW; B4].** *Captured* metadata
  (immutable, `shared_ptr<const i_image_meta>`, lives on the image) and *edited*
  metadata (mutable, an overlay owned by `edit_session`, §3) are distinct layers,
  not one mutable store. They merge only **on export** (mechanics — [LATER]).
  This is also why one `image_metadata_handle` alias cannot cover both: the
  captured handle and the overlay handle are different kinds of thing (§3).
- **Image writer never parses EXIF [design NOW; B5].** `vc_image_writer` composes
  a **ready-made** `vc_image_info` (including its `shared_ptr<const i_image_meta>`)
  and seals it — it does not itself read or parse EXIF bytes. Constructing the
  `i_image_meta` object is the **loader's/backend's** job, handed to the writer
  already built. This keeps EXIF-parsing concerns out of the writer entirely
  (separation of concerns): the writer's job is composing a sealed, ready image,
  not decoding metadata formats.

## 7. Storage interfaces [LATER; shape settled]

"Program to an interface" applies — at the **storage engine**, not the data role
(§16 L10). The substitutability test: an interface earns its place only when there are
≥2 real implementations *and* a caller uses them interchangeably. Kind A vs Kind C
**fail the second criterion** (no caller is polymorphic over "settings vs blobs"; they
have opposite miss- and durability-semantics), so they are **not** one caller-facing
interface. What they *share* is the storage engine beneath:

- **`kv_store`** — shared low-level `put(key, bytes)` / `get(key) → optional<bytes>`.
  Backends: `file_store`, `sqlite_store`, `memory_store` (tests). **[SUPERSEDED
  2026-07-25]** Originally both roles built on it; Kind A's role (`doc_writer`/
  `doc_reader` below) is removed, so `kv_store` (shipped as `i_table`) is now
  Kind C's storage engine only.
- **`doc_writer`/`doc_reader`** — this doc's illustrative names for what shipped
  as `vc_edit_settings_writer`/`vc_edit_settings_reader`: a typed adapter over
  `kv_store` for Kind A, hiding misses behind defaults (total get).
  **[SUPERSEDED 2026-07-25]** — removed along with the schema mechanism that
  was their sole reason to exist (§18's superseding note); Kind A's only
  surviving persistence path is the hand-written JSON in
  `vc_edit_document_io.h`/`.cpp` (§4.4).
- **`derived_store`** — *content-hash* adapter over `kv_store` for Kind C; **exposes**
  misses (⇒ recompute). **Separate instance, different durability** (may-evict vs the
  document's never-evict) — the one distinction to preserve.

> **[SUPERSEDED 2026-07-18]** The single `derived_store` bullet above (built, in the
> code that existed this session, over a raw `i_kv_store&`) is superseded. Kind C
> storage is now **two concrete implementations behind one `i_edit_table`
> interface** — `vc_cached_edits_table` and `vc_persistent_edits_table` (§5, B6) — not one
> store object. The "one distinction to preserve" (may-evict vs never-evict) is now
> expressed as two *separate types*, not one type's policy setting: they differ in
> **both** eviction policy **and** serialization lifecycle (whether the store is
> ever saved/bundled, §4.4). A consumer reads/writes through `i_edit_table` and
> typed slots without knowing which concrete store answered — **source-agnostic
> access** (§5).

**Reader/writer standardized as concepts, not one inherited interface
[design NOW; B11].** `vc_optional_reader_req`/`vc_writer_req` are **concepts**,
checked at compile time, not a base class every backend derives from — this
matches the `vc_pixel_element_req` style already used elsewhere in this
codebase rather than introducing virtual dispatch where it isn't needed.
**[SUPERSEDED 2026-07-25]** This section originally described **two reader
flavors**: total-get (settings-style: a miss silently returns a default —
what the now-removed `doc_reader`/`vc_edit_settings_reader` did) vs
miss-exposing (cache-style: a miss returns `optional`, so the caller can
decide to recompute — what `vc_optional_reader_req`/`vc_cached_edits_table`
do, §5.1). With `vc_edit_settings_reader`/`vc_edit_settings_writer` removed
(§18's superseding note), miss-exposing is the only flavor with a living
conformer in this codebase; total-get remains a documented *possibility* (a
future settings-writer could still need it) rather than a currently-embodied
one.

`edit_session` (§3) composes these; it is a facade (has-a), not a shared interface (is-a).

> **Naming (recorded here; applies to code when built).** The
> low-level byte-store family is renamed: the
> `kv_store` class above becomes `i_table`, its in-memory runtime backend
> becomes `vc_memory_table`, and the `kv_bytes` alias becomes `data_bytes`
> (alias, no prefix, per the naming rule at §14/§19). Store access verbs are
> standardized to **`get`/`set` everywhere** — the existing `optional`/default-
> fallback return already carries the miss semantics (total-get vs miss-exposing,
> above), so the verb name itself does not need to encode it. **Naming-state
> flag:** this doc's sketches above write `kv_store` unprefixed, as illustrative
> pseudocode predating the `vc_` class-prefix convention (§14.1); the
> renames in this note are recorded against the *intended* prefixed identifiers
> (`vc_kv_store → i_table`, etc.), not against the literal unprefixed
> spelling shown in the sketches above. The same sketch's `put(key, bytes)`
> likewise predates the get/set verb standardization above; read it as
> `set(key, bytes)` under the renamed identifiers.

> **[DECIDED 2026-07-20]** The byte-store family's interim names
> (`i_data_store`/`vc_memory_data_store`, built earlier this session under the
> naming this note originally recorded) are superseded by `i_table`/
> `vc_memory_table` above — `i_data_store` sat one word away from the
> consumer-facing `i_edit_table` (§5, B6) for two genuinely different
> abstraction layers, which recreated the vagueness the original rename was
> meant to fix; `i_table` instead reads as `i_edit_table`'s lower-level,
> generic sibling. Same day: the reader concepts are renamed
> `vc_reader → vc_optional_reader` and `vc_total_reader → vc_defaulted_reader`
> (named after each one's return shape — `optional<V>` vs. a defaulted `V` —
> rather than the more jargon-y "total function" sense of "total"), and all
> three concepts (`vc_optional_reader`, `vc_defaulted_reader`, `vc_writer`)
> move out of the shared `vc_store_concepts.h` (deleted) into the header of
> their sole or primary consumer — `vc_optional_reader` into
> `vc_edit_table.h`, `vc_defaulted_reader` into `vc_edit_settings_store.h`
> (both since removed — see the follow-up note below), and `vc_writer`
> (shared by both) into `vc_table.h`, the common dependency both already had
> (also since relocated further — see below). A concept cannot be a class
> member (C++20 restricts `concept` declarations to namespace scope), so
> "under the relevant class" was not on the table — this is the closest
> equivalent, no concept left owned by a dedicated concepts-only file.
>
> **Follow-up [SUPERSEDED 2026-07-25].** `vc_defaulted_reader` (by then
> `vc_defaulted_reader_req`) and its host `vc_edit_settings_store.h` are both
> removed — their sole reason to exist (serving the schema mechanism's write
> target) no longer applies (§18's superseding note). `vc_writer` (by then
> `vc_writer_req`) has moved again, out of `vc_table.h` and into
> `vc_edit_table.h`: with `vc_edit_settings_writer` gone, only one consumer
> family remains (`vc_cached_edits_table`/`vc_persistent_edits_table`), so by
> this same blockquote's own reasoning it belongs beside its sole conformers,
> not in the shared byte-store header.

> **[DECIDED 2026-07-25] `_req` suffix convention.** Every concept in this
> codebase now carries a `_req` suffix (`vc_optional_reader_req`,
> `vc_writer_req`, `vc_pixel_element_req`, `vc_stage_builder_req`) — the
> suffix marks a name as a compile-time *requirement* check (a concept),
> distinguishing it at a glance from an ordinary type. This note names the
> four concepts that survive the schema-removal cleanup; two others that
> also gained the suffix in code (`vc_param_struct_req`,
> `vc_defaulted_reader_req`) are omitted since both are deleted in the same
> cleanup (§18's superseding note) and never had a lasting home in this doc.

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
  conditionals) stays code. **Erasure shape [design NOW; B10]:**
  `register_stage<StageT>(kind, builder)` → uniform `create(kind, name, session) ->
  stage_ptr`, concept-enforced, no casts; full detail on the `from_session` static
  at §4.2.
- **Stage purity, restated for the runner [B1]** — a stage sees only its slots and
  params, never the session, the runner, or the cache (§4.3). This is the invariant
  the rest of this list (buffers, caching, concurrency) leans on.
- **Runner memoization [design NOW; B7]** — before any stage runs, a cheap
  chained-hash **pre-pass** computes every node's
  `hash(params-via-i_pipe::params_hash() + input-hashes)` and decides
  hit/miss for the whole graph; a miss causes the
  runner to run that slot's **producer** on demand. Mechanics, and the relation to
  §5.2's tap/injection design, are in §5.1 — this is the runner-level summary.
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
- **`render_image` / `export_image` [NOW, scaffolded 2026-07-21].** `build_pipeline` →
  `run()` (the diagram in §1) is composed into one callable,
  `vc::edit::render_image(session, request) → vc_image` — the ONE place both a future
  preview path and export call, rather than each re-deriving the composition.
  `vc::edit::export_image(session, vc_export_config)` renders (always at the default,
  full-res whole-image request — see below) and writes the result through a
  `vc::io::stb_image_writer` it constructs itself. Both currently throwing shells.

  `vc_export_config { path; write /* vc::io::write_config */ }` deliberately excludes
  two things a first pass might expect:
  - **No render-request field.** `vc_render_request`'s fields are either a no-op today
    (no stage is resolution-aware yet) or an unsettled coordinate frame (§11) — a
    resize/crop-on-export knob is a real future feature, but is designed once
    resolution-awareness and the ROI frame actually land, not pre-baked into the config
    shape now. `write` covers what export genuinely owns today (destination format);
    it is the extensibility seam for quality/color-profile knobs later — new fields on
    `vc::io::write_config`, no signature change.
  - **No injected writer.** `i_image_writer` fails the substitutability test (§7): one
    real implementation (`stb_image_writer`), no caller chooses between two — so
    `export_image` constructs it directly, matching `main.cpp`'s existing read/write
    round trip. Contrast `i_table` / `i_image_meta`, which DO clear that bar and stay
    injected.

  **Port discovery is an open question, deliberately not settled here.**
  `render_image`'s body needs to feed `session.source()` onto the open input of the
  graph `build_pipeline` returns and harvest its open output, but `vc_pipeline` exposes
  no public port query today (§4.2's "no public stage accessor" is deliberate), and the
  SPINE's ports are only knowable because `build_pipeline` currently assembles exactly
  one `vc_passthrough_stage`. A general mechanism — a port-query API on `vc_pipeline`,
  or `build_pipeline` returning pre-wired inputs alongside the pipeline — is left for
  whoever writes `render_image`'s body, not pre-decided now.
- **Run context [design NOW; B8/B9] / Cancellation** — `run()` takes a
  **`vc_render_context`** (see the note below) that carries a cooperative cancellation
  token, checked between stages and at checkpoints inside long stages. Interactive
  re-render (drag ⇒ cancel in-flight ⇒ restart) needs it.

  > **[SUPERSEDED 2026-07-18]** `vc_pipeline::run(inputs, const
  > vc_cancellation_token& = {})` is superseded by `run(inputs, const
  > vc_render_context&)`. The run context is a **control-only host [B8]**:
  > cancellation lives there now, a **progress**-reporting seam is added
  > **[LATER]**, and — deliberately — it does **not** hold the derived-data
  > cache (§5); control and cache stay separate concerns threaded independently
  > into `run()`.
  >
  > **Cancellation mechanics are unchanged in kind, wrapped differently [B9].**
  > The existing value-semantic **`token`/`source` split** (read-only `token`
  > checked by the pipeline; write-only `source` held by the caller that
  > requests cancellation) is **kept** — justified by least-privilege + CQS, and
  > by staying cheaply copyable — and is now wrapped inside the run context
  > rather than passed bare. A base/derived inheritance design (one polymorphic
  > cancellation type) was considered and **rejected**: it would lose the cheap
  > copyable token and add a virtual call on a path checked between every stage.
  > This is idiom (B) from §1/§16 L19 — read/write capability split, not
  > immutable-value+builder.
  >
  > **[DECIDED 2026-07-18, built — Part C-3]:** `vc_pipe_context` receives the
  > run context **now** — the runner threads it in (held BY VALUE, defaulted so
  > every existing call site is unaffected) and exposes `run_context()`, so a
  > stage's `process()` MAY read `run_context().cancelled()` as an in-process
  > checkpoint. No stage does yet (the blur `process()` stays a rep shell), but the
  > seam is in place without a future interface change.
- **Concurrency [LATER]** — pure stages enable parallel execution; the executor/
  threading/tiling is built later without touching stage semantics (Halide's algorithm/
  schedule split validates this, §15.5).
- **Error model** — `vc_exception` for **fatal** (OOM, corrupt data, contract
  violation) ⇒ abort; the pipeline **wraps** with stage context (kind/name/slot) +
  a `vc_error_code` category. **Valid partial outcomes** (a bracket won't align ⇒ merge
  the rest) are **robust stage domain logic, not errors** — the design scales without a
  partial-failure framework because robustness is per-stage. Per-stage "bypass-on-error
  ⇒ pass-through" policy is [LATER] (editor-era; darktable disables a failed module).
  **A cancelled/failed `run()` is not a partial-failure edge case for the edit
  state** — edits are never mutated by rendering, so there is nothing to roll back;
  see the render-failure-consistency invariant (B15) and the disposable-pipeline /
  resource-cache note (B13), both at §1.
- **Resolution awareness / preview [LATER; design NOW]** — the render *request* carries
  `{target resolution / pyramid level, ROI}`; it is **not** source metadata (the source
  image's `vc_image_info` describes *its* geometry; a preview is a different image).
  Stages must be resolution-aware, and **spatial params must scale with level** (a 5 px
  blur at full-res ≈ 1.25 px at 1/4 preview) so preview matches export. Scale-awareness
  is the crux of preview↔export parity (§17.6).

## 9. Reproducibility & portability [design NOW]

"Transfer image + edit ⇒ same net render" requires three things met deliberately:

1. **All inputs travel.** Transfer bundle = **source + JSON doc + non-reproducible
   derived** (AI masks embedded/bundled — never left in the local cache). Reproducible
   derived regenerates on the target. Concretely (§4.4, B14): the bundle is the JSON
   **sidecar** (edit doc + metadata overlay + derived-data ids) plus the
   **companion blob store** those ids reference — never the evictable cache, which
   is reproducible by definition and simply rebuilds on the target.
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
`i_pipe::params_hash()` (per-stage hand-written cache-key seam — see §18's
superseding note); `build_pipeline` (hand-written assembly);
construction `kind()` registry; `edit_session` aggregate (thin); `vc_render_context`
hosting cancellation on `run()` (§8, B8/B9); reserved version fields
(doc/module/engine); fatal-error model with stage-context wrapping;
reference-frame discipline for stored coordinates.

**[LATER]** `vc_cached_edits_table` / `vc_persistent_edits_table` real backends (§5, B6);
taps/injections `run()` extension;
metadata subsystem; `kv_store` + adapters; buffer pooling
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
4. **[DECIDED, B14]** Non-reproducible-derived storage location: a **companion blob
   store** bundled beside the sidecar (bundled sibling) — explicitly NOT base64'd
   inline for large derived data (§4.4, §9).
5. **[OPEN]** Metadata master persistence location & catalog scope (editor-era).
6. **[OPEN]** Coordinate-space/geometry full model, and the reference-frame choice — §11.
7. **[DECIDED 2026-07-18, built]** Aggregate name: **`vc_edit_session`** (namespace `vc::edit`).
8. **[PARKED]** Derived-store eviction/GC/invalidation policy; exact cache-key hashing;
   tap/injection API shape; preview↔export parity details;
   `vc_image_spec` field set (`pipe_design.md §11 #4`). Related: neither
   `vc_cached_edits_table`'s key nor `vc_persistent_edits_table`'s stored payload currently folds
   in `render_engine_version` (`vc_engine_version.h`) — so a cache hit or a persisted
   artifact from a superseded engine version could currently look valid when it may not
   be. The real hashing/serialization mechanism (still unbuilt) needs to account for this.
   Related: `i_pipe::params_hash()` is per-**stage**, not per-slot — a stage with two
   output slots invalidates both together on any params change, even if only one slot's
   value actually depends on the changed param; over-conservative, never incorrect.
   Related: `std::hash` is only stable within one process's lifetime (no cross-run
   guarantee), so a persisted cache key computed via `params_hash()` cannot be compared
   across separate runs of the program without a stable hash function — fine for the
   in-memory `vc_cached_edits_table`, a real constraint if `params_hash()`'s value is
   ever persisted.

## 14. Linkage & learning-build split

`pipe_design.md` owns the **execution substrate**; this doc owns the **editing model**.
They meet at `build_pipeline` (edit state → `vc_pipeline`) and at three amendments this
doc requests to the `pipe_design.md` `run()` contract, to be reconciled there when built:
**(a)** a `vc_render_context` (control-only host wrapping the cancellation token now,
a progress seam [LATER] — §8), **(b)** `taps` + `injections` (read/write any slot),
**(c)** a resolution/ROI render request.

**Learning-build split:** Claude scaffolds interfaces and framework plumbing
(`edit_session`, `kv_store`/adapters, `metadata` interface, registry); the user
writes the rep logic (settings→stage derivations in `build_pipeline`, stage
`params`/`process` bodies, validation). Nothing generic remains to scaffold on
the serialization side — a schema/descriptor framework was tried there and
removed (§18's superseding note); what's left is per-stage hand-written code
(`params_hash()`, and the hand-written JSON in `vc_edit_document_io.h`/`.cpp`),
which is the user's rep, not scaffolded plumbing.

### 14.1 Naming & aliasing convention [design NOW; B16]
Two, and only two, spellings: **classes/structs/enums/concepts carry a `vc_`
prefix**; **type aliases are lowercase, no prefix** (already the working
convention — the Phase 2 alias sweep applied it). The rule for *what gets a
named type at all*: anything that **means something in the system** gets one —
reuse existing vocabulary where it already fits (`vc::utils::message`, `path`,
`slot_name`, …) or add a new alias at the seam where the meaning is introduced;
only genuinely **incidental** primitives (a loop index, a transient bool) stay
raw. **Aliases stay weak `using` aliases, never strong wrapper types** — a strong
type (a distinct class wrapping, say, an `int` id) was considered and rejected as
more machinery than the goal needs; the aliases exist for readability and
refactorability, not for compile-time type-safety, and `using` gives exactly that
without the ceremony.

### 14.2 Test doubles vs runtime backends [design NOW; B17]
**Test doubles never ship in library code.** A pure test spy — e.g. a
`recording_store` that exists only to observe calls in a test — belongs in a
dedicated test-support area, not in the library alongside real backends. This is
distinct from an **in-memory backend that is a legitimate runtime state** (an
ephemeral, never-saved session genuinely wants an in-memory store as *the*
backend, not as a mock of one) — that kind of in-memory implementation may stay in
the library. The test/library line is drawn by *purpose* (observing test
behavior vs serving a real runtime need), not by "is it in-memory."

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
  output stays readable. All five agents independently recommended this for us. A
  mechanism built on this principle (§18) was tried and then removed (§18's
  superseding note) after its only real consumer turned out to be cache-keying,
  which a per-stage hand-written hash serves just as well; named-field JSON
  (hand-written, §4.4) still collapses most of darktable's `legacy_params`
  cascade into "unknown field ⇒ default" on its own, without a generic
  descriptor layer.

### 15.2 Params & serialization, per engine
- **RawTherapee** — `ProcParams` = one master struct of ~30 per-*tool* sub-structs
  (user-facing); defaults in constructors + a global `setDefaults()`; no schema (plain
  fields). `.pp3` via Glib KeyFile; `ppVersion` integer (reported `353`, 2025-07-22)
  distinct from app version; back-compat via inline `if (ppVersion < N)` migrations
  scattered through `load()`. `ParamsEdited` tracks "was this field set" (powers partial
  presets/paste/batch). `ImProcCoordinator` reads across `ProcParams` to drive processing
  stages that are **not** 1:1 with tools. **We adopt** the user-facing per-tool document
  and **fix** the whole-`ProcParams` coupling (narrow slices); the hand-written
  duplication itself we now **accept** as a hand-written cost rather than solve
  generically (a schema mechanism aimed at solving it was tried and removed —
  §18's superseding note); **we defer** `ParamsEdited` (§17.4).
- **darktable** — `dt_iop_params_t` per module; `DT_MODULE_INTROSPECTION(version, type)`
  + `$MIN/$MAX/$DEFAULT/$DESCRIPTION` field comments; a build-time Perl pass
  (`tools/introspection/parser.pl`) generates an introspection table
  (`dt_introspection_field_t`, offset-based). `legacy_params()` migrates old param blobs
  version-to-version. History serialized to XMP (`darktable:history`) as gzip+hex blob.
  **We adopt** version-keyed migration + the (operation, instance) stable key;
  **we avoid** hex blobs — named JSON, hand-written, instead. (A schema-descriptor
  mechanism — this doc's rough analogue of darktable's introspection-as-metadata
  and its type-safe-descriptor alternative to C-preprocessor codegen — was tried,
  as `vc_param_field`, and removed; see §18's superseding note.)
- **GEGL** — every op is a GObject class; every param a **`GParamSpec`** (name, GType,
  default, min/max, nick/blurb) queryable via `gegl_operation_list_properties` — one
  reflection layer drives serialize + UI + CLI, *no per-op code*. Authoring via
  `gegl-op.h` "chant" X-macros; `ui_meta` string k/v for extensibility. Serializes to
  readable XML and a terse one-line "chain". **No versioning** (a named gap they told us
  to close). **We add** explicit `doc_version` + migration from day one. (A
  reflection-first descriptor layer — this doc's rough analogue of GEGL's
  `GParamSpec` reflection, rebuilt as plain C++ `PropDesc` without GObject — was
  tried and removed; see §18's superseding note.)
- **vkdt** — module *class* = a directory of declarative files: `params`
  (`name:type:count:default`), `params.ui` (widget + ranges, **separate** from data),
  `connectors` (typed named ports). Graph is a line-oriented token `.cfg`
  (`module:` / `connect:` / `param:`). Order is **derived** by topological sort, not
  stored. Module→node expansion (authoring graph vs execution graph). **We adopt** the
  authoring-vs-execution-graph idea. (A data-schema-vs-UI split — this doc's rough
  analogue of vkdt's `params`/`params.ui` separation — was tried and removed; see
  §18's superseding note.) **vkdt's own self-critique, heeded while the data schema
  existed:** put min/max in the *data* schema too (not UI-only), else headless
  validation can't clamp — moot now that there is no data schema to put it in
  (§18's superseding note: no range-validation code exists anywhere in this
  codebase); if range checking returns, it will be hand-written directly in a
  stage's `validate_inputs()`, not sourced from a shared descriptor.

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
| L5 | serialization: hand-written vs schema-driven | **hand-written, durably** | schema was tried and removed (§18's superseding note) — its only real consumer (cache-keying) is served just as well by a per-stage hand-written hash |
| L6 | format: JSON vs XMP vs INI | **JSON (nlohmann)** | interop matters only for metadata; XMP RDF + GPL cost; edits are app-specific |
| L7 | buffers: in-place vs new vs pooled | **pipeline-owned, pure stages, pooling later** | purity enables it; memory-budget headroom; in-place safety is *global* |
| L8 | cache key: params-only vs +input-hash | **params slice + input hash (chained)** | correctness — params-only returns stale downstream ("metaphysical dependency") |
| L9 | Kind A/C: unified vs separate stores | **separate stores, unified access (`edit_session`)** | different durability/format/interop; false-abstraction test |
| L10 | interface: one store vs role-specific | **`kv_store` (now Kind-C-only); blob adapters** | substitutability test; the byte-KV was the shared seam until Kind A's typed adapter was removed (§7, §18) |
| L11 | assembly: hardcoded vs registry vs data | **construction registry now; code assembly; data-graph later** | wiring is always explicit; dynamic structure resists pure data |
| L12 | determinism: automatic vs engineered | **engineered** | IEEE ops deterministic, but source ≠ computation (FMA/reassoc/transcendentals) |
| L13 | immutability: convention vs type-guarantee | **type-guarantee (`vc_image_writer`/`seal`)** | greenfield C++ affords it; stronger than all four studied engines |
| L14 | metadata: in edit-doc vs subsystem | **separate subsystem behind interface** | standards/interop matter for metadata; avoid bottling up |
| L15 | "was-set" tracking | **defer** | full doc handles single-image; sparsity/presets are later |
| L16 | non-reproducible derived: cache vs persist | **persist with the edit** | can't regenerate identically (model drift) ⇒ behaves like intent |
| L17 | error model | **exceptions for fatal; degradation as stage logic** | robustness is per-stage; no partial-failure framework needed |
| L18 | display transform: edit vs view | **view — terminal stage, output-target config** | it is *how you view*, not *what you edited* |
| L19 | immutability idiom: one universal rule vs applied per-driver | **two idioms, applied by driver (B12)** | (A) immutable-value+builder where *stable shared identity* drives (images, captured metadata, cached derived); (B) read/write split where *least-privilege/CQS* drives (storage interfaces §7, cancellation §8); never smear (A) onto genuinely mutable edits |

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
instances become a `vector<settings>` field, hand-written like any other JSON array of
custom objects (§4.4) — no generic container serializer is needed for this.
**Verdict:** scales; document representation deferred to P4.

### 17.3 Custom config types (matrix, curve, structured mask) [scales]
Cost is **per distinct type, written once, reused everywhere** (O(types), not O(uses)):
a per-type `to_json`/`from_json` pair (nlohmann's ADL-based macro, already the
mechanism behind `vc_edit_document_io.h`) handles a nested struct or a
`vector<CustomType>` automatically once the type itself has its pair defined —
verified empirically to nest and to serialize containers of custom objects
correctly. This is hand-written, not schema-generated (a generic schema mechanism
was tried and removed — §18's superseding note), but the *cost* still
concentrates per-type, not per-use: a `mat3` gets one `to_json`/`from_json` pair,
reused everywhere a `mat3` appears. **Does not** hold opaque bulk (embeddings,
raster pixels) — those are Kind C blobs, correctly not in the document.

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
(upstream, unchanged) are a cache hit** — no re-align. Alignment/features are Kind
C-repro (§2), so on device this needs **`vc_cached_edits_table`** (§5, B6) —
content-hash-keyed, not the (non-reproducible, id-keyed) `vc_persistent_edits_table` —
hence "core, not optional." If the burst is closed and reopened, a disk-backed
cache avoids re-aligning (reproducible-but-precious).

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

## 18. The schema / serialization mechanism [BUILT 2026-07-17, SUPERSEDED 2026-07-25 — see note below]

Captures the §15.1 escape hatch as a concrete C++ design. Declare each field **once**;
generate save/load/describe.

> **Status update (2026-07-17): the framework is now BUILT** (was [LATER]; pulled
> forward at the user's request to power toy/learning stages like blur, even though
> those params are not user-facing). It ships as **`include/vc/vc_param_schema.h`**
> (namespace `vc::params`): the `field<C,T>` descriptor + deduction guides, the
> `param_value` concept (exactly `{double,int,bool}` — the doc_writer/doc_reader
> overload set), and the generic `save`/`load` (sink-templated `std::apply`+fold).
> A worked reference (`vc_blur_stage` + `blur_params`/`blur_schema`) and green
> round-trip tests (`tests/test_vc_param_schema.cpp`) accompany it. **Still gated:**
> wiring the schema through the *persistent* doc store awaits the `doc_writer`/
> `doc_reader` bodies (still `TODO(you)`); `save`/`load` already work against any
> conforming sink today. The hand-written per-field baseline remains valid for
> user-facing settings — adopt the schema where it helps.

> **[SUPERSEDED 2026-07-25].** The entire mechanism below — `vc_param_field`,
> the `vc_param_struct_req`/`vc_param_writer_req`/`vc_param_reader_req`
> concepts, `save()`/`load()`, and their host file
> `include/vc/vc_param_schema.h` — is **removed**, after tracing its only
> real downstream consumer (cache-keying) through in full. Its own stated
> justification above ("more than serialization — ranges, UI hints,
> cache-key material") rested on three legs; two never materialized (no
> range-validation code exists anywhere in this codebase, and no UI exists
> or is concretely planned), and the third — cache-key hashing — stopped
> needing the schema's genericity once `i_pipe::params_hash()` became a
> per-stage virtual method: that already gives the "uniform access across
> concrete types" the schema was meant to provide. Hash-keying now uses a
> direct hand-written combine per stage (e.g. `vc_blur_stage::params_hash()`),
> mirroring `stage_port::hash()` — an established pattern in this codebase,
> not a new one. `vc_edit_settings_writer`/`vc_edit_settings_reader`
> (`doc_writer`/`doc_reader` above) are removed too, not "still gated" —
> their entire reason to exist was serving as this mechanism's write target.
> Everything below this note is preserved as a historical record of what
> was built and why, not a description of current code. Not a one-way door:
> cheap to reintroduce from git history if a second real params struct later
> makes hand-written duplication genuinely painful.

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

> **Naming (recorded here; applies to code when built): `vc_field →
> vc_param_field`.** The sketch above writes unprefixed `field` as illustrative
> pseudocode (§14.1); the tree already declares the prefixed type this rename
> targets — `struct vc_field` at `include/vc/vc_param_schema.h:47` — so this
> rename applies to code (`vc_field` → `vc_param_field`) when the build pass
> reaches it.

## 19. Glossary

- **Non-destructive editing** — original preserved; result computed from
  original + a description of operations.
- **edit document** — the saveable, structured record of Kind-A edit settings.
- **`edit_session`** — the per-image aggregate composing source + edit doc + metadata +
  derived-store handle (§3). **[SUPERSEDED 2026-07-18]** — the session now owns
  two derived stores directly (`vc_persistent_edits_table` + `vc_cached_edits_table`,
  behind `i_edit_table`) plus a mutable edited-metadata overlay, not one
  derived-store handle; see §3.
- **sidecar** — a companion file stored next to the image, holding the edit document.
- **serialize/deserialize** — object ↔ savable bytes; **JSON** is the text encoding.
- **`kv_store` / `doc_writer` / `derived_store`** — shared byte store + typed adapter
  (Kind A) + content-hash adapter (Kind C) (§7). **[SUPERSEDED 2026-07-18]** — the
  single `derived_store` shape is superseded by the two-store `i_edit_table`
  design (`vc_cached_edits_table` + `vc_persistent_edits_table`); see the
  `i_edit_table` / `vc_cached_edits_table` / `vc_persistent_edits_table` entry below
  and §5/§7. **[SUPERSEDED 2026-07-25]** — `doc_writer`/`doc_reader` (Kind A's
  typed adapter) are themselves removed along with the schema mechanism that
  was their sole reason to exist (§18's superseding note); `kv_store` (shipped
  as `i_table`) now serves Kind C only.
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
- **naming convention (B16, §14.1)** — classes/structs/enums/concepts: `vc_` prefix;
  type aliases: lowercase, no prefix, `using` only (never a strong wrapper type).
- **`vc_param_field`** — **[SUPERSEDED 2026-07-25, see §18]** Q2 rename of the
  schema field descriptor (`vc_field → vc_param_field`); the type itself, and
  its host file `include/vc/vc_param_schema.h`, are removed along with the
  rest of the schema mechanism (§18's superseding note). Historical entry,
  kept for the naming-decision record.
- **`i_table` family** — Q2 rename of the byte-store family: `vc_kv_store →
  i_table`, `vc_memory_kv_store → vc_memory_table`, `kv_bytes →
  data_bytes` (alias, no prefix) (§7).
- **`get`/`set`** — Q2-standardized store access verbs, used uniformly across the
  storage interfaces (§5.1, §7); the total-get/miss-exposing distinction lives in
  return type and doc comment, not in the verb name.
- **`i_edit_table` / `vc_cached_edits_table` / `vc_persistent_edits_table`** — one
  interface, two concrete Kind-C stores, distinguished by eviction policy *and*
  serialization lifecycle (§5, §7).
- **`vc_render_context`** — the control-only host `run()` takes in place of a bare
  cancellation token: holds cancellation now, a progress seam [LATER]; never the
  cache (§8).
- **`render_image`** — composes `build_pipeline` + `vc_pipeline::run()` into one
  callable (§8); both a future preview path and `export_image` call it rather than
  re-deriving the composition.
- **`export_image` / `vc_export_config`** — renders via `render_image` and writes the
  result to a file (§8). Not to be confused with the *portability* sense of "export"
  (§4.4, §9) — an unrelated bundle format for transferring edit state, which shares
  the word by accident, not by design.
```