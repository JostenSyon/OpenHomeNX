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

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <unordered_map>
#include <sys/stat.h>
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif



// ==================== Dock inferiore (riga bassa selettore giochi) ====================
// Stato persistito in settings.cfg (dock_order CSV + dock_visible). Disegno,
// tap e navigazione sono tutti guidati da dockLayout(), cosi' l'ordine utente
// non desincronizza mai le tre cose (era il bug del menu popup v0.1.37).
#ifdef OH_LINUX
static constexpr int DOCK_ROW_DX = 80; // 4:3: 7 voci x 80 = 560 <= 640
#else
static constexpr int DOCK_ROW_DX = 104; // spaziatura icone dock (condivisa con la molla)
#endif

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

// readQuickMenu: definita in ui_settings.cpp (usata anche li'), non piu' static.
bool readQuickMenu(const std::string& basePath);
// readUpdateCfg: definita in ui_update.cpp (usata anche qui), non piu' nel suo namespace anonimo.
bool readUpdateCfg(const std::string& basePath, UpdateCfg& out);

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
// Non piu' static: usata anche da drawSaveMenuPopup() in ui_backups.cpp.
std::vector<std::string> saveMenuRows(GameType g, bool canSend) {
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
        DebugLog::line("bank count: %s -> %d", gameInfo(g).gameTag, gameBankCounts_[g]);
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
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back: esce come START
                    if (!confirmQuitWithHold()) break;
                    if (Settings::confirmExit() &&
                        !showConfirmDialog(i18n::get(StrKey::ConfirmExitTitle),
                                           i18n::get(StrKey::ConfirmExitBody)))
                        break;
                    running = false;
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
                    if (Settings::confirmExit() &&
                        !showConfirmDialog(i18n::get(StrKey::ConfirmExitTitle),
                                           i18n::get(StrKey::ConfirmExitBody)))
                        break;
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
    availableGamesNative_.clear();
    constexpr GameType allGames[] = {
        GameType::GP, GameType::GE, GameType::Sw, GameType::Sh,
        GameType::BD, GameType::SP, GameType::LA, GameType::S,
        GameType::V, GameType::ZA, GameType::FR, GameType::LG,
        GameType::FR_ES, GameType::LG_ES, GameType::FR_DE, GameType::LG_DE,
        GameType::FR_IT, GameType::LG_IT, GameType::FR_FR, GameType::LG_FR,
        GameType::FR_JA, GameType::LG_JA
    };
    for (GameType g : allGames) {
        if (account_.hasSaveData(index, g)) {
            availableGames_.push_back(g);
            availableGamesNative_.push_back(1);
        }
    }
    appendImportedGames();
    applyFavoritesOrder();
    assertGamesInSync();

    if (availableGames_.empty()) {
        showMessageAndWait(i18n::get(StrKey::NoSaveData),
            i18n::get(StrKey::NoSaveDataBody));
        return;
    }

    refreshBankCounts();

    // Auto-sync FRLG (spento di default): silenzioso, solo log. A questo
    // punto nessun gioco e' ancora aperto, quindi niente conflitti RAM.
    if (Settings::frlgAutoSync())
        syncFrlgSaves(true);

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

bool UI::hideFrlgRom(GameType t) const {
    if (!isFRLG(t) || Settings::showFrlgRoms()) return false;
    // Solo le native contano (flag esatto) e solo dello STESSO gioco base:
    // una nativa FR non deve mai nascondere una ROM LG (stesso errore del
    // sync, vedi frlgBase in game_type.h).
    for (size_t i = 0; i < availableGames_.size(); i++)
        if (availableGamesNative_[i] != 0 && frlgBase(availableGames_[i]) == frlgBase(t))
            return true;
    return false;
}

void UI::appendImportedGames() {
    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_, Settings::showRomsWithoutSave());
    for (const auto& ig : importedGames_) {
        // FireRed/LeafGreen sono l'unica famiglia import che puo' anche
        // avere un GameType nativo con titleId reale (NSO GBA) gia' in
        // availableGames_ -- vedi hideFrlgRom() (stesso filtro del rescan).
        if (hideFrlgRom(ig.type)) continue;
        availableGames_.push_back(ig.type);
        availableGamesNative_.push_back(0);
    }
    assertGamesInSync();
    // Non riordino qui: il chiamante (selectProfile/fillPresentGames) fa già apply; rescan fa a parte.
}

std::string UI::importedSavePath(GameType game, int occurrence) const {
    int seen = 0;
    for (const auto& ig : importedGames_)
        if (ig.type == game) {
            if (seen == occurrence)
                return ig.hasSave ? ig.filePath : std::string();
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

std::string UI::importedRomPath(GameType game, int occurrence) const {
    int seen = 0;
    for (const auto& ig : importedGames_)
        if (ig.type == game) {
            if (seen == occurrence)
                return ig.hasSave ? Emulator::findRomForSave(ig.filePath, game) : ig.filePath;
            seen++;
        }
    return "";
}

bool UI::importedIsRomOnly(GameType game, int occurrence) const {
    int seen = 0;
    for (const auto& ig : importedGames_)
        if (ig.type == game) {
            if (seen == occurrence)
                return !ig.hasSave;
            seen++;
        }
    return false;
}

void UI::assertGamesInSync() const {
    // NDEBUG non e' definita in nessuna delle due build (Switch/R36S), quindi
    // un assert() qui abortirebbe anche in release per un utente vero. Per
    // uno stato interno ricostruibile (non un salvataggio) meglio degradare
    // con grazia: logga l'anomalia, isNativeAt()/importedOccurrence() sotto
    // si difendono gia' da soli confrontando le dimensioni invece di
    // assumerle uguali -- mai un accesso fuori range, mai un crash.
    if (availableGamesNative_.size() != availableGames_.size())
        DebugLog::line("BUG: availableGamesNative_ (%zu) fuori sync con availableGames_ (%zu)",
                       availableGamesNative_.size(), availableGames_.size());
}

bool UI::isNativeAt(int idx) const {
    assertGamesInSync();
    // >= availableGamesNative_.size() (non availableGames_.size()) cosi' un
    // eventuale disallineamento degrada su "nativa" invece di leggere fuori
    // dal vettore piu' corto.
    if (idx < 0 || idx >= (int)availableGamesNative_.size())
        return true; // fuori range (o disallineato): via nativa, mai un import
    return availableGamesNative_[(size_t)idx] != 0;
}

int UI::importedOccurrence(int cursor) const {
    assertGamesInSync();
    if (cursor < 0 || cursor >= (int)availableGames_.size() ||
        cursor >= (int)availableGamesNative_.size())
        return -1;
    if (availableGamesNative_[(size_t)cursor] != 0)
        return -1; // nativa: mai un indice in importedGames_
    GameType game = availableGames_[cursor];
    int n = 0;
    for (int i = 0; i < cursor; i++)
        if (i < (int)availableGamesNative_.size() &&
            availableGames_[i] == game && availableGamesNative_[(size_t)i] == 0)
            n++;
    return n;
}

void UI::rescanImportedGames() {
    std::vector<GameType> oldTypes;
    for (const auto& ig : importedGames_)
        oldTypes.push_back(ig.type);

    // Unlike appendImportedGames(), availableGames_ isn't being rebuilt from
    // scratch here — drop the previous imported entries first so re-running
    // this on every hotplug doesn't pile up duplicates. Filtro per indice
    // (non per valore) cosi' il vettore parallelo availableGamesNative_
    // resta allineato: le native non si toccano mai, qui si toglie solo
    // import. Stessa semantica di prima per FRLG (via da import solo se il
    // tipo era gia' fra gli import precedenti).
    assertGamesInSync();
    {
        std::vector<GameType> keptGames;
        std::vector<char> keptNative;
        for (size_t i = 0; i < availableGames_.size(); i++) {
            GameType g = availableGames_[i];
            bool isPrevImport = availableGamesNative_[i] == 0 &&
                (isFRLG(g) ? std::find(oldTypes.begin(), oldTypes.end(), g) != oldTypes.end()
                           : (isImportedFile(g) || isGen1File(g) || isGen2File(g) ||
                              isGen45File(g) || isGen6XY(g) || isGen6ORAS(g) || isGen7SM(g) || isGen7USUM(g)));
            if (!isPrevImport) {
                keptGames.push_back(g);
                keptNative.push_back(availableGamesNative_[i]);
            }
        }
        availableGames_.swap(keptGames);
        availableGamesNative_.swap(keptNative);
    }

    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_, Settings::showRomsWithoutSave());
    for (const auto& ig : importedGames_) {
        // Stesso filtro di appendImportedGames (qui prima mancava: il rescan
        // mostrava i doppioni FRLG anche con nativa presente).
        if (hideFrlgRom(ig.type)) continue;
        availableGames_.push_back(ig.type);
        availableGamesNative_.push_back(0);
    }
    applyFavoritesOrder();
    assertGamesInSync();

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

    DebugLog::line("import hotplug: %zu new game(s) found", newlyFound.size());
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
    std::unordered_map<GameType, int> occCount;
    for (size_t ai = 0; ai < availableGames_.size(); ai++) {
        GameType game = availableGames_[ai];
        // Imported games (RSE/Gen1/Gen2/FRLG from a scanned save, OR a
        // save-less ROM via Settings::showRomsWithoutSave()) have no
        // titleId and no NS control data — always try their boxart cover,
        // never the NS fetch below. FRLG is the only ambiguous type here:
        // it's ALSO the native Switch title's GameType (real titleId), so
        // it only takes this path when THIS occurrence really is
        // import-backed (importedRomPath() resolves it via importedGames_
        // regardless of hasSave) — a native FRLG occurrence has no
        // importedGames_ entry at all, romPath stays "", falls through to
        // NS fetch below like every other native title.
        int occ = occCount[game]++;
        std::string romPath = importedRomPath(game, occ);
        if (isImportedFile(game) || isGen1File(game) || isGen2File(game) ||
            (isFRLG(game) && !romPath.empty())) {
            std::string cover = Boxart::findCachedCover(basePath_, romPath);
            if (!cover.empty()) {
                SDL_Surface* csurf = IMG_Load(cover.c_str());
                if (csurf) gameAccentCache_[game] = computeAccentColor(csurf);
                if (csurf) {
                    if (SDL_Surface* rr = roundCornersSurface(csurf, std::min(csurf->w, csurf->h) / 12)) {
                        SDL_FreeSurface(csurf);
                        csurf = rr;
                    }
                }
                if (csurf) {
                    SDL_Texture* ctex = SDL_CreateTextureFromSurface(renderer_, csurf);
                    SDL_FreeSurface(csurf);
                    if (ctex) {
                        SDL_SetTextureBlendMode(ctex, SDL_BLENDMODE_BLEND);
                        gameIconCache_[game] = ctex;
                        DebugLog::line("icons: %s cover %s",
                            gameInfo(game).gameTag, cover.c_str());
                    }
                }
            }
            continue;
        }
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
            // FRLG: can be native (with NS icon) or imported (with boxart).
            // If it's FRLG and already has a cover from the earlier boxart
            // loop (imported with cover), skip NS. If not, try NS for native.
            if (isFRLG(game) && gameIconCache_.count(game))
                continue;
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
    std::string romPath = importedRomPath(availableGames_[i], importedOccurrence(i));
    bool isFileBackedRom = isImportedFile(availableGames_[i]) || isGen1File(availableGames_[i]) ||
                           isGen2File(availableGames_[i]) || (isFRLG(availableGames_[i]) && !romPath.empty());
    // Badge sorgente (basso-sinistra): stesso identico blocco usato da
    // sempre per i giochi senza copertina in cache (vedi piu' sotto) --
    // estratto qui cosi' lo stile Locale puo' richiamarlo ANCHE quando una
    // copertina c'e', senza duplicare il disegno. Richiesta utente: la
    // label deve essere esattamente la stessa, non una nuova diversa.
    auto drawSourceBadge = [&]() {
        std::string srcTag = importedSourceTag(availableGames_[i], importedOccurrence(i));
        if (srcTag.empty()) return;
        if (srcTag.length() > 10) srcTag = srcTag.substr(0, 9) + ".";
        const auto& te = getTextEntry(srcTag, fontSmall_, T().text);
        int badgeW = te.w + 8, badgeH = te.h + 4;
        int badgeX = iconX + 2, badgeY = iconY + IS - badgeH - 2;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        // Pill arrotondata stile "pop" invece del rettangolo secco; il
        // testo e' sempre chiaro perche' lo sfondo e' sempre nero.
        drawRoundRect(badgeX, badgeY, badgeW, badgeH, badgeH / 2, {0, 0, 0, 160});
        drawText(srcTag, badgeX + 4, badgeY + 2, {240, 240, 240, 255}, fontSmall_);
    };
    auto it = gameIconCache_.find(availableGames_[i]);
    if (it != gameIconCache_.end() && it->second) {
        SDL_Rect dst = {iconX, iconY, IS, IS};
        SDL_RenderCopy(renderer_, it->second, nullptr, &dst);
        // Locale: il badge sorgente c'e' sempre su una ROM importata,
        // qualunque sia la copertina mostrata (arte locale vera, interna
        // hardcoded, o fallback 2D scaricato) -- non e' legata al
        // fallback, e' una proprieta' dello stile Locale stesso (richiesta
        // utente). Su 2D/3D nessun badge.
        if (isFileBackedRom && Settings::boxartStyle() == 0) drawSourceBadge();
    } else if (isFileBackedRom) {
        // FRLG e' l'unico tipo ambiguo (nativo Switch CON titleId reale, O
        // import da ROM senza titleId): stessa disambiguazione per-istanza
        // gia' usata in loadGameIcons() (importedRomPath() risolve "" per
        // un'occorrenza nativa). Prima mancava qui: un FireRed/LeafGreen
        // importato senza cover in cache cadeva nel placeholder generico
        // (sigla "FR"/"LG" su sfondo colore fisso) invece del riquadro
        // flat+nome-completo usato da RSE/RBY/GSC -- inconsistente, e
        // "l'etichetta non appare" per quei giochi specifici.
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
                // No logo asset (Gen1/Gen2 file games): centered game tag,
                // a contrasto con lo sfondo flat della tile (su alcuni
                // sfondi scuri il T().text dei temi chiari era illeggibile).
                const char* tag = gameInfo(availableGames_[i]).gameTag;
                SDL_Color tagCol = contrastTextForBg(bg);
                const auto& te = getTextEntry(tag, font_, tagCol);
                drawText(tag, iconX + (IS - te.w) / 2, iconY + (IS - te.h) / 2,
                         tagCol, font_);
            }
        }
        // Small source-folder badge (bottom-left corner of the icon) —
        // only useful when more than one plausible source could hold the
        // same game (e.g. a "roms/saves" copy AND a "roms" companion
        // file); harmless/redundant otherwise, so always shown rather
        // than only-on-ambiguity, which would need an extra pass to
        // detect and would still surprise the user the first time a
        // second source shows up.
        drawSourceBadge();
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
            case GameType::GOLD: abbr = "Go"; break;
            case GameType::SILVER: abbr = "Si"; break;
            case GameType::CRYSTAL: abbr = "Cr"; break;
            case GameType::DIAMOND: abbr = "D"; break;
            case GameType::PEARL: abbr = "P"; break;
            case GameType::PLATINUM: abbr = "Pt"; break;
            case GameType::HEARTGOLD: abbr = "HG"; break;
            case GameType::SOULSILVER: abbr = "SS"; break;
            case GameType::BLACK: abbr = "B"; break;
            case GameType::WHITE: abbr = "W"; break;
            case GameType::BLACK2: abbr = "B2"; break;
            case GameType::WHITE2: abbr = "W2"; break;
            case GameType::X: abbr = "X"; break;
            case GameType::Y: abbr = "Y"; break;
            case GameType::OMEGA_RUBY: abbr = "OR"; break;
            case GameType::ALPHA_SAPPHIRE: abbr = "AS"; break;
            case GameType::SUN: abbr = "Su"; break;
            case GameType::MOON: abbr = "Mo"; break;
            case GameType::ULTRA_SUN: abbr = "US"; break;
            case GameType::ULTRA_MOON: abbr = "UM"; break;
            default: break;
        }
        // Testo a contrasto con lo sfondo placeholder (temi chiari/scuro).
        drawTextCentered(abbr, iconX + IS / 2, iconY + IS / 2,
                         contrastTextForBg(T().iconPlaceholder), font_);
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
#ifdef OH_LINUX
    // 4:3: dock compatto sopra la status bar (445), sotto il preview (384).
    constexpr int R = 26;
    constexpr int ICON_R = 20;
    constexpr int BTN_Y = 412;
#else
    constexpr int R = 34;
    constexpr int ICON_R = 24;
    constexpr int BTN_Y = SCREEN_H - 110;
#endif
    auto slots = dockLayout();
    if (slots.empty()) {
        dockClearFocus();
        return;
    }
    // Animazione espelli (come la vecchia riga fissa): il target e' la
    // posizione della voce Eject nel layout, o fuori schermo se nascosta.
    int ejectTarget = SCREEN_W / 2 + DOCK_ROW_DX;
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
#ifdef OH_LINUX
                // 4:3: sotto c'e' la status bar, label sopra l'icona.
                int ly = BTN_Y - R - 32;
#else
                int ly = BTN_Y + R + 19; // un paio di px sotto l'icona, come nel radial
#endif
                SDL_Color sh = shadowForText(T().text, 220);
                // ombra rinforzata: alone 8 direzioni + leggero offset per staccare dal fondo
                drawTextCentered(lbl, cx + 1, ly + 1, sh, font_);
                drawTextCentered(lbl, cx - 1, ly + 1, sh, font_);
                drawTextCentered(lbl, cx + 1, ly - 1, sh, font_);
                drawTextCentered(lbl, cx - 1, ly - 1, sh, font_);
                drawTextCentered(lbl, cx, ly + 1, sh, font_);
                drawTextCentered(lbl, cx, ly - 1, sh, font_);
                drawTextCentered(lbl, cx + 1, ly, sh, font_);
                drawTextCentered(lbl, cx - 1, ly, sh, font_);
                SDL_Color sh2 = shadowForText(T().text, 140);
                drawTextCentered(lbl, cx + 2, ly + 2, sh2, font_);
                drawTextCentered(lbl, cx - 2, ly + 2, sh2, font_);
                drawTextCentered(lbl, cx, ly, T().text, font_);
            }
        }
    }
    if (dockState_.reorderMode) {
#ifdef OH_LINUX
        drawTextCentered("Sposta: L/R  Conferma: A  Annulla: B", SCREEN_W / 2,
                         BTN_Y - R - 32, T().textDim, fontSmall_);
#else
        drawTextCentered("Sposta: L/R  Conferma: A  Annulla: B", SCREEN_W / 2,
                         BTN_Y + R + 17, T().textDim, fontSmall_);
#endif
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
#ifdef OH_LINUX
    // Nativo 4:3 (640px): 4 colonne, UNA riga sola (4/pagina). Con 2 righe
    // lo spazio verticale restava stretto qualunque fosse CARD_W (il fondo
    // griglia toccava esattamente il bordo del dock, zero margine) --
    // con una riga sola CARD_H puo' tornare grande senza quel vincolo,
    // niente piu' schiacciamento ne' icone a ridosso del dock.
    // Rapporto vicino a quello originale (140x190, 0.737) -- la prima prova
    // (132x220, 0.6) era troppo stretta/allungata. Margine orizzontale
    // verificato contro le frecce pagina ridotte per R36S qui sotto (AR=16,
    // x=22): riga da 533px centrata lascia 53px per lato, le frecce
    // arrivano a 38px -- 15px di respiro reale, non solo sulla carta.
    constexpr int COLS = 4;
    constexpr int ROWS_PER_PAGE = 1;
    constexpr int GAMES_PER_PAGE = COLS * ROWS_PER_PAGE;
    constexpr int CARD_W = 122;
    constexpr int CARD_H = 166;
    constexpr int CARD_GAP = 15;
    constexpr int ICON_SIZE = 98;
#else
    constexpr int COLS = 6;
    constexpr int ROWS_PER_PAGE = 2;
    constexpr int GAMES_PER_PAGE = COLS * ROWS_PER_PAGE;
    constexpr int CARD_W = 160;
    constexpr int CARD_H = 200;
    constexpr int CARD_GAP = 20;
    constexpr int ICON_SIZE = 128;
#endif

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
#ifdef OH_LINUX
        // 4:3: stessa riga del dock (BTN_Y=412 in drawDock()).
        constexpr int GR = 22;
        int gcx = SCREEN_W - 48;
        int gcy = 412;
#else
        constexpr int GR = 26;
        int gcx = SCREEN_W - 64;
        int gcy = SCREEN_H - 110;
#endif
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
        // Come la posizione (34 / SCREEN_W-34) sotto: mai stata platform-
        // specific, andava bene sul margine largo di Switch (1280px) ma su
        // R36S (640px) mangiava lo stesso spazio della griglia -> overlap.
#ifdef OH_LINUX
        constexpr int AR = 16;
#else
        constexpr int AR = 24;
#endif
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
#ifdef OH_LINUX
        constexpr int ARROW_X = 22;
#else
        constexpr int ARROW_X = 34;
#endif
        arrow(ARROW_X, false, gameSelPage_ > 0, leftFocused);
        arrow(SCREEN_W - ARROW_X, true, gameSelPage_ < totalPages - 1, rightFocused);
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
#ifdef OH_LINUX
    constexpr float BTN_Y = 412; // sync con drawDock()
    constexpr float HIT_R = 34; // spacing 80: raggio ridotto per non sovrapporre i vicini
#else
    constexpr float BTN_Y = SCREEN_H - 110;
    constexpr float HIT_R = 45;
#endif
    for (auto& s : dockLayout()) {
        if (s.item == DockState::Item::Eject && ejectBtnA_ <= 128) continue; // in fade: non cliccabile
        if (dist2(px, py, (float)s.cx, BTN_Y) < HIT_R * HIT_R) {
            dockFocusItem(s.item); // setter unico: azzera avatar/gear/launch/chevron
            dockActivateFocused(running);
            return;
        }
    }
#ifdef OH_LINUX
    if (dist2(px, py, SCREEN_W - 48, 412) < 34 * 34) {
#else
    if (dist2(px, py, SCREEN_W - 64, BTN_Y) < 40 * 40) {
#endif
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
            gameSelCursor_ = pageLastCursor(gameSelPage_);
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
                selectOrLaunchGame(availableGames_[i], importedOccurrence(i), running);
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
    float et = (float)SCREEN_W / 2 + (float)DOCK_ROW_DX;
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

int UI::pageLastCursor(int page) const {
#ifdef OH_LINUX
    constexpr int kPerPage = 4;
#else
    constexpr int kPerPage = 12;
#endif
    int n = (int)availableGames_.size();
    int end = std::min((page + 1) * kPerPage, n);
    return end > 0 ? end - 1 : 0;
}

void UI::handleGameSelectorInput(bool& running) {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    const bool gallerySel_ = (gameSelectorLayout_ == GameSelectorLayout::Gallery);
#ifdef OH_LINUX
    // TENERE IN SYNC con la griglia Classica in drawGameSelectorFrame():
    // era rimasta ai valori Switch (6/12), quindi su R36S (griglia 4x2=8)
    // L/R non cambiava mai pagina -- il conteggio pagine pensava che
    // entrassero sempre 12 giochi per pagina, non gli 8 davvero disegnati.
    const int COLS = gallerySel_ ? 1 : 4;
    const int GAMES_PER_PAGE = gallerySel_ ? numGames : 4;
#else
    const int COLS = gallerySel_ ? 1 : 6;
    const int GAMES_PER_PAGE = gallerySel_ ? numGames : 12;
#endif

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
                dockFocusFirst(); // giu' dai chevron: banche o prima voce
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
                    dockFocusFirst();
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
                dockFocusFirst();
            }
            return;
        }

        // Navigate to chevrons when going past grid edges (only if page exists).
        // Su R36S (poche card/pagina, 4) si scorre subito la pagina invece di
        // mettere a fuoco la freccina e aspettare una seconda pressione: con
        // cosi' poche card per riga il doppio passaggio sarebbe fastidioso.
        // Su Switch resta il focus-poi-conferma originale.
        if (totalPages > 1) {
            if (col < 0 && gameSelPage_ > 0) {
#ifdef OH_LINUX
                gameSelPage_--;
                gameSelCursor_ = pageLastCursor(gameSelPage_);
#else
                gsSetFocus(GSFocus::ChevLeft);
#endif
                return;
            }
            int rowItems = std::min(COLS, pageCount - row * COLS);
            if (col >= rowItems && gameSelPage_ < totalPages - 1) {
#ifdef OH_LINUX
                gameSelPage_++;
                gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
#else
                gsSetFocus(GSFocus::ChevRight);
#endif
                return;
            }
        }

        // Stop ai bordi esterni invece di wrap: a prima/ultima pagina
        // (o pagina unica) sinistra sul primo gioco e destra sull'ultimo
        // si fermano sull'icona di bordo invece di saltare all'altro capo.
        int rowItems = std::min(COLS, pageCount - row * COLS);
        if (rowItems <= 0) rowItems = COLS;
        if (col < 0) col = 0;
        if (col >= rowItems) col = rowItems - 1;

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
                dockFocusFirst();
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
                        gameSelCursor_ = pageLastCursor(gameSelPage_);
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
                        gameSelCursor_ = pageLastCursor(gameSelPage_);
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
                        selectOrLaunchGame(availableGames_[gameSelCursor_],
                                           importedOccurrence(gameSelCursor_), running);
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
                        if (Settings::confirmExit() &&
                            !showConfirmDialog(i18n::get(StrKey::ConfirmExitTitle),
                                               i18n::get(StrKey::ConfirmExitBody)))
                            break;
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
                        gameSelCursor_ = pageLastCursor(gameSelPage_);
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
    // Stabile: preferiti in testa mantenendo ordine originale relativo.
    // Riordina per indici cosi' availableGamesNative_ segue la stessa
    // permutazione (stable_partition diretto perderebbe l'allineamento).
    assertGamesInSync();
    std::vector<size_t> idx(availableGames_.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        bool fa = isFavorite(availableGames_[a]);
        bool fb = isFavorite(availableGames_[b]);
        return fa && !fb;
    });
    std::vector<GameType> games(idx.size());
    std::vector<char> native(idx.size());
    for (size_t i = 0; i < idx.size(); i++) {
        games[i] = availableGames_[idx[i]];
        native[i] = availableGamesNative_[idx[i]];
    }
    availableGames_.swap(games);
    availableGamesNative_.swap(native);
    assertGamesInSync();
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
    // isTitle da solo non basta: una ROM FRLG condivide GameType/titleId
    // con la nativa — senza isNativeAt la tile ROM lancerebbe il titolo NSO.
    bool isTitle = isNativeAt(idx) && selectedProfile_ >= 0 &&
                   titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
    if (isTitle) return true;
    ensureMgbaChecked();
    if (mgbaPath_.empty()) return false;
    return !importedRomPath(g, importedOccurrence(idx)).empty();
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
// Sempre in alto (270), su entrambe le piattaforme -- l'apertura di lato
// ai bordi (4:3) e' stata provata e tolta: con la griglia Classica R36S
// ridotta a 4 card/pagina il ventaglio non sfora piu' nemmeno per le card
// di bordo, e aprire sempre verso l'alto e' piu' prevedibile (niente voci
// "storte" sulla prima/ultima colonna).
static float radialCenterDeg(int ax) {
    (void)ax;
    return 270.0f;
}

static float radialItemAngleOriented(int j, int n, float centerDeg) {
    constexpr float ANGLE_STEP = 45.0f;
    if (n <= 1) return centerDeg;
    float span = ANGLE_STEP * (n - 1);
    return (centerDeg - span / 2.0f) + ANGLE_STEP * j;
}

// Centro del bottone j su n intorno all'ancora (ax, ay). Clampata a
// restare a schermo (le tile di riga 0 altrimenti sforerebbero in alto,
// stesso problema/fix del mockup). TENERE IN SYNC fra draw e tap-hit-test:
// entrambi passano da qui.
static void radialItemCenter(int ax, int ay, int j, int n, int& cx, int& cy) {
    // SCREEN_W/H ridichiarati localmente (sono private in UI, non
    // raggiungibili da una funzione libera) -- stessa convenzione delle
    // altre costanti di layout duplicate per funzione in questo file.
#ifdef OH_LINUX
    constexpr int SCREEN_W = 640, SCREEN_H = 480;
#else
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
#endif
#ifdef OH_LINUX
    // 4:3: ventaglio compatto (card piccole, schermo stretto).
    constexpr int EDGE = 8, HALF = 32;
    float R = 84.0f;
#else
    constexpr int EDGE = 12, HALF = 42; // 42 ~= raggio bottone (34) + alone fuoco
    float R = 112.0f; // fisso, un po' piu' distante ora che l'ancora e' il centro vero della card (era 99)
#endif
    // 4:3: il ventaglio e' orientato (centro/alto, bordi/lato) e resta
    // centrato sull'ancora; il clamp sotto e' solo sicurezza residua.
    float rad = radialItemAngleOriented(j, n, radialCenterDeg(ax)) * 3.14159265f / 180.0f;
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
    // TENERE IN SYNC con la griglia Classica in drawGameSelectorFrame().
#ifdef OH_LINUX
    constexpr int COLS = 4, CARD_W = 122, CARD_H = 166, CARD_GAP = 15, GAMES_PER_PAGE = 4;
#else
    constexpr int COLS = 6, CARD_W = 160, CARD_H = 200, CARD_GAP = 20, GAMES_PER_PAGE = 12;
#endif
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
    // ROM senza save (Settings::showRomsWithoutSave()): niente da modificare
    // finche' non esiste un save -- solo avvio, come una voce da launcher.
    GameType radialGame = availableGames_[idx];
    if (!importedIsRomOnly(radialGame, importedOccurrence(idx))) {
        radialItems_.push_back((int)RadialAction::Backpack);
        radialItems_.push_back((int)RadialAction::Bank);
        radialItems_.push_back((int)RadialAction::SaveMenu);
        if (!isDualBankMode() && TradeEvo::supported(radialGame))
            radialItems_.push_back((int)RadialAction::Trade);
    }

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
        // dello stick (stessa convenzione di radialItemAngleOriented), non
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
        float center = radialCenterDeg(radialAnchorX_);
        for (int j = 0; j < n; j++) {
            float ia = radialItemAngleOriented(j, n, center);
            if (ia < 0.0f) ia += 360.0f;
            if (ia >= 360.0f) ia -= 360.0f;
            float diff = std::fabs(ang - ia);
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
#ifdef OH_LINUX
    constexpr int BTN_R = 26, ICON_R = 18;
#else
    constexpr int BTN_R = 34, ICON_R = 24;
#endif
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
            SDL_Color sh = shadowForText(T().text, 220);
            // ombra rinforzata: alone 8 direzioni + leggero offset per staccare dal fondo
            drawTextCentered(lbl, cx + 1, ly + 1, sh, font_);
            drawTextCentered(lbl, cx - 1, ly + 1, sh, font_);
            drawTextCentered(lbl, cx + 1, ly - 1, sh, font_);
            drawTextCentered(lbl, cx - 1, ly - 1, sh, font_);
            drawTextCentered(lbl, cx, ly + 1, sh, font_);
            drawTextCentered(lbl, cx, ly - 1, sh, font_);
            drawTextCentered(lbl, cx + 1, ly, sh, font_);
            drawTextCentered(lbl, cx - 1, ly, sh, font_);
            SDL_Color sh2 = shadowForText(T().text, 140);
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

    // Come isGameLaunchableAt: la tile ROM FRLG non deve mai prendere la
    // strada del titolo nativo (aprirebbe/lancerebbe il gioco sbagliato).
    bool isTitle = isNativeAt(gameSelCursor_) && selectedProfile_ >= 0 &&
                   titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
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
    std::string romPath = importedRomPath(g, occ);
    if (romPath.empty()) return; // nessuna rom risolvibile per questa occorrenza
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
