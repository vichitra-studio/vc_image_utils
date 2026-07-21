# Pipe & Pipeline Design — vc_image_utils

Design target for the image-processing **pipe** (a single processing stage) and the
**pipeline** that chains pipes. This document captures the *design we are building toward*,
the reasoning behind each decision, what we build now versus later, and how we expect it to
evolve.

> **Status legend.** Every decision below is tagged:
> - **[DECIDED]** — settled; build to this.
> - **[NOW]** — in scope for the near-term scaffold (P1–P3 learning reps).
> - **[LATER]** — designed here, deliberately not built yet; built when the problem demands it.
> - **[OPEN]** — a genuine unresolved question; do not assume an answer.
> - **[ASSUMPTION]** — something taken as true that is not yet verified or committed.
>
> This is a living document. Update it when a decision changes — do not let code drift from it
> (same rule as `coding_guidelines.md`).
>
> **⇒ See [§12](#12-converged-decisions--phase-1-build-spec-2026-07-design-session) for the
> converged decisions from the 2026-07 design session — they *supersede the illustrative
> sketches* in §2–§8 where they differ (the `i_pipe` shape, `expects<T>()`, the 3-arg `run()`).
> The reasoning in §1–§11 still stands; §12 is the settled concrete surface and the Phase 1 build
> list.**

---

## 0. TL;DR

A **pipe** is one image-processing stage. A **pipeline** is a chain of pipes. We are designing
a *generic, runtime-composable* pipe interface — one that can eventually be wired from outside
the C++ source (a CLI, a config file, a camera app's UI) — because that runtime composition is
a real roadmap item, not a hypothetical. The mechanism is:

- **Named slots** — each pipe has named input/output ports (not a fixed single image-in/image-out).
- **Type-erased packets** — a slot carries a box that can hold any payload (image, matrix, mask, feature list).
- **Two-layer contracts** — a *type* contract (is this a `vc_image` or a `matrix3`?) and a *data*
  contract (is this image planar / linear / float32?), the latter expressed as a propagating
  `vc_image_spec` and checked when the graph is assembled, before any pixels flow.

We build a **minimal linear version now** to get comfortable and surface friction while doing
P1–P3, and let the full mechanism fill in as real requirements (the CLI, the Block-2 HDR merge,
the camera app) arrive.

---

## 1. Scope — what a "pipe" is and is not

**In scope: a pipe is a single unit of image processing** that transforms data. Examples across
the roadmap: grayscale, blur, convolution, warp, demosaic, white-balance, tone-map, HDR merge,
corner detection.

**Explicitly out of scope: file load and save are *not* pipes.** [DECIDED] They are the
*harness* around the pipeline — the entry and exit through which data gets into and out of the
pipeline, not stages within it. A pipe operates on in-memory data; load produces the first
in-memory buffer and save consumes the last one. Keeping them out keeps the pipe interface
about *processing*, not I/O. (This mirrors darktable, where the pixelpipe is fed decoded data;
decode/export sit outside it.)

**Not in scope for this document: pipeline *orchestration* internals** (scheduling, threading,
tiling, caching, demand-driven pull). We design the *pipe interface* and a *minimal linear
runner*. The sophisticated pipeline engine is [LATER].

---

## 2. The problem, and why the simple shape is not enough

### 2.1 The obvious shape, and where it breaks

The simplest possible pipe interface is *one image in, one image out*:

```cpp
// illustrative
class i_pipe {
  public:
    virtual ~i_pipe() = default;
    virtual vc_image apply(const vc_image& input) const = 0;
};
```

This is genuinely fine for the majority of stages (blur, grayscale, sharpen, tone-map) and
covers ~all of P1–P3. It is worth stating plainly: **for a fixed, code-written chain of
1-image→1-image stages, this interface is sufficient and nothing more is needed.**

It breaks for stages whose *shape* differs:

| Stage | Takes | Produces | Fits `image→image`? |
|---|---|---|---|
| blur, grayscale | 1 image | 1 image | ✅ |
| HDR merge | *N* images | 1 image | ❌ many-in |
| mask / layer composite | 2 images (+ mask) | 1 image | ❌ many-in |
| warp | 1 image **+ a matrix** | 1 image | ❌ non-image input |
| align (find transform) | 2 images | **a 3×3 matrix** | ❌ non-image output |
| corner detection (Harris) | 1 image | **a list of points** | ❌ non-image output |

So there are two groups: the large group that fits `image→image`, and a set of stages that
genuinely do not — varying in **arity** (many-in / many-out) and in **payload type** (matrix,
mask, point-list, not just image).

### 2.2 The core tension

A *uniform plug* (every pipe looks identical from outside, so you can chain them blindly in a
loop) pulls **against** *heterogeneous inputs/outputs* (which give every pipe a different plug).
The more shapes you allow, the less a single blind loop works. Every node-graph image system
wrestles with this; it is not a flaw in any one design.

### 2.3 Important correction to an intuition: heterogeneity alone does **not** force a generic bag

[DECIDED — reasoning] It is tempting to conclude "the stages are heterogeneous, therefore I need
one generic container." **That inference is false**, and the doc records it so we don't lean on
it. Static typing handles heterogeneous *signatures* natively:

```cpp
// illustrative — all compile-checked, all composable in code, no container needed
matrix3            find_transform(const vc_image& reference, const vc_image& moving);
vc_image           warp(const vc_image& img, const matrix3& m);
vc_image           merge(const std::vector<vc_image>& frames);        // N→1
std::vector<point> harris(const vc_image& img);                       // image→data
```

Every one of these is heterogeneous, and every one is fully type-safe and composable *as plain
functions*, with zero framework. **What actually defeats the static type system is not
heterogeneity — it is holding *unknown* stages in a runtime collection, wired together from
input that arrives while the program runs.** That is the real driver, and it is the subject of
the next section.

---

## 3. The key decision: a *generic runtime* pipe, and why

### 3.1 The real discriminator: runtime composition **and** open/heterogeneous payloads — both, not either

[DECIDED — reasoning] It is tempting to reduce this to a single factor. It is not, and the doc
records the honest version so the decision does not rest on a claim its own evidence refutes.

*Naive version (wrong):* "runtime-composed graphs need type-erased payloads." **darktable
disproves this directly.** darktable is runtime-composed — users enable, disable, and reorder
modules in the GUI and the pixelpipe rebuilds — yet it flows a **single fixed buffer type**
through every module, with *no* type-erased heterogeneous packets. Modules declare which color
space they operate in (a *data* contract), and the pipe inserts conversions. So runtime
composition alone does **not** force type erasure — a proven system in *our exact domain* (raw
photo pipeline) does the opposite.

*Also wrong (per §2.3):* "heterogeneous stages need a generic bag." Static typing handles
heterogeneous signatures natively.

*Honest version:* type erasure is forced only when **both** hold — (a) the graph is composed at
runtime (the compiler can't see the wiring), **and** (b) the payloads flowing between stages are
genuinely heterogeneous and **open-ended**, i.e. you are unwilling to commit to a single fixed
buffer type or a closed set of payload types. A photo pipeline's *main chain* is mostly
homogeneous (images flow stage-to-stage); the heterogeneous cases (align→matrix→warp,
Harris→points) are largely side-computations or terminal outputs. That is precisely why
darktable's single-buffer design suffices for it.

> When the graph is code, the compiler already **is** the contract and the wiring check — for
> free, at compile time. When the graph is data (a config the compiler never read), that safety
> leaves the compiler's reach, and you re-check it yourself. But that only *pushes you to erasure*
> if condition (b) also holds; otherwise darktable's single-type-flow answer is available even
> for a runtime graph.

### 3.2 Why we still choose the generic type-erased design

[DECIDED] Both conditions from §3.1 hold for us, and we additionally *want* the open-payload
model for reasons a single-buffer design would not serve:

- **(a) Runtime composition — committed roadmap, not speculation:**
  - The **P4 CLI** is specified in the curriculum: every stage a composable subcommand, JSON
    output, an introspection command listing stages and parameters — runtime composition by
    definition (a user chains stages at the terminal).
  - A **camera app** wrapping this library (curriculum P16; JNI designed from day one) assembles
    a *different* stage graph per capture mode (HDR / Night / Portrait), with runtime-varying
    bracket counts and user-toggled options (deghosting, merge-algorithm choice).
  - The **HDR merge** path (Block 2) has a bracket count that is a runtime property.
- **(b) Open, heterogeneous, first-class payloads — a deliberate goal:** we want non-image data
  (alignment matrices, feature/point lists, masks, per-stage parameters) to travel between stages
  as *first-class slot payloads*, not as side-channels bolted onto one buffer type the way
  darktable carries masks. And we want the payload set **open** — future stages or third parties
  (the SDK/EXTEND aspiration, §9) can introduce new payload types without editing a central
  buffer or variant.

**The considered alternative, explicitly:** darktable's single-fixed-buffer + data-contract
design is simpler, proven, and would cover the homogeneous main chain of a photo pipeline. We
consciously go further because of **(b)** — first-class heterogeneous payloads and open
extensibility — accepting the cost it buys (loss of compile-time inter-stage safety, §6). **This
is the pivotal trade. If (b) ever proves not worth its cost, darktable's single-type model is the
fallback — and it is a good one.**

### 3.3 What we consciously give up, and what we get back

[DECIDED — reasoning] Going generic means **voluntarily discarding C++'s compile-time
inter-stage safety** in exchange for runtime composability *and* open payloads. We get most of it
back by checking
the graph *at assembly time* (before any pixels flow), not deep inside a stage at execution
time. See the safety model in §6, including an **honest correction** to how much of this is
"compile-time."

The patterns and their costs, for the record:

| Pattern | What flows | Multi-input | Where safety lives | Exemplar |
|---|---|---|---|---|
| Fixed single buffer | one image (single type) | ❌ (masks as side-channel) | trivial on type (only one type flows) + runtime data-contract (colorspace) | darktable — *runtime-composed* |
| Typed values (templates) | statically-typed value | numbered ports | compile-time (rigid, template blow-up) | ITK, OpenCV G-API |
| Named typed ports + negotiation | buffer per named pad | ✅ named/aux pads | runtime negotiation | GEGL, GStreamer |
| **Type-erased packets on named slots** | **any payload, named slots** | ✅ named slots | **checked at graph assembly** | **MediaPipe (our model)** |

---

## 4. The design

### 4.1 Slots — named ports

[DECIDED] A pipe has **named slots**: some input, some output. A slot is *a name plus a place
for one item*. This generalizes the single unnamed input/output of the simple interface to any
number of named inputs and outputs.

**Why names are mandatory (not derivable from type):** when two inputs share a type — HDR merge
takes several `vc_image`s — the type cannot tell them apart. The slot's identity must be
separate from its payload type. The name carries the **role**.

Two multiplicity cases, decided:

- **Distinct roles, fixed count** → **one named slot per role.** [DECIDED]
  e.g. align: `reference` and `moving` are both images but not interchangeable (swapping them
  computes the inverse transform), so they are two slots.
- **Interchangeable set, runtime count** → **one list-valued slot.** [DECIDED]
  e.g. merge of *N* brackets: a single slot (`frames`) whose payload is a `std::vector<vc_image>`.
  **The runtime count lives inside the payload (the vector), never as a variable number of
  slots.** This is the resolution to "how do enum-named slots handle N same-typed inputs" — they
  don't need to; the multiplicity is in the value, not the slot set.

> **[OPEN] Fan-in wiring for list-valued slots.** If *N* separate upstream stages each feed one
> frame into merge's single `frames` slot, the wiring layer must gather *N* connections into
> one vector. GStreamer ("request pads") and GEGL do this. Alternatives: (a) a slot that accepts
> multiple connections and auto-gathers; (b) an explicit `collect` stage that stacks *N* inputs
> into one vector. **Not decided.** Either keeps merge's contract as one list-valued slot.

### 4.2 Slot identity: generic at the interface, enum sugar inside a stage

[DECIDED — reasoning] This is a subtlety that is a *gap* if left implicit, so it is pinned here.

The pipeline framework holds pipes generically (as `i_pipe*`) and must connect slots without
knowing any concrete stage's enum. Therefore **at the interface boundary, slots are identified
generically** — by a stable name (string) and/or an index. A concrete stage may define its own
scoped enum purely as *named indices* for readability:

```cpp
// illustrative — inside the warp stage's own code
enum class warp_slot : int { image, matrix, result };   // just named ints (== slot indices)
```

The enum's integer value **is** the generic slot index the framework sees.

> **Input and output slots are separate index namespaces.** [DECIDED] A slot is keyed by
> *(direction, index/name)*, so an input at index 0 and an output at index 0 do **not** collide.
> A stage may use one enum spanning both directions (as `warp_slot` above — the context resolves
> each value by its declared direction) or two enums (`warp_in_slot` / `warp_out_slot`); either
> is fine, but the framework never treats a bare integer as a global slot id.

So:

- The **framework** speaks *(direction, string/index)* (needed for config- and CLI-driven wiring
  — a config file can only name a slot as text).
- The **stage author** speaks their enum (compile-checked names, no string typos in stage code).
- A **string ↔ index/enum map** bridges them. This map is a lean, mechanical, fully unit-testable
  layer — its job is only to translate the user's text world into the stage's typed world.

This also resolves the "virtual functions can't be templates" constraint (see
`vc_log_info_builder.h` for the same rule): `i_pipe`'s methods are non-template virtuals operating
on generic slot identity; the enum + a per-stage type trait (§4.3) are a *non-virtual,
per-stage* ergonomic layer on top.

### 4.3 Packet — the type-erased payload

[DECIDED] What sits in a slot is a **packet**: a box that can hold *any* payload type and knows
what it holds. In C++ the natural spelling is `std::any` (open set of payload types).

```cpp
// illustrative
class vc_packet {
  public:
    template <typename T> explicit vc_packet(T value);
    template <typename T> const T& get() const;   // throws if the held type != T
    const std::type_info& type() const noexcept;
  private:
    std::any value_;
};
```

**[OPEN] `std::any` vs a closed `std::variant`.** `std::any` supports an *open* set of payloads
(third-party stages can introduce new payload types — aligns with the "library-as-SDK" EXTEND
aspiration), at the cost of heap allocation + RTTI. A `std::variant<vc_image, matrix3, ...>` is
a *closed* set: cheaper and exhaustively switchable, but every new payload type edits the
variant. **Not decided.** Leaning `std::any` for openness, but this is revisited when the real
payload set is known.

**Compile-time slot access via an enum + type trait** — with an *honest* boundary:

```cpp
// illustrative — a per-stage trait binds each slot to its payload type
template <warp_slot S> struct warp_slot_type;
template <> struct warp_slot_type<warp_slot::image>  { using type = vc_image; };
template <> struct warp_slot_type<warp_slot::matrix> { using type = matrix3; };

// inside warp::process(), the context returns the statically-known type:
const matrix3& m = ctx.in<warp_slot::matrix>();   // no string, no type annotation, no slot typo
```

[DECIDED — with correction] What is genuinely **compile-time** here: the slot *name* (enum, so
no typo) and the *expected payload type* (the trait, so `ctx.in<...>()` needs no explicit type
and can't be annotated wrongly). What is **still runtime**: the actual unboxing of the packet
(`std::any_cast`), which can only be checked when the value exists. **Correction to how this was
described in design discussion:** the trait does *not* make the unbox a no-op compile-time
operation — it removes the annotation/typo burden, but the payload check itself is a runtime
cast. That cast is *guaranteed to succeed* in a graph that passed its assembly-time contract
check (§6), so in a well-formed pipeline it never throws — but it is not a compile-time
guarantee on its own.

> **[OPEN] The `ctx.in<S>()` sugar is not free to wire.** `vc_pipe_context` is *generic* (it
> serves all stages), but `slot_type<S>` is *per-stage*. Making `ctx.in<warp_slot::matrix>()`
> resolve the per-stage trait through a generic context is a real implementation task — likely a
> small stage-side helper templated on the stage's own enum/trait that wraps a generic
> `ctx.get(index) -> const std::any&`. The sketch shows the *intended ergonomics*, not a free or
> obviously-correct mechanism. Treat the exact spelling as unresolved.

### 4.4 The pipe interface

[DECIDED — shape] Two responsibilities: **declare** (the contract) and **do** (the work).

```cpp
// illustrative
class i_pipe {
  public:
    virtual ~i_pipe() = default;

    // Declare slots + their type/data contracts. Called at graph-assembly time,
    // before any data flows. Does no work and moves no data.
    virtual void declare(vc_contract& c) const = 0;

    // Do the work: read named input packets, produce named output packets.
    virtual void process(vc_pipe_context& ctx) const = 0;
};
```

`vc_pipe_context` exposes the input packets and collects output packets by slot identity
(generic index/string at the boundary; enum+trait sugar inside the stage per §4.3).

> **[ASSUMPTION]** Pipes are *stateless* with respect to a single run: all per-stage
> configuration (blur radius, Harris `k`, merge algorithm choice) is captured at pipe
> construction and held as `private` members, exactly like the strategy/functor pattern. The
> `process()` signature therefore needs no parameter block. This matches every P1–P3 stage.
> If a stage ever needs cross-invocation state (e.g. streaming), this assumption is revisited.

### 4.5 Contracts — two layers

[DECIDED] A contract is a stage *declaring, up front,* what each slot requires/produces, before
data flows. There are **two independent layers**, and both are needed:

1. **Type contract** — *"is this a `vc_image`, a `matrix3`, a `std::vector<point>`?"* Structural.
   Catches "you wired a matrix into an image slot."
2. **Data contract** — *"is this `vc_image` planar / linear-Rec.2020 / float32?"* Semantic.
   Catches "you wired a display-referred RGB image into a demosaic stage that needs raw Bayer" —
   a mistake that *passes* the type check (both are `vc_image`) but is still wrong.

```cpp
// illustrative — demosaic declares both layers on its input
c.input("raw")
    .expects<vc_image>()                              // type layer
    .matching(vc_image_spec{ .dtype = pixel_dtype::u16,
                             .kind  = channel_kind::bayer_rggb,
                             .space = color_space::camera_native });   // data layer
```

The data layer requires that a `vc_image` actually *carry* its semantic properties (§5). Until
it does (that growth is [LATER]), only the type layer is live; the data layer is designed but
dormant.

---

## 5. `vc_image`, `vc_pixel_buffer`, and the data contract

### 5.1 What exists today, and what it already solves

[VERIFIED against source, 2026-07] `vc_pixel_buffer` is a **runtime-typed** store:
`std::variant<std::vector<buf_f32>, std::vector<buf_u8>, std::vector<buf_u16>>`, with
`dtype()`, `size()`, and typed `as<T>()` access. `vc_image` holds `width_ / height_ /
channels_` and a `pixel_buffer_ptr` (a `shared_ptr<vc_pixel_buffer>`).

The choice of a **runtime variant** rather than a templated `vc_pixel_buffer<T>` is deliberate
and correct (see `vc_pixel_buffer.h` and `coding_guidelines.md` §3.1): templating the element
type would force a type parameter onto `vc_image` and everything downstream. This means a
uint16 Bayer frame and a float32 RGB image are *already* the same storage type with a different
`dtype()` — the **element-type axis** of the RGB/Bayer duality is solved, via the right
mechanism.

### 5.2 What "hold both RGB and Bayer" actually requires (five axes)

[DECIDED — reasoning] The curriculum's Tier-D "one buffer, two worlds" requirement (ARCH-3:
planar-float32 RGB *and* single-channel Bayer uint16/float16) is **more than the element-type
axis.** The five axes and where each lives today:

| # | Axis | RGB pipeline | Bayer frame | Status today |
|---|---|---|---|---|
| 1 | Element dtype | float32 | uint16 (→ f16 merged) | ✅ solved (`vc_pixel_buffer` variant) |
| 2 | Layout | **planar** (RRR…GGG…BBB) | single plane | ❌ unencoded — buffer is a flat vector; planar-vs-interleaved is convention only |
| 3 | Channel meaning | 3ch = R,G,B | 1ch = *mosaic* (role depends on x,y) | ❌ `channel_count` says "1" or "3", not what they *mean* |
| 4 | CFA + levels | n/a | RGGB pattern, black/white level, WB multipliers | ❌ no home in `vc_image` |
| 5 | Color space / range | Linear Rec.2020, **unbounded** | camera-native | ❌ untagged (see note) |

Two concrete, verified tells:

- **Layout is implicit.** `vc_image` stores `w*h*channels` flat elements; whether that is planar
  (curriculum-mandated) or interleaved (what stb produces) is written down nowhere. Two stages
  disagreeing on this silently corrupt pixels.
- **A display-referred fossil.** `vc_pixel_buffer.h` comments the `f32` dtype as
  `// normalised [0.0, 1.0]`. Scene-referred Linear Rec.2020 is explicitly *unbounded* (values
  above 1.0 are the point of HDR). That comment marks an assumption the merge pipeline will
  break; it is a [LATER] tension, recorded now.
- **f16 not present.** The buffer holds f32/u8/u16, not float16 (needed for merged Bayer output).
  Additive when reached.

Axes 2–5 are **not the buffer's job** — they belong to `vc_image` (or a small metadata layer
above it). The buffer solved the axis it owns.

### 5.3 `vc_image_spec` — the data contract descriptor

[DECIDED — shape; LATER — build] Model the semantic axes as a small descriptor that `vc_image`
carries and contracts match against:

```cpp
// illustrative — provisional field set
struct vc_image_spec {
    pixel_dtype  dtype;   // f32 / u8 / u16 / (f16 later)
    layout       layout;  // planar / interleaved
    color_space  space;   // linear_rec2020 / camera_native / srgb / …
    channel_kind kind;    // rgb / rgba / grey / bayer_rggb / scalar / …
};
```

Prefer this **declarative descriptor** over an opaque predicate lambda: a descriptor is
*introspectable* (printable for the CLI/agent introspection command), *comparable* (two specs
can be checked for compatibility), and *negotiable* (a converter can be auto-inserted between
incompatible-but-convertible specs — the darktable colorspace-insertion trick). A lambda can
only be run, not reasoned about.

### 5.4 Spec propagation — how the data contract is checked *before pixels flow*

[DECIDED — reasoning; LATER — build] Each stage declares its **output spec as a function of its
input spec**:

- `demosaic`: in `{bayer, u16, native}` → out `{rgb, planar, f32, linear}` (fixed).
- `blur`: out = in (*"I preserve whatever I am handed"*).
- `tone_map`: in `{…, linear}` → out `{…, display}`.

At graph assembly, the framework **propagates specs stage-to-stage and checks each adjacency**
(upstream declared-output vs downstream declared-input) *before a single pixel moves* — this is
GStreamer caps negotiation in miniature. Optionally, a cheap per-buffer assertion at execution
time serves as a runtime safety net.

This is why "contract on data, checked before running" is not a contradiction: the spec is a
*declaration that propagates*, not a measurement of actual pixels.

### 5.5 Why the data contract is runtime — and why that is *consistent*, not a compromise

[DECIDED — reasoning] Data contracts (color space, layout) are inherently **runtime /
assembly-time, never compile-time**, because those are properties of *values*, not *types*.
One *could* force them into the type system (`vc_image<bayer_tag, u16_tag, …>`, phantom types)
to get compile-time data contracts — but that is precisely the templated explosion that
`vc_pixel_buffer` **already rejected**. So the semantic axes follow the *same decision already
made and validated* for `dtype`: **a runtime property, not a compile-time type.** Runtime data
contracts are the consistent extension of the buffer's existing philosophy, not a concession.

### 5.6 Is adding `vc_image_spec` an "extension" or a "modification"?

[DECIDED — reasoning] Structurally, an **extension**: the *design* of `vc_image` and
`vc_pixel_buffer` does not change. The buffer is untouched. `vc_image` gains a member and an
accessor; a **defaulted** constructor parameter means every existing P1–P3 call site compiles
unchanged.

The one honest, *deferrable* cost is not in the type — it is that once the spec exists, existing
loaders and stages must begin to *populate and respect* it (the stb loader declares
"interleaved, sRGB, u8"; a blur declares "I preserve layout"). That is teaching existing code to
*speak* the new vocabulary, not redesigning `vc_image`. It is fully opt-in: P1–P3 can run with a
sensible default spec that nobody checks yet, and the cost is paid only when contracts are turned
on. So the precise claim is: **the type grows additively; the only real work is later, in the
stages, and it is opt-in.**

---

## 6. The safety model — where every check happens

[DECIDED — with an explicit correction to design-discussion shorthand]

| Layer | Checks | When | Mechanism |
|---|---|---|---|
| Slot name / local payload type (within a stage) | slot typo; wrong local type annotation | **compile time** | enum keys + per-stage type trait (§4.3) |
| Inter-stage **type** contract | `vc_image` vs `matrix3` mismatch across a connection | **graph-assembly time** (runtime) | declared **output type** vs declared **input `expects<T>`** — comparing *declarations*; no packet exists yet |
| Inter-stage **data** contract | planar-linear-f32 vs bayer-u16-native mismatch | **graph-assembly time** (runtime) | `vc_image_spec` propagation — again comparing *declarations* (§5.4) |
| Packet unbox (`any_cast`) + optional assertion | payload actually matches its slot | **execution time** | `std::any_cast` inside `ctx.in<>()`; guaranteed to pass in a graph that cleared assembly-time validation, so never throws in a well-formed pipeline |

> **Correction to what was said in discussion.** It was stated that the type contract is
> "compile-time in code / load-time from config." With **type-erased packets** (the design we
> chose), that is **too strong for the default path**: because packets are type-erased, even a
> graph wired in C++ via a runtime `connect()` is checked at **assembly time**, not compile time.
> There is *no* compile-time inter-stage check in the default design. Compile-time safety exists
> only *within* a stage (row 1).
>
> **[RESOLVED — not building it] Optional compile-time-checked code-composition path.** Because
> the typed `slot<T>` descriptors already expose each stage's payload types statically (§12.2), we
> *could* additionally offer a typed, code-only wiring API — e.g. a templated
> `connect<Producer, out_slot, Consumer, in_slot>()` that `static_assert`s the payload types match —
> giving compile-time inter-stage checks *for code-defined graphs only*, alongside the runtime
> `connect(name, name)` for config-defined graphs.
>
> **Decision (2026-07 session): we do not build this.** `vc_image_utils` is a **runtime-composition
> library** — its reason to exist is CLI/config/app-assembled graphs, where slot types are not known
> at compile time and the check *must* be runtime anyway. A `static_assert` path would harden only
> the code-wired minority case, while adding a permanent second wiring API to keep consistent (the
> "real cost" above). The runtime `validate()` already catches every mismatch on *both* paths. So
> the assert is cost without matching payoff, and the compile-time inter-stage check stays
> deliberately out of scope. (This removes the `static_assert` item from §12.4 Phase 2; the
> `add()`-handle ergonomic remains.)

---

## 7. The pipeline (minimal runner)

[NOW — minimal] A pipeline is a chain of pipes plus the wiring between their slots:

1. **Assemble** — add pipes, `connect(upstream, out_slot, downstream, in_slot)`.
2. **Validate** — at assembly/finalize, run every pipe's `declare()`, then check every
   connection's type contract (and, when live, data contract) — **fail here, before execution,
   with a clear message** (e.g. *"warp.image expects vc_image but is connected to
   find_transform.matrix (matrix3)"*).
3. **Run** — drive data through: each pipe's `process()` reads its input packets and writes its
   output packets.

**[NOW]** the runner is **linear** (a straight chain). A general DAG (branches, fan-in, fan-out)
is [LATER] — the interface does not preclude it, but the near-term runner does not implement it.

This satisfies the curriculum's P4 pipeline-interface acceptance bar directly: *"two stub
modules chain and run on a buffer end-to-end; adding a module is a localized change (no
pipeline-wide edit)."*

---

## 8. What we build now vs later

### 8.1 [NOW] — the near-term scaffold (during P1–P3)

Purpose: get comfortable with the mechanism and **surface friction early**, while the stages
themselves are simple. Concretely:

- `i_pipe` with `declare()` + `process()`.
- `vc_packet` (type-erased), `vc_pipe_context`, generic slot identity (index/string) + the
  enum-trait sugar for stage authors.
- `vc_contract` with the **type layer** live; the **data layer** present but dormant.
- A **linear** `vc_pipeline` with assembly-time type-contract validation.
- Two or more real P1–P3 stages as the first occupants (e.g. grayscale, blur), plus a
  pass-through stub, chained end-to-end.

Everything here works with the **current** `vc_image` / `vc_pixel_buffer` unchanged.

### 8.2 [LATER] — filled in when the problem demands it

- `vc_image_spec` + the five semantic axes on `vc_image`; the **data contract** goes live;
  spec propagation + optional auto-conversion.
- List-valued slots + fan-in wiring for **HDR merge** (Block 2).
- Non-linear DAG runner; scheduling/tiling/caching; demand-driven pull.
- CLI: stages as subcommands, JSON output, introspection command; the string↔enum boundary
  map exercised for real.
- Config/graph loading (file- or app-driven composition); the camera-app graph assembly.
- `f16` dtype; scene-referred unbounded range (retire the `[0,1]` assumption).

---

## 9. How we see this evolving

[Reasoning / forward view — not commitments]

- **Camera app as the first true runtime-composition consumer.** A UI that assembles a different
  stage graph per capture mode is the cleanest trigger for the full mechanism, and plausibly
  arrives before the curriculum's nominal P16 if an app wrapper is built early. When it does,
  the slot/packet/contract scaffold should *slot in* rather than require redesign — that "it
  slots in automatically if it is the right design" is the bet this whole document makes.
- **CLI/agent-ready surface.** The introspection command ("list all stages and their
  parameters/specs") is *why* specs are declarative descriptors rather than opaque predicates:
  the same descriptors that gate connections also feed discovery.
- **Library-as-SDK (EXTEND axis).** If third parties ever add their own stages, the open-payload
  `std::any` packet and the localized "add a stage = localized change" property are the
  enabling design; whether to harden the internal interface into a public, stable one is a
  post-MVP product decision, gated on real developer pull — explicitly *not* built now.
- **Auto-negotiation maturing.** Spec propagation starts as a *check* (reject mismatches) and can
  grow into *negotiation* (auto-insert converters for convertible mismatches), the way darktable
  inserts colorspace conversions — a natural, backward-compatible extension of §5.4.

---

## 10. Relationship to the curriculum (P4 / ARCH-3)

[Honest note] The curriculum **defers** the pipeline/buffer abstraction to P4, on a
"design-after-reconnaissance, build-when-the-problem-demands-it" philosophy. We are deliberately
building a **minimal scaffold earlier** — a considered exception, made for two stated reasons:
(1) hands-on learning reps and early friction discovery during P1–P3; (2) the design target is
well-specified (the P4 CLI, ARCH-3's dual buffer), so the scaffold is built against a real spec,
not an imagined one. The *full* mechanism (data contracts, DAG runner, config loading) remains
deferred exactly as the curriculum intends; only the **1-image→1-image linear skeleton** is
pulled forward. The Tier-D crux the curriculum flags — the buffer/`vc_image` semantics — is
explicitly *not* pulled forward; it is [LATER], for the reasons in §5.

---

## 11. Consolidated open questions & assumptions

**[OPEN]**
1. Fan-in wiring for list-valued slots — multi-connection slot vs explicit `collect` stage (§4.1).
2. Packet payload container — open `std::any` vs closed `std::variant` (§4.3). **[Resolved in §12: `std::any`.]**
3. Optional compile-time-checked code-composition wiring API — build it or not (§6). **[Deferred in §12.4 — the typed `connect` enables a `static_assert` later.]**
4. Final field set of `vc_image_spec`; where CFA pattern + black/white levels + WB multipliers
   live (on the spec, on `vc_image`, or a separate raw-metadata struct) (§5.2–5.3).
5. Whether spec matching is exact-only or supports negotiation/auto-conversion from day one (§9).
6. Exact spelling/mechanism of the `ctx.in<S>()` per-stage-trait sugar over a generic context (§4.3). **[Resolved in §12: subsumed by the typed `slot<T>` descriptor — `ctx.in(slots::rgb)` deduces the type.]**

**[ASSUMPTION]**
1. Pipes are per-run stateless; all config is constructor-captured (§4.4).
2. Provisional type/identifier names (`i_pipe`, `vc_packet`, `vc_pipe_context`, `vc_contract`,
   `vc_image_spec`, `vc_pipeline`, the `layout` / `color_space` / `channel_kind` enums) are
   placeholders pending implementation; only the *shapes* are decided, not the final spellings.
   **[Resolved in §12 — the pipe-framework names are now committed; the `vc_image_spec` field enums
   remain `[LATER]`.]**
3. The near-term runner is linear; the interface is designed not to preclude a DAG, but that is
   unverified until a DAG is actually built.

---

## 12. Converged decisions & Phase 1 build spec (2026-07 design session)

> **This section supersedes the *illustrative sketches* in §2–§8 where they differ** (the `i_pipe`
> shape, `expects<T>()`, the 3-arg `run()`). The reasoning in §1–§11 still stands; this is the
> settled concrete surface built from it. The identifier names below are now **committed**, not
> `[ASSUMPTION]` (this resolves §11 Assumption 2).

### 12.1 The organizing principle — authoring edge vs framework interior

One line runs through every decision: **compile-time types live only at the authoring edge; the
framework interior operates on erased runtime values.** A stage *names and types* its slots with a
compile-time `slot<T>` descriptor; the instant that crosses into the framework it is **lowered** —
the type erased to `std::type_index`, the name kept as a string. This is the *same* choice already
made for `vc_pixel_buffer` (runtime variant, not `vc_pixel_buffer<T>`) and `vc_packet`
(`std::any`), and it is **forced** by runtime composition (CLI / config / camera-app graphs): the
framework cannot be templated on payload type and still be wired from data.

**Corollary — one canonical representation per abstraction.** `slot<T>` is the single *authoring*
definition; everything else is a projection of it, in lowered form:

| Abstraction | Canonical form | Visibility |
|---|---|---|
| slot definition (name + type), authoring | `slot<T>` | **public** (stage authors) |
| slot identity | `slot_name` (string) | public |
| payload type | the C++ type / `std::type_index` | — |
| author-facing contract surface | `contract_builder` (write-only view) | **public** (stage authors) — see §12.6 |
| declared slot (runtime record) | `slot_decl { name, type_index }` | **private to `vc_pipe_contract`** |
| graph coordinate / `run()` map key | `stage_port { stage_name, slot_name }` | framework/assembly (§12.6) |

Exactly **three** slot representations, each with one job: `slot<T>` authors, `slot_decl` stores
(private), `stage_port` wires. `slot<T>` appears only at the author-facing call sites
(`contract_builder::add_input_slot` / `add_output_slot`, `ctx.get_input`, `ctx.set_output`, and
`connect`) and is gone by the next line — it never propagates or is stored. `stage_port` is the graph
coordinate: `connect` builds them from `slot<T>` and `run()` keys its maps by them. **Visibility is
decided by audience — *what may a stage author or external caller touch?*** The author-facing surface
is `slot<T>`-only; name-keyed methods (`stage_port`'s string ctor, the contract's type/name lookups,
the context's string get/set) are framework-internal, reached by `vc_pipeline` using types it owns, no
`friend` (§12.6). The erased `type_index` leaks only as an opaque token for the type-check.

### 12.2 Decisions

**Namespace & aliases.** `vc::pipe`. `slot_name` / `stage_name = vc::utils::string`. `slot_index`
(uint32) reserved, unused by the linear runner.

**`i_pipe` — a thin abstract base (no separate base class).**
- `name()` — **per-instance**, stored, set via a `protected` ctor. The instance's *address in this
  graph*; must be per-instance because a graph can hold several stages of the same type (e.g. N
  `align` stages in an HDR merge).
- `kind()` — **per-type**, pure virtual returning a `const char*` literal (`"grayscale"`) for type
  identity (CLI registry / config factory / logging). [DECIDED — kept, even though no Phase-1
  consumer uses it yet.]
- `declare()` / `process()` — pure virtual, `const`.
- Accepted trade: `i_pipe` is no longer a *pure* interface (it carries one field). The earlier
  `vc_pipe_base` helper is dropped — folded into `i_pipe`.

**`vc_packet`.** Unchanged: by-value templated ctor + `requires` guard (defensive for by-value;
load-bearing only under a `T&&` forwarding-ref form), default ctor, `std::any`, pointer-form
`any_cast` in `get<T>`.

**Slots — a typed descriptor, defined per stage.**
- `template <typename T> struct slot { std::string_view name; };` — bonds name ↔ type.
- Defined **on each stage** as `static constexpr` members (a nested `struct slots`). Local, because
  a slot is a port on *that* stage — the narrowest scope where it is meaningful (contrast
  `vc_error_code`, which is cross-cutting and therefore central). **No central slot registry.** A
  small **opt-in** `vc::pipe::common` set may hold only genuinely ubiquitous ports.
- Retires `set_type<T>()` — the type comes from the descriptor. (This also **subsumes** the
  per-stage type-deduction trait discussed earlier: `ctx.in(slots::rgb)` deduces `T` for free.)

**`vc_contract`.**
- `add_input(slot<T>)` / `add_output(slot<T>)` — renamed to convey they *add* a slot; take the
  descriptor; lower it to a private `slot_decl`.
- `slot_decl { slot_name, slot_direction, std::type_index }` — **private nested; never escapes.**
- Exposes **projections only**: `input_type(name)` / `output_type(name)` →
  `std::optional<std::type_index>` (`nullopt` = no such slot), plus the `{name, type}` enumeration
  `inputs()` / `outputs()` → `std::vector<slot_view>`. `type` is a `std::type_index`, not a raw
  `type_info*`.
- **Enumeration is Phase 1, not `[LATER]`.** The map-variant `run()` forces it: an *open input* is
  a declared input slot that no connection feeds, so computing the open set needs each stage's full
  slot list — the connection list alone cannot supply it. This is the same `{name, type}` projection
  the CLI introspection command will later read; `run()` just needs it first. Raw enumeration only —
  the open-set computation lives in `run()` (a user rep), not in a contract helper.
- **Consequence to confirm:** folding the type into `slot<T>` removes the fluent builder's Phase-1
  job — there is nothing left to chain until the `[LATER]` `.matching(spec)` data-contract. So
  `add_*` **returns `void`** in Phase 1; the `slot_builder` returns when the data layer lands.

**`vc_pipe_context`.** Author-facing: `in(slot<T>) → const T&` (deduces `T` from the descriptor;
the unbox is the runtime `any_cast`) and `set_output(slot<T>, vc_packet)`. Runner-facing (used by
`run()`): `set_input(slot_name, vc_packet)` and `output(slot_name) → const vc_packet&`, keyed by
plain name. The descriptor overloads are thin sugar over the `slot_name`-keyed maps, which stay
string-keyed (a runner routing packets across a `port` has only the name).

**`port`.** `struct port { stage_name stage; slot_name slot; };` + `operator==` + `std::hash<port>`.
Public wiring coordinate. Built from *named references* (a per-instance stage name + a slot
descriptor's `.name`), never hand-typed string literals. A free helper
`make_port(stage_name, slot<T>) → port` builds a key from a slot descriptor, so `run()`'s map keys
are typo-safe like `connect()`'s arguments.

**`connect`.** `connect(stage_name from, slot<T> out, stage_name to, slot<U> in)` — stage names +
typed slot descriptors; lowers to `port{ stage, slot.name }`. Slot args are typo-safe constants. (A
compile-time `static_assert(same<T,U>)` for code-defined graphs is a `[LATER]` bonus this shape
enables.)

**Pipeline assembly.** `vc_pipeline::add(std::unique_ptr<i_pipe>)` takes ownership and reads the
stage's own `name()` — **no separate name argument** — so the internal `stage` record no longer
carries a name field. (Re-typing the stage name in both the stage ctor and `connect()` is the
restated-string cost the deferred `add()`-returns-a-handle ergonomics in §12.4 would remove.)

**`run` — the map variant (not the 1-in / 1-out form).**
- `std::unordered_map<port, vc_packet> run(std::unordered_map<port, vc_packet> inputs) const;`
- **Inject** the caller's packets onto the **open inputs** (input slots no connection feeds) →
  **execute** stages in insertion order → **harvest** the **open outputs** (output slots no
  connection consumes) into the returned map.
- Execution is **insertion-order** (add stages in dependency order); a real DAG topological sort is
  `[LATER]`.
- The caller's input map must cover **exactly** the open inputs — a missing or extra key is an error.
- Only **open** outputs are returned; an intermediate output consumed by a connection is *not*
  observable via `run()` (use `vc::utils::debug::dump()` to inspect mid-pipeline).
- Body is `TODO(you)`.

> **`make_port(stage_name, slot<T>) → port`** [DECIDED] closes this — `run()`'s map keys are built
> typo-safely from a slot descriptor, matching `connect()`'s safety:
> `make_port("grey", vc_grayscale_stage::slots::rgb)` instead of hand-assembling
> `port{ "grey", vc_grayscale_stage::slots::rgb.name }`. Used both to build the input map and to
> look results up in the returned map.

**`validate`.** Build each stage's contract via `declare()`, then per connection compare
`output_type(out)` against `input_type(in)`; throw on a missing slot or a `type_index` mismatch.
Type layer only; the data layer is `[LATER]`. Body is `TODO(you)`.

**Stages.** Each: ctor takes a `stage_name` → forwards to `i_pipe(name)`; implements `kind()`;
defines its `slots`. `vc_passthrough_stage` is the fully-implemented **worked reference**;
`vc_grayscale_stage` (image → image) and `vc_mean_brightness_stage` (image → `double` analyzer, the
heterogeneous case) have `declare()` / `process()` as `TODO(you)`.

**Input access is read-only by construction.** `ctx.in(...)` returns `const T&`, so a stage reads
its input image through `vc_image::pixels()` (const) and **produces a new image** rather than
mutating its input — `vc_image` copy is shallow (it shares the pixel buffer via `shared_ptr`), so
in-place mutation of an input would corrupt whoever else holds it. Safe in-place mutation gated on
an ownership check (`use_count() == 1`) is `[LATER]`.

### 12.3 Phase 1 build spec — the batch

| # | Change | Body written by |
|---|---|---|
| A | `i_pipe`: per-instance `name()` (protected ctor) + per-type `kind()` | me |
| B | `slot<T>` descriptor; stages publish `static constexpr` slot members | me + stages |
| C | `vc_contract`: `add_input`/`add_output(slot<T>)`; `slot_decl` → private; `input_type`/`output_type` projections + `inputs()`/`outputs()` `{name,type}` enumeration (needed by `run()`); `type_index` | me |
| D | `slot_builder` deferred to the data-contract phase; `add_*` returns `void` for now | me |
| E | `port` + `operator==` / `std::hash` + `make_port(name, slot<T>)`; `connect(name, slot<T>, name, slot<U>)` | me |
| F | `vc_pipeline`: `run()` → **map variant**; `validate()` uses type projections | signatures me · **bodies you** |
| G | Stages: name / kind / `slots`; passthrough implemented; grayscale + mean-brightness `TODO(you)` | me (shells) · **bodies you** |
| H | Tests updated to the new API; green primitives + red-spec (incl. a multi-input map-`run` case) | me |

Also in this batch (me): `vc_pipe_context` gains the author-facing `in(slot<T>)` /
`set_output(slot<T>)` overloads (§12.2), keeping its runner-facing string-keyed methods.

**Untouched in Phase 1:** `vc_image`, `vc_pixel_buffer`, `vc_types`.

### 12.4 Deferred work, sequenced (Phase 2+)

Most of what earlier discussion called "Phase 2 ergonomics" was **absorbed into Phase 1** by the
`slot<T>` decision — typed slot descriptors, `ctx.in` type deduction, and typed `make_port` keys are
all in the §12.3 batch, not deferred. What genuinely remains, in rough order (this orders the same
items §5 and §8.2 describe in detail):

- **Phase 2 — assembly ergonomics:** `add()` returns a stage handle (`stage_ref`) so `connect` /
  `make_port` refer to a stage by that handle instead of re-typing its name string; additive over the
  string-keyed API (the config path keeps it). *(The `static_assert(same<T,U>)` compile-time-check
  item was **dropped** — §6: this is a runtime-composition library, so the runtime `validate()` is
  the check; a compile-time wiring API is cost without payoff.)*
- **Phase 3 — data contract:** `vc_image_spec` + the five semantic axes on `vc_image`; the dormant
  data layer goes live; the `slot_builder` returns (with the `std::deque`-backed, reference-holding
  cleanup — dropping the raw owner pointer + index) to carry `.matching(spec)`; spec propagation /
  auto-conversion (§5).
- **Phase 4 — non-linear execution:** list-valued slots + fan-in for HDR merge (§4.1); a DAG
  topological runner (add-order stops mattering); scheduling / tiling / caching / demand-driven pull.
- **Phase 5 — external composition:** the CLI (stages as subcommands, JSON output, an introspection
  command driven by `kind()` + the contract's `{name, type}` projection); config / graph loading
  (file- or app-driven composition); the camera-app graph assembly (§8.2).
- **Ongoing / perf:** hashed or interned slot names; `f16` dtype; scene-referred unbounded range
  (retire the `[0, 1]` assumption).

### 12.5 Consolidation review (2026-07-14)

A design review after the Phase 1/2 build tightened the surface: five slot-ish representations were
scattered where three suffice. **These decisions supersede the matching bullets in §12.2–§12.4**;
where a name below differs from earlier prose, this section wins. Build stayed green; the pipe suite
stayed 14 green / 6 red (the six reps are untouched).

**Naming — `vc_pipe_*` on every core type.** `vc_packet → vc_pipe_packet`, `vc_contract →
vc_pipe_contract` (files too); `vc_pipe_context` / `vc_pipe_types` / `vc_pipeline` / `i_pipe` already
conformed. Consistency chosen over avoiding the `vc::pipe::vc_pipe_*` stutter.

**Aliases are plain `std::string`.** `slot_name` / `stage_name = std::string` (was
`vc::utils::string`, itself an alias for `std::string`). Name interning stays an Ongoing item; the
alias seam is reintroduced only if a profile demands it.

**`slot_view` removed — the load-bearing simplification.** A contract now exposes only what two
distinct callers need: `run()` computes open ports from slot **names** (`input_slot_names()` /
`output_slot_names() → std::vector<slot_name>`), and `validate()` compares **types by name**
(`input_slot_type()` / `output_slot_type()`). Nobody needs `{name, type}` together, so the public
`{name,type}` projection struct is gone. `slot_decl` drops its `direction` field too (which vector
holds it already encodes direction), collapsing to `{ slot_name, std::type_index }`. **A contract is
exactly one stage's own input/output slot formats — nothing else;** it has no knowledge of wiring.

**Context API — the symmetric 2×2 of {get,set} × {input,output}.** `in(...)` → `get_input(...)`,
`output(...)` → `get_output(...)`; `set_input` / `set_output` unchanged. Author-facing typed
overloads (`get_input(slot<T>)`, `set_output(slot<T>)`) sit beside the runner-facing string-keyed
ones, grouped by side.

**`stage_ref` removed.** `add()` now returns the stage's `stage_name` (a string) directly; the
existing string-keyed `port` ctor and `connect` serve the "refer to a stage without re-typing its
name" ergonomic. One type and two overloads deleted. *(This un-builds the §12.4 Phase-2 `stage_ref`
item; the ergonomic goal it served is met more simply.)*

**`make_port` free functions → `stage_port` constructors.** `stage_port(stage_name, slot<T>)` (lowers
the descriptor) and `stage_port(stage_name, slot_name)` (already-lowered); `stage_port` gains a
defaulted default ctor and a `hash()` member that the required `std::hash<stage_port>` specialization
forwards to. `stage_port` is no longer an aggregate, so designated-init (`port{.stage=…}`) is replaced
by the ctors.

**`connect(stage_port, stage_port)` — one wiring currency.** Was `connect(stage, slot<T>, stage,
slot<U>)`; now `connect({from, slots::out}, {to, slots::in})`, unifying `connect` and `run()` on
`stage_port` and dropping the templated `connect` overloads. Costs a brace pair per call site; buys a
single graph-wiring type.

**`port` renamed `stage_port`.** The bare noun carried no scope; the type is `{stage, slot}` — a port
*on a stage* — so the name now matches its own `.stage` field and its vocabulary siblings `stage_name`
/ `slot_name`. (Rejected `pipe_node`: a node is a stage, a port is an endpoint. Rejected `pipe_port`:
it stutters as `vc::pipe::pipe_port` and the field is `.stage`, not `.pipe`.)

**Pipeline internals trimmed.** The one-field `struct stage` wrapper is gone —
`std::vector<std::unique_ptr<i_pipe>>` directly (the name lives on the pipe). `connections_` stays on
the pipeline: a connection joins two stages, so it is graph-global topology that only exists once
stages are assembled — a per-stage contract cannot and should not hold it.

**Error model — type lookups throw, not `optional`.** `input_slot_type` / `output_slot_type` return
`std::type_index` and **throw** `vc_exception` on an undeclared slot. The review first kept them
`optional` (so `validate()` could compose a connection-aware message), then reversed it: this project
handles errors with **exceptions**, and an `optional` forces the sole caller — `validate()`, a rep —
into a not-found branch per endpoint, i.e. cyclomatic complexity the exception removes. `validate()`
now just looks the two types up and compares them; a missing slot surfaces as the throw. Consistent
with `get<T>()` / `get_input()`, which throw for the same reason (they run *after* validation, where
the caller can add nothing). Confirmed safe because the *only* callers of these two methods are
`validate()` and the tests.

**Kept (reviewed, not changed):** `run()` by value — the sink idiom: it moves packets into stage
contexts, so an rvalue argument avoids a deep image copy while an lvalue caller copies knowingly
(by-value is only cheaper for rvalues).

**Formatting.** clang-format 22 has **no config** for the "open brace → one field per line → closing
brace on its own line" designated-init block style (it only breaks a braced list past the column limit,
and `AlignAfterOpenBracket: BlockIndent` gives a different shape and would churn the whole codebase).
`// clang-format off` guards were rejected (unwanted noise), so `add_*_slot` takes clang-format's
natural wrap — which is close anyway: one designated field per line, brace attached:
`push_back(slot_decl{.name = …,` / `.type = …});`.

### 12.6 Encapsulation review (2026-07-15)

A follow-up review asked the load-bearing question: *what may a stage author or an external caller
actually touch?* The answer tightened the surface so that **no name-keyed method sits on the
author-facing surface** — the invariant is now "typed at the edge, name-keyed only inside the
framework." **These decisions supersede the matching §12.5 bullets** (Context 2×2 with public
string-keyed methods; `connect(stage_port, stage_port)`; `declare(vc_pipe_contract&)`). Build stayed
green; the pipe suite stayed 14 green / 6 red (reps untouched).

The organizing distinction is **who holds the object**, not whether a name string appears in the code.
`validate()`/`run()` are members of `vc_pipeline`; a pipeline method reading a contract *it owns* is a
class using its own internals, not exposed API — so **no `friend` is needed** (an explicit non-goal:
friending the whole pipeline would widen access as the pipeline grows pooling/caching). Two things stay
irreducibly name-based and that is fine: stage *instance* names, and `run()`'s open-slot computation
("declared names minus connected names", and `run()` is generic — no `T`).

**`contract_builder` — the whole author-facing contract surface.** `declare()` now takes a
`contract_builder&` (new type, `include/vc/pipe/vc_pipe_contract_builder.h`): a narrow, write-only,
non-copyable **view** over a `vc_pipe_contract` the framework owns, exposing only
`add_input_slot(slot<T>)` / `add_output_slot(slot<T>)` and nothing else. The full `vc_pipe_contract`
(with `input_slot_type` / `input_slot_names` / …) is unchanged but **framework-internal by use** — the
pipeline builds it, wraps it in a builder for the `declare()` call, then queries the populated contract
in `validate()`/`run()`. A stage author only ever holds a builder, so the name-keyed query methods are
off their surface without being deleted or friended.

**Context — map boundary in, `take_outputs()` out; no public per-slot name-keyed method.** The
runner-facing `set_input` / `get_output` / `has_output` and the string-keyed `get_input` are gone from
the public surface. Inputs enter through a **constructor** taking
`std::unordered_map<slot_name, vc_pipe_packet>` (the runner gathers a stage's open inputs + upstream
outputs into one map and hands it over in a single coarse call); outputs leave through
`take_outputs() &&` (harvest all at once). The author sees only the typed `get_input(slot<T>)` /
`set_output(slot<T>)`; their string-keyed lowering targets are now **private**, reached only through a
`slot<T>`. This is the mechanism that lets `run()` drive a context with **no `friend`** — it works
name-keyed on plain maps it owns, never on a per-slot context method.

**`connect` takes `slot<T>`, not `stage_port`.** `connect(stage_name, slot<Tout>, stage_name,
slot<Tin>)` (a template) replaces `connect(stage_port, stage_port)`, so the raw-string
`stage_port(stage_name, slot_name)` ctor leaves the *assembly* surface — every wire is spelled through
a stage's `slots::` members. **Deliberately NOT a compile-time type check:** `Tout`/`Tin` are
independent (the user rejected a compile-time-safety framing twice); `connect` just lowers each
descriptor to its `stage_port`, and `validate()` still does the runtime type compare (this project is
runtime-composition; config-wired graphs get no compile step). The `stage_port(stage_name, slot_name)`
ctor is **kept** (not on the author/rep surface): `stage_port` is the graph *coordinate* — `run()`'s
map key — and `run()` must build result keys from `(stage_name, slot_name)` internally during harvest;
with no `friend`, that ctor stays reachable. It is the config/framework coordinate ctor, analogous to
instance names being irreducibly string.

**`validate()` stays a real rep (option A).** It looks each connection's two types up from the internal
contracts and compares them — the meaningful exercise — rather than having `connect` pre-capture types
into the connection struct (which would make `validate()` a trivial field compare).

**Net author-facing surface:** `slot<T>` for `declare()`, `get_input`, `set_output`, and `connect`; the
`run()` map at one coarse boundary; the context constructor / `take_outputs()`. **Zero name-keyed
methods on the surfaces handed to a stage author** (`declare` → `contract_builder`, `process` →
typed-only context); zero `friend`. Note the honest scope: `vc_pipe_contract` and `stage_port` remain
**public, freely-constructible** types whose name-keyed members an external caller *can* reach —
"framework-internal" here means *not handed to authors*, not *inaccessible*. The boundary that matters,
and the one this review closes, is the author/rep surface. Fully hiding `stage_port`'s string ctor
would need a narrow `friend class vc_pipeline` on `stage_port` (so `run()`'s harvest builds result keys
through a private ctor) — a value-type friendship, unlike the rejected friend-on-the-growing-pipeline;
[OPEN] — **taken up and resolved in §12.7**.

### 12.7 Encapsulation closure — specified, then deferred by choice (2026-07-16)

The §12.6 `[OPEN]` question — *should the residual public string surfaces be mechanically closed?* — was
taken up in full. Outcome: **specify the closure completely, keep the code where it is, do not implement
it now.** The residual public string surfaces on `vc_pipe_contract` / `stage_port` / `vc_pipe_context`
are left open **by choice, not oversight.**

**The full closure, specified** (what "zero public string surface" would actually take):

- **`stage_port`** — privatize the `stage_port(stage_name, slot_name)` ctor (keep the typed
  `stage_port(stage_name, slot<T>)` ctor public); add `friend class vc_pipeline` so `run()`'s harvest
  builds result keys through the private ctor.
- **`vc_pipe_contract`** — privatize the ctor, `add_*_slot`, and the `*_slot_type` / `*_slot_names`
  query methods; add `friend class vc_pipeline` (owns and queries the contract) **and**
  `friend class contract_builder` (writes through it in `declare()`).
- **`vc_pipe_context`** — privatize the map constructor and `take_outputs()`; add
  `friend class vc_pipeline` (the only legitimate driver).

That is **four narrow friend grants**, each scoped to a small value/view type that opens only to
`vc_pipeline` (one-directional, not mutual, not transitive) — categorically different from the broad
*friend-the-whole-pipeline* rejected in §12.6, whose objection was that the pipeline itself grows
pooling/caching surface over time. These grants do not have that problem; they were never the concern.

**Decision: keep the code as-is — and the reason is *not* "the callers are still stubs."**

- On record and corrected: arguing that `validate()`/`run()` "don't exist yet" is **not** a valid
  justification — they will be written soon, and a design is not settled by the transient absence of its
  callers. The sequencing principle only holds in its *correct* form: harden an interface against
  *observed* usage friction, never against the mere fact that usage has not happened yet.
- The load-bearing reason is that **the interior strings are the erased core, not a leak to seal.** There
  are exactly **two** real audiences: the *author edge*, which must be typed — already closed in §12.6
  via `contract_builder` + the typed context — and the *framework interior*, which is legitimately
  name-keyed because `run()`/`validate()` are generic over `i_pipe*` and hold no `T`. The closure would
  defend a **third** boundary — "the interior must be *mechanically* unreachable from outside" — whose
  beneficiary does not exist. No external caller constructs a rogue `vc_pipe_contract` or `stage_port`:
  the pipeline owns contracts; authors are handed builders; a future plugin authors *stages*, not
  contracts or graph coordinates. Spending four friend grants plus value-type→pipeline coupling to
  defend a threat model with no inhabitant is cost without payoff.
- The right moment to close it is when a real caller reveals friction, or an actual external-author
  boundary (a third-party plugin surface) materializes — informed by usage, not preemptive.

**Learning-build note.** This is a scaffold-for-fluency build (Claude scaffolds; the user writes the
reps). Implementing the closure as a **rep on the `friend`/attorney idiom** is a legitimate exercise on
its own axis, wholly separate from the engineering verdict above — practice in the idiom, not a
correctness fix. The §12.6 surface is already more than sufficient to write `validate()`/`run()` and the
stages against; nothing downstream is blocked on this closure either way.

This resolves the §12.6 `[OPEN]` item: **deferred, by choice.**
