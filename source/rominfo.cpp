#include "rominfo.h"
#include "debug_log.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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
        if (c == '_') c = ' ';
        s += (char)toupper(c);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
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

// Titolo GB/GBC normalizzato -> gioco (SOLO gioco, mai lingua: i titoli
// localizzati ingannano, es. Rossa italiana ha titolo "POKEMON RED").
// La lingua viene SEMPRE dal filename (o dalla lettera regione su GBA).
struct TitleEntry { const char* title; const char* game; };
static const TitleEntry kGbTitles[] = {
    {"POKEMON RED", "red"}, {"POKEMON BLUE", "blue"},
    {"POKEMON YELLOW", "yellow"}, {"POKEMON GOLD", "gold"},
    {"POKEMON SILVER", "silver"}, {"POKEMON CRYSTAL", "crystal"},
    {"POKEMON ROSSA", "red"}, {"POKEMON BLU", "blue"},
    {"POKEMON GIALLA", "yellow"}, {"POKEMON ORO", "gold"},
    {"POKEMON ARGENTO", "silver"}, {"POKEMON CRISTALLO", "crystal"},
    {"POKEMON ROJA", "red"}, {"POKEMON AZUL", "blue"},
    {"POKEMON AMARILLO", "yellow"}, {"POKEMON PLATA", "silver"},
    {"POKEMON CRISTAL", "crystal"},
    {"POKEMON ROUGE", "red"}, {"POKEMON BLEU", "blue"},
    {"POKEMON JAUNE", "yellow"}, {"POKEMON OR", "gold"},
    {"POKEMON ARGENT", "silver"},
    {"POKEMON ROT", "red"}, {"POKEMON BLAU", "blue"},
    {"POKEMON GELB", "yellow"}, {"POKEMON KRISTALL", "crystal"},
    // Abbreviati reali visti su dump (titoli troncati/manufacturer):
    {"POKEMON YEL", "yellow"}, {"POKEMON YELPSI", "yellow"},
    {"POKEMON GLD", "gold"}, {"POKEMON SLV", "silver"},
    {"PM CRYSTAL", "crystal"},
};

// Lingua dal filename: token interi, vincitore unico con score>=2,
// altrimenti ignota (mai indovinare: meglio saltare il rename).
// Es. "Pokemon Versione Rossa" -> it:2 (versione, rossa).
// "crystal" da solo -> en:1 -> sotto soglia -> ignota (sicuro).
bool langFromFilename(const std::string& filename, std::string& out) {
    static const struct { const char* lang; const char* toks[24]; } k[] = {
        {"it", {"versione", "rosso", "rossa", "fuoco", "verde", "foglia",
                "rubino", "zaffiro", "smeraldo", "blu", "gialla", "oro",
                "argento", "cristallo", "italy", "italia", "italiano", 0}},
        {"en", {"version", "red", "blue", "yellow", "gold", "silver",
                "crystal", "fire", "leaf", "ruby", "sapphire", "emerald",
                "green", "usa", "europe", "english", 0}},
        {"es", {"edicion", "roja", "rojo", "azul", "amarilla", "amarillo",
                "oro", "plata", "cristal", "fuego", "hoja", "rubi",
                "zafiro", "esmeralda", "verde", "spain", "espana", 0}},
        {"fr", {"version", "rouge", "bleue", "jaune", "or", "argent",
                "cristal", "feu", "feuille", "rubis", "saphir", "emeraude",
                "verte", "france", "francais", 0}},
        {"de", {"edition", "rot", "blau", "gelb", "gold", "silber",
                "kristall", "feuer", "blatt", "grun", "smaragd", "saphir",
                "rubin", "germany", "deutschland", 0}},
        {"ja", {"japan", "japanese", "ja", "jp", 0, 0, 0, 0}},
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
    std::string best;
    int bestN = 0;
    bool tie = false;
    for (const auto& e : k) {
        int n = 0;
        for (int i = 0; e.toks[i]; i++)
            for (const auto& w : toks)
                if (w == e.toks[i]) { n++; break; }
        if (n > bestN) { bestN = n; best = e.lang; tie = false; }
        else if (n == bestN && n > 0) { tie = true; }
    }
    if (!tie && bestN >= 2) { out = best; return true; }
    return false;
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
            else if (pre == "BPL" || pre == "BPG") game = "leafgreen"; // BPG = LeafGreen IT
            else if (pre == "AXV") game = "ruby";
            else if (pre == "AXP") game = "sapphire";
            std::string lang, region;
            bool rok = regionOf(code[3], lang, region);
            if (!rok)
                rok = langFromFilename(fileNameOf(path), lang); // lettera ignota: prova filename
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
            out.valid = true;
            out.kind = cgb ? "gbc" : "gb";
            // Prova titolo 11-char e (CGB) variante 15-char 0x134-0x142:
            // alcuni dump reali hanno il titolo esteso (es. "POKEMON YELPSI").
            std::string game;
            {
                std::string t11 = upperTrim(h + 0x134, 11);
                std::string t15 = upperTrim(h + 0x134, 15);
                for (const auto& e : kGbTitles) {
                    if (t11 == e.title || (cgb && t15 == e.title)) {
                        game = e.game;
                        break;
                    }
                }
                if (game.empty()) {
                    char hex[64] = {0};
                    for (int i = 0; i < 16 && got > (size_t)(0x134 + i); i++)
                        snprintf(hex + i * 3, 4, "%02X ", h[0x134 + i]);
                    DebugLog::line("rominfo: titolo GB sconosciuto '%s' [%s]",
                                   t11.c_str(), hex);
                }
            }
            out.game = game;
            // Lingua MAI dal titolo (Rossa IT ha titolo inglese!): solo filename,
            // con soglia per non indovinare (es. "crystal" da solo -> ignota).
            if (!game.empty()) {
                std::string lang;
                if (langFromFilename(fileNameOf(path), lang)) {
                    out.lang = lang;
                } else {
                    DebugLog::line("rominfo: %s lingua ignota, salto rename",
                                   game.c_str());
                }
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
    if (canon.empty()) {
        // game o lingua non determinabili (es. titolo ambiguo senza regione
        // ne' filename chiaro): mai rinominare alla cieca, l'utente lo fa a mano.
        DebugLog::line("rominfo: skip %s (game='%s' lang='%s' incerti)",
                       romPath.c_str(), info.game.c_str(), info.lang.c_str());
        return false;
    }
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

std::vector<std::string> scanRoms(const std::vector<std::string>& dirs) {
    static const char* kExt[] = {".gba", ".gbc", ".gb", ".nds"};
    std::vector<std::string> out;
    for (const auto& dir : dirs) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string low = name;
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            bool rom = false;
            for (const char* x : kExt) {
                std::string xs = x;
                if (low.size() > xs.size() &&
                    low.compare(low.size() - xs.size(), xs.size(), xs) == 0) {
                    rom = true;
                    break;
                }
            }
            if (!rom) continue;
            std::string full = dir;
            if (!full.empty() && full.back() != '/') full += '/';
            full += name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            out.push_back(full);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    DebugLog::line("rominfo: scanRoms %zu dir -> %zu ROM", dirs.size(), out.size());
    return out;
}

} // namespace RomInfo
