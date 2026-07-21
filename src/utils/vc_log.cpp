// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <iostream>
#include <string>

#include "vc/utils/vc_log.h"

namespace vc::utils::log {

namespace {

// Single-threaded assumption: configure before logging starts on another
// thread.
bool g_enabled = true;

#ifdef NDEBUG
level g_min_level = level::warning;
#else
level g_min_level = level::debug;
#endif

std::string level_to_string(level lvl) {
    switch (lvl) {
    case level::debug:
        return "DEBUG";
    case level::info:
        return "INFO";
    case level::warning:
        return "WARNING";
    case level::error:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void write(level lvl,
           const vc::utils::string& tag,
           const vc::utils::message& msg) {
    std::cerr << "[" << level_to_string(lvl) << "][" << tag << "] " << msg
              << "\n";
}

void write_temp(const vc::utils::string& tag, const vc::utils::message& msg) {
    std::cerr << "[TEMP][" << tag << "] " << msg << "\n";
}

} // namespace

void set_enabled(bool on) {
    g_enabled = on;
}

bool enabled() noexcept {
    return g_enabled;
}

void set_min_level(level lvl) {
    g_min_level = lvl;
}

level min_level() noexcept {
    return g_min_level;
}

bool should_log(level lvl) noexcept {
    return enabled() && lvl >= min_level();
}

void debug(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg) {
    if (!msg || !should_log(level::debug))
        return;
    write(level::debug, tag, *msg);
}

void info(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg) {
    if (!msg || !should_log(level::info))
        return;
    write(level::info, tag, *msg);
}

void warning(const vc::utils::string& tag,
             const std::optional<vc::utils::message>& msg) {
    if (!msg || !should_log(level::warning))
        return;
    write(level::warning, tag, *msg);
}

void error(const vc::utils::string& tag,
           const std::optional<vc::utils::message>& msg) {
    if (!msg || !should_log(level::error))
        return;
    write(level::error, tag, *msg);
}

void temp(const vc::utils::string& tag,
          const std::optional<vc::utils::message>& msg) {
    if (!msg || !enabled())
        return;
    write_temp(tag, *msg);
}

} // namespace vc::utils::log
