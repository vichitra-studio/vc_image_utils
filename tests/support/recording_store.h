// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// A PURE test spy — it exists only to let test_vc_param_schema.cpp exercise
// vc::params::save/load against a minimal, hand-rolled sink, and has no
// runtime purpose. Test doubles never ship in library code, so it lives
// here under tests/support/, NOT in include/ or src/ — unlike
// vc_memory_table / vc_memory_image_meta, which stay in the library
// because they are legitimate runtime backends (ephemeral/unsaved sessions
// can genuinely use an in-memory store), this type is never constructed by
// anything but a test.

#include <string>
#include <unordered_map>
#include <variant>

namespace vc::test_support {

// A recording sink that mirrors vc_edit_settings_writer/vc_edit_settings_reader's EXACT overload
// set (double / int / bool) — deliberately NOT a permissive `set(name, T)`
// template. This makes the framework test compile against the SAME limit as
// the real edit-settings adapter, so a param type the adapter can't
// serialize fails HERE too, instead of silently passing against a generic
// sink and hiding the gap.
struct recording_store {
    std::unordered_map<std::string, std::variant<double, int, bool>> kv;

    void set(const std::string& key, double value) {
        kv[key] = value;
    }
    void set(const std::string& key, int value) {
        kv[key] = value;
    }
    void set(const std::string& key, bool value) {
        kv[key] = value;
    }

    double get(const std::string& key, double fallback) const {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::get<double>(it->second);
    }
    int get(const std::string& key, int fallback) const {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::get<int>(it->second);
    }
    bool get(const std::string& key, bool fallback) const {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::get<bool>(it->second);
    }
};

} // namespace vc::test_support
