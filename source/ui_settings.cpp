#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "nro_version.h"
#include "app_version.h"
#include "update_net.h"
#include "remote_sync.h"
#include "forwarder.h"
#include "settings_cfg.h"
#include "emulator.h"
#include "trade_evo.h"
#include "boxart.h"
#include "job.h"
#include "rominfo.h"
#include "pokedex.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif


// removeRecursive(): definita in ui_backups.cpp (usata anche da openSettings()), non piu' static.
bool removeRecursive(const std::string& p);
// updateCfgPaths/fileHasKey/updateCfgHome/parseUpdateCfgFile: definite in ui_update.cpp
// (usate anche qui per le righe update.cfg custom in Sviluppatore), non piu' static.
// UpdateCfg (il tipo) e' in include/ui.h, condivisa.
void updateCfgPaths(const std::string& basePath, std::string& cfg, std::string& off);
bool fileHasKey(const std::string& path, const std::string& want);
std::string updateCfgHome(const std::string& basePath);
void parseUpdateCfgFile(const std::string& path, UpdateCfg& out, bool channelOnly);
bool readUpdateCfg(const std::string& basePath, UpdateCfg& out);
// entryDiskSize: definita in ui_backups.cpp (usata anche da openSettings() qui), non piu' static.
uint64_t entryDiskSize(const std::string& p);
// readQuickMenu: definita piu' sotto in questo stesso file, ma usata anche prima (settingsRowValue).
bool readQuickMenu(const std::string& basePath);

static std::string normalizeRowLabel() { return i18n::get(StrKey::SetNormalizeSave); }
static std::string boxartStyleLabel(int v) {
    if (v == 1) return i18n::get(StrKey::ScraperBoxartStyle2d);
    if (v == 2) return i18n::get(StrKey::ScraperBoxartStyle3d);
    return i18n::get(StrKey::ScraperBoxartStyleLocale);
}

static std::string readDefaultUser(const std::string& basePath);
int UI::defaultUserIndex() const {
    std::string want = readDefaultUser(basePath_);
    if (want.empty()) return -1;
    const auto& users = account_.profiles();
    for (int i = 0; i < (int)users.size(); i++)
        if (users[i].nickname == want) return i;
    return -1;
}

static std::vector<DevRow> devRowList(bool debugOn, bool sendOn);
void UI::openSettings() {
    // Se si apre Impostazioni da dentro il Box Remoto (es. dal gear, non
    // dalla B sulla griglia) senza chiuderlo prima, importedGames_/
    // availableGames_ restano scambiati con la lista remota (1-2 save) per
    // il resto della sessione -- "Aggiorna BoxArt" vede solo quei pochi
    // giochi invece di tutti gli import locali (bug reale, causa di
    // "trova 0 copertine" con un box remoto aperto e mai chiuso). L'unico
    // altro punto che ripristina lo stato locale era il B sulla griglia
    // (ui_selectors.cpp) -- stessa guardia qui, unico punto d'ingresso.
    if (remoteBoxActive_) closeRemoteBox();
    showSettings_ = true;
    setCat_ = 0;
    setRow_ = 0;
    setFocusLeft_ = true;
    // Tendine: niente animazione fantasma alla prima apertura -- le molle
    // partono solo se qualcosa cambia mentre le impostazioni sono aperte.
    appearanceAnim_.reset((gameSelectorLayout_ == GameSelectorLayout::Gallery) ? 1.0f : 0.0f);
    emuAnim_.reset(mgbaPath_.empty() ? 1.0f : 0.0f);
    modUrlAnim_.reset(sendAvailable() ? 0.0f : 1.0f);
    devDrawList_ = devRowList(DebugLog::enabled(), sendAvailable());
    devGhosts_.clear();
    devExpanding_ = false;
    devAnim_.reset(0.0f);
    showGameSelMenu_ = false;
    if (langList_.empty()) langList_ = i18n::availableLangs();
    // Rilevamento emulatore (v1: solo mGBA): idempotente, gia' fatto al
    // boot (vedi ui.cpp) -- qui e' solo un fallback difensivo.
    ensureMgbaChecked();
    markDirty();
}

// Potatura cumulativa per pool (titoli o SD): dal piu vecchio finche il
// totale supera il tetto unico. Mai i manuali, mai sotto 1 voce.
uint64_t UI::prunePoolToCap(bool fileBacked) {
    long mb = backupCapMb(fileBacked);
    if (mb <= 0) return 0;
    uint64_t cap = (uint64_t)mb * 1024 * 1024;
    std::vector<std::string> all;
    for (GameType g : availableGames_) {
        bool title = titleIdOf(g) >= 0x0100000000010000ULL;
        if (title == !fileBacked) {
            auto e = autoBackupEntries(g);
            all.insert(all.end(), e.begin(), e.end());
        }
    }
    std::sort(all.begin(), all.end()); // nomi timestamp: oldest first
    uint64_t total = 0;
    for (auto& e : all) total += entryDiskSize(e);
    uint64_t freed = 0;
    while (total > cap && all.size() > 1) {
        std::string oldest = all.front();
        all.erase(all.begin());
        uint64_t sz = entryDiskSize(oldest);
        if (removeRecursive(oldest)) {
            total -= (sz < total) ? sz : total;
            freed += sz;
            DebugLog::line("backup prune pool: %s (-%llu B)", oldest.c_str(), (unsigned long long)sz);
        } else {
            DebugLog::line("backup prune pool FAILED: %s", oldest.c_str());
            break;
        }
    }
    return freed;
}

bool UI::sendAvailable() const {
    if (!DebugLog::enabled()) return false;
    UpdateCfg cfg;
    readUpdateCfg(basePath_, cfg);
    return !cfg.url.empty();
}

// update.cfg (attivo) o update.cfg.off (spento): basta che esista uno dei
// due (in basePath_ o nel percorso fisso) per mostrare toggle ed edit.
bool UI::hasCustomUrlFile(const std::string& basePath) {
    const std::string dirs[] = { basePath, "sdmc:/switch/OpenHomeNX/" };
    for (auto& d : dirs) {
        struct stat st;
        if (stat((d + "update.cfg").c_str(), &st) == 0) return true;
        if (stat((d + "update.cfg.off").c_str(), &st) == 0) return true;
    }
    return false;
}

// Trova update.cfg attivo (suo path) ed eventuale .off. "" se assenti.
void UI::findUpdateCfgFiles(const std::string& basePath, std::string& cfg, std::string& off) {
    updateCfgPaths(basePath, cfg, off); // unica implementazione (vedi sopra)
}

// URL custom da update.cfg o .off (per precompilare l'edit).
std::string UI::customUrlAny(const std::string& basePath) {
    std::string cfg, off;
    findUpdateCfgFiles(basePath, cfg, off);
    for (auto& p : {cfg, off}) {
        if (p.empty()) continue;
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("url=", 0) == 0) return line.substr(4);
        }
    }
    return "";
}

// Scrive key= a path esplicito preservando le altre righe.
static bool setKeyInFile(const std::string& dst, const std::string& key,
                         const std::string& value) {
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    {
        // Lo stream di lettura va CHIUSO prima di aprire in scrittura:
        // su FatFs tenere entrambi aperti sullo stesso file esistente
        // fa fallire l'open in truncate (bug 2026-09-12: 46 scritture
        // .off fallite, solo creazioni riuscite).
        std::ifstream f(dst);
        if (f.good()) {
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto eq = line.find('=');
                std::string k = (eq == std::string::npos) ? line : line.substr(0, eq);
                if (k == key) { line = key + "=" + value; found = true; }
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back(key + "=" + value);
    std::ofstream o(dst, std::ios::trunc);
    if (!o.good()) {
        DebugLog::line("updatecfg: scrittura %s fallita", dst.c_str());
        return false;
    }
    for (auto& l : lines) o << l << "\n";
    return true;
}

// Dopo ogni scrittura: se esiste un secondo file senza url, è un'esca
// di un write passato — ripiega il channel nella home (se manca) e
// rimuovila, così lo split-brain si autoripara al primo toggle.
static void updateCfgFoldDecoys(const std::string& basePath, const std::string& home) {
    std::string cfg, off;
    updateCfgPaths(basePath, cfg, off);
    for (const auto& other : {cfg, off}) {
        if (other.empty() || other == home) continue;
        if (fileHasKey(other, "url")) continue; // vero config, non esca
        if (!fileHasKey(home, "channel")) {
            UpdateCfg tmp;
            parseUpdateCfgFile(other, tmp, true);
            if (!tmp.channel.empty()) setKeyInFile(home, "channel", tmp.channel);
        }
        std::remove(other.c_str());
        DebugLog::line("updatecfg: esca %s ripiegata", other.c_str());
    }
}

// Scrive una chiave key= nel file home (mai esche, vedi REGOLA HOME).
bool UI::writeUpdateCfgKey(const std::string& basePath, const std::string& key,
                            const std::string& value) {
    std::string home = updateCfgHome(basePath);
    if (!setKeyInFile(home, key, value)) return false;
    updateCfgFoldDecoys(basePath, home);
    return true;
}

// Scrive url= in update.cfg preservando le altre chiavi; attiva (toglie .off).
bool UI::writeUpdateCfgUrl(const std::string& basePath, const std::string& url) {
    std::string cfg, off;
    findUpdateCfgFiles(basePath, cfg, off);
    std::string dst = cfg.empty() ? basePath + "update.cfg" : cfg;
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    {
        // Vedi setKeyInFile: chiudere la lettura prima del truncate,
        // altrimenti su FatFs l'open fallisce a file esistente.
        std::ifstream f(dst);
        if (f.good()) {
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("url=", 0) == 0) { line = "url=" + url; found = true; }
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back("url=" + url);
    std::ofstream o(dst, std::ios::trunc);
    if (!o.good()) return false;
    for (auto& l : lines) o << l << "\n";
    if (!off.empty() && off != dst) std::remove(off.c_str());
    std::string baseOff = basePath + "update.cfg.off";
    if (baseOff != off) std::remove(baseOff.c_str());
    return true;
}

// Righe categoria 5 (Sviluppatore) in ordine di visualizzazione.
// UNICA fonte di verita' per count/label/value/activate di cat.5:
// aggiungere una voce = un enumeratore in ui.h + un push_back sotto + un
// case nelle tre funzioni. Niente piu' aritmetica settingsRowCount(5)-N
// sparsa (era fragile: 11 siti da tenere in sync a mano).
static std::vector<DevRow> devRowList(bool debugOn, bool sendOn) {
    std::vector<DevRow> v = { DevRow::DbgToggle, DevRow::QuickMenu };
    if (debugOn) { v.push_back(DevRow::ClearBp); v.push_back(DevRow::Normalize); v.push_back(DevRow::ClearGal); }
    if (sendOn) { v.push_back(DevRow::SendLog); v.push_back(DevRow::Crash); }
    v.push_back(DevRow::Rename);
    v.push_back(DevRow::Style);
    v.push_back(DevRow::Update);
    v.push_back(DevRow::Clear);
    v.push_back(DevRow::ShowRomsNoSave);
    v.push_back(DevRow::DevSync);
    return v;
}
static DevRow devRowAt(const std::vector<DevRow>& v, int row) {
    if (v.empty()) return DevRow::DevSync; // non succede mai, difensivo
    if (row < 0) return v.front();
    if (row >= (int)v.size()) return v.back();
    return v[(size_t)row];
}

std::vector<DevRow> UI::devShownList() const {
    if (!devDrawList_.empty()) return devDrawList_;
    return devRowList(DebugLog::enabled(), sendAvailable()); // pre-apertura
}

int UI::sysEmuRow() const {
    if (!mgbaPath_.empty()) return 2;
    if (!emuAnim_.settled(1.0f)) return 2; // ghost in chiusura
    return -1;
}
int UI::sysConfirmRow() const {
    return sysEmuRow() >= 0 ? 3 : 2;
}

int UI::settingsRowCount(int cat) const {
    switch (cat) {
        case 0: return 1; // Utente predefinito
        case 1: // Tema, Lingua, Layout selettore, [Zoom, Menu radiale,] Animazione scambio, Dock, Reset dock
            // Zoom e Menu radiale sono voci morte in Galleria (la vedi non li usa):
            // con il layout Galleria la lista si accorcia di 2 righe.
            return (gameSelectorLayout_ == GameSelectorLayout::Gallery) ? 6 : 8;
        case 2: return sysConfirmRow() + 1; // Core + Launcher [+ Emulatore] + Conferma uscita
        case 3: return 4; // Cartelle, Scansiona, Max, Pulisci
        case 4: {
            // Sorgente/edit custom solo con debug: l'utente normale resta su GitHub.
            // Modifica compare solo a sorgente custom ATTIVA (sendAvailable):
            // hasCustomUrlFile era vera anche col solo .off residuo dopo il
            // passaggio a GitHub, e la riga restava visibile a vuoto.
            // Ghost tendina: resta finche' la molla non si e' chiusa.
            int n = 4;
            if (sendAvailable() || !modUrlAnim_.settled(1.0f)) n = 5;
            return n; // Boot, Check, Sorgente, Canale [, Modifica]
        }
        case 5: // vedi devRowList() sopra: unica fonte di verita'
            return (int)devShownList().size();
        default: return 2; // Versione, Crediti
    }
}

// Riga cat 1 visualizzata -> riga logica. Solo in Galleria le voci Zoom (3) e
// Menu radiale (4) sono nascoste: le righe visualizzate dopo "Layout" (2)
// puntano alle righe logiche 5/6/7. In Classico l'identita'.
// Da usare in LABEL/VALUE/ACTIVATE: MAI indicizzare row grezza nel cat 1.
static int appearanceRow(int row, bool gallery) {
    if (gallery && row >= 3) return row + 2;
    return row;
}

// Frame tendina UNICO (era duplicato tra Aspetto e loop generico): stessa
// matematica per tutti — alpha quantizzata, slide 30px a destra, risalita
// righe sotto con overshoot della molla (collapse grezzo, non clampato).
static float collapseClamped(float c) {
    if (c < 0.0f) return 0.0f;
    if (c > 1.3f) return 1.3f; // margine per l'overshoot della molla
    return c;
}
static Uint8 collapseAlphaMul(float c) {
    float a = collapseClamped(c);
    if (a > 1.0f) a = 1.0f;
    return (Uint8)(((int)(255.0f * (1.0f - a)) / 16) * 16);
}
static int collapseXShift(float c) {
    return (int)(30.0f * collapseClamped(c));
}
static float collapseRowShift(float rawCollapse, int hiddenRows, int rowH) {
    return (float)(hiddenRows * rowH) * rawCollapse;
}

static std::string readDefaultUser(const std::string& basePath);
std::string UI::settingsRowLabel(int cat, int row) const {
    if (cat == 0) return i18n::get(StrKey::SetDefaultUser);
    if (cat == 1) {
        int r = appearanceRow(row, gameSelectorLayout_ == GameSelectorLayout::Gallery);
        if (r == 0) return i18n::get(StrKey::SetTheme);
        if (r == 1) return i18n::get(StrKey::SetLanguage);
        if (r == 2) return i18n::get(StrKey::SetGalleryLayout);
        if (r == 3) return i18n::get(StrKey::SetZoom);
        if (r == 4) return i18n::get(StrKey::SetRadialMenu);
        if (r == 5) return i18n::get(StrKey::SetTradeAnim);
        if (r == 6) return i18n::get(StrKey::SetDockVisible);
        return i18n::get(StrKey::SetDockReset);
    }
    if (cat == 2) {
        if (row == 0) return i18n::get(StrKey::SetCore);
        if (row == 1) return i18n::get(StrKey::SetInstallLauncher);
        if (row == sysConfirmRow()) return i18n::get(StrKey::SetConfirmExit);
        return i18n::get(StrKey::SetDefaultEmulator);
    }
    if (cat == 3) {
        if (row == 0) return i18n::get(StrKey::SetSavePaths);
        if (row == 1) return i18n::get(StrKey::SetScan);
        if (row == 2) return i18n::get(StrKey::SetBackupMax);
        return i18n::get(StrKey::SetBackupClean);
    }
    if (cat == 4) {
        if (row == 0) return i18n::get(StrKey::SetCheckUpdate);
        if (row == 1) return i18n::get(StrKey::SetUpdateBoot);
        if (row == 2) return i18n::get(StrKey::SetSource);
        if (row == 3) return i18n::get(StrKey::SetChannel);
        return i18n::get(StrKey::SetEditUrl);
    }
    if (cat == 5) {
        // Dispatch per tag (devRowList): robusto a debug on/off e rete on/off,
        // niente piu' collisioni tra indici di testa e settingsRowCount(5)-N.
        switch (devRowAt(devShownList(), row)) {
            case DevRow::DevSync: return i18n::get(StrKey::DevSyncTitle);
            case DevRow::Clear: return i18n::get(StrKey::ScraperBoxartClear);
            case DevRow::Update: return i18n::get(StrKey::ScraperBoxartTitle);
            case DevRow::Style: return i18n::get(StrKey::ScraperBoxartStyle);
            case DevRow::Rename: return i18n::get(StrKey::ScraperRenameTitle);
            case DevRow::ShowRomsNoSave: return i18n::get(StrKey::ShowRomsNoSaveTitle);
            case DevRow::DbgToggle: return i18n::get(StrKey::SetDebugToggle);
            case DevRow::QuickMenu: return i18n::get(StrKey::SetDbgMenu);
            case DevRow::ClearBp: return i18n::get(StrKey::ClearBpHistTitle);
            case DevRow::Normalize: return normalizeRowLabel();
            case DevRow::ClearGal: return i18n::get(StrKey::ClearGalCacheTitle);
            case DevRow::SendLog: return i18n::get(StrKey::SendLogTitle);
            case DevRow::Crash: return i18n::get(StrKey::CrashReportTitle);
        }
        return i18n::get(StrKey::CrashReportTitle); // irraggiungibile, difensivo
    }
    if (row == 0) return i18n::get(StrKey::SetVersion);
    return i18n::get(StrKey::SetCredits);
}
std::string UI::settingsRowValue(int cat, int row) {
    if (cat == 0) {
        std::string want = readDefaultUser(basePath_);
        if (want.empty()) return i18n::get(StrKey::SetUserAsk);
        return want;
    }
    if (cat == 1) {
        int r = appearanceRow(row, gameSelectorLayout_ == GameSelectorLayout::Gallery);
        if (r == 0) return getThemeName(themeIndex_);
        if (r == 1) return langDisplayName(i18n::currentLang());
        if (r == 2)
            return (gameSelectorLayout_ == GameSelectorLayout::Gallery)
                 ? i18n::get(StrKey::LayoutGallery) : i18n::get(StrKey::LayoutClassic);
        if (r == 3) return std::to_string(zoomGrow_) + "px";
        if (r == 4) return Settings::radialMenu() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        if (r == 5) return Settings::tradeAnim() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        if (r == 6) {
            if (!dockLoaded_) dockStateLoad();
            return dockState_.visible ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        }
        return ""; // Reset dock: riga azione, niente valore
    }
    if (cat == 2) {
        if (row == 0)
            return useOpenHome() ? i18n::get(StrKey::SetCoreOh) : i18n::get(StrKey::SetCorePk);
        if (row == 1) return ""; // riga azione, come "Scansiona": niente valore a destra
        if (row == sysConfirmRow())
            return Settings::confirmExit() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
#ifdef OH_LINUX
        // La riga compare solo se rilevato: su R36S findMgba() trova
        // retroarch (lanciato poi col core mgba), quindi si mostra
        // RetroArch e non mGBA per non confondere.
        return "RetroArch (mgba)"; // riga info: emulatore rilevato, nessuna azione
#else
        return "mGBA"; // riga info: unico emulatore supportato per ora
#endif
    }
    if (cat == 3) {
        if (row == 0) {
            int on = 0;
            for (auto& e : importPaths_)
                if (e.enabled) on++;
            // "N (M ON)": la riga toggle USB della lista non e un percorso.
            return std::to_string((int)importPaths_.size()) + " (" +
                   std::to_string(on) + " " + i18n::get(StrKey::SetOn) + ")";
        }
        if (row == 1) return "";
        if (row == 2)
            return std::to_string(backupCapMb(false)) + " MB";
        return "";
    }
    if (cat == 4) {
        if (row == 0) return ""; // riga azione manuale, niente valore a destra
        if (row == 1) {
            // Check al boot: default ON se l'utente non ha espresso preferenza.
            // Coerente con readUpdateAutoCfg() che tratta la chiave mancante come ON.
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            return cfg.autoOn ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        }
        if (row == 2) {
            // Solo GitHub/Custom, mai l'IP (quello sta sotto).
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            return cfg.url.empty() ? "GitHub" : "Custom";
        }
        if (row == 3) {
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            return cfg.channel == "beta" ? i18n::get(StrKey::ChannelBeta)
                                         : i18n::get(StrKey::ChannelStable);
        }
        // Modifica: mostra l'indirizzo custom a destra (come un tempo).
        std::string cu = customUrlAny(basePath_);
        if (cu.empty()) return "";
        auto proto = cu.find("://");
        std::string h = (proto == std::string::npos) ? cu : cu.substr(proto + 3);
        auto slash = h.find('/');
        if (slash != std::string::npos) h = h.substr(0, slash);
        return h;
    }
    if (cat == 5) {
        switch (devRowAt(devShownList(), row)) {
            case DevRow::DbgToggle:
                return DebugLog::enabled() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
            case DevRow::QuickMenu:
                return readQuickMenu(basePath_) ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
            case DevRow::Style:
                return boxartStyleLabel(Settings::boxartStyle());
            case DevRow::ShowRomsNoSave:
                return Settings::showRomsWithoutSave() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
            default:
                return ""; // righe azione, niente valore a destra
        }
    }
    if (row == 0) {
#ifdef BUILD_SHA
        return std::string("v") + APP_VERSION + " (" + BUILD_SHA + ")";
#else
        return "v" APP_VERSION;
#endif
    }
    return "";
}

static std::string readDefaultUser(const std::string& basePath) {
    (void)basePath;
    return Settings::defaultUser();
}

// Menu debug rapido: ON = il gear apre il menu + classico, OFF = le impostazioni.
// Non piu' static: usata anche da handleGameSelectorInput() in ui_selectors.cpp.
bool readQuickMenu(const std::string& basePath) {
    (void)basePath;
    return Settings::quickMenu();
}

static void writeQuickMenu(const std::string& basePath, bool on) {
    (void)basePath;
    Settings::setQuickMenu(on);
}

static bool writeBackupMb(const std::string& basePath, long mb) {    std::string path = updateCfgHome(basePath);
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    {
        // Vedi setKeyInFile: chiudere la lettura prima del truncate.
        std::ifstream f(path);
        if (f.good()) {
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto eq = line.find('=');
                std::string k = (eq == std::string::npos) ? line : line.substr(0, eq);
                if (k == "backup_mb") { line = "backup_mb=" + std::to_string(mb); found = true; }
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back("backup_mb=" + std::to_string(mb));
    std::ofstream o(path, std::ios::trunc);
    if (!o.good()) {
        DebugLog::line("updatecfg: scrittura %s fallita", path.c_str());
        return false;
    }
    for (auto& l : lines) o << l << "\n";
    updateCfgFoldDecoys(basePath, path);
    return true;
}

// Impostazioni -> Sviluppatore -> Ricerca dispositivi (stage 1): prova
// login + lista della cartella radice sul Filebrowser web di ArkOS/JELOS/
// ROCKNIX (vedi remote_sync.h). Flusso a blocchi (come lo swkbd stesso):
// primo l'IP salvato (chiesto una sola volta, poi riusato da Settings::),
// poi le credenziali di default ArkOS "ark"/"ark", chieste a mano solo se
// il login con quelle fallisce. Nessun browser di cartelle qui (quello e'
// lo stage 2): questo test conferma solo che si riesce a raggiungere il
// device e autenticarsi, utile anche per debug quando qualcosa non va.
std::string UI::promptTextBlocking(const std::string& header, const std::string& initial, int maxLen) {
    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetStringLenMax(&kbd, maxLen);
    swkbdConfigSetHeaderText(&kbd, header.c_str());
    if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());
    char result[160] = {};
    Result rc = swkbdShow(&kbd, result, sizeof(result));
    swkbdClose(&kbd);
    if (R_SUCCEEDED(rc)) return std::string(result);
    return std::string(); // annullato dall'utente
}

void UI::settingsRowActivate(int cat, int row, int dir, bool& running) {
    if (dir == 0) dir = 1;
    int n = settingsRowCount(cat);
    if (row < 0) row = 0;
    if (row >= n) row = n - 1; // cursore stale dopo cambio conteggio (es. debug off)
    if (cat == 0) {
        // Utente predefinito: [Chiedi, ...profili]. Salva nickname, vuoto = chiedi.
        std::vector<std::string> opts = {""};
        for (auto& p : account_.profiles()) opts.push_back(p.nickname);
        std::string cur = readDefaultUser(basePath_);
        int i = 0;
        for (; i < (int)opts.size(); i++)
            if (opts[i] == cur) break;
        if (i >= (int)opts.size()) i = 0;
        std::string next = opts[(i + dir + (int)opts.size()) % (int)opts.size()];
        Settings::setDefaultUser(next); // "" = chiedi
    } else if (cat == 1) {
        int r = appearanceRow(row, gameSelectorLayout_ == GameSelectorLayout::Gallery);
        if (r == 0) {
            themeIndex_ = (themeIndex_ + dir + THEME_COUNT) % THEME_COUNT;
            theme_ = &getTheme(themeIndex_);
            saveThemeIndex(basePath_, themeIndex_);
            clearTextCache();
        } else if (r == 1) {
            int n = (int)langList_.size();
            if (n > 0) {
                int cur = 0;
                for (int i = 0; i < n; i++)
                    if (langList_[i] == i18n::currentLang()) cur = i;
                std::string nl = langList_[(cur + dir + n) % n];
                i18n::init(nl);
                clearTextCache();
                Settings::setLanguage(nl);
            }
        } else if (r == 2) {
            // Layout selettore giochi: solo 2 valori, qualunque dir alterna.
            gameSelectorLayout_ = (gameSelectorLayout_ == GameSelectorLayout::Classic)
                ? GameSelectorLayout::Gallery : GameSelectorLayout::Classic;
            saveGameSelectorLayout(basePath_, (int)gameSelectorLayout_);
            // GAMES_PER_PAGE cambia con il layout: azzera pagina/slide per
            // evitare un pageStart stale (pagina vuota) al prossimo draw.
            gameSelPage_ = 0;
            selPageShown_ = 0;
            selSlide_ = 0.0f;
            gsSetFocus(GSFocus::Grid); // cambio layout: focus in griglia
            // Stessa ragione per l'anteprima Galleria: senza reset, al
            // prossimo ingresso in Galleria galSelShown_ punterebbe a un
            // indice della sessione precedente e farebbe partire uno slide
            // enorme dal nulla verso la selezione corrente.
            galSelShown_ = -1;
            galSlide_ = 0.0f;
            // Zoom/Menu radiale spariscono in Galleria: se il cursore era lì
            // riportalo sull'ultima riga visibile (il clamp in testa serve al
            // prossimo giro, qui setRow_ va corretto subito per il draw).
            int newCount = settingsRowCount(cat);
            if (setRow_ >= newCount) setRow_ = newCount - 1;
        } else if (r == 3) {
            static const int STEPS[] = {0, 4, 8, 12, 16};
            int i = 0;
            for (; i < 5; i++)
                if (STEPS[i] >= zoomGrow_) break;
            if (i > 4) i = 4;
            int ni = i + dir;
            if (ni < 0) ni = 0;
            if (ni > 4) ni = 4;
            zoomGrow_ = STEPS[ni];
            saveZoomGrow(basePath_, zoomGrow_);
        } else if (r == 4) {
            // Menu radiale: solo 2 valori, qualunque dir alterna (come Layout).
            Settings::setRadialMenu(!Settings::radialMenu());
        } else if (r == 5) {
            // Animazione scambio: idem, solo on/off.
            Settings::setTradeAnim(!Settings::tradeAnim());
        } else if (r == 6) {
            // Dock inferiore: mostra/nascondi (sincronizza lo stato live).
            if (!dockLoaded_) dockStateLoad();
            dockState_.visible = !dockState_.visible;
            if (!dockState_.visible) dockClearFocus();
            dockStateSave();
        } else {
            // Reset ordine dock: torna al factory e salva.
            if (!dockLoaded_) dockStateLoad();
            dockStateResetToDefault();
            dockStateSave();
            dockFocusFirst();
            showMessageAndWait(i18n::get(StrKey::SetDockReset), "OK");
        }
    } else if (cat == 2) {
        if (row == 0) {
            setCryptoEngine(useOpenHome() ? CryptoEngine::PK : CryptoEngine::OH);
        } else if (row == 1) {
            installLauncherForwarder();
        } else if (row == sysConfirmRow()) {
            Settings::setConfirmExit(!Settings::confirmExit());
        } // Emulatore predefinito (ghost compresa): riga info, nessuna azione
    } else if (cat == 3) {
        if (row == 0) {
            // Stessa lista del menu + (Import): toggle/rimuovi percorsi.
            showSettings_ = false;
            importFromSettings_ = true;
            showImportSettings_ = true;
            importSettingsCursor_ = 0;
        } else if (row == 1) {
            rescanImportedGames();
            showMessageAndWait(i18n::get(StrKey::SetTitle),
                i18n::fmt(StrKey::SetScanDone, std::to_string((int)importedGames_.size())));
        } else if (row == 2) {
            static const long STEPS[] = {32, 64, 128, 256, 512, 1024};
            long cur = backupCapMb(false);
            int i = 0;
            for (; i < 6; i++)
                if (STEPS[i] >= cur) break;
            if (i > 5) i = 5;
            int ni = i + dir;
            if (ni < 0) ni = 0;
            if (ni > 5) ni = 5;
            if (writeBackupMb(basePath_, STEPS[ni]))
                DebugLog::line("settings: backup_mb=%ld", STEPS[ni]);
        } else {
            if (showConfirmDialog(i18n::get(StrKey::SetTitle),
                    i18n::get(StrKey::SetCleanConfirm))) {
                uint64_t freed = prunePoolToCap(false) + prunePoolToCap(true);
                char msg[64];
                std::snprintf(msg, sizeof(msg), "%s %.1f MB",
                    i18n::get(StrKey::SetCleanDone).c_str(), freed / 1048576.0);
                showMessageAndWait(i18n::get(StrKey::SetTitle), msg);
            }
        }
        // Riga Spazio rimossa: il conteggio rallentava tutto (verra rifatta bene).
    } else if (cat == 4) {
        if (row == 0) {
            if (checkForUpdate(false)) running = false;
        } else if (row == 1) {
            // Toggle del check al boot. Default ON se update.cfg non ha scritto
            // un valore esplicito (vedi readUpdateAutoCfg).
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            std::string next = cfg.autoOn ? "0" : "1";
            if (writeUpdateCfgKey(basePath_, "auto", next))
                DebugLog::line("settings: auto=%s", next.c_str());
        } else if (row == 2) {
            if (!DebugLog::enabled()) return; // solo display senza debug
            // Switch GitHub <-> custom senza ridigitare: se non esiste alcun
            // file, apre direttamente l'edit per crearlo.
            std::string cfg, off;
            findUpdateCfgFiles(basePath_, cfg, off);
            if (cfg.empty() && off.empty()) {
                beginTextInput(TextInputPurpose::EditUpdateUrl);
            } else if (!cfg.empty()) {
                // Se l'attivo non ha url ma l'off sì, cfg è un'esca di un
                // write passato: ripiega channel/backup nell'off e rimuovila
                // invece di sovrascrivere l'off perdendo l'url (bug 2026-09-12).
                if (!off.empty() && !fileHasKey(cfg, "url") && fileHasKey(off, "url")) {
                    UpdateCfg decoy;
                    readUpdateCfg(basePath_, decoy); // mergia già entrambi
                    if (!decoy.channel.empty() && !fileHasKey(off, "channel"))
                        setKeyInFile(off, "channel", decoy.channel);
                    std::remove(cfg.c_str());
                    DebugLog::line("settings: sorgente -> GitHub (esca %s ripiegata)", cfg.c_str());
                    if (setRow_ == 4) setRow_ = 3;
                } else {
                    std::string dst = cfg + ".off";
                    if (std::rename(cfg.c_str(), dst.c_str()) == 0) {
                        DebugLog::line("settings: sorgente -> GitHub (%s disattivato)", cfg.c_str());
                        // La ghost Modifica collassa sotto: il cursore non
                        // resta sulla riga che sparisce.
                        if (setRow_ == 4) setRow_ = 3;
                    } else
                        showMessageAndWait(i18n::get(StrKey::SetTitle), std::string("rename FAIL:\n") + cfg);
                }
            } else {
                std::string dst = off.substr(0, off.size() - 4);
                if (std::rename(off.c_str(), dst.c_str()) == 0)
                    DebugLog::line("settings: sorgente -> custom (%s)", dst.c_str());
                else
                    showMessageAndWait(i18n::get(StrKey::SetTitle), std::string("rename FAIL:\n") + off);
            }
        } else if (row == 3) {
            // 2026-09-19: era "row == 2", stessa condizione del blocco Sorgente
            // appena sopra -- da quando il boot-toggle (nuovo row==1) ha
            // spostato Sorgente/Canale di una posizione, questo ramo era
            // diventato IRRAGGIUNGIBILE (il primo "row == 2" vince sempre) e
            // la riga "Canale" (ora davvero row==3) cadeva nel fallback
            // sottostante, aprendo l'edit dell'URL al posto del toggle
            // stabile/beta.
            // Canale stabile/beta: solo 2 valori, qualunque dir alterna.
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            std::string next = (cfg.channel == "beta") ? "stable" : "beta";
            if (writeUpdateCfgKey(basePath_, "channel", next))
                DebugLog::line("settings: channel=%s", next.c_str());
        } else {
            if (!sendAvailable()) return; // ghost Modifica: nessuna azione
            beginTextInput(TextInputPurpose::EditUpdateUrl);
        }
    } else if (cat == 5) {
        // Dispatch per tag (devRowList): l'ordine delle righe vive in UN solo
        // posto. I corpi sotto sono invariati rispetto alla vecchia versione
        // a indici (solo ri-ancorati ai tag).
        DevRow tag = devRowAt(devShownList(), row);
        if (!devExpanding_ && std::find(devGhosts_.begin(), devGhosts_.end(), tag) != devGhosts_.end())
            return; // ghost in chiusura: nessuna azione
        if (tag == DevRow::DevSync) {
            remoteSyncTestRow();
        } else if (tag == DevRow::Rename) {
            // Rinomina ROM: passa a setaccio TUTTE le ROM nei path import
            // abilitati (anche senza save), rileva lingua dall'header e
            // propone nomi No-Intro; salta quelle gia' corrette; conferma
            // prima di toccare i file (ROM + save/stati + gamelist).
            std::vector<RomInfo::RenamePlan> plans;
            {
                std::vector<std::string> dirs;
                for (const auto& e : importPaths_)
                    if (e.enabled && !e.path.empty()) dirs.push_back(e.path);
                size_t scanned = 0;
                for (const auto& rom : RomInfo::scanRoms(dirs)) {
                    scanned++;
                    RomInfo::RenamePlan p;
                    if (RomInfo::planRename(rom, p)) plans.push_back(p);
                }
                DebugLog::line("rominfo: rename scan %zu ROM -> %zu da rinominare",
                               scanned, plans.size());
            }
            std::string rtitle = i18n::get(StrKey::ScraperRenameTitle);
            if (plans.empty()) {
                showMessageAndWait(rtitle,
                    i18n::fmt(StrKey::ScraperRenameDone, "0", "0"));
            } else {
                // Niente piu' cap righe: il dialog scrolla (D-pad/analogico).
                std::string body;
                for (size_t i = 0; i < plans.size(); i++)
                    body += plans[i].oldBase + plans[i].ext + " -> " +
                            plans[i].newBase + plans[i].ext + "\n";
                if (showConfirmDialog(rtitle, body)) {
                    int ok = 0;
                    for (const auto& p : plans) {
                        std::string err;
                        if (RomInfo::applyRename(p, err)) ok++;
                    }
                    showMessageAndWait(rtitle,
                        i18n::fmt(StrKey::ScraperRenameDone,
                            std::to_string(ok), std::to_string(plans.size())));
                    rescanImportedGames();
                    loadGameIcons();
                }
            }
        } else if (tag == DevRow::Style) {
            // Stile boxart: B/X cicla lo stile e ricarica subito le tile
            // dalla cache dello stile (altrimenti restano quelle vecchie
            // finche' non si fa Aggiorna).
            int v = (Settings::boxartStyle() + dir + 3) % 3;
            Settings::setBoxartStyle(v);
            DebugLog::line("settings: boxart_style=%d", v);
            loadGameIcons();
        } else if (tag == DevRow::ShowRomsNoSave) {
            // Stesso pattern di DevRow::Style: toggle + rescan + ricarica tile,
            // effetto immediato senza riavvio ne' popup di conferma.
            // rescanImportedGames() droppa prima tutti gli import correnti e
            // rifa' la scan con il nuovo valore di showRomsWithoutSave(), quindi
            // gestisce da solo sia l'aggiunta che la rimozione delle ROM orfane.
            bool on = !Settings::showRomsWithoutSave();
            Settings::setShowRomsWithoutSave(on);
            DebugLog::line("settings: show_roms_without_save=%d", on ? 1 : 0);
            rescanImportedGames();
            loadGameIcons();
        } else if (tag == DevRow::Update) {
            // Aggiorna boxart (ROM): cache hit, poi locale (stile), poi
            // download 2D se la rete e' pronta (entrambe le piattaforme).
            bool netOk = true;
#ifndef OH_LINUX
            netOk = updateNetEnsureReady();
#endif
            if (!netOk) {
                // Senza rete il download e' impossibile: dirlo subito
                // invece di un generico 0/N che non spiega nulla.
                showMessageAndWait(i18n::get(StrKey::ScraperBoxartTitle),
                    i18n::get(StrKey::ScraperBoxartOffline));
            } else {
                // Barra di progresso per-ROM su entrambe le piattaforme: lo
                // scrape puo' metterci diversi secondi per gioco (rete +
                // fuzzy/GitHub), senza feedback sembra bloccato. Stesso
                // pattern del download update (showWorking con "%" -> barra).
                showWorking(i18n::get(StrKey::ScraperBoxartTitle));
                // Scrape su worker (job.h): il main resta libero per barra
                // e B in tempo reale. scrape() e' invariato (progress +
                // cancel cooperativo a granularita' singola ROM). Il fronte
                // di salita su B viene gratis dalla coda eventi (DOWN una
                // volta sola): niente piu' falso annullo da livello residuo.
                std::string cancelHint = i18n::get(StrKey::ScraperBoxartCancel);
                std::string cancellingMsg = i18n::get(StrKey::ScraperBoxartCancelling);
                std::string barTitle = i18n::get(StrKey::ScraperBoxartTitle);
                auto doScrape = [&]() -> Boxart::ScrapeResult {
                    bool scrapeCancel = false;
                    Boxart::ScrapeResult res;
                    BackgroundJob job;
                    auto scrapeWorker = [&](BackgroundJob& j) {
                        res = Boxart::scrape(basePath_, importedGames_,
                            [&](const std::string& s){ j.report(s); }, &scrapeCancel);
                    };
                    if (!job.start(scrapeWorker)) {
                        // Thread non partito: fallback sincrono senza cancel
                        // (esplicito, mai hang).
                        DebugLog::line("boxart: job.start fallita, scrape sincrono");
                        res = Boxart::scrape(basePath_, importedGames_,
                            [this](const std::string& s){ showWorking(s); });
                        return res;
                    }
                    std::string line;
                    while (!job.done()) {
                        job.poll(line);
                        if (!scrapeCancel) {
                            std::string msg = line.empty() ? barTitle : line;
                            size_t nl = msg.find('\n');
                            if (nl != std::string::npos) msg.insert(nl, "  " + cancelHint);
                            else msg += "  " + cancelHint;
                            showWorking(msg);
                        } else {
                            showWorking(cancellingMsg);
                        }
                        SDL_Event e;
                        while (SDL_PollEvent(&e)) {
                            if (e.type == SDL_QUIT) {
                                scrapeCancel = true;
                                SDL_PushEvent(&e); // non mangiarla: la vede il loop esterno
                            } else if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                                       e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                                scrapeCancel = true;
                            }
                        }
                        SDL_Delay(16);
                    }
                    job.join();
                    return res;
                };
                auto doneMsg = [&](const Boxart::ScrapeResult& r) {
                    return i18n::fmt(StrKey::ScraperBoxartDone,
                        std::to_string(r.found), std::to_string(r.total),
                        std::to_string(r.skipped));
                };
                Boxart::ScrapeResult res = doScrape();
                // Saltate da miss flaky (es. timeout SS poi rientrato):
                // offri il retry SENZA buttare le cover buone (Pulisci
                // riscaricherebbe tutto, spreco di API). A = riprova, B = chiudi.
                if (!res.cancelled && res.skipped > 0 &&
                    showConfirmDialog(barTitle,
                        doneMsg(res) + "\n" + i18n::get(StrKey::ScraperBoxartRetry))) {
                    Boxart::clearMissMarkers(basePath_);
                    res = doScrape();
                }
                showMessageAndWait(barTitle, doneMsg(res));
                loadGameIcons(); // le nuove cover in cache appaiono subito
            }
        } else if (tag == DevRow::Clear) {
            // Pulisci boxart: cancella cache/covers/ cosi' tornano le tile
            // composte logo+sfondo+label da romfs.
            if (showConfirmDialog(i18n::get(StrKey::ScraperBoxartClear),
                    i18n::get(StrKey::ScraperBoxartClearBody))) {
                int n = Boxart::clearCache(basePath_);
                showMessageAndWait(i18n::get(StrKey::ScraperBoxartClear),
                    i18n::fmt(StrKey::ScraperBoxartCleared, std::to_string(n)));
                loadGameIcons(); // le tile composte riappaiono subito
            }
        } else if (tag == DevRow::DbgToggle) {
            bool on = !DebugLog::enabled();
            DebugLog::setEnabled(on);
            std::string flag = basePath_ + "debug.enable";
            if (on) {
                FILE* f = std::fopen(flag.c_str(), "w");
                if (f) std::fclose(f);
            } else {
                std::remove(flag.c_str());
                if (setRow_ > 1) setRow_ = 0; // le righe extra spariscono
            }
        } else if (tag == DevRow::QuickMenu) {
            // Menu debug rapido: ON = gear apre il + classico, OFF = impostazioni.
            writeQuickMenu(basePath_, !readQuickMenu(basePath_));
        } else if (tag == DevRow::ClearBp) {
            if (showConfirmDialog(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistBody))) {
                if (Backpack::clearJournal(basePath_))
                    showMessageAndWait(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistDone));
                else
                    showMessageAndWait(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistFailed));
            }
        } else if (tag == DevRow::Normalize) {
            // Normalize save: analizza tutti i save salvati in sdmc e
            // corregge i Delta GBA con i 16B extra.  TODO: estendere a
            // scan cartelle ROM per corruzione/normalizzazione.
            int count = 0, fixed = 0;
            for (GameType g : availableGames_) {
                std::string p = importedSavePath(g, 0);
                if (p.empty()) continue;
                std::string info;
                if (SaveFile::normalizeDeltaSave(p, info)) fixed++;
                count++;
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s: %d/%d %s",
                normalizeRowLabel().c_str(), fixed, count,
                fixed > 0 ? "corretti" : "tutto OK");
            showMessageAndWait(normalizeRowLabel(), buf);
        } else if (tag == DevRow::ClearGal) {
            // Pulisci cache Galleria: svuota la mappa in RAM + il file su
            // disco (gallery_cache.dat). Non tocca i save dei giochi -- si
            // ricostruisce da sola al primo giro su ogni gioco (stesso
            // meccanismo del bump v3->v4, ma a richiesta invece che
            // automatico a ogni fix di formato).
            if (showConfirmDialog(i18n::get(StrKey::ClearGalCacheTitle), i18n::get(StrKey::ClearGalCacheBody))) {
                galPartyCache_.clear();
                std::remove((basePath_ + "gallery_cache.dat").c_str());
                showMessageAndWait(i18n::get(StrKey::ClearGalCacheTitle), i18n::get(StrKey::ClearGalCacheDone));
            }
        } else if (tag == DevRow::SendLog) {
            sendLogNow();
        } else {
            // DevRow::Crash: ultima rimasta, niente tag ambiguo possibile
            // (devRowAt mappa sempre dentro la lista).
            openCrashList();
        }
    } else {
        if (row == 1) {
            // Crediti: resta nelle impostazioni, B dall'About torna qui.
            showAbout_ = true;
        }
    }
    markDirty();
}

void UI::drawSettingsPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
#ifdef OH_LINUX
    // 4:3 (640x480): popup compatto, stessa struttura (12 righe max
    // in Sviluppatore x 32px + titolo = 454 <= 460).
    constexpr int POP_W = 620;
    constexpr int POP_H = 460;
    constexpr int ROW_H = 32;
    constexpr int CAT_W = 170;
#else
    constexpr int POP_W = 1000;
    constexpr int POP_H = 560;
    constexpr int ROW_H = 44;
    constexpr int CAT_W = 280;
#endif
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    drawTextCentered(i18n::get(StrKey::SetTitle), popX + POP_W / 2, popY + 24, T().text, font_);
    const char* cats[7] = { StrKey::SetUser, StrKey::SetAppearance, StrKey::SetEngine,
                            StrKey::SetData, StrKey::SetUpdate, StrKey::SetDebug,
                            StrKey::SetInfo };
    int listY = popY + 70;
    for (int c = 0; c < 7; c++) {
        int rowY = listY + c * ROW_H;
        if (c == setCat_ && setFocusLeft_) {
            drawRect(popX + 20, rowY, CAT_W - 20, ROW_H - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, CAT_W - 20, ROW_H - 4, T().cursor, 2);
        }
        drawText(i18n::get(cats[c]), popX + 26, rowY + 8, T().text, font_);
    }
    // Divisore verticale
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    int divX = popX + CAT_W + 10;
#ifdef OH_LINUX
    SDL_RenderDrawLine(renderer_, divX, listY, divX, popY + POP_H - 20);
#else
    SDL_RenderDrawLine(renderer_, divX, listY, divX, popY + POP_H - 50);
#endif
    int rx = divX + 24;
    if (setCat_ == 1) {
        // Aspetto: le voci "Zoom giochi" (3) e "Menu radiale" (4) sono voci
        // morte in layout Galleria (vedi settingsRowCount/appearanceRow).
        // Anziche' farle sparire di colpo, qui si anima con la stessa molla
        // usata per il riordino dock (dockSpringStep, K/D uguali) un fattore
        // di collasso 0..1: le due righe sfumano scivolando a destra, le
        // righe sotto risalgono a riempire lo spazio con un filo di
        // overshoot (budino) prima di fermarsi. Il layout logico/nav resta
        // quello istantaneo di sempre (appearanceRow) -- qui e' solo il
        // disegno a interpolare.
        float target = (gameSelectorLayout_ == GameSelectorLayout::Gallery) ? 1.0f : 0.0f;
        if (appearanceAnim_.step(target)) markDirty();
        float collapse = appearanceAnim_.v;
        int selectedLogical = appearanceRow(setRow_, gameSelectorLayout_ == GameSelectorLayout::Gallery);
        for (int r = 0; r < 8; r++) {
            bool hideable = (r == 3 || r == 4);
            float localCollapse = hideable ? appearanceAnim_.v : 0.0f;
            Uint8 alphaMul = hideable ? collapseAlphaMul(localCollapse) : 255;
            if (hideable && alphaMul == 0) continue; // completamente nascosta: niente da disegnare
            float rowShift = collapseRowShift(collapse, 2, ROW_H); // le righe sotto risalgono seguendo la molla (con overshoot)
            int rowY = listY + (int)(r * ROW_H - (r >= 5 ? rowShift : 0.0f) + 0.5f);
            std::string label, value;
            switch (r) {
                case 0: label = i18n::get(StrKey::SetTheme); value = getThemeName(themeIndex_); break;
                case 1: label = i18n::get(StrKey::SetLanguage); value = langDisplayName(i18n::currentLang()); break;
                case 2:
                    label = i18n::get(StrKey::SetGalleryLayout);
                    value = (gameSelectorLayout_ == GameSelectorLayout::Gallery)
                          ? i18n::get(StrKey::LayoutGallery) : i18n::get(StrKey::LayoutClassic);
                    break;
                case 3: label = i18n::get(StrKey::SetZoom); value = std::to_string(zoomGrow_) + "px"; break;
                case 4:
                    label = i18n::get(StrKey::SetRadialMenu);
                    value = Settings::radialMenu() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
                    break;
                case 5:
                    label = i18n::get(StrKey::SetTradeAnim);
                    value = Settings::tradeAnim() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
                    break;
                case 6:
                    label = i18n::get(StrKey::SetDockVisible);
                    if (!dockLoaded_) dockStateLoad();
                    value = dockState_.visible ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
                    break;
                default: label = i18n::get(StrKey::SetDockReset); value = ""; break;
            }
            int rowX = rx + (hideable ? collapseXShift(localCollapse) : 0); // scivolano a destra mentre sfumano
            if (r == selectedLogical && !setFocusLeft_) {
                drawRect(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().cursor, 2);
            }
            // Faded via modulate (una texture sola): al primo giro non si
            // cuoce nessuna variante alpha e non scatta.
            drawTextFaded(label, rowX, rowY + 8, T().text, alphaMul, font_);
            if (!value.empty()) {
                const auto& e = getTextEntry(value, font_, T().selected);
                drawTextFaded(value, popX + POP_W - 36 - e.w, rowY + 8, T().selected, alphaMul, font_);
            }
            if (r == 3) {
                // Slider zoom 0..16px con pallino, accanto al valore (stessa riga).
                // Larghezza riservata fissa su "16px" così la barra non balla quando passa da 1 a 2 cifre.
                static int maxVw = -1;
                if (maxVw < 0) maxVw = getTextEntry("16px", font_, T().selected).w;
                int bw = 100, bh = 8;
                int bx = popX + POP_W - 36 - maxVw - 14 - bw;
                int by = rowY + (ROW_H - 4) / 2 - bh / 2;
                SDL_Color barCol = T().textDim; barCol.a = (Uint8)((int)barCol.a * alphaMul / 255);
                drawRect(bx, by, bw, bh, barCol);
                int dxp = bx + (int)(bw * zoomGrow_ / 16.0);
                if (dxp < bx) dxp = bx;
                if (dxp > bx + bw) dxp = bx + bw;
                auto dot = [&](int cx, int cy, int rr, SDL_Color c) {
                    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                    for (int ddy = -rr; ddy <= rr; ddy++) {
                        int ddx = static_cast<int>(std::sqrt((double)(rr * rr - ddy * ddy)));
                        SDL_RenderDrawLine(renderer_, cx - ddx, cy + ddy, cx + ddx, cy + ddy);
                    }
                };
                SDL_Color dotCol = T().selected;
                dotCol.a = (Uint8)((int)dotCol.a * alphaMul / 255);
                dot(dxp, by + bh / 2, 7, dotCol);
            }
        }
    } else {
        // Tendine ghost (stessa molla di Aspetto, copiata): Emulatore e
        // Modifica sfumano scivolando a destra invece di sparire di colpo.
        // Step qui (una volta per frame), non per-riga: la ghost potrebbe
        // essere fuori finestra e non verrebbe mai avanzata.
        if (setCat_ == 2 && emuAnim_.step(mgbaPath_.empty() ? 1.0f : 0.0f)) markDirty();
        if (setCat_ == 4 && modUrlAnim_.step(sendAvailable() ? 0.0f : 1.0f)) markDirty();
        if (setCat_ == 5) {
            // Tendina righe dev (stessa molla): il toggle debug / sorgente
            // cambia la lista; le righe tolte/aggiunte sono ghost che
            // collassano/espandono, quelle sotto risalgono/scendono.
            std::vector<DevRow> cur = devRowList(DebugLog::enabled(), sendAvailable());
            float devTarget = devExpanding_ ? 0.0f : 1.0f;
            if (devAnim_.settled(devTarget) && cur != devDrawList_) {
                std::vector<DevRow> added, removed;
                for (DevRow t : cur)
                    if (std::find(devDrawList_.begin(), devDrawList_.end(), t) == devDrawList_.end())
                        added.push_back(t);
                for (DevRow t : devDrawList_)
                    if (std::find(cur.begin(), cur.end(), t) == cur.end())
                        removed.push_back(t);
                if (!added.empty()) {
                    devExpanding_ = true;
                    devGhosts_ = added;
                    devDrawList_ = cur;
                    devAnim_.reset(1.0f);
                } else if (!removed.empty()) {
                    devExpanding_ = false;
                    devGhosts_ = removed;
                    devAnim_.reset(0.0f); // disegna la vecchia lista
                } else {
                    devDrawList_ = cur; // stesso set, ordine mai cambia
                }
                devTarget = devExpanding_ ? 0.0f : 1.0f;
            }
            if (devAnim_.step(devTarget)) markDirty();
            if (!devExpanding_ && devAnim_.settled(1.0f) && devDrawList_ != cur) {
                devDrawList_ = cur; // collasso finito: aggancia la nuova
                devGhosts_.clear();
                if (setRow_ >= (int)cur.size()) setRow_ = (int)cur.size() - 1;
            }
            if (devExpanding_ && devAnim_.settled(0.0f)) devGhosts_.clear();
        }
        int n = settingsRowCount(setCat_);
        // Finestra scorrevole: centra la selezione, lascia spazio per footer.
        // visRows viene dallo spazio POPUP davvero disponibile (gia' meno
        // margine footer via -30/-40) -- niente cap fisso sopra: un cap
        // piu' stretto dello spazio reale (5 su Switch, 8 su R36S) era
        // rimasto da quando Sviluppatore aveva meno voci, e con lo scroll
        // ora coerente (vedi break sotto) tagliava righe che c'entravano
        // benissimo invece di scrollare solo quando serve davvero.
#ifdef OH_LINUX
        int visRows = (popY + POP_H - 30 - listY) / ROW_H;
#else
        int visRows = (popY + POP_H - 40 - listY) / ROW_H;
#endif
        if (visRows < 1) visRows = 1;
        int first = 0;
        if (n > visRows) {
            first = setRow_ - visRows / 2;
            if (first < 0) first = 0;
            if (first + visRows > n) first = n - visRows;
            if (first < 0) first = 0; // cursore oltre la coda (ghost appena chiusa)
        }
        // Frecce di scroll (come liste banche e popup): indicano le voci sopra/sotto.
        if (first > 0)
            drawTextCentered("^", rx + (POP_W - (rx - popX) - 28) / 2, listY - 14, T().arrow, font_);
        if (first + visRows < n)
            drawTextCentered("v", rx + (POP_W - (rx - popX) - 28) / 2, listY + visRows * ROW_H + 2, T().arrow, font_);
        for (int r = first; r < n; r++) {
            int vy = r - first;
            if (vy >= visRows) break; // rispetta il cap che decide anche dove va la freccia sotto
            int rowY = listY + vy * ROW_H;
            if (rowY + ROW_H > popY + POP_H) break;
            // Frame tendina UNICO (stessi helper di Aspetto): solo le ghost sfumano.
            bool ghost = (setCat_ == 2 && r == 2 && sysEmuRow() == 2 && mgbaPath_.empty()) ||
                         (setCat_ == 4 && r == 4 && !sendAvailable());
            float gcol = 0.0f;
            if (ghost) gcol = (setCat_ == 2) ? emuAnim_.v : modUrlAnim_.v;
            // Cat 5: ghost = righe in devGhosts_ (stessa molla devAnim_).
            int ghostsAbove = 0;
            if (setCat_ == 5 && !devGhosts_.empty()) {
                DevRow rtag = devRowAt(devDrawList_, r);
                if (std::find(devGhosts_.begin(), devGhosts_.end(), rtag) != devGhosts_.end()) {
                    ghost = true;
                    gcol = devAnim_.v;
                }
                for (int k = 0; k < r; k++) {
                    DevRow ktag = devRowAt(devDrawList_, k);
                    if (std::find(devGhosts_.begin(), devGhosts_.end(), ktag) != devGhosts_.end())
                        ghostsAbove++;
                }
            }
            Uint8 gMul = ghost ? collapseAlphaMul(gcol) : 255;
            if (ghost && gMul == 0) continue; // completamente nascosta: niente da disegnare
            int growX = ghost ? rx + collapseXShift(gcol) : rx; // scivola a destra mentre sfuma
            // Stesso helper di Aspetto: le righe sotto le ghost risalgono
            // di una riga per ghost seguendo la molla (con overshoot).
            // Cat 2 ne ha una (Conferma sotto Emulatore), cat 5 fino a 3
            // (toggle debug); Modifica e' sempre ultima.
            float belowShift = 0.0f;
            if (setCat_ == 2 && mgbaPath_.empty() && !emuAnim_.settled(1.0f) && r > 2)
                belowShift = collapseRowShift(emuAnim_.v, 1, ROW_H);
            if (setCat_ == 5 && ghostsAbove > 0)
                belowShift = collapseRowShift(devAnim_.v, ghostsAbove, ROW_H);
            rowY -= (int)(belowShift + 0.5f);
            if (r == setRow_ && !setFocusLeft_) {
                drawRect(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().cursor, 2);
            }
            drawTextFaded(settingsRowLabel(setCat_, r), growX, rowY + 8, T().text, gMul, font_);
            std::string v = settingsRowValue(setCat_, r);
            if (!v.empty()) {
                const auto& e = getTextEntry(v, font_, T().selected);
                // Tronca a larghezza utile (evita che "Aggiornamento" spinga fuori).
                std::string vt = v;
                int maxV = popX + POP_W - 36 - (rx + 8) - 12;
                while (vt.size() > 4 && e.w > maxV) {
                    vt = vt.substr(0, vt.size() - 5) + "..";
                    // ricalcola su vt, non su v
                    auto ee = getTextEntry(vt, font_, T().selected);
                    if (ee.w <= maxV) break;
                }
                const auto& ee = getTextEntry(vt, font_, T().selected);
                drawTextFaded(vt, popX + POP_W - 36 - ee.w, rowY + 8, T().selected, gMul, font_);
            }
        }
    }
    // Footer contestuale: se la riga focalizzata è uno slider, mostra hint con Stick ←/→
    bool _isSlider = !setFocusLeft_ && ((setCat_ == 1 && appearanceRow(setRow_, gameSelectorLayout_ == GameSelectorLayout::Gallery) == 3) || (setCat_ == 3 && setRow_ == 2));
    const char* _footKey = _isSlider ? StrKey::SetFooterSlider : StrKey::SetFooter;
    std::string _foot = i18n::get(_footKey);
    if (_isSlider && _foot == _footKey) _foot = i18n::get(StrKey::SetFooter); // fallback se traduzione manca
    drawTextCentered(_foot, popX + POP_W / 2, popY + POP_H - 20, T().textDim, fontSmall_);
}

void UI::handleSettingsInput(const SDL_Event& event, bool& running) {
    auto _isSliderRow = [&](int cat, int row) -> bool {
        return (cat == 1 && appearanceRow(row, gameSelectorLayout_ == GameSelectorLayout::Gallery) == 3) || (cat == 3 && row == 2);
    };
    // Stick analogico: su/giu come il D-pad (con repeat), sulla colonna attiva.
    // In impostazioni usiamo anche LEFTX per regolare gli slider (zoom / backup).
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }
    // Slider via stick orizzontale: ← diminuisce, → aumenta (solo a fuoco destro su riga slider)
    if (!setFocusLeft_ && stickDirX_ != 0 && _isSliderRow(setCat_, setRow_)) {
        uint32_t now = SDL_GetTicks();
        if (now - stickMoveTime_ >= (stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY)) {
            int dir = (stickDirX_ > 0) ? 1 : -1;
            settingsRowActivate(setCat_, setRow_, dir, running);
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
            return;
        }
    }
    if (stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        if (now - stickMoveTime_ >= (stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY)) {
            int d = (stickDirY_ > 0) ? 1 : -1;
            if (setFocusLeft_) {
                setCat_ = (setCat_ + d + 7) % 7;
                setRow_ = 0;
            } else {
                int n = settingsRowCount(setCat_);
                setRow_ = (setRow_ + d + n) % n;
            }
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        markDirty();
        int n = settingsRowCount(setCat_);
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (setFocusLeft_) { setCat_ = (setCat_ + 6) % 7; setRow_ = 0; }
                else setRow_ = (setRow_ + n - 1) % n;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (setFocusLeft_) { setCat_ = (setCat_ + 1) % 7; setRow_ = 0; }
                else setRow_ = (setRow_ + 1) % n;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (!setFocusLeft_ && _isSliderRow(setCat_, setRow_)) settingsRowActivate(setCat_, setRow_, -1, running);
                else if (!setFocusLeft_) setFocusLeft_ = true;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (setFocusLeft_) { setFocusLeft_ = false; setRow_ = 0; }
                else if (_isSliderRow(setCat_, setRow_)) settingsRowActivate(setCat_, setRow_, 1, running);
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A
                if (setFocusLeft_) { setFocusLeft_ = false; setRow_ = 0; }
                else settingsRowActivate(setCat_, setRow_, 1, running);
                break;
            case SDL_CONTROLLER_BUTTON_X: // Switch Y
                if (!setFocusLeft_) settingsRowActivate(setCat_, setRow_, -1, running);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B
                if (!setFocusLeft_) setFocusLeft_ = true;
                else showSettings_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_BACK:
            case SDL_CONTROLLER_BUTTON_START:
                showSettings_ = false;
                break;
        }
    }
}
