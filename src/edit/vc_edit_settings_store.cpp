// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/edit/vc_edit_settings_store.h"

#include "vc/vc_error_code.h"
#include "vc/vc_exception.h"

// TODO(you): the serialization rep needs the vendored JSON codec —
//   #include "nlohmann/json.hpp"   // third_party/, SYSTEM PRIVATE (like stb)
// Deliberately NOT included yet: the ~900KB header should not be pulled into a
// TU whose bodies only throw. Add it when you write the set()/get() mapping.

namespace vc::edit {

vc_edit_settings_writer::vc_edit_settings_writer(i_table& store) : store_(store) {
}

// ── set(): serialise a scalar under `key`. TODO(you) — throwing shells. ──

void vc_edit_settings_writer::set(const edit_settings_key& key, double value) {
    (void)key;
    (void)value;
    (void)store_; // the rep encodes into store_.put(); marks the use site
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_writer::set(double) not yet implemented");
}

void vc_edit_settings_writer::set(const edit_settings_key& key, int value) {
    (void)key;
    (void)value;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_writer::set(int) not yet implemented");
}

void vc_edit_settings_writer::set(const edit_settings_key& key, bool value) {
    (void)key;
    (void)value;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_writer::set(bool) not yet implemented");
}

vc_edit_settings_reader::vc_edit_settings_reader(const i_table& store) : store_(store) {
}

// ── get(): total get — stored value or `fallback`. TODO(you) — shells. ──

double vc_edit_settings_reader::get(const edit_settings_key& key, double fallback) const {
    (void)key;
    (void)fallback;
    (void)store_; // the rep reads store_.get(); marks the use site
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_reader::get(double) not yet implemented");
}

int vc_edit_settings_reader::get(const edit_settings_key& key, int fallback) const {
    (void)key;
    (void)fallback;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_reader::get(int) not yet implemented");
}

bool vc_edit_settings_reader::get(const edit_settings_key& key, bool fallback) const {
    (void)key;
    (void)fallback;
    throw vc::vc_exception(vc_error_code::invalid_argument,
                           "vc_edit_settings_reader::get(bool) not yet implemented");
}

} // namespace vc::edit
