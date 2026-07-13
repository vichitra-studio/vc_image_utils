// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include "vc/utils/vc_log.h"

// TODO(you): you will need <iostream> once write()/write_temp() below
// actually write to std::cerr.
// #include <iostream>

namespace vc::utils::log {

namespace {

// Process-wide state, owned entirely by this translation unit — never
// exposed directly, only through the setters/getters below. Single-threaded
// assumption: configure before logging starts on another thread.
bool g_enabled = true;
level g_min_level = level::info;

// TODO(you): implement — the two non-template primitives every public
// logging function below funnels into once it has a real message in hand.
// Never declared in vc_log.h: nothing outside this file needs to call
// these directly, and since debug()/info()/etc. below are no longer
// templates, there's no compile-time reason to expose them either.
//   1. Map `lvl` to a short label ("DEBUG"/"INFO"/"WARNING"/"ERROR").
//   2. std::cerr << "[" << label << "][" << tag << "] " << msg << "\n";
void write(level lvl,
           const vc::utils::string& tag,
           const vc::utils::message& msg) {
    (void)lvl;
    (void)tag;
    (void)msg;
}

// TODO(you): same shape as write() above, fixed "[TEMP]" label, no level
// involved.
void write_temp(const vc::utils::string& tag, const vc::utils::message& msg) {
    (void)tag;
    (void)msg;
}

} // namespace

void set_enabled(bool on) {
    // TODO(you): g_enabled = on;
    (void)on;
}

bool enabled() noexcept {
    // TODO(you): return g_enabled;
    return false;
}

void set_min_level(level lvl) {
    // TODO(you): g_min_level = lvl;
    (void)lvl;
}

level min_level() noexcept {
    // TODO(you): return g_min_level;
    return level::info;
}

bool should_log(level lvl) noexcept {
    // TODO(you): return enabled() && lvl >= min_level();
    (void)lvl;
    return false;
}

void debug(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg) {
    // TODO(you): if (!msg || !should_log(level::debug)) return;
    //            write(level::debug, tag, *msg);
    (void)tag;
    (void)msg;
}

void info(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg) {
    // TODO(you): same shape as debug() above, level::info.
    (void)tag;
    (void)msg;
}

void warning(const vc::utils::string& tag,
             const std::optional<vc::utils::message>& msg) {
    // TODO(you): same shape as debug() above, level::warning.
    (void)tag;
    (void)msg;
}

void error(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg) {
    // TODO(you): same shape as debug() above, level::error.
    (void)tag;
    (void)msg;
}

void temp(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg) {
    // TODO(you): if (!msg || !enabled()) return;
    //            write_temp(tag, *msg);
    (void)tag;
    (void)msg;
}

} // namespace vc::utils::log
