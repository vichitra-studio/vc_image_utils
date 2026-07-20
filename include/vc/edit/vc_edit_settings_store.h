// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <concepts>
#include <string>

#include "vc/edit/vc_table.h"

namespace vc::edit {

// A settings key path (e.g. "exposure.ev").
using edit_settings_key = std::string;

// TOTAL-GET reader concept: `const R&` exposes `get(key, fallback) -> V`, a
// miss silently returning `fallback`. vc_edit_settings_reader below conforms
// to THIS concept, not i_edit_table's vc_optional_reader (vc_edit_table.h) —
// the two flavors are siblings, not one shape (do not force-fit
// vc_edit_settings_reader into vc_optional_reader, or vice versa; they differ
// in arity as well as return type). Lives here, not in a shared concepts
// file, because vc_edit_settings_reader is its only conformer.
template <typename R, typename K, typename V>
concept vc_defaulted_reader = requires(const R& r, const K& key, V fallback) {
    { r.get(key, fallback) } -> std::same_as<V>;
};

// TYPED adapter over i_table for edit settings. The [LATER] schema-driven
// save/load programs against THIS surface. vc_edit_settings_writer/
// vc_edit_settings_reader HIDE misses behind defaults (a "total get": a
// missing key yields the field's default, never an error) — the opposite of
// i_edit_table, which exposes misses (vc_edit_table.h). The value vocabulary
// starts minimal (the scalar param types vc_edit_document uses) and broadens
// when custom types (matrices, curves) appear.
class vc_edit_settings_writer {
  public:
    explicit vc_edit_settings_writer(i_table& store);

    // TODO(you): serialise `value` under `key` into the backing store. Hand-roll
    // the mapping now — which fields, ranges, versioning — over the
    // nlohmann/json codec (vendored at third_party/nlohmann/json.hpp, MIT).
    // The codec is available; the MAPPING is the rep. Encode to data_bytes and
    // store_.put(key, ...).
    void set(const edit_settings_key& key, double value); // representative scalar
    void set(const edit_settings_key& key, int value);
    void set(const edit_settings_key& key, bool value);

  private:
    i_table& store_;
};

// Pin the contract at compile time: vc_edit_settings_writer conforms to the
// shared vc_writer concept (same `set(key, value)` shape the edit-table
// stores use).
static_assert(vc_writer<vc_edit_settings_writer, edit_settings_key, double>);

class vc_edit_settings_reader {
  public:
    explicit vc_edit_settings_reader(const i_table& store);

    // TODO(you): TOTAL get — decode the stored value under `key`, or return
    // `fallback` on a miss (store_.get() yields nullopt). A missing key is a
    // default, never an error — this is the miss-hiding half of the split.
    double get(const edit_settings_key& key, double fallback) const;
    int get(const edit_settings_key& key, int fallback) const;
    bool get(const edit_settings_key& key, bool fallback) const;

  private:
    const i_table& store_;
};

// Pin the contract at compile time: vc_edit_settings_reader conforms to
// vc_defaulted_reader above, NOT i_edit_table's vc_optional_reader.
static_assert(vc_defaulted_reader<vc_edit_settings_reader, edit_settings_key, double>);

} // namespace vc::edit
