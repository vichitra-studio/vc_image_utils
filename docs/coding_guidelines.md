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
`std::concepts` is used for the same reason — `vc_pixel_element` constrains `as<T>()`/the
constructor to the two types the variant actually holds, turning a request for an unsupported
type into a compile error instead of a runtime throw. Modules remain unused — no concrete
problem in this project needs them yet.

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
| Functions / methods | snake_case | `to_string()`, `mutable_pixels()` |
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
vc              — vc_image, vc_exception, vc_error_code + all core typedefs
vc::utils       — string, message (low-level, no circular deps — everything can include this)
vc::utils::log  — level, debug/info/warning/error/temp, log_builder
vc::utils::perf — scoped_timer
vc::utils::debug — image dump/visualisation (dump, dump_builder)
vc::io          — path, vc_image_format, read_config, write_config,
                  i_image_reader, i_image_writer, stb_image_reader, stb_image_writer
```

No namespace aliases anywhere (`using image = vc::vc_image` etc.). Write fully-qualified
names in headers. Two levels (`vc::io::i_image_reader`) is readable without aliasing.

**Exception — `vc::utils::{log,perf,debug}` are three levels deep.** These three
subsystems are genuinely low-level, dependency-free utilities (the same description
`vc::utils` itself is defined by below), which is why they live under `utils` rather than
as top-level siblings of it. But they also deliberately mirror each other's shape —
`set_enabled(bool)` / `enabled() noexcept`, a `*_builder` class, the same tag-scoped
gating pattern — so they can't be flattened directly into `vc::utils` itself without
colliding (three unrelated `set_enabled`/`enabled` pairs can't coexist in one namespace).
Nesting them one level deeper resolves the collision without giving up the intentional
consistency across the three. Not a general license to add a third level elsewhere —
this is a specific, reasoned carve-out for this one case.

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
    enum class pixel_dtype { f32, u8 };                          // which concrete type is stored
    class  vc_pixel_buffer        { /* fixed-size, dtype-tagged — see Sec 4.2 */ };
    using pixel_buffer_ptr       = std::shared_ptr<vc_pixel_buffer>;
    using const_pixel_buffer_ptr = std::shared_ptr<const vc_pixel_buffer>;
    using image_dim              = std::uint32_t;                // width or height in pixels
    using channel_count          = std::uint32_t;                // 1=grey 2=greyA 3=RGB 4=RGBA
}
```

`pixel_dtype` and `vc_pixel_buffer` are declared in `vc_pixel_buffer.h`; `vc_types.h` includes
it and adds the `shared_ptr` aliases plus `image_dim`/`channel_count`.

`vc_pixel_buffer` stores one of a closed set of element types (currently `float`, `uint8_t`)
in a `std::variant`, not a single fixed type — dtype is discovered at load time (a JPEG
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
    using path = std::string;   // filesystem path — upgrade to std::filesystem::path at P4
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

`vc_image` holds a `pixel_buffer_ptr` (`shared_ptr<vc_pixel_buffer>`).

Copying a `vc_image` is **cheap** (reference count bump) but **aliases** — both copies point
to the same pixel data. Mutating through one copy mutates all. This is intentional: the
library passes images around as lightweight handles.

Returning the `shared_ptr` (rather than an iterator or `span`) from `pixels()`/
`mutable_pixels()` is also what lets a caller extend the buffer's lifetime past the owning
`vc_image` — e.g. handing pixel data to an async writer or a cache that outlives the image
object that produced it. A non-owning view type cannot do this; it dangles the moment the
`vc_image` is destroyed. This is a real, recurring need (not just Week 0.2 scope), so
`pixels()`/`mutable_pixels()` stay as the one paradigm for buffer access — see §4.2 for how
the resize/clear risk that would normally come with sharing a mutable container is designed
out instead of routed around with a second accessor type.

### 4.2 Pixel access — const truly prevents mutation, size truly cannot change

```cpp
// Returns shared_ptr<const vc_pixel_buffer> by value — data is read-only through this ptr
vc::const_pixel_buffer_ptr pixels() const noexcept;

// Returns shared_ptr<vc_pixel_buffer> by value — explicit mutation path, name signals intent
vc::pixel_buffer_ptr mutable_pixels() noexcept;
```

`pixels()` returns `const_pixel_buffer_ptr` by value (cheap — refcount bump, no data copy).
The pointed-to `vc_pixel_buffer` is `const`, so the compiler prevents any write through it.
`mutable_pixels()` forces the caller to explicitly opt into mutation — but even then, the
`width*height*channels == size()` invariant cannot be broken, because `vc_pixel_buffer`
(§3.1) exposes no `resize()`/`clear()`/`push_back()` at all. This isn't caller discipline —
the invariant is enforced by construction. The same holds one layer in: `as<T>()` returns
`std::span<T>`, not the backing `std::vector<T>&`, so typed element access doesn't reopen the
capacity-mutating surface either. A caller can still write wrong pixel *values* through
`mutable_pixels()`/`as<T>()` (inherent to any mutation access), and calling a C-interop
raw-pointer accessor and holding it past the `shared_ptr`'s lifetime is still a dangle —
that's deliberately stepping outside ownership, not something a wrapper type can prevent.

Do not return `shared_ptr<T>&` from either accessor: converting `shared_ptr<T>` to
`shared_ptr<const T>` constructs a temporary, and binding a reference to it is a
`-Wreturn-stack-address` bug (dangling reference to a local temporary) — verified directly
with `clang++ -std=c++20 -Wall -Wextra -fsyntax-only`. Always return by value.

### 4.3 Passing function parameters

| Type | Convention |
|---|---|
| Small value types (enums, `uint32_t`, `bool`) | pass by value |
| Large / heap-owning types (`vc_image`, `string`) | `const&` for read; value for sink (moves in) |
| `shared_ptr` | `const pixel_buffer_ptr&` — avoid copying unless sharing ownership is intended |

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
int                  to_int(vc_error_code code) noexcept;   // enum → underlying int, by value
vc_error_code        to_error_code(int value);               // int → enum; throws vc_exception on invalid
vc::utils::string    to_string(vc_error_code code) noexcept; // enum → short label; NRVO applies
```

- Enums are small value types — always pass by value, not const ref
- `to_string` returns `vc::utils::string` (a short label, not a full message — use `vc::utils::message` for the latter)
- Callers dispatch on `code()`, not on string matching

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

```cpp
vc_image(vc::image_dim w, vc::image_dim h, vc::channel_count c) {
    if (w == 0 || h == 0 || c == 0 || c > 4)
        throw vc_exception(vc_error_code::invalid_argument, "invalid dimensions");
    // cast the FIRST operand to size_t before multiplying — keeps the rest of the
    // chain in 64-bit and avoids uint32_t overflow for large-but-valid images
    pixels_ = std::make_shared<vc::vc_pixel_buffer>(
        static_cast<std::size_t>(w) * h * c, 0.0f);
    width_ = w; height_ = h; channels_ = c;
}
```

### 6.4 No raw `new` / `delete`

Use `std::make_shared`, `std::make_unique`. Raw heap allocation is banned.

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
struct read_config  { /* fields added here as needed — empty for now */ };
struct write_config { vc_image_format format = vc_image_format::png; /* + quality etc. later */ };
```

Virtual methods take config structs, not format-specific parameters.
Adding a field to a config struct is not a breaking change. Adding a virtual method is.

### 7.3 Format enum (`vc::io::vc_image_format`)

```cpp
enum class vc_image_format {
    png,    // PNG (lossless, supports all channel counts)
    jpeg,   // JPEG (lossy, RGB only — alpha stripped)
    bmp,    // BMP (uncompressed, no alpha)
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
include/vc/vc_pixel_buffer.h  — pixel_dtype enum + vc_pixel_element concept + vc_pixel_buffer class
include/vc/vc_types.h         — remaining vc:: typedefs (pixel_buffer_ptr, image_dim, etc.)
include/vc/vc_error_code.h    — vc_error_code enum + to_int/to_error_code/to_string
include/vc/vc_exception.h     — vc_exception class
include/vc/vc_image.h         — vc_image class
include/vc/utils/vc_strings.h — vc::utils string typedefs
include/vc/utils/vc_info_builder.h — vc::utils::info_builder<T>: shared base for
                                 log_builder/dump_builder (header-only, no .cpp — see §9)
include/vc/utils/vc_log.h     — vc::utils::log: level enum, debug/info/warning/error/temp,
                                 log_builder
include/vc/utils/vc_perf.h    — vc::utils::perf: scoped_timer (no builder — reports once
                                 at destruction from an enabled() answer already captured)
include/vc/utils/vc_image_dumper.h — vc::utils::debug: dump, dump_builder
include/vc/io/vc_io_types.h   — vc::io typedefs (path, vc_image_format, read/write config)
include/vc/io/vc_io.h         — vc::io interfaces + stb adapter declarations

src/vc_error_code.cpp         — vc_error_code utilities implementation
src/vc_exception.cpp          — vc_exception implementation
src/vc_image.cpp              — vc_image implementation
src/io/vc_io_stb.cpp          — stb adapter implementations; also the sole TU that defines
                                 the stb `_IMPLEMENTATION` macros (see §10)
src/utils/vc_log.cpp          — vc::utils::log implementation
src/utils/vc_perf.cpp         — vc::utils::perf implementation
src/utils/vc_image_dumper.cpp — vc::utils::debug implementation
src/main.cpp                  — application entry point

tests/data/                   — bundled test fixtures (committed to repo)
docs/                         — project documentation
```

One class / one interface per header. No omnibus headers.
`src/` mirrors `include/vc/` for implementation files — except `vc_pixel_buffer.h` and
`vc_info_builder.h`, neither of which has a `.cpp`: every member that isn't a template is a
one-liner (`dtype()`/`size()`; `set_tag_enabled()`/`tag_enabled()`), and the rest — the
constructor and `as<T>()` for `vc_pixel_buffer`, `operator()`/`resolve()` for `gate<T>` —
are templates that must be defined where instantiated. Nothing non-template is left to put in
a `.cpp`.

### 9.1 Week 0.2 scaffold — suggested implementation order

`vc_error_code.cpp` and `vc_exception.cpp` are fully implemented (low-value plumbing —
everything else needs working error signalling to give useful feedback). `vc_pixel_buffer` is
likewise fully implemented, header-only (see §9). Everything else is a `TODO(you)` stub that
compiles and runs, but fails its tests until implemented:

1. `src/vc_image.cpp` — the constructor (validate + overflow-safe allocate), `pixel_count()`,
   `pixels()`, `mutable_pixels()`. Run `ctest --preset debug` — the first two `vc_image` test
   cases should go green.
2. `src/io/vc_io_stb.cpp` — `stb_image_reader::read()` and `stb_image_writer::write()`. This
   is the actual hello-image toy deliverable. The round-trip test case should go green.
3. `src/main.cpp` — wire the reader/writer together into a working CLI round-trip.

Each stub's TODO comment lists the exact steps. Build stays green throughout — only tests
fail until each piece lands.

---

## 10. Third-party code

- Vendored under `third_party/` — never modified
- Never inject our license header into `third_party/` files
- Never run clang-format on `third_party/` files
- `third_party/` is a `SYSTEM PRIVATE` include path — suppresses vendored-header warnings
- Every vendored dependency has an entry in `THIRD_PARTY_LICENSES.md` with pinned commit SHA
- stb is compiled in exactly one translation unit (`src/io/vc_io_stb.cpp`, the only file that
  includes the stb headers) via `_IMPLEMENTATION` defines
