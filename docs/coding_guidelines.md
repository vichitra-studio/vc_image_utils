# Coding Guidelines — vc_image_utils

Authoritative rulebook for all C++ code in this repository.
Update this document when a new guideline is decided — do not let code drift from it.

---

## 0. Language standard — C++20

The project targets **C++20**, not the C++17 originally noted in the Week 0 curriculum doc.
Reasons this was revisited:

- No dependency in the current roadmap requires C++17 — Eigen, OIIO, Halide, LibRaw, TinyDNG
  all support C++20 cleanly (Eigen 5.0.1 verified directly: compiles and runs correctly under
  `-std=c++20` with the project's full warning set)
- **rawspeed** (a real future RAW-decoding candidate) requires C++20 as its own minimum —
  C++17 would make it impossible to integrate at all, not just harder
- The original C++17 note in the curriculum was not backed by a technical constraint

C++20 features are adopted **only when they solve a concrete problem**, not for their own
sake. The pixel-buffer mutation-surface problem (see §4.2) is solved by `vc_pixel_buffer`'s
fixed-size design, not by `std::span` alone — but `vc_pixel_buffer` is dtype-tagged (a
runtime property, decided when a file is decoded — see §3.1), so its typed accessor
(`as<T>()`) needs a return type that doesn't hard-code one element type. `std::span<T>` fills
that role: element access without the capacity-mutating surface a `vector<T>&` would reopen.
`std::concepts` is used for the same reason — `vc_pixel_element_req` constrains `as<T>()`/the
constructor to the three types the variant actually holds (`buf_f32`/`buf_u8`/`buf_u16`),
turning a request for an unsupported type into a compile error instead of a runtime throw.
Modules remain unused — no concrete problem in this project needs them yet.

---

## 1. Naming

### 1.1 Case — snake_case everywhere

All identifiers use `snake_case`. No `PascalCase`, no `camelCase`, no `SCREAMING_SNAKE` (except macros, which we avoid).

| Category | Convention | Example |
|---|---|---|
| Namespaces | snake_case | `vc`, `vc::io` |
| Classes / structs | snake_case | `vc_image`, `vc_exception` |
| Interfaces (abstract) | `i_` prefix + snake_case | `i_image_reader`, `i_image_writer` |
| Enums (scoped) | snake_case | `vc_error_code`, `vc_image_format` |
| Enum values | snake_case | `vc_error_code::file_not_found` |
| Typedefs / aliases | snake_case | `pixel_buffer_ptr`, `image_dim` |
| Functions / methods | snake_case | `to_string()`, `pixel_count()` |
| Variables | snake_case | `pixel_count`, `output_path` |
| Private members | snake_case + trailing `_` | `width_`, `channels_`, `pixels_` |
| Constants | snake_case | `max_channel_count` |
| Macros (avoid) | SCREAMING_SNAKE | `VC_ASSERT(x)` |

### 1.2 `vc_` prefix

Every user-defined type exported from this library carries the `vc_` prefix:
- Classes: `vc_image`, `vc_exception`
- Enums: `vc_error_code`, `vc_image_format`
- NOT applied to: local variables, function parameters, namespace names, typedef aliases
  (aliases already live inside a `vc::` namespace, so the namespace is the prefix)

### 1.3 No `_t` suffix

We do not use the `_t` convention (a C/POSIX pattern). Aliases read like stdlib types:

```cpp
using pixel_buffer_ptr = std::shared_ptr<vc_pixel_buffer>; // not pixel_buffer_ptr_t
using image_dim        = std::uint32_t;                    // not image_dim_t
```

### 1.4 Interface prefix

Abstract classes (pure virtual, no data) use the `i_` prefix: `i_image_reader`, `i_image_writer`.
Concrete implementations use no prefix: `stb_image_reader`, `stb_image_writer`.

---

## 2. Namespaces

### 2.1 Hierarchy — two levels maximum, no namespace aliases

Core types live at `vc::` directly. Sub-namespaces exist only where a genuinely distinct layer
warrants grouping. The `vc_` prefix on type names already provides visual disambiguation —
deep nesting adds redundancy without clarity.

```
vc              — vc_image, vc_exception, vc_error_code, i_image_meta,
                  vc_metadata_value + all core typedefs
vc::utils       — string, message, log_info_builder_base<T> (low-level, no circular
                  deps — everything can include this)
vc::utils::log  — level, debug/info/warning/error/temp, log_info_builder
vc::utils::perf — scoped_timer
vc::io          — path, vc_image_format, read_config, write_config,
                  i_image_reader, i_image_writer, stb_image_reader, stb_image_writer
vc::pipe        — i_pipe, vc_pipeline, vc_pipe_context, vc_pipe_contract,
                  vc_cancellation_token, vc_render_context, vc_passthrough_stage
vc::edit        — vc_edit_session, vc_edit_document, i_edit_table,
                  vc_cached_edits_table, vc_persistent_edits_table, vc_stage_registry,
                  vc_memory_image_meta, export_image
vc::debug       — image dump/visualisation (dump, dump_image_builder)
```

No namespace aliases anywhere (`using image = vc::vc_image` etc.). Write fully-qualified
names in headers. Two levels (`vc::io::i_image_reader`) is readable without aliasing.

**Exception — `vc::utils::{log,perf}` are three levels deep.** Both subsystems share a
shape — `set_enabled(bool)` / `enabled() noexcept`, plus (for `log`) a `*_builder` class
and tag-scoped gating that `dump_image_builder` also happens to reuse (see
`vc_log_info_builder.h`) — and neither can flatten directly into `vc::utils` itself
without colliding: two unrelated `set_enabled`/`enabled` pairs can't coexist in one
namespace. Nesting one level deeper resolves that collision without giving up the shared
shape. This is *not* because `log`/`perf` are somehow more deserving of a nested spot —
`perf` in particular has no `*_builder` and no tag-gating at all, only the bare
`set_enabled`/`enabled` pair (see `vc_perf.h`'s own comment); the "mirrors each other"
framing this exception used to make was overstated. The actual justification is narrower,
and it is about *where* a dependency goes, not whether one exists at all: `log` depends
only on things already inside `vc::utils` — `vc::utils::string`/`message` (`vc_strings.h`)
and the shared `log_info_builder_base` (`vc_log_info_builder.h`) — plus the standard
library. `perf` adds one real dependency on top of that: `vc_perf.cpp` includes
`vc_log.h` and calls `vc::utils::log::info(...)` from `~scoped_timer()` to report elapsed
time, so `perf` depends on `log`. That edge is real, but it stays inside `vc::utils` —
`log` is a sibling namespace nested under the same parent, not a different top-level
package — so it is not a reason to move `perf` anywhere, unlike `vc::debug`'s dependency
on `vc::io` below, which does cross out of `vc::utils` entirely. The invariant that
actually earns `log`/`perf` their spot is: no dependency *outside* `vc::utils`, not no
dependency at all, and the collision is what forces the extra level once they're there.

`vc::debug` (image dump/visualisation) faces the identical `set_enabled`/`enabled`
collision but resolves it differently, by *not* nesting under `utils` at all: it
depends on `vc::io` (to write the dumped file) and on `vc::vc_image`, so it fails the
"nothing outside `vc::utils`" test that gets `log`/`perf` into `utils` to begin with. It
sits at the top level instead, alongside `vc::io`/`vc::pipe`/`vc::edit`, with its own
`include/vc/debug` and `src/debug`. Its builder class (`dump_image_builder`) still
derives from `vc::utils::log_info_builder_base<T>` across that namespace boundary —
deliberately (see `vc_log_info_builder.h`'s comment) — because that base enforces shape,
not co-location. Not a general license to add a third level elsewhere under `utils` —
this is a specific, reasoned carve-out for `log`/`perf` only, now that `debug` has left.

### 2.2 Never `using namespace` in headers

`using namespace X` in a header injects all of X into every file that includes it — silent
name collisions at scale. It is banned in all headers.

### 2.3 `using` declarations are acceptable in `.cpp` files

```cpp
// At the top of vc_io_stb.cpp — imports specific names, not the whole namespace
using vc::vc_image;
using vc::vc_exception;
using vc::vc_error_code;
```

---

## 3. Types — no raw primitives

We do not use `float`, `int`, `uint32_t`, `std::string`, `std::size_t` etc. directly in
public API signatures. Every primitive has a named alias that encodes its semantic role.

### 3.1 Core image types (namespace `vc`)

```cpp
namespace vc {
    enum class pixel_dtype : std::uint8_t { f32, u8, u16 };      // which concrete type is stored
    class  vc_pixel_buffer        { /* fixed-size, dtype-tagged — see Sec 4.2 */ };
    using pixel_buffer_ptr       = std::shared_ptr<vc_pixel_buffer>;
    using const_pixel_buffer_ptr = std::shared_ptr<const vc_pixel_buffer>;
    using image_dim              = std::uint32_t;                // width or height in pixels
    using channel_count          = std::uint32_t;                // 1=grey 2=greyA 3=RGB 4=RGBA
}
```

`pixel_dtype` is declared in `vc_types.h`, alongside the `shared_ptr` aliases and
`image_dim`/`channel_count` — `vc_types.h` only forward-declares `vc_pixel_buffer` itself (a
`shared_ptr` alias needs no complete type). `vc_pixel_buffer.h` includes `vc_types.h` for
`pixel_dtype` and defines the actual `vc_pixel_buffer` class, plus the `vc_pixel_element_req`
concept its constructor and `as<T>()` are constrained by.

`vc_pixel_buffer` stores one of a closed set of element types (currently `float`, `uint8_t`,
`uint16_t`) in a `std::variant`, not a single fixed type — dtype is discovered at load time (a JPEG
decodes to `uint8`, an EXR to `float`), so it's a runtime property of one concrete class, not
a compile-time template parameter that would cascade into `vc_image` and everything that
touches it. `as<T>()` is the typed accessor: state the dtype an algorithm requires, get a
`std::span<T>` back, or a `vc::vc_exception` if the buffer actually holds something else.

### 3.2 String types (namespace `vc::utils`)

```cpp
namespace vc::utils {
    using string  = std::string;    // general-purpose text
    using message = std::string;    // human-readable error or log text
}
```

### 3.3 I/O types (namespace `vc::io`)

```cpp
namespace vc::io {
    using path = std::filesystem::path;   // filesystem path
}
```

### 3.4 Why `uint32_t` for image dimensions and channels

- Images do not exceed ~4 billion pixels per dimension in practice; `uint32_t` makes the bound explicit
- `uint8_t` for small values (e.g. channel count) triggers integer promotion warnings under
  `-Wconversion`: C++ promotes `uint8_t` to `int` before every arithmetic operation
- `uint32_t` avoids this; always use `uint32_t` loop counters when iterating over dimensions:
  ```cpp
  for (vc::channel_count c = 0; c < img.channels(); ++c) { ... }
  ```

---

## 4. Ownership model

### 4.1 Pixel buffer — shared ownership

`vc_image` holds a `const_pixel_buffer_ptr` (`shared_ptr<const vc_pixel_buffer>`).

Copying a `vc_image` is **cheap** (reference count bump) and **aliases** — both copies point
to the same pixel data — but that data is `const`, so no copy can ever mutate it. This is
intentional: the library passes images around as lightweight, safe-to-share handles.

Returning the `shared_ptr` (rather than an iterator or `span`) from `pixels()` is also what
lets a caller extend the buffer's lifetime past the owning `vc_image` — e.g. handing pixel
data to an async writer or a cache that outlives the image object that produced it. A
non-owning view type cannot do this; it dangles the moment the `vc_image` is destroyed. This
is a real, recurring need (not just Week 0.2 scope), so `pixels()` stays the one paradigm for
*reading* buffer access — see §4.2 for how mutation is confined to construction instead of
being offered as a second accessor on `vc_image` itself.

### 4.2 Pixel access — const truly prevents mutation, size truly cannot change

```cpp
// vc_image — read-only, always. Returns shared_ptr<const vc_pixel_buffer> by value —
// data is read-only through this ptr, and there is no mutable counterpart on this type.
vc::const_pixel_buffer_ptr pixels() const noexcept;

// vc_image_writer — the ONLY type with write access, and only until seal(). Typed element
// write; at()/pixels<T>()/with_pixels<T>() below are the three ways in.
template <vc_pixel_element_req T> T& at(image_dim x, image_dim y, channel_count ch);
```

`vc_image::pixels()` returns `const_pixel_buffer_ptr` by value (cheap — refcount bump, no data
copy). The pointed-to `vc_pixel_buffer` is `const`, so the compiler prevents any write through
it, and `vc_image` declares no mutable accessor at all — not a private one, not a
carefully-gated one, none. The only way to get pixels into an image is to fill a
`vc_image_writer` (`at<T>()`, `pixels<T>()`, or the preferred `with_pixels<T>()`) and then
`seal()` it; sealing moves the buffer out, const-qualified, and the writer is spent. Mutation
is confined to that one construction window, never reachable afterward.

Either way, the `width*height*channels == size()` invariant cannot be broken, because
`vc_pixel_buffer` (§3.1) exposes no `resize()`/`clear()`/`push_back()` at all. This isn't
caller discipline — the invariant is enforced by construction. The same holds one layer in:
`as<T>()` returns `std::span<T>`, not the backing `std::vector<T>&`, so typed element access
doesn't reopen the capacity-mutating surface either. A caller can still write wrong pixel
*values* through the writer's accessors (inherent to any mutation access), and calling a
C-interop raw-pointer accessor and holding it past the `shared_ptr`'s lifetime is still a
dangle — that's deliberately stepping outside ownership, not something a wrapper type can
prevent.

Do not return `shared_ptr<T>&` from `pixels()` or the writer's accessors: converting
`shared_ptr<T>` to `shared_ptr<const T>` constructs a temporary, and binding a reference to it
is a `-Wreturn-stack-address` bug (dangling reference to a local temporary) — verified directly
with `clang++ -std=c++20 -Wall -Wextra -fsyntax-only`. Always return by value.

### 4.3 Passing function parameters

| Type | Convention |
|---|---|
| Small value types (enums, `uint32_t`, `bool`) | pass by value |
| Large / heap-owning types (`vc_image`, `string`) | `const&` for read; value for sink (moves in) |
| `shared_ptr` | `const pixel_buffer_ptr&` — avoid copying unless sharing ownership is intended |

### 4.4 Non-owning dependencies — `T&`, not a nullable pointer

A dependency a type does not own but requires to be valid for its entire lifetime (an
injected backing store, a required collaborator) is held as a plain C++ reference, not a raw
pointer:

```cpp
class vc_edit_session {
    // ...
    i_edit_table& persistent_; // non-owning; non-nullable
    i_edit_table& cache_;      // non-owning; non-nullable
};
```

A reference cannot be null and cannot be rebound after construction — the compiler enforces
non-nullability for free, with no wrapper type and no runtime check. Where a member is
owning but the pointer's type technically allows null even though the value must never be
(e.g. `vc_edit_session`'s `unique_ptr<i_image_meta> meta_`), the constructor's body checks
for null and throws `vc::vc_exception` before construction completes
(`vc_edit_session.cpp`), so no caller ever observes a live object with a null `meta_` —
every later access can stay an unchecked dereference. Validation happens once, at the one
construction boundary, not on every subsequent access.

(Both members above are typed against the `i_edit_table` interface rather than the concrete
`vc_persistent_edits_table`/`vc_cached_edits_table` stores they're bound to. The constructor's
two parameters deliberately stay the concrete types, though: two same-typed `i_edit_table&`
parameters could be silently swapped positionally by a caller, while distinct parameter types
make that mistake a compile error. Abstraction where storage is *used*, type safety where
identity is *established*.)

Raw pointers are reserved for two narrow cases, not a general-purpose "maybe valid" type:
- A genuinely nullable lookup result, always internal/private (e.g.
  `vc_pipeline::find_stage()`, which returns `nullptr` on a miss and is never exposed on a
  public surface).
- A C-API boundary that itself hands back a raw pointer (e.g. `stbi_load`) — checked and
  either thrown on or freed before the function returns, never held or passed around
  afterward. `stb_image_reader::read()` follows this: null is checked and thrown on
  immediately, and `stbi_image_free()` runs right before the return. Note this is a manual
  free, not RAII — a throw between the null-check and the free (e.g.
  `vc_image_writer::validated()` rejecting the decoded geometry) would leak the buffer.
  Low risk in practice, since stb only ever decodes geometry `validated()` already accepts,
  but worth knowing if that assumption ever changes.

No `gsl::not_null` or similar wrapper is used — a plain reference already gives the same
compile-time non-null guarantee for both cases above, with no added dependency. Reach for a
wrapper type only if a future site needs *pointer* semantics (rebindable, storable in a
container) combined with a non-null guarantee a reference can't express — no such site
exists today.

---

## 5. Error handling

### 5.1 Always throw `vc_exception` — never standard exceptions directly

```cpp
throw vc::vc_exception(vc::vc_error_code::file_not_found, "could not open: " + path);
// NOT: throw std::runtime_error("...");
```

`vc_exception` inherits from `std::exception` so standard catch-all handlers still work.

### 5.2 Error code enum (`vc::vc_error_code`)

```cpp
enum class vc_error_code {
    file_not_found,    // path does not exist or process lacks read permission
    invalid_format,    // file header is not a recognised image format
    decode_error,      // format recognised but data is corrupt or unsupported variant
    encode_error,      // output could not be written (bad path, disk full, permissions)
    invalid_argument,  // caller passed logically invalid data (zero dimensions, null buffer)
};
```

Every value must have a one-line comment stating exactly when it is used.

### 5.3 Error code utilities (namespace `vc`)

```cpp
[[nodiscard]] int                  to_int(vc_error_code code) noexcept;   // enum → underlying int, by value
[[nodiscard]] vc_error_code        to_error_code(int value);               // int → enum; throws vc_exception on invalid
[[nodiscard]] vc::utils::string    to_string(vc_error_code code) noexcept; // enum → short label; NRVO applies
```

- Enums are small value types — always pass by value, not const ref
- `to_string` returns `vc::utils::string` (a short label, not a full message — use `vc::utils::message` for the latter)
- Callers dispatch on `code()`, not on string matching

### 5.4 `[[nodiscard]]` on optional-returning lookups and pure factories

Because §5.1 already routes every failure path through `vc::vc_exception`, there are no
ignorable bool/error-code status returns in this codebase to protect — the usual
`[[nodiscard]] bool save(...)` case doesn't arise here. `[[nodiscard]]` is used instead
where silently discarding the return value is a plausible, specific bug:

- **Optional-returning lookups**, where a miss is expected control flow the caller must
  branch on (`i_table::get`, `i_edit_table::get`, `i_image_meta::get`, and every override):
  ```cpp
  [[nodiscard]] virtual std::optional<data_bytes>
  get(const std::string& key) const = 0;
  ```
  Mark both the interface declaration and every override explicitly — do not rely on the
  attribute propagating through virtual dispatch alone.
- **Pure factories and one-way transitions**, whose entire effect is the value they hand
  back (`vc_image::zeros`/`with_fill`, `vc_image_writer::seal()`) and pure converters
  (`to_int`, `to_error_code`, `to_string`).

Not applied to mutators/setters (they return `void`) or to trivial accessors already always
used at the call site — a blanket project-wide default would be noise given how much of
this API already fails via exceptions rather than a return value.

A `[[nodiscard]]` call made only for its side effect (e.g. a doctest `CHECK_THROWS_AS`
exercising a validating constructor) discards the result explicitly with a `static_cast<void>`
or C-style `(void)` cast — `(void)` casts to `void` are exempt from `-Wold-style-cast`, so
this is the idiomatic suppression, not a warning-flag workaround.

---

## 6. Class design

### 6.1 Getters are inline in the header

Simple single-expression accessors are defined in the class body (implicitly inline):

```cpp
vc::image_dim     width()    const noexcept { return width_; }
vc::image_dim     height()   const noexcept { return height_; }
vc::channel_count channels() const noexcept { return channels_; }
```

### 6.2 `const noexcept` on pure getters

All accessors that only read internal state are marked `const noexcept`.

### 6.3 Constructors validate then allocate

Validate inputs first, allocate after. No two-phase init, no `init()` method.

`vc_image` itself has no public constructor (§6.5) — it is only ever produced
by `vc_image_writer::seal()`. The gate lives in `vc_image_writer`'s
constructor, which validates via a private static helper so validation can
run in the member-initializer list, before the pixel buffer that depends on
the validated geometry is allocated:

```cpp
template <vc_pixel_element_req T>
vc_image_writer(image_dim width, image_dim height, channel_count channels, T fill)
    : meta_(validated(width, height, channels)),
      pixels_(std::make_shared<vc_pixel_buffer>(meta_.element_count(), fill)) {
}
```

`validated()` is kept as a `static` specifically so it can run before
`pixels_` exists — there is no member left default-constructed while
validation happens, and no way to reach the allocation with unvalidated
geometry:

```cpp
static vc_image_info
validated(image_dim width, image_dim height, channel_count channels);
// rejects width == 0 || height == 0 || channels == 0 || channels > 4
// before the descriptor is ever built
```

(`validated()` is fully implemented: it rejects `width == 0`, `height == 0`,
`channels == 0`, and `channels > 4`, throwing `vc::vc_exception` for each — the
"rejects invalid dimensions" test in `test_vc_image.cpp` passes.)

### 6.4 No raw `new` / `delete`

Use `std::make_shared`, `std::make_unique`. Raw heap allocation is banned.

### 6.5 State invariants explicitly; enforce in exactly one place

A class that promises "valid by construction" (§6.3) should say so directly, not leave the
guarantee implicit or scattered across the files that happen to rely on it:

```cpp
// Invariant: width() * height() * channels() == pixels()->size() for the lifetime of
// this object. Enforced by construction: the writer allocates the buffer at exactly
// element_count() elements, and vc_pixel_buffer exposes no resize()/clear() to break
// that afterward — never re-checked here.
class vc_image { /* ... */ };
```

The invariant is enforced in exactly one gate — typically the constructor, or, for a
builder/writer type like `vc_image_writer`, the point where the built value is validated
before use — and nowhere else re-derives or re-checks it. This is what makes "valid by
construction" true rather than aspirational: once the gate passes, every other member
function can assume the invariant holds instead of defensively re-verifying it.

(`vc_image_info::element_count()` computes the real `width*height*channels` product today, and
`vc_image_writer` allocates its buffer at exactly that many elements — so the equality above
holds in practice, not just as a target shape, and `vc_pixel_buffer`'s fixed-size, no-resize
design guarantees it can never be broken afterward. `vc_image_writer::validated()` remains a
separate, narrower gate: it only rejects degenerate *geometry* — zero dimensions, `channels > 4`
— the size==product equality itself is established by the writer's allocation, not by
`validated()`. Don't attribute an invariant to a gate that doesn't actually establish it; say
what's true today.)

Not every class needs a stated invariant. A plain data-holding struct with no constraint on
its fields yet (e.g. a settings/params struct whose valid ranges are still an open domain
question) has none to state — inventing one prematurely is worse than leaving it
undocumented until the constraint is actually decided. State an invariant when it is real
and decided; enforce it the moment it is stated.

---

## 7. I/O layer

### 7.1 Interface / adapter split

```
i_image_reader    — pure virtual interface (namespace vc::io)
i_image_writer    — pure virtual interface (namespace vc::io)
stb_image_reader  — concrete stb adapter, implements i_image_reader
stb_image_writer  — concrete stb adapter, implements i_image_writer
```

New libraries (libraw, rawspeed, libjpeg-turbo) implement the same interfaces — callers unchanged.

### 7.2 Config structs — extensible without breaking the interface

```cpp
struct read_config  { vc::pixel_dtype dtype = vc::pixel_dtype::u8; };
struct write_config { vc_image_format format = vc_image_format::jpeg;
                      int jpeg_quality = 100; };
```

Virtual methods take config structs, not format-specific parameters.
Adding a field to a config struct is not a breaking change. Adding a virtual method is.

### 7.3 Format enum (`vc::io::vc_image_format`)

```cpp
enum class vc_image_format {
    png,    // PNG (lossless, supports all channel counts)
    jpeg,   // JPEG (lossy, RGB only — alpha stripped)
    // future: tiff, exr, dng, raw, ...
};
```

### 7.4 `desired_channels = 0` always

stb loads images with their native channel count. We never force a channel conversion on load.
`vc_image::channels()` reflects what the file actually contained.

---

## 8. Comments

Write a comment only when the **why** is non-obvious: a hidden constraint, a subtle invariant,
a workaround for a known issue. Do not describe what the code does.

Mandatory exceptions:
- Every `enum class` value must have a comment stating when it is used (see §5.2, §7.3)
- Any arithmetic that requires a specific cast order to avoid overflow must explain why
  (see §6.3 constructor note on `static_cast<std::size_t>`)

---

## 9. File layout

```
include/vc/vc_pixel_buffer.h  — vc_pixel_element_req concept + vc_pixel_buffer class
include/vc/vc_types.h         — pixel_dtype enum + remaining vc:: aliases (pixel_buffer_ptr, image_dim, etc.)
include/vc/vc_error_code.h    — vc_error_code enum + to_int/to_error_code/to_string
include/vc/vc_exception.h     — vc_exception class
include/vc/vc_any_box.h       — vc_any_box<Tag> class: shared type-erased single-value box behind
                                 vc_pipe_packet / vc_metadata_value (header-only, no .cpp)
include/vc/vc_image_meta.h    — i_image_meta interface + vc_metadata_value alias (header-only, no
                                 .cpp — pure interface, no non-pure members to define). Demoted
                                 from vc::edit 2026-07-28 — see docs/edit_model.md Sec 6 and this
                                 header's own comment; the concrete backend (vc_memory_image_meta)
                                 stays in vc::edit (include/vc/edit/vc_memory_image_meta.h).
include/vc/vc_image_info.h    — vc_image_info class: image geometry + composed metadata (header-only)
include/vc/vc_image.h         — vc_image class (header-only; see §9.1)
include/vc/vc_image_writer.h  — vc_image_writer class: validated(), seal() (header-only ctor)
include/vc/utils/vc_strings.h — vc::utils string typedefs
include/vc/utils/vc_log_info_builder.h — vc::utils::log_info_builder_base<T>: shared base for
                                 log_info_builder/dump_image_builder (header-only, no .cpp — see §9)
include/vc/utils/vc_log.h     — vc::utils::log: level enum, debug/info/warning/error/temp,
                                 log_info_builder
include/vc/utils/vc_perf.h    — vc::utils::perf: scoped_timer (no builder — reports once
                                 at destruction from an enabled() answer already captured)
include/vc/io/vc_io_types.h   — vc::io typedefs (path, vc_image_format, read/write config)
include/vc/io/vc_io.h         — vc::io interfaces: i_image_reader, i_image_writer
include/vc/io/vc_io_stb.h     — stb adapter declarations: stb_image_reader, stb_image_writer
include/vc/io/vc_io_fs.h      — vc::io filesystem helpers: existence checks, directory creation,
                                 atomic writes (see src/io/vc_io_fs.cpp)
include/vc/debug/vc_image_dumper.h — vc::debug: dump, dump_image_builder

src/vc_error_code.cpp         — vc_error_code utilities implementation
src/vc_exception.cpp          — vc_exception implementation
src/vc_image.cpp              — empty TU (vc_image is header-only; see §9.1)
src/vc_image_writer.cpp       — vc_image_writer::validated()/seal() implementation
src/io/vc_io_stb.cpp          — stb adapter implementations; also the sole TU that defines
                                 the stb `_IMPLEMENTATION` macros (see §10)
src/io/vc_io_fs.cpp           — vc::io filesystem helpers implementation
src/utils/vc_log.cpp          — vc::utils::log implementation
src/utils/vc_perf.cpp         — vc::utils::perf implementation
src/debug/vc_image_dumper.cpp — vc::debug implementation
src/main.cpp                  — application entry point

tests/data/                   — bundled test fixtures (committed to repo)
docs/                         — project documentation
```

One class / one interface per header. No omnibus headers.
`src/` mirrors `include/vc/` for implementation files above — not an exhaustive rule over the whole
tree (the later `pipe`/`edit` layers are not enumerated in this box — see their own design docs).
Five worth calling out because a reader might expect a `.cpp` and not find one:
`vc_pixel_buffer.h`, `vc_log_info_builder.h`, `vc_any_box.h`, `vc_image_info.h`, and
`vc_image_meta.h` have none. For the first two, every member that isn't a template is a one-liner
(`dtype()`/`size()`; `set_tag_enabled()`/`tag_enabled()`), and the rest — the constructor and
`as<T>()` for `vc_pixel_buffer`, `operator()`/`resolve()` for `log_info_builder_base<T>` — are
templates that must be defined where instantiated. `vc_any_box<Tag>` is templated on `Tag`
itself, so the same reasoning covers the whole class, not just some members. `vc_image_info` has
no template at all — every member (including `element_count()`/`index()`) is simply a one-liner —
so there is nothing non-trivial to put in a `.cpp` regardless. `vc_image_meta.h`'s reason is
different from all four: `i_image_meta` is a pure interface (every member is either `= default`,
`= 0`, or `virtual ~... = default`) with no non-pure member to define anywhere — its one concrete
implementation, `vc_memory_image_meta`, lives in `vc::edit` and is implemented in
`src/edit/vc_image_meta.cpp` (named for the interface it backs — same convention as
`src/edit/vc_table.cpp` implementing `vc_memory_table`, see the `pipe`/`edit` layers' own file
layout).

### 9.1 Week 0.2 scaffold — original suggested implementation order (now mostly complete)

`vc_error_code.cpp` and `vc_exception.cpp` are fully implemented (low-value plumbing —
everything else needs working error signalling to give useful feedback). `vc_pixel_buffer` is
likewise fully implemented, header-only (see §9). Of the originally-suggested order below,
1 and 2 are now implemented; 3 remains a `TODO(you)` stub:

1. `vc_image`'s construction path — the validate-then-allocate logic now lives in
   `vc_image_writer::validated()`/`seal()` (`src/vc_image_writer.cpp`,
   `include/vc/vc_image_writer.h`), not `src/vc_image.cpp` (now an empty TU; the class is
   header-only, see §9). `pixel_count()` and `pixels()` are implemented; there is no
   `mutable_pixels()` — `vc_image` is uniformly read-only (§4.2). The `vc_image` test cases
   this was gating pass.
2. `src/io/vc_io_stb.cpp` — `stb_image_reader::read()` and `stb_image_writer::write()` are
   implemented. The round-trip test case passes.
3. `src/main.cpp` — wire the reader/writer together into a working CLI round-trip. Still a
   `TODO(you)` stub (untested — no case in the suite exercises `main()`).

Build stays green throughout; `src/main.cpp`'s round-trip and the three sanctioned edit-layer
reps (`export_image`; `vc_cached_edits_table`/`vc_persistent_edits_table` `get`/`set`) are the
remaining `TODO(you)` bodies in the repo.

---

## 10. Third-party code

- Vendored under `third_party/` — never modified
- Never inject our license header into `third_party/` files
- Never run clang-format on `third_party/` files
- `third_party/` is a `SYSTEM PRIVATE` include path — suppresses vendored-header warnings
- Every vendored dependency has an entry in `THIRD_PARTY_LICENSES.md` with pinned commit SHA
- stb is compiled in exactly one translation unit (`src/io/vc_io_stb.cpp`, the only file that
  includes the stb headers) via `_IMPLEMENTATION` defines
