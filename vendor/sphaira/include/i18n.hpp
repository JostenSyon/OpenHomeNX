// SHIM OpenHomeNX (non Sphaira): operatore _i18n passthrough (le stringhe
// forwarder restano in inglese come in Sphaira; la UI nostra usa i18n proprio).
#pragma once

#include <string>

namespace sphaira {

inline std::string operator""_i18n(const char* s, std::size_t n) {
    return std::string(s, n);
}

} // namespace sphaira
