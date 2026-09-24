#pragma once
#include <string>
#include <cctype>

// Unifica i 6+ toLower* sparsi (boxart.cpp, import_scan.cpp, update_net.cpp,
// remote_sync.cpp, game_type.h, ui_input.cpp). Unico punto per normalizzare.
inline std::string toLowerCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Alias storici per minimizzare churn nei caller esistenti.
inline std::string toLower(std::string s) { return toLowerCopy(std::move(s)); }
inline std::string toLowerStr(std::string s) { return toLowerCopy(std::move(s)); }
inline std::string toLowerRS(const std::string& s) { return toLowerCopy(s); }
