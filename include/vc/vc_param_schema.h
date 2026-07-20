// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

// The parameter SCHEMA mechanism. Declare each field of a plain params struct
// ONCE as a `vc_param_field` descriptor; then ONE generic save()/load() walks the
// schema — so serialization (and, later, defaults, range checks, UI hints,
// cache-key material) all derive from a single source instead of
// hand-written per-field code. That per-field duplication is RawTherapee's
// ~5900-line trap; this escapes it while keeping the params a plain,
// hot-path-friendly typed struct (no variant bag).
//
// FOUNDATIONAL / neutral: lives in `vc::params`, not `vc::edit` or `vc::pipe`, so
// BOTH a pipe stage's params (a toy blur's radius) and an edit setting can
// use it with no layering cycle.
//
// Teaching notes (feature follows problem) live inline below — each template
// facility is here because a specific need forces it.
namespace vc::params {

// A param's value type. Deliberately EXACTLY {double, int, bool} — the three
// scalar types the vc_edit_settings_writer/vc_edit_settings_reader overloads
// accept (vc_edit_settings_store.h, and the sink contract below). A wider
// constraint (e.g. std::is_arithmetic_v,
// which also admits long / std::size_t / unsigned) would make the
// `out.set(name, value)` call in save() AMBIGUOUS across those three equal-rank
// overloads — a compile error, not a warning. Broaden this AND the sink together,
// per new type, when a real param needs it (custom types are [LATER]). Mirrors
// the `vc_pixel_element` concept: the concept IS the compile-time type check,
// with better diagnostics than a static_assert.
template <typename T>
concept vc_param_value = std::is_same_v<T, double> || std::is_same_v<T, int> ||
                         std::is_same_v<T, bool>;

// ONE schema entry. Templated on the owning struct `C` AND the field type `T`
// because a pointer-to-member has type `T C::*` — it names "the member, in
// general," independent of any object. `p.*member` then reads that member out of
// a specific `p`. min/max are range metadata that has no home in the plain struct
// itself (an `optional` so a field can be unranged, like a bool).
template <typename C, vc_param_value T>
struct vc_param_field {
    std::string_view name;   // serialization key / UI label
    T C::* member;           // pointer-to-member: reach the field generically
    std::optional<T> min{};  // range metadata (empty for unranged fields)
    std::optional<T> max{};
};

// Deduction guides — so you write `vc_param_field{"radius", &vc_blur_params::radius}`
// and let `C` and `T` be deduced, instead of spelling
// `vc_param_field<vc_blur_params, double>{...}` by hand. One guide for the unranged
// (2-arg) form, one for the ranged (4-arg).
template <typename C, vc_param_value T>
vc_param_field(std::string_view, T C::*) -> vc_param_field<C, T>;
template <typename C, vc_param_value T>
vc_param_field(std::string_view, T C::*, T, T) -> vc_param_field<C, T>;

// A schema is just a `std::tuple` of `vc_param_field`s — a tuple (not a vector)
// because the entries have DIFFERENT types (vc_param_field<C,double>,
// vc_param_field<C,bool>, …) and a tuple is a fixed heterogeneous bundle. A params
// struct co-locates its schema as a static member function:
//   struct vc_blur_params {
//       static constexpr auto schema() {
//           return std::tuple{ vc_param_field{...}, vc_param_field{...} };
//       }
//   };

// Every params struct must satisfy this: it co-locates its schema as a
// static member function, rather than taking the schema as a separate
// argument, so save()/load() can find it from the type alone.
template <typename P>
concept vc_param_struct = requires { P::schema(); };

// Generic SAVE: for every field in `Params::schema()`, emit
// `out.set(name, params.field)`. `Sink` is any object exposing
// `set(std::string, T)` for each param type — the vc_edit_settings_writer
// adapter is one such sink; a test double is another. Templating on the sink
// (rather than taking `vc_edit_settings_writer&`) keeps this header
// dependency-free and testable BEFORE vc_edit_settings_writer's body is
// written.
template <vc_param_struct Params, typename Sink>
void save(const Params& params, Sink& out) {
    // std::apply spreads the tuple's elements into the lambda as a parameter pack
    // `f...` (the variadic `auto...` GATHERS them); the fold `( expr, ... )` then
    // SPREADS the pack, emitting one `out.set(...)` per field. That fold IS the
    // hand-written save body the compiler writes for you — the compile-time
    // "loop" over a heterogeneous tuple.
    std::apply(
        [&](const auto&... f) {
            ((out.set(std::string(f.name), params.*(f.member))), ...);
        },
        Params::schema());
}

// Generic LOAD: a TOTAL get — for every field, `in.get(name, current)` returns the
// stored value or, on a miss, the field's CURRENT value (so an absent key leaves
// the existing default in place rather than erroring). This matches
// vc_edit_settings_reader's total-get contract (vc_edit_settings_store.h) and makes forward/back-compat
// trivial: an unknown/removed key simply keeps the struct's default.
template <vc_param_struct Params, typename Source>
void load(Params& params, const Source& in) {
    std::apply(
        [&](const auto&... f) {
            (((params.*(f.member)) =
                  in.get(std::string(f.name), params.*(f.member))),
             ...);
        },
        Params::schema());
}

} // namespace vc::params
