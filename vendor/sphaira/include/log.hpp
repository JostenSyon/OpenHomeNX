// SHIM OpenHomeNX (non Sphaira): log_write() usato da keys.cpp, rotta su DebugLog.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include "debug_log.h"

namespace sphaira {

static inline void log_write(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // via via i \n finali di Sphaira (il nostro logger aggiunge timestamp)
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    DebugLog::line("%s", s.c_str());
}

} // namespace sphaira
