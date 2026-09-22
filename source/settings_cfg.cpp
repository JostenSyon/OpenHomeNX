#include "settings_cfg.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace Settings {
namespace {

std::string g_base;
bool g_inited = false;
std::unordered_map<std::string, std::string> g_kv;

std::string path() { return g_base + "settings.cfg"; }

void save() {
    std::ofstream o(path(), std::ios::trunc);
    if (!o.good()) return;
    for (const auto& kv : g_kv) o << kv.first << "=" << kv.second << "\n";
}

int getInt(const char* k, int def) {
    auto it = g_kv.find(k);
    if (it == g_kv.end() || it->second.empty()) return def;
    return std::atoi(it->second.c_str());
}
void setInt(const char* k, int v) {
    g_kv[k] = std::to_string(v);
    save();
}

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}
std::string readLine(const std::string& p) {
    std::ifstream f(p);
    std::string s;
    if (f.good() && std::getline(f, s)) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
        return s;
    }
    return "";
}
int readByte(const std::string& p, int def) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return def;
    uint8_t v = (uint8_t)def;
    if (std::fread(&v, 1, 1, f) != 1) v = (uint8_t)def;
    std::fclose(f);
    return v;
}

// Importa un legacy file in chiave key (solo se settings.cfg non esisteva).
// convert: 0 = esistenza->"1", 1 = byte grezzo, 2 = prima riga testo.
// Non cancella subito il file: lo aggiunge a toRemove, cancellato solo a
// migrazione completa e settings.cfg scritto (vedi migrate()).
void importLegacy(const std::string& file, const char* key, int convert,
                   std::vector<std::string>& toRemove) {
    std::string p = g_base + file;
    if (!fileExists(p)) return;
    if (convert == 0) {
        g_kv[key] = "1";
    } else if (convert == 1) {
        g_kv[key] = std::to_string(readByte(p, 0));
    } else {
        std::string v = readLine(p);
        if (!v.empty() || std::string(key) == "defaultuser") g_kv[key] = v;
    }
    toRemove.push_back(p);
}

void migrate() {
    std::vector<std::string> toRemove;
    importLegacy("theme.cfg", "theme", 1, toRemove);
    importLegacy("zoom.cfg", "zoom", 1, toRemove);
    importLegacy("gallery.cfg", "gallery_layout", 1, toRemove);
    importLegacy("crypto.cfg", "crypto", 1, toRemove);
    importLegacy("autocheck_usb.cfg", "autocheck_usb", 1, toRemove);
    importLegacy("launcher_prompt_seen.cfg", "launcher_seen", 0, toRemove);
    importLegacy("noled.cfg", "noled", 0, toRemove);
    importLegacy("language.txt", "language", 2, toRemove);
    importLegacy("defaultuser.txt", "defaultuser", 2, toRemove);
    importLegacy("quickmenu.txt", "quickmenu", 2, toRemove); // "1" o assente
    // quickmenu.txt contiene "1" quando on: normalizza a 0/1
    auto it = g_kv.find("quickmenu");
    if (it != g_kv.end()) it->second = (it->second == "1") ? "1" : "0";
    save();
    // Cancella i legacy SOLO dopo che settings.cfg e' stato scritto: se si
    // perde corrente a meta' migrazione, al prossimo boot i file non ancora
    // cancellati vengono semplicemente re-importati (idempotente) invece di
    // sparire in silenzio prima che il loro valore sia al sicuro altrove.
    for (const auto& p : toRemove) std::remove(p.c_str());
}

void parse() {
    g_kv.clear();
    std::ifstream f(path());
    if (!f.good()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

} // namespace

void init(const std::string& basePath) {
    if (g_inited && g_base == basePath) return;
    g_base = basePath;
    g_inited = true;
    g_kv.clear();
    if (!fileExists(path())) {
        migrate(); // importa legacy + scrive settings.cfg
    } else {
        parse();
    }
}

int themeIndex() { return getInt("theme", 0); }
void setThemeIndex(int v) { setInt("theme", v); }
int zoomGrow() { return getInt("zoom", 12); }
void setZoomGrow(int v) { setInt("zoom", v); }
int galleryLayout() { return getInt("gallery_layout", 1); }
void setGalleryLayout(int v) { setInt("gallery_layout", v); }
int cryptoEngine() { return getInt("crypto", 1); }
void setCryptoEngine(int v) { setInt("crypto", v); }
bool autoCheckUsb() { return getInt("autocheck_usb", 0) != 0; }
void setAutoCheckUsb(bool v) { setInt("autocheck_usb", v ? 1 : 0); }
bool launcherSeen() { return getInt("launcher_seen", 0) != 0; }
void setLauncherSeen() { setInt("launcher_seen", 1); }
bool noLed() { return getInt("noled", 0) != 0; }
std::string language() {
    auto it = g_kv.find("language");
    return (it == g_kv.end()) ? "" : it->second; // "" = mai scelta -> sistema
}
void setLanguage(const std::string& v) {
    g_kv["language"] = v;
    save();
}
std::string defaultUser() {
    auto it = g_kv.find("defaultuser");
    return (it == g_kv.end()) ? "" : it->second;
}
void setDefaultUser(const std::string& v) {
    g_kv["defaultuser"] = v;
    save();
}
bool quickMenu() { return getInt("quickmenu", 0) != 0; }
void setQuickMenu(bool v) { setInt("quickmenu", v ? 1 : 0); }
bool radialMenu() { return getInt("radial_menu", 0) != 0; }
void setRadialMenu(bool v) { setInt("radial_menu", v ? 1 : 0); }
bool tradeAnim() { return getInt("trade_anim", 1) != 0; }
void setTradeAnim(bool v) { setInt("trade_anim", v ? 1 : 0); }
std::string dockOrder() {
    auto it = g_kv.find("dock_order");
    return (it == g_kv.end()) ? "Backpack,Banks,SaveMenu,Trade,Eject" : it->second;
}
void setDockOrder(const std::string& v) {
    g_kv["dock_order"] = v;
    save();
}
bool dockVisible() { return getInt("dock_visible", 1) != 0; }
void setDockVisible(bool v) { setInt("dock_visible", v ? 1 : 0); }
int boxartStyle() {
    int v = getInt("boxart_style", 0);
    if (v < 0 || v > 2) v = 0;
    return v;
}
void setBoxartStyle(int v) {
    if (v < 0) v = 0;
    if (v > 2) v = 2;
    setInt("boxart_style", v);
}
bool showRomsWithoutSave() { return getInt("show_roms_without_save", 0) != 0; }
void setShowRomsWithoutSave(bool v) { setInt("show_roms_without_save", v ? 1 : 0); }
std::string remoteSyncHost() {
    auto it = g_kv.find("remotesync_host");
    return (it == g_kv.end()) ? "" : it->second;
}
void setRemoteSyncHost(const std::string& v) {
    g_kv["remotesync_host"] = v;
    save();
}
std::string remoteSyncUser() {
    auto it = g_kv.find("remotesync_user");
    return (it == g_kv.end()) ? "ark" : it->second;
}
void setRemoteSyncUser(const std::string& v) {
    g_kv["remotesync_user"] = v;
    save();
}
std::string remoteSyncPass() {
    auto it = g_kv.find("remotesync_pass");
    return (it == g_kv.end()) ? "ark" : it->second;
}
void setRemoteSyncPass(const std::string& v) {
    g_kv["remotesync_pass"] = v;
    save();
}

} // namespace Settings
