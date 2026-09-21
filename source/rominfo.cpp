#include "rominfo.h"
#include "debug_log.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace RomInfo {
namespace {

bool readHead(const std::string& path, unsigned char* buf, size_t n, size_t& got) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    f.read((char*)buf, n);
    got = (size_t)f.gcount();
    return got > 0;
}

std::string upperTrim(const unsigned char* d, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = d[i];
        if (c == 0) break;
        s += (char)toupper(c);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '_')) s.pop_back();
    size_t a = s.find_first_not_of(" ");
    if (a != std::string::npos && a > 0) s = s.substr(a);
    return s;
}

bool printable4(const unsigned char* d) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = d[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

// Lettera regione Nintendo standard (GBA/NDS, 4o char game code).
bool regionOf(char r, std::string& lang, std::string& region) {
    switch (r) {
        case 'E': lang = "en"; region = "USA"; return true;
        case 'J': lang = "ja"; region = "Japan"; return true;
        case 'P': lang = "en"; region = "Europe"; return true;
        case 'D': lang = "de"; region = "Germany"; return true;
        case 'F': lang = "fr"; region = "France"; return true;
        case 'S': lang = "es"; region = "Spain"; return true;
        case 'I': lang = "it"; region = "Italy"; return true;
        default: return false;
    }
}

// Titolo GB/GBC normalizzato -> candidati (game, lang). Piu' candidati =
// ambiguo (es. "POKEMON ORO" vale it+es): si scioglie col filename.
struct TitleEntry { const char* title; const char* game; const char* lang; };
static const TitleEntry kGbTitles[] = {
    {"POKEMON RED", "red", "en"}, {"POKEMON BLUE", "blue", "en"},
    {"POKEMON YELLOW", "yellow", "en"}, {"POKEMON GOLD", "gold", "en"},
    {"POKEMON SILVER", "silver", "en"}, {"POKEMON CRYSTAL", "crystal", "en"},
    {"POKEMON ROSSA", "red", "it"}, {"POKEMON BLU", "blue", "it"},
    {"POKEMON GIALLA", "yellow", "it"}, {"POKEMON ORO", "gold", "it"},
    {"POKEMON ARGENTO", "silver", "it"}, {"POKEMON CRISTALLO", "crystal", "it"},
    {"POKEMON ROJA", "red", "es"}, {"POKEMON AZUL", "blue", "es"},
    {"POKEMON AMARILLO", "yellow", "es"}, {"POKEMON ORO", "gold", "es"},
    {"POKEMON PLATA", "silver", "es"}, {"POKEMON CRISTAL", "crystal", "es"},
    {"POKEMON ROUGE", "red", "fr"}, {"POKEMON BLEU", "blue", "fr"},
    {"POKEMON JAUNE", "yellow", "fr"}, {"POKEMON OR", "gold", "fr"},
    {"POKEMON ARGENT", "silver", "fr"}, {"POKEMON CRISTAL", "crystal", "fr"},
    {"POKEMON ROT", "red", "de"}, {"POKEMON BLAU", "blue", "de"},
    {"POKEMON GELB", "yellow", "de"}, {"POKEMON KRISTALL", "crystal", "de"},
};

// Token lingua dal filename (tiebreak ambiguiti').
int langHint(const std::string& filename, const std::string& lang) {
    static const struct { const char* lang; const char* toks[8]; } k[] = {
        {"it", {"it", "ita", "italy", "italiano", "italia", 0, 0, 0}},
        {"es", {"es", "esp", "spa", "spain", "espana", 0, 0, 0}},
        {"fr", {"fr", "fra", "france", "francais", 0, 0, 0, 0}},
        {"de", {"de", "ger", "germany", "deutsch", 0, 0, 0, 0}},
        {"en", {"en", "eng", "usa", "us", "europe", "uk", "english", 0}},
        {"ja", {"ja", "jp", "jpn", "japan", "japanese", 0, 0, 0}},
    };
    std::string low;
    for (char c : filename) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            low += (char)tolower((unsigned char)c);
        else
            low += ' ';
    }
    std::vector<std::string> toks;
    std::istringstream ss(low);
    std::string t;
    while (ss >> t) toks.push_back(t);
    for (const auto& e : k) {
        if (lang != e.lang) continue;
        int n = 0;
        for (int i = 0; e.toks[i]; i++)
            for (const auto& w : toks)
                if (w == e.toks[i]) n++;
        return n;
    }
    return 0;
}

struct CanonEntry { const char* game; const char* lang; const char* name; };
static const CanonEntry kCanon[] = {
    {"emerald", "en", "Pokemon - Emerald Version (USA, Europe)"},
    {"emerald", "it", "Pokemon - Versione Smeraldo (Italy)"},
    {"emerald", "es", "Pokemon - Edicion Esmeralda (Spain)"},
    {"emerald", "fr", "Pokemon - Version Emeraude (France)"},
    {"emerald", "de", "Pokemon - Smaragd-Edition (Germany)"},
    {"ruby", "en", "Pokemon - Ruby Version (USA, Europe)"},
    {"ruby", "it", "Pokemon - Versione Rubino (Italy)"},
    {"ruby", "es", "Pokemon - Edicion Rubi (Spain)"},
    {"ruby", "fr", "Pokemon - Version Rubis (France)"},
    {"ruby", "de", "Pokemon - Rubin-Edition (Germany)"},
    {"sapphire", "en", "Pokemon - Sapphire Version (USA, Europe)"},
    {"sapphire", "it", "Pokemon - Versione Zaffiro (Italy)"},
    {"sapphire", "es", "Pokemon - Edicion Zafiro (Spain)"},
    {"sapphire", "fr", "Pokemon - Version Saphir (France)"},
    {"sapphire", "de", "Pokemon - Saphir-Edition (Germany)"},
    {"firered", "en", "Pokemon - FireRed Version (USA, Europe)"},
    {"firered", "it", "Pokemon - Versione Rosso Fuoco (Italy)"},
    {"firered", "es", "Pokemon - Edicion Rojo Fuego (Spain)"},
    {"firered", "fr", "Pokemon - Version Rouge Feu (France)"},
    {"firered", "de", "Pokemon - Feuerrote Edition (Germany)"},
    {"leafgreen", "en", "Pokemon - LeafGreen Version (USA, Europe)"},
    {"leafgreen", "it", "Pokemon - Versione Verde Foglia (Italy)"},
    {"leafgreen", "es", "Pokemon - Edicion Verde Hoja (Spain)"},
    {"leafgreen", "fr", "Pokemon - Version Vert Feuille (France)"},
    {"leafgreen", "de", "Pokemon - Blattgrune Edition (Germany)"},
    {"red", "en", "Pokemon - Red Version (USA, Europe)"},
    {"red", "it", "Pokemon - Versione Rossa (Italy)"},
    {"red", "es", "Pokemon - Edicion Roja (Spain)"},
    {"red", "fr", "Pokemon - Version Rouge (France)"},
    {"red", "de", "Pokemon - Rote Edition (Germany)"},
    {"blue", "en", "Pokemon - Blue Version (USA, Europe)"},
    {"blue", "it", "Pokemon - Versione Blu (Italy)"},
    {"blue", "es", "Pokemon - Edicion Azul (Spain)"},
    {"blue", "fr", "Pokemon - Version Bleue (France)"},
    {"blue", "de", "Pokemon - Blaue Edition (Germany)"},
    {"yellow", "en", "Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe)"},
    {"yellow", "it", "Pokemon - Versione Gialla - Speciale Edizione Pikachu (Italy)"},
    {"yellow", "es", "Pokemon - Edicion Amarilla - Edicion Especial Pikachu (Spain)"},
    {"yellow", "fr", "Pokemon - Version Jaune - Edition Speciale Pikachu (France)"},
    {"yellow", "de", "Pokemon - Gelbe Edition - Special Pikachu Edition (Germany)"},
    {"gold", "en", "Pokemon - Gold Version (USA, Europe)"},
    {"gold", "it", "Pokemon - Versione Oro (Italy)"},
    {"gold", "es", "Pokemon - Edicion Oro (Spain)"},
    {"gold", "fr", "Pokemon - Version Or (France)"},
    {"gold", "de", "Pokemon - Goldene Edition (Germany)"},
    {"silver", "en", "Pokemon - Silver Version (USA, Europe)"},
    {"silver", "it", "Pokemon - Versione Argento (Italy)"},
    {"silver", "es", "Pokemon - Edicion Plata (Spain)"},
    {"silver", "fr", "Pokemon - Version Argent (France)"},
    {"silver", "de", "Pokemon - Silberne Edition (Germany)"},
    {"crystal", "en", "Pokemon - Crystal Version (USA, Europe)"},
    {"crystal", "it", "Pokemon - Versione Cristallo (Italy)"},
    {"crystal", "es", "Pokemon - Edicion Cristal (Spain)"},
    {"crystal", "fr", "Pokemon - Version Cristal (France)"},
    {"crystal", "de", "Pokemon - Kristall-Edition (Germany)"},
};

std::string fileNameOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

} // namespace

bool detect(const std::string& path, Info& out) {
    out = Info();
    unsigned char h[0x1000];
    size_t got = 0;
    if (!readHead(path, h, sizeof(h), got) || got < 0x200) return false;

    // --- GBA: complement check 0xA0-0xBD ---
    {
        unsigned chk = 0;
        for (size_t i = 0xA0; i <= 0xBC; i++) chk = (chk - h[i]) & 0xFF;
        chk = (chk - 0x19) & 0xFF;
        if (chk == h[0xBD] && printable4(h + 0xAC)) {
            std::string code((char*)h + 0xAC, 4);
            std::string pre = code.substr(0, 3);
            std::string game;
            if (pre == "BPE") game = "emerald";
            else if (pre == "BPR") game = "firered";
            else if (pre == "BPL") game = "leafgreen";
            else if (pre == "AXV") game = "ruby";
            else if (pre == "AXP") game = "sapphire";
            std::string lang, region;
            bool rok = regionOf(code[3], lang, region);
            out.valid = true;
            out.kind = "gba";
            out.game = game;
            if (rok) { out.lang = lang; out.region = region; }
            if (game.empty())
                DebugLog::line("rominfo: GBA code %s non mappato", code.c_str());
            return true;
        }
    }

    // --- GB/GBC: checksum 0x14D ---
    if (got > 0x14D) {
        int x = 0;
        for (size_t i = 0x134; i <= 0x14C; i++) x = x - h[i] - 1;
        if ((x & 0xFF) == h[0x14D]) {
            bool cgb = (h[0x143] == 0x80 || h[0x143] == 0xC0);
            size_t tlen = cgb ? 11 : 16;
            if (cgb && h[0x143] != 0x80 && h[0x143] != 0xC0) tlen = 16;
            std::string title = upperTrim(h + 0x134, tlen);
            std::vector<const TitleEntry*> hits;
            for (const auto& e : kGbTitles)
                if (title == e.title) hits.push_back(&e);
            out.valid = true;
            out.kind = cgb ? "gbc" : "gb";
            if (hits.size() == 1) {
                out.game = hits[0]->game;
                out.lang = hits[0]->lang;
            } else if (hits.size() > 1) {
                // ambiguo (es. ORO it/es, CRISTAL es/fr): tiebreak col filename
                std::string fn = fileNameOf(path);
                const TitleEntry* best = nullptr;
                int bestN = 0;
                for (const auto* e : hits) {
                    int n = langHint(fn, e->lang);
                    if (n > bestN) { bestN = n; best = e; }
                }
                if (best && bestN > 0) {
                    out.game = best->game;
                    out.lang = best->lang;
                } else {
                    DebugLog::line("rominfo: titolo '%s' ambiguo, lingua ignota",
                                   title.c_str());
                }
            } else {
                DebugLog::line("rominfo: titolo GB sconosciuto '%s'", title.c_str());
            }
            return true;
        }
    }

    // --- NDS: CRC16 logo 0xC0-0x15B vs 0x15C (kind solo, game non mappato) ---
    if (got > 0x160 && printable4(h + 0x0C)) {
        unsigned crc = 0xFFFF;
        for (size_t i = 0xC0; i <= 0x15B; i++) {
            crc ^= h[i];
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
        unsigned stored = h[0x15C] | ((unsigned)h[0x15D] << 8);
        if ((crc & 0xFFFF) == stored) {
            out.valid = true;
            out.kind = "nds";
            std::string lang, region;
            if (regionOf((char)h[0x0F], lang, region)) {
                out.lang = lang;
                out.region = region;
            }
            return true;
        }
    }

    return false;
}

std::string canonicalName(const Info& info) {
    if (!info.valid || info.game.empty() || info.lang.empty()) return "";
    for (const auto& e : kCanon)
        if (info.game == e.game && info.lang == e.lang) return e.name;
    return "";
}

bool planRename(const std::string& romPath, RenamePlan& out) {
    out = RenamePlan();
    Info info;
    if (!detect(romPath, info)) return false;
    std::string canon = canonicalName(info);
    if (canon.empty()) return false;
    size_t slash = romPath.find_last_of('/');
    size_t dot = romPath.find_last_of('.');
    if (dot == std::string::npos) return false;
    out.dir = (slash == std::string::npos) ? "" : romPath.substr(0, slash);
    out.oldBase = romPath.substr(slash == std::string::npos ? 0 : slash + 1, dot - (slash == std::string::npos ? 0 : slash + 1));
    out.newBase = canon;
    out.ext = romPath.substr(dot);
    if (out.oldBase == out.newBase) return false; // gia' corretto
    auto join = [&](const std::string& b, const std::string& e) {
        return (out.dir.empty() ? "" : out.dir + "/") + b + e;
    };
    // ROM sempre; associati solo se esistono (stesso basename).
    out.moves.push_back({romPath, join(canon, out.ext)});
    static const char* kAssoc[] = {".sav", ".srm", ".state", ".state.auto", ".rtc", ".bak"};
    for (const char* e : kAssoc) {
        std::string src = join(out.oldBase, e);
        std::ifstream t(src, std::ios::binary);
        if (t.good()) out.moves.push_back({src, join(canon, e)});
    }
    // Salta se la destinazione esiste gia' (mai sovrascrivere).
    for (const auto& m : out.moves) {
        std::ifstream t(m.second, std::ios::binary);
        if (t.good()) {
            DebugLog::line("rominfo: rename saltato, esiste gia' %s", m.second.c_str());
            return false;
        }
    }
    return true;
}

bool applyRename(const RenamePlan& plan, std::string& err) {
    for (const auto& m : plan.moves) {
        if (std::rename(m.first.c_str(), m.second.c_str()) != 0) {
            err = m.first;
            DebugLog::line("rominfo: rename FAIL %s", m.first.c_str());
            return false;
        }
        DebugLog::line("rominfo: rename %s -> %s", m.first.c_str(), m.second.c_str());
    }
    // Patch <path> in gamelist.xml della stessa cartella.
    if (!plan.dir.empty()) {
        std::string xmlPath = plan.dir + "/gamelist.xml";
        std::ifstream f(xmlPath);
        if (f.good()) {
            std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            bool changed = false;
            for (const char* prefix : {"./", ""}) {
                std::string needle = std::string("<path>") + prefix + plan.oldBase + plan.ext + "</path>";
                std::string repl = std::string("<path>") + prefix + plan.newBase + plan.ext + "</path>";
                size_t pos = 0;
                while ((pos = xml.find(needle, pos)) != std::string::npos) {
                    xml.replace(pos, needle.size(), repl);
                    pos += repl.size();
                    changed = true;
                }
            }
            if (changed) {
                std::ofstream o(xmlPath, std::ios::binary | std::ios::trunc);
                if (o.good()) {
                    o << xml;
                    DebugLog::line("rominfo: gamelist.xml aggiornato (%s)", plan.newBase.c_str());
                }
            }
        }
    }
    return true;
}

} // namespace RomInfo
