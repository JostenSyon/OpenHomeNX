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

namespace {
// update.cfg accanto all'NRO (o in sdmc:/switch/OpenHomeNX/). Righe key=value:
//   url=http://192.168.1.50:8000            (radice con latest.json + il .nro)
//   token=<PAT>                             (solo repo privati, header Bearer)
//   auto=1                                  (check update in parallelo al boot)
//   channel=stable|beta                     (canale update, default stable)
// Senza `url=` il check update usa le GitHub releases pubbliche
// (githubReleasesUrl sotto); Send log/save richiedono comunque `url=`
// (GitHub non riceve upload).
// REGOLA HOME (bug 2026-09-12): url+channel+backup vivono in UN solo file,
// quello con url= (attivo o .off che sia). Scrivere altrove crea un'esca
// che lo switch GitHub/Custom sovrascrive perdendo l'url per sempre.
// Etichetta corta per la sorgente update nei popup: l'URL intero sborda
// dalle card (github.../download = 60+ caratteri). Il fetch usa sempre
// l'URL completo, qui solo display.
static std::string updateSourceLabel(const std::string& url) {    auto gh = url.find("github.com/");
    if (gh != std::string::npos) {
        std::string rest = url.substr(gh + 11);
        auto slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string repo = rest.substr(slash + 1);
            auto end = repo.find('/');
            if (end != std::string::npos) repo = repo.substr(0, end);
            return "GitHub (" + rest.substr(0, slash) + "/" + repo + ")";
        }
        return "GitHub";
    }
    std::string h = url;
    auto proto = h.find("://");
    if (proto != std::string::npos) h = h.substr(proto + 3);
    auto slash = h.find('/');
    if (slash != std::string::npos) h = h.substr(0, slash);
    if (!h.empty() && h.size() <= 48) return "Rete locale (" + h + ")";
    const std::string& t = h.empty() ? url : h;
    return t.size() <= 48 ? t : "..." + t.substr(t.size() - 45);
}
struct UpdateCfg {
    std::string url, token;
    std::string channel;   // "" o "stable" = release stabili, "beta" = pre-release
    long backupMb = 256;   // tetto CUMULATIVO auto-backup titoli installati
    long backupMbSd = 32;  // tetto cumulativo save file-backed (SD, piccoli)
    bool autoOn = true;    // check al boot: default ON se la chiave `auto` manca
};

// Cerca update.cfg/.off nelle due dir note (duplica findUpdateCfgFiles,
// che è membro UI definito più sotto e qui non visibile come free).
static void updateCfgPaths(const std::string& basePath, std::string& cfg, std::string& off) {
    cfg.clear();
    off.clear();
    const std::string dirs[] = { basePath, "sdmc:/switch/OpenHomeNX/" };
    for (const auto& d : dirs) {
        struct stat st;
        if (cfg.empty() && stat((d + "update.cfg").c_str(), &st) == 0) cfg = d + "update.cfg";
        if (off.empty() && stat((d + "update.cfg.off").c_str(), &st) == 0) off = d + "update.cfg.off";
    }
}

// true se il file contiene una riga url= (anche vuota? no: chiave presente).
static bool fileHasKey(const std::string& path, const std::string& want) {
    std::ifstream f(path);
    if (!f.good()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        std::string k = (eq == std::string::npos) ? line : line.substr(0, eq);
        while (!k.empty() && (k.front() == ' ' || k.front() == '\t')) k.erase(k.begin());
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (k == want) return true;
    }
    return false;
}

// Il file home della config (vedi REGOLA HOME sopra): quello con url=,
// attivo o spento che sia; altrimenti l'attivo; altrimenti path da creare.
static std::string updateCfgHome(const std::string& basePath) {
    std::string cfg, off;
    updateCfgPaths(basePath, cfg, off);
    if (!off.empty() && fileHasKey(off, "url") && (cfg.empty() || !fileHasKey(cfg, "url")))
        return off;
    if (!cfg.empty())
        return cfg;
    if (!off.empty())
        return off;
    return basePath + "update.cfg";
}

// Parsa un file cfg in out; se keysOnlyChannel, prende solo channel
// (per l'altro file: non deve mai sovrascrivere la home).
static void parseUpdateCfgFile(const std::string& path, UpdateCfg& out, bool channelOnly) {
    std::ifstream f(path);
    if (!f.good()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.pop_back();
        };
        trim(k); trim(v);
        if (channelOnly) {
            if (k == "channel" && out.channel.empty()) out.channel = v;
            continue;
        }
        if (k == "url") out.url = v;
        else if (k == "token") out.token = v;
        else if (k == "channel") out.channel = v;
        else if (k == "backup_mb") out.backupMb = std::atol(v.c_str());
        else if (k == "backup_mb_sd") out.backupMbSd = std::atol(v.c_str());
        else if (k == "auto") out.autoOn = (v == "1" || v == "on" || v == "yes" || v == "true");
    }
}

bool readUpdateCfg(const std::string& basePath, UpdateCfg& out) {
    // Precedenza: prima il file ATTIVO per intero (solo lui decide url e
    // modalità GitHub/custom: l'url di un .off non deve mai riattivarsi da
    // solo), poi l'altro file SOLO per il channel mancante. Un'esca senza
    // url non sovrascrive mai nulla (bug 2026-09-12).
    std::string cfg, off;
    updateCfgPaths(basePath, cfg, off);
    if (!cfg.empty()) parseUpdateCfgFile(cfg, out, false);
    if (!off.empty()) {
        parseUpdateCfgFile(off, out, true); // channel: fallback se l'attivo non lo ha
        // 2026-09-19: "auto" (check aggiornamenti al boot) è una preferenza
        // dell'utente indipendente dalla sorgente (url/GitHub vs custom) --
        // ma updateCfgHome() sceglie come "home" il file con l'url quando
        // esiste (per non riattivare mai un url da un .off), quindi
        // writeUpdateCfgKey("auto", ...) in modalità GitHub finiva scritto
        // nel .off (l'unico file con url in quel momento). channelOnly sopra
        // legge SOLO channel da quel file, ignorando "auto": il toggle
        // risultava sempre bloccato su ON, mai spegnibile con sorgente
        // GitHub, perché la scrittura c'era ma la lettura non la vedeva mai.
        // Fix: se l'attivo non specifica "auto" esplicitamente, recuperalo
        // dal .off con un parse isolato (mai in "out" direttamente, per non
        // fargli sovrascrivere url/channel/token già decisi sopra).
        if (cfg.empty() || !fileHasKey(cfg, "auto")) {
            UpdateCfg offAuto;
            parseUpdateCfgFile(off, offAuto, false);
            out.autoOn = offAuto.autoOn;
        }
    }
    return !out.url.empty();
}
} // namespace

// Forward: definita piu' sotto accanto agli altri helper backup.
static bool removeRecursive(const std::string& p);

// ==================== Dock inferiore (riga bassa selettore giochi) ====================
// Stato persistito in settings.cfg (dock_order CSV + dock_visible). Disegno,
// tap e navigazione sono tutti guidati da dockLayout(), cosi' l'ordine utente
// non desincronizza mai le tre cose (era il bug del menu popup v0.1.37).
static constexpr int DOCK_ROW_DX = 104; // spaziatura icone dock (condivisa con la molla)

void UI::dockStateLoad() {
    dockState_.customOrder.clear();
    std::string orderStr = Settings::dockOrder();
    size_t start = 0;
    while (start < orderStr.size()) {
        size_t end = orderStr.find(',', start);
        if (end == std::string::npos) end = orderStr.size();
        std::string token = orderStr.substr(start, end - start);
        if (token == "Backpack") dockState_.customOrder.push_back(DockState::Item::Backpack);
        else if (token == "Banks") dockState_.customOrder.push_back(DockState::Item::Banks);
        else if (token == "SaveMenu") dockState_.customOrder.push_back(DockState::Item::SaveMenu);
        else if (token == "Trade") dockState_.customOrder.push_back(DockState::Item::Trade);
        else if (token == "RemoteBox") dockState_.customOrder.push_back(DockState::Item::RemoteBox);
        else if (token == "DevSync") dockState_.customOrder.push_back(DockState::Item::DevSync);
        else if (token == "Eject") dockState_.customOrder.push_back(DockState::Item::Eject);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (dockState_.customOrder.empty()) {
        dockStateResetToDefault();
    } else {
        // Migrazione: chi aveva gia' un ordine salvato da prima che
        // RemoteBox/DevSync esistessero non le troverebbe mai (il parse qui
        // sopra ignora semplicemente i token che non riconosce) -- le
        // inseriamo appena prima di Eject (o in fondo se Eject non c'e', es.
        // find() arriva a end()), cosi' compaiono anche per chi ha gia'
        // personalizzato la dock, senza toccare l'ordine del resto scelto
        // dall'utente.
        auto hasItem = [&](DockState::Item it) {
            for (auto x : dockState_.customOrder)
                if (x == it) return true;
            return false;
        };
        auto insertBeforeEjectOrAppend = [&](DockState::Item it) {
            auto pos = std::find(dockState_.customOrder.begin(), dockState_.customOrder.end(),
                                  DockState::Item::Eject);
            dockState_.customOrder.insert(pos, it);
        };
        if (!hasItem(DockState::Item::RemoteBox)) insertBeforeEjectOrAppend(DockState::Item::RemoteBox);
        if (!hasItem(DockState::Item::DevSync)) insertBeforeEjectOrAppend(DockState::Item::DevSync);
    }
    dockState_.visible = Settings::dockVisible();
    dockLoaded_ = true;
}

void UI::dockStateSave() const {
    std::string orderStr;
    for (size_t i = 0; i < dockState_.customOrder.size(); ++i) {
        if (i > 0) orderStr += ",";
        switch (dockState_.customOrder[i]) {
            case DockState::Item::Backpack: orderStr += "Backpack"; break;
            case DockState::Item::Banks: orderStr += "Banks"; break;
            case DockState::Item::SaveMenu: orderStr += "SaveMenu"; break;
            case DockState::Item::Trade: orderStr += "Trade"; break;
            case DockState::Item::RemoteBox: orderStr += "RemoteBox"; break;
            case DockState::Item::DevSync: orderStr += "DevSync"; break;
            case DockState::Item::Eject: orderStr += "Eject"; break;
        }
    }
    Settings::setDockOrder(orderStr);
    Settings::setDockVisible(dockState_.visible);
}

void UI::dockStateResetToDefault() {
    dockState_.customOrder = { DockState::Item::Backpack, DockState::Item::Banks,
        DockState::Item::SaveMenu, DockState::Item::Trade, DockState::Item::RemoteBox,
        DockState::Item::DevSync, DockState::Item::Eject };
    dockState_.visible = true;
    dockState_.reorderMode = false;
    dockState_.reorderFocusIdx = 0;
    dockState_.reorderEnterTime = 0;
}

bool UI::dockStateCanReorder() const {
    return dockLayout().size() >= 2 && !dockState_.reorderMode;
}

void UI::dockStateEnterReorderMode(int startIdx) {
    if (!dockLoaded_) dockStateLoad();
    auto slots = dockLayout();
    if (slots.size() < 2) return;
    dockState_.reorderMode = true;
    if (startIdx < 0 || startIdx >= (int)slots.size()) startIdx = 0;
    dockState_.reorderFocusIdx = startIdx;
    dockState_.reorderEnterTime = SDL_GetTicks();
    dockFocusItem(slots[startIdx].item);
    // Azzera molla: le icone partono tutte dalle loro posizioni target.
    for (int i = 0; i < MAX_DOCK_SLOTS; i++) { dockSlide_[i] = 0.0f; dockSlideVel_[i] = 0.0f; }
}

void UI::dockStateExitReorderMode(bool save) {
    dockState_.reorderMode = false;
    dockState_.reorderFocusIdx = 0;
    dockState_.reorderEnterTime = 0;
    if (save) dockStateSave();
}

// Effetto molla/budino sulle icone dock: ogni slot ha un offset x che
// parte dalla posizione "altra" dello swap e ritorna a 0 con overshoot.
// Chiamato ogni frame da drawDock().  K=0.35 stiffness, D=0.65 damping:
// producing ~1 overshoot before settling (budino).
void UI::dockSpringStep(std::vector<DockSlot>& slots) {
    constexpr float K = 0.35f;
    constexpr float D = 0.65f;
    bool anyMoving = false;
    for (int i = 0; i < MAX_DOCK_SLOTS && i < (int)slots.size(); i++) {
        if (dockSlide_[i] == 0.0f && dockSlideVel_[i] == 0.0f) continue;
        float force = -dockSlide_[i] * K - dockSlideVel_[i] * D;
        dockSlideVel_[i] += force;
        dockSlide_[i] += dockSlideVel_[i];
        if (std::fabs(dockSlide_[i]) < 0.3f && std::fabs(dockSlideVel_[i]) < 0.3f) {
            dockSlide_[i] = 0.0f;
            dockSlideVel_[i] = 0.0f;
        } else {
            anyMoving = true;
        }
    }
    if (anyMoving) markDirty();
}

void UI::dockStateSwapItems(int i, int j) {
    if (i >= 0 && i < (int)dockState_.customOrder.size() && j >= 0 && j < (int)dockState_.customOrder.size())
        std::swap(dockState_.customOrder[i], dockState_.customOrder[j]);
}

bool UI::dockStateItemVisible(DockState::Item item) const {
    switch (item) {
        case DockState::Item::SaveMenu:
            return true; // opera sul gioco evidenziato (come la voce radiale)
        case DockState::Item::Trade: {
            if (isDualBankMode()) return false;
            if (gameSelectorLayout_ == GameSelectorLayout::Gallery) {
                if (gameSelCursor_ < 0 || gameSelCursor_ >= (int)availableGames_.size()) return false;
                return TradeEvo::supported(availableGames_[gameSelCursor_]);
            }
            // Classica: la dock apre una lista giochi -> visibile se almeno
            // UN gioco usabile supporta lo scambio (non solo il cursore).
            for (int i = 0; i < (int)availableGames_.size(); i++) {
                if (!tileHasUsableSave(i)) continue;
                if (TradeEvo::supported(availableGames_[i])) return true;
            }
            return false;
        }
        case DockState::Item::RemoteBox:
            // Come UI::openRemoteBox(): niente in dual-bank mode, e solo
            // quando il worker in background ha davvero trovato un device.
            return remoteDeviceAvailable_ && !isDualBankMode();
        case DockState::Item::DevSync:
            return remoteDeviceAvailable_;
        case DockState::Item::Eject:
#ifdef OH_USB_UPDATE
            return usbHsFsGetMountedDeviceCount() > 0;
#else
            return false;
#endif
        default:
            return true; // Backpack, Banks sempre visibili
    }
}

static bool readQuickMenu(const std::string& basePath);
static void writeQuickMenu(const std::string& basePath, bool on);

// Righe popup save: backup, browse, clean [, invio save] , chiudi.
// Normalize e' ora in Impostazioni -> Sviluppatore (analizza tutti i save).
// 2026-09-19: "Send save" veniva aggiunta qui SEMPRE, incondizionatamente --
// il commento originale ("X con debug") presupponeva che questo popup fosse
// raggiungibile solo dalla scorciatoia X in griglia (quella si', gated da
// DebugLog::enabled() al chiamante), ma lo stesso popup si apre anche
// dall'icona dock "SaveMenu" e dal menu radiale per-gioco (RadialAction::
// SaveMenu), NESSUNA delle due gated da debug: "Send save" compariva quindi
// sempre, a prescindere da debug e da sorgente GitHub/custom. Ora richiede
// lo stesso override rete di UI::sendAvailable() (debug on + url custom),
// passato dal chiamante perche' questa e' una funzione libera senza `this`.
static std::string normalizeRowLabel() { return i18n::get(StrKey::SetNormalizeSave); }
static std::vector<std::string> saveMenuRows(GameType g, bool canSend) {
    std::vector<std::string> r = { "Backup save", "Browse backups", "Clean old backups" };
    if (canSend) r.push_back("Send save");
    r.push_back("Close");
    return r;
}
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include <switch.h>

void UI::refreshBankCounts() {
    gameBankCounts_.clear();
    for (GameType g : availableGames_) {
        GameType p = pairedGame(g);
        auto it = gameBankCounts_.find(p);
        if (it != gameBankCounts_.end())
            gameBankCounts_[g] = it->second;
        else
            gameBankCounts_[g] = BankManager::countBanks(basePath_, g);
    }
}

// --- Profile Selector ---

void UI::drawProfileSelectorFrame() {
    SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
    SDL_RenderClear(renderer_);

    drawTextCentered(i18n::get(StrKey::SelectProfile), SCREEN_W / 2, 40, T().text, font_);

    const auto& profiles = account_.profiles();
    int count = (int)profiles.size();

    constexpr int CARD_W = 160;
    constexpr int CARD_H = 200;
    constexpr int CARD_GAP = 20;
    constexpr int ICON_SIZE = 128;

    int totalW = count * CARD_W + (count - 1) * CARD_GAP;
    int startX = (SCREEN_W - totalW) / 2;
    int startY = (SCREEN_H - CARD_H) / 2;

    for (int i = 0; i < count; i++) {
        int cardX = startX + i * (CARD_W + CARD_GAP);
        int cardY = startY;

        if (i == profileSelCursor_) {
            drawRoundRect(cardX, cardY, CARD_W, CARD_H, 12, T().menuHighlight);
            drawRoundRectOutline(cardX, cardY, CARD_W, CARD_H, 12, T().cursor, 3);
        } else {
            drawRoundRect(cardX, cardY, CARD_W, CARD_H, 12, T().panelBg);
        }

        int iconX = cardX + (CARD_W - ICON_SIZE) / 2;
        int iconY = cardY + 10;

        if (profiles[i].iconTexture) {
            SDL_Rect dst = {iconX, iconY, ICON_SIZE, ICON_SIZE};
            SDL_RenderCopy(renderer_, profiles[i].iconTexture, nullptr, &dst);
        } else {
            drawRect(iconX, iconY, ICON_SIZE, ICON_SIZE, T().iconPlaceholder);
            if (!profiles[i].nickname.empty()) {
                std::string initial(1, profiles[i].nickname[0]);
                drawTextCentered(initial, iconX + ICON_SIZE / 2, iconY + ICON_SIZE / 2,
                                 T().text, font_);
            }
        }

        std::string name = profiles[i].nickname;
        if (name.length() > 14) name = name.substr(0, 13) + ".";
        drawTextCentered(name, cardX + CARD_W / 2, cardY + ICON_SIZE + 24, T().text, fontSmall_);
    }

    drawStatusBar(i18n::get(StrKey::StatusProfile));
}

void UI::handleProfileSelectorInput(bool& running) {
    int count = account_.profileCount();
    if (count == 0) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return;
        }

        if (event.type == SDL_CONTROLLERBUTTONDOWN)
            markDirty();

        if (event.type == SDL_CONTROLLERAXISMOTION) {
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                updateStick(lx, ly);
            }
        }

        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    profileSelCursor_ = (profileSelCursor_ + count - 1) % count;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    profileSelCursor_ = (profileSelCursor_ + 1) % count;
                    break;
                case SDL_CONTROLLER_BUTTON_B: // Switch A = select
                    selectProfile(profileSelCursor_);
                    break;
                case SDL_CONTROLLER_BUTTON_X: // Switch Y = theme
                    showThemeSelector_ = true;
                    themeSelCursor_ = themeIndex_;
                    themeSelOriginal_ = themeIndex_;
                    break;
                case SDL_CONTROLLER_BUTTON_BACK: // - = about
                    showAbout_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    // Quit dal selettore profili: la mano (carry) muore qui.
                    if (!confirmQuitWithHold()) break;
                    running = false;
                    break;
            }
        }
    }

    // Joystick repeat navigation
    if (stickDirX_ != 0 || stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            if (stickDirX_ < 0)
                profileSelCursor_ = (profileSelCursor_ + count - 1) % count;
            else if (stickDirX_ > 0)
                profileSelCursor_ = (profileSelCursor_ + 1) % count;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    }
}

void UI::selectProfile(int index) {
    selectedProfile_ = index;

    // Build filtered game list: only games with save data for this profile
    availableGames_.clear();
    constexpr GameType allGames[] = {
        GameType::GP, GameType::GE, GameType::Sw, GameType::Sh,
        GameType::BD, GameType::SP, GameType::LA, GameType::S,
        GameType::V, GameType::ZA, GameType::FR, GameType::LG,
        GameType::FR_ES, GameType::LG_ES, GameType::FR_DE, GameType::LG_DE,
        GameType::FR_IT, GameType::LG_IT, GameType::FR_FR, GameType::LG_FR,
        GameType::FR_JA, GameType::LG_JA
    };
    for (GameType g : allGames) {
        if (account_.hasSaveData(index, g))
            availableGames_.push_back(g);
    }
    appendImportedGames();
    applyFavoritesOrder();

    if (availableGames_.empty()) {
        showMessageAndWait(i18n::get(StrKey::NoSaveData),
            i18n::get(StrKey::NoSaveDataBody));
        return;
    }

    refreshBankCounts();

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    selPageShown_ = 0;
    selSlide_ = 0.0f;
    galSelShown_ = -1;
    galSlide_ = 0.0f;
    gsSetFocus(GSFocus::Grid); // reset focus periferico (vedi GSFocus)
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();
    screen_ = AppScreen::GameSelector;
}

// --- File import (save SD/USB da emulatori o dump) ---

void UI::appendImportedGames() {
    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_);
    for (const auto& ig : importedGames_)
        availableGames_.push_back(ig.type);
    // Non riordino qui: il chiamante (selectProfile/fillPresentGames) fa già apply; rescan fa a parte.
}

std::string UI::importedSavePath(GameType game, int occurrence) const {
    int seen = 0;
    for (const auto& ig : importedGames_)
        if (ig.type == game) {
            if (seen == occurrence)
                return ig.filePath;
            seen++;
        }
    return "";
}

std::string UI::importedSourceTag(GameType game, int occurrence) const {
    int seen = 0;
    for (const auto& ig : importedGames_)
        if (ig.type == game) {
            if (seen == occurrence)
                return ig.sourceTag;
            seen++;
        }
    return "";
}

int UI::importedOccurrence(int cursor) const {
    if (cursor < 0 || cursor >= (int)availableGames_.size())
        return 0;
    GameType game = availableGames_[cursor];
    int n = 0;
    for (int i = 0; i < cursor; i++)
        if (availableGames_[i] == game)
            n++;
    return n;
}

void UI::rescanImportedGames() {
    std::vector<GameType> oldTypes;
    for (const auto& ig : importedGames_)
        oldTypes.push_back(ig.type);

    // Unlike appendImportedGames(), availableGames_ isn't being rebuilt from
    // scratch here — drop the previous imported entries first so re-running
    // this on every hotplug doesn't pile up duplicates.
    availableGames_.erase(
        std::remove_if(availableGames_.begin(), availableGames_.end(),
                       [](GameType g) { return isImportedFile(g) || isGen1File(g) || isGen2File(g) ||
                                               isGen45File(g) || isGen6XY(g) || isGen6ORAS(g) || isGen7SM(g) || isGen7USUM(g); }),
        availableGames_.end());

    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_);
    for (const auto& ig : importedGames_)
        availableGames_.push_back(ig.type);
    applyFavoritesOrder();

    // Clamp cursor/page: removals may have shrunk the list under them, and a
    // stale cursor + A press would OOB-read availableGames_.
    if (availableGames_.empty()) {
        gameSelCursor_ = 0;
        gameSelPage_ = 0;
    } else {
        if (gameSelCursor_ >= (int)availableGames_.size() || gameSelCursor_ < 0)
            gameSelCursor_ = (int)availableGames_.size() - 1;
        int totalPages = ((int)availableGames_.size() + 12 - 1) / 12;
        if (gameSelPage_ >= totalPages)
            gameSelPage_ = totalPages - 1;
        selPageShown_ = gameSelPage_;
        selSlide_ = 0.0f;
        if (gameSelCursor_ < gameSelPage_ * 12)
            gameSelCursor_ = gameSelPage_ * 12;
    }

    std::vector<GameType> newlyFound;
    for (const auto& ig : importedGames_)
        if (std::find(oldTypes.begin(), oldTypes.end(), ig.type) == oldTypes.end())
            newlyFound.push_back(ig.type);

    if (newlyFound.empty())
        return;

    DebugLog::line("import hotplug: %zu new save(s) found", newlyFound.size());
    refreshBankCounts();
    loadGameIcons();

    std::string names;
    for (GameType g : newlyFound) {
        if (!names.empty()) names += ", ";
        names += gameDisplayNameOf(g);
    }
    showMessageAndWait(i18n::get(StrKey::ImportFoundTitle), names);
    markDirty();
}

// --- Folder browser ("+ Add path..." without swkbd) ---

void UI::openFolderBrowser() {
    folderBrowserPath_.clear(); // roots view
    folderCursor_ = 0;
    folderScroll_ = 0;
    refreshFolderEntries();
    showFolderBrowser_ = true;
    DebugLog::line("folderbrowser: aperto, %zu root", folderEntries_.size());
    markDirty();
}

void UI::refreshFolderEntries() {
    folderEntries_.clear();
    folderCursor_ = 0;
    folderScroll_ = 0;
    if (folderBrowserPath_.empty()) {
        // Roots: SD always, USB devices when mounted.
        folderEntries_.push_back({"sdmc:/", true});
#ifdef OH_USB_UPDATE
        u32 n = usbHsFsGetMountedDeviceCount();
        if (n > 8) n = 8;
        if (n > 0) {
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            for (u32 i = 0; i < got; i++) {
                std::string name = devs[i].name;
                if (!name.empty() && name.back() != '/') name += '/';
                if (!name.empty()) folderEntries_.push_back({name, true});
            }
        }
#endif
        return;
    }
    DIR* d = opendir(folderBrowserPath_.c_str());
    if (!d)
        return;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;
        std::string full = folderBrowserPath_ + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0)
            continue;
        folderEntries_.push_back({entry->d_name, S_ISDIR(st.st_mode) != 0});
    }
    closedir(d);
    std::sort(folderEntries_.begin(), folderEntries_.end(),
              [](const FolderEntry& a, const FolderEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir > b.isDir;
                  return a.name < b.name;
              });
}

void UI::folderMoveCursor(int dir) {
    int count = static_cast<int>(folderEntries_.size());
    if (count <= 0) return;
    folderCursor_ += dir;
    if (folderCursor_ < 0) folderCursor_ = count - 1;
    if (folderCursor_ >= count) folderCursor_ = 0;
    constexpr int VISIBLE = 10; // matches drawFolderBrowserPopup
    if (folderCursor_ < folderScroll_)
        folderScroll_ = folderCursor_;
    else if (folderCursor_ >= folderScroll_ + VISIBLE)
        folderScroll_ = folderCursor_ - VISIBLE + 1;
    if (folderScroll_ < 0) folderScroll_ = 0;
    markDirty();
}

void UI::handleFolderBrowserInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERBUTTONUP) {
        // Stop hold-repeat when the repeated direction is released.
        auto btn = event.cbutton.button;
        if ((folderRepeatDir_ < 0 && btn == SDL_CONTROLLER_BUTTON_DPAD_UP) ||
            (folderRepeatDir_ > 0 && btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            folderRepeatDir_ = 0;
        return;
    }
    if (event.type != SDL_CONTROLLERBUTTONDOWN)
        return;
    int count = static_cast<int>(folderEntries_.size());
    switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            folderMoveCursor(-1);
            folderRepeatDir_ = -1;
            folderRepeatTime_ = SDL_GetTicks();
            folderRepeatFast_ = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            folderMoveCursor(1);
            folderRepeatDir_ = 1;
            folderRepeatTime_ = SDL_GetTicks();
            folderRepeatFast_ = false;
            break;
        case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
            showFolderBrowser_ = false;
            folderRepeatDir_ = 0;
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_B: // Switch A = enter dir (files ignored)
            if (folderBrowserPath_.empty()) {
                // Roots view: enter the selected root.
                if (count > 0) {
                    folderBrowserPath_ = folderEntries_[folderCursor_].name;
                    refreshFolderEntries();
                }
            } else if (count > 0 && folderEntries_[folderCursor_].isDir) {
                std::string next = folderBrowserPath_ + folderEntries_[folderCursor_].name + "/";
                DIR* probe = opendir(next.c_str());
                if (probe) {
                    closedir(probe);
                    folderBrowserPath_ = next;
                    refreshFolderEntries();
                }
            }
            folderRepeatDir_ = 0;
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_Y: // Switch X = up one level
            if (!folderBrowserPath_.empty()) {
                // Strip trailing '/' then last segment; a bare "xxx:/"
                // goes back to the roots view.
                std::string p = folderBrowserPath_;
                while (!p.empty() && p.back() == '/') p.pop_back();
                auto pos = p.find_last_of('/');
                if (pos == std::string::npos || pos + 1 >= p.size() - 1) {
                    folderBrowserPath_.clear(); // was at "xxx:/" -> roots
                } else {
                    folderBrowserPath_ = p.substr(0, pos + 1);
                }
                refreshFolderEntries();
            }
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_START: // + = use this folder
            if (!folderBrowserPath_.empty()) {
                bool dup = false;
                for (const auto& e : importPaths_)
                    if (e.path == folderBrowserPath_) { dup = true; break; }
                if (dup) {
                    showMessageAndWait(i18n::get(StrKey::FolderBrowserExists), folderBrowserPath_);
                } else {
                    importPaths_.push_back({folderBrowserPath_, true});
                    saveImportPaths(basePath_, importPaths_);
                    rescanImportedGames();
                    showMessageAndWait(i18n::get(StrKey::FolderBrowserAdded), folderBrowserPath_);
                }
                showFolderBrowser_ = false;
            }
            markDirty();
            break;
    }
}

// --- Game Icons ---

void UI::loadGameIcons() {
    freeGameIcons();
    std::string cacheDir = basePath_ + "cache/";
    mkdir(cacheDir.c_str(), 0755);

    if (DebugLog::enabled()) {
        std::string tags;
        for (GameType game : availableGames_) {
            tags += gameInfo(game).gameTag;
            tags += " ";
        }
        DebugLog::line("icons: %zu available game(s): %s",
            availableGames_.size(), tags.c_str());
    }

    bool needSystem = false;
    for (GameType game : availableGames_) {
        // Imported games (Ruby/Sapphire/Emerald/Gen1 from a scanned file) have no
        // real titleId and no NS control data — they always use the abbrev.
        // placeholder in drawGameSelectorFrame() instead of a fetched icon.
        if (isImportedFile(game) || isGen1File(game) || isGen2File(game))
            continue;
        // Try loading from cache first
        char hexId[32];
        std::snprintf(hexId, sizeof(hexId), "%016lX", titleIdOf(game));
        std::string cachePath = cacheDir + hexId + ".jpg";

        SDL_Surface* surf = IMG_Load(cachePath.c_str());
        if (surf) DebugLog::line("icons: %s surf %dx%d", gameInfo(game).gameTag, surf->w, surf->h);
        if (surf) gameAccentCache_[game] = computeAccentColor(surf);
        if (surf) {
            if (SDL_Surface* rr = roundCornersSurface(surf, std::min(surf->w, surf->h) / 12)) {
                SDL_FreeSurface(surf);
                surf = rr;
            }
        }
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                gameIconCache_[game] = tex;
                continue;
            }
        }
        // A file that exists but won't decode is a truncated/corrupt cache
        // entry from an earlier failed fetch — drop it so we re-fetch cleanly.
        struct stat st;
        if (stat(cachePath.c_str(), &st) == 0) {
            DebugLog::line("icons: %s stale cache (%s, %lld B) -> removing",
                gameInfo(game).gameTag, cachePath.c_str(), (long long)st.st_size);
            remove(cachePath.c_str());
        }
        needSystem = true;
    }

    // Fetch uncached icons from system
    if (needSystem) {
        nsInitialize();
        for (GameType game : availableGames_) {
            if (isImportedFile(game) || isGen1File(game) || isGen2File(game))
                continue; // no titleId, no NS control data — placeholder only
            if (gameIconCache_.count(game))
                continue; // already loaded from cache

            NsApplicationControlData ctrlData;
            std::memset(&ctrlData, 0, sizeof(ctrlData));
            uint64_t controlSize = 0;
            Result rc = 1;
            // Official software uses _Storage (cache, then storage). Some titles
            // — e.g. a game whose control-data NCA isn't in the NS cache — only
            // resolve via _StorageOnly or the home-menu _CacheOnly copy, so try
            // all three before giving up.
            const NsApplicationControlSource kSources[] = {
                NsApplicationControlSource_Storage,
                NsApplicationControlSource_StorageOnly,
                NsApplicationControlSource_CacheOnly,
            };
            for (NsApplicationControlSource src : kSources) {
                controlSize = 0;
                std::memset(&ctrlData, 0, sizeof(ctrlData));
                rc = nsGetApplicationControlData(src, titleIdOf(game),
                        &ctrlData, sizeof(ctrlData), &controlSize);
                if (R_SUCCEEDED(rc) && controlSize > sizeof(NacpStruct)) {
                    if (src != NsApplicationControlSource_Storage)
                        DebugLog::line("icons: %s resolved via source %d",
                            gameInfo(game).gameTag, (int)src);
                    break;
                }
            }
            if (R_FAILED(rc) || controlSize <= sizeof(NacpStruct)) {
                DebugLog::line("icons: %s (%016lX) no control data: rc=0x%08X size=%llu",
                    gameInfo(game).gameTag, titleIdOf(game), (unsigned)rc,
                    (unsigned long long)controlSize);
                continue;
            }

            size_t iconSize = controlSize - sizeof(NacpStruct);

            // Save JPEG to cache
            char hexId[32];
            std::snprintf(hexId, sizeof(hexId), "%016lX", titleIdOf(game));
            std::string cachePath = cacheDir + hexId + ".jpg";
            FILE* f = std::fopen(cachePath.c_str(), "wb");
            if (f) {
                size_t wrote = std::fwrite(ctrlData.icon, 1, iconSize, f);
                std::fclose(f);
                if (wrote != iconSize) {
                    DebugLog::line("icons: %s cache write short (%zu/%zu) -> removing",
                        gameInfo(game).gameTag, wrote, iconSize);
                    remove(cachePath.c_str());
                }
            }

            // Decode and create texture
            SDL_RWops* rw = SDL_RWFromMem(ctrlData.icon, iconSize);
            if (!rw)
                continue;
            SDL_Surface* surf = IMG_Load_RW(rw, 1);
            if (surf) gameAccentCache_[game] = computeAccentColor(surf);
            if (surf) {
                if (SDL_Surface* rr = roundCornersSurface(surf, std::min(surf->w, surf->h) / 12)) {
                    SDL_FreeSurface(surf);
                    surf = rr;
                }
            }
            if (!surf) {
                DebugLog::line("icons: %s icon decode failed (%zu B): %s",
                    gameInfo(game).gameTag, iconSize, IMG_GetError());
                continue;
            }
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                gameIconCache_[game] = tex;
                DebugLog::line("icons: %s loaded from system (%zu B)",
                    gameInfo(game).gameTag, iconSize);
            }
        }
        nsExit();
    }
}

void UI::freeGameIcons() {
    for (auto& [game, tex] : gameIconCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    gameIconCache_.clear();
    gameAccentCache_.clear();
}

// --- Game Selector ---

// Icona/box-art di availableGames_[i] dentro il rettangolo (iconX, iconY,
// size, size): copertina reale se disponibile, altrimenti loghi/colori
// flat per GameType, posizionamento custom RSE, badge sorgente import.
// Estratta da drawGameSelectorFrame() cosi' la stessa logica si riusa
// identica per la griglia Classica (size=IS) e l'anteprima Galleria
// (size=COVER): tutte le formule qui dentro sono gia' espresse come
// percentuali di IS, quindi scalano automaticamente.
// Colore di sfondo flat per i GameType senza titleId (Bulbapedia color
// templates, stessi valori di pkm_rs_types::OriginGame::color()). Estratto
// da drawGameArt() cosi' lo stesso colore alimenta anche l'accent del
// pannello "vetro" in Galleria per questi giochi (vedi drawGameList_Gallery
// in ui_gallery.cpp): non avendo un titleId non hanno mai una texture in
// gameIconCache_, quindi gameAccentCache_ (popolata solo li') non li copre.
SDL_Color UI::flatBgColorFor(GameType g) const {
    switch (g) {
        case GameType::RUBY:     return {0xCD, 0x22, 0x36, 255};
        case GameType::SAPPHIRE: return {0x3D, 0x51, 0xA7, 255};
        case GameType::DIAMOND:  return {0x7E, 0xC8, 0xE8, 255};
        case GameType::PEARL:    return {0xE8, 0xA0, 0xC0, 255};
        case GameType::PLATINUM: return {0x90, 0x90, 0x98, 255};
        case GameType::HEARTGOLD: return {0xE8, 0xB8, 0x28, 255};
        case GameType::SOULSILVER: return {0x98, 0xB8, 0xD8, 255};
        case GameType::BLACK:    return {0x28, 0x28, 0x30, 255};
        case GameType::WHITE:    return {0xE8, 0xE8, 0xE8, 255};
        case GameType::BLACK2:   return {0x18, 0x18, 0x20, 255};
        case GameType::WHITE2:   return {0xF8, 0xF8, 0xF8, 255};
        case GameType::X:        return {0x20, 0x60, 0xC0, 255};
        case GameType::Y:        return {0xC0, 0x30, 0x30, 255};
        case GameType::OMEGA_RUBY:    return {0xC0, 0x20, 0x20, 255};
        case GameType::ALPHA_SAPPHIRE: return {0x20, 0x60, 0xC0, 255};
        case GameType::SUN:      return {0xE8, 0x70, 0x20, 255};
        case GameType::MOON:     return {0x30, 0x30, 0x60, 255};
        case GameType::ULTRA_SUN:  return {0xF8, 0x90, 0x18, 255};
        case GameType::ULTRA_MOON: return {0x40, 0x20, 0x80, 255};
        case GameType::RED:      return {0xE0, 0x20, 0x20, 255};
        case GameType::BLUE:     return {0x20, 0x60, 0xE0, 255};
        case GameType::YELLOW:   return {0xE8, 0xC8, 0x10, 255};
        case GameType::GOLD:     return {0xD8, 0xA8, 0x20, 255};
        case GameType::SILVER:   return {0xA0, 0xB0, 0xC0, 255};
        case GameType::CRYSTAL:  return {0x40, 0xC0, 0xE0, 255};
        case GameType::EMERALD: default: return {0x50, 0xC8, 0x78, 255};
    }
}

void UI::drawGameArt(int i, int iconX, int iconY, int size, bool scaleInner) {
    const int IS = size;
    // Unita di riferimento interna: 128 fisso (Classico invariato), oppure
    // size se scaleInner (Galleria: tutto in proporzione).
    const int UU = scaleInner ? size : 128;
    auto it = gameIconCache_.find(availableGames_[i]);
    if (it != gameIconCache_.end() && it->second) {
        SDL_Rect dst = {iconX, iconY, IS, IS};
        SDL_RenderCopy(renderer_, it->second, nullptr, &dst);
    } else if (isImportedFile(availableGames_[i]) || isGen1File(availableGames_[i]) || isGen2File(availableGames_[i])) {
        // No NS control data (no titleId) — a fixed per-game background
        // (Bulbapedia color templates, same values pkm_rs_types uses for
        // OriginGame::color()) plus the OpenHome logo PNG, letterboxed to
        // fit without stretching.
        SDL_Color bg = flatBgColorFor(availableGames_[i]);
        drawRoundRect(iconX, iconY, IS, IS, (10 * UU) / 128, bg);
        // Tile background image (e.g. Emerald artwork): center-cropped
        // square stretched over the icon rect, on top of the flat color
        // (which stays as fallback when the file is missing).
        auto bgIt = tileBgCache_.find(availableGames_[i]);
        if (bgIt != tileBgCache_.end() && bgIt->second) {
            int texW = 0, texH = 0;
            SDL_QueryTexture(bgIt->second, nullptr, nullptr, &texW, &texH);
            if (texW > 0 && texH > 0) {
                int side = std::min(texW, texH);
                SDL_Rect src = {(texW - side) / 2, (texH - side) / 2, side, side};
                SDL_Rect dst = {iconX, iconY, IS, IS};
                SDL_RenderCopy(renderer_, bgIt->second, &src, &dst);
            }
        }
        if (availableGames_[i] == GameType::RUBY ||
            availableGames_[i] == GameType::SAPPHIRE ||
            availableGames_[i] == GameType::EMERALD ||
            availableGames_[i] == GameType::RED ||
            availableGames_[i] == GameType::BLUE ||
            availableGames_[i] == GameType::YELLOW) {
            // RSE tile: box art grande quasi tutto il riquadro (box 120px
            // dentro 128, non esce mai), logo sopra come titolo. Entrambi
            // con sfondo trasparente verificato, quindi sovrapponibili.
            auto artIt = boxArtCache_.find(availableGames_[i]);
            if (artIt != boxArtCache_.end() && artIt->second) {
                int texW = 0, texH = 0;
                SDL_QueryTexture(artIt->second, nullptr, nullptr, &texW, &texH);
                if (texW > 0 && texH > 0) {
                    if (availableGames_[i] == GameType::RED ||
                        availableGames_[i] == GameType::BLUE ||
                        availableGames_[i] == GameType::YELLOW) {
                        // RBY: artwork full-bleed su tutto il riquadro
                        // (center-crop quadrato, nessun valore custom).
                        int side = std::min(texW, texH);
                        SDL_Rect src = {(texW - side) / 2, (texH - side) / 2, side, side};
                        SDL_Rect dst = {iconX, iconY, IS, IS};
                        SDL_RenderCopy(renderer_, artIt->second, &src, &dst);
                    } else {
                        // Base +15% di dimensione, a destra del 15% e in basso
                        // del 5% (coordinate tunate a mano per ogni gioco).
                        // Ruby: Groudon centrato, Sapphire: Kyogre +10% e +5pp.
                        // Prima era fisso su 128 (non seguiva lo zoom) → ora
                        // scala con IS così sfondo + scritta + pokemon
                        // ingrandiscono insieme della stessa quantità (zoomGrow).
                        float z = (float)IS / 128.0f;
                        const int SPR = (int)(117 * z);
                        const int SHIFT_X = (int)(15 * z);
                        const int SHIFT_Y = (int)(5 * z);
                    int shiftX = SHIFT_X;
                    int spr = SPR;
                    int shiftY = SHIFT_Y;
                    if (availableGames_[i] == GameType::RUBY) {
                        shiftX = 0;
                    }
                    if (availableGames_[i] == GameType::SAPPHIRE) {
                        shiftX = (int)(5 * z);
                        spr = (SPR * 110) / 100;
                        shiftY = (int)(15 * z);
                    }
                    float scale = std::min((float)spr / texW, (float)spr / texH);
                    int dstW = (int)(texW * scale);
                    int dstH = (int)(texH * scale);
                    int oX = iconX;
                    int oY = iconY;
                    SDL_Rect dst = {oX + (IS - dstW) / 2 + shiftX,
                                    oY + IS - dstH + shiftY, dstW, dstH};
                    SDL_Rect clip = {oX, oY, IS, IS};
                    SDL_RenderSetClipRect(renderer_, &clip);
                    SDL_RenderCopy(renderer_, artIt->second, nullptr, &dst);
                    SDL_RenderSetClipRect(renderer_, nullptr);
                    } // else (RSE con valori custom)
                }
            }
            auto logoIt = gameLogoCache_.find(availableGames_[i]);
            if (logoIt != gameLogoCache_.end() && logoIt->second) {
                int texW = 0, texH = 0;
                SDL_QueryTexture(logoIt->second, nullptr, nullptr, &texW, &texH);
                    if (texW > 0 && texH > 0) {
                        float zLogo = (float)IS / 128.0f;
                        const int LOGO_H = (int)(54 * zLogo);
                        int dstW = (int)(texW * ((float)LOGO_H / texH));
                        if (dstW > IS) dstW = IS;
                        int oX = iconX;
                        int oY = iconY;
                        SDL_Rect dst = {oX + (IS - dstW) / 2, oY, dstW, LOGO_H};
                    SDL_RenderCopy(renderer_, logoIt->second, nullptr, &dst);
                }
            }
        } else {
            auto logoIt = gameLogoCache_.find(availableGames_[i]);
            bool drewLogo = false;
            if (logoIt != gameLogoCache_.end() && logoIt->second) {
                int texW = 0, texH = 0;
                SDL_QueryTexture(logoIt->second, nullptr, nullptr, &texW, &texH);
                if (texW > 0 && texH > 0) {
                    float scale = std::min((float)IS / texW, (float)IS / texH);
                    int dstW = (int)(texW * scale);
                    int dstH = (int)(texH * scale);
                    SDL_Rect dst = {iconX + (IS - dstW) / 2, iconY + (IS - dstH) / 2, dstW, dstH};
                    SDL_RenderCopy(renderer_, logoIt->second, nullptr, &dst);
                    drewLogo = true;
                }
            }
            if (!drewLogo) {
                // No logo asset (Gen1 file games): centered game tag.
                const char* tag = gameInfo(availableGames_[i]).gameTag;
                const auto& te = getTextEntry(tag, font_, T().text);
                drawText(tag, iconX + (IS - te.w) / 2, iconY + (IS - te.h) / 2,
                         T().text, font_);
            }
        }
        // Small source-folder badge (bottom-left corner of the icon) —
        // only useful when more than one plausible source could hold the
        // same game (e.g. a "roms/saves" copy AND a "roms" companion
        // file); harmless/redundant otherwise, so always shown rather
        // than only-on-ambiguity, which would need an extra pass to
        // detect and would still surprise the user the first time a
        // second source shows up.
        std::string tag = importedSourceTag(availableGames_[i], importedOccurrence(i));
        if (!tag.empty()) {
            if (tag.length() > 10) tag = tag.substr(0, 9) + ".";
            const auto& te = getTextEntry(tag, fontSmall_, T().text);
            int badgeW = te.w + 8, badgeH = te.h + 4;
            int badgeX = iconX + 2, badgeY = iconY + IS - badgeH - 2;
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
            SDL_Rect badgeRect = {badgeX, badgeY, badgeW, badgeH};
            SDL_RenderFillRect(renderer_, &badgeRect);
            drawText(tag, badgeX + 4, badgeY + 2, T().text, fontSmall_);
        }
    } else {
        // Colored placeholder with game abbreviation
        drawRoundRect(iconX, iconY, IS, IS, 10, T().iconPlaceholder);
        const char* abbr = "";
        switch (availableGames_[i]) {
            case GameType::Sw: abbr = "Sw"; break;
            case GameType::Sh: abbr = "Sh"; break;
            case GameType::BD: abbr = "BD"; break;
            case GameType::SP: abbr = "SP"; break;
            case GameType::LA: abbr = "LA"; break;
            case GameType::S:  abbr = "S";  break;
            case GameType::V:  abbr = "V";  break;
            case GameType::ZA: abbr = "ZA"; break;
            case GameType::GP: abbr = "GP"; break;
            case GameType::GE: abbr = "GE"; break;
            case GameType::FR: case GameType::FR_ES: case GameType::FR_DE: case GameType::FR_IT: case GameType::FR_FR: case GameType::FR_JA: abbr = "FR"; break;
            case GameType::LG: case GameType::LG_ES: case GameType::LG_DE: case GameType::LG_IT: case GameType::LG_FR: case GameType::LG_JA: abbr = "LG"; break;
            default: break;
        }
        drawTextCentered(abbr, iconX + IS / 2, iconY + IS / 2,
                         T().text, font_);
    }
}

// Voci visibili nell'ordine utente + posizioni x centrate (stesso stile
// della vecchia riga fissa: con 3 voci le posizioni coincidono con le
// vecchie PCX/VCX/eject, cosi' tap e memoria muscolare non cambiano).
std::vector<UI::DockSlot> UI::dockLayout() const {
    std::vector<DockSlot> out;
    if (!dockState_.visible) return out;
    std::vector<DockState::Item> vis;
    for (auto it : dockState_.customOrder)
        if (dockStateItemVisible(it)) vis.push_back(it);
    int n = (int)vis.size();
    int x0 = SCREEN_W / 2 - (n - 1) * DOCK_ROW_DX / 2;
    for (int i = 0; i < n; i++) out.push_back({ vis[i], x0 + i * DOCK_ROW_DX });
    return out;
}

void UI::gsSetFocus(GSFocus f, DockState::Item dockItem) {
    gsFocus_ = f;
    if (f == GSFocus::Dock) gsDockItem_ = dockItem;
}

void UI::gsUnfocus(GSFocus f) {
    if (gsFocus_ == f) gsFocus_ = GSFocus::Grid;
}

bool UI::gsDockIs(DockState::Item it) const {
    return gsFocus_ == GSFocus::Dock && gsDockItem_ == it;
}

bool UI::gsChevronActive() const {
    return gsFocus_ == GSFocus::ChevLeft || gsFocus_ == GSFocus::ChevRight;
}

void UI::dockClearFocus() {
    gsUnfocus(GSFocus::Dock);
}

void UI::dockFocusItem(DockState::Item item) {
    gsSetFocus(GSFocus::Dock, item);
}

bool UI::dockFocusedItem(DockState::Item& out) const {
    if (gsFocus_ != GSFocus::Dock) return false;
    out = gsDockItem_;
    return true;
}

bool UI::dockHasFocus() const {
    DockState::Item it;
    return dockFocusedItem(it);
}

bool UI::dockMoveFocus(int dir) {
    auto slots = dockLayout();
    if (slots.empty()) return false;
    DockState::Item cur;
    int idx = -1;
    if (dockFocusedItem(cur)) {
        for (int i = 0; i < (int)slots.size(); i++)
            if (slots[i].item == cur) { idx = i; break; }
    } else {
        // Nessuna voce a fuoco: atterra all'estremo verso cui si va.
        dockFocusItem(slots[dir > 0 ? 0 : (int)slots.size() - 1].item);
        return true;
    }
    // La voce a fuoco e' sparita dal layout (es. chiavetta rimossa):
    // riparti dall'estremo.
    if (idx < 0) {
        dockFocusItem(slots[dir > 0 ? 0 : (int)slots.size() - 1].item);
        return true;
    }
    int nxt = idx + dir;
    if (nxt < 0 || nxt >= (int)slots.size()) return false;
    dockFocusItem(slots[nxt].item);
    return true;
}

bool UI::dockFocusFirst() {
    auto slots = dockLayout();
    if (slots.empty()) return false;
    dockFocusItem(slots[0].item);
    return true;
}

bool UI::dockFocusBanksOrFirst() {
    return dockFocusFirst();
}

bool UI::dockFocusLast() {
    auto slots = dockLayout();
    if (slots.empty()) return false;
    dockFocusItem(slots.back().item);
    return true;
}

// Stesse azioni del tap/conferma sulle vecchie icone fisse (pack -> zaino,
// vault -> tutte le banche, eject -> espelli) + SaveMenu/Trade come il menu
// radiale (openSaveMenu / selectGame+openTradeList).
void UI::dockActivateFocused(bool& running) {
    DockState::Item it;
    if (!dockFocusedItem(it)) return;
    switch (it) {
        case DockState::Item::Backpack:
            openBackpackOn(gameSelCursor_);
            break;
        case DockState::Item::Banks:
            enterAllBanksMode();
            break;
        case DockState::Item::SaveMenu:
            if (gameSelectorLayout_ == GameSelectorLayout::Classic) {
                // Classico: niente gioco evidenziato -> lista popup.
                openGamePick(GamePickTarget::SaveMenu);
            } else if (gameSelCursor_ >= 0 && gameSelCursor_ < (int)availableGames_.size()) {
                openSaveMenu(availableGames_[gameSelCursor_],
                             importedOccurrence(gameSelCursor_));
            }
            break;
        case DockState::Item::Trade: {
            if (gameSelectorLayout_ == GameSelectorLayout::Classic) {
                // Classico: niente gioco evidenziato -> lista popup.
                openGamePick(GamePickTarget::Trade);
                break;
            }
            if (gameSelCursor_ < 0 || gameSelCursor_ >= (int)availableGames_.size()) break;
            AppScreen prevScreen = screen_;
            selectGame(availableGames_[gameSelCursor_], importedOccurrence(gameSelCursor_));
            screen_ = prevScreen;
            openTradeList();
            break;
        }
        case DockState::Item::RemoteBox:
            openRemoteBox();
            break;
        case DockState::Item::DevSync:
            remoteSyncTestRow();
            break;
        case DockState::Item::Eject:
            ejectUsbDevices();
            break;
    }
    (void)running;
}

void UI::drawDock() {
    if (!dockLoaded_) dockStateLoad();
    // Timeout auto-uscita dal riordino (20 s senza tocchi): esce senza salvare.
    if (dockState_.reorderMode && SDL_GetTicks() - dockState_.reorderEnterTime > 20000)
        dockStateExitReorderMode(false);
    if (!dockState_.visible) {
        dockClearFocus();
        for (int i = 0; i < MAX_DOCK_SLOTS; i++) { dockSlide_[i] = 0.0f; dockSlideVel_[i] = 0.0f; }
        return;
    }
    constexpr int R = 34;
    constexpr int ICON_R = 24;
    constexpr int BTN_Y = SCREEN_H - 110;
    auto slots = dockLayout();
    if (slots.empty()) {
        dockClearFocus();
        return;
    }
    // Animazione espelli (come la vecchia riga fissa): il target e' la
    // posizione della voce Eject nel layout, o fuori schermo se nascosta.
    int ejectTarget = SCREEN_W / 2 + 104;
    for (auto& s : slots)
        if (s.item == DockState::Item::Eject) ejectTarget = s.cx;
    float ejectAlphaT =
#ifdef OH_USB_UPDATE
        (dockStateItemVisible(DockState::Item::Eject) ? 255.0f : 0.0f);
#else
        0.0f;
#endif
    if (ejectBtnX_ < 0) ejectBtnX_ = (float)ejectTarget;
    if (ejectAnimStage_ == 1) {
        // Appena espulso: prima sparisce l'eject.
        ejectAlphaT = 0.0f;
        if (ejectBtnA_ == 0.0f) ejectAnimStage_ = 2;
    } else if (ejectAnimStage_ == 2) {
        ejectAlphaT = 0.0f;
        ejectAnimStage_ = 0;
    } else if (ejectAlphaT > 0.0f) {
        ejectAnimStage_ = 0;
    }
    if (ejectBtnA_ == 0 && ejectAlphaT > 0.0f) ejectBtnX_ = (float)ejectTarget + 60.0f; // entra da destra
    auto approach = [](float cur, float tgt) {
        float d = tgt - cur;
        if (d > -1.0f && d < 1.0f) return tgt;
        return cur + d * 0.25f;
    };
    float ne = approach(ejectBtnX_, (float)ejectTarget);
    float na = approach(ejectBtnA_, ejectAlphaT);
    if (ne != ejectBtnX_ || na != ejectBtnA_) markDirty();
    ejectBtnX_ = ne; ejectBtnA_ = na;
    // Effetto molla/budino del riordino: slot inseguono il target.
    dockSpringStep(slots);
    auto drawIcon = [&](SDL_Texture* tex, int cx) {
        SDL_SetTextureColorMod(tex, T().text.r, T().text.g, T().text.b);
        SDL_Rect dst = {cx - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    };
    auto iconFor = [&](DockState::Item it) -> SDL_Texture* {
        switch (it) {
            case DockState::Item::Backpack: return iconPack_;
            case DockState::Item::Banks: return iconVault_;
            case DockState::Item::SaveMenu: return iconFloppy_;
            case DockState::Item::Trade: return iconTrade_;
            case DockState::Item::RemoteBox: return iconDevBox_;
            case DockState::Item::DevSync: return iconDevSync_;
            case DockState::Item::Eject: return iconEject_;
        }
        return nullptr;
    };
    auto focusedFor = [&](DockState::Item it) {
        return gsDockIs(it);
    };
    for (auto& s : slots) {
        int si = (int)(&s - &slots[0]);
        float slideOff = (si < MAX_DOCK_SLOTS) ? dockSlide_[si] : 0.0f;
        int cx = (s.item == DockState::Item::Eject) ? (int)(ejectBtnX_ + 0.5f) : s.cx;
        if (s.item != DockState::Item::Eject)
            cx += (int)(slideOff + 0.5f); // molla/budino sul riordino
        bool focused = focusedFor(s.item);
        // In riordino evidenzia la voce che si sta spostando.
        if (dockState_.reorderMode && (int)(&s - &slots[0]) == dockState_.reorderFocusIdx)
            focused = true;
        if (s.item == DockState::Item::Eject && ejectBtnA_ <= 1.0f) {
            if (gsDockIs(DockState::Item::Eject) && !dockStateItemVisible(DockState::Item::Eject))
                gsUnfocus(GSFocus::Dock); // voce sparita in fade: torna in griglia
            continue; // fade-out: non disegnabile (come prima)
        }
        drawRoundSelect(cx, BTN_Y, R + 1, focused);
        if (s.item == DockState::Item::Backpack) {
            // Zaino: texture gia' a misura (48px), blit 1:1 senza scaling.
            if (iconPack_) {
                SDL_SetTextureColorMod(iconPack_, T().text.r, T().text.g, T().text.b);
                SDL_Rect dst = {cx - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
                SDL_RenderCopy(renderer_, iconPack_, nullptr, &dst);
                SDL_SetTextureColorMod(iconPack_, 255, 255, 255);
            }
        } else if (SDL_Texture* tex = iconFor(s.item)) {
            if (s.item == DockState::Item::Eject) {
                SDL_SetTextureAlphaMod(tex, (Uint8)ejectBtnA_);
                SDL_SetTextureColorMod(tex, T().text.r, T().text.g, T().text.b);
                SDL_Rect dst = {cx - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_SetTextureAlphaMod(tex, 255);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            } else {
                drawIcon(tex, cx);
            }
        }
        if (focused && !dockState_.reorderMode) {
            const char* lblKey = nullptr;
            switch (s.item) {
                case DockState::Item::Backpack: lblKey = StrKey::BackpackTitle; break;
                case DockState::Item::Banks: lblKey = StrKey::ViewAllBanks; break;
                case DockState::Item::SaveMenu: lblKey = StrKey::RadialSaveMenu; break;
                case DockState::Item::Trade: lblKey = StrKey::RadialTrade; break;
                case DockState::Item::RemoteBox: lblKey = StrKey::RemoteBoxMenuLabel; break;
                case DockState::Item::DevSync: lblKey = StrKey::DockDevSync; break;
                case DockState::Item::Eject: lblKey = StrKey::DockEject; break;
            }
            if (lblKey) {
                std::string lbl = i18n::get(lblKey);
                int ly = BTN_Y + R + 19; // un paio di px sotto l'icona, come nel radial
                SDL_Color sh = {0, 0, 0, 220};
                // ombra rinforzata: alone 8 direzioni + leggero offset per staccare dal fondo
                drawTextCentered(lbl, cx + 1, ly + 1, sh, font_);
                drawTextCentered(lbl, cx - 1, ly + 1, sh, font_);
                drawTextCentered(lbl, cx + 1, ly - 1, sh, font_);
                drawTextCentered(lbl, cx - 1, ly - 1, sh, font_);
                drawTextCentered(lbl, cx, ly + 1, sh, font_);
                drawTextCentered(lbl, cx, ly - 1, sh, font_);
                drawTextCentered(lbl, cx + 1, ly, sh, font_);
                drawTextCentered(lbl, cx - 1, ly, sh, font_);
                SDL_Color sh2 = {0, 0, 0, 140};
                drawTextCentered(lbl, cx + 2, ly + 2, sh2, font_);
                drawTextCentered(lbl, cx - 2, ly + 2, sh2, font_);
                drawTextCentered(lbl, cx, ly, T().text, font_);
            }
        }
    }
    if (dockState_.reorderMode) {
        drawTextCentered("Sposta: L/R  Conferma: A  Annulla: B", SCREEN_W / 2,
                         BTN_Y + R + 17, T().textDim, fontSmall_);
    }
}

void UI::drawGameSelectorFrame() {
    SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
    SDL_RenderClear(renderer_);

    if (appletMode_) {
        drawTextCentered(i18n::get(StrKey::SelectGameDual), SCREEN_W / 2, 30, T().text, font_);
        drawTextCentered(i18n::get(StrKey::DualBankHint),
                         SCREEN_W / 2, 55, T().textDim, fontSmall_);
    } else {
        drawTextCentered(i18n::get(StrKey::SelectGame), SCREEN_W / 2, 40, T().text, font_);
    }

    // Avatar utente in alto a sinistra: A torna al selettore profili.
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()) {
        constexpr int AV = 56;
        constexpr int AVX = 36, AVY = 30;
        if (gsFocus_ == GSFocus::Avatar) {
            drawRoundSelect(AVX + AV / 2, AVY + AV / 2, AV / 2 + 1, true);
        }
        SDL_Texture* av = account_.profiles()[selectedProfile_].iconTextureRound;
        if (!av) av = account_.profiles()[selectedProfile_].iconTexture;
        if (av) {
            SDL_Rect dst = {AVX, AVY, AV, AV};
            SDL_RenderCopy(renderer_, av, nullptr, &dst);
        }
        drawText(account_.profiles()[selectedProfile_].nickname, AVX + AV + 10, AVY + 14, T().text, font_);
    }

    // WiFi/LAN in alto a destra come nella barra Switch: pieno se rete on,
    // grigio se off. LAN col cavo mostra la sua icona al posto del wifi.
    {
        std::string link = updateNetLinkStr();
        bool netOn = link != "OFF";
        SDL_Texture* nic = (link == "LAN" && iconLan_) ? iconLan_ : iconWifi_;
        if (nic) {
            if (netOn)
                SDL_SetTextureColorMod(nic, T().text.r, T().text.g, T().text.b);
            else
                SDL_SetTextureColorMod(nic, 110, 110, 110);
        SDL_Rect wdst = {SCREEN_W - 36 - 36, 34, 36, 36};
        SDL_RenderCopy(renderer_, nic, nullptr, &wdst);
        SDL_SetTextureColorMod(nic, 255, 255, 255);
        // Bug debug a sinistra del wifi, solo con debug attivo.
        if (DebugLog::enabled() && iconDebug_) {
            SDL_SetTextureColorMod(iconDebug_, T().text.r, T().text.g, T().text.b);
            SDL_Rect ddst = {SCREEN_W - 36 - 36 - 8 - 36, 34, 36, 36};
            SDL_RenderCopy(renderer_, iconDebug_, nullptr, &ddst);
            SDL_SetTextureColorMod(iconDebug_, 255, 255, 255);
        }
        // Device remoto trovato (worker in background): un altro posto a
        // sinistra del primo libero fra wifi e il bug debug.
        if (remoteDeviceAvailable_ && iconDevLink_) {
            int dlx = SCREEN_W - 36 - 36 - 8 - 36;
            if (DebugLog::enabled() && iconDebug_) dlx -= 8 + 36;
            SDL_SetTextureColorMod(iconDevLink_, T().text.r, T().text.g, T().text.b);
            SDL_Rect rdst = {dlx, 34, 36, 36};
            SDL_RenderCopy(renderer_, iconDevLink_, nullptr, &rdst);
            SDL_SetTextureColorMod(iconDevLink_, 255, 255, 255);
        }
    }
    }

    int totalPages = 1; // Galleria: nessuna paginazione (lista scorrevole unica)
    if (gameSelectorLayout_ == GameSelectorLayout::Gallery) {
        drawGameList_Gallery();
    } else {
    int numGames = (int)availableGames_.size();
    constexpr int COLS = 6;
    constexpr int ROWS_PER_PAGE = 2;
    constexpr int GAMES_PER_PAGE = COLS * ROWS_PER_PAGE;
    constexpr int CARD_W = 160;
    constexpr int CARD_H = 200;
    constexpr int CARD_GAP = 20;
    constexpr int ICON_SIZE = 128;

    totalPages = (numGames + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;
    // Slide orizzontale tipo Switch: la pagina disegnata insegue il target.
    // Fase 1 (selPageShown_ != target): vecchia esce verso -dir.
    // Allo swap l'offset salta sul lato opposto e la nuova rientra verso 0.
    if (selPageShown_ != gameSelPage_) {
        float dir = (gameSelPage_ > selPageShown_) ? -1.0f : 1.0f;
        float mag = std::fabs(selSlide_) + (1.0f - std::fabs(selSlide_)) * 0.3f + 0.02f;
        if (mag >= 1.0f) {
            selPageShown_ = gameSelPage_;
            selSlide_ = -dir; // lato opposto: rientro
        } else {
            selSlide_ = mag * dir;
        }
        markDirty();
    } else if (selSlide_ != 0.0f) {
        // Fase 2: rientro verso 0
        float s = selSlide_ * 0.7f;
        selSlide_ = (std::fabs(s) < 0.02f) ? 0.0f : s;
        markDirty();
    }
    int showPage = selPageShown_;
    int pageStart = showPage * GAMES_PER_PAGE;
    int pageEnd = std::min(pageStart + GAMES_PER_PAGE, numGames);
    int pageCount = pageEnd - pageStart;

    int rows = (pageCount + COLS - 1) / COLS;
    int totalH = rows * CARD_H + (rows - 1) * CARD_GAP;
    int gridStartY = (SCREEN_H - totalH) / 2 - 20; // griglia 20px piu in alto
    // Avanza zoom selezione (step interi via cast: niente aliasing frazionario)
    if (zoomT_ < 1.0f) {
        zoomT_ += 0.2f;
        if (zoomT_ > 1.0f) zoomT_ = 1.0f;
        markDirty();
    }

    for (int i = pageStart; i < pageEnd; i++) {
        int idx = i - pageStart;
        int r = idx / COLS;
        int c = idx % COLS;

        // Center each row: count items in this row
        int rowItems = std::min(COLS, pageCount - r * COLS);
        int rowW = rowItems * CARD_W + (rowItems - 1) * CARD_GAP;
        int rowStartX = (SCREEN_W - rowW) / 2;

        int cardX = rowStartX + c * (CARD_W + CARD_GAP)
                  + (int)(selSlide_ * COLS * (CARD_W + CARD_GAP));
        int cardY = gridStartY + r * (CARD_H + CARD_GAP);

        // Card selezionata ingrandita (zoom animato intero): sfondo e icona
        // crescono, i testi restano centrati (il centro non si sposta).
        bool sel = (i == gameSelCursor_ && gsFocus_ == GSFocus::Grid);
        if (gameSelCursor_ != zoomCard_) {
            zoomPrev_ = zoomCard_;
            zoomCard_ = gameSelCursor_;
            zoomT_ = 0.0f;
        }
        float ze = zoomT_ * zoomT_ * (3 - 2 * zoomT_); // smoothstep
        int grow = 0;
        if (i == zoomCard_ && sel) grow = (int)(zoomGrow_ * ze);
        else if (i == zoomPrev_) grow = (int)(zoomGrow_ * (1.0f - ze));
        if (zoomT_ >= 1.0f) zoomPrev_ = -2;
        int cw = CARD_W + 2 * grow;
        int ch = CARD_H + 2 * grow;
        int cx0 = cardX - grow;
        int cy0 = cardY - grow;
        int IS = ICON_SIZE + 2 * grow;

        // Card background (angoli arrotondati)
        if (sel) {
            drawRoundRect(cx0, cy0, cw, ch, 12, T().menuHighlight);
            drawRoundRectOutline(cx0, cy0, cw, ch, 12, T().cursor, 3);
        } else {
            drawRoundRect(cardX, cardY, CARD_W, CARD_H, 12, T().panelBg);
        }

        // Icon
        int iconX = cx0 + (cw - IS) / 2;
        int iconY = cy0 + 10;

        drawGameArt(i, iconX, iconY, IS);

        // Game name below icon (RSE show the logo on top too, but the
        // text label below stays for readability at a glance).
        // Con lo zoom la card cresce (cx0/cw/cy0/IS): anche scritta e
        // conteggio banche devono scalare/centrarsi sulla card ingrandita,
        // non restare fissi sull'impronta originale.
        {
            std::string name = gameDisplayNameOf(availableGames_[i]);
            if (name.substr(0, 8) == "Pokemon ")
                name = name.substr(8);
            if (name.length() > 20) name = name.substr(0, 19) + ".";
            drawTextCentered(name, cx0 + cw / 2, cy0 + IS + 30,
                             T().text, fontSmall_);
        }

        auto bc = gameBankCounts_.find(availableGames_[i]);
        int bankCount = (bc != gameBankCounts_.end()) ? bc->second : 0;
        std::string bankStr = "(" + std::to_string(bankCount) + ")";
        drawTextCentered(bankStr, cx0 + cw / 2, cy0 + IS + 50,
                         T().textDim, fontSmall_);
    }

    }

    // Riga bassa: dock guidato da dockLayout() (ordine utente, voci
    // condizionali). Con l'ordine di fabbrica coincide con la vecchia riga
    // fissa zaino/banche/eject.
    drawDock();

    // Ingranaggio impostazioni in basso a destra, stessa riga delle banche.
    // Apre lo stesso menu del tasto + (menu dedicato in futuro).
    {
        constexpr int GR = 26;
        int gcx = SCREEN_W - 64;
        int gcy = SCREEN_H - 110;
        drawRoundSelect(gcx, gcy, GR + 1, gsFocus_ == GSFocus::Settings);
        if (iconSettings_) {
            constexpr int SET_R = 20;
            SDL_SetTextureColorMod(iconSettings_, T().text.r, T().text.g, T().text.b);
            SDL_Rect dst = {gcx - SET_R, gcy - SET_R, SET_R * 2, SET_R * 2};
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
            SDL_RenderCopy(renderer_, iconSettings_, nullptr, &dst);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            SDL_SetTextureColorMod(iconSettings_, 255, 255, 255);
        }
    }

    // Frecce pagine: icona diretta senza riquadro (dx = stessa ruotata 180).
    // Grigia se non disponibile, colore tema se attiva, cursor se evidenziata.
    if (totalPages > 1 && iconArrow_) {
        constexpr int AR = 24;
        int midY = SCREEN_H / 2;
        auto arrow = [&](int cx, bool flip, bool can, bool focused) {
            SDL_Color c = !can ? SDL_Color{110, 110, 110, 255}
                        : focused ? T().cursor : T().text;
            SDL_SetTextureColorMod(iconArrow_, c.r, c.g, c.b);
            SDL_Rect dst = {cx - AR, midY - AR, AR * 2, AR * 2};
            SDL_Point ctr = {AR, AR};
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
            SDL_RenderCopyEx(renderer_, iconArrow_, nullptr, &dst,
                             flip ? 180.0 : 0.0, &ctr, SDL_FLIP_NONE);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            SDL_SetTextureColorMod(iconArrow_, 255, 255, 255);
        };
        bool leftFocused = gsFocus_ == GSFocus::ChevLeft;
        bool rightFocused = gsFocus_ == GSFocus::ChevRight;
        arrow(34, false, gameSelPage_ > 0, leftFocused);
        arrow(SCREEN_W - 34, true, gameSelPage_ < totalPages - 1, rightFocused);
    }

    // "X: Espelli USB" suffix only while a device is actually mounted.
    std::string ejectHint;
#ifdef OH_USB_UPDATE
    if (usbHsFsGetMountedDeviceCount() > 0)
        ejectHint = i18n::get(StrKey::StatusGameEject);
#endif
    // Debug: X apre il popup save (backup/restore/send) sul gioco puntato.
    std::string saveHint;
    if (DebugLog::enabled())
        saveHint = " | X: save";
    // "ZL: Avvia" solo in Galleria e solo se il gioco evidenziato e'
    // davvero lanciabile ora -- titolo Switch nativo, oppure emulato con
    // mGBA rilevato e rom trovata accanto al save. Stessa filosofia di
    // ejectHint: compare solo quando il tasto avrebbe un effetto reale.
    std::string launchHint;
    if (gameSelectorLayout_ == GameSelectorLayout::Gallery && !appletMode_ &&
        isGameLaunchableAt(gameSelCursor_)) {
        launchHint = i18n::get(StrKey::StatusGameLaunch);
    }
    if (selectedProfile_ >= 0) {
        drawStatusBar((totalPages > 1 ? i18n::get(StrKey::StatusGameBackPage)
                                      : i18n::get(StrKey::StatusGameBack)) + ejectHint + saveHint + launchHint);
        std::string profileLabel = account_.profiles()[selectedProfile_].nickname;
        profileLabel += " | ";
        profileLabel += useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            profileLabel += " | DBG";
        profileLabel += std::string(" | ") + updateNetLinkStr();
        const auto& e = getTextEntry(profileLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(profileLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    } else {
        drawStatusBar((totalPages > 1 ? i18n::get(StrKey::StatusGameQuitPage)
                                      : i18n::get(StrKey::StatusGameQuit)) + ejectHint + saveHint + launchHint);
        // Show core even without profile so feedback is always visible
        std::string coreLabel = useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            coreLabel += " | DBG";
        coreLabel += std::string(" | ") + updateNetLinkStr();
        const auto& e = getTextEntry(coreLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(coreLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }
    if (appletMode_) {
        std::string modeLabel = std::string(i18n::get(StrKey::DualBankMode)) + " | " + (useOpenHome() ? "OH" : "PK");
        if (DebugLog::enabled())
            modeLabel += " | DBG";
        modeLabel += std::string(" | ") + updateNetLinkStr();
        const auto& e = getTextEntry(modeLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(modeLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }
}

void UI::selectorTap(float px, float py, bool& running) {
    auto dist2 = [](float ax, float ay, float bx, float by) {
        float dx = ax - bx, dy = ay - by;
        return dx * dx + dy * dy;
    };
    // Avatar -> profili
    if (selectedProfile_ >= 0 && px >= 36 && px <= 36 + 56 && py >= 30 && py <= 30 + 56) {
        freeGameIcons();
        account_.unmountSave();
        screen_ = AppScreen::ProfileSelector;
        markDirty();
        return;
    }
    // Dock (riga bassa guidata da dockLayout()) / gear.
    // In riordino il tap non attiva nulla: esce senza salvare.
    if (dockState_.reorderMode) {
        dockStateExitReorderMode(false);
        markDirty();
        return;
    }
    constexpr float BTN_Y = SCREEN_H - 110;
    for (auto& s : dockLayout()) {
        if (s.item == DockState::Item::Eject && ejectBtnA_ <= 128) continue; // in fade: non cliccabile
        if (dist2(px, py, (float)s.cx, BTN_Y) < 45 * 45) {
            dockFocusItem(s.item); // setter unico: azzera avatar/gear/launch/chevron
            dockActivateFocused(running);
            return;
        }
    }
    if (dist2(px, py, SCREEN_W - 64, BTN_Y) < 40 * 40) {
        openSettings(); // il gear apre SEMPRE le impostazioni
        markDirty();
        return;
    }
    // Frecce pagine
    int numGames = (int)availableGames_.size();
    int totalPages = (gameSelectorLayout_ == GameSelectorLayout::Gallery)
                    ? 1 : (numGames + 12 - 1) / 12;
    if (totalPages > 1) {
        if (dist2(px, py, 34, SCREEN_H / 2) < 34 * 34 && gameSelPage_ > 0) {
            gameSelPage_--;
            gameSelCursor_ = gameSelPage_ * 12;
            gsSetFocus(GSFocus::Grid);
            markDirty();
            return;
        }
        if (dist2(px, py, SCREEN_W - 34, SCREEN_H / 2) < 34 * 34 && gameSelPage_ < totalPages - 1) {
            gameSelPage_++;
            gameSelCursor_ = gameSelPage_ * 12;
            gsSetFocus(GSFocus::Grid);
            markDirty();
            return;
        }
    }
    if (gameSelectorLayout_ == GameSelectorLayout::Gallery) {
        selectorTapGallery(px, py, running);
        return;
    }
    // Card giochi (stesso layout del draw)
    constexpr int COLS = 6, CARD_W = 160, CARD_H = 200, CARD_GAP = 20;
    int pageStart = selPageShown_ * 12;
    int pageEnd = std::min(pageStart + 12, numGames);
    int pageCount = pageEnd - pageStart;
    int rows = (pageCount + COLS - 1) / COLS;
    int totalH = rows * CARD_H + (rows - 1) * CARD_GAP;
    int gridStartY = (SCREEN_H - totalH) / 2 - 20;
    for (int i = pageStart; i < pageEnd; i++) {
        int idx = i - pageStart;
        int r = idx / COLS, c = idx % COLS;
        int rowItems = std::min(COLS, pageCount - r * COLS);
        int rowW = rowItems * CARD_W + (rowItems - 1) * CARD_GAP;
        int cardX = (SCREEN_W - rowW) / 2 + c * (CARD_W + CARD_GAP);
        int cardY = gridStartY + r * (CARD_H + CARD_GAP);
        if (px >= cardX && px <= cardX + CARD_W && py >= cardY && py <= cardY + CARD_H) {
            gameSelCursor_ = i;
            gsSetFocus(GSFocus::Grid);
            if (Settings::radialMenu()) {
                openRadialMenu(i);
            } else {
                selectGame(availableGames_[i], importedOccurrence(i));
            }
            markDirty();
            return;
        }
    }
    (void)running;
}

void UI::ejectUsbDevices() {
#ifdef OH_USB_UPDATE
    u32 n = usbHsFsGetMountedDeviceCount();
    if (n > 8) n = 8;
    std::vector<UsbHsFsDevice> devs(n > 0 ? n : 1);
    u32 got = n > 0 ? usbHsFsListMountedDevices(devs.data(), n) : 0;
    int ok = 0;
    for (u32 i = 0; i < got; i++)
        if (usbHsFsUnmountDevice(&devs[i], true)) ok++;
    DebugLog::line("usb eject: unmounted %d/%u device(s)", ok, got);
    rescanImportedGames();
    if (gsDockIs(DockState::Item::Eject)) gsUnfocus(GSFocus::Dock); // l'eject sparisce
    if (ok > 0) ejectAnimStage_ = 1; // sequenza: fade eject, poi rientro vault
    markDirty();
#else
    (void)0;
#endif
}

bool UI::bottomButtonsAnim() {
#ifdef OH_USB_UPDATE
    bool vis = usbHsFsGetMountedDeviceCount() > 0;
#else
    bool vis = false;
#endif
    float et = (float)SCREEN_W / 2 + 104.0f;
    float at = vis ? 255.0f : 0.0f;
    if (ejectBtnX_ < 0) return true;
    if (ejectAnimStage_ != 0) return true;
    if (selSlide_ != 0.0f || selPageShown_ != gameSelPage_) return true;
    if (zoomT_ < 1.0f) return true;
    if (galleryScrollAnim()) return true;
    if (galleryPreviewAnim()) return true;
    // Link rete cambiato: aggiorna l'icona wifi anche a schermo fermo.
    static std::string lastLink;
    std::string link = updateNetLinkStr();
    if (link != lastLink) { lastLink = link; return true; }
    return ejectBtnX_ != et || ejectBtnA_ != at;
}

void UI::sendLogNow() {
    UpdateCfg cfg;
    std::string err;
    if (!readUpdateCfg(basePath_, cfg) || cfg.url.empty()) {
        showMessageAndWait(i18n::get(StrKey::SendLogTitle), i18n::get(StrKey::SendLogNoUrl));
    } else if (!updateNetEnsureReady()) {
        showMessageAndWait(i18n::get(StrKey::SendLogTitle), i18n::get(StrKey::SendLogNetOff));
    } else {
        showWorking(i18n::fmt(StrKey::SendLogUploading, cfg.url));
        bool sentLib = false;
        if (updateNetUploadLog(cfg.url, cfg.token, basePath_, err, &sentLib))
            showMessageAndWait(i18n::get(StrKey::SendLogTitle),
                sentLib ? i18n::get(StrKey::SendLogSentBoth)
                        : i18n::get(StrKey::SendLogSent));
        else
            showMessageAndWait(i18n::get(StrKey::SendLogTitle), i18n::fmt(StrKey::SendLogFailed, err));
    }
}

void UI::handleGameSelectorInput(bool& running) {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    const bool gallerySel_ = (gameSelectorLayout_ == GameSelectorLayout::Gallery);
    const int COLS = gallerySel_ ? 1 : 6;

    const int GAMES_PER_PAGE = gallerySel_ ? numGames : 12;

    int totalPages = (numGames + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;

    auto moveGrid = [&](int dx, int dy) {
        int pageStart = gameSelPage_ * GAMES_PER_PAGE;
        int pageEnd = std::min(pageStart + GAMES_PER_PAGE, numGames);
        int pageCount = pageEnd - pageStart;

        // On a chevron button
        if (gsChevronActive()) {
            if (dx != 0) {
                if (gsFocus_ == GSFocus::ChevLeft && dx > 0) {
                    // Right from left chevron → back to grid col 0
                    gsUnfocus(GSFocus::ChevLeft);
                } else if (gsFocus_ == GSFocus::ChevRight && dx < 0) {
                    // Left from right chevron → back to grid last col
                    gsUnfocus(GSFocus::ChevRight);
                    int localIdx = gameSelCursor_ - pageStart;
                    int row = localIdx / COLS;
                    int rowItems = std::min(COLS, pageCount - row * COLS);
                    gameSelCursor_ = pageStart + row * COLS + rowItems - 1;
                }
            }
            if (dy > 0) {
                gsSetFocus(GSFocus::Grid);
                dockFocusBanksOrFirst(); // giu' dai chevron: banche o prima voce
            }
            if (dy < 0) {
                gsUnfocus(GSFocus::ChevLeft);
                gsUnfocus(GSFocus::ChevRight);
            }
            return;
        }

        if (gsFocus_ == GSFocus::Launch) {
            // Sul tastino "Avvia" del pannello anteprima: sinistra torna
            // alla lista (cursore invariato), destra o giu' continuano
            // verso la prima icona della dock -- "giu'" e' lo stesso gesto
            // che dalla dock riporta su qui (vedi dy < 0 nel blocco dock).
            if (dx < 0) {
                gsUnfocus(GSFocus::Launch);
            } else if (dx > 0 || dy > 0) {
                dockFocusFirst(); // verso la prima icona della dock (azzera Launch)
            }
            return;
        }

        // Dock: navigazione guidata dal layout (l'ordine utente non conta,
        // i gesti restano quelli di sempre). In riordino sx/dx scambiano
        // le voci, su/giu' annullano.
        if (dockHasFocus()) {
            if (dockState_.reorderMode) {
                if (dx != 0) {
                    DockState::Item cur;
                    if (dockFocusedItem(cur)) {
                        auto slots = dockLayout();
                        int idx = -1;
                        for (int i = 0; i < (int)slots.size(); i++)
                            if (slots[i].item == cur) { idx = i; break; }
                        int nxt = idx + dx;
                        if (idx >= 0 && nxt >= 0 && nxt < (int)slots.size()) {
                            // Scambia nell'ordine utente (il layout e'
                            // filtrato per visibilita': gli indici non
                            // coincidono), poi riaggancia il focus.
                            int pi = -1, pj = -1;
                            for (int i = 0; i < (int)dockState_.customOrder.size(); i++) {
                                if (dockState_.customOrder[i] == slots[idx].item) pi = i;
                                if (dockState_.customOrder[i] == slots[nxt].item) pj = i;
                            }
                            if (pi >= 0 && pj >= 0) {
                                // Effetto molla/budino: le due icone partono
                                // dalla posizione dell'altra e ritornano a posto.
                                if (idx < MAX_DOCK_SLOTS && nxt < MAX_DOCK_SLOTS) {
                                    dockSlide_[idx] = (float)((nxt - idx) * DOCK_ROW_DX);
                                    dockSlideVel_[idx] = 0.0f;
                                    dockSlide_[nxt] = (float)((idx - nxt) * DOCK_ROW_DX);
                                    dockSlideVel_[nxt] = 0.0f;
                                }
                                dockStateSwapItems(pi, pj);
                                dockState_.reorderFocusIdx = nxt;
                                // L'icona spostata resta a fuoco sulla sua nuova
                                // posizione (destinazione): mai quella di partenza.
                                dockFocusItem(cur);
                            }
                        }
                    }
                } else if (dy != 0) {
                    dockStateExitReorderMode(false);
                }
                return;
            }
            if (dx > 0) {
                if (!dockMoveFocus(1)) {
                    // Oltre l'ultima voce: ingranaggio (come il vecchio eject->gear).
                    gsSetFocus(GSFocus::Settings);
                }
            } else if (dx < 0) {
                if (!dockMoveFocus(-1)) dockClearFocus(); // prima voce: torna alla lista
            } else if (dy < 0) {
                dockClearFocus();
                if (gallerySel_ && isGameLaunchableAt(gameSelCursor_)) {
                    // Galleria, gioco lanciabile: "su" dalla dock torna al
                    // tastino "Avvia" del pannello anteprima (stesso gesto
                    // simmetrico del "giu'" dal tastino).
                    gsSetFocus(GSFocus::Launch);
                } else if (!gallerySel_) {
                    // Solo Classica: la griglia ha piu' righe, "su" atterra
                    // sull'ultima riga mantenendo la colonna. In Galleria e'
                    // una lista sola: il cursore resta dov'era.
                    int totalRows = (pageCount + COLS - 1) / COLS;
                    int lastRowStart = (totalRows - 1) * COLS;
                    int lastRowItems = pageCount - lastRowStart;
                    int col = (gameSelCursor_ - pageStart) % COLS;
                    if (col >= lastRowItems) col = lastRowItems - 1;
                    gameSelCursor_ = pageStart + lastRowStart + col;
                }
            }
            return;
        }

        if (gsFocus_ == GSFocus::Settings) {
            // Sull'ingranaggio (fuori dock, regole proprie): sinistra torna
            // all'ultima voce visibile della dock, su in griglia.
            if (dx < 0) {
                if (!dockFocusLast()) gsSetFocus(GSFocus::Grid);
            } else if (dy < 0) {
                gsUnfocus(GSFocus::Settings);
                if (!gallerySel_) {
                    // (Galleria: cursore invariato, vedi blocco dock sopra)
                    int totalRows = (pageCount + COLS - 1) / COLS;
                    int lastRowStart = (totalRows - 1) * COLS;
                    int lastRowItems = pageCount - lastRowStart;
                    int col = (gameSelCursor_ - pageStart) % COLS;
                    if (col >= lastRowItems) col = lastRowItems - 1;
                    gameSelCursor_ = pageStart + lastRowStart + col;
                }
            }
            return;
        }

        // Galleria: sinistra/destra scavalcano subito in cima (avatar) o
        // in fondo (riga banche/zaino/eject/gear), cosi' non serve
        // scorrere tutta la lista dei giochi per raggiungerli. Solo
        // quando il cursore e' nella lista stessa: l'avatar (unico
        // elemento periferico non gia' filtrato dai return sopra) ha
        // la sua gestione dx piu' sotto (nessun effetto), invariata.
        if (gallerySel_ && dx != 0 && gsFocus_ != GSFocus::Avatar) {
            if (dx < 0) {
                if (selectedProfile_ >= 0) {
                    gsSetFocus(GSFocus::Avatar);
                } else {
                    dockFocusBanksOrFirst();
                }
            } else {
                // Destra: se il gioco evidenziato e' lanciabile, prima il
                // tastino "Avvia" del pannello anteprima; altrimenti dritti
                // alla prima icona della dock, come prima.
                if (isGameLaunchableAt(gameSelCursor_)) {
                    gsSetFocus(GSFocus::Launch);
                } else {
                    dockFocusFirst();
                }
            }
            return;
        }

        int localIdx = gameSelCursor_ - pageStart;
        int col = localIdx % COLS;
        int row = localIdx / COLS;
        int totalRows = (pageCount + COLS - 1) / COLS;

        col += dx;
        row += dy;

        // Moving down past the last row goes to "All Banks" in Classica;
        // in Galleria va invece alla prima icona della dock (lo zaino),
        // coerente con "destra" dalla lista (vedi blocco piu' sotto).
        if (row >= totalRows) {
            gsUnfocus(GSFocus::Avatar);
            if (gallerySel_) {
                dockFocusFirst();
            } else {
                dockFocusBanksOrFirst();
            }
            return;
        }

        // Navigate to chevrons when going past grid edges (only if page exists)
        if (totalPages > 1) {
            if (col < 0 && gameSelPage_ > 0) {
                gsSetFocus(GSFocus::ChevLeft);
                return;
            }
            int rowItems = std::min(COLS, pageCount - row * COLS);
            if (col >= rowItems && gameSelPage_ < totalPages - 1) {
                gsSetFocus(GSFocus::ChevRight);
                return;
            }
        }

        // Wrap columns within the row (single-page fallback)
        int rowItems = std::min(COLS, pageCount - row * COLS);
        if (rowItems <= 0) rowItems = COLS;
        if (col < 0) col = rowItems - 1;
        if (col >= rowItems) col = 0;

        // Wrap rows (up from top goes to avatar, down from avatar to grid)
        if (gsFocus_ == GSFocus::Avatar) {
            // Giu' o destra tornano alla lista/griglia (posizione invariata).
            if (dy > 0 || dx > 0) gsUnfocus(GSFocus::Avatar);
            return;
        }
        if (row < 0) {
            if (selectedProfile_ >= 0) {
                gsSetFocus(GSFocus::Avatar);
            } else {
                dockFocusBanksOrFirst();
            }
            return;
        }

        int newLocal = row * COLS + col;
        if (newLocal >= pageCount)
            newLocal = pageCount - 1;
        gameSelCursor_ = pageStart + newLocal;
    };

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return;
        }

        // Touch: tap = click, swipe orizzontale = cambio pagina.
        if (event.type == SDL_FINGERDOWN) {
            touchDown_ = true;
            touchMoved_ = false;
            touchStartX_ = event.tfinger.x * SCREEN_W;
            touchStartY_ = event.tfinger.y * SCREEN_H;
            continue;
        }
        if (event.type == SDL_FINGERMOTION && touchDown_) {
            float mdx = event.tfinger.x * SCREEN_W - touchStartX_;
            float mdy = event.tfinger.y * SCREEN_H - touchStartY_;
            if (mdx * mdx + mdy * mdy > 30.0f * 30.0f) touchMoved_ = true;
            continue;
        }
        if (event.type == SDL_FINGERUP && touchDown_) {
            touchDown_ = false;
            float px = event.tfinger.x * SCREEN_W;
            float py = event.tfinger.y * SCREEN_H;
            float dx = px - touchStartX_, dy = py - touchStartY_;
            if (showTradeList_) {
                // popup trade sopra il selettore: tap non deve arrivare al selector
            } else if (showRadialMenu_) {
                if (!touchMoved_) radialMenuTap(px, py, running);
            } else if (!showBackupList_ && !showCrashList_ && !showSaveMenu_ && !showGameSelMenu_ && !showSettings_ && !showBackpack_) {
                if (dx < -120 && std::fabs(dy) < 200) {
                    if (totalPages > 1 && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsSetFocus(GSFocus::Grid);
                        markDirty();
                    }
                } else if (dx > 120 && std::fabs(dy) < 200) {
                    if (totalPages > 1 && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsSetFocus(GSFocus::Grid);
                        markDirty();
                    }
                } else if (!touchMoved_) {
                    selectorTap(px, py, running);
                }
            }
            continue;
        }

        // Trade popup sopra il selettore: intercetta tutto prima degli altri
        if (showTradeList_) {
            handleTradeListInput(event);
            continue;
        }

        // Debug backup list intercepts input (sopra il popup save)
        if (showBackupList_) {
            int count = (int)backupListEntries_.size();
            auto scrollIntoView = [&]() {
                constexpr int VISIBLE = 12;
                if (backupListCursor_ < backupListScroll_)
                    backupListScroll_ = backupListCursor_;
                else if (backupListCursor_ >= backupListScroll_ + VISIBLE)
                    backupListScroll_ = backupListCursor_ - VISIBLE + 1;
            };
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (count > 0) {
                            if (backupListCursor_ > 0) backupListCursor_--;
                            else backupListCursor_ = count - 1;
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (count > 0) {
                            if (backupListCursor_ < count - 1) backupListCursor_++;
                            else backupListCursor_ = 0;
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                        // L = pagina su (-10), mai delete (ZL+ZR è solo trigger)
                        if (count > 0) {
                            backupListCursor_ = std::max(0, backupListCursor_ - 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        // R = pagina giu (+10), mai delete
                        if (count > 0) {
                            backupListCursor_ = std::min(count - 1, backupListCursor_ + 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_B: { // Switch A = restore selezionato
                        if (count == 0) break;
                        std::string e = backupListEntries_[backupListCursor_].path;
                        auto slash = e.find_last_of('/');
                        std::string base = (slash == std::string::npos) ? e : e.substr(slash + 1);
                        if (showConfirmDialog("Restore backup",
                                              base + "\nSovrascrivo il save attuale. Procedo?")) {
                            if (restoreBackupEntry(backupListGame_, e))
                                showMessageAndWait("Restore backup", "OK, save ripristinato.");
                            else
                                showMessageAndWait("Restore backup", "FAILED (vedi debug.log)");
                        }
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_A: // Switch B = indietro
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_BACK:
                    case SDL_CONTROLLER_BUTTON_START:
                        backupListZlHeld_ = backupListZrHeld_ = false;
                        showBackupList_ = false;
                        break;
                }
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                    bool pressed = event.caxis.value > TRIGGER_DEADZONE;
                    bool was = backupListZlHeld_;
                    backupListZlHeld_ = pressed;
                    if (pressed && !was && backupListZrHeld_) {
                        backupListZlHeld_ = backupListZrHeld_ = false;
                        tryDeleteHighlightedBackup();
                    }
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    bool pressed = event.caxis.value > TRIGGER_DEADZONE;
                    bool was = backupListZrHeld_;
                    backupListZrHeld_ = pressed;
                    if (pressed && !was && backupListZlHeld_) {
                        backupListZlHeld_ = backupListZrHeld_ = false;
                        tryDeleteHighlightedBackup();
                    }
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    updateStick(lx, ly);
                }
            }
            continue;
        }

        // Debug crash-report list intercepts input (stesso livello della
        // lista backup: aperta dal menu +, sopra save/backpack/settings)
        if (showCrashList_) {
            int count = (int)crashListEntries_.size();
            auto scrollIntoView = [&]() {
                constexpr int VISIBLE = 12;
                if (crashListCursor_ < crashListScroll_)
                    crashListScroll_ = crashListCursor_;
                else if (crashListCursor_ >= crashListScroll_ + VISIBLE)
                    crashListScroll_ = crashListCursor_ - VISIBLE + 1;
            };
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (count > 0) {
                            if (crashListCursor_ > 0) crashListCursor_--;
                            else crashListCursor_ = count - 1;
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (count > 0) {
                            if (crashListCursor_ < count - 1) crashListCursor_++;
                            else crashListCursor_ = 0;
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                        if (count > 0) {
                            crashListCursor_ = std::max(0, crashListCursor_ - 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        if (count > 0) {
                            crashListCursor_ = std::min(count - 1, crashListCursor_ + 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_B: // Switch A = invia il selezionato
                        if (count > 0)
                            sendCrashReportNow(crashListEntries_[crashListCursor_].path);
                        break;
                    case SDL_CONTROLLER_BUTTON_A: // Switch B = indietro
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_BACK:
                    case SDL_CONTROLLER_BUTTON_START:
                        showCrashList_ = false;
                        break;
                }
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    updateStick(lx, ly);
                }
            }
            continue;
        }

        // Game picker popup (Classica da dock: Scambio/Salvataggi). Intercetta
        // input sopra il save-menu: aperto solo con layout Classic.
        if (showGamePick_) {
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                int count = (int)gamePickAvail_.size();
                constexpr int VISIBLE = 12;
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                    if (count > 0) gamePickCursor_ = (gamePickCursor_ + count - 1) % count;
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                    if (count > 0) gamePickCursor_ = (gamePickCursor_ + 1) % count;
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    // Switch A = conferma: agisci sul gioco scelto.
                    if (count > 0 && gamePickCursor_ < count) {
                        GamePickTarget t = gamePickTarget_;
                        int idx = gamePickAvail_[gamePickCursor_];
                        GameType g = availableGames_[idx];
                        int occ = importedOccurrence(idx);
                        showGamePick_ = false;
                        if (t == GamePickTarget::SaveMenu) {
                            openSaveMenu(g, occ);
                        } else {
                            AppScreen prevScreen = screen_;
                            selectGame(g, occ);
                            screen_ = prevScreen;
                            openTradeList();
                        }
                    }
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_X ||
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK ||
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    // Switch B = indietro
                    showGamePick_ = false;
                }
                // Keep scroll visible.
                if (gamePickCursor_ < gamePickScroll_) gamePickScroll_ = gamePickCursor_;
                else if (gamePickCursor_ >= gamePickScroll_ + VISIBLE)
                    gamePickScroll_ = gamePickCursor_ - VISIBLE + 1;
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    updateStick(lx, ly);
                }
            }
            continue;
        }

        // Debug save popup intercepts input (sotto il menu +)
        if (showSaveMenu_) {
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                int smN = (int)saveMenuRows(saveMenuGame_, sendAvailable()).size();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        saveMenuCursor_ = (saveMenuCursor_ + smN - 1) % smN;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        saveMenuCursor_ = (saveMenuCursor_ + 1) % smN;
                        break;
                    case SDL_CONTROLLER_BUTTON_B: { // Switch A = conferma
                        std::string sel = saveMenuRows(saveMenuGame_, sendAvailable())[saveMenuCursor_];
                        if (sel == "Backup save") {
                            std::string out;
                            showSaveMenu_ = false;
                            if (backupGameSave(saveMenuGame_, out))
                                showMessageAndWait("Save backup", std::string("OK:\n") + out);
                            else
                                showMessageAndWait("Save backup", "FAILED (vedi debug.log)");
                        } else if (sel == "Browse backups") {
                            openBackupList(saveMenuGame_);
                        } else if (sel == "Clean old backups") {
                            // Pulisci: applica il tetto retroattivamente (mai i manuali).
                            bool fb = !importedSavePath(saveMenuGame_, saveMenuOcc_).empty();
                            showSaveMenu_ = false;
                            uint64_t freed = pruneBackupsToCap(saveMenuGame_, fb);
                            char msg[128];
                            std::snprintf(msg, sizeof(msg), "Liberati %.1f MB di auto-backup.",
                                          freed / 1048576.0);
                            showMessageAndWait("Clean old backups", msg);
                        } else if (sel == "Send save") {
                            showSaveMenu_ = false;
                            sendSaveFor(saveMenuGame_, saveMenuOcc_);
                        } else {
                            showSaveMenu_ = false;
                        }
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_A: // Switch B = chiudi
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_BACK:
                    case SDL_CONTROLLER_BUTTON_START:
                        showSaveMenu_ = false;
                        break;
                }
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    updateStick(lx, ly);
                }
            }
            continue;
        }

        // Radial menu (Classica) intercepts input (above backpack/settings)
        if (showRadialMenu_) {
            handleRadialMenuInput(event, running);
            continue;
        }

        // Backpack popup intercepts input (above settings/menu)
        if (showBackpack_) {
            handleBackpackInput(event);
            continue;
        }

        // Settings page intercepts input (above game selector menu)
        if (showSettings_) {
            handleSettingsInput(event, running);
            continue;
        }

        // Game selector menu intercepts input
        if (showGameSelMenu_) {
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: {
                        int ms = (int)gameSelMenuActions().size();
                        gameSelMenuCursor_ = (gameSelMenuCursor_ + ms - 1) % ms;
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
                        int ms = (int)gameSelMenuActions().size();
                        gameSelMenuCursor_ = (gameSelMenuCursor_ + 1) % ms;
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_B: { // Switch A = conferma
                        std::vector<GameSelMenuAction> actions = gameSelMenuActions();
                        if (gameSelMenuCursor_ < 0 || gameSelMenuCursor_ >= (int)actions.size())
                            break;
                        switch (actions[gameSelMenuCursor_]) {
                            case GameSelMenuAction::SwitchCore:
                                g_cryptoEngine = (g_cryptoEngine == CryptoEngine::PK) ? CryptoEngine::OH : CryptoEngine::PK;
                                saveCryptoEngine(basePath_, g_cryptoEngine);
                                showGameSelMenu_ = false;
                                break;
                            case GameSelMenuAction::DebugLog: {
                                // Toggle on/off, resta nel menu per feedback visivo.
                                DebugLog::setEnabled(!DebugLog::enabled());
                                // Il menu può essersi appena rimpicciolito (Send log
                                // sparisce con debug off) — riaggancia il cursore.
                                int ms = (int)gameSelMenuActions().size();
                                if (gameSelMenuCursor_ >= ms) gameSelMenuCursor_ = ms - 1;
                                break;
                            }
                            case GameSelMenuAction::ClearLog: {
                                // Tiene solo le ultime righe: libera spazio SD e
                                // sblocca l'invio quando il log accumulato e'
                                // troppo grande per l'upload (richiesto esplicitamente).
                                constexpr int KEEP_LINES = 200;
                                if (DebugLog::clearLog(KEEP_LINES))
                                    showMessageAndWait(i18n::get(StrKey::ClearLogTitle),
                                        i18n::fmt(StrKey::ClearLogDone, std::to_string(KEEP_LINES)));
                                else
                                    showMessageAndWait(i18n::get(StrKey::ClearLogTitle),
                                        i18n::get(StrKey::ClearLogFailed));
                                break;
                            }
                            case GameSelMenuAction::SendLog:
                                sendLogNow();
                                break;
                            case GameSelMenuAction::SendSave:
                                if (gsDockIs(DockState::Item::Banks) || gsChevronActive() ||
                                    gameSelCursor_ < 0 || gameSelCursor_ >= (int)availableGames_.size())
                                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNoGame));
                                else
                                    sendSaveFor(availableGames_[gameSelCursor_],
                                                importedOccurrence(gameSelCursor_));
                                break;
                            case GameSelMenuAction::CrashReport:
                                showGameSelMenu_ = false;
                                openCrashList();
                                break;
                            case GameSelMenuAction::ImportSettings:
                                showGameSelMenu_ = false;
                                showImportSettings_ = true;
                                importSettingsCursor_ = 0;
                                break;
                            case GameSelMenuAction::CheckUpdate:
                                // Check for a newer NRO and (if found) install + relaunch
                                showGameSelMenu_ = false;
                                if (checkForUpdate()) {
                                    running = false;
                                    return;
                                }
                                break;
                            case GameSelMenuAction::OpenSettings:
                                showGameSelMenu_ = false;
                                openSettings();
                                break;
                            case GameSelMenuAction::RemoteBox:
                                showGameSelMenu_ = false;
                                openRemoteBox();
                                break;
                            case GameSelMenuAction::ToggleDock: {
                                if (!dockLoaded_) dockStateLoad();
                                dockState_.visible = !dockState_.visible;
                                if (!dockState_.visible) dockClearFocus();
                                dockStateSave();
                                showGameSelMenu_ = false;
                                markDirty();
                                break;
                            }
                            case GameSelMenuAction::ReorderDock:
                                showGameSelMenu_ = false;
                                if (!dockLoaded_) dockStateLoad();
                                if (!dockState_.visible) {
                                    dockState_.visible = true;
                                    dockStateSave();
                                }
                                dockStateEnterReorderMode(0);
                                markDirty();
                                break;
                            case GameSelMenuAction::Exit:
                                DebugLog::line("nav: menu Exit -> QUIT");
                                showGameSelMenu_ = false;
                                running = false;
                                return;
                        }
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_A: // Switch B = chiudi menu
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_BACK: // - = cancel
                        showGameSelMenu_ = false;
                        break;
                }
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    updateStick(lx, ly);
                }
            }
            continue;
        }

        if (event.type == SDL_CONTROLLERBUTTONDOWN)
            markDirty();

        if (event.type == SDL_CONTROLLERAXISMOTION) {
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                bool pressed = event.caxis.value > TRIGGER_DEADZONE;
                if (pressed && !favTriggerHeld_ && gallerySel_ && !showGameSelMenu_ && !showSaveMenu_ && !showBackupList_ && !showCrashList_ && !showSettings_ && !showBackpack_ && !showAbout_ && !showThemeSelector_ && !showLanguageSelector_ && gsFocus_ == GSFocus::Grid) {
                    if (gameSelCursor_ >= 0 && gameSelCursor_ < (int)availableGames_.size()) {
                        GameType g = availableGames_[gameSelCursor_];
                        toggleFavorite(g);
                    }
                }
                favTriggerHeld_ = pressed;
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                bool pressed = event.caxis.value > TRIGGER_DEADZONE;
                if (pressed && !launchTriggerHeld_ && gallerySel_ && !showGameSelMenu_ && !showSaveMenu_ && !showBackupList_ && !showCrashList_ && !showSettings_ && !showBackpack_ && !showAbout_ && !showThemeSelector_ && !showLanguageSelector_ && gsFocus_ == GSFocus::Grid) {
                    requestLaunchGame(running);
                }
                launchTriggerHeld_ = pressed;
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                updateStick(lx, ly);
            }
        }

        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    moveGrid(-1, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    moveGrid(1, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    moveGrid(0, -1);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    moveGrid(0, 1);
                    break;
                case SDL_CONTROLLER_BUTTON_B: // Switch A = select
                    if (dockState_.reorderMode) { dockStateExitReorderMode(true); break; } // A conferma il riordino
                    if (gsFocus_ == GSFocus::ChevLeft && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsUnfocus(GSFocus::ChevLeft);
                    } else if (gsFocus_ == GSFocus::ChevRight && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsUnfocus(GSFocus::ChevRight);
                    } else if (dockHasFocus()) {
                        dockActivateFocused(running); // stessa azione del tap
                    } else if (gsFocus_ == GSFocus::Launch) {
                        requestLaunchGame(running);
                    } else if (gsFocus_ == GSFocus::Avatar) {
                        gsUnfocus(GSFocus::Avatar);
                        freeGameIcons();
                        account_.unmountSave();
                        screen_ = AppScreen::ProfileSelector;
                    }
                    else if (gsFocus_ == GSFocus::Settings) {
                        openSettings(); // il gear apre SEMPRE le impostazioni
                    }
                    else if (!gallerySel_ && Settings::radialMenu())
                        openRadialMenu(gameSelCursor_);
                    else
                        selectGame(availableGames_[gameSelCursor_], importedOccurrence(gameSelCursor_));
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                    if (dockState_.reorderMode) { dockStateExitReorderMode(false); break; } // B annulla il riordino
                    if (gsFocus_ == GSFocus::Avatar) { gsUnfocus(GSFocus::Avatar); break; }
                    if (remoteBoxActive_) { closeRemoteBox(); break; } // B esce dal box remoto, torna alla lista locale
                    DebugLog::line("nav: B in games profile=%d -> %s", selectedProfile_,
                        selectedProfile_ >= 0 ? "ProfileSelector" : "QUIT");
                    if (selectedProfile_ >= 0) {
                        freeGameIcons();
                        account_.unmountSave();
                        screen_ = AppScreen::ProfileSelector;
                    } else {
                        // Quit dal selettore giochi: la mano (carry) muore qui.
                        if (!confirmQuitWithHold()) break;
                        running = false;
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_X: // Switch Y = theme
                    if (dockState_.reorderMode) { dockStateExitReorderMode(false); break; }
                    showThemeSelector_ = true;
                    themeSelCursor_ = themeIndex_;
                    themeSelOriginal_ = themeIndex_;
                    break;
                case SDL_CONTROLLER_BUTTON_Y: // Switch X = riordino dock (se icona dock) / save menu (debug) / eject
                    if (dockState_.reorderMode) { dockStateExitReorderMode(false); break; }
                    if (dockHasFocus() && dockState_.visible) {
                        DockState::Item cur;
                        if (dockFocusedItem(cur)) {
                            auto slots = dockLayout();
                            for (int i = 0; i < (int)slots.size(); i++)
                                if (slots[i].item == cur) {
                                    dockStateEnterReorderMode(i);
                                    markDirty();
                                    break;
                                }
                        }
                        break;
                    }
                    if (DebugLog::enabled() && gsFocus_ == GSFocus::Grid &&
                        gameSelCursor_ >= 0 && gameSelCursor_ < (int)availableGames_.size()) {
                        openSaveMenu(availableGames_[gameSelCursor_],
                                     importedOccurrence(gameSelCursor_));
                        break;
                    }
                    ejectUsbDevices();
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: { // L = previous page
                    if (totalPages > 1 && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsSetFocus(GSFocus::Grid);
                    }
                    break;
                }
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: { // R = next page
                    if (totalPages > 1 && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gsSetFocus(GSFocus::Grid);
                    }
                    break;
                }
                case SDL_CONTROLLER_BUTTON_BACK: // - = about
                    if (dockState_.reorderMode) { dockStateExitReorderMode(false); break; }
                    showAbout_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_START: // + : menu rapido se ON, impostazioni se OFF
                    if (dockState_.reorderMode) { dockStateExitReorderMode(false); break; }
                    if (readQuickMenu(basePath_)) {
                        showGameSelMenu_ = true;
                        gameSelMenuCursor_ = 0;
                    } else {
                        openSettings();
                    }
                    break;
            }
        }
    }

    // Joystick repeat navigation
    if (showBackupList_ && stickDirY_ != 0 && !backupListEntries_.empty()) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int count = (int)backupListEntries_.size();
            if (stickDirY_ > 0) {
                if (backupListCursor_ < count - 1) backupListCursor_++;
                else backupListCursor_ = 0;
            } else {
                if (backupListCursor_ > 0) backupListCursor_--;
                else backupListCursor_ = count - 1;
            }
            constexpr int VISIBLE = 12;
            if (backupListCursor_ < backupListScroll_)
                backupListScroll_ = backupListCursor_;
            else if (backupListCursor_ >= backupListScroll_ + VISIBLE)
                backupListScroll_ = backupListCursor_ - VISIBLE + 1;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (showCrashList_ && stickDirY_ != 0 && !crashListEntries_.empty()) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int count = (int)crashListEntries_.size();
            if (stickDirY_ > 0) {
                if (crashListCursor_ < count - 1) crashListCursor_++;
                else crashListCursor_ = 0;
            } else {
                if (crashListCursor_ > 0) crashListCursor_--;
                else crashListCursor_ = count - 1;
            }
            constexpr int VISIBLE = 12;
            if (crashListCursor_ < crashListScroll_)
                crashListScroll_ = crashListCursor_;
            else if (crashListCursor_ >= crashListScroll_ + VISIBLE)
                crashListScroll_ = crashListCursor_ - VISIBLE + 1;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
} else if (showSaveMenu_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int smN = (int)saveMenuRows(saveMenuGame_, sendAvailable()).size();
            saveMenuCursor_ = (saveMenuCursor_ + (stickDirY_ > 0 ? 1 : smN - 1)) % smN;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (showGamePick_ && stickDirY_ != 0 && !gamePickAvail_.empty()) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int count = (int)gamePickAvail_.size();
            if (stickDirY_ > 0) {
                if (gamePickCursor_ < count - 1) gamePickCursor_++;
                else gamePickCursor_ = 0;
            } else {
                if (gamePickCursor_ > 0) gamePickCursor_--;
                else gamePickCursor_ = count - 1;
            }
            constexpr int VISIBLE = 12;
            if (gamePickCursor_ < gamePickScroll_)
                gamePickScroll_ = gamePickCursor_;
            else if (gamePickCursor_ >= gamePickScroll_ + VISIBLE)
                gamePickScroll_ = gamePickCursor_ - VISIBLE + 1;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (showTradeList_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int count = (int)tradeCandidates_.size();
            constexpr int VISIBLE = 6;
            if (count > 0) {
                if (stickDirY_ > 0) {
                    if (tradeCursor_ < count - 1) tradeCursor_++;
                    else tradeCursor_ = 0;
                } else {
                    if (tradeCursor_ > 0) tradeCursor_--;
                    else tradeCursor_ = count - 1;
                }
                if (tradeCursor_ < tradeScroll_) tradeScroll_ = tradeCursor_;
                else if (tradeCursor_ >= tradeScroll_ + VISIBLE) tradeScroll_ = tradeCursor_ - VISIBLE + 1;
            }
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (showGameSelMenu_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int ms = (int)gameSelMenuActions().size();
            gameSelMenuCursor_ = (gameSelMenuCursor_ + (stickDirY_ > 0 ? 1 : ms - 1)) % ms;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (showBackpack_ && (stickDirX_ != 0 || stickDirY_ != 0)) {
        // Mancava del tutto: la levetta ferma non genera nuovi eventi SDL,
        // quindi senza questo tick per-frame lo zaino non scorreva mai a
        // levetta (il d-pad funzionava perche' ogni pressione e' un evento).
        // Verticale: accelera tenendola ferma, stesso schema gia' validato
        // per la lista Galleria (liste lunghe altrimenti lentissime da
        // scorrere). Orizzontale: cambia fuoco catalogo/zaino tramite
        // backpackFocusStep, che e' idempotente quindi il repeat non
        // fa "sbattere" avanti e indietro se la levetta resta inclinata.
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (stickDirY_ != 0 && stickMoved_) {
            uint32_t held = now - stickHoldStart_;
            if (held > 1500) delay = 40;
            else if (held > 800) delay = 80;
            else if (held > 400) delay = 130;
        }
        if (now - stickMoveTime_ >= delay) {
            if (stickDirY_ != 0) backpackStickStep(stickDirY_ > 0 ? 1 : -1);
            if (stickDirX_ != 0) backpackFocusStep(stickDirX_);
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (!showTradeList_ && !showGameSelMenu_ && !showSaveMenu_ && !showBackupList_ && !showCrashList_ && !showSettings_ && !showBackpack_ && !showRadialMenu_ && !showGamePick_ && (stickDirX_ != 0 || stickDirY_ != 0)) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        // Galleria: lista verticale una voce alla volta. Con lo stesso passo
        // fisso della griglia Classica (che avanza per colonne/pagine molto
        // piu' in fretta) scendere fino in fondo a tanti giochi e' lentissimo.
        // Levetta ferma su/giu': dopo un po' accelera progressivamente.
        if (gameSelectorLayout_ == GameSelectorLayout::Gallery && stickDirY_ != 0 && stickMoved_) {
            uint32_t held = now - stickHoldStart_;
            if (held > 1500) delay = 40;
            else if (held > 800) delay = 80;
            else if (held > 400) delay = 130;
        }
        if (now - stickMoveTime_ >= delay) {
            if (stickDirX_ != 0) moveGrid(stickDirX_, 0);
            if (stickDirY_ != 0) moveGrid(0, stickDirY_);
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    }
}

// --- Popup di scoperta "Installa launcher" (categoria Sistema): mostrato
// una sola volta. Stesso schema di noled.cfg -- esistenza file = flag true,
// niente contenuto da leggere/scrivere (a differenza di favorites.cfg qui
// sotto, che invece serializza una lista).
bool UI::hasSeenLauncherPrompt() const {
    return Settings::launcherSeen();
}
void UI::markLauncherPromptSeen() const {
    Settings::setLauncherSeen();
}

// --- Riga "Installa launcher" (categoria Sistema) -----------------------
// Crea davvero il forwarder sul Menu Home (vendor/sphaira/owo.cpp, vedi
// source/forwarder.cpp per il collante). Nessun controllo su appletMode_:
// Sphaira stessa lo crea anche avviata da Album (R su un gioco), quindi
// non e' un prerequisito reale -- coerente con showLauncherPromptPopup(),
// che infatti non lo controlla piu' nemmeno lei (vedi ui.cpp).
void UI::installLauncherForwarder() {
    if (!showConfirmDialog(i18n::get(StrKey::SetInstallLauncher),
                            i18n::get(StrKey::LauncherInstallConfirm))) {
        return;
    }
    attemptLauncherForwarderInstall();
}

// Tentativo vero e proprio: chi chiama ha gia' ottenuto un si (conferma
// esplicita in Impostazioni, oppure A premuto direttamente nel popup di
// scoperta -- li' la domanda e' gia' nel testo del popup stesso, niente
// doppia conferma).
//
void UI::attemptLauncherForwarderInstall() {
    std::string nroPath = basePath_ + "OpenHomeNX.nro";
    struct stat st;
    if (stat(nroPath.c_str(), &st) != 0)
        nroPath = "sdmc:/switch/OpenHomeNX/OpenHomeNX.nro";

    // Chiamata sincrona e potenzialmente lunga (costruzione NCA/RomFS +
    // scrittura su ncm) -- senza showWorking() lo schermo resterebbe fermo
    // come se fosse bloccato, a differenza di ogni altra operazione lunga
    // del programma. Il callback di owo.cpp (via lo shim ProgressBox) puo'
    // aggiornare il messaggio durante l'operazione; se non chiama nulla,
    // resta visibile la label iniziale.
    const std::string title = i18n::get(StrKey::SetInstallLauncher);
    showWorking(title);
    std::string err;
    bool ok = forwarderInstall(nroPath, err,
        [this, &title](const std::string& msg) {
            showWorking(msg.empty() ? title : msg);
        });

    if (ok) {
        showMessageAndWait(i18n::get(StrKey::SetInstallLauncher),
            i18n::get(StrKey::LauncherInstallOk));
    } else {
        // `err` resta un promemoria tecnico minimo: il dettaglio vero
        // (rc/desc/mod) e' gia' nel debug.log, scritto da forwarderInstall()
        // stesso -- niente da ripetere qui. All'utente va sempre e solo il
        // testo gentile.
        showMessageAndWait(i18n::get(StrKey::SetInstallLauncher),
            i18n::get(StrKey::LauncherInstallUnavail));
    }
}

// --- Preferiti galleria (ZR toggle, persistenza favorites.cfg, ordine stabile in cima) ---
void UI::loadFavorites() {
    favorites_.clear();
    std::string p = basePath_ + "favorites.cfg";
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return;
    uint8_t n = 0;
    if (std::fread(&n, 1, 1, f) != 1) { std::fclose(f); return; }
    if (n > GAME_TYPE_COUNT) n = GAME_TYPE_COUNT;
    for (int i = 0; i < n; i++) {
        uint8_t v = 0;
        if (std::fread(&v, 1, 1, f) != 1) break;
        if (v < GAME_TYPE_COUNT) favorites_.insert((int)v);
    }
    std::fclose(f);
}
void UI::saveFavorites() const {
    std::string p = basePath_ + "favorites.cfg";
    if (favorites_.empty()) {
        std::remove(p.c_str());
        return;
    }
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return;
    uint8_t n = (uint8_t)std::min<size_t>(favorites_.size(), GAME_TYPE_COUNT);
    std::fwrite(&n, 1, 1, f);
    for (int v : favorites_) {
        uint8_t b = (uint8_t)v;
        std::fwrite(&b, 1, 1, f);
    }
    std::fclose(f);
}
void UI::applyFavoritesOrder() {
    // Stabile: preferiti in testa mantenendo ordine originale relativo
    std::stable_partition(availableGames_.begin(), availableGames_.end(),
        [&](GameType g){ return isFavorite(g); });
}
// Rilevamento mGBA (v1): una tantum, non ogni frame. Vedi Emulator::findMgba().
void UI::ensureMgbaChecked() {
    if (mgbaChecked_) return;
    mgbaChecked_ = true;
    mgbaPath_ = Emulator::findMgba();
}

// Vero se availableGames_[idx] e' lanciabile ora: stessa logica a due strade
// di requestLaunchGame() (titolo nativo vs emulato via mGBA) ma senza alcun
// effetto -- solo lettura/rilevamento, nessun dialogo, nessun avvio. Usata
// sia dall'hint "ZL: Avvia" sia dal tastino "Avvia" del pannello anteprima
// Galleria, cosi' i due non possano disallinearsi.
bool UI::isGameLaunchableAt(int idx) {
    if (idx < 0 || idx >= (int)availableGames_.size()) return false;
    GameType g = availableGames_[idx];
    bool isTitle = selectedProfile_ >= 0 && titleIdOf(g) >= 0x0100000000010000ULL &&
                   saveFileNameOf(g)[0] != '\0';
    if (isTitle) return true;
    ensureMgbaChecked();
    if (mgbaPath_.empty()) return false;
    std::string sp = importedSavePath(g, importedOccurrence(idx));
    if (sp.empty()) return false;
    return !Emulator::findRomForSave(sp, g).empty();
}

// ---------------------------------------------------------------------------
// Menu radiale (Classica, dietro Settings::radialMenu()). Vedi enum
// RadialAction / membri radial*_ in ui.h. Angoli e raggio-in-funzione-del-
// numero-di-voci ripresi dal mockup HTML validato con l'utente (arco
// 200-340 gradi sopra l'ancora, si allarga aggiungendo voci) -- cosi' e'
// spontaneo aggiungere una quinta icona in futuro: basta una entry in piu'
// in openRadialMenu() e un case in radialMenuActivate(), la geometria si
// aggiusta da sola.
// ---------------------------------------------------------------------------

// Angolo (gradi, convenzione schermo y-down: 0=destra, 90=giu', 180=sinistra,
// 270=su) della voce j su n lungo l'arco. Condivisa fra radialItemCenter()
// (dove disegnare i bottoni) e il puntamento analogico in
// handleRadialMenuInput() (quale voce "punta" lo stick) cosi' restano
// sempre coerenti fra loro.
static float radialItemAngleDeg(int j, int n) {
    // Passo fisso fra voci adiacenti: l'arco totale cresce con n invece di
    // restare fisso, cosi' le icone non si stringono mai fra loro (vedi nota
    // sopra radialItemCenter). Sempre centrato in alto (270).
    constexpr float ANGLE_STEP = 45.0f;
    if (n <= 1) return 270.0f;
    float span = ANGLE_STEP * (n - 1);
    return (270.0f - span / 2.0f) + ANGLE_STEP * j;
}

// Centro del bottone j su n intorno all'ancora (ax, ay). Clampata a
// restare a schermo (le tile di riga 0 altrimenti sforerebbero in alto,
// stesso problema/fix del mockup). TENERE IN SYNC fra draw e tap-hit-test:
// entrambi passano da qui.
static void radialItemCenter(int ax, int ay, int j, int n, int& cx, int& cy) {
    // SCREEN_W/H ridichiarati localmente (sono private in UI, non
    // raggiungibili da una funzione libera) -- stessa convenzione delle
    // altre costanti di layout duplicate per funzione in questo file.
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
    constexpr int EDGE = 12, HALF = 42; // 42 ~= raggio bottone (34) + alone fuoco
    float R = 112.0f; // fisso, un po' piu' distante ora che l'ancora e' il centro vero della card (era 99)
    float rad = radialItemAngleDeg(j, n) * 3.14159265f / 180.0f;
    int x = ax + (int)(R * std::cos(rad));
    int y = ay + (int)(R * std::sin(rad));
    if (x < EDGE + HALF) x = EDGE + HALF;
    if (x > SCREEN_W - EDGE - HALF) x = SCREEN_W - EDGE - HALF;
    if (y < EDGE + HALF) y = EDGE + HALF;
    if (y > SCREEN_H - EDGE - HALF) y = SCREEN_H - EDGE - HALF;
    cx = x; cy = y;
}

void UI::openRadialMenu(int idx) {
    if (idx < 0 || idx >= (int)availableGames_.size()) return;
    constexpr int COLS = 6, CARD_W = 160, CARD_H = 200, CARD_GAP = 20, GAMES_PER_PAGE = 12;
    int numGames = (int)availableGames_.size();
    int pageStart = selPageShown_ * GAMES_PER_PAGE;
    int pageEnd = std::min(pageStart + GAMES_PER_PAGE, numGames);
    int pageCount = pageEnd - pageStart;
    if (pageCount <= 0) return;
    int rows = (pageCount + COLS - 1) / COLS;
    int totalH = rows * CARD_H + (rows - 1) * CARD_GAP;
    int gridStartY = (SCREEN_H - totalH) / 2 - 20;
    int rel = idx - pageStart;
    if (rel < 0 || rel >= pageCount) return; // tile non nella pagina mostrata (difensivo)
    int r = rel / COLS, c = rel % COLS;
    int rowItems = std::min(COLS, pageCount - r * COLS);
    int rowW = rowItems * CARD_W + (rowItems - 1) * CARD_GAP;
    int cardX = (SCREEN_W - rowW) / 2 + c * (CARD_W + CARD_GAP);
    int cardY = gridStartY + r * (CARD_H + CARD_GAP);
    radialAnchorX_ = cardX + CARD_W / 2;
    radialAnchorY_ = cardY + CARD_H / 2; // centro vero della card (non il bordo alto): il menu deve apparire centrato sull'icona e piu' in basso, non sbattere in alto

    radialItems_.clear();
    if (isGameLaunchableAt(idx)) radialItems_.push_back((int)RadialAction::Launch);
    radialItems_.push_back((int)RadialAction::Backpack);
    radialItems_.push_back((int)RadialAction::Bank);
    radialItems_.push_back((int)RadialAction::SaveMenu);
    if (!isDualBankMode() && TradeEvo::supported(availableGames_[idx]))
        radialItems_.push_back((int)RadialAction::Trade);

    radialGameIdx_ = idx;
    radialCursor_ = -1; // nessuna voce a fuoco finche' D-pad o stick non puntano da qualche parte
    radialAnim_ = 0.0f;
    radialClosing_ = false;
    showRadialMenu_ = true;
    markDirty();
}

void UI::closeRadialMenu() {
    if (!showRadialMenu_ || radialClosing_) return;
    radialClosing_ = true; // drawRadialMenu() anima radialAnim_ verso 0
    markDirty();
}

void UI::radialMenuActivate(bool& running) {
    int n = (int)radialItems_.size();
    if (n <= 0) { closeRadialMenu(); return; }
    if (radialCursor_ < 0) return; // niente puntato (analogico al centro): A non fa nulla
    if (radialCursor_ >= n) radialCursor_ = n - 1;
    RadialAction action = (RadialAction)radialItems_[radialCursor_];
    int idx = radialGameIdx_;
    // Chiusura immediata (senza animazione): si passa a un'altra schermata/
    // popup, non ha senso animare la chiusura sotto quello che segue.
    showRadialMenu_ = false;
    radialClosing_ = false;
    radialAnim_ = 0.0f;
    if (idx < 0 || idx >= (int)availableGames_.size()) return;
    switch (action) {
        case RadialAction::Launch:
            gameSelCursor_ = idx;
            requestLaunchGame(running);
            break;
        case RadialAction::Bank:
            selectGame(availableGames_[idx], importedOccurrence(idx));
            break;
        case RadialAction::Backpack:
            openBackpackOn(idx);
            break;
        case RadialAction::SaveMenu:
            openSaveMenu(availableGames_[idx], importedOccurrence(idx));
            break;
        case RadialAction::Trade: {
            // selectGame() naviga anche alla schermata Banca (screen_ =
            // BankSelector) come side-effect del "seleziona questo gioco":
            // per il badge Scambio basta il popup sopra il selettore,
            // senza passare dalla Banca -- ripristino la schermata subito
            // dopo (nei percorsi di errore di selectGame() screen_ non e'
            // mai stato toccato, quindi il ripristino e' innocuo anche li').
            AppScreen prevScreen = screen_;
            selectGame(availableGames_[idx], importedOccurrence(idx));
            screen_ = prevScreen;
            openTradeList();
            break;
        }
    }
    markDirty();
}

void UI::handleRadialMenuInput(const SDL_Event& event, bool& running) {
    if (radialClosing_) return; // lascia finire l'animazione, ignora l'input
    int n = (int)radialItems_.size();
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        markDirty();
        if (n <= 0) { closeRadialMenu(); return; }
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                radialCursor_ = (radialCursor_ < 0) ? (n - 1) : (radialCursor_ + n - 1) % n;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                radialCursor_ = (radialCursor_ < 0) ? 0 : (radialCursor_ + 1) % n;
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = conferma
                radialMenuActivate(running);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = chiudi
            case SDL_CONTROLLER_BUTTON_BACK:
            case SDL_CONTROLLER_BUTTON_START:
                closeRadialMenu();
                break;
        }
    } else if (event.type == SDL_CONTROLLERAXISMOTION) {
        // Puntamento analogico vero: la voce evidenziata segue l'angolo
        // dello stick (stessa convenzione di radialItemAngleDeg), non
        // scatta a sinistra/destra come un tasto digitale. Leggo entrambi
        // gli assi da SDL_GameControllerGetAxis (non solo quello che ha
        // generato l'evento) per avere sempre il vettore 2D completo.
        if (event.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX &&
            event.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY) return;
        if (n <= 0) return;
        float lx = (float)SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
        float ly = (float)SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
        constexpr float DEADZONE = 8000.0f;
        if (lx * lx + ly * ly < DEADZONE * DEADZONE) {
            // Al centro: nessuna voce evidenziata.
            if (radialCursor_ != -1) { radialCursor_ = -1; markDirty(); }
            return;
        }
        float ang = std::atan2(ly, lx) * 180.0f / 3.14159265f;
        if (ang < 0.0f) ang += 360.0f;
        int best = 0;
        float bestDiff = 1e9f;
        for (int j = 0; j < n; j++) {
            float diff = std::fabs(ang - radialItemAngleDeg(j, n));
            if (diff > 180.0f) diff = 360.0f - diff;
            if (diff < bestDiff) { bestDiff = diff; best = j; }
        }
        if (radialCursor_ != best) { radialCursor_ = best; markDirty(); }
    }
}

void UI::radialMenuTap(float px, float py, bool& running) {
    if (radialClosing_) return;
    int n = (int)radialItems_.size();
    for (int j = 0; j < n; j++) {
        int cx, cy;
        radialItemCenter(radialAnchorX_, radialAnchorY_, j, n, cx, cy);
        // dist2 qui e' una lambda locale di un'altra funzione (selectorTap),
        // non un helper condiviso: calcolo la distanza al quadrato inline.
        float ddx = px - (float)cx, ddy = py - (float)cy;
        if (ddx * ddx + ddy * ddy < 42.0f * 42.0f) {
            radialCursor_ = j;
            radialMenuActivate(running);
            markDirty();
            return;
        }
    }
    // Tap fuori da qualunque voce: chiudi, come toccare "fuori" nel mockup.
    closeRadialMenu();
    markDirty();
}

void UI::drawRadialMenu() {
    // Anima l'apertura/chiusura un passo per frame (stesso schema di
    // selSlide_ nel draw della griglia: nessun timer, un incremento fisso
    // a ogni draw finche' non arriva a destinazione).
    if (radialClosing_) {
        radialAnim_ -= 0.16f;
        if (radialAnim_ <= 0.0f) {
            radialAnim_ = 0.0f;
            showRadialMenu_ = false;
            radialClosing_ = false;
            return;
        }
        markDirty();
    } else if (radialAnim_ < 1.0f) {
        radialAnim_ += 0.16f;
        if (radialAnim_ >= 1.0f) radialAnim_ = 1.0f;
        else markDirty();
    }

    // Velo scuro dietro al menu, dosato con l'animazione (niente blur
    // disponibile in SDL2 -- stesso "overlay" di tema usato dagli altri
    // popup, qui pero' sfuma insieme all'apertura invece di essere fisso).
    // La tile aperta resta illuminata (spotlight): il velo si disegna in
    // 4 strisce che la circondano, non su tutto lo schermo.
    //
    // Il buco deve combaciare con la card COME VIENE DISEGNATA DAVVERO dal
    // grid draw: stessa crescita per lo zoom della card selezionata (grow,
    // dal valore Settings Zoom via zoomGrow_, con la stessa easing) e stessi
    // angoli arrotondati (raggio 12, identico a drawRoundRect(...,12,...)
    // nel grid draw) -- non un rettangolo fisso a spigoli vivi.
    constexpr int CARD_W = 160, CARD_H = 200, CARD_R = 12;
    float zEase = zoomT_ * zoomT_ * (3 - 2 * zoomT_); // smoothstep, stessa curva del grid draw
    int grow = 0;
    if (radialGameIdx_ == zoomCard_) grow = (int)(zoomGrow_ * zEase);
    else if (radialGameIdx_ == zoomPrev_) grow = (int)(zoomGrow_ * (1.0f - zEase));
    int cw = CARD_W + 2 * grow, ch = CARD_H + 2 * grow;
    int tileX = radialAnchorX_ - cw / 2;
    int tileY = radialAnchorY_ - CARD_H / 2 - grow; // radialAnchorY_ e' il centro della card, non il suo bordo alto
    if (tileX < 0) tileX = 0;
    if (tileY < 0) tileY = 0;
    SDL_Color ov = T().overlay;
    ov.a = (Uint8)(ov.a * radialAnim_);
    if (tileY > 0) drawRect(0, 0, SCREEN_W, tileY, ov);
    int belowY = tileY + ch;
    if (belowY < SCREEN_H) drawRect(0, belowY, SCREEN_W, SCREEN_H - belowY, ov);
    if (tileX > 0) drawRect(0, tileY, tileX, ch, ov);
    int rightX = tileX + cw;
    if (rightX < SCREEN_W) drawRect(rightX, tileY, SCREEN_W - rightX, ch, ov);
    // Angoli: la card e' arrotondata ma il buco sopra e' ancora un
    // rettangolo a spigoli vivi -- tinteggia nei 4 angoli la parte FUORI dal
    // cerchio di raggio CARD_R (stessa equazione usata da drawRoundRect, qui
    // invertita: coloriamo il di fuori invece del dentro).
    int rr = CARD_R;
    if (rr * 2 > cw) rr = cw / 2;
    if (rr * 2 > ch) rr = ch / 2;
    for (int k = 0; k < rr; k++) {
        int dv = rr - k;
        int dh = (int)std::sqrt((double)(rr * rr - dv * dv));
        int dimW = rr - dh;
        if (dimW <= 0) continue;
        drawRect(tileX, tileY + k, dimW, 1, ov);                      // angolo alto-sinistra
        drawRect(tileX + cw - dimW, tileY + k, dimW, 1, ov);           // angolo alto-destra
        drawRect(tileX, tileY + ch - 1 - k, dimW, 1, ov);              // angolo basso-sinistra
        drawRect(tileX + cw - dimW, tileY + ch - 1 - k, dimW, 1, ov);  // angolo basso-destra
    }

    int n = (int)radialItems_.size();
    if (n <= 0) return;
    float ease = 1.0f - (1.0f - radialAnim_) * (1.0f - radialAnim_) * (1.0f - radialAnim_); // ease-out cubic
    constexpr int BTN_R = 34, ICON_R = 24;
    for (int j = 0; j < n; j++) {
        int tx, ty;
        radialItemCenter(radialAnchorX_, radialAnchorY_, j, n, tx, ty);
        // Parte dal centro della tile (radialAnchorY_ e' il suo bordo alto,
        // ma va benissimo come "centro" apparente per l'effetto zoom) e
        // arriva alla posizione sull'arco.
        int cx = radialAnchorX_ + (int)((tx - radialAnchorX_) * ease);
        int cy = radialAnchorY_ + (int)((ty - radialAnchorY_) * ease);
        int r = (int)(BTN_R * ease);
        int ir = (int)(ICON_R * ease);
        if (r < 1) continue;
        bool focused = (j == radialCursor_);
        drawRoundSelect(cx, cy, r + 1, focused);
        SDL_Texture* tex = nullptr;
        const char* labelKey = nullptr;
        switch ((RadialAction)radialItems_[j]) {
            case RadialAction::Launch:   tex = iconRocket_; labelKey = StrKey::LaunchGameButton; break;
            case RadialAction::Bank:     tex = iconVault_;  labelKey = StrKey::LocBank; break;
            case RadialAction::Backpack: tex = iconPack_;   labelKey = StrKey::BackpackTitle; break;
            case RadialAction::SaveMenu: tex = iconFloppy_; labelKey = StrKey::RadialSaveMenu; break;
            case RadialAction::Trade:    tex = iconTrade_;  labelKey = StrKey::RadialTrade; break;
        }
        if (tex && ir > 0) {
            SDL_SetTextureColorMod(tex, T().text.r, T().text.g, T().text.b);
            SDL_Rect dst = {cx - ir, cy - ir, ir * 2, ir * 2};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        }
        if (focused && radialAnim_ >= 1.0f && labelKey) {
            std::string lbl = i18n::get(labelKey);
            bool below = (labelKey == StrKey::LaunchGameButton || labelKey == StrKey::RadialTrade);
            int ly = below ? cy + BTN_R + 26 : cy - BTN_R - 24;
            SDL_Color sh = {0, 0, 0, 220};
            // ombra rinforzata: alone 8 direzioni + leggero offset per staccare dal fondo
            drawTextCentered(lbl, cx + 1, ly + 1, sh, font_);
            drawTextCentered(lbl, cx - 1, ly + 1, sh, font_);
            drawTextCentered(lbl, cx + 1, ly - 1, sh, font_);
            drawTextCentered(lbl, cx - 1, ly - 1, sh, font_);
            drawTextCentered(lbl, cx, ly + 1, sh, font_);
            drawTextCentered(lbl, cx, ly - 1, sh, font_);
            drawTextCentered(lbl, cx + 1, ly, sh, font_);
            drawTextCentered(lbl, cx - 1, ly, sh, font_);
            SDL_Color sh2 = {0, 0, 0, 140};
            drawTextCentered(lbl, cx + 2, ly + 2, sh2, font_);
            drawTextCentered(lbl, cx - 2, ly + 2, sh2, font_);
            drawTextCentered(lbl, cx, ly, T().text, font_);
        }
    }
}

// Tasto rapido ZL in Galleria: avvia il gioco evidenziato, con conferma
// esplicita. Due strade, mutuamente esclusive per costruzione (un GameType
// e' o titleId-bound o file-backed, mai entrambi -- vedi game_type.h):
//
// - Titolo Switch nativo (titleId reale, es. l'app GBA di Nintendo Switch
//   Online): appletRequestLaunchApplication() e' l'API sanzionata per
//   "avvia questo titolo" (libnx applet.h) -- nessun rilevamento necessario,
//   se c'e' il titleId il sistema sa gia' dove si trova sul disco.
// - Emulato (file-backed, rom scansionata dall'import): invariato, mGBA se
//   rilevato + rom trovata accanto al save (vedi Emulator::findRomForSave).
//
// V1: niente in appletMode_ (salto Album/Library Applet). Per il titolo
// nativo, application_id=0 significa li' "rilancia il titolo corrente"
// (doc libnx) -- un id diverso non e' garantito con certezza in quel
// contesto. Per l'emulato, e' lo stesso limite del chainload mGBA (niente
// nextLoad da un Library Applet). Meglio verificare prima su hardware vero
// che restringere subito: nessun popup per i casi che non si applicano,
// il tasto resta semplicemente muto (stessa filosofia della riga
// "Emulatore predefinito").
void UI::requestLaunchGame(bool& running) {
    if (appletMode_) return;
    if (gameSelCursor_ < 0 || gameSelCursor_ >= (int)availableGames_.size()) return;
    GameType g = availableGames_[gameSelCursor_];

    bool isTitle = selectedProfile_ >= 0 && titleIdOf(g) >= 0x0100000000010000ULL &&
                   saveFileNameOf(g)[0] != '\0';
    if (isTitle) {
        if (!showConfirmDialog(i18n::get(StrKey::LaunchGameTitle),
                                i18n::fmt(StrKey::LaunchGameConfirm, gameDisplayNameOf(g))))
            return;
        Result rc = appletRequestLaunchApplication(titleIdOf(g), nullptr);
        if (R_SUCCEEDED(rc)) {
            running = false;
        } else {
            DebugLog::line("launch: appletRequestLaunchApplication(%016lX) fallita rc=0x%x",
                           titleIdOf(g), rc);
            showMessageAndWait(i18n::get(StrKey::LaunchGameTitle), i18n::get(StrKey::LaunchGameFailed));
        }
        return;
    }

    ensureMgbaChecked();
    if (mgbaPath_.empty()) return;
    int occ = importedOccurrence(gameSelCursor_);
    std::string savePath = importedSavePath(g, occ);
    if (savePath.empty()) return;
    std::string romPath = Emulator::findRomForSave(savePath, g);
    if (romPath.empty()) return; // rom non trovata accanto al save
    if (!showConfirmDialog(i18n::get(StrKey::LaunchGameTitle),
                            i18n::fmt(StrKey::LaunchGameConfirm, gameDisplayNameOf(g))))
        return;
    if (Emulator::launchInMgba(mgbaPath_, romPath))
        running = false;
    else
        showMessageAndWait(i18n::get(StrKey::LaunchGameTitle), i18n::get(StrKey::LaunchGameFailed));
    launchTriggerHeld_ = false;
    favTriggerHeld_ = false;
}

void UI::toggleFavorite(GameType g) {
    int v = static_cast<int>(g);
    if (favorites_.count(v)) favorites_.erase(v);
    else favorites_.insert(v);
    saveFavorites();
    // Riordina mantenendo il gioco toggolato sotto il cursore
    GameType cur = g;
    applyFavoritesOrder();
    for (int i = 0; i < (int)availableGames_.size(); i++) {
        if (availableGames_[i] == cur) { gameSelCursor_ = i; break; }
    }
    // Riallinea scroll/pagina galleria
    if (gameSelectorLayout_ == GameSelectorLayout::Gallery) {
        int numGames = (int)availableGames_.size();
        constexpr int GAL_VISIBLE_ROWS = 9;
        int maxScroll = std::max(0, numGames - GAL_VISIBLE_ROWS);
        int scroll = gameSelCursor_ - GAL_VISIBLE_ROWS / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;
        galScrollX_ = (float)scroll;
    } else {
        gameSelPage_ = gameSelCursor_ / 12;
    }
    markDirty();
}

void UI::enterAllBanksMode() {
    allBanksMode_ = true;
    bankManager_.initAll(basePath_);

    if (bankManager_.list().empty()) {
        allBanksMode_ = false;
        showMessageAndWait(i18n::get(StrKey::NoBanksTitle), i18n::get(StrKey::NoBanksAnyGame));
        return;
    }

    bankSelCursor_ = 0;
    bankSelScroll_ = 0;
    // First pick goes RIGHT, second pick LEFT (original order: inverting it
    // caused a crash loop on A/B toggling in the all-banks list).
    bankSelTarget_ = Panel::Bank;
    leftBankName_.clear();
    leftBankPath_.clear();
    activeBankName_.clear();
    activeBankPath_.clear();

    screen_ = AppScreen::BankSelector;
}

// Consolidate an update left as OpenHomeNX.nro.new by the previous run.
// After envSetNextLoad(.new) + restart we are executing from .new, so the
// canonical .nro is not in use and can be overwritten with a plain copy
// (a rename of the in-use file is what failed before). MUST run at every
// boot, not only when the user opens "Check for update" — otherwise .nro
// stays stale (hbmenu keeps launching the old build) and the residual
// nextLoad keeps relaunching .new, which looks like a double restart.
bool UI::finalizePendingUpdate() {
    const std::string runningNro = basePath_ + "OpenHomeNX.nro";
    const std::string pending = runningNro + ".new";
    struct stat st;
    if (stat(pending.c_str(), &st) != 0)
        return false;                   // nothing pending

    // NOTE: never call envSetNextLoad("", "") to "clear" a pending nextLoad.
    // Setting it to an empty path makes hbloader try to chainload "" on the
    // next exit, which is the fatal-error ("ugly crash") screen the user saw.
    // And a pending nextLoad->.new is NOT consumed by chainloading (verified
    // 2026-09-08: without the bounce every exit relaunched .new) — it must be
    // overwritten with the canonical .nro, which is exactly what the bounce does.

    std::string pendVer;
    if (!readNroDisplayVersion(pending, pendVer)) {
        // NACP non leggibile: NON cancellare (era la causa di "aggiorna, riavvia,
        // ma sono ancora alla vecchia versione" — un read transitorio buttava via
        // l'update). Se il file ha una dimensione plausibile lo finalizziamo lo
        // stesso; lo scartiamo solo se è vuoto/minuscolo o se la copia fallisce.
        if (st.st_size < 1024 * 1024) {
            DebugLog::line("update: pending %s illeggibile e troppo piccolo (%lld B) -> rimuovo",
                           pending.c_str(), (long long)st.st_size);
            std::remove(pending.c_str());
            return false;
        }
        DebugLog::line("update: pending %s NACP illeggibile (%lld B) -> finalizzo comunque",
                       pending.c_str(), (long long)st.st_size);
        pendVer = "?";
        if (copyFileTo(pending, runningNro)) {
            DebugLog::line("update: finalized (blind) %s -> %s", pending.c_str(), runningNro.c_str());
            if (std::remove(pending.c_str()) != 0)
                DebugLog::line("update: .new is the running image, cleaned next boot");
            if (envHasNextLoad()) { envSetNextLoad(runningNro.c_str(), runningNro.c_str()); return true; }
            return false;
        }
        if (envHasNextLoad()) envSetNextLoad(pending.c_str(), pending.c_str());
        return false;
    }

    // Same version in canonical and pending: either a stale leftover (a previous
    // finalize succeeded but .new couldn't be deleted) or a genuine same-version
    // reinstall. Tell them apart WITHOUT trusting byte size (NRO sizes are
    // page-aligned: different builds often compare equal, so size match proves
    // nothing): if .new can be unlinked it was stale — drop it. If not, we ARE
    // the throw-away .new, so fall through and finalize for real (copy + bounce).
    // NOTE: version compare only, never size. Same-version reinstall MUST apply.
    std::string nroVer;
    bool nroReadable = readNroDisplayVersion(runningNro, nroVer);
    if (nroReadable && nroVer == pendVer) {
        if (std::remove(pending.c_str()) == 0) {
            DebugLog::line("update: canonical already v%s, stale .new removed",
                           nroVer.c_str());
            return false;
        }
        DebugLog::line("update: same-version reinstall v%s, finalizing for real (size ignored)",
                       nroVer.c_str());
        // fall through to copyFileTo below: canonical isn't in use from here,
        // so the copy lands; the bounce + next-boot cleanup handle the rest.
    }

    if (copyFileTo(pending, runningNro)) {
        DebugLog::line("update: finalized pending %s -> %s v%s (copy)",
                       pending.c_str(), runningNro.c_str(), pendVer.c_str());
        // Try to drop the sidecar. If we ARE .new (Horizon refuses to unlink
        // the running image) it stays and the next canonical boot removes it
        // via the same-version branch above (remove succeeds from there).
        if (std::remove(pending.c_str()) != 0)
            DebugLog::line("update: .new is the running image, cleaned next boot");
        // Bounce into the canonical .nro (now the new build) behind the
        // "Updating…" mask. This *is* a second restart, but the throw-away
        // .new boot that runs it is stripped to the bone (see main.cpp:
        // no net/USB/text-data/splash), so it's a quick flash, not a full
        // second app launch. The bounce also overwrites the stale
        // nextLoad->.new: without it every exit chainloads .new again
        // (verified 2026-09-08: exit rebooted instead of quitting).
        if (envHasNextLoad()) {
            envSetNextLoad(runningNro.c_str(), runningNro.c_str());
            return true;
        }
        return false;
    }

    // We are running from the canonical .nro (hbloader ignored the nextLoad,
    // or the user relaunched from hbmenu), so it is in use and can't be
    // overwritten. Re-arm the nextLoad so the next restart lands on .new,
    // where this boot-time finalize can consolidate it. Keep .new.
    DebugLog::line("update: canonical %s in use, re-arming nextLoad -> %s",
                   runningNro.c_str(), pending.c_str());
    if (envHasNextLoad()) envSetNextLoad(pending.c_str(), pending.c_str());
    return false;
}

// Called from main() BEFORE net/USB/text-data/splash when OpenHomeNX.nro.new is
// present. Consolidates the update into OpenHomeNX.nro; if a bounce into the
// fresh .nro is armed, flashes the "Updating…" card and returns true so main()
// exits straight away (libnx then chainloads the nextLoad). Returns false when
// there is nothing to bounce (a stale .new was just cleared) — main() then
// continues a normal boot. Needs init() (renderer) already done.
bool UI::tryUpdateBounce(const std::string& basePath) {
    basePath_ = basePath;
    // Card PRIMA della copia finalize (18MB a schermo nero sembravano un hang).
    showWorking(i18n::get(StrKey::UpdateUpdating));
    if (!finalizePendingUpdate())
        return false;
    // finalizePendingUpdate() armed envSetNextLoad(the real .nro). Draw one
    // frame of the card so the chainload isn't a black gap, then let main exit.
    showWorking(i18n::get(StrKey::UpdateUpdating));
    SDL_Delay(150);
    return true;
}

// Prima di scaricare dalla rete, rimuove eventuali .nro stantii lasciati da
// update precedenti — SOLO file che si chiamano esattamente "OpenHomeNX.nro"
// (mai update.nro, mai l'nro in uso). Evita che un vecchio file in update/ o
// in root venga consumato/installato al posto del download fresco.
static void removeStaleLocalUpdates(const std::string& basePath, const std::string& runningNro) {
    std::vector<std::string> paths = {
        basePath + "update/OpenHomeNX.nro",
        "sdmc:/switch/OpenHomeNX/update/OpenHomeNX.nro",
        "sdmc:/OpenHomeNX.nro",
    };
    for (auto& p : paths) {
        if (p == runningNro) continue;
        auto slash = p.rfind('/');
        std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        if (base != "OpenHomeNX.nro") continue; // safety: solo quel basename
        if (std::remove(p.c_str()) == 0)
            DebugLog::line("update: rimosso stale %s", p.c_str());
    }
}

bool UI::checkForUpdate(bool usbOnly) {
    const std::string runningNro = basePath_ + "OpenHomeNX.nro";
    finalizePendingUpdate();
    const std::string curVer =
#ifdef APP_VERSION
        APP_VERSION;
#else
        "0.0.0";
#endif
    DebugLog::line("update: check start, running v%s, base=%s, applet=%d",
                   curVer.c_str(), basePath_.c_str(), (int)appletMode_);

    // Candidate NRO locations, checked in order. USB drives (FAT/exFAT) come
    // first when built with OH_USB_UPDATE; the SD "update/" folder always works.
    std::vector<std::string> candidates;
#ifdef OH_USB_UPDATE
    {
        // Instant check only — never wait/retry here. USB drives are handled
        // by the hotplug poll in run() (which re-scans import paths and, once
        // per session, calls this same function right after a rising edge —
        // by then the drive is already mounted, so n reflects it immediately).
        // Waiting here too used to make the manual "Check for Update" menu
        // entry slow for no reason: this menu isn't when a drive gets
        // detected, only when the user asks "is there an update", and that
        // question should answer from SD/network without a multi-second USB
        // stall (explicit user request 2026-09-05).
        u32 phys = usbHsFsGetPhysicalDeviceCount();
        u32 n = usbHsFsGetMountedDeviceCount();
        DebugLog::line("update: USB physical=%u mounted=%u", phys, n);
        if (DebugLog::enabled() && phys > 0 && n == 0)
            DebugLog::line("update: USB drive seen but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
        if (n > 0) {
            if (n > 8) n = 8;
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            for (u32 i = 0; i < got; i++) {
                DebugLog::line("update: UMS[%u] name='%s' fs=%u cap=%llu",
                               i, devs[i].name, (unsigned)devs[i].fs_type,
                               (unsigned long long)devs[i].capacity);
                std::string mnt = devs[i].name; // e.g. "ums0:"
                candidates.push_back(mnt + "/OpenHomeNX.nro");
                candidates.push_back(mnt + "/switch/OpenHomeNX/OpenHomeNX.nro");
            }
        }
    }
#endif
    // Auto-check USB: solo candidati USB, niente SD e niente rete dopo.
    if (!usbOnly) {
        candidates.push_back(basePath_ + "update/OpenHomeNX.nro");
        candidates.push_back("sdmc:/switch/OpenHomeNX/update/OpenHomeNX.nro");
        // SD root (richiesta utente: butta direttamente in sdmc:/)
        candidates.push_back("sdmc:/OpenHomeNX.nro");
        candidates.push_back("sdmc:/OpenHomeNX/update.nro");
    }
    // Dedup (basePath_ è spesso già sdmc:/switch/OpenHomeNX/) e mai il file in uso.
    {
        std::vector<std::string> uniq;
        for (auto& c : candidates)
            if (c != runningNro && std::find(uniq.begin(), uniq.end(), c) == uniq.end())
                uniq.push_back(c);
        candidates.swap(uniq);
    }

    std::string foundPath, foundVer;
    int foundCmp = 0;
    for (const auto& c : candidates) {
        std::string v;
        bool ok = readNroDisplayVersion(c, v);
        int cmp = ok ? compareVersionStrings(v, curVer) : 0;
        DebugLog::line("update: try '%s' -> read=%d ver='%s' cmp=%d",
                       c.c_str(), (int)ok, ok ? v.c_str() : "", cmp);
        if (ok) {
            foundPath = c;
            foundVer = v;
            foundCmp = cmp;
            if (cmp > 0) break; // prefer newer, ma tieni anche older per prompt
        }
    }
    DebugLog::line("update: result found='%s' v%s cmp=%d",
                   foundPath.empty() ? "(none)" : foundPath.c_str(),
                   foundVer.c_str(), foundCmp);

    if (usbOnly && foundCmp <= 0) {
        // Auto-trigger su inserimento: parla solo se l'USB ha davvero un
        // update (newer). Vuoto/older/same = silenzio totale, niente rete,
        // niente dialoghi informativi.
        DebugLog::line("update: USB auto-check, nothing newer -> silent");
        return false;
    }

    // Layer 1 — sorgente di rete. Solo se nessuna build LOCALE più recente è
    // già stata trovata (una .nro locale più nuova vince senza toccare la rete).
    // URL: update.cfg `url=` se presente, altrimenti le release GitHub
    // pubbliche. Mai in modo usbOnly.
    bool fromNet = false;
    if (!usbOnly && foundCmp <= 0) {
        UpdateCfg cfg;
        readUpdateCfg(basePath_, cfg);
        std::string netUrl;
        if (!cfg.url.empty()) {
            netUrl = cfg.url; // custom vince sempre (anche in beta)
        } else if (cfg.channel == "beta") {
            // Canale beta: prima risolvi la pre-release corrente via API.
            // Mai fallback silenzioso sullo stabile: se fallisce lo dici.
            std::string betaBase, betaTag, betaErr;
            if (!updateNetEnsureReady()) {
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateNetOff, "GitHub beta"));
                return false;
            }
            showWorking(i18n::fmt(StrKey::UpdateContacting, "GitHub beta"));
            if (!updateNetFetchBetaBase("JostenSyon", "OpenHomeNX", cfg.token,
                                        betaBase, betaTag, betaErr)) {
                if (betaErr == "none") {
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateBetaNone, curVer));
                } else {
                    DebugLog::line("update: beta resolve fallito: %s", betaErr.c_str());
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateUnreachable, betaErr, "GitHub beta"));
                }
                return false;
            }
            netUrl = betaBase;
        } else {
            netUrl = githubReleasesUrl("JostenSyon", "OpenHomeNX");
        }
        {
            DebugLog::line("update: net url=%s token=%s", netUrl.c_str(),
                           cfg.token.empty() ? "no" : "yes");
            if (!updateNetEnsureReady()) {
                DebugLog::line("update: rete non disponibile, salto Layer 1");
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateNetOff, updateSourceLabel(netUrl)));
            } else {
                showWorking(i18n::fmt(StrKey::UpdateContacting, updateSourceLabel(netUrl)));
                RemoteUpdateInfo info;
                std::string err;
                if (!updateNetFetchInfo(netUrl, cfg.token, info, err)) {
                    DebugLog::line("update: fetch info fallito: %s", err.c_str());
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateUnreachable, err, updateSourceLabel(netUrl)));
                } else {
                    int cmp = compareVersionStrings(info.version, curVer);
                    DebugLog::line("update: remoto v%s cmp=%d", info.version.c_str(), cmp);
                    if (cmp > 0) {
                        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailNetTitle),
                                i18n::fmt(StrKey::UpdateAvailNetBody, info.version, curVer, updateSourceLabel(netUrl))))
                            return false;
                        removeStaleLocalUpdates(basePath_, runningNro);
                        showWorking(i18n::fmt(StrKey::UpdateDownloading, info.version));
                        const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                        if (!updateNetDownload(info.nroUrl, cfg.token, dst, info.sha256, err,
                                [this](const std::string& s){ showWorking(s); })) {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateDlFailed, err));
                            return false;
                        }
                        foundPath = dst;
                        foundVer = info.version;
                        foundCmp = 1;
                        fromNet = true;
                    } else if (cmp < 0) {
                        // Downgrade: la rete e piu vecchia. Mai silenzioso:
                        // solo debug offre installazione esplicita.
                        DebugLog::line("update: downgrade remoto v%s < v%s",
                            info.version.c_str(), curVer.c_str());
                        if (DebugLog::enabled()) {
                            if (!showConfirmDialog(i18n::get(StrKey::UpdateDowngradeTitle),
                                    i18n::fmt(StrKey::UpdateDowngradeBody, info.version, curVer,
                                              updateSourceLabel(netUrl))))
                                return false;
                            removeStaleLocalUpdates(basePath_, runningNro);
                            showWorking(i18n::fmt(StrKey::UpdateDownloading, info.version));
                            const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                            if (!updateNetDownload(info.nroUrl, cfg.token, dst, info.sha256, err,
                                    [this](const std::string& s){ showWorking(s); })) {
                                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                    i18n::fmt(StrKey::UpdateDlFailed, err));
                                return false;
                            }
                            foundPath = dst;
                            foundVer = info.version;
                            foundCmp = -1;
                            fromNet = true;
                        } else {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateLatestBody, curVer, info.version));
                            return false;
                        }
                    } else {
                        // La rete ha risposto e non c'è niente di più recente:
                        // con debug attivo confronta lo SHA live solo per
                        // dirtelo (stessi bit o no), ma chiede SEMPRE se
                        // reinstallare — mai skip automatico.
                        if (DebugLog::enabled()) {
                            std::string localShort = "?", remoteShort = "?";
                            if (!info.sha256.empty()) {
                                showWorking(i18n::fmt(StrKey::UpdateContacting, "sha…"));
                                std::string local = sha256HexFile(runningNro);
                                localShort = local.empty() ? "?" : local.substr(0, 8);
                                remoteShort = info.sha256.substr(0, 8);
                                DebugLog::line("update: sha local=%s remote=%.16s same=%d",
                                    local.empty() ? "(unreadable)" : local.c_str(),
                                    info.sha256.c_str(), local == info.sha256 ? 1 : 0);
                            }
                            if (showConfirmDialog(i18n::get(StrKey::UpdateSameDbgTitle),
                                    i18n::fmt(StrKey::UpdateSameDbgBody, curVer, localShort,
                                              info.version, remoteShort))) {
                                removeStaleLocalUpdates(basePath_, runningNro);
                                showWorking(i18n::fmt(StrKey::UpdateDownloading, info.version));
                                const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                                if (!updateNetDownload(info.nroUrl, cfg.token, dst, info.sha256, err,
                                        [this](const std::string& s){ showWorking(s); })) {
                                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                        i18n::fmt(StrKey::UpdateDlFailed, err));
                                    return false;
                                }
                                foundPath = dst; foundVer = info.version; foundCmp = 0; fromNet = true;
                            } else {
                                return false;
                            }
                        } else {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateLatestBody, curVer, info.version));
                            return false;
                        }
                    }
                }
            }
        }
    }

    if (foundPath.empty()) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateNoBuildBody, curVer));
        return false;
    }

    if (fromNet) {
        // già confermato prima del download — niente doppio prompt
    } else if (foundCmp > 0) {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailTitle),
                i18n::fmt(StrKey::UpdateAvailBody, foundVer, curVer, foundPath)))
            return false;
    } else if (foundCmp == 0) {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateSameTitle),
                i18n::fmt(StrKey::UpdateSameBody, foundVer, curVer, foundPath)))
            return false;
    } else {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateDowngradeTitle),
                i18n::fmt(StrKey::UpdateDowngradeBody, foundVer, curVer, foundPath)))
            return false;
    }

    showWorking(i18n::get(StrKey::UpdateUpdating));
    const std::string tmp = runningNro + ".new";
    std::remove(tmp.c_str());
    if (!copyFileTo(foundPath, tmp)) {
        std::remove(tmp.c_str());
        showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::get(StrKey::UpdateCopyFailed));
        return false;
    }

    // Consume-once: an update dropped somewhere on the SD (root or an update/
    // folder) is removed now that its bytes are safe in the pending .new file,
    // so it doesn't re-trigger the prompt on every boot. Never touch the
    // running NRO itself, nor files on a USB drive (external master copy).
    if (foundPath != runningNro && foundPath.rfind("sdmc:/", 0) == 0) {
        if (std::remove(foundPath.c_str()) == 0)
            DebugLog::line("update: consumed source %s (removed)", foundPath.c_str());
        else
            DebugLog::line("update: could not remove source %s", foundPath.c_str());
    }
    // Preferred path: overwrite the canonical .nro *in place* from the good
    // copy we just wrote. fopen("wb") truncates+rewrites the same directory
    // entry, so it works even while a forwarder holds the file open — unlike
    // rename()/remove(), which return EBUSY on FAT for the in-use NRO. The
    // running code is already in RAM, so truncating the on-disk file is safe.
    // This gives a SINGLE restart: exit -> forwarder relaunches its target
    // .nro, now the new build. No .new sidecar left to trip a second restart.
    if (copyFileTo(tmp, runningNro)) {
        DebugLog::line("update: overwrote %s in place -> v%s", runningNro.c_str(), foundVer.c_str());
        std::remove(tmp.c_str());
        if (envHasNextLoad()) envSetNextLoad(runningNro.c_str(), runningNro.c_str());
        // Auto-bounce, no button press: mirrors the boot-time finalize screen so
        // the whole update is a couple of "Updating…" frames, not taps.
        showWorking(i18n::fmt(StrKey::UpdateUpdatingTo, foundVer));
        SDL_Delay(700);
        return true;
    }

    // Fallback: in-place overwrite refused. Chainload the .new sidecar and let
    // the boot-time finalizePendingUpdate() consolidate .nro on the next run.
    // On a forwarder this may cost a second restart, but the update still lands.
    DebugLog::line("update: in-place overwrite of %s failed, using .new sidecar", runningNro.c_str());
    if (envHasNextLoad()) {
        envSetNextLoad(tmp.c_str(), tmp.c_str());
        DebugLog::line("update: nextLoad -> %s", tmp.c_str());
        // Auto-bounce. The .new instance's boot-time finalize shows its own
        // brief "Updating…" and bounces again into the real .nro — no taps.
        showWorking(i18n::fmt(StrKey::UpdateUpdatingTo, foundVer));
        SDL_Delay(700);
        return true; // caller stops the loop -> main() returns -> hbloader relaunches
    }
    std::remove(runningNro.c_str());
    if (std::rename(tmp.c_str(), runningNro.c_str()) != 0) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::get(StrKey::UpdateReplaceFailed));
        return false;
    }
    showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::fmt(StrKey::UpdateInstalled, foundVer));
    return false;
}

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

static uint64_t entryDiskSize(const std::string& p) {
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

static bool removeRecursive(const std::string& p) {
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

static std::string readDefaultUser(const std::string& basePath);
int UI::defaultUserIndex() const {
    std::string want = readDefaultUser(basePath_);
    if (want.empty()) return -1;
    const auto& users = account_.profiles();
    for (int i = 0; i < (int)users.size(); i++)
        if (users[i].nickname == want) return i;
    return -1;
}

void UI::openSettings() {
    showSettings_ = true;
    setCat_ = 0;
    setRow_ = 0;
    setFocusLeft_ = true;
    // Aspetto: niente animazione fantasma alla prima apertura -- la molla
    // parte solo se il layout cambia mentre le impostazioni sono aperte.
    appearanceCollapse_ = (gameSelectorLayout_ == GameSelectorLayout::Gallery) ? 1.0f : 0.0f;
    appearanceCollapseVel_ = 0.0f;
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

int UI::settingsRowCount(int cat) const {
    switch (cat) {
        case 0: return 1; // Utente predefinito
        case 1: // Tema, Lingua, Layout selettore, [Zoom, Menu radiale,] Animazione scambio, Dock, Reset dock
            // Zoom e Menu radiale sono voci morte in Galleria (la vedi non li usa):
            // con il layout Galleria la lista si accorcia di 2 righe.
            return (gameSelectorLayout_ == GameSelectorLayout::Gallery) ? 6 : 8;
        case 2: return mgbaPath_.empty() ? 2 : 3; // Sistema: Core + Installa launcher [+ Emulatore predefinito]
        case 3: return 4; // Cartelle, Scansiona, Max, Pulisci
        case 4: {
            // Sorgente/edit custom solo con debug: l'utente normale resta su GitHub.
            int n = 4;
            if (DebugLog::enabled() && hasCustomUrlFile(basePath_)) n = 5;
            return n; // Boot, Check, Sorgente, Canale [, Modifica]
        }
        case 5: {
            // Debug, Menu + [, Pulisci cronologia zaino] [, Normalize save] [, Pulisci cache Galleria] [, Invia log, Crash report], Ricerca dispositivi
            int n = 2;
            if (DebugLog::enabled()) n += 1 + 1 + 1; // + ClearBp, + Normalize, + ClearGalCache
            if (sendAvailable()) n += 2;
            n += 1; // + Ricerca dispositivi (sempre ultima riga, vedi remoteSyncTestRow)
            return n;
        }
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
        // Ricerca dispositivi e' sempre l'ultima riga della categoria,
        // qualunque sia il numero di righe extra sbloccate da debug/rete
        // (vedi settingsRowCount): va controllata per prima o finirebbe per
        // combaciare con uno degli indici fissi sotto quando le righe extra
        // non ci sono tutte.
        if (row == settingsRowCount(5) - 1) return i18n::get(StrKey::DevSyncTitle);
        if (row == 0) return i18n::get(StrKey::SetDebugToggle);
        if (row == 1) return i18n::get(StrKey::SetDbgMenu);
        if (row == 2) return i18n::get(StrKey::ClearBpHistTitle);
        if (row == 3) return normalizeRowLabel();
        if (row == 4) return i18n::get(StrKey::ClearGalCacheTitle);
        if (row == 5) return i18n::get(StrKey::SendLogTitle);
        return i18n::get(StrKey::CrashReportTitle);
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
        return "mGBA"; // riga info: unico emulatore supportato per ora
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
        if (row == 0)
            return DebugLog::enabled() ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        if (row == 1)
            return readQuickMenu(basePath_) ? i18n::get(StrKey::SetOn) : i18n::get(StrKey::SetOff);
        return "";
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
static bool readQuickMenu(const std::string& basePath) {
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

bool UI::remoteSyncEnsureLogin(const std::string& title, std::string& host,
                                std::string& user, std::string& pass, std::string& token) {
    host = Settings::remoteSyncHost();
    user = Settings::remoteSyncUser();
    pass = Settings::remoteSyncPass();
    std::string err;
    bool ok = false;

    // 1) Host gia' noto (stesso device di prima): riprova diretto, e' il
    //    caso comune -- zero attesa di scansione. Se il token è ancora valido
    //    (JWT 2h) lo riusiamo senza rifare login.
    if (!host.empty()) {
        if (remoteSyncGetCachedToken(host, token)) {
            ok = true;
            DebugLog::line("remote sync: token cached riusato per %s (no login)", host.c_str());
        } else {
            ok = remoteSyncLogin(host, user, pass, token, err);
            if (!ok) {
                DebugLog::line("remote sync: login FALLITO su host noto %s: %s", host.c_str(), err.c_str());
            }
        }
        if (ok) {
            // Login ok (o token riusato) ma potrebbe essere un altro device (router che risponde
            // 200 con token fake) -- verifico subito che la root sia listabile,
            // altrimenti invalido e passo alla scansione (gestisce IP cambiato
            // es. 192.168.4.5 → 192.168.4.28).
            std::vector<RemoteEntry> probeEntries;
            std::string probeErr;
            if (!remoteSyncListPath(host, token, "", probeEntries, probeErr)) {
                DebugLog::line("remote sync: host noto %s login ok ma root non listabile (%s) → invalido, provo scansione", host.c_str(), probeErr.c_str());
                ok = false;
            }
        }
    }

    // 2) Host sconosciuto o non risponde piu' (es. IP cambiato via DHCP):
    //    scansione automatica della LAN che prova il login vero su OGNI
    //    host che risponde sulla porta 80, non solo il primo -- su una rete
    //    con piu' web-server (router, NAS, ecc.) il primo a rispondere e'
    //    quasi sempre il router e non e' un Filebrowser: fermarsi li' era
    //    il bug segnalato (login sempre rifiutato, nessun modo di andare
    //    oltre). Si ferma solo al primo che risponde 200 al login.
    //    Annullabile con B (vedi remote_sync.cpp).
    std::string lastScanErr;
    if (!ok) {
        std::string scanErr, foundHost, foundToken;
        // Popup con barra di avanzamento durante lo scan (che resta
        // sincrono -- nessun thread nuovo, vedi il commento su onProgress
        // in remote_sync.h): stesso schema gia' usato per il progresso di
        // download degli aggiornamenti (vedi le chiamate a showWorking nel
        // flusso di UpdateNetDownload piu' sotto in questo file), cosi'
        // l'utente vede la ricerca avanzare invece di uno schermo fermo
        // per i secondi che dura.
        if (remoteSyncScanLan(user, pass, foundHost, foundToken, scanErr,
                               [this](const std::string& s) { showWorking(s); })) {
            host = foundHost;
            token = foundToken;
            ok = true;
        } else {
            DebugLog::line("remote sync: scansione LAN senza esito: %s", scanErr.c_str());
            lastScanErr = scanErr;
        }
    }

    // 3) Ancora niente: si chiede l'host a mano -- ma solo se l'utente vuole.
    //    Prima la scansione spammava subito IP/user/pass anche se l'utente
    //    aveva appena premuto B per annullare. Ora chiediamo conferma.
    if (!ok) {
        // Se la scansione è stata annullata con B, torna subito senza chiedere altro
        if (lastScanErr.find("annullato") != std::string::npos) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled));
            return false;
        }
        if (!showConfirmDialog(title, "Scansione non ha trovato dispositivi.\nVuoi inserire un IP fisso manualmente?")) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled));
            return false;
        }
        std::string typed = promptTextBlocking(i18n::get(StrKey::DevSyncHostPrompt), host, 63);
        if (typed.empty()) { showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled)); return false; }
        host = typed;
        ok = remoteSyncLogin(host, user, pass, token, err);
    }

    // 4) ...poi le credenziali, solo se anche l'host appena confermato (che
    //    sia quello scansionato o quello digitato) rifiuta user/pass default.
    if (!ok) {
        user = promptTextBlocking(i18n::get(StrKey::DevSyncUserPrompt), user, 31);
        if (user.empty()) { showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled)); return false; }
        pass = promptTextBlocking(i18n::get(StrKey::DevSyncPassPrompt), "", 31);
        ok = remoteSyncLogin(host, user, pass, token, err);
    }

    if (!ok) {
        showMessageAndWait(title, i18n::fmt(StrKey::DevSyncLoginFailed, err));
        DebugLog::line("remote sync: login FALLITO (host=%s): %s", host.c_str(), err.c_str());
        return false;
    }

    // Login riuscito: salva sempre host/credenziali che hanno funzionato --
    // che fossero gia' salvati, trovati dalla scansione o appena digitati --
    // cosi' la prossima volta si riparte dal passo 1 (istantaneo).
    Settings::setRemoteSyncHost(host);
    Settings::setRemoteSyncUser(user);
    Settings::setRemoteSyncPass(pass);
    return true;
}

// Picker per scegliere il gioco tra i candidati trovati sul R36S.
// Ritorna indice del candidato scelto o -1 se annullato. Popup bloccante
// con lista + highlight, stesso stile dei menu impostazioni (round rect).
int UI::pickRemoteSyncGame(const std::vector<SyncCandidate>& candidates) {
    if (!renderer_ || candidates.empty()) return -1;
    markDirty();
    int sel = 0;
    int result = -2; // -2 = picking, -1 = cancel, >=0 = picked
    const int POP_W = 640, POP_H = 420;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    // Analog stick debounce
    uint32_t lastStickMs = 0;
    while (result == -2) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = -1;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    sel = (sel - 1 + (int)candidates.size()) % (int)candidates.size();
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    sel = (sel + 1) % (int)candidates.size();
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) // Switch A = conferma
                    result = sel;
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B = annulla
                    result = -1;
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    uint32_t now = SDL_GetTicks();
                    if (now - lastStickMs > 180) {
                        if (event.caxis.value < -12000) {
                            sel = (sel - 1 + (int)candidates.size()) % (int)candidates.size();
                            lastStickMs = now;
                        } else if (event.caxis.value > 12000) {
                            sel = (sel + 1) % (int)candidates.size();
                            lastStickMs = now;
                        }
                    }
                }
            }
        }
        // Popup overlay (non full-screen) come drawSaveMenuPopup
        drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
        drawRect(popX, popY, POP_W, POP_H, T().panelBg);
        drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
        drawTextCentered(i18n::get(StrKey::DevSyncPickerTitle), popX + POP_W / 2, popY + 18, T().text, fontLarge_);
        const int ROW_H = 44, listY = popY + 60;
        int vis = std::min<int>((int)candidates.size(), 7);
        for (int i = 0; i < vis; i++) {
            int idx = i;
            // Se ci sono più di 7, centra la selezione (semplice windowing)
            if ((int)candidates.size() > 7) {
                int start = std::clamp(sel - 3, 0, (int)candidates.size() - 7);
                idx = start + i;
                if (idx >= (int)candidates.size()) break;
            }
            const auto& c = candidates[idx];
            const GameInfo& gi = gameInfo(c.type);
            int rowY = listY + i * ROW_H;
            if (idx == sel) {
                drawRoundRect(popX + 12, rowY, POP_W - 24, ROW_H - 6, 8, T().menuHighlight);
                drawRoundRectOutline(popX + 12, rowY, POP_W - 24, ROW_H - 6, 8, T().cursor, 2);
            }
            std::string label = gi.displayName;
            bool isSwitchLocal = c.hasLocal && c.localPath.rfind("save:/", 0) == 0;
            label += isSwitchLocal ? " [SW]" : (c.hasLocal ? " [ROM]" : " [R36S]");
            drawText(label, popX + 28, rowY + 10, T().text, font_);
        }
        drawTextCentered(i18n::get(StrKey::DevSyncPickerHint), popX + POP_W / 2, popY + POP_H - 24, T().textDim, fontSmall_);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    markDirty();
    return result;
}

// Overlay con 3 grossi bottoni arrotondati (stesso stile Trade/Bank).
// Ritorna 0=Invia, 1=Ricevi, 2=Sincronizza, -1=annulla. Blocca con loop eventi.
int UI::pickRemoteSyncAction(const SyncCandidate& c) {
    if (!renderer_) return -1;
    markDirty();
    const GameInfo& gi = gameInfo(c.type);
    int sel = 0;
    // Determina quali azioni sono sensate (per disabilitare visivamente)
    bool canSend = c.hasLocal;
    bool canReceive = c.hasRemoteSave;
    // Sincronizza ha senso solo se entrambi presenti (check identità fatto dopo)
    bool canSync = c.hasLocal && c.hasRemoteSave;
    int result = -2;
    const int POP_W = 520, POP_H = 360;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    const int BTN_W = 340, BTN_H = 56, BTN_R = 12;
    const int BTN_X = popX + (POP_W - BTN_W) / 2;
    // Label brevi (senza {0}) per i bottoni
    const char* labels[3] = { StrKey::DevSyncActionSend, StrKey::DevSyncActionReceive, StrKey::DevSyncActionSync };
    bool enabled[3] = { canSend, canReceive, canSync };
    // Parti dal primo abilitato (se "Invia" è disabilitato vai su "Ricevi")
    for (int k = 0; k < 3; k++) if (enabled[k]) { sel = k; break; }
    auto nextSel = [&](int dir) {
        for (int step = 1; step <= 3; step++) {
            int cand = (sel + dir * step + 3) % 3;
            if (enabled[cand]) { sel = cand; break; }
        }
    };
    uint32_t lastStickMs = 0;
    while (result == -2) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = -1;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    nextSel(-1);
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    nextSel(1);
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) { // Switch A
                    if (enabled[sel]) result = sel;
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B
                    result = -1;
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    uint32_t now = SDL_GetTicks();
                    if (now - lastStickMs > 180) {
                        if (event.caxis.value < -12000) { nextSel(-1); lastStickMs = now; }
                        else if (event.caxis.value > 12000) { nextSel(1); lastStickMs = now; }
                    }
                }
            }
        }
        drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
        drawRect(popX, popY, POP_W, POP_H, T().panelBg);
        drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
        drawTextCentered(i18n::fmt(StrKey::DevSyncActionTitle, gi.displayName), popX + POP_W / 2, popY + 18, T().text, font_);
        for (int i = 0; i < 3; i++) {
            int y = popY + 70 + i * (BTN_H + 18);
            bool focused = (i == sel);
            SDL_Color bg = focused ? T().menuHighlight : T().bg;
            SDL_Color fg = enabled[i] ? T().text : T().textDim;
            if (!enabled[i] && focused) bg = T().panelBg;
            drawRoundRect(BTN_X, y, BTN_W, BTN_H, BTN_R, bg);
            drawRoundRectOutline(BTN_X, y, BTN_W, BTN_H, BTN_R, focused ? T().cursor : T().textDim, focused ? 2 : 1);
            std::string txt = i18n::get(labels[i]);
            if (!enabled[i]) txt += " (--)";
            // Centra testo nel bottone
            auto e = getTextEntry(txt, font_, fg);
            drawText(txt, BTN_X + (BTN_W - e.w) / 2, y + (BTN_H - e.h) / 2, fg, font_);
        }
        drawTextCentered("A: scegli  B: annulla", popX + POP_W / 2, popY + POP_H - 22, T().textDim, fontSmall_);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    markDirty();
    return result;
}

void UI::remoteSyncTestRow() {
    std::string title = i18n::get(StrKey::DevSyncTitle);
    if (!updateNetEnsureReady()) {
        showMessageAndWait(title, i18n::get(StrKey::SendLogNetOff));
        return;
    }

    std::string host, user, pass, token;
    if (!remoteSyncEnsureLogin(title, host, user, pass, token))
        return;

    // "Monta" il device per il resto della UI (icona barra di stato, icone
    // dock RemoteBox/DevSync via dockStateItemVisible): senza questo, la
    // scansione manuale da qui trova il device e lo usa per la sync ma lo
    // lascia "invisibile" altrove, perche' quei campi li scrive solo il
    // worker in background al boot (vedi ui.cpp). Stesso aggiornamento,
    // solo replicato qui.
    remoteDeviceAvailable_ = true;
    remoteDeviceHost_ = host;
    remoteDeviceToken_ = token;
    markDirty();

    // Elenco locale (bank/import) da confrontare con quello remoto -- stessa
    // scansione gia' usata dal selettore giochi, nessuna logica duplicata.
    // Aggiunge anche i save Switch montati (FRLG Switch) che altrimenti
    // verrebbero visti come "solo ROM" e non offrirebbero l'invio.
    std::vector<ImportedGame> localGames = scanImportPaths(importPaths_, autoCheckUsb_);
    {
        // Switch saves: presentApplications() + hasSaveData() -> se il gioco
        // Switch esiste, considera che ha un save locale anche se non c'è un
        // file in sdmc:/roms/. Il path per il sync sarà gestito come file
        // temporaneo estratto dal mount save:/ (compatibile perché il formato
        // save è identico tra Switch e ROM per FRLG).
        std::set<uint64_t> present = account_.presentApplications();
        for (GameType g : {GameType::FR, GameType::LG, GameType::FR_ES, GameType::LG_ES, GameType::FR_DE, GameType::LG_DE, GameType::FR_IT, GameType::LG_IT, GameType::FR_FR, GameType::LG_FR, GameType::FR_JA, GameType::LG_JA}) {
            if (!present.count(titleIdOf(g))) continue;
            // Controlla se c'è almeno un profilo con save per questo gioco
            bool hasAny = false;
            for (int p = 0; p < account_.profileCount(); p++) {
                if (account_.hasSaveData(p, g)) { hasAny = true; break; }
            }
            if (!hasAny) continue;
            // Se non è già presente come file-backed, aggiungilo come Switch save
            bool already = false;
            for (auto& ig : localGames) if (ig.type == g) { already = true; break; }
            if (!already) {
                ImportedGame ig;
                ig.type = g;
                ig.filePath = std::string("save:/") + saveFileNameOf(g); // marker per Switch save
                ig.sourceTag = "Switch";
                localGames.push_back(ig);
                DebugLog::line("remote sync: Switch save aggiunto per %s (%s)", gameInfo(g).displayName, ig.filePath.c_str());
            }
        }
    }

    std::string buildErr;
    std::vector<SyncCandidate> candidates = remoteSyncBuildCandidates(host, token, localGames, buildErr);
    if (candidates.empty()) {
        showMessageAndWait(title, i18n::get(StrKey::DevSyncNoCandidates));
        DebugLog::line("remote sync: nessun candidato su %s", host.c_str());
        return;
    }

    // Cartella per gli eventuali download di controllo/ricezione -- creata al
    // volo, mai fatale se fallisce (i download successivi falliranno da soli
    // e verranno segnalati normalmente).
    std::string tmpDir = basePath_ + "remote_sync_tmp/";
    mkdir(tmpDir.c_str(), 0755);

    int sent = 0, received = 0, skipped = 0, failed = 0;

    // NOTA: se localPath e' in realta' un marker "save:/..." (save Switch
    // nativo), il percorso reale e' gia' stato risolto una sola volta,
    // subito dopo aver scelto il candidato -- vedi "switchSaveRealPath" /
    // "switchLocalPath" piu' sotto. Qui non si guarda piu' save_/dirty_:
    // vedi il commento estenso sopra remoteSyncTestRow() per i due bug che
    // questo evita (save di un altro gioco inviato per errore; dirty_
    // azzerato per un save scorrelato dalla sync).
    auto doUpload = [&](const std::string& localPath, const std::string& remotePath) -> bool {
        std::string opErr;
        bool okUp = remoteSyncUpload(host, token, localPath, remotePath, opErr);
        if (okUp) sent++; else failed++;
        DebugLog::line("remote sync: invia %s -> %s (%s)", localPath.c_str(),
                       okUp ? "OK" : "FALLITO", opErr.c_str());
        return okUp;
    };
    // Scrive srcFile nel save Switch nativo di "gtype": mount in scrittura ->
    // copia byte grezzi -> commitSave() esplicito, il cui risultato e' SEMPRE
    // controllato (senza commitSave() Horizon scarta la scrittura all'unmount,
    // stesso bug gia' corretto altrove in questo file -- vedi il commento su
    // UI::restoreBackupEntry piu' sopra) -> unmount sempre, anche se la
    // copia/commit e' fallita. Mai scrittura diretta su "save:/..." (che non
    // e' montato quando arriviamo qui).
    auto writeToNativeSave = [&](const std::string& srcFile, GameType gtype) -> bool {
        const char* tag = gameInfo(gtype).gameTag;
        int profileIdx = -1;
        if (selectedProfile_ >= 0 && account_.hasSaveData(selectedProfile_, gtype))
            profileIdx = selectedProfile_;
        else {
            for (int p = 0; p < account_.profileCount(); p++)
                if (account_.hasSaveData(p, gtype)) { profileIdx = p; break; }
        }
        if (profileIdx < 0) {
            DebugLog::line("remote sync: nessun profilo con save Switch per %s (scrittura)", tag);
            return false;
        }
        std::string mnt = account_.mountSave(profileIdx, gtype);
        if (mnt.empty()) {
            DebugLog::line("remote sync: mountSave (scrittura) FALLITO per %s (profilo %d)", tag, profileIdx);
            return false;
        }
        std::string dst = mnt + saveFileNameOf(gtype);
        bool okCopy = false;
        {
            std::ifstream in(srcFile, std::ios::binary);
            if (in.is_open()) {
                std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out << in.rdbuf();
                    okCopy = out.good();
                }
            }
        }
        bool okCommit = okCopy && account_.commitSave();
        if (okCopy && !okCommit)
            DebugLog::line("remote sync: commitSave FALLITO dopo scrittura save nativo %s", tag);
        account_.unmountSave();
        return okCommit;
    };
    auto doDownload = [&](const std::string& remotePath, const std::string& localPath, GameType gtype) -> bool {
        std::string opErr;
        bool okDown;
        if (localPath.rfind("save:/", 0) == 0) {
            // Save Switch nativo: scarica in un tmp, poi scrivilo nel mount
            // nativo con writeToNativeSave() -- mai in scrittura diretta sul
            // marker "save:/..." (non montato in questo momento).
            std::string tmpRecv = tmpDir + std::string(gameInfo(gtype).gameTag) + "_switch_recv.tmp";
            okDown = remoteSyncDownload(host, token, remotePath, tmpRecv, opErr);
            if (okDown && !writeToNativeSave(tmpRecv, gtype)) {
                okDown = false;
                opErr = "scrittura save nativo fallita";
            }
            std::remove(tmpRecv.c_str());
        } else {
            okDown = remoteSyncDownload(host, token, remotePath, localPath, opErr);
        }
        if (okDown) received++; else failed++;
        DebugLog::line("remote sync: ricevi %s -> %s (%s)", remotePath.c_str(),
                       okDown ? "OK" : "FALLITO", opErr.c_str());
        return okDown;
    };
    // Copia locale pura (nessuna rete): usata quando il file remoto e' gia'
    // stato scaricato per il controllo allenatore/TID (tmpPath) e la
    // sincronizzazione lo conferma come la copia da tenere -- riscaricarlo
    // sarebbe una richiesta di rete identica e inutile.
    auto copyLocalAsReceived = [&](const std::string& srcTmp, const std::string& dstLocal) -> bool {
        std::ifstream in(srcTmp, std::ios::binary);
        bool okCopy = false;
        if (in.is_open()) {
            std::ofstream out(dstLocal, std::ios::binary | std::ios::trunc);
            if (out.is_open()) {
                out << in.rdbuf();
                okCopy = out.good();
            }
        }
        if (okCopy) received++; else failed++;
        DebugLog::line("remote sync: sincronizza (copia da verifica gia' scaricata) %s -> %s (%s)",
                       srcTmp.c_str(), dstLocal.c_str(), okCopy ? "OK" : "FALLITO");
        return okCopy;
    };

    // Nuovo flusso: picker gioco + 3 bottoni (evita spam). B nel picker torna
    // alla schermata precedente, B nei 3 bottoni torna al picker (1 passo indietro).
    SyncCandidate c;
    const GameInfo* giPtr = nullptr;
    int action = -1;
    while (true) {
        int pickedIdx = pickRemoteSyncGame(candidates);
        if (pickedIdx < 0) return;
        c = candidates[pickedIdx];
        giPtr = &gameInfo(c.type);
        action = pickRemoteSyncAction(c);
        if (action < 0) continue; // B nei bottoni -> torna al picker
        break;
    }
    const GameInfo& gi = *giPtr;

    // Se il candidato locale e' un save Switch nativo (marker "save:/...",
    // aggiunto per FRLG quando manca un file in sdmc:/roms/), qui non c'e'
    // ancora nessun mount reale: risolvilo UNA SOLA VOLTA in un file
    // temporaneo con i byte grezzi letti dal mount nativo del gioco ESATTO
    // scelto (c.type) -- mount -> copia file-a-file -> unmount immediato,
    // mai attraverso save_/dirty_ (vedi nota su doUpload piu' sopra). Usato
    // sia per l'invio (doUpload) sia per il controllo identita' nel flusso
    // "Sincronizza" piu' sotto; il tmp viene ripulito in fondo alla funzione.
    std::string switchSaveRealPath;
    if (c.hasLocal && c.localPath.rfind("save:/", 0) == 0) {
        int profileIdx = -1;
        if (selectedProfile_ >= 0 && account_.hasSaveData(selectedProfile_, c.type))
            profileIdx = selectedProfile_;
        else {
            for (int p = 0; p < account_.profileCount(); p++)
                if (account_.hasSaveData(p, c.type)) { profileIdx = p; break; }
        }
        if (profileIdx < 0) {
            DebugLog::line("remote sync: nessun profilo con save Switch per %s", gi.gameTag);
        } else {
            std::string mnt = account_.mountSave(profileIdx, c.type);
            if (mnt.empty()) {
                DebugLog::line("remote sync: mountSave FALLITO per %s (profilo %d)", gi.gameTag, profileIdx);
            } else {
                std::string src = mnt + saveFileNameOf(c.type);
                std::string dst = tmpDir + std::string(gi.gameTag) + "_switch_native.tmp";
                // Chiudi gli stream PRIMA di unmountSave(): distruggere un
                // ifstream ancora aperto su un device smontato ("save")
                // abortisce in _close_r (Data Abort su null, visto su HW).
                {
                    std::ifstream in(src, std::ios::binary);
                    if (in.is_open()) {
                        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) {
                            out << in.rdbuf();
                            if (out.good()) switchSaveRealPath = dst;
                        }
                    }
                }
                account_.unmountSave();
                DebugLog::line("remote sync: save Switch nativo %s risolto in tmp (%s): %s",
                               gi.gameTag, switchSaveRealPath.empty() ? "FALLITO" : "OK", dst.c_str());
            }
        }
    }
    std::string switchLocalPath = switchSaveRealPath.empty() ? c.localPath : switchSaveRealPath;

    // Logica centralizzata per le 3 azioni — il save deve avere esattamente lo stesso base della ROM
    auto getExtLocal = [](const std::string& p) -> std::string {
        size_t dot = p.find_last_of('.');
        return (dot == std::string::npos) ? std::string(".sav") : p.substr(dot);
    };
    auto getBaseLocal = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of('/');
        std::string f = (slash == std::string::npos) ? p : p.substr(slash + 1);
        size_t dot = f.find_last_of('.');
        return (dot == std::string::npos) ? f : f.substr(0, dot);
    };
    // Helper per trovare la ROM locale corrispondente al save (solo per file-backed gb/gbc/gba/nds + FRLG)
    // Mai stat() su marker "save:/" (save Switch non montato qui): su Horizon
    // ha causato crash. Per i marker si usa remoteRomBaseName/gameTag.
    auto findLocalRom = [&](const std::string& savePath, GameType type) -> std::string {
        if (!(isFRLG(type) || isImportedFile(type) || isGen1File(type) || isGen2File(type) || isGen4File(type) || isGen5File(type)))
            return std::string();
        if (savePath.rfind("save:/", 0) == 0) return std::string();
        size_t slash = savePath.find_last_of('/');
        std::string dir = (slash == std::string::npos) ? "" : savePath.substr(0, slash + 1);
        std::string file = (slash == std::string::npos) ? savePath : savePath.substr(slash + 1);
        size_t dot = file.find_last_of('.');
        std::string base = (dot == std::string::npos) ? file : file.substr(0, dot);
        const char* exts[] = {".gba",".gbc",".gb",".nds"};
        for (auto ext : exts) {
            std::string cand = dir + base + ext;
            struct stat st2;
            if (stat(cand.c_str(), &st2) == 0 && S_ISREG(st2.st_mode)) return cand;
        }
        return std::string();
    };
    // Costruisci il path remoto del save facendo combaciare esattamente il base con la ROM
    // Se il locale e' un marker save:/ non usare il suo base (es. "FireRed_i"):
    // usa la ROM remota o il gameTag.
    std::string wantRomBase;
    bool localIsSaveMarker = c.localPath.rfind("save:/", 0) == 0;
    if (!c.remoteRomBaseName.empty()) wantRomBase = c.remoteRomBaseName;
    else if (!localIsSaveMarker) {
        std::string lr = findLocalRom(c.localPath, c.type);
        if (!lr.empty()) wantRomBase = getBaseLocal(lr);
        else if (!c.localPath.empty()) wantRomBase = getBaseLocal(c.localPath);
        else wantRomBase = std::string(gameInfo(c.type).gameTag);
    } else {
        wantRomBase = std::string(gameInfo(c.type).gameTag);
    }
    for (char& ch : wantRomBase) if (ch == '/' || ch == '\\') ch = '_';
    // Estensioni corrette: Switch (locale) usa .sav per file-backed, R36S (remoto) usa .srm/.dsv
    auto getLocalSaveExtFor = [&](GameType t) -> std::string {
        if (isGen4File(t) || isGen5File(t)) return ".sav"; // NDS su Switch
        if (isFRLG(t) || isImportedFile(t) || isGen1File(t) || isGen2File(t)) return ".sav";
        return ".sav";
    };
    auto getRemoteSaveExtFor = [&](GameType t) -> std::string {
        if (isGen4File(t) || isGen5File(t)) return ".dsv"; // DraStic su R36S
        if (isFRLG(t) || isImportedFile(t) || isGen1File(t) || isGen2File(t)) return ".srm";
        return ".sav";
    };
    std::string localExtWanted = getLocalSaveExtFor(c.type);
    std::string remoteExtWanted = getRemoteSaveExtFor(c.type);
    // Se il save remoto esistente ha base diversa dalla ROM, usa il base della ROM per far combaciare
    std::string remoteTargetPath;
    if (c.hasRemoteSave) {
        std::string curSaveBase = getBaseLocal(c.remoteSavePath);
        std::string curLower = curSaveBase, wantLower = wantRomBase;
        for (char& ch : curLower) ch = (char)std::tolower((unsigned char)ch);
        for (char& ch : wantLower) ch = (char)std::tolower((unsigned char)ch);
        if (curLower != wantLower && !wantRomBase.empty()) {
            size_t slash = c.remoteSavePath.find_last_of('/');
            std::string dir = (slash == std::string::npos) ? c.remoteDir : c.remoteSavePath.substr(0, slash + 1);
            if (dir.empty()) dir = c.remoteDir;
            remoteTargetPath = dir + wantRomBase + remoteExtWanted;
        } else {
            // Mantieni il path esistente ma assicurati l'estensione sia quella corretta per il remoto (.srm/.dsv)
            std::string curExt = getExtLocal(c.remoteSavePath);
            if (curExt != remoteExtWanted) {
                size_t slash = c.remoteSavePath.find_last_of('/');
                std::string dir = (slash == std::string::npos) ? "" : c.remoteSavePath.substr(0, slash + 1);
                remoteTargetPath = dir + curSaveBase + remoteExtWanted;
            } else {
                remoteTargetPath = c.remoteSavePath;
            }
        }
        } else {
            remoteTargetPath = c.remoteDir + wantRomBase + remoteExtWanted;
        }
    bool didSomething = false;
    if (action == 0) { // Invia
        if (!c.hasLocal) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else if (showConfirmDialog(i18n::fmt(StrKey::DevSyncSendTitle, gi.displayName), i18n::get(StrKey::DevSyncSendOnlyLocalBody))) {
            DebugLog::line("remote sync: invia confermato %s -> %s", switchLocalPath.c_str(), remoteTargetPath.c_str());
            bool ok = doUpload(switchLocalPath, remoteTargetPath);
            // Se sul remoto manca la ROM e il gioco è file-backed, invia anche la ROM
            if (ok && !c.hasRemoteRom) {
                std::string localRom = findLocalRom(c.localPath, c.type);
                if (!localRom.empty()) {
                    size_t dot = localRom.find_last_of('.');
                    std::string ext = (dot == std::string::npos) ? ".gba" : localRom.substr(dot);
                    std::string romBase = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                    // Pulisci romBase
                    for (char& ch : romBase) if (ch == '/' || ch == '\\') ch = '_';
                    std::string remoteRomPath = c.remoteDir + romBase + ext;
                    std::string romErr;
                    if (remoteSyncUpload(host, token, localRom, remoteRomPath, romErr)) {
                        DebugLog::line("remote sync: ROM inviata %s -> %s", localRom.c_str(), remoteRomPath.c_str());
                    } else {
                        DebugLog::line("remote sync: ROM non inviata %s: %s", localRom.c_str(), romErr.c_str());
                    }
                }
            }
            didSomething = ok;
        }
    } else if (action == 1) { // Ricevi
        if (!c.hasRemoteSave) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else {
            std::string body = c.hasLocal ? i18n::get(StrKey::DevSyncChooseReceiveBody) : i18n::get(StrKey::DevSyncReceiveOnlyRemoteBody);
            if (showConfirmDialog(i18n::fmt(StrKey::DevSyncReceiveTitle, gi.displayName), body)) {
                bool ok = false;
                // Helper per estrarre estensione da un path (include punto)
                auto getExt = [](const std::string& p) -> std::string {
                    size_t dot = p.find_last_of('.');
                    if (dot == std::string::npos) return std::string(".sav");
                    return p.substr(dot);
                };
                if (c.hasLocal) {
                    // Quando ricevi su un save esistente, assicurati che il nome del save
                    // corrisponda esattamente al nome della ROM (stessa base). Se differiscono,
                    // rinomina il save locale per far combaciare con la ROM.
                    std::string localRom = findLocalRom(c.localPath, c.type);
                    std::string wantBase;
                    if (!localRom.empty()) {
                        size_t slash = localRom.find_last_of('/');
                        std::string romFile = (slash == std::string::npos) ? localRom : localRom.substr(slash + 1);
                        size_t dot = romFile.find_last_of('.');
                        wantBase = (dot == std::string::npos) ? romFile : romFile.substr(0, dot);
                    } else if (!c.remoteRomBaseName.empty()) {
                        wantBase = c.remoteRomBaseName;
                    }
                    std::string destPath = c.localPath;
                    if (!wantBase.empty()) {
                        size_t slash = destPath.find_last_of('/');
                        std::string dir = (slash == std::string::npos) ? "" : destPath.substr(0, slash + 1);
                        std::string ext = getLocalSaveExtFor(c.type);
                        std::string curBase;
                        {
                            std::string file = (slash == std::string::npos) ? destPath : destPath.substr(slash + 1);
                            size_t dot = file.find_last_of('.');
                            curBase = (dot == std::string::npos) ? file : file.substr(0, dot);
                        }
                        if (curBase != wantBase) {
                            destPath = dir + wantBase + ext;
                            DebugLog::line("remote sync: rinomino save locale per match ROM: %s -> %s", c.localPath.c_str(), destPath.c_str());
                        } else {
                            // Usa estensione corretta per Switch (.sav) anche se remoto era .srm
                            size_t dot2 = destPath.find_last_of('.');
                            std::string curExt = (dot2 == std::string::npos) ? "" : destPath.substr(dot2);
                            if (curExt != ext) destPath = dir + wantBase + ext;
                        }
                    }
                    ok = doDownload(c.remoteSavePath, destPath, c.type);
                    // Se il destPath è diverso dal vecchio c.localPath e il download è ok, rimuovi il vecchio file orfano
                    if (ok && destPath != c.localPath) {
                        std::remove(c.localPath.c_str());
                        DebugLog::line("remote sync: vecchio save rimosso %s", c.localPath.c_str());
                    }
                    if (ok) { rescanImportedGames(); markDirty(); }
                } else {
                    // Per file-backed (gba/gbc/gb/nds + FRLG) salva in sdmc:/roms/ insieme alla ROM,
                    // non nella cartella dell'app. Per gli altri usa il primo import path.
                    std::string localDest;
                    bool fileBacked = isFRLG(c.type) || isImportedFile(c.type) || isGen1File(c.type) || isGen2File(c.type) || isGen4File(c.type) || isGen5File(c.type);
                    if (fileBacked) localDest = "sdmc:/roms/";
                    else localDest = importPaths_.empty() ? (basePath_ + "import/") : importPaths_.front().path;
                    if (!localDest.empty() && localDest.back() != '/') localDest += "/";
                    mkdir(localDest.c_str(), 0755);
                    std::string baseName = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                    // Pulisci baseName da caratteri non validi per filesystem locale se serve
                    std::string safeBase = baseName;
                    for (char& ch : safeBase) if (ch == '/' || ch == '\\') ch = '_';
                    std::string ext = getLocalSaveExtFor(c.type);
                    std::string saveDest = localDest + safeBase + ext;
                    ok = doDownload(c.remoteSavePath, saveDest, c.type);
                    // Se manca il gioco (hasLocal==false) e c'è una ROM remota, chiedi se scaricare anche la ROM
                    // così il save diventa subito utilizzabile. Il save deve avere esattamente lo stesso base della ROM.
                    if (ok && c.hasRemoteRom) {
                        // Controlla se la ROM locale già esiste (con lo stesso base)
                        std::string romCheckPath = localDest + safeBase + ".gba";
                        bool romExists = false;
                        {
                            // Prova le estensioni note per vedere se una ROM con quel base esiste già
                            const char* tryExts[] = {".gba",".gbc",".gb",".nds"};
                            for (auto ext : tryExts) {
                                std::string cand = localDest + safeBase + ext;
                                struct stat st2;
                                if (stat(cand.c_str(), &st2) == 0) { romExists = true; break; }
                            }
                        }
                        if (!romExists) {
                            if (showConfirmDialog(i18n::get(StrKey::DevSyncAskRomTitle), i18n::fmt(StrKey::DevSyncAskRomBody, safeBase))) {
                                std::vector<RemoteEntry> dirEntries;
                                std::string listErr;
                                if (remoteSyncListPath(host, token, c.remoteDir, dirEntries, listErr)) {
                                    std::string romFile;
                                    std::string wantBaseLower = safeBase;
                                    for (char& ch : wantBaseLower) ch = (char)std::tolower((unsigned char)ch);
                                    for (auto& e : dirEntries) {
                                        if (e.isDir) continue;
                                        if (!remoteSyncIsRomFileName(e.name)) continue;
                                        std::string baseLower = e.name;
                                        for (char& ch : baseLower) ch = (char)std::tolower((unsigned char)ch);
                                        size_t dot = baseLower.find_last_of('.');
                                        std::string baseOnly = (dot == std::string::npos) ? baseLower : baseLower.substr(0, dot);
                                        if (baseOnly == wantBaseLower) { romFile = e.name; break; }
                                    }
                                    if (!romFile.empty()) {
                                        std::string romDest = localDest + safeBase + romFile.substr(romFile.find_last_of('.'));
                                        std::string romErr;
                                        if (remoteSyncDownload(host, token, c.remoteDir + romFile, romDest, romErr)) {
                                            DebugLog::line("remote sync: ROM ricevuta %s -> %s", (c.remoteDir + romFile).c_str(), romDest.c_str());
                                        } else {
                                            DebugLog::line("remote sync: ROM non ricevuta %s: %s", romFile.c_str(), romErr.c_str());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                didSomething = ok;
                if (ok) {
                    // Aggiorna subito la lista giochi senza dover riavviare l'app
                    rescanImportedGames();
                    markDirty();
                }
                if (!ok) failed = 1; // assicurati che il riepilogo mostri fallito
            }
        }
    } else if (action == 2) { // Sincronizza
        if (!c.hasLocal || !c.hasRemoteSave) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else {
            // Verifica identità allenatore prima di confrontare date (come prima)
            DebugLog::line("remote sync: sincronizza controllo identita' %s...", gi.gameTag);
            std::string tmpPath = tmpDir + gi.gameTag + "_check.tmp";
            std::string dlErr;
            bool tmpOk = remoteSyncDownload(host, token, c.remoteSavePath, tmpPath, dlErr);
            std::string localOt;
            bool sameIdentity = false;
            // localProbe/remoteProbe dichiarati qui (non dentro l'if sotto)
            // perche' servono anche piu' avanti per il confronto
            // playtime/dex -- stesso oggetto gia' caricato per il controllo
            // identita', zero costo di rete o parsing in piu'.
            SaveFile localProbe, remoteProbe;
            if (tmpOk) {
                localProbe.setGameType(c.type);
                remoteProbe.setGameType(c.type);
                if (localProbe.load(switchLocalPath) && remoteProbe.load(tmpPath)) {
                    localOt = localProbe.dsOtName();
                    sameIdentity = !localOt.empty() && localOt == remoteProbe.dsOtName() && localProbe.dsTid() == remoteProbe.dsTid();
                }
            } else {
                DebugLog::line("remote sync: download di controllo fallito per %s: %s", gi.gameTag, dlErr.c_str());
            }
            if (!sameIdentity) {
                showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoIdentity));
                std::remove(tmpPath.c_str());
            } else {
                // 2026-09-19: la sola data di modifica del file NON dice chi ha
                // davvero piu' progressi -- una copia via USB, un semplice
                // caricamento in un emulatore, o un orologio di sistema sballato
                // bastano a confonderla, e su questi due save (stesso allenatore,
                // appena verificato sopra) la differenza reale che conta e'
                // quanto si e' giocato. localProbe/remoteProbe sono gia' caricati
                // per il controllo identita': stesso costo di rete, nessun
                // download in piu'.
                // Priorita': 1) tempo di gioco (solo GBA per ora, vedi
                // SaveFile::playTimeSeconds) 2) Pokedex catturati (copre anche
                // GB/GBC/NDS, vedi Pokedex::getDexStatus) 3) mtime del file, come
                // ultima spiaggia quando nessuno dei due segnali e' disponibile o
                // i due save risultano identici su entrambi.
                long localPt = localProbe.playTimeSeconds();
                long remotePt = remoteProbe.playTimeSeconds();
                bool remoteNewer;
                std::string howDecided;
                if (localPt >= 0 && remotePt >= 0 && localPt != remotePt) {
                    remoteNewer = remotePt > localPt;
                    howDecided = "playtime";
                } else {
                    Pokedex::DexStatus localDex = Pokedex::getDexStatus(localProbe);
                    Pokedex::DexStatus remoteDex = Pokedex::getDexStatus(remoteProbe);
                    if (localDex.supported && remoteDex.supported && localDex.caught != remoteDex.caught) {
                        remoteNewer = remoteDex.caught > localDex.caught;
                        howDecided = "dex";
                    } else {
                        struct stat st;
                        long long localModified = 0;
                        if (stat(switchLocalPath.c_str(), &st) == 0) localModified = (long long)st.st_mtime;
                        remoteNewer = c.remoteSaveModifiedUnix > localModified;
                        howDecided = "mtime";
                    }
                }
                DebugLog::line("remote sync: sincronizza direzione=%s per %s (local pt=%ld dex=%d, remote pt=%ld dex=%d)",
                               howDecided.c_str(), gi.gameTag, localPt,
                               Pokedex::getDexStatus(localProbe).caught, remotePt,
                               Pokedex::getDexStatus(remoteProbe).caught);
                std::string dir = i18n::get(remoteNewer ? StrKey::DevSyncDirRemoteToLocal : StrKey::DevSyncDirLocalToRemote);
                if (showConfirmDialog(i18n::fmt(StrKey::DevSyncSyncTitle, gi.displayName), i18n::fmt(StrKey::DevSyncSyncBody, localOt, dir))) {
                    if (remoteNewer) {
                        // Usa copia già scaricata (tmpPath) — evita seconda richiesta di rete
                        // e soprattutto non confrontare più dopo: il mtime locale va sovrascritto ora.
                        // Save Switch nativo: scrivi nel mount nativo (mai su "save:/..." diretto,
                        // non montato qui), stesso helper usato da doDownload piu' sopra.
                        if (c.localPath.rfind("save:/", 0) == 0) {
                            bool okNative = writeToNativeSave(tmpPath, c.type);
                            if (okNative) received++; else failed++;
                            DebugLog::line("remote sync: sincronizza (save nativo, da verifica gia' scaricata) %s (%s)",
                                           gi.gameTag, okNative ? "OK" : "FALLITO");
                        } else {
                            copyLocalAsReceived(tmpPath, c.localPath);
                        }
                    } else {
                        bool ok = doUpload(switchLocalPath, remoteTargetPath);
                        if (ok && !c.hasRemoteRom) {
                            std::string localRom = findLocalRom(c.localPath, c.type);
                            if (!localRom.empty()) {
                                size_t dot = localRom.find_last_of('.');
                                std::string ext = (dot == std::string::npos) ? ".gba" : localRom.substr(dot);
                                std::string romBase = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                                for (char& ch : romBase) if (ch == '/' || ch == '\\') ch = '_';
                                std::string remoteRomPath = c.remoteDir + romBase + ext;
                                std::string romErr;
                                if (remoteSyncUpload(host, token, localRom, remoteRomPath, romErr))
                                    DebugLog::line("remote sync: ROM sincronizzata %s -> %s", localRom.c_str(), remoteRomPath.c_str());
                            }
                        }
                    }
                    didSomething = true;
                }
                std::remove(tmpPath.c_str());
                // Nota: playtime/dex/mtime sopra sono tutti letti PRIMA del
                // transfer (localProbe/localModified originali) -- dopo il
                // transfer il file locale ha mtime = now, non va mai riletto
                // per decisioni nello stesso giro.
            }
        }
    }

    // Summary per singola azione: non mostrare popup "inviato/saltati" se l'utente ha
    // appena premuto B per annullare — con B deve tornare subito alla schermata
    // precedente (giochi / dock sync save), senza spam. Logga solo, mostra solo se fallito.
    skipped = didSomething ? 0 : 1;
    if (failed > 0) {
        showMessageAndWait(title, i18n::fmt(StrKey::DevSyncFlowSummary, std::to_string(sent), std::to_string(received), std::to_string(skipped), std::to_string(failed)));
    }
    DebugLog::line("remote sync: flusso completato su %s (inviati=%d ricevuti=%d saltati=%d falliti=%d)",
                   host.c_str(), sent, received, skipped, failed);
    // Ripulisci il tmp del save Switch nativo risolto sopra (se creato) --
    // mai lasciato sul dispositivo dopo che il flusso e' terminato, come
    // ogni altro tmp di questa funzione.
    if (!switchSaveRealPath.empty()) std::remove(switchSaveRealPath.c_str());
}

// --- Box Remoto: apre save gia' presenti sul dispositivo remoto dentro lo
// stesso selettore giochi/banca locale. Il save scelto si scarica in un
// file temporaneo che si comporta come un ImportedGame qualunque (stessa
// UI::selectGame(), stesso bank/edit di sempre); in uscita dal gioco (non
// dal box) UI::returnToGameSelector() chiede conferma e rispedisce il file
// al dispositivo remoto solo se e' stato davvero modificato.

void UI::openRemoteBox() {
    std::string title = i18n::get(StrKey::RemoteBoxTitle);
    if (!updateNetEnsureReady()) {
        showMessageAndWait(title, i18n::get(StrKey::SendLogNetOff));
        return;
    }
    if (isDualBankMode()) {
        showMessageAndWait(title, i18n::get(StrKey::RemoteBoxNotInDualMode));
        return;
    }

    std::string host, user, pass, token;
    if (!remoteSyncEnsureLogin(title, host, user, pass, token))
        return;

    // Stesso motivo di remoteSyncTestRow() sopra: tiene "montato" il device
    // per icona barra di stato/dock anche qui (in pratica il Box Remoto e'
    // raggiungibile solo quando gia' visibile, ma un token rinnovato da
    // remoteSyncEnsureLogin() va comunque ripubblicato).
    remoteDeviceAvailable_ = true;
    remoteDeviceHost_ = host;
    remoteDeviceToken_ = token;

    std::vector<ImportedGame> localGames = scanImportPaths(importPaths_, autoCheckUsb_);
    std::string buildErr;
    std::vector<SyncCandidate> candidates = remoteSyncBuildCandidates(host, token, localGames, buildErr);

    // Cartella per i save remoti scaricati mentre il box e' aperto -- ripulita
    // (i singoli file) alla chiusura in closeRemoteBox().
    std::string tmpDir = basePath_ + "remote_box_tmp/";
    mkdir(tmpDir.c_str(), 0755);

    remoteBoxEntries_.clear();
    std::vector<ImportedGame> boxGames;
    for (auto& c : candidates) {
        // Solo save che esistono gia' sul dispositivo remoto: il box apre
        // partite esistenti, non ne crea di nuove da una sola ROM (per
        // quello c'e' "Invia" nel flusso Invia/Ricevi/Sincronizza).
        if (!c.hasRemoteSave) continue;
        const GameInfo& gi = gameInfo(c.type);
        std::string tmpPath = tmpDir + gi.gameTag + "_box.tmp";
        std::string dlErr;
        if (!remoteSyncDownload(host, token, c.remoteSavePath, tmpPath, dlErr)) {
            DebugLog::line("box remoto: download fallito per %s: %s", gi.gameTag, dlErr.c_str());
            continue;
        }
        RemoteBoxEntry entry;
        entry.type = c.type;
        entry.tmpPath = tmpPath;
        entry.host = host;
        entry.token = token;
        entry.remoteSavePath = c.remoteSavePath;
        {
            struct stat st;
            if (stat(tmpPath.c_str(), &st) == 0) {
                entry.snapSize = (long long)st.st_size;
                entry.snapMtime = (long long)st.st_mtime;
            }
        }
        remoteBoxEntries_.push_back(entry);

        ImportedGame ig;
        ig.type = c.type;
        ig.filePath = tmpPath;
        ig.sourceTag = "R36S";
        boxGames.push_back(ig);
    }

    if (boxGames.empty()) {
        showMessageAndWait(title, i18n::get(StrKey::RemoteBoxNoSaves));
        return;
    }

    // Salva lo stato locale della griglia -- si ripristina alla chiusura del
    // box (closeRemoteBox), esattamente come si trovava prima di entrare.
    savedAvailableGames_ = availableGames_;
    savedImportedGames_ = importedGames_;

    importedGames_ = boxGames;
    availableGames_.clear();
    for (auto& ig : importedGames_)
        availableGames_.push_back(ig.type);

    remoteBoxActive_ = true;

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    selPageShown_ = 0;
    selSlide_ = 0.0f;
    galSelShown_ = -1;
    galSlide_ = 0.0f;
    gsSetFocus(GSFocus::Grid);
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();
    screen_ = AppScreen::GameSelector;

    showMessageAndWait(title, i18n::fmt(StrKey::RemoteBoxEntered, std::to_string((int)boxGames.size())));
    DebugLog::line("box remoto: aperto, %d save da %s", (int)boxGames.size(), host.c_str());
}

void UI::closeRemoteBox() {
    availableGames_ = savedAvailableGames_;
    importedGames_ = savedImportedGames_;
    savedAvailableGames_.clear();
    savedImportedGames_.clear();

    // Save modificati (dimensione/mtime diversi dall'istantanea presa al
    // download, vedi RemoteBoxEntry) che nessuno ha ancora rispedito al
    // device remoto -- capita normalmente uscendo dal save con B, che porta
    // alla lista banche (UI::actionCancel) e NON passa da
    // UI::returnToGameSelector() a meno di usare il menu "Cambia gioco":
    // senza questo controllo, chiudere il box li scartava in silenzio (bug
    // segnalato: il save modificato viene scritto in locale correttamente,
    // solo mai rispedito -- "i pokemon inviati non appaiono poi nel
    // dispositivo"). Stessa policy di UI::returnToGameSelector(): mai un
    // invio automatico silenzioso, sempre una conferma prima -- qui
    // aggregata in un solo popup invece di uno per save.
    std::vector<size_t> pending;
    for (size_t i = 0; i < remoteBoxEntries_.size(); i++) {
        struct stat st;
        if (stat(remoteBoxEntries_[i].tmpPath.c_str(), &st) != 0) continue;
        if ((long long)st.st_size != remoteBoxEntries_[i].snapSize ||
            (long long)st.st_mtime != remoteBoxEntries_[i].snapMtime)
            pending.push_back(i);
    }
    if (!pending.empty()) {
        std::string title = i18n::get(StrKey::RemoteBoxSendTitle);
        if (showConfirmDialog(title, i18n::fmt(StrKey::RemoteBoxCloseSendBody,
                                               std::to_string(pending.size())))) {
            int sent = 0, failed = 0;
            for (size_t idx : pending) {
                auto& e = remoteBoxEntries_[idx];
                std::string upErr;
                if (remoteSyncUpload(e.host, e.token, e.tmpPath, e.remoteSavePath, upErr)) {
                    sent++;
                    DebugLog::line("box remoto: invio OK alla chiusura (%s)", e.tmpPath.c_str());
                } else {
                    failed++;
                    DebugLog::line("box remoto: invio FALLITO alla chiusura (%s): %s",
                                   e.tmpPath.c_str(), upErr.c_str());
                }
            }
            showMessageAndWait(title, i18n::fmt(StrKey::RemoteBoxCloseSendResult,
                                                std::to_string(sent), std::to_string(failed)));
        } else {
            DebugLog::line("box remoto: invio alla chiusura rifiutato dall'utente (%d save), modifiche solo locali",
                           (int)pending.size());
        }
    }

    for (auto& e : remoteBoxEntries_)
        std::remove(e.tmpPath.c_str());
    remoteBoxEntries_.clear();
    remoteBoxActive_ = false;

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    selPageShown_ = 0;
    selSlide_ = 0.0f;
    galSelShown_ = -1;
    galSlide_ = 0.0f;
    gsSetFocus(GSFocus::Grid);
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();

    DebugLog::line("box remoto: chiuso, ripristinata lista locale");
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
        } // row 2 (Emulatore predefinito): riga info, nessuna azione
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
                } else {
                    std::string dst = cfg + ".off";
                    if (std::rename(cfg.c_str(), dst.c_str()) == 0)
                        DebugLog::line("settings: sorgente -> GitHub (%s disattivato)", cfg.c_str());
                    else
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
            beginTextInput(TextInputPurpose::EditUpdateUrl);
        }
    } else if (cat == 5) {
        if (row == settingsRowCount(5) - 1) {
            // Ricerca dispositivi: sempre l'ultima riga, va controllata per
            // prima per lo stesso motivo spiegato in settingsRowLabel.
            remoteSyncTestRow();
        } else if (row == 0) {
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
        } else if (row == 1) {
            // Menu debug rapido: ON = gear apre il + classico, OFF = impostazioni.
            writeQuickMenu(basePath_, !readQuickMenu(basePath_));
        } else if (row == 2) {
            if (showConfirmDialog(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistBody))) {
                if (Backpack::clearJournal(basePath_))
                    showMessageAndWait(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistDone));
                else
                    showMessageAndWait(i18n::get(StrKey::ClearBpHistTitle), i18n::get(StrKey::ClearBpHistFailed));
            }
        } else if (row == 3) {
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
        } else if (row == 4) {
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
        } else if (row == 5) {
            sendLogNow();
        } else {
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
    constexpr int POP_W = 1000;
    constexpr int POP_H = 560;
    constexpr int ROW_H = 44;
    constexpr int CAT_W = 280;
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
        drawText(i18n::get(cats[c]), popX + 36, rowY + 8, T().text, font_);
    }
    // Divisore verticale
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    int divX = popX + CAT_W + 10;
    SDL_RenderDrawLine(renderer_, divX, listY, divX, popY + POP_H - 50);
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
        {
            constexpr float K = 0.35f, D = 0.65f;
            float disp = appearanceCollapse_ - target;
            if (disp != 0.0f || appearanceCollapseVel_ != 0.0f) {
                float force = -disp * K - appearanceCollapseVel_ * D;
                appearanceCollapseVel_ += force;
                appearanceCollapse_ += appearanceCollapseVel_;
                if (std::fabs(appearanceCollapse_ - target) < 0.01f && std::fabs(appearanceCollapseVel_) < 0.01f) {
                    appearanceCollapse_ = target;
                    appearanceCollapseVel_ = 0.0f;
                } else {
                    markDirty();
                }
            }
        }
        float collapse = appearanceCollapse_;
        int selectedLogical = appearanceRow(setRow_, gameSelectorLayout_ == GameSelectorLayout::Gallery);
        for (int r = 0; r < 8; r++) {
            bool hideable = (r == 3 || r == 4);
            float localCollapse = hideable ? appearanceCollapse_ : 0.0f;
            if (localCollapse < 0.0f) localCollapse = 0.0f;
            if (localCollapse > 1.3f) localCollapse = 1.3f; // margine per l'overshoot della molla
            float alphaCollapse = localCollapse > 1.0f ? 1.0f : localCollapse;
            Uint8 alphaMul = hideable ? (Uint8)(((int)(255.0f * (1.0f - alphaCollapse)) / 16) * 16) : 255;
            if (hideable && alphaMul == 0) continue; // completamente nascosta: niente da disegnare
            float rowShift = 2.0f * ROW_H * collapse; // le righe sotto risalgono seguendo la molla (con overshoot)
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
            int rowX = rx + (hideable ? (int)(30.0f * localCollapse) : 0); // scivolano a destra mentre sfumano
            SDL_Color textCol = T().text; textCol.a = (Uint8)((int)textCol.a * alphaMul / 255);
            SDL_Color valCol = T().selected; valCol.a = (Uint8)((int)valCol.a * alphaMul / 255);
            if (r == selectedLogical && !setFocusLeft_) {
                drawRect(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().cursor, 2);
            }
            drawText(label, rowX, rowY + 8, textCol, font_);
            if (!value.empty()) {
                const auto& e = getTextEntry(value, font_, valCol);
                drawText(value, popX + POP_W - 36 - e.w, rowY + 8, valCol, font_);
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
                dot(dxp, by + bh / 2, 7, valCol);
            }
        }
    } else {
        int n = settingsRowCount(setCat_);
        for (int r = 0; r < n; r++) {
            int rowY = listY + r * ROW_H;
            if (r == setRow_ && !setFocusLeft_) {
                drawRect(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 8, rowY, POP_W - (rx - popX) - 28, ROW_H - 4, T().cursor, 2);
            }
            drawText(settingsRowLabel(setCat_, r), rx, rowY + 8, T().text, font_);
            std::string v = settingsRowValue(setCat_, r);
            if (!v.empty()) {
                const auto& e = getTextEntry(v, font_, T().selected);
                drawText(v, popX + POP_W - 36 - e.w, rowY + 8, T().selected, font_);
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
