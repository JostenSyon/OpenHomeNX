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
#include "rominfo.h"
#include "pokedex.h"
#include "path_utils.h"

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


// saveMenuRows(): definita in ui_selectors.cpp (usata anche li'), non piu' static.
std::vector<std::string> saveMenuRows(GameType g, bool canSend);
// readUpdateCfg: definita in ui_update.cpp (usata anche qui), non piu' nel suo namespace anonimo.
bool readUpdateCfg(const std::string& basePath, UpdateCfg& out);
// removeRecursive: definita piu' sotto in questo stesso file, ma usata anche prima (tryDeleteHighlightedBackup).
bool removeRecursive(const std::string& p);

// --- Debug save popup (Switch X sul gioco, solo con debug on) -------------

static std::string backupTimestamp() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char b[32];
    std::snprintf(b, sizeof(b), "%04d%02d%02d_%02d%02d%02d",
                  t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                  t->tm_hour, t->tm_min, t->tm_sec);
    return b;
}

static void ensureDirRecursive(const std::string& dir) {
    std::string cur;
    for (char c : dir) {
        cur += c;
        if (c == '/') mkdir(cur.c_str(), 0755);
    }
}

void UI::openSaveMenu(GameType g, int occ) {
    saveMenuGame_ = g;
    saveMenuOcc_ = occ;
    saveMenuCursor_ = 0;
    showSaveMenu_ = true;
}

bool UI::tileHasUsableSave(int i) const {
    if (i < 0 || i >= (int)availableGames_.size()) return false;
    GameType g = availableGames_[i];
    int occ = importedOccurrence(i);
    bool fileBacked = !importedSavePath(g, occ).empty();
    bool title = !fileBacked && selectedProfile_ >= 0 && !appletMode_ &&
                 titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
    return fileBacked || title;
}

void UI::openGamePick(GamePickTarget t) {
    gamePickTarget_ = t;
    gamePickAvail_.clear();
    for (int i = 0; i < (int)availableGames_.size(); i++) {
        if (!tileHasUsableSave(i)) continue;
        // Trade esposto solo sui giochi che lo supportano davvero.
        if (t == GamePickTarget::Trade && !TradeEvo::supported(availableGames_[i])) continue;
        gamePickAvail_.push_back(i);
    }
    if (gamePickAvail_.empty()) {
        showMessageAndWait(i18n::get(StrKey::Error),
                           t == GamePickTarget::Trade
                               ? "Nessun gioco supporta lo scambio."
                               : "Nessun save disponibile.");
        return;
    }
    gamePickCursor_ = 0;
    gamePickScroll_ = 0;
    showGamePick_ = true;
    markDirty();
}

void UI::drawGamePickPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    constexpr int POP_W = 360;
    constexpr int MAX_VIS = 12;
    int count = (int)gamePickAvail_.size();
    int vis = std::min(count, MAX_VIS);
    int rowH = 36;
    int POP_H = 50 + vis * rowH + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    std::string title = std::string(gamePickTarget_ == GamePickTarget::Trade ? "Trade: " : "Save: ") +
                        i18n::get(StrKey::SelectGame);
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);
    int startY = popY + 50;
    for (int i = 0; i < vis; i++) {
        int rowY = startY + i * rowH;
        if (gamePickScroll_ + i == gamePickCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        int idx = gamePickAvail_[gamePickScroll_ + i];
        std::string nm = gameDisplayNameOf(availableGames_[idx]);
        if (nm.substr(0, 8) == "Pokemon ") nm = nm.substr(8);
        drawTextCentered(nm, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }
    drawTextCentered(i18n::get(StrKey::ASelectBCancel), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

std::string UI::manualBackupDir(GameType g) const {
    return basePath_ + "backups/manual/" + gamePathNameOf(g) + "/";
}

std::string UI::autoBackupDir(GameType g) const {
    return basePath_ + "backups/auto/" + gamePathNameOf(g) + "/";
}

static std::vector<std::string> listDirNames(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        out.push_back(n);
    }
    closedir(d);
    return out;
}

static bool dirHasFile(const std::string& dir) {
    for (auto& n : listDirNames(dir)) {
        struct stat st2;
        std::string full = dir + n;
        if (stat(full.c_str(), &st2) == 0 && !S_ISDIR(st2.st_mode)) return true;
    }
    return false;
}

// "20260909_162349..." -> "2026-09-09 16:23", altrimenti nome invariato.
static std::string prettyBackupName(const std::string& n) {
    if (n.size() >= 15 && n[8] == '_' && n[15] == '_') {
        bool digits = true;
        for (int i = 0; i < 15 && digits; i++)
            if (i != 8 && (n[i] < '0' || n[i] > '9')) digits = false;
        if (digits) {
            std::string rest = n.substr(16);
            char b[64];
            std::snprintf(b, sizeof(b), "%.4s-%.2s-%.2s %.2s:%.2s%s%s",
                          n.c_str(), n.c_str() + 4, n.c_str() + 6,
                          n.c_str() + 9, n.c_str() + 11,
                          rest.empty() ? "" : " ", rest.c_str());
            return b;
        }
    }
    return n;
}

std::vector<UI::BackupListEntry> UI::collectBackupEntries(GameType g) {
    // Solo unita ripristinabili: file per i save file-backed, dir con file
    // dentro per i titoli installati. Le dir intermedie (legacy) e i file
    // sciolti dentro i backup-dir (es. "main") non sono cliccabili -> fuori.
    bool isTitle = selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()
        && titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
    struct Root { std::string dir; const char* tag; };
    std::vector<Root> roots = { { manualBackupDir(g), "MAN" }, { autoBackupDir(g), "AUTO" } };
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
        roots.push_back({ basePath_ + "backups/" +
                          account_.profiles()[selectedProfile_].pathSafeName +
                          "/" + gamePathNameOf(g) + "/", "AUTO" });
    std::vector<BackupListEntry> entries;
    auto consider = [&](const std::string& full, const std::string& name, const char* tag) {
        struct stat st;
        if (stat(full.c_str(), &st) != 0) return;
        bool restorable = S_ISDIR(st.st_mode) ? (isTitle && dirHasFile(full + "/"))
                                              : !isTitle;
        if (!restorable) return;
        char label[128];
        std::snprintf(label, sizeof(label), "[%s] %s", tag, prettyBackupName(name).c_str());
        entries.push_back({ full, label });
    };
    for (auto& r : roots) {
        for (auto& n : listDirNames(r.dir)) {
            std::string full = r.dir + n;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode) && !dirHasFile(full + "/")) {
                // Dir intermedia legacy: scendi di un livello.
                for (auto& m : listDirNames(full + "/"))
                    consider(full + "/" + m, n + "/" + m, r.tag);
            } else {
                consider(full, n, r.tag);
            }
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const BackupListEntry& a, const BackupListEntry& b) { return a.path > b.path; });
    // Deduplica mantenendo l'ordine (stesso file da due dir mai, ma gratis).
    std::vector<BackupListEntry> uniq;
    for (auto& e : entries)
        if (uniq.empty() || uniq.back().path != e.path) uniq.push_back(e);
    return uniq;
}

void UI::openBackupList(GameType g) {
    backupListGame_ = g;
    backupListEntries_ = collectBackupEntries(g);
    backupListCursor_ = 0;
    backupListScroll_ = 0;
    backupListZlHeld_ = backupListZrHeld_ = false;
    showBackupList_ = true;
    showSaveMenu_ = false;
}

// ZL+ZR: elimina il backup evidenziato (con conferma), poi ricarica la
// lista e riaggancia cursore/scroll. Mai silenzioso (vedi deleteBackupEntry).
void UI::tryDeleteHighlightedBackup() {
    int count = (int)backupListEntries_.size();
    if (count <= 0) return;
    if (backupListCursor_ < 0 || backupListCursor_ >= count) return;
    std::string e = backupListEntries_[backupListCursor_].path;
    auto slash = e.find_last_of('/');
    std::string base = (slash == std::string::npos) ? e : e.substr(slash + 1);
    if (!showConfirmDialog("Delete backup", base + "\nElimino definitivamente. Procedo?")) return;
    if (deleteBackupEntry(e)) {
        backupListEntries_ = collectBackupEntries(backupListGame_);
        count = (int)backupListEntries_.size();
        if (backupListCursor_ >= count)
            backupListCursor_ = count > 0 ? count - 1 : 0;
        if (backupListScroll_ > backupListCursor_)
            backupListScroll_ = backupListCursor_;
        showMessageAndWait("Delete backup", "OK, backup eliminato.");
    } else {
        showMessageAndWait("Delete backup", "FAILED (vedi debug.log)");
    }
}

// Elimina un backup (file o dir, speculare al restore). Mai silenzioso:
// ogni fallimento torna false e il chiamante mostra FAILED.
bool UI::deleteBackupEntry(const std::string& entry) {
    struct stat st;
    if (stat(entry.c_str(), &st) != 0) {
        DebugLog::line("backup delete FAILED (stat): %s", entry.c_str());
        return false;
    }
    bool ok = removeRecursive(entry);
    DebugLog::line("backup delete: %s (%s)", entry.c_str(), ok ? "ok" : "FAIL");
    return ok;
}

void UI::drawBackupListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    std::string title = std::string("Backups: ") + gameInfo(backupListGame_).gameTag;
    constexpr int POP_W = 560;
    constexpr int ROW_H = 36;
    constexpr int VISIBLE = 12;
    int count = (int)backupListEntries_.size();
    int rows = count > 0 ? std::min(count, VISIBLE) : 1;
    int POP_H = 50 + rows * ROW_H + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);
    int startY = popY + 50;
    if (count == 0) {
        drawTextCentered("No backups for this game.", popX + POP_W / 2,
                         startY + (ROW_H - 4) / 2, T().textDim, font_);
    } else {
        for (int r = 0; r < rows; r++) {
            int i = backupListScroll_ + r;
            if (i >= count) break;
            int rowY = startY + r * ROW_H;
            if (i == backupListCursor_) {
                drawRect(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().menuHighlight);
                drawRectOutline(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().cursor, 2);
            }
            std::string base = backupListEntries_[i].label;
            if (base.size() > 52) base = base.substr(0, 51) + "~";
            drawText(base, popX + 30, rowY + 6, T().text, fontSmall_);
        }
    }
    drawTextCentered("A: restore  B: back  ZL+ZR: delete", popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

// Legge sdmc:/atmosphere/crash_reports/ (+ il vecchio fatal_errors/ come
// fallback) e ordina per data di modifica, piu' recente in cima: un crash
// "brutto" in uscita dal gioco non lascia traccia nel nostro debug.log
// (finisce dopo "exit: shutdown complete", quando il nostro processo ha
// gia' fatto return) ma Atmosphere lo scrive li' per conto suo.
std::vector<UI::BackupListEntry> UI::collectCrashReportEntries() {
    struct Item { std::string path; std::string label; long mtime; };
    std::vector<Item> items;
    static const char* kDirs[] = {
        "sdmc:/atmosphere/crash_reports/",
        "sdmc:/atmosphere/fatal_errors/",
    };
    for (const char* dir : kDirs) {
        for (auto& n : listDirNames(dir)) {
            std::string full = std::string(dir) + n;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
            struct tm tmv;
            localtime_r(&st.st_mtime, &tmv);
            char dt[32];
            std::snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d",
                          tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour, tmv.tm_min);
            std::string nm = n;
            if (nm.size() > 40) nm = nm.substr(0, 39) + "~";
            char label[160];
            std::snprintf(label, sizeof(label), "%s  %s", dt, nm.c_str());
            items.push_back({ full, label, (long)st.st_mtime });
        }
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.mtime > b.mtime; });
    std::vector<BackupListEntry> out;
    out.reserve(items.size());
    for (auto& it : items) out.push_back({ it.path, it.label });
    return out;
}

void UI::openCrashList() {
    crashListEntries_ = collectCrashReportEntries();
    crashListCursor_ = 0;
    crashListScroll_ = 0;
    showCrashList_ = true;
}

void UI::drawCrashListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    std::string title = "Crash report (Atmosphere)";
    constexpr int POP_W = 560;
    constexpr int ROW_H = 36;
    constexpr int VISIBLE = 12;
    int count = (int)crashListEntries_.size();
    int rows = count > 0 ? std::min(count, VISIBLE) : 1;
    int POP_H = 50 + rows * ROW_H + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);
    int startY = popY + 50;
    if (count == 0) {
        drawTextCentered("No crash reports found on the SD card.", popX + POP_W / 2,
                         startY + (ROW_H - 4) / 2, T().textDim, font_);
    } else {
        for (int r = 0; r < rows; r++) {
            int i = crashListScroll_ + r;
            if (i >= count) break;
            int rowY = startY + r * ROW_H;
            if (i == crashListCursor_) {
                drawRect(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().menuHighlight);
                drawRectOutline(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().cursor, 2);
            }
            std::string base = crashListEntries_[i].label;
            if (base.size() > 52) base = base.substr(0, 51) + "~";
            drawText(base, popX + 30, rowY + 6, T().text, fontSmall_);
        }
    }
    drawTextCentered("A: send  B: back", popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

// Riusa lo stesso upload dei save (endpoint /upload-save, tetto 128MB: un
// .bin di crash report e' comunque minuscolo rispetto a quel limite) con
// tag "crash" cosi' sul server finisce in dist/uploads/saves/ come
// crash_<timestamp>_<ip>.sav (estensione del server, il contenuto resta
// quello originale del file scelto).
void UI::sendCrashReportNow(const std::string& path) {
    UpdateCfg cfg;
    std::string err;
    if (!readUpdateCfg(basePath_, cfg) || cfg.url.empty()) {
        showMessageAndWait(i18n::get(StrKey::CrashReportTitle), i18n::get(StrKey::CrashReportNoUrl));
    } else if (!updateNetEnsureReady()) {
        showMessageAndWait(i18n::get(StrKey::CrashReportTitle), i18n::get(StrKey::CrashReportNetOff));
    } else {
        showWorking(i18n::fmt(StrKey::CrashReportUploading, cfg.url));
        if (updateNetUploadSave(cfg.url, cfg.token, path, "crash", err))
            showMessageAndWait(i18n::get(StrKey::CrashReportTitle), i18n::get(StrKey::CrashReportSent));
        else
            showMessageAndWait(i18n::get(StrKey::CrashReportTitle), i18n::fmt(StrKey::CrashReportFailed, err));
    }
}

void UI::autoBackupFileSave(GameType g, const std::string& path) {
    struct stat sst;
    bool haveSrc = stat(path.c_str(), &sst) == 0;
    uint64_t sz = haveSrc ? (uint64_t)sst.st_size : 0;
    long mt = haveSrc ? (long)sst.st_mtime : 0;
    if (!autoBackupNeeded(g, path, sz, mt, haveSrc)) {
        DebugLog::line("save: auto backup saltato (invariato)");
        return;
    }
    std::string dir = autoBackupDir(g);
    ensureDirRecursive(dir);
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    std::string dst = dir + backupTimestamp() + "_" + base;
    if (!copyFileTo(path, dst)) {
        DebugLog::line("auto backup FAILED: %s", path.c_str());
        return;
    }
    DebugLog::line("auto backup: %s -> %s", path.c_str(), dst.c_str());
    writeAutoInfo(dst, sz, mt);
    prunePoolToCap(true);
}

// Backup titoli all'apertura (solo first-ever) e all'uscita (se dirty):
// check via sidecar, mai walk. Ritorna true se ha copiato.
bool UI::backupTitleNow(GameType g, const std::string& mountPath, const std::string& saveFile) {
    struct stat sst;
    bool haveSrc = stat(saveFile.c_str(), &sst) == 0;
    uint64_t sz = haveSrc ? (uint64_t)sst.st_size : 0;
    long mt = haveSrc ? (long)sst.st_mtime : 0;
    if (!autoBackupNeeded(g, saveFile, sz, mt, haveSrc)) {
        DebugLog::line("save: auto backup saltato (invariato)");
        return false;
    }
    std::string backupDir = buildBackupDir(g);
    bool ok = AccountManager::backupSaveDir(mountPath, backupDir);
    if (!ok) return false;
    writeAutoInfo(backupDir, sz, mt);
    prunePoolToCap(false);
    return true;
}

// Choke point uscita: backup una tantum se il save e stato modificato.
// Idempotente (sidecar): chiamabile da piu punti senza doppie copie.
void UI::backupOnExitIfNeeded() {
    if (!save_.isLoaded() || exitBackedUp_) return;
    if (!save_.isDirty()) { exitBackedUp_ = true; return; }
    if (isDualBankMode()) { exitBackedUp_ = true; return; }
    uint32_t t0 = SDL_GetTicks();
    // Titoli = savePath_ dentro "save:/" (mount); resto = file su SD.
    bool fileBacked = savePath_.rfind("save:/", 0) != 0;
    if (fileBacked) {
        autoBackupFileSave(selectedGame_, savePath_);
    } else if (selectedProfile_ >= 0) {
        std::string mnt = account_.mountSave(selectedProfile_, selectedGame_);
        if (!mnt.empty()) {
            backupTitleNow(selectedGame_, mnt, mnt + saveFileNameOf(selectedGame_));
            account_.unmountSave();
        }
    }
    exitBackedUp_ = true;
    DebugLog::line("exit backup: %ums", SDL_GetTicks() - t0);
}

long UI::backupCapMb(bool fileBacked) const {
    UpdateCfg cfg;
    readUpdateCfg(basePath_, cfg); // url non richiesto per il tetto
    long mb = fileBacked ? cfg.backupMbSd : cfg.backupMb;
    return mb < 0 ? 0 : mb;
}

// Non piu' static: usata anche da openSettings() in ui_settings.cpp (prune pool backup).
uint64_t entryDiskSize(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    if (S_ISDIR(st.st_mode))
        return (uint64_t)AccountManager::calculateDirSize(p);
    return (uint64_t)st.st_size;
}

// Solo AUTO (auto/ + legacy profilo): i manuali non si toccano mai.
// Newest first (nomi con timestamp decrescente).
std::vector<std::string> UI::autoBackupEntries(GameType g) const {
    std::vector<std::string> entries;
    for (auto& n : listDirNames(autoBackupDir(g)))
        entries.push_back(autoBackupDir(g) + n);
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()) {
        std::string leg = basePath_ + "backups/" +
                          account_.profiles()[selectedProfile_].pathSafeName +
                          "/" + gamePathNameOf(g) + "/";
        for (auto& n : listDirNames(leg))
            entries.push_back(leg + n);
    }
    std::sort(entries.begin(), entries.end(), std::greater<std::string>());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    return entries;
}

// --- Sync FRLG nativo <-> ROM (stesso GIOCO base) ---
// ATTENZIONE (bug reale 2026-09-24): il pairing NON va fatto per
// bankGroupName ("FireRed / LeafGreen" copre ENTRAMBI i giochi) ma per gioco
// base (FR vs LG, regione ignorata) -- prima il save NSO di FireRed veniva
// copiato anche sulla ROM di LeafGreen, distruggendone il save diverso.
// Regola: vince il save con piu' tempo di gioco (playTimeSeconds, stesso
// container GBA da ambo i lati, gia' verificato). Mai copie alla cieca: se
// un playtime e' illeggibile la coppia si salta con log. Prima di OGNI
// scrittura entrambi i lati devono avere un backup (creato qui sul momento
// se manca: backupTitleNow per il nativo, autoBackupFileSave per la ROM).
// Orfane (ROM senza save): solo destinazione (nome che mGBA si aspetta:
// stessa cartella della ROM, stesso basename, .sav).
void UI::syncFrlgSaves(bool silent) {
    auto msg = [&](const char* k) {
        if (!silent) showMessageAndWait(i18n::get(StrKey::SyncFrlgTitle), i18n::get(k));
    };
    if (selectedProfile_ < 0) {
        DebugLog::line("frlg-sync: nessun profilo, niente native");
        msg(StrKey::SyncFrlgNoProfile);
        return;
    }
    if (isFRLG(selectedGame_) && save_.isLoaded()) {
        // Il save aperto vive in RAM: copiarci sopra/sotto divergerebbe.
        DebugLog::line("frlg-sync: gioco FRLG aperto, chiuderlo prima");
        msg(StrKey::SyncFrlgNeedClose);
        return;
    }
    // Native FRLG in lista (flag esatto, mai per euristica).
    assertGamesInSync();
    std::vector<GameType> natives;
    for (size_t i = 0; i < availableGames_.size(); i++)
        if (availableGamesNative_[i] != 0 && isFRLG(availableGames_[i]) &&
            std::find(natives.begin(), natives.end(), availableGames_[i]) == natives.end())
            natives.push_back(availableGames_[i]);
    int synced = 0;
    std::string detail;
    for (const auto& ig : importedGames_) {
        if (!isFRLG(ig.type)) continue;
        GameType want = frlgBase(ig.type);
        GameType nat = GameType::FR;
        bool haveNat = false;
        for (GameType n : natives)
            if (frlgBase(n) == want) { nat = n; haveNat = true; break; }
        if (!haveNat) continue;
        std::string mnt = account_.mountSave(selectedProfile_, nat);
        if (mnt.empty()) {
            DebugLog::line("frlg-sync: mount nativo fallito");
            continue;
        }
        std::string natFile = mnt + saveFileNameOf(nat);
        std::string romFile = ig.hasSave ? ig.filePath
            : parentDir(ig.filePath) + "/" + stemOf(ig.filePath) + ".sav";
        SaveFile a, b;
        a.setGameType(nat);
        b.setGameType(ig.type);
        long ta = -1, tb = -1;
        if (a.load(natFile)) ta = a.playTimeSeconds();
        if (ig.hasSave && b.load(romFile)) tb = b.playTimeSeconds();
        if (ta < 0 && tb < 0) {
            DebugLog::line("frlg-sync: playtime illeggibile ambo i lati, salto");
            account_.unmountSave();
            continue;
        }
        if (ta == tb) {
            account_.unmountSave();
            continue; // pari: niente da fare
        }
        // Backup-gate: crea i mancanti ADESSO, mai scrivere senza rete.
        if (autoBackupEntries(nat).empty())
            backupTitleNow(nat, mnt, natFile);
        if (ig.hasSave) {
            std::string base = romFile.substr(romFile.find_last_of("/\\") + 1);
            bool haveRomBk = false;
            for (auto& e : autoBackupEntries(ig.type))
                if (e.find(base) != std::string::npos) { haveRomBk = true; break; }
            if (!haveRomBk) autoBackupFileSave(ig.type, romFile);
        }
        bool natWins = ta > tb;
        if (copyFileTo(natWins ? natFile : romFile, natWins ? romFile : natFile)) {
            synced++;
            DebugLog::line("frlg-sync: %s %s -> %s %s (%ld vs %ld s)",
                gameInfo(nat).gameTag, (natWins ? "nativo" : "rom"),
                gameInfo(ig.type).gameTag, (natWins ? "rom" : "nativo"), ta, tb);
            if (!detail.empty()) detail += "\n";
            detail += std::string(gameInfo(nat).gameTag) + (natWins ? " NSO -> ROM (" : " ROM -> NSO (") +
                      std::to_string((natWins ? ta : tb) / 3600) + "h)";
        } else {
            DebugLog::line("frlg-sync: copia FALLITA");
        }
        account_.unmountSave();
    }
    if (!silent) {
        if (synced == 0) msg(StrKey::SyncFrlgNone);
        else showMessageAndWait(i18n::get(StrKey::SyncFrlgTitle),
                i18n::fmt(StrKey::SyncFrlgDone, std::to_string(synced), detail));
    }
    if (synced > 0) loadGameIcons();
}

// Throttle 30 min + skip se invariato. Solo auto (i manuali sempre).
// Sidecar .info accanto a ogni auto-backup (byte+mtime della sorgente al
// momento della copia): i check diventano stat singoli, mai walk ricorsivi.
static std::string autoInfoPath(const std::string& entry) {
    struct stat st;
    if (stat(entry.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return entry + "/.info";
    return entry + ".info";
}

static bool readAutoInfo(const std::string& entry, uint64_t& bytes, long& mt) {
    std::ifstream f(autoInfoPath(entry));
    if (!f.good()) return false;
    std::string line;
    bytes = 0;
    mt = 0;
    while (std::getline(f, line)) {
        if (line.rfind("bytes=", 0) == 0) bytes = std::strtoull(line.c_str() + 6, nullptr, 10);
        else if (line.rfind("mt=", 0) == 0) mt = std::atol(line.c_str() + 3);
    }
    return true;
}

void UI::writeAutoInfo(const std::string& entry, uint64_t bytes, long mt) {
    std::ofstream o(autoInfoPath(entry), std::ios::trunc);
    if (!o.good()) return;
    o << "bytes=" << bytes << "\nmt=" << mt << "\n";
}

bool UI::autoBackupNeeded(GameType g, const std::string& srcFile, uint64_t srcSize, long srcMt, bool haveSrc) {
    auto entries = autoBackupEntries(g);
    if (entries.empty()) return true;
    if (!haveSrc) return true;
    uint64_t b = 0;
    long mt = 0;
    if (!readAutoInfo(entries[0], b, mt)) {
        // Backup legacy senza .info: un walk una tantum, poi sidecar con lo
        // stato corrente (vale per i confronti futuri).
        b = entryDiskSize(entries[0]);
        writeAutoInfo(entries[0], b, srcMt);
        mt = srcMt;
    }
    if (srcSize == b && srcMt == mt) {
        DebugLog::line("auto backup skipped (invariato): %s", gameInfo(g).gameTag);
        return false;
    }
    return true;
}

bool removeRecursive(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return std::remove(p.c_str()) == 0;
    DIR* d = opendir(p.c_str());
    if (!d) return false;
    bool ok = true;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        if (!removeRecursive(p + "/" + n)) ok = false;
    }
    closedir(d);
    if (rmdir(p.c_str()) != 0) ok = false;
    return ok;
}

// Pota gli AUTO dal piu vecchio finche si rientra nel tetto (0 = no tetto).
// Tiene sempre almeno il piu recente. Ritorna i byte liberati.
uint64_t UI::pruneBackupsToCap(GameType g, bool fileBacked) {
    long mb = backupCapMb(fileBacked);
    if (mb <= 0) return 0;
    uint64_t cap = (uint64_t)mb * 1024 * 1024;
    auto entries = autoBackupEntries(g);
    uint64_t total = 0;
    for (auto& e : entries) total += entryDiskSize(e);
    uint64_t freed = 0;
    while (total > cap && entries.size() > 1) {
        std::string oldest = entries.back();
        entries.pop_back();
        uint64_t sz = entryDiskSize(oldest);
        if (removeRecursive(oldest)) {
            total -= (sz < total) ? sz : total;
            freed += sz;
            DebugLog::line("backup prune: %s (-%llu B)", oldest.c_str(), (unsigned long long)sz);
        } else {
            DebugLog::line("backup prune FAILED: %s", oldest.c_str());
            break;
        }
    }
    return freed;
}

void UI::sendSaveFor(GameType g, int occ) {
    // Ex blocco SendSave del menu + (cursor checks fuori, dal chiamante).
    // File-backed: upload diretto; titoli installati: mount temporaneo.
    UpdateCfg cfg;
    std::string err;
    if (!readUpdateCfg(basePath_, cfg) || cfg.url.empty()) {
        showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNoUrl));
    } else if (!updateNetEnsureReady()) {
        showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNetOff));
    } else {
        std::string path = importedSavePath(g, occ);
        if (!path.empty()) {
            showWorking(i18n::fmt(StrKey::SendSaveUploading, gameInfo(g).gameTag));
            if (updateNetUploadSave(cfg.url, cfg.token, path, gameInfo(g).gameTag, err))
                showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveSent));
            else
                showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::fmt(StrKey::SendSaveFailed, err));
        } else if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()
                   && titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0') {
            // v2: save account (titoli installati) — mount temporaneo,
            // upload, unmount sempre. Il guard titleId/nome esclude i giochi
            // sentinella (importati: 0x1-0x16, nome vuoto).
            std::string fpath;
            {
                std::string mnt = account_.mountSave(selectedProfile_, g);
                if (!mnt.empty())
                    fpath = mnt + saveFileNameOf(g);
            }
            if (fpath.empty()) {
                account_.unmountSave();
                showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveOnlyImported));
            } else {
                showWorking(i18n::fmt(StrKey::SendSaveUploading, gameInfo(g).gameTag));
                bool ok = updateNetUploadSave(cfg.url, cfg.token, fpath, gameInfo(g).gameTag, err);
                account_.unmountSave();
                if (ok)
                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveSent));
                else
                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::fmt(StrKey::SendSaveFailed, err));
            }
        } else {
            showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveOnlyImported));
        }
    }
}

bool UI::backupGameSave(GameType g, std::string& out, const std::string& alreadyMounted, bool manual) {
    std::string dir = manual ? manualBackupDir(g) : autoBackupDir(g);
    ensureDirRecursive(dir);
    std::string path = importedSavePath(g, saveMenuOcc_);
    if (!path.empty()) {
        std::string base = path.substr(path.find_last_of("/\\") + 1);
        std::string dst = dir + backupTimestamp() + "_" + base;
        if (!copyFileTo(path, dst)) return false;
        out = dst;
        DebugLog::line("save backup: %s -> %s", path.c_str(), dst.c_str());
        return true;
    }
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()
        && titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0') {
        std::string dst = dir + backupTimestamp() + "/";
        ensureDirRecursive(dst);
        // Se il chiamante ha gia' un mount attivo per questo gioco (zaino),
        // riusalo: mountSave() qui smonterebbe il suo, valido solo finche'
        // resta montato lui (vedi commento in ui.h).
        bool ownMount = alreadyMounted.empty();
        std::string mnt = ownMount ? account_.mountSave(selectedProfile_, g) : alreadyMounted;
        if (mnt.empty()) return false;
        bool ok = AccountManager::backupSaveDir(mnt, dst);
        if (ownMount) account_.unmountSave();
        if (!ok) return false;
        out = dst;
        DebugLog::line("save backup (account): %s -> %s", gameInfo(g).gameTag, dst.c_str());
        return true;
    }
    return false;
}

bool UI::restoreBackupEntry(GameType g, const std::string& entry) {
    struct stat st;
    if (stat(entry.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) {
        std::string orig = importedSavePath(g, saveMenuOcc_);
        if (orig.empty()) return false;
        bool ok = copyFileTo(entry, orig);
        DebugLog::line("save restore: %s -> %s (%s)", entry.c_str(), orig.c_str(), ok ? "ok" : "FAIL");
        return ok;
    }
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()
        && titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0') {
        std::string mnt = account_.mountSave(selectedProfile_, g);
        if (mnt.empty()) return false;
        bool ok = AccountManager::backupSaveDir(entry + "/", mnt);
        // Senza commit l'unmount scarta le scritture (Horizon): restore
        // fantasma che dice ok ma non cambia niente (Violetto 2026-09-09).
        // Il risultato del commit ora e' controllato davvero (prima veniva
        // ignorato: poteva dire "ok" anche se il commit falliva).
        if (ok && !account_.commitSave()) {
            DebugLog::line("save restore (account): commitSave FALLITO dopo copia ok");
            ok = false;
        }
        account_.unmountSave();
        DebugLog::line("save restore (account): %s (%s)", entry.c_str(), ok ? "ok" : "FAIL");
        return ok;
    }
    return false;
}

void UI::drawSaveMenuPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    std::vector<std::string> rows = saveMenuRows(saveMenuGame_, sendAvailable());
    int NROWS = (int)rows.size();
    constexpr int POP_W = 360;
    int rowH = 36;
    int POP_H = 50 + NROWS * rowH + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    std::string title = std::string("Save: ") + gameInfo(saveMenuGame_).gameTag;
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);
    int startY = popY + 50;
    for (int i = 0; i < NROWS; i++) {
        int rowY = startY + i * rowH;
        if (i == saveMenuCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        drawTextCentered(rows[i], popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }
    drawTextCentered(i18n::get(StrKey::ASelectBCancel), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

std::vector<GameSelMenuAction> UI::gameSelMenuActions() const {
    std::vector<GameSelMenuAction> v = { GameSelMenuAction::SwitchCore, GameSelMenuAction::DebugLog,
                                          GameSelMenuAction::ClearLog };
    // Invio log/save solo con override rete attivo (GitHub non riceve upload).
    // Stessa condizione di UI::sendAvailable() (debug on + url custom) —
    // richiamata invece di duplicarla qui una quarta volta.
    if (sendAvailable()) {
        v.push_back(GameSelMenuAction::SendLog);
        v.push_back(GameSelMenuAction::SendSave);
        // Niente CrashReport nel menu rapido +: resta solo in
        // Impostazioni -> Sviluppatore (richiesto esplicitamente,
        // il + doveva restare corto).
    }
    v.push_back(GameSelMenuAction::ImportSettings);
    v.push_back(GameSelMenuAction::CheckUpdate);
    v.push_back(GameSelMenuAction::OpenSettings);
    v.push_back(GameSelMenuAction::RemoteBox);
    v.push_back(GameSelMenuAction::Exit);
    return v;
}

void UI::drawGameSelMenuPopup() {
    // Semi-transparent dark overlay
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    // Single source of truth for both row count and row selection — the old
    // parallel hardcoded index/count logic drifted every time a row was
    // added (see v0.1.37: the highlight box vs text alignment bug in this
    // same popup started life as a similar copy-paste-and-forget mismatch).
    std::vector<GameSelMenuAction> actions = gameSelMenuActions();
    constexpr int POP_W = 300;
    int rowH = 36;
    int POP_H = 50 + (int)actions.size() * rowH + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::MenuTitle), popX + POP_W / 2, popY + 22, T().text, font_);

    int startY = popY + 50;

    for (int i = 0; i < (int)actions.size(); i++) {
        int rowY = startY + i * rowH;
        if (i == gameSelMenuCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        std::string label;
        switch (actions[i]) {
            case GameSelMenuAction::SwitchCore:      label = std::string("Switch Core") + (useOpenHome() ? " (OH)" : " (PK)"); break;
            case GameSelMenuAction::DebugLog:        label = std::string("Debug log") + (DebugLog::enabled() ? " (on)" : " (off)"); break;
            case GameSelMenuAction::ClearLog:         label = "Clear log"; break;
            case GameSelMenuAction::SendLog:         label = "Send log"; break;
            case GameSelMenuAction::SendSave:        label = "Send save"; break;
            case GameSelMenuAction::CrashReport:     label = "Crash report"; break;
            case GameSelMenuAction::ImportSettings:  label = "Import settings"; break;
            case GameSelMenuAction::CheckUpdate:     label = "Check for update"; break;
            case GameSelMenuAction::OpenSettings:     label = i18n::get(StrKey::SetTitle); break;
            case GameSelMenuAction::ToggleDock:        label = std::string("Dock: ") + (dockState_.visible ? "on" : "off"); break;
            case GameSelMenuAction::ReorderDock:       label = "Reorder dock"; break;
            case GameSelMenuAction::RemoteBox:         label = i18n::get(StrKey::RemoteBoxMenuLabel); break;
            case GameSelMenuAction::Exit:             label = "Exit"; break;
        }
        // drawTextCentered() takes the text's vertical CENTRE; match it to the
        // highlight box centre (box: top=rowY, height=rowH-4).
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }
}

