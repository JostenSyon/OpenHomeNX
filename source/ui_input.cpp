#include "ui.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "pokemon_ffi.h"
#include "led.h"
#include "species_converter.h"
#include "trade_evo.h"
#include "item_locations.h"
#include "pokedex.h"
#include "form_names.h"
#include "personal_za.h"
#include "personal_sv.h"
#include "personal_swsh.h"
#include "personal_bdsp.h"
#include "personal_la.h"
#include "personal_gg.h"
#include "update_net.h"
#include "settings_cfg.h"
#include "remote_sync.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

namespace {
// update.cfg accanto all'NRO (o in sdmc:/switch/OpenHomeNX/). Duplicato a
// posta: readUpdateCfg in ui_selectors.cpp è in un anonymous namespace
// (stesso precedente di readUpdateAutoCfg in autoupdate.cpp).
bool readMainMenuUpdateCfg(const std::string& basePath, std::string& urlOut,
                           std::string& tokenOut) {
    const std::string paths[] = { basePath + "update.cfg",
                                  "sdmc:/switch/OpenHomeNX/update.cfg" };
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (!f.good()) continue;
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
            if (k == "url") urlOut = v;
            else if (k == "token") tokenOut = v;
        }
        if (!urlOut.empty()) return true;
    }
    return false;
}
} // namespace

// --- Joystick ---

void UI::updateStick(int16_t axisX, int16_t axisY) {
    int newDirX = 0, newDirY = 0;
    if (axisX < -STICK_DEADZONE) newDirX = -1;
    else if (axisX > STICK_DEADZONE) newDirX = 1;
    if (axisY < -STICK_DEADZONE) newDirY = -1;
    else if (axisY > STICK_DEADZONE) newDirY = 1;

    if (newDirX != stickDirX_ || newDirY != stickDirY_) {
        stickDirX_ = newDirX;
        stickDirY_ = newDirY;
        stickMoved_ = false;
        stickMoveTime_ = 0;
        stickHoldStart_ = SDL_GetTicks();
    }
}

// --- Input ---

void UI::handleBoxViewInput(const SDL_Event& event) {

    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }
    // Can rename if viewing a bank panel (always in applet, only Bank panel in normal)
    auto canRenameBox = [&]() -> Bank* {
        if (isDualBankMode())
            return (boxViewPanel_ == Panel::Game) ? &bankLeft_ : &bank_;
        return (boxViewPanel_ == Panel::Bank) ? &bank_ : nullptr;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm
                closeBoxView(true);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                closeBoxView(false);
                break;
            case SDL_CONTROLLER_BUTTON_X: // Switch Y = rename
            {
                Bank* b = canRenameBox();
                if (b) {
                    renamingBoxIdx_ = boxViewCursor_;
                    renamingBoxBank_ = b;
                    beginTextInput(TextInputPurpose::RenameBoxName);
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    moveBoxViewCursor(0, -1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:   moveBoxViewCursor(0, +1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:   moveBoxViewCursor(-1, 0); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:  moveBoxViewCursor(+1, 0); break;
        }
    }
}

void UI::handleInput(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return;
        }

        // Any button/key event dirties the screen
        if (event.type == SDL_CONTROLLERBUTTONDOWN ||
            event.type == SDL_CONTROLLERBUTTONUP)
            markDirty();

        if (showMenu_)               { handleMenuInput(event, running); continue; }
        if (showSpeciesListPicker_)  { handleSpeciesListPickerInput(event); continue; }
        if (showSpeciesLetterPicker_){ handleSpeciesLetterPickerInput(event); continue; }
        if (showSearchFilter_)       { handleSearchFilterInput(event); continue; }
        if (showSearchResults_)      { handleSearchResultsInput(event); continue; }
        if (showWondercardList_)     { handleWondercardListInput(event); continue; }
        if (showPkImportList_)       { handlePkImportListInput(event); continue; }
        if (showTradeList_)          { handleTradeListInput(event); continue; }
        if (showGenMonList_)         { handleGenMonListInput(event); continue; }
        if (showLearnset_)           { handleLearnsetInput(event); continue; }
        if (showBoxView_)            { handleBoxViewInput(event); continue; }
        if (showDetail_)             { handleDetailInput(event); continue; }

        handleNormalInput(event);
    }

    handleStickRepeat();
    handleBumperRepeat();
}

void UI::handleMenuInput(const SDL_Event& event, bool& running) {
    bool hasWC = gameInfo(selectedGame_).hasWondercards;
    bool hasExport = !selectedSlots_.empty();
    // "Send current save": solo a save caricato e mai in dual-bank (lì il
    // pannello sinistro è una banca, non il save del gioco aperto).
    bool hasSend = !isDualBankMode() && save_.isLoaded();
    // "Generate test mons": solo debug, mai dual-bank.
    bool hasGen = DebugLog::enabled() && !isDualBankMode();
    // "Scambio": solo a save Gen3 caricato (record Pk3, Fase 1), mai dual-bank.
    bool hasTrade = !isDualBankMode() && save_.isLoaded() && TradeEvo::supported(save_.gameType());
    int menuCount = menuVisibleCount();
    if (menuSelection_ >= menuCount) menuSelection_ = menuCount - 1;
    if (menuSelection_ < 0) menuSelection_ = 0;
    auto menuConfirm = [&]() {
        // 0=Theme, 1=Language, 2=Crypto, 3=Search
        if (menuSelection_ == 0) {
            showThemeSelector_ = true;
            themeSelCursor_ = themeIndex_;
            themeSelOriginal_ = themeIndex_;
            return;
        }
        if (menuSelection_ == 1) {
            langList_ = i18n::availableLangs();
            langSelCursor_ = 0;
            for (int i = 0; i < (int)langList_.size(); i++) {
                if (langList_[i] == i18n::currentLang()) { langSelCursor_ = i; break; }
            }
            showLanguageSelector_ = true;
            return;
        }
        if (menuSelection_ == 2) {
            // Crypto Engine toggle (M3d) — PK ↔ OH, resta nel menu per feedback visivo
            g_cryptoEngine = (g_cryptoEngine == CryptoEngine::PK) ? CryptoEngine::OH : CryptoEngine::PK;
            saveCryptoEngine(basePath_, g_cryptoEngine);
            DebugLog::line("crypto toggle: engine=%s", useOpenHome() ? "OH" : "PK");
            return;
        }
        if (menuSelection_ == 3) {
            showMenu_ = false;
            showSearchFilter_ = true;
            searchFilterCursor_ = 0;
            searchFilter_ = SearchFilter{};
            clearSearchHighlight();
            return;
        }
        // Wondercard (index 4) for SV/SwSh games (shifted +1 per Crypto)
        if (hasWC && menuSelection_ == 4) {
            showMenu_ = false;
            wcList_ = scanWondercards(basePath_, selectedGame_);
            wcListCursor_ = 0;
            wcListScroll_ = 0;
            showWondercardList_ = true;
            return;
        }
        // Export Selected (after Wondercard)
        int exportIdx = hasWC ? 5 : 4;
        if (hasExport && menuSelection_ == exportIdx) {
            showMenu_ = false;
            int exported = 0;
            int failed = 0;
            for (int slot : selectedSlots_) {
                Pokemon pkm = getPokemonAt(selectedBox_, slot, selectedPanel_);
                if (!pkm.isEmpty()) {
                    std::string name = exportPokemon(pkm);
                    if (!name.empty()) exported++;
                    else failed++;
                }
            }
            std::string body = i18n::fmt(StrKey::PokemonExported, std::to_string(exported));
            if (failed > 0) body += "\n" + i18n::fmt(StrKey::ExportFailedCount, std::to_string(failed));
            showMessageAndWait(i18n::get(StrKey::ExportComplete), body);
            return;
        }
        // Import PK files: always visible, right after Export Selected.
        // Opens the .pk1/.pk2 picker (mirrors the wondercard list).
        int importIdx = exportIdx + (hasExport ? 1 : 0);
        if (menuSelection_ == importIdx) {
            showMenu_ = false;
            pkImportList_ = scanPkImportFiles();
            pkImportCursor_ = 0;
            pkImportScroll_ = 0;
            showPkImportList_ = true;
            return;
        }
        // Send current save (indice importIdx+1, +1 se riga Generate debug)
        int genIdx = importIdx + 1;
        if (hasGen && menuSelection_ == genIdx) {
            showMenu_ = false;
            genMonList_ = genMonTable();
            genMonCursor_ = 0;
            genMonScroll_ = 0;
            showGenMonList_ = true;
            return;
        }
        int sendIdx = genIdx + (hasGen ? 1 : 0);
        // Send current save: usa savePath_ già montato dal gioco aperto —
        // NIENTE mount/unmount, NIENTE commit del save.
        if (hasSend && menuSelection_ == sendIdx) {
            showMenu_ = false;
            std::string url, token;
            std::string err;
            if (!readMainMenuUpdateCfg(basePath_, url, token) || url.empty()) {
                showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNoUrl));
            } else if (!updateNetEnsureReady()) {
                showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNetOff));
            } else {
                showWorking(i18n::fmt(StrKey::SendSaveUploading, gameInfo(selectedGame_).gameTag));
                if (updateNetUploadSave(url, token, savePath_, gameInfo(selectedGame_).gameTag, err))
                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveSent));
                else
                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::fmt(StrKey::SendSaveFailed, err));
            }
            return;
        }
        // Scambio self-trade (indice sendIdx+1, dopo Send save).
        int tradeIdx = sendIdx + (hasSend ? 1 : 0);
        if (hasTrade && menuSelection_ == tradeIdx) {
            showMenu_ = false;
            openTradeList();
            return;
        }
        int sel = menuSelection_ - (hasWC ? 5 : 4) - (hasExport ? 1 : 0) - 1 - (hasGen ? 1 : 0) - (hasSend ? 1 : 0) - (hasTrade ? 1 : 0);
        if (isDualBankMode()) {
            // sel: 0=Switch Left Bank, 1=Switch Right Bank, 2=Change Game,
            // 3=Save Banks, 4=Quit
            if (sel == 0) {
                // Need at least 1 bank available that isn't loaded on the right
                int avail = 0;
                for (auto& b : bankManager_.list())
                    if (b.name != activeBankName_) avail++;
                if (avail == 0) {
                    showMenu_ = false;
                    if (!showConfirmDialog(i18n::get(StrKey::NoBanksAvailable),
                            i18n::get(StrKey::CreateNewBank))) return;
                    if (!saveBankFiles()) return;
                    bankManager_.refresh();
                    bankSelTarget_ = Panel::Game;
                    screen_ = AppScreen::BankSelector;
                    beginTextInput(TextInputPurpose::CreateBank);
                    return;
                }
                if (!saveBankFiles()) { showMenu_ = false; return; }
                bankManager_.refresh();
                bankSelTarget_ = Panel::Game;
                screen_ = AppScreen::BankSelector;
                showMenu_ = false;
            } else if (sel == 1) {
                // Need at least 1 bank available that isn't loaded on the left
                int avail = 0;
                for (auto& b : bankManager_.list())
                    if (b.name != leftBankName_) avail++;
                if (avail == 0) {
                    showMenu_ = false;
                    if (!showConfirmDialog(i18n::get(StrKey::NoBanksAvailable),
                            i18n::get(StrKey::CreateNewBank))) return;
                    if (!saveBankFiles()) return;
                    bankManager_.refresh();
                    bankSelTarget_ = Panel::Bank;
                    screen_ = AppScreen::BankSelector;
                    beginTextInput(TextInputPurpose::CreateBank);
                    return;
                }
                if (!saveBankFiles()) { showMenu_ = false; return; }
                bankManager_.refresh();
                bankSelTarget_ = Panel::Bank;
                screen_ = AppScreen::BankSelector;
                showMenu_ = false;
            } else if (sel == 2) {
                // Change Game
                returnToGameSelector();
            } else if (sel == 3) {
                saveBankFiles();
                showMenu_ = false;
            } else {
                // Quit dual-bank: la mano muore al quit, chiedi prima.
                if (!confirmQuitWithHold()) { showMenu_ = false; return; }
                running = false;
            }
        } else {
            // sel: 0=Switch Bank, 1=Change Game, 2=Save & Quit, 3=Quit Without Saving
            if (sel == 0) {
                if (!saveBankFiles()) { showMenu_ = false; return; }
                if (!persistGameSaveIfDirty()) { showMenu_ = false; return; }
                bankManager_.refresh();
                // Cross-gen: the right-panel bank selector lists ALL banks of
                // every game, sectioned by game (same as "All Banks"). A SwSh
                // save can then hold an SV bank on the right; prepareForPlacement
                // converts on drop. openSelectedBank/back restore single-game.
                bankSelTarget_ = Panel::Bank;
                bankManager_.initAll(basePath_);
                bankRightCrossGen_ = true;
                bankSelCursor_ = 0;
                bankSelScroll_ = 0;
                screen_ = AppScreen::BankSelector;
                showMenu_ = false;
            } else if (sel == 1) {
                // Change Game
                returnToGameSelector();
            } else if (sel == 2) {
                saveNow_ = true;
                running = false;
            } else {
                // Quit Without Saving: niente scritture, ma la mano muore
                // comunque al quit — chiedi prima.
                if (!confirmQuitWithHold()) { showMenu_ = false; return; }
                running = false;
            }
        }
    };
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
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                menuSelection_ = (menuSelection_ + menuCount - 1) % menuCount;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                menuSelection_ = (menuSelection_ + 1) % menuCount;
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm
                menuConfirm();
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                showMenu_ = false;
                break;
        }
    }
}

void UI::handleDetailInput(const SDL_Event& event) {
    auto tryRelease = [&]() {
        int box = cursor_.box;
        int slot = cursor_.slot(gridCols());
        // Block releasing LGPE party members
        if (save_.isLGPEPartySlot(box, slot) && cursor_.panel == Panel::Game) {
            showMessageAndWait(i18n::get(StrKey::PartyPokemon),
                i18n::get(StrKey::CantReleaseParty));
            return;
        }
        Pokemon pkm = getPokemonAt(box, slot, cursor_.panel);
        if (pkm.isEmpty()) return;
        std::string name = pkm.displayName();
        if (showConfirmDialog(i18n::get(StrKey::ReleasePokemon),
                i18n::fmt(StrKey::ReleaseConfirm, name))) {
            clearPokemonAt(box, slot, cursor_.panel);
            // Remove from multi-select if selected
            if (!selectedSlots_.empty() && cursor_.panel == selectedPanel_
                && box == selectedBox_) {
                auto it = std::find(selectedSlots_.begin(),
                                    selectedSlots_.end(), slot);
                if (it != selectedSlots_.end())
                    selectedSlots_.erase(it);
            }
            showDetail_ = false;
            refreshHighlightSet();
        }
    };
    // Navigate to prev/next non-empty Pokemon in the box
    auto detailNav = [&](int dir) {
        int cols = gridCols();
        int slots = maxSlots();
        int cur = cursor_.slot(cols);
        for (int step = 1; step < slots; step++) {
            int next = (cur + dir * step % slots + slots) % slots;
            Pokemon pkm = getPokemonAt(cursor_.box, next, cursor_.panel);
            if (!pkm.isEmpty()) {
                cursor_.col = next % cols;
                cursor_.row = next / cols;
                return;
            }
        }
    };
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        const bool partyRO = (detailParty_ >= 0);
        const bool partyROStrict = partyRO && !DebugLog::enabled();
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: // Switch B
                showDetail_ = false;
                detailParty_ = -1;
                break;
            case SDL_CONTROLLER_BUTTON_START: { // Switch + — export (unico)
                if (partyROStrict) break;
                Pokemon pkm = getPokemonAt(cursor_.box, cursor_.slot(gridCols()), cursor_.panel);
                if (!pkm.isEmpty()) {
                    std::string name = exportPokemon(pkm);
                    if (!name.empty())
                        showMessageAndWait(i18n::get(StrKey::Exported), name);
                    else
                        showMessageAndWait(i18n::get(StrKey::ExportFailed), i18n::get(StrKey::CouldNotWrite));
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_B: // Switch A — chiudi (rilascio spostato su -)
                showDetail_ = false;
                detailParty_ = -1;
                break;
            case SDL_CONTROLLER_BUTTON_BACK: // Switch - — rilascia
                if (partyROStrict) break;
                tryRelease();
                break;
            case SDL_CONTROLLER_BUTTON_X: // Switch Y = learnset viewer
                {
                    Pokemon pkm = getPokemonAt(cursor_.box, cursor_.slot(gridCols()), cursor_.panel);
                    if (!pkm.isEmpty())
                        openLearnset(pkm);
                }
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                if (partyROStrict) break;
                detailNav(-1);
                lHeld_ = true;
                bumperRepeatTime_ = SDL_GetTicks();
                bumperMoved_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                if (partyROStrict) break;
                detailNav(1);
                rHeld_ = true;
                bumperRepeatTime_ = SDL_GetTicks();
                bumperMoved_ = false;
                break;
        }
    }
    if (event.type == SDL_CONTROLLERBUTTONUP) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  lHeld_ = false; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: rHeld_ = false; break;
        }
    }
}

void UI::handleNormalInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            bool pressed = event.caxis.value > TRIGGER_DEADZONE;
            if (pressed && !zlPressed_ &&
                !(isDualBankMode() && leftBankName_.empty())) {
                openBoxView(Panel::Game);
                markDirty();
            }
            zlPressed_ = pressed;
        }
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            bool pressed = event.caxis.value > TRIGGER_DEADZONE;
            if (pressed && !zrPressed_) {
                openBoxView(Panel::Bank);
                markDirty();
            }
            zrPressed_ = pressed;
        }
    }

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_B: // Switch A (right) = SDL B -> pick / take
                if (!yHeld_) {
                    if (partyCursor_ >= 0) {
                        // La cella box puntata (solo LGPE, -1 altrove): pick e
                        // place la cambiano su disco ma la vista box la cacha
                        // (slotDisplayCache_) — senza invalidate resta
                        // appiccicata fino al prossimo place (celle gialle
                        // fantasma). Si invalida qui sotto, pre e post.
                        int preFlat = save_.lgpeFlatOfParty(partyCursor_);
                        if (!canEditParty()) {
                            // Sola lettura (no debug, o GB/GBC): dettaglio come Y,
                            // mano intatta.
                            Pokemon pm = save_.getPartySlot(partyCursor_);
                            if (!pm.isEmpty()) {
                                detailParty_ = partyCursor_;
                                showDetail_ = true;
                            }
                        } else if (holding_) {
                            // Place held mon into party slot (hand stays on strip)
                            Pokemon target = save_.getPartySlot(partyCursor_);
                            if (target.isEmpty()) {
                                std::string whyNot;
                                if (!prepareForPlacement(heldPkm_, Panel::Game, whyNot)) {
                                    showMessageAndWait(i18n::get(StrKey::TransferTitle), whyNot);
                                } else if (heldFromParty_) {
                                    // Pick already cleared the origin slot: never clear heldPartyIdx_
                                    // here — after a swap it holds the just-placed mon (box->slot1
                                    // then hand->slot2 wiped slot1). Just place.
                                    save_.setPartySlot(partyCursor_, heldPkm_);
                                    holding_=false; heldPkm_=Pokemon{}; heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
                                    heldFromLGPEParty_=false; lgpeHeldPartyIdx_=-1;
                                } else {
                                    save_.setPartySlot(partyCursor_, heldPkm_);
                                    holding_=false; heldPkm_=Pokemon{}; heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
                                    heldFromLGPEParty_=false; lgpeHeldPartyIdx_=-1;
                                    swapHistory_.clear();
                                }
                            } else {
                                std::string whyNot;
                                Pokemon tmp = heldPkm_;
                                DebugLog::line("party swap: held %s (%u) -> slot %d occupied %s (%u), heldFromParty=%d heldIdx=%d",
                                    heldPkm_.displayName().c_str(), heldPkm_.species(), partyCursor_, target.displayName().c_str(), target.species(), heldFromParty_?1:0, heldPartyIdx_);
                                if (!prepareForPlacement(tmp, Panel::Game, whyNot)) {
                                    showMessageAndWait(i18n::get(StrKey::TransferTitle), whyNot);
                                } else {
                                    save_.setPartySlot(partyCursor_, tmp);
                                    heldPkm_ = target;
                                    heldPartyIdx_ = partyCursor_;
                                    heldFromParty_ = true;
                                    // heldPartyOrig_ untouched: still the pick origin (for cancel
                                    // undo); box-origin swaps keep swapHistory for the box side.
                                    DebugLog::line("party after: slot %d=%s hand=%s", partyCursor_, save_.getPartySlot(partyCursor_).displayName().c_str(), heldPkm_.displayName().c_str());
                                }
                            }
                            refreshHighlightSet();
                        } else {
                            // Pick from party: hand stays on strip, mon disappears from strip.
                            // Debug: anche l'ultimo mon si puo prendere (party svuotabile);
                            // l'exit-hook (ensurePartyOnExit) offre il Caterpie prima del save.
                            Pokemon pm = save_.getPartySlot(partyCursor_);
                            if (!pm.isEmpty()) {
                                DebugLog::line("party pick: slot=%d spc=%u gt=%d ec=%08x iv32=%08x egg=%d",
                                    partyCursor_, pm.species(), (int)pm.gameType_,
                                    pm.encryptionConstant(), pm.iv32(), pm.isEgg() ? 1 : 0);
                                heldPkm_ = pm;
                                holding_ = true;
                                heldFromParty_ = true;
                                heldPartyIdx_ = partyCursor_;
                                heldPartyOrig_ = partyCursor_;
                                heldMulti_.clear();
                                if (isLGPE(selectedGame_)) {
                                    lgpeHeldPartyIdx_ = partyCursor_;
                                    heldFromLGPEParty_ = true;
                                }
                                // LGPE: clearPartySlot svuota pointer E cella insieme
                                // (LGPE esente dal guard: sempre subito).
                                save_.clearPartySlot(partyCursor_);
                                refreshHighlightSet();
                            }
                        }
                        // Invalida la vista delle celle toccate (pre e post):
                        // la box view cacha i display e resterebbe appiccicata.
                        {
                            int spb = save_.slotsPerBox();
                            if (spb > 0) {
                                if (preFlat >= 0)
                                    invalidateSlotDisplay(Panel::Game, preFlat / spb);
                                int postFlat = save_.lgpeFlatOfParty(partyCursor_);
                                if (postFlat >= 0 && postFlat != preFlat)
                                    invalidateSlotDisplay(Panel::Game, postFlat / spb);
                            }
                        }
                    } else {
                        actionSelect();
                    }
                    refreshHighlightSet();
                }
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B (bottom) = SDL A -> back / cancel
                if (!yHeld_) {
                    if (partyCursor_ >= 0) {
                        if (holding_ && heldFromParty_) {
                            // Undo without loss: box-origin swap restores the box source
                            // too, pure-party swap restores both party slots.
                            // VERBATIM: si rimette src.pkm (l'originale, con blob
                            // OHPKM se da banca), MAI inD (la versione convertita
                            // per la strip non rientra nelle celle blob e il
                            // guard xbank la rifiuterebbe perdendo il mon —
                            // Smeraldo: Pikachu banca sparito al cancel).
                            if (!swapHistory_.empty()) {
                                auto src = swapHistory_.back();
                                setPokemonAt(src.box, src.slot, src.panel, src.pkm);
                                save_.setPartySlot(heldPartyIdx_, heldPkm_);
                            } else if (heldPartyOrig_ >= 0 && heldPartyOrig_ != heldPartyIdx_) {
                                Pokemon curD = save_.getPartySlot(heldPartyIdx_);
                                save_.setPartySlot(heldPartyIdx_, heldPkm_);
                                save_.setPartySlot(heldPartyOrig_, curD);
                            } else {
                                save_.setPartySlot(heldPartyIdx_, heldPkm_);
                            }
                            // Come sopra: le celle LGPE toccate dal restore
                            // vanno invalidate nella vista box.
                            {
                                int spb = save_.slotsPerBox();
                                if (spb > 0) {
                                    int f1 = save_.lgpeFlatOfParty(heldPartyIdx_);
                                    if (f1 >= 0) invalidateSlotDisplay(Panel::Game, f1 / spb);
                                    if (heldPartyOrig_ >= 0 && heldPartyOrig_ != heldPartyIdx_) {
                                        int f2 = save_.lgpeFlatOfParty(heldPartyOrig_);
                                        if (f2 >= 0 && f2 != f1)
                                            invalidateSlotDisplay(Panel::Game, f2 / spb);
                                    }
                                }
                            }
                            holding_=false; heldPkm_=Pokemon{}; heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
                            heldFromLGPEParty_=false; lgpeHeldPartyIdx_=-1;
                            swapHistory_.clear();
                        } else {
                            partyCursor_ = -1; // leave the OT strip
                        }
                    } else {
                        actionCancel();
                    }
                    refreshHighlightSet();
                }
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Switch X (top) = SDL Y -> detail only (release moved to -)
            {
                if (yHeld_) break;
                if (!holding_) {
                    // Party row: same Y opens detail for the focused party mon (always viewable)
                    if (partyCursor_ >= 0 && partyCursor_ < (int)save_.dsParty().size()) {
                        const auto& pm = save_.dsParty()[partyCursor_];
                        if (!pm.isEmpty()) {
                            detailParty_ = partyCursor_;
                            showDetail_ = true;
                        }
                    } else {
                        Pokemon pkm = getPokemonAt(cursor_.box, cursor_.slot(gridCols()), cursor_.panel);
                        if (!pkm.isEmpty())
                            showDetail_ = true;
                    }
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_X: // Switch Y (left) = SDL X
                beginYPress();
                break;
            case SDL_CONTROLLER_BUTTON_START: // + (open menu)
                if (!yHeld_) {
                    showMenu_ = true;
                    menuSelection_ = 0;
                }
                break;
            case SDL_CONTROLLER_BUTTON_BACK: // - : release held, else about
                if (!yHeld_) {
                    if (holding_) {
                        if (heldFromLGPEParty_) {
                            showMessageAndWait(i18n::get(StrKey::PartyPokemon),
                                i18n::get(StrKey::CantReleaseParty));
                            break;
                        }
                        int count = heldMulti_.empty() ? 1 : (int)heldMulti_.size();
                        std::string msg = i18n::fmt(StrKey::ReleaseMultiConfirm, std::to_string(count));
                        if (showConfirmDialog(i18n::get(StrKey::ReleasePokemon), msg)) {
                            heldMulti_.clear();
                            heldMultiSlots_.clear();
                            heldPkm_ = Pokemon{};
                            swapHistory_.clear();
                            holding_ = false;
                            positionPreserve_ = false;
                            heldFromLGPEParty_ = false;
                            lgpeHeldPartyIdx_ = -1;
                            heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
                            refreshHighlightSet();
                        }
                    } else {
                        showAbout_ = true;
                    }
                }
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                if (!yHeld_) {
                    switchBox(-1);
                    lHeld_ = true;
                    bumperRepeatTime_ = SDL_GetTicks();
                    bumperMoved_ = false;
                }
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                if (!yHeld_) {
                    switchBox(+1);
                    rHeld_ = true;
                    bumperRepeatTime_ = SDL_GetTicks();
                    bumperMoved_ = false;
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                // Unified row: party is row -1 above grid. Both dpad and analog can enter it.
                // Single selection: box cursor hidden when party focused.
                // Allow empty party when holding so you can put the last mon back (Spada 1/0 bug).
                if (partyCursor_ < 0 && selectedSlots_.empty() &&
                    cursor_.panel == Panel::Game && cursor_.row == 0 && (save_.hasParty() || holding_)) {
                    partyCursor_ = 0;
                    markDirty();
                } else if (partyCursor_ >= 0) {
                    partyCursor_ = -1; // back to grid
                    markDirty();
                } else {
                    moveCursor(0, -1);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (partyCursor_ >= 0) {
                    partyCursor_ = -1;
                    markDirty();
                } else {
                    moveCursor(0, +1);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (partyCursor_ >= 0) {
                    // Party sempre 6 slot, anche se alcuni vuoti (Smeraldo 5/6
                    // bloccava lo slot 6: dsParty().size()==5 → modulo 5 non
                    // raggiunge mai l'indice 5).
                    int n = 6;
                    partyCursor_ = (partyCursor_ + n - 1) % n;
                    markDirty();
                } else {
                    moveCursor(-1, 0);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (partyCursor_ >= 0) {
                    int n = 6;
                    partyCursor_ = (partyCursor_ + 1) % n;
                    markDirty();
                } else {
                    moveCursor(+1, 0);
                }
                break;
        }
    }

    if (event.type == SDL_CONTROLLERBUTTONUP) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_X: // Switch Y released
                endYPress();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                lHeld_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                rHeld_ = false;
                break;
        }
    }

}

bool UI::canEditParty() const {
    if (!save_.isLoaded())
        return false;
    // Gen5: save intero read-only (footer CRC a blocchi) — editarne il party
    // in memoria per poi fallire il save sarebbe perdita mascherata da edit.
    if (isGen5File(save_.gameType()))
        return false;
    // Tutte le altre famiglie (incl. GB/GBC con write-back): solo con debug
    // attivo (toggle dedicato in futuro).
    return DebugLog::enabled();
}

void UI::handleStickRepeat() {
    if (stickDirX_ == 0 && stickDirY_ == 0) return;

    uint32_t now = SDL_GetTicks();
    uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
    if (now - stickMoveTime_ < delay) return;

    markDirty();
    if (showSpeciesLetterPicker_) {
        constexpr int COLS = 2;
        constexpr int TOTAL_ITEMS = 27;
        int dx = stickDirX_ > 0 ? 1 : (stickDirX_ < 0 ? -1 : 0);
        int dy = stickDirY_ > 0 ? 1 : (stickDirY_ < 0 ? -1 : 0);
        int col = speciesLetterCursor_ % COLS;
        int row = speciesLetterCursor_ / COLS;
        int totalRows = (TOTAL_ITEMS + COLS - 1) / COLS;
        for (int attempt = 0; attempt < TOTAL_ITEMS; attempt++) {
            col += dx; row += dy;
            if (row < 0) row = totalRows - 1;
            if (row >= totalRows) row = 0;
            if (col < 0) col = COLS - 1;
            if (col >= COLS) col = 0;
            int idx = row * COLS + col;
            if (idx >= TOTAL_ITEMS) { col = 0; idx = row * COLS; }
            if (letterHasSpecies(idx)) {
                speciesLetterCursor_ = idx;
                break;
            }
        }
    } else if (showSpeciesListPicker_) {
        constexpr int COLS = 3;
        int total = static_cast<int>(speciesPickerList_.size());
        if (total > 0) {
            int col = speciesListCursor_ % COLS;
            int row = speciesListCursor_ / COLS;
            int totalRows = (total + COLS - 1) / COLS;
            if (stickDirY_ != 0) {
                row += stickDirY_ > 0 ? 1 : -1;
                row = (row + totalRows) % totalRows;
            }
            if (stickDirX_ != 0) {
                col += stickDirX_ > 0 ? 1 : -1;
                col = (col + COLS) % COLS;
            }
            int idx = row * COLS + col;
            if (idx >= total) idx = total - 1;
            speciesListCursor_ = idx;
        }
    } else if (showSearchFilter_) {
        bool alpha = (selectedGame_ == GameType::LA || selectedGame_ == GameType::ZA);
        if (stickDirY_ != 0) {
            int dir = stickDirY_ > 0 ? 1 : -1;
            searchFilterCursor_ = (searchFilterCursor_ + dir + 12) % 12;
            if (!alpha && searchFilterCursor_ == 4)
                searchFilterCursor_ = (searchFilterCursor_ + dir + 12) % 12;
        }
        if (stickDirX_ != 0 && searchFilterCursor_ == 6)
            searchLevelFocus_ = stickDirX_ > 0 ? 1 : 0;
        if (stickDirX_ != 0 && searchFilterCursor_ == 9)
            searchFilter_.mode = stickDirX_ > 0 ? SearchMode::Highlight : SearchMode::List;
    } else if (showSearchResults_) {
        if (stickDirY_ != 0 && !searchResults_.empty()) {
            searchResultCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (searchResultCursor_ < 0) searchResultCursor_ = 0;
            if (searchResultCursor_ >= (int)searchResults_.size())
                searchResultCursor_ = (int)searchResults_.size() - 1;
            int visibleRows = 12;
            if (searchResultCursor_ < searchResultScroll_)
                searchResultScroll_ = searchResultCursor_;
            if (searchResultCursor_ >= searchResultScroll_ + visibleRows)
                searchResultScroll_ = searchResultCursor_ - visibleRows + 1;
        }
    } else if (showWondercardList_) {
        if (stickDirY_ != 0 && !wcList_.empty()) {
            int count = static_cast<int>(wcList_.size());
            wcListCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (wcListCursor_ < 0) wcListCursor_ = count - 1;
            if (wcListCursor_ >= count) wcListCursor_ = 0;
            constexpr int ROW_H = 36;
            int visibleRows = (550 - 40 - 50) / ROW_H;
            if (wcListCursor_ < wcListScroll_)
                wcListScroll_ = wcListCursor_;
            else if (wcListCursor_ >= wcListScroll_ + visibleRows)
                wcListScroll_ = wcListCursor_ - visibleRows + 1;
        }
    } else if (showPkImportList_) {
        if (stickDirY_ != 0 && !pkImportList_.empty()) {
            int count = static_cast<int>(pkImportList_.size());
            pkImportCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (pkImportCursor_ < 0) pkImportCursor_ = count - 1;
            if (pkImportCursor_ >= count) pkImportCursor_ = 0;
            constexpr int ROW_H = 36;
            int visibleRows = (550 - 40 - 50) / ROW_H;
            if (pkImportCursor_ < pkImportScroll_)
                pkImportScroll_ = pkImportCursor_;
            else if (pkImportCursor_ >= pkImportScroll_ + visibleRows)
                pkImportScroll_ = pkImportCursor_ - visibleRows + 1;
        }
    } else if (showTradeList_) {
        if (stickDirY_ != 0 && !tradeCandidates_.empty()) {
            int count = static_cast<int>(tradeCandidates_.size());
            tradeCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (tradeCursor_ < 0) tradeCursor_ = count - 1;
            if (tradeCursor_ >= count) tradeCursor_ = 0;
            constexpr int VISIBLE = 6;
            if (tradeCursor_ < tradeScroll_) tradeScroll_ = tradeCursor_;
            else if (tradeCursor_ >= tradeScroll_ + VISIBLE) tradeScroll_ = tradeCursor_ - VISIBLE + 1;
        }
    } else if (showGenMonList_) {
        if (stickDirY_ != 0 && !genMonList_.empty()) {
            int count = static_cast<int>(genMonList_.size());
            genMonCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (genMonCursor_ < 0) genMonCursor_ = count - 1;
            if (genMonCursor_ >= count) genMonCursor_ = 0;
            constexpr int VISIBLE = 12;
            if (genMonCursor_ < genMonScroll_)
                genMonScroll_ = genMonCursor_;
            else if (genMonCursor_ >= genMonScroll_ + VISIBLE)
                genMonScroll_ = genMonCursor_ - VISIBLE + 1;
        }
    } else if (showLearnset_) {
        if (stickDirY_ != 0 && !learnset_.empty()) {
            int count = static_cast<int>(learnset_.size());
            learnsetCursor_ += stickDirY_ > 0 ? 1 : -1;
            if (learnsetCursor_ < 0) learnsetCursor_ = count - 1;
            if (learnsetCursor_ >= count) learnsetCursor_ = 0;
            constexpr int ROW_H = 36;
            int visibleRows = (550 - 40 - 50) / ROW_H;
            if (learnsetCursor_ < learnsetScroll_)
                learnsetScroll_ = learnsetCursor_;
            else if (learnsetCursor_ >= learnsetScroll_ + visibleRows)
                learnsetScroll_ = learnsetCursor_ - visibleRows + 1;
        }
    } else if (showBoxView_) {
        if (stickDirX_ != 0) moveBoxViewCursor(stickDirX_, 0);
        if (stickDirY_ != 0) moveBoxViewCursor(0, stickDirY_);
    } else if (showMenu_) {
        if (stickDirY_ != 0)
        {
            int menuCount = menuVisibleCount();
            menuSelection_ = (menuSelection_ + (stickDirY_ > 0 ? 1 : menuCount - 1)) % menuCount;
        }
    } else if (partyCursor_ >= 0) {
        // Party row focused: stick mirrors dpad (sempre 6 slot)
        if (stickDirX_ != 0) {
            int n = 6;
            partyCursor_ = (partyCursor_ + (stickDirX_ > 0 ? 1 : n - 1)) % n;
        }
        if (stickDirY_ != 0) {
            partyCursor_ = -1; // any vertical stick -> back to grid
        }
    } else if (!showDetail_) {
        // Analog up from top row enters party (mirrors dpad) — even while holding so you can drop back (empty party allowed when holding)
        if (stickDirY_ < 0 && cursor_.row == 0 && cursor_.panel == Panel::Game &&
            (save_.hasParty() || holding_) && selectedSlots_.empty()) {
            partyCursor_ = 0;
        } else {
            if (stickDirX_ != 0) moveCursor(stickDirX_, 0);
            if (stickDirY_ != 0) moveCursor(0, stickDirY_);
        }
    }
    stickMoveTime_ = now;
    stickMoved_ = true;
}

void UI::handleBumperRepeat() {
    if (!lHeld_ && !rHeld_) return;

    uint32_t now = SDL_GetTicks();
    uint32_t delay = bumperMoved_ ? BUMPER_REPEAT_DELAY : BUMPER_INITIAL_DELAY;
    if (now - bumperRepeatTime_ < delay) return;

    markDirty();
    if (showSearchResults_ && !searchResults_.empty()) {
        // Page through search results
        int dir = lHeld_ ? -10 : 10;
        searchResultCursor_ += dir;
        if (searchResultCursor_ < 0) searchResultCursor_ = 0;
        if (searchResultCursor_ >= (int)searchResults_.size())
            searchResultCursor_ = (int)searchResults_.size() - 1;
        int visibleRows = 12;
        if (searchResultCursor_ < searchResultScroll_)
            searchResultScroll_ = searchResultCursor_;
        if (searchResultCursor_ >= searchResultScroll_ + visibleRows)
            searchResultScroll_ = searchResultCursor_ - visibleRows + 1;
    } else if (showDetail_) {
        // Navigate to prev/next non-empty slot
        int dir = lHeld_ ? -1 : 1;
        int cols = gridCols();
        int slots = maxSlots();
        int cur = cursor_.slot(cols);
        for (int step = 1; step < slots; step++) {
            int next = (cur + dir * step % slots + slots) % slots;
            Pokemon pkm = getPokemonAt(cursor_.box, next, cursor_.panel);
            if (!pkm.isEmpty()) {
                cursor_.col = next % cols;
                cursor_.row = next / cols;
                break;
            }
        }
    } else if (!showMenu_ && !showSearchFilter_ &&
               !showBoxView_) {
        // Box switching
        if (lHeld_) switchBox(-1);
        if (rHeld_) switchBox(+1);
    }
    bumperRepeatTime_ = now;
    bumperMoved_ = true;
}

void UI::moveCursor(int dx, int dy) {
    Panel prevPanel = cursor_.panel;
    int cols = gridCols();
    int maxCol = cols - 1;
    cursor_.col += dx;
    cursor_.row += dy;

    if (yHeld_) {
        // During drag: clamp to same panel, no wrapping
        int maxRow = gridRowsFor(cursor_.panel) - 1;
        if (cursor_.col < 0) cursor_.col = 0;
        if (cursor_.col > maxCol) cursor_.col = maxCol;
        if (cursor_.row < 0) cursor_.row = 0;
        if (cursor_.row > maxRow) cursor_.row = maxRow;

        if (cursor_.col != dragAnchorCol_ || cursor_.row != dragAnchorRow_)
            yDragActive_ = true;
        updateDragSelection();
        return;
    }

    // Wrap row
    int wrapMaxRow = gridRowsFor(cursor_.panel) - 1;
    if (cursor_.row < 0) cursor_.row = wrapMaxRow;
    if (cursor_.row > wrapMaxRow) cursor_.row = 0;

    // Horizontal: crossing panel boundary or wrapping within single panel
    if (cursor_.col < 0) {
        if (cursor_.panel == Panel::Bank &&
            !(isDualBankMode() && leftBankName_.empty())) {
            cursor_.panel = Panel::Game;
            cursor_.col = maxCol;
            cursor_.box = gameBox_;
        } else if (cursor_.panel == Panel::Game) {
            cursor_.panel = Panel::Bank;
            cursor_.col = maxCol;
            cursor_.box = bankBox_;
        } else {
            cursor_.col = maxCol; // wrap within same panel
        }
    }
    if (cursor_.col > maxCol) {
        if (cursor_.panel == Panel::Game) {
            cursor_.panel = Panel::Bank;
            cursor_.col = 0;
            cursor_.box = bankBox_;
        } else if (!(isDualBankMode() && leftBankName_.empty())) {
            cursor_.panel = Panel::Game;
            cursor_.col = 0;
            cursor_.box = gameBox_;
        } else {
            cursor_.col = 0; // wrap within same panel
        }
    }

    // Panels can be different widths (25-slot Let's Go save = 5 wide, a
    // 30-slot bank / the cross-gen bank = 6). After a panel cross, re-clamp
    // the column to the panel we actually landed in.
    int destMaxCol = gridColsFor(cursor_.panel) - 1;
    if (cursor_.col > destMaxCol) cursor_.col = destMaxCol;
    if (cursor_.col < 0)          cursor_.col = 0;
    int destMaxRow = gridRowsFor(cursor_.panel) - 1;
    if (cursor_.row > destMaxRow) cursor_.row = destMaxRow;
    if (cursor_.row < 0)          cursor_.row = 0;

    if (cursor_.panel != prevPanel)
        clearSelection();
}

void UI::switchBox(int direction) {
    if (yHeld_)
        return;
    clearSelection();
    int maxBox;
    if (cursor_.panel == Panel::Game) {
        if (isDualBankMode())
            maxBox = leftBankName_.empty() ? 1 : bankLeft_.boxCount();
        else
            maxBox = save_.boxCount();
    } else {
        maxBox = bank_.boxCount();
    }
    cursor_.box += direction;
    if (cursor_.box < 0) cursor_.box = maxBox - 1;
    if (cursor_.box >= maxBox) cursor_.box = 0;

    if (cursor_.panel == Panel::Game)
        gameBox_ = cursor_.box;
    else
        bankBox_ = cursor_.box;
}

Pokemon UI::getPokemonAt(int box, int slot, Panel panel) const {
    if (panel == Panel::Game) {
        if (isDualBankMode()) {
            if (leftBankName_.empty()) return Pokemon{};
            return bankLeft_.getSlot(box, slot);
        }
        return save_.getBoxSlot(box, slot);
    }
    if (bank_.isCrossGen()) {
        const std::vector<uint8_t>& blob = bank_.ohpkmAt(box, slot);
        if (blob.empty()) return Pokemon{};
        // The OHPKM blob is the source of truth; attach it FIRST so the mon is
        // never "lost" even if every preview conversion below fails.
        Pokemon p{};
        p.ohpkmBlob_ = blob;

        PkmHandle* h = OpenHomeNX::loadOhpkm(blob);
        if (h) {
            // Materialize a preview in the first format the C++ offset tables
            // can actually render. PK9 alone is wrong: a dex-cut species (e.g.
            // Pidgey, not in the Paldea dex) fails PK9 conversion and the slot
            // would render as empty even though the mon is safely stored.
            static const struct { uint32_t gen; GameType gt; } kPreviewFmts[] = {
                {9, GameType::S},  {8, GameType::Sw}, {13, GameType::GP},
                {10, GameType::LA}, {12, GameType::BD}, {11, GameType::ZA},
                {3, GameType::FR}, {1, GameType::RED}, {2, GameType::GOLD},
            };
            bool rendered = false;
            for (const auto& f : kPreviewFmts) {
                std::vector<uint8_t> b = OpenHomeNX::getPkmBoxBytesForGen(h, f.gen);
                if (b.empty() || b.size() > p.data.size()) continue;
                p.data.fill(0);
                std::memcpy(p.data.data(), b.data(), b.size());
                p.gameType_ = f.gt;
                rendered = true;
                break;
            }
            if (!rendered) {
                // No target dex accepted it. Fall back to a minimal PK9-shaped
                // stub from the raw OHPKM header so the slot stays visible and
                // the user can still move it out (the move uses ohpkmBlob_).
                uint16_t sp = OpenHomeNX::ohpkmSpecies(h);
                if (sp != 0) {
                    p.data.fill(0);
                    p.data[0] = 1; // non-zero EC so isEmpty() is false
                    p.data[8] = static_cast<uint8_t>(sp & 0xFF);
                    p.data[9] = static_cast<uint8_t>(sp >> 8);
                    uint16_t fm = OpenHomeNX::ohpkmForm(h);
                    p.data[0x18] = static_cast<uint8_t>(fm & 0xFF);
                    p.data[0x19] = static_cast<uint8_t>((fm >> 8) & 0x01);
                    p.gameType_ = GameType::S;
                    DebugLog::line("xbank view: b%d s%d blob=%zu spc=%u -> stub preview",
                                   box, slot, blob.size(), sp);
                } else {
                    DebugLog::line("xbank view: b%d s%d blob=%zu -> preview fail (no species)",
                                   box, slot, blob.size());
                }
            }
            OpenHomeNX::freePkm(h);
        } else {
            DebugLog::line("xbank view: b%d s%d blob=%zu -> loadOhpkm NULL",
                           box, slot, blob.size());
        }
        return p;
    }
    return bank_.getSlot(box, slot);
}

void UI::setPokemonAt(int box, int slot, Panel panel, const Pokemon& pkm) {
    if (panel == Panel::Game) {
        if (isDualBankMode()) {
            if (leftBankName_.empty()) return;
            bankLeft_.setSlot(box, slot, pkm);
            // In dual-bank la banca sinistra può essere dello stesso gioco del save
            // caricato (es. Smeraldo). Il box della banca non aggiorna il dex del
            // save, ma in-game il dex deve risultare posseduto — sincronizziamo
            // subito il SaveFile in memoria.
            if (save_.isLoaded() && !pkm.isEmpty() && !pkm.isEgg() &&
                save_.gameType() == bankLeft_.gameType()) {
                Pokedex::registerPokemon(save_, pkm);
            }
        } else
            save_.setBoxSlot(box, slot, pkm);
    } else if (bank_.isCrossGen()) {
        // prepareForPlacement() has already put the OHPKM on pkm.ohpkmBlob_ (or
        // refused). Only write when we actually have a record — never clear a
        // slot on an empty blob, so a mon can't silently vanish here.
        if (pkm.isEmpty())
            bank_.clearOhpkmAt(box, slot);
        else if (!pkm.ohpkmBlob_.empty())
            bank_.setOhpkmAt(box, slot, pkm.ohpkmBlob_);
        else
            // Non-empty mon with no OHPKM blob reaching this point is a bug in
            // the placement pipeline (prepareForPlacement should have filled it
            // or refused). Do NOT touch the slot; leave a loud trace instead.
            DebugLog::line("xbank in: BUG b%d s%d non-empty mon spc=%u but ohpkmBlob_ empty -> slot left as-is",
                           box, slot, pkm.species());
    } else {
        bank_.setSlot(box, slot, pkm);
    }
    invalidateSlotDisplay(panel, box);
}

// M6 transfer-on-drop. Mirrors setPokemonAt's dispatch: a placement is
// ultimately re-encrypted with the destination's own SIZE_*, so the
// destination decides the format.
GameType UI::destGameFor(Panel panel) const {
    if (panel == Panel::Game) {
        if (isDualBankMode()) return bankLeft_.gameType();
        return save_.gameType();
    }
    return bank_.gameType();
}

bool UI::destIsCrossGenBank(Panel panel) const {
    return panel == Panel::Bank && bank_.isCrossGen();
}

// True if the current record differs from its OriginalBackup in user-meaningful
// fields (moves, level, nickname, held item). The backup is [tag u16 LE][record];
// it is parsed with the offsets of its own format via a temp Pokemon, so no FFI
// round-trip is needed and volatile fields (met date, PP, friendship) can never
// trigger a false positive. Tags with no local GameType (Pk7/Alola) return false
// (unverifiable -> no fallback, explicit fail). EXP-without-level and ribbon-only
// edits are NOT detected (known limit, see PKHeX SAV7 reference).
static bool monEditedSinceBackup(const Pokemon& cur, const std::vector<uint8_t>& backup) {
    if (backup.size() <= 2)
        return false;
    const int bkTag = backup[0] | (backup[1] << 8);
    GameType bkGame;
    switch (bkTag) {
        case 1:  bkGame = GameType::RED; break; // Pk1 (offsets via Gen1 branches)
        case 2:  bkGame = GameType::GOLD; break; // Pk2 (offsets via Gen2 branches)
        case 3:  bkGame = GameType::FR; break; // Pk3
        case 8:  bkGame = GameType::GP; break; // Pb7 (LGPE)
        case 9:  bkGame = GameType::Sw; break; // Pk8
        case 10: bkGame = GameType::LA; break; // Pa8
        case 11: bkGame = GameType::BD; break; // Pb8
        case 12: bkGame = GameType::S;  break; // Pk9
        case 13: bkGame = GameType::ZA; break; // Pa9
        default: return false; // incl. Pk7: no local offsets table
    }
    Pokemon old;
    old.data.fill(0);
    const size_t rec = backup.size() - 2;
    std::memcpy(old.data.data(), backup.data() + 2,
                rec <= old.data.size() ? rec : old.data.size());
    old.gameType_ = bkGame;
    if (cur.move1() != old.move1() || cur.move2() != old.move2() ||
        cur.move3() != old.move3() || cur.move4() != old.move4())
        return true;
    if (cur.level() != old.level())
        return true;
    if (bkTag == 1 || bkTag == 2)
        return false; // Pk1/Pk2 backups are bare records (no OT/nick/item to compare)
    if (cur.nickname() != old.nickname())
        return true;
    if (cur.heldItem() != old.heldItem())
        return true;
    return false;
}

// Seconda scelta (nostra, NON upstream): se la ricostruzione Rust fallisce ma
// il mon porta ancora l'OriginalBackup del formato di destinazione ed è
// intatto dalla conversione, ripristina quei byte verbatim invece di fallire.
// Se è stato modificato nel frattempo, il restore cancellerebbe mosse/livelli
// guadagnati → si rifiuta (il chiamante fallisce con il suo whyNot, il mon
// resta in mano intatto). Mai perdita silenziosa. Non muta pkm se ritorna false.
bool UI::restoreVerbatimFallback(Pokemon& pkm, GameType dest, int dstGen) const {
    if (pkm.ohBackup_.size() < 2)
        return false;
    const int bkTag = pkm.ohBackup_[0] | (pkm.ohBackup_[1] << 8);
    const size_t rec = pkm.ohBackup_.size() - 2;
    if (!(bkTag == ohBackupTagForGen(dstGen) && rec > 0 && rec <= pkm.data.size()))
        return false;
    if (monEditedSinceBackup(pkm, pkm.ohBackup_))
        return false;
    pkm.data.fill(0);
    std::memcpy(pkm.data.data(), pkm.ohBackup_.data() + 2, rec);
    pkm.gameType_ = dest;
    DebugLog::line("xfer: primary failed, verbatim fallback (tag %d)", bkTag);
    // Keep ohBackup_: data == backup now, still valid for a re-transfer.
    return true;
}

bool UI::prepareForPlacement(Pokemon& pkm, Panel panel, std::string& whyNot) const {
    if (pkm.isEmpty()) return true;

    // setPokemonAt silently returns without writing when the left bank slot has
    // no bank loaded. Refuse up front instead: otherwise we would convert the
    // Pokemon for a placement that never happens, mutating what is in hand.
    if (panel == Panel::Game && isDualBankMode() && leftBankName_.empty()) {
        whyNot = i18n::get(StrKey::TransferNoBankLeft);
        return false;
    }

    // Cross-gen bank destination: slots hold OHPKM. Build the OHPKM HERE so a
    // failure refuses the drop (the mon stays in hand) instead of vanishing.
    if (destIsCrossGenBank(panel)) {
        if (!pkm.ohpkmBlob_.empty())
            return true; // already OHPKM (from another cross-gen bank) — just move it
        if (!useOpenHome()) {
            whyNot = i18n::get(StrKey::TransferNeedOhBank);
            return false;
        }
        const int g = ohSourceGenFor(pkm.gameType_);
        if (g == 0) {
            whyNot = i18n::fmt(StrKey::TransferCantReadSrc,
                               gameDisplayNameOf(pkm.gameType_));
            return false;
        }
        int sz = ohRecordBytesFor(g);
        if (sz <= 0 || sz > (int)pkm.data.size()) sz = (int)pkm.data.size();
        std::vector<uint8_t> src(pkm.data.begin(), pkm.data.begin() + sz);
        PkmHandle* h = OpenHomeNX::loadPkmFromGen(src, static_cast<uint32_t>(g));
        if (!h) {
            DebugLog::line("xbank in: %s g%d recBytes=%d loadPkmFromGen -> NULL",
                           gameDisplayNameOf(pkm.gameType_), g, sz);
            whyNot = i18n::fmt(StrKey::TransferBadRecord, std::to_string(g));
            return false;
        }
        std::vector<uint8_t> blob = OpenHomeNX::getOhpkmBytes(h);
        OpenHomeNX::freePkm(h);
        DebugLog::line("xbank in: %s g%d -> OHPKM %zu B",
                       gameDisplayNameOf(pkm.gameType_), g, blob.size());
        if (blob.empty()) {
            whyNot = i18n::get(StrKey::TransferNoOhpkm);
            return false;
        }
        // Preserve an older OriginalBackup already carried by the mon: the fresh
        // blob only knows the current record, but a later return to the origin
        // format needs the oldest one. No-op when there is none or it won't parse.
        if (!pkm.ohBackup_.empty()) {
            PkmHandle* hb = OpenHomeNX::ohpkmWithOriginalBackup(blob, pkm.ohBackup_);
            if (hb) {
                std::vector<uint8_t> re = OpenHomeNX::getOhpkmBytes(hb);
                OpenHomeNX::freePkm(hb);
                if (!re.empty())
                    blob = std::move(re);
                else
                    DebugLog::line("xbank in: backup re-attach produced empty blob, keeping fresh");
            } else {
                DebugLog::line("xbank in: backup re-attach failed, keeping fresh blob");
            }
        }
        pkm.ohpkmBlob_ = std::move(blob);
        return true;
    }

    // Mon picked up from a cross-gen bank (carries a full OHPKM): build the
    // destination format straight from it via the Rust engine (primary path,
    // same as below) — the OHPKM keeps its own backup for a later return.
    if (!pkm.ohpkmBlob_.empty()) {
        const GameType d = destGameFor(panel);
        if (sameStoredFormat(GameType::S, d) && !pkm.data.empty()) {
            // Fast path ONLY for faithful previews: the bank shows a stub
            // preview (EC=1, species only) when no dex accepts the species,
            // and placing it would land unparseable bytes while dropping the
            // blob (proven: Violet party mon stuck with gen9 read error).
            // Parse-test the preview; on failure fall through to the real
            // transfer below, which refuses dex-cut explicitly with the blob
            // (and the mon) intact in hand.
            const int dg0 = ohTargetGenFor(d);
            bool faithful = false;
            if (dg0 != 0) {
                int sz0 = ohRecordBytesFor(dg0);
                if (sz0 > 0 && sz0 <= (int)pkm.data.size()) {
                    std::vector<uint8_t> pre(pkm.data.begin(), pkm.data.begin() + sz0);
                    PkmHandle* th = OpenHomeNX::loadPkmFromGen(pre, static_cast<uint32_t>(dg0));
                    if (th) {
                        OpenHomeNX::freePkm(th);
                        faithful = true;
                    } else {
                        DebugLog::line("xfer: stub preview for %s, forcing real transfer",
                                       gameDisplayNameOf(d));
                    }
                }
            }
            if (faithful) {
                pkm.ohpkmBlob_.clear();
                pkm.gameType_ = d;
                return true;
            }
        }
        if (!useOpenHome()) {
            whyNot = i18n::get(StrKey::TransferNeedOh);
            return false;
        }
        const int dg = ohTargetGenFor(d);
        if (dg == 0) {
            whyNot = i18n::fmt(StrKey::TransferCantBuild, gameDisplayNameOf(d));
            return false;
        }
        PkmHandle* h = OpenHomeNX::loadOhpkm(pkm.ohpkmBlob_);
        if (!h) { whyNot = i18n::get(StrKey::TransferBadOhpkm); return false; }
        {
            uint16_t mm[4] = { 0, 0, 0, 0 };
            if (OpenHomeNX::ohpkmMoves(h, mm))
                DebugLog::line("xfer: in moves=%u,%u,%u,%u -> g%d",
                               mm[0], mm[1], mm[2], mm[3], dg);
        }
        PkmHandle* out = PokemonFFI::transfer(h, static_cast<uint32_t>(dg));
        PokemonFFI::free(h);
        DebugLog::line("xfer: blob -> g%d %s", dg, out ? "ok" : "NULL");
        if (!out) {
            whyNot = i18n::fmt(StrKey::TransferNotInDex, pkm.displayName(), gameDisplayNameOf(d));
            return false;
        }
        std::vector<uint8_t> bytes = OpenHomeNX::getPkmBoxBytesForGen(out, static_cast<uint32_t>(dg));
        // Keep the oldest backup for a later lossless return (transfer()
        // propagates the pre-conversion original through the OHPKM).
        std::vector<uint8_t> backup = OpenHomeNX::getPkmOriginalBackup(out);
        // Gen1 records carry no names: snapshot OT/nick from the converted
        // OHPKM (which preserved them) before freeing the handle.
        std::string gen1Ot, gen1Nick;
        if (dg == 1) {
            gen1Ot = OpenHomeNX::ohpkmTrainerName(out);
            gen1Nick = OpenHomeNX::ohpkmNickname(out);
        }
        PokemonFFI::free(out);
        DebugLog::line("xfer: g%d bytes=%zu data=%zu", dg, bytes.size(), pkm.data.size());
        if (bytes.empty() || bytes.size() > pkm.data.size()) {
            whyNot = i18n::get(StrKey::TransferNoBytes);
            return false;
        }
        pkm.data.fill(0);
        std::memcpy(pkm.data.data(), bytes.data(), bytes.size());
        pkm.gameType_ = d;
        pkm.ohpkmBlob_.clear();
        if (dg == 1) fillGen1Names(pkm, gen1Ot, gen1Nick);
        // Hotfix Jolteon Lv0: cross-gen bank → Gen3 a volte produce EXP 0 (OHPKM
        // con exp 0, vedi Emerald box10 Jolteon 135). EXP 0 → Lv1, non 0, ma
        // in-game appare 0 perché la coda party non è ancora ricalcolata.
        // Se il record appena convertito ha EXP 0, forziamo a Lv50 (125000 MF)
        // così non arriva mai a 0 e il checksum resta valido.
        if ((isFRLG(d) || isImportedFile(d)) && pkm.readU32(0x24) == 0) {
            DebugLog::line("xfer: fix EXP 0 -> 125000 for spc=%u (was Lv0)", pkm.species());
            pkm.writeU32(0x24, 125000);
            pkm.refreshChecksum();
        }
        if (pkm.ohBackup_.empty() && !backup.empty())
            pkm.ohBackup_ = std::move(backup);
        return true;
    }

    const GameType dest = destGameFor(panel);
    // Same stored layout: the bytes are already correct, place as-is.
    if (sameStoredFormat(pkm.gameType_, dest)) return true;

    // Cross-gen conversion runs entirely through the OpenHome Rust engine.
    // With the pkHouse (PK) core active, keep the two pipelines fully separate:
    // no OH FFI call, so a cross-format drop is refused.
    if (!useOpenHome()) {
        whyNot = i18n::get(StrKey::TransferNeedOh);
        return false;
    }

    const int dstGen = ohTargetGenFor(dest);
    if (dstGen == 0) {
        whyNot = i18n::fmt(StrKey::TransferCantBuild, gameDisplayNameOf(dest));
        return false;
    }

    // Debug-log helper: FNV-1a of a blob + (when it fits) the full hex. Lets a
    // SwSh->SV->SwSh round trip be verified from debug.log — the "src" hash of
    // the first hop must equal the "restored" hash of the return hop.
    auto dbgBlob = [](const char* tag, const uint8_t* p, size_t n) {
        if (!DebugLog::enabled() || !p || !n) return;
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
        DebugLog::line("  %s: %zu B fnv=%08X", tag, n, h);
        if (n <= 400) {
            std::string hx; hx.reserve(n * 2);
            static const char D[] = "0123456789abcdef";
            for (size_t i = 0; i < n; ++i) { hx += D[p[i] >> 4]; hx += D[p[i] & 0xF]; }
            DebugLog::line("  %s hex: %s", tag, hx.c_str());
        }
    };
    DebugLog::line("xfer: %s -> %s (gen? -> gen%d)",
                   gameDisplayNameOf(pkm.gameType_), gameDisplayNameOf(dest), dstGen);

    // Primary path = upstream method: always rebuild the
    // destination record via the Rust engine below. The carried OriginalBackup
    // is only a fallback (restoreVerbatimFallback) if that fails — never
    // preferred, so moves/levels gained meanwhile survive by construction.

    // Gen1 records carry no names: snapshot OT/nick from the CURRENT format
    // now (pkm.data is overwritten by the conversion below). The level byte
    // is derived from EXP inside Rust (level_for_exp), no snapshot needed.
    std::string gen1Ot, gen1Nick;
    if (dstGen == 1) {
        gen1Ot = pkm.otName();
        gen1Nick = pkm.nickname();
    }

    // genOf() would report 8/9/7 for BDSP/LA/Z-A/LGPE too, but their PB8/PA8/
    // PA9/PB7 records are not what openhome_transfer_pkm consumes — mis-parsing
    // them would silently produce a wrong Pokemon. Refuse explicitly instead.
    const int srcGen = ohSourceGenFor(pkm.gameType_);
    if (srcGen == 0) {
        whyNot = i18n::fmt(StrKey::TransferCantReadSrcXfer, gameDisplayNameOf(pkm.gameType_));
        return false;
    }

    // Lossless return: when coming BACK to the format of
    // a carried OriginalBackup, test whether the user changed anything since
    // conversion by re-running that same conversion now (deterministic, see
    // transfer_is_deterministic_per_target): backup -> current format.
    // Identical bytes mean untouched -> restore the backup verbatim, so
    // origin-only data and original moves come back. Nothing the user did is
    // lost (they did nothing; battle-state reset on transfer is standard).
    // Anything else (edited, or any failure) falls through to the normal
    // reconstruct below — fail-open: worst case is current behavior.
    if (pkm.ohBackup_.size() >= 2) {
        const int bkTag = pkm.ohBackup_[0] | (pkm.ohBackup_[1] << 8);
        const size_t bkRec = pkm.ohBackup_.size() - 2;
        const int bkGen = ohGenForBackupTag(bkTag);
        const size_t curRec = static_cast<size_t>(ohRecordBytesFor(srcGen));
        if (bkGen != 0 && bkGen == dstGen && bkRec > 0 && bkRec <= pkm.data.size() &&
            curRec > 0 && curRec <= pkm.data.size()) {
            std::vector<uint8_t> bkBytes(pkm.ohBackup_.begin() + 2, pkm.ohBackup_.end());
            PkmHandle* th = OpenHomeNX::loadPkmFromGen(bkBytes, static_cast<uint32_t>(bkGen));
            if (th) {
                PkmHandle* tout = PokemonFFI::transfer(th, static_cast<uint32_t>(srcGen));
                OpenHomeNX::freePkm(th);
                if (tout) {
                    std::vector<uint8_t> trial = OpenHomeNX::getPkmBoxBytesForGen(tout, static_cast<uint32_t>(srcGen));
                    OpenHomeNX::freePkm(tout);
                    if (trial.size() >= curRec &&
                        std::memcmp(trial.data(), pkm.data.data(), curRec) == 0) {
                        pkm.data.fill(0);
                        std::memcpy(pkm.data.data(), pkm.ohBackup_.data() + 2, bkRec);
                        pkm.gameType_ = dest;
                        DebugLog::line("xfer: lossless return from backup (tag %d)", bkTag);
                        dbgBlob("returned", pkm.data.data(), bkRec);
                        return true;
                    }
                    DebugLog::line("xfer: return-trip edited, reconstructing");
                }
            }
        }
    }

    // Source stored record -> OHPKM. openhome_load_pkm is NOT usable here: it
    // only accepts real OHPKM files (magic + version 2), never a Pk3/8/9 record.
    // Trim to the exact record length: PkN::from_bytes rejects any other size
    // and pkm.data is the full MAX_PARTY_SIZE array.
    size_t srcSize = static_cast<size_t>(ohRecordBytesFor(srcGen));
    if (srcSize == 0 || srcSize > pkm.data.size())
        srcSize = pkm.data.size();
    std::vector<uint8_t> src(pkm.data.begin(), pkm.data.begin() + srcSize);
    dbgBlob("src", src.data(), src.size());
    PkmHandle* in = OpenHomeNX::loadPkmFromGen(src, static_cast<uint32_t>(srcGen));
    if (!in) {
        if (restoreVerbatimFallback(pkm, dest, dstGen)) return true;
        whyNot = i18n::fmt(StrKey::TransferBadRecord, std::to_string(srcGen));
        return false;
    }

    PkmHandle* out = PokemonFFI::transfer(in, static_cast<uint32_t>(dstGen));
    DebugLog::line("convert gen%d->gen%d: %s", srcGen, dstGen, out ? "ok" : "FAIL");
    PokemonFFI::free(in);
    if (!out) {
        if (restoreVerbatimFallback(pkm, dest, dstGen)) return true;
        whyNot = i18n::fmt(StrKey::TransferNotInDex, pkm.displayName(), gameDisplayNameOf(dest));
        return false;
    }

    std::vector<uint8_t> bytes =
        OpenHomeNX::getPkmBoxBytesForGen(out, static_cast<uint32_t>(dstGen));
    // Pre-conversion original, propagated through the OHPKM by transfer(): grab
    // it before freeing the handle so a later return trip is lossless.
    std::vector<uint8_t> backup = OpenHomeNX::getPkmOriginalBackup(out);
    PokemonFFI::free(out);
    if (bytes.empty() || bytes.size() > pkm.data.size()) {
        if (restoreVerbatimFallback(pkm, dest, dstGen)) return true;
        whyNot = i18n::get(StrKey::TransferNoBytes);
        return false;
    }

    dbgBlob("out", bytes.data(), bytes.size());
    dbgBlob("backup", backup.data(), backup.size());

    // Commit only now that every step succeeded: pkm is untouched on failure.
    // Oldest origin wins (upstream "imported original" semantics): seed the
    // backup only on first conversion, never overwrite one already carried.
    pkm.data.fill(0);
    std::memcpy(pkm.data.data(), bytes.data(), bytes.size());
    pkm.gameType_ = dest;
    if (dstGen == 1) fillGen1Names(pkm, gen1Ot, gen1Nick);
    if (pkm.ohBackup_.empty() && !backup.empty())
        pkm.ohBackup_ = std::move(backup);
    return true;
}

void UI::clearPokemonAt(int box, int slot, Panel panel) {
    if (panel == Panel::Game) {
        if (isDualBankMode()) {
            if (leftBankName_.empty()) return;
            bankLeft_.clearSlot(box, slot);
        } else
            save_.clearBoxSlot(box, slot);
    } else {
        bank_.clearSlot(box, slot);
    }
    invalidateSlotDisplay(panel, box);
}

void UI::actionSelect() {
    // Block interaction on empty left panel in applet mode
    if (isDualBankMode() && cursor_.panel == Panel::Game && leftBankName_.empty())
        return;

    int box = cursor_.box;
    int slot = cursor_.slot(gridCols());
    // Mai depositare fuori box: con griglie piu strette del pannello (GB/GBC
    // 20 slot) uno slot oltre il limite cadrebbe nel box dopo. Tieni in mano.
    if (slot < 0 || slot >= maxSlotsFor(cursor_.panel)) {
        DebugLog::line("actionSelect: REFUSED box=%d slot=%d oltre griglia, mano intatta", box, slot);
        return;
    }

    // Multi-select pick up
    if (!holding_ && !selectedSlots_.empty()) {
        heldMulti_.clear();
        heldMultiSlots_.clear();
        heldMultiSource_ = selectedPanel_;
        heldMultiBox_ = selectedBox_;

        // Check if any selected slot is an LGPE party member; backup indices
        heldFromLGPEParty_ = false;
        lgpePartyBackup_ = save_.lgpePartyIndices();
        if (selectedPanel_ == Panel::Game) {
            for (int s : selectedSlots_) {
                if (save_.isLGPEPartySlot(selectedBox_, s)) {
                    heldFromLGPEParty_ = true;
                    break;
                }
            }
        }

        // Collect in selection order
        for (int s : selectedSlots_) {
            Pokemon pkm = getPokemonAt(selectedBox_, s, selectedPanel_);
            if (!pkm.isEmpty()) {
                heldMulti_.push_back(pkm);
                heldMultiSlots_.push_back(s);
                clearPokemonAt(selectedBox_, s, selectedPanel_);
            }
        }
        selectedSlots_.clear();
        if (!heldMulti_.empty())
            holding_ = true;
        return;
    }

    // Multi-select place
    if (holding_ && !heldMulti_.empty()) {
        // Block LGPE party Pokemon from moving to bank
        if (heldFromLGPEParty_ && cursor_.panel == Panel::Bank) {
            showMessageAndWait(i18n::get(StrKey::PartyPokemon),
                i18n::get(StrKey::CantMovePartyBank));
            return;
        }
        // Helper: update LGPE party pointer when a Pokemon is placed at a new slot
        auto updatePartyPtr = [&](int origSlot, int newBox, int newSlot) {
            if (!heldFromLGPEParty_ || cursor_.panel != Panel::Game) return;
            // Find which party index the original slot belongs to (using backup)
            uint16_t origFlat = static_cast<uint16_t>(
                heldMultiBox_ * save_.slotsPerBox() + origSlot);
            for (int p = 0; p < 6; p++) {
                if (lgpePartyBackup_[p] == origFlat) {
                    uint16_t newFlat = static_cast<uint16_t>(
                        newBox * save_.slotsPerBox() + newSlot);
                    save_.setLGPEPartyPointer(p, newFlat);
                    break;
                }
            }
        };

        // M6 transfer-on-drop, multi-select: convert every held Pokemon to the
        // destination format up front. All-or-nothing — if any one cannot be
        // converted we place none, so a partial multi-drop is impossible.
        // Covers both the position-preserving and first-available branches.
        {
            // Move-drop warning (F2, generalizzato Gen 1-6): conta le mosse non
            // native PRIMA di convertire (preview load, no mutation). A procede,
            // B tiene tutto in mano, intatto. Un dialogo per drop. Il body e
            // parametrizzato sulla gen ("Gen {2}") e drop+refill vale per 1-6.
            // Dex-cut PRIMA del warning mosse (tutto il batch resta in mano).
            // Primo loop: se un mon non e nel dex target, messaggio subito e
            // niente dialoghi mosse. Secondo loop: warning drop come prima.
            for (int i = 0; i < (int)heldMulti_.size(); i++) {
                const int dg0 = ohTargetGenFor(destGameFor(cursor_.panel));
                if (!useOpenHome() || dg0 < 1 || dg0 > 6) break;
                const int sg0 = ohSourceGenFor(heldMulti_[i].gameType_);
                const int sz0 = ohRecordBytesFor(sg0);
                if (sg0 == 0 || sz0 <= 0 || sz0 > (int)heldMulti_[i].data.size()) continue;
                std::vector<uint8_t> src0(heldMulti_[i].data.begin(), heldMulti_[i].data.begin() + sz0);
                PkmHandle* ih = OpenHomeNX::loadPkmFromGen(src0, static_cast<uint32_t>(sg0));
                if (!ih) continue;
                uint32_t legal = OpenHomeNX::speciesLegalInGen(ih, static_cast<uint32_t>(dg0));
                OpenHomeNX::freePkm(ih);
                if (legal == 0) {
                    showMessageAndWait(i18n::get(StrKey::TransferTitle),
                        i18n::fmt(StrKey::TransferNotInDex, heldMulti_[i].displayName(),
                                  gameDisplayNameOf(destGameFor(cursor_.panel))));
                    return; // tutto resta in mano, intatto
                }
            }
            for (int i = 0; i < (int)heldMulti_.size(); i++) {
                const int dg0 = ohTargetGenFor(destGameFor(cursor_.panel));
                if (!useOpenHome() || dg0 < 1 || dg0 > 6) break;
                const int sg0 = ohSourceGenFor(heldMulti_[i].gameType_);
                const int sz0 = ohRecordBytesFor(sg0);
                if (sg0 == 0 || sz0 <= 0 || sz0 > (int)heldMulti_[i].data.size()) continue;
                std::vector<uint8_t> src0(heldMulti_[i].data.begin(), heldMulti_[i].data.begin() + sz0);
                PkmHandle* ih = OpenHomeNX::loadPkmFromGen(src0, static_cast<uint32_t>(sg0));
                if (!ih) continue;
                uint32_t dropped = OpenHomeNX::countMovesNotInGen(ih, static_cast<uint32_t>(dg0));
                OpenHomeNX::freePkm(ih);
                if (dropped != UINT32_MAX && dropped > 0) {
                    if (!showConfirmDialog(i18n::get(StrKey::Gen1DropsTitle),
                            i18n::fmt(StrKey::Gen1DropsBody, heldMulti_[i].displayName(), std::to_string(dropped), std::to_string(dg0))))
                        return; // B: cancel, everything stays in hand, untouched
                    break; // A once: convert the whole batch below
                }
            }
            std::vector<Pokemon> converted = heldMulti_;
            for (int i = 0; i < (int)converted.size(); i++) {
                std::string whyNot;
                if (!prepareForPlacement(converted[i], cursor_.panel, whyNot)) {
                    showMessageAndWait(i18n::get(StrKey::TransferTitle),
                        converted[i].displayName() + ": " + whyNot);
                    return;  // keep everything in hand, write nothing
                }
            }
            heldMulti_ = std::move(converted);
        }

        if (positionPreserve_) {
            // Position-preserving: place each Pokemon at its original slot index
            for (int i = 0; i < (int)heldMulti_.size(); i++) {
                int targetSlot = heldMultiSlots_[i];
                if (!getPokemonAt(box, targetSlot, cursor_.panel).isEmpty()) {
                    showMessageAndWait(i18n::get(StrKey::SlotsOccupied),
                        i18n::get(StrKey::SlotsOccupiedBody));
                    return;
                }
            }
            for (int i = 0; i < (int)heldMulti_.size(); i++) {
                setPokemonAt(box, heldMultiSlots_[i], cursor_.panel, heldMulti_[i]);
                updatePartyPtr(heldMultiSlots_[i], box, heldMultiSlots_[i]);
            }
        } else {
            // First-available: fill empty slots in order
            int slotsInBox = maxSlots();
            int emptyCount = 0;
            for (int s = 0; s < slotsInBox; s++) {
                if (getPokemonAt(box, s, cursor_.panel).isEmpty())
                    emptyCount++;
            }
            if (emptyCount < (int)heldMulti_.size()) {
                showMessageAndWait(i18n::get(StrKey::NotEnoughSpaceSlots),
                    i18n::fmt(StrKey::NeedEmptySlots, std::to_string(heldMulti_.size()), std::to_string(emptyCount)));
                return;
            }
            int placed = 0;
            for (int s = 0; s < slotsInBox && placed < (int)heldMulti_.size(); s++) {
                if (getPokemonAt(box, s, cursor_.panel).isEmpty()) {
                    setPokemonAt(box, s, cursor_.panel, heldMulti_[placed]);
                    updatePartyPtr(heldMultiSlots_[placed], box, s);
                    placed++;
                }
            }
        }
        heldMulti_.clear();
        heldMultiSlots_.clear();
        holding_ = false;
        positionPreserve_ = false;
        heldFromLGPEParty_ = false;
        lgpeHeldPartyIdx_ = -1;
        heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
        return;
    }

    // Single pick/place
    if (!holding_) {
        Pokemon pkm = getPokemonAt(box, slot, cursor_.panel);
        if (pkm.isEmpty())
            return;

        heldPkm_ = pkm;
        holding_ = true;
        lgpeHeldPartyIdx_ = (cursor_.panel == Panel::Game)
            ? save_.lgpePartyIndexOf(box, slot) : -1;
        heldFromLGPEParty_ = (lgpeHeldPartyIdx_ >= 0);
        // Debug: presa libera anche dell'ultimo membro (exit-hook copre).
        lgpePartyBackup_ = save_.lgpePartyIndices();
        swapHistory_.clear();
        swapHistory_.push_back({pkm, cursor_.panel, box, slot});

        clearPokemonAt(box, slot, cursor_.panel);
        // LGPE: clearPartySlot invalida pointer E azzera la cella insieme.
        // Niente restore: LGPE e esente dal guard anti-svuotamento, quindi
        // qui si libera sempre tutto (una sola copia in mano).
        if (lgpeHeldPartyIdx_ >= 0)
            save_.clearPartySlot(lgpeHeldPartyIdx_);
    } else {
        // Block LGPE party Pokemon from moving to bank
        if (heldFromLGPEParty_ && cursor_.panel == Panel::Bank) {
            showMessageAndWait(i18n::get(StrKey::PartyPokemon),
                i18n::get(StrKey::CantMovePartyBank));
            return;
        }

        Pokemon target = getPokemonAt(box, slot, cursor_.panel);

        // M6 transfer-on-drop: convert the held Pokemon to the destination
        // format before it is written. Covers both the place-on-empty and the
        // swap branch below, since both place heldPkm_ into this same panel.
        {
            // Dex-cut PRIMA del warning mosse (Koraidon 2026-09-09: prima
            // diceva "mosse non entrano", solo dopo "non c'e nel pokedex").
            // Stesso messaggio che darebbe prepareForPlacement, ma subito.
            const int dg0 = ohTargetGenFor(destGameFor(cursor_.panel));
            if (useOpenHome() && dg0 >= 1 && dg0 <= 6) {
                const int sg0 = ohSourceGenFor(heldPkm_.gameType_);
                const int sz0 = ohRecordBytesFor(sg0);
                if (sg0 != 0 && sz0 > 0 && sz0 <= (int)heldPkm_.data.size()) {
                    std::vector<uint8_t> src0(heldPkm_.data.begin(), heldPkm_.data.begin() + sz0);
                    PkmHandle* ih = OpenHomeNX::loadPkmFromGen(src0, static_cast<uint32_t>(sg0));
                    if (ih) {
                        uint32_t legal = OpenHomeNX::speciesLegalInGen(ih, static_cast<uint32_t>(dg0));
                        uint32_t dropped = OpenHomeNX::countMovesNotInGen(ih, static_cast<uint32_t>(dg0));
                        OpenHomeNX::freePkm(ih);
                        if (legal == 0) {
                            showMessageAndWait(i18n::get(StrKey::TransferTitle),
                                i18n::fmt(StrKey::TransferNotInDex, heldPkm_.displayName(),
                                          gameDisplayNameOf(destGameFor(cursor_.panel))));
                            return; // mon untouched in hand
                        }
                        if (dropped != UINT32_MAX && dropped > 0 &&
                            !showConfirmDialog(i18n::get(StrKey::Gen1DropsTitle),
                                i18n::fmt(StrKey::Gen1DropsBody, heldPkm_.displayName(), std::to_string(dropped), std::to_string(dg0)))) {
                            return; // B: cancel, mon untouched in hand
                        }
                    }
                }
            }
            std::string whyNot;
            if (!prepareForPlacement(heldPkm_, cursor_.panel, whyNot)) {
                showMessageAndWait(i18n::get(StrKey::TransferTitle), whyNot);
                return;  // keep the Pokemon in hand, write nothing
            }
        }

        if (target.isEmpty()) {
            // Place on empty — commit, clear history
            setPokemonAt(box, slot, cursor_.panel, heldPkm_);
            // Update party pointer to follow the Pokemon — but ONLY for
            // box-origin holds. A strip-origin hold (heldPartyOrig_ >= 0, set
            // solely by strip pick) leaving to a box LEAVES the party: the
            // pointer was already emptied at pick, re-pointing would silently
            // re-add it (proven: LGPE party->box kept the mini).
            if (lgpeHeldPartyIdx_ >= 0 && heldPartyOrig_ < 0 && cursor_.panel == Panel::Game) {
                uint16_t newFlat = static_cast<uint16_t>(
                    box * save_.slotsPerBox() + slot);
                save_.setLGPEPartyPointer(lgpeHeldPartyIdx_, newFlat);
                save_.refreshPartyEntryFromPointer(lgpeHeldPartyIdx_);
            }
            // Strip-origin sync (anti-dupe LGPE): il mon ha lasciato la strip
            // per una NUOVA cella — rilascia subito l'origine (pointer EMPTY +
            // cella zeroata), altrimenti disco ha 2 copie (pointer intatto +
            // nuova cella) e al reload resuscita all'infinito. Se torna sulla
            // propria cella gialla, ricongiungi la strip (refresh).
            if (isLGPE(selectedGame_) && heldPartyOrig_ >= 0 && cursor_.panel == Panel::Game) {
                int of = save_.lgpeFlatOfParty(heldPartyOrig_);
                int nf = box * save_.slotsPerBox() + slot;
                if (of >= 0 && of != nf) {
                    save_.setLGPEPartyPointer(heldPartyOrig_, SaveFile::LGPE_SLOT_EMPTY);
                    save_.lgpeZeroFlatSlot(of);
                    DebugLog::line("lgpe: origine strip %d rilasciata (cella %d)", heldPartyOrig_, of);
                } else if (of == nf && of >= 0) {
                    save_.refreshPartyEntryFromPointer(heldPartyOrig_);
                }
            }
            holding_ = false;
            heldPkm_ = Pokemon{};
            swapHistory_.clear();
            heldFromLGPEParty_ = false;
            lgpeHeldPartyIdx_ = -1;
            heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
        } else {
            // Swap: check if target is also a party member BEFORE modifying
            int targetPartyIdx = (cursor_.panel == Panel::Game)
                ? save_.lgpePartyIndexOf(box, slot) : -1;

            swapHistory_.push_back({target, cursor_.panel, box, slot});
            setPokemonAt(box, slot, cursor_.panel, heldPkm_);

            // Update party pointer for the placed Pokemon (box-origin holds
            // only — strip-origin holds leave the party, see above).
            if (lgpeHeldPartyIdx_ >= 0 && heldPartyOrig_ < 0 && cursor_.panel == Panel::Game) {
                uint16_t newFlat = static_cast<uint16_t>(
                    box * save_.slotsPerBox() + slot);
                save_.setLGPEPartyPointer(lgpeHeldPartyIdx_, newFlat);
                save_.refreshPartyEntryFromPointer(lgpeHeldPartyIdx_);
            }
            // Stesso sync anti-dupe del place (vedi sopra): anche allo swap
            // l'origine strip va rilasciata, A vive ormai nella cella target
            // (il cancel la rilegge da history[0]).
            if (isLGPE(selectedGame_) && heldPartyOrig_ >= 0 && cursor_.panel == Panel::Game) {
                int of = save_.lgpeFlatOfParty(heldPartyOrig_);
                int nf = box * save_.slotsPerBox() + slot;
                if (of >= 0 && of != nf) {
                    save_.setLGPEPartyPointer(heldPartyOrig_, SaveFile::LGPE_SLOT_EMPTY);
                    save_.lgpeZeroFlatSlot(of);
                    DebugLog::line("lgpe: origine strip %d rilasciata (cella %d)", heldPartyOrig_, of);
                } else if (of == nf && of >= 0) {
                    save_.refreshPartyEntryFromPointer(heldPartyOrig_);
                }
            }
            // Target was a party member but held Pokemon was not (cross-panel swap):
            // the party Pokemon is now held, so invalidate its pointer until placed.
            // Strip-origin hold swapping with a party cell: that pointer now
            // legitimately covers the placed mon, so refresh (don't invalidate).
            if (targetPartyIdx >= 0 && lgpeHeldPartyIdx_ < 0) {
                save_.setLGPEPartyPointer(targetPartyIdx, SaveFile::LGPE_SLOT_EMPTY);
                save_.refreshPartyEntryFromPointer(targetPartyIdx);
            } else if (targetPartyIdx >= 0 && heldPartyOrig_ >= 0) {
                save_.refreshPartyEntryFromPointer(targetPartyIdx);
            }

            heldPkm_ = target;
            lgpeHeldPartyIdx_ = targetPartyIdx;
            heldFromLGPEParty_ = (targetPartyIdx >= 0);
            // Generic party: target was from box, not party, so the hand is
            // now box-side. MA la strip-origin (heldPartyOrig_ >= 0) va
            // preservata per il cancel: A sta in history[0] e l'origine serve
            // a rimettercela (Smeraldo: swap strip->box + B perdeva A per
            // sempre, il replay sovrascriveva A con B). Per box-origin orig
            // e gia -1: niente cambia.
            heldFromParty_=false; heldPartyIdx_=-1;
        }
    }
}

bool UI::ensurePartyOnExit() {
    if (!save_.isLoaded() || isDualBankMode()) return true;
    if (save_.hasParty()) return true;
    // Save vergine (strip vuota gia al load): niente dialogo, niente
    // segnaposto forzato — vuoto e lo stato iniziale legittimo (Violetto).
    if (save_.wasPartyEmptyAtLoad()) {
        DebugLog::line("exit: strip vuota da load, nessun segnaposto (save vergine)");
        return true;
    }
    DebugLog::line("exit: party vuota, chiedo Caterpie/manuale");
    if (isFRLG(selectedGame_) || isImportedFile(selectedGame_)) {
        if (showConfirmDialog(i18n::get(StrKey::EmptyPartyTitle),
                              i18n::get(StrKey::EmptyPartyCaterpie))) {
            if (!save_.placeCaterpiePlaceholder()) {
                showMessageAndWait(i18n::get(StrKey::EmptyPartyTitle),
                                   i18n::get(StrKey::CantEmptyParty));
                return false;
            }
            if (holding_)
                DebugLog::line("exit: Caterpie piazzato, mon in mano segue il flusso standard");
            return true;
        }
        return false; // B: torno a mettere qualcosa a mano
    }
    // Altre famiglie (debug): stesso patto con Magikarp L5 Splash.
    if (showConfirmDialog(i18n::get(StrKey::EmptyPartyTitle),
                          i18n::get(StrKey::EmptyPartyMagikarp))) {
        if (!save_.placePlaceholder()) {
            showMessageAndWait(i18n::get(StrKey::EmptyPartyTitle),
                               i18n::get(StrKey::CantEmptyParty));
            return false;
        }
        return true;
    }
    return false;
}

// Quit vero con mano occupata: il mon in mano vive solo in memoria e al quit
// andrebbe perso (Smeraldo: Pikachu sparito al quit, disco senza traccia).
// La mano DEVE sopravvivere ai cambi gioco (carry cross-gen: selectGame non
// la azzera), quindi il warn scatta solo qui, mai in uscita dal gioco.
bool UI::confirmQuitWithHold() {
    if (!holding_ && heldMulti_.empty()) return true;
    DebugLog::line("quit: mano occupata");
    if (!DebugLog::enabled()) {
        // Non-debug: NO-OP esplicita — niente quit con perdite, solo B
        // (indietro). Il popup messaggio ha solo chiusura, nessuna A.
        showMessageAndWait(i18n::get(StrKey::QuitHoldTitle),
                           i18n::get(StrKey::QuitHoldBody));
        return false;
    }
    return showConfirmDialog(i18n::get(StrKey::QuitHoldTitle),
                             i18n::get(StrKey::QuitHoldBody));
}

void UI::returnToGameSelector() {
    if (!saveBankFiles())
        return;
    // Catturato PRIMA di persistGameSaveIfDirty(): save_.save() dentro quella
    // funzione azzera isDirty() anche in caso di scrittura riuscita, quindi
    // e' l'unico punto in cui si puo' ancora sapere se c'era davvero una
    // modifica da mandare al dispositivo remoto (vedi blocco Box Remoto sotto).
    bool hadChanges = save_.isLoaded() && save_.isDirty();
    // Niente guard qui: vive dentro persistGameSaveIfDirty (choke point
    // unico, evita doppi dialoghi). False (B) = resta nel gioco, niente
    // save, niente unmount: memoria intatta e si continua da dove si era.
    if (!persistGameSaveIfDirty())
        return;
    // Backup all'uscita se modificato (prima dell'unmount: serve il mount).
    backupOnExitIfNeeded();
    // Box Remoto: questo save veniva da un dispositivo remoto (vedi
    // UI::openRemoteBox/UI::selectGame) -- se e' stato davvero modificato,
    // chiede sempre conferma prima di rispedirlo (mai un invio automatico
    // silenzioso, per scelta esplicita: remoteSyncUpload() non e' ancora
    // stato verificato su hardware reale).
    if (activeSaveIsRemote_) {
        if (hadChanges) {
            if (showConfirmDialog(i18n::get(StrKey::RemoteBoxSendTitle),
                                   i18n::get(StrKey::RemoteBoxSendBody))) {
                std::string upErr;
                if (remoteSyncUpload(activeRemoteHost_, activeRemoteToken_, savePath_,
                                      activeRemoteSavePath_, upErr)) {
                    DebugLog::line("box remoto: invio OK (%s)", savePath_.c_str());
                } else {
                    showMessageAndWait(i18n::get(StrKey::RemoteBoxTitle),
                                        i18n::fmt(StrKey::RemoteBoxSendFailed, upErr));
                    DebugLog::line("box remoto: invio FALLITO (%s): %s", savePath_.c_str(), upErr.c_str());
                }
            } else {
                DebugLog::line("box remoto: invio rifiutato dall'utente, modifiche solo locali (%s)", savePath_.c_str());
            }
        }
        activeSaveIsRemote_ = false;
    }
    // Unmount regardless — leaving the game, so release the save mount even
    // when nothing was written.
    if (!isDualBankMode() && save_.isLoaded())
        account_.unmountSave();
    leftBankName_.clear();
    leftBankPath_.clear();
    activeBankName_.clear();
    activeBankPath_.clear();
    allBanksMode_ = false;
    gsSetFocus(GSFocus::Grid);
    bankRightCrossGen_ = false;
    showMenu_ = false;
    screen_ = AppScreen::GameSelector;
}

void UI::actionCancel() {
    // Dismiss search highlight
    if (searchHighlightActive_ && !holding_ && selectedSlots_.empty()) {
        clearSearchHighlight();
        return;
    }

    // Multi-hold cancel: return all to original positions
    if (holding_ && !heldMulti_.empty()) {
        for (int i = 0; i < (int)heldMulti_.size(); i++)
            setPokemonAt(heldMultiBox_, heldMultiSlots_[i], heldMultiSource_, heldMulti_[i]);
        heldMulti_.clear();
        heldMultiSlots_.clear();
        holding_ = false;
        positionPreserve_ = false;
        heldFromLGPEParty_ = false;
        lgpeHeldPartyIdx_ = -1;
        heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
        save_.setLGPEPartyIndices(lgpePartyBackup_);
        return;
    }

    // Clear selection (when not holding)
    if (!holding_ && !selectedSlots_.empty()) {
        selectedSlots_.clear();
        positionPreserve_ = false;
        return;
    }

    // Nothing to cancel (not holding, no selection, no search): B in the box
    // view saves everything and leaves. When we got here through "View All
    // Banks", step back to that list instead of jumping past it to the game
    // selector.
    if (!holding_) {
        if (allBanksMode_) {
            if (!saveBankFiles()) return;
            activeBankName_.clear();
            activeBankPath_.clear();
            leftBankName_.clear();
            leftBankPath_.clear();
            enterAllBanksMode();  // re-inits the list + cursor, screen = BankSelector
            // Resta vista split (save a sinistra, lista a destra): con
            // allBanksMode_=true, isDualBankMode() forzerebbe lista fullscreen.
            allBanksMode_ = false;
            bankSelTarget_ = Panel::Bank;
            DebugLog::line("bank: B in box view (all-mode) -> split list, count=%d dual=%d save=%d",
                           (int)bankManager_.list().size(), (int)isDualBankMode(), (int)save_.isLoaded());
            if (screen_ == AppScreen::MainView)  // list came back empty
                returnToGameSelector();
            return;
        }
        // B from an open bank always steps back to the ALL-banks list (grouped
        // by game): the single-game rescope hid other-game banks (BD folder empty
        // -> "Nessuna banca"; Sw showed only its own folder). Persist first.
        if (!saveBankFiles()) return;
        if (!persistGameSaveIfDirty()) return;
        activeBankName_.clear();
        activeBankPath_.clear();
        leftBankName_.clear();
        leftBankPath_.clear();
        enterAllBanksMode();  // re-inits the list + cursor, screen = BankSelector
        // Resta vista split (save a sinistra, lista a destra): con
        // allBanksMode_=true, isDualBankMode() forzerebbe lista fullscreen.
        allBanksMode_ = false;
        bankSelTarget_ = Panel::Bank;
        DebugLog::line("bank: B in box view -> split list, count=%d dual=%d save=%d",
                       (int)bankManager_.list().size(), (int)isDualBankMode(), (int)save_.isLoaded());
        if (screen_ == AppScreen::MainView)  // list came back empty
            returnToGameSelector();
        return;
    }

    // Party hold cancel: undo without loss (same branches as the A-on-strip cancel).
    // VERBATIM come sopra: src.pkm, mai inD convertito (celle blob).
    if (heldFromParty_) {
        if (!swapHistory_.empty()) {
            auto src = swapHistory_.back();
            setPokemonAt(src.box, src.slot, src.panel, src.pkm);
            save_.setPartySlot(heldPartyIdx_, heldPkm_);
        } else if (heldPartyOrig_ >= 0 && heldPartyOrig_ != heldPartyIdx_) {
            Pokemon curD = save_.getPartySlot(heldPartyIdx_);
            save_.setPartySlot(heldPartyIdx_, heldPkm_);
            save_.setPartySlot(heldPartyOrig_, curD);
        } else {
            save_.setPartySlot(heldPartyIdx_, heldPkm_);
        }
        holding_=false; heldPkm_=Pokemon{}; heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
        heldFromLGPEParty_=false; lgpeHeldPartyIdx_=-1;
        swapHistory_.clear();
        return;
    }
    // Single hold cancel: replay swap history in reverse to restore all slots.
    // Strip-origin (presa strip + swap in box/banca, mai dual-bank: li non
    // c'e strip e setPartySlot scriverebbe nel save sbagliato): A sta nella
    // PRIMA cella toccata (history[0]; lo strip pick non pusha). La si legge
    // e converte ORA (prima che il replay la sovrascriva con B), ma la si
    // rimette in strip S DOPO il reset (che ripristina anche i pointer LGPE
    // dal backup — farlo prima verrebbe annullato). Smeraldo: A persa.
    // Se non convertibile (dex-cut), la cella si salta nel replay: A resta
    // li, B in mano, niente perso.
    int stripOrig = (!isDualBankMode()) ? heldPartyOrig_ : -1;
    Pokemon stripMon;
    bool haveStripMon = false;
    if (stripOrig >= 0 && !swapHistory_.empty()) {
        const auto& first = swapHistory_.front();
        Pokemon inCell = getPokemonAt(first.box, first.slot, first.panel);
        if (!inCell.isEmpty()) {
            std::string whyNot;
            if (prepareForPlacement(inCell, Panel::Game, whyNot)) {
                stripMon = inCell;
                haveStripMon = true;
            } else {
                DebugLog::line("cancel: strip-origin non convertibile (%s), A resta in cella",
                               whyNot.c_str());
                swapHistory_.erase(swapHistory_.begin());
                stripOrig = -1;
            }
        } else {
            stripOrig = -1;
        }
    }
    for (int i = (int)swapHistory_.size() - 1; i >= 0; i--) {
        auto& rec = swapHistory_[i];
        setPokemonAt(rec.box, rec.slot, rec.panel, rec.pkm);
    }
    swapHistory_.clear();
    holding_ = false;
    heldPkm_ = Pokemon{};
    heldFromLGPEParty_ = false;
    lgpeHeldPartyIdx_ = -1;
    heldFromParty_=false; heldPartyIdx_=-1; heldPartyOrig_=-1;
    save_.setLGPEPartyIndices(lgpePartyBackup_);
    if (haveStripMon) {
        DebugLog::line("cancel: %s torna in strip %d",
                       stripMon.displayName().c_str(), stripOrig);
        save_.setPartySlot(stripOrig, stripMon);
        int spb = save_.slotsPerBox();
        if (spb > 0) {
            int f = save_.lgpeFlatOfParty(stripOrig);
            if (f >= 0) invalidateSlotDisplay(Panel::Game, f / spb);
        }
    }
}

void UI::toggleSelect() {
    if (holding_)
        return; // can't multi-select while holding
    if (isDualBankMode() && cursor_.panel == Panel::Game && leftBankName_.empty())
        return;

    int slot = cursor_.slot(gridCols());
    Pokemon pkm = getPokemonAt(cursor_.box, slot, cursor_.panel);
    if (pkm.isEmpty())
        return;

    // If selecting in a different panel/box, start fresh
    if (!selectedSlots_.empty() &&
        (cursor_.panel != selectedPanel_ || cursor_.box != selectedBox_)) {
        selectedSlots_.clear();
    }

    selectedPanel_ = cursor_.panel;
    selectedBox_ = cursor_.box;
    positionPreserve_ = false; // individual toggle = first-available mode

    auto it = std::find(selectedSlots_.begin(), selectedSlots_.end(), slot);
    if (it != selectedSlots_.end())
        selectedSlots_.erase(it);
    else
        selectedSlots_.push_back(slot);
}

void UI::clearSelection() {
    selectedSlots_.clear();
    if (!holding_)
        positionPreserve_ = false;
    yHeld_ = false;
    yDragActive_ = false;
}

void UI::beginYPress() {
    if (holding_)
        return;
    if (isDualBankMode() && cursor_.panel == Panel::Game && leftBankName_.empty())
        return;

    yHeld_ = true;
    yDragActive_ = false;
    dragAnchorCol_ = cursor_.col;
    dragAnchorRow_ = cursor_.row;
    dragPanel_ = cursor_.panel;
    dragBox_ = cursor_.box;
}

void UI::endYPress() {
    if (!yHeld_)
        return;

    if (!yDragActive_) {
        // No movement while held — check for double-tap
        uint32_t now = SDL_GetTicks();
        if (now - lastYTapTime_ <= DOUBLE_TAP_MS) {
            // Double-tap Y = select all (position-preserving)
            yHeld_ = false;
            lastYTapTime_ = 0;
            selectAll();
        } else {
            // Single tap — toggle individual slot
            yHeld_ = false;
            lastYTapTime_ = now;
            toggleSelect();
        }
    } else {
        yHeld_ = false;
        yDragActive_ = false;
    }
}

void UI::updateDragSelection() {
    int cols = gridCols();
    int minCol = std::min(dragAnchorCol_, cursor_.col);
    int maxCol = std::max(dragAnchorCol_, cursor_.col);
    int minRow = std::min(dragAnchorRow_, cursor_.row);
    int maxRow = std::max(dragAnchorRow_, cursor_.row);

    selectedSlots_.clear();
    selectedPanel_ = dragPanel_;
    selectedBox_ = dragBox_;
    positionPreserve_ = false; // drag = first-available mode

    // Add slots left-to-right, top-to-bottom (only non-empty)
    for (int r = minRow; r <= maxRow; r++) {
        for (int c = minCol; c <= maxCol; c++) {
            int slot = r * cols + c;
            Pokemon pkm = getPokemonAt(dragBox_, slot, dragPanel_);
            if (!pkm.isEmpty())
                selectedSlots_.push_back(slot);
        }
    }
}

void UI::selectAll() {
    if (holding_)
        return;
    if (isDualBankMode() && cursor_.panel == Panel::Game && leftBankName_.empty())
        return;

    int slots = maxSlots();

    selectedSlots_.clear();
    selectedPanel_ = cursor_.panel;
    selectedBox_ = cursor_.box;
    positionPreserve_ = true;

    for (int s = 0; s < slots; s++) {
        Pokemon pkm = getPokemonAt(cursor_.box, s, cursor_.panel);
        if (!pkm.isEmpty())
            selectedSlots_.push_back(s);
    }
}

// --- Species Picker ---

void UI::buildAvailableSpeciesList() {
    availableSpecies_.clear();

    // Use IsPresentInGame flags from personal tables (extracted from PKHeX binary data)
    auto isAvailable = [&](uint16_t species) -> bool {
        if (selectedGame_ == GameType::ZA) {
            return species < PERSONAL_ZA_COUNT && PersonalZA::IS_PRESENT[species];
        } else if (isSV(selectedGame_)) {
            return species < PERSONAL_SV_COUNT && PersonalSV::IS_PRESENT[species];
        } else if (isSwSh(selectedGame_)) {
            return species < PersonalSWSH::NUM_ENTRIES && PersonalSWSH::IS_PRESENT[species];
        } else if (selectedGame_ == GameType::LA) {
            return species < PERSONAL_LA_COUNT && PersonalLA::IS_PRESENT[species];
        } else if (isBDSP(selectedGame_)) {
            // BDSP: all species within table range are present
            return species >= 1 && species < PERSONAL_BDSP_COUNT;
        } else if (isLGPE(selectedGame_)) {
            // LGPE: no IsPresentInGame flag; check base stats
            if (species >= PERSONAL_GG_COUNT) return false;
            auto s = PersonalGG::BASE_STATS[species];
            return (s.hp | s.atk | s.def | s.spe | s.spa | s.spd) != 0;
        } else if (isFRLG(selectedGame_) || isImportedFile(selectedGame_)) {
            return species >= 1 && species <= 386;
        } else if (isGen1File(selectedGame_)) {
            return species >= 1 && species <= 151;
        } else if (isGen2File(selectedGame_)) {
            return species >= 1 && species <= 251;
        }
        return false;
    };

    int maxSpecies = 1024;
    for (uint16_t i = 1; i <= maxSpecies; i++) {
        if (isAvailable(i))
            availableSpecies_.push_back(i);
    }
}

void UI::buildSpeciesListForLetter(int letterIndex) {
    speciesPickerList_.clear();
    if (letterIndex < 1 || letterIndex > 26) return;

    char letter = 'A' + (letterIndex - 1);
    for (uint16_t id : availableSpecies_) {
        const std::string& name = SpeciesName::get(id);
        if (!name.empty() && std::toupper(static_cast<unsigned char>(name[0])) == letter)
            speciesPickerList_.push_back(id);
    }
    // Sort alphabetically by name
    std::sort(speciesPickerList_.begin(), speciesPickerList_.end(),
        [](uint16_t a, uint16_t b) {
            return SpeciesName::get(a) < SpeciesName::get(b);
        });
}

bool UI::letterHasSpecies(int letterIndex) const {
    if (letterIndex == 0) return true; // "-" is always valid (clear filter)
    if (letterIndex < 1 || letterIndex > 26) return false;
    char letter = 'A' + (letterIndex - 1);
    for (uint16_t id : availableSpecies_) {
        const std::string& name = SpeciesName::get(id);
        if (!name.empty() && std::toupper(static_cast<unsigned char>(name[0])) == letter)
            return true;
    }
    return false;
}

void UI::handleSpeciesLetterPickerInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        return;
    }

    constexpr int TOTAL_ITEMS = 27; // "-" + A-Z
    constexpr int COLS = 2;
    int col = speciesLetterCursor_ % COLS;
    int row = speciesLetterCursor_ / COLS;

    auto move = [&](int dx, int dy) {
        int totalRows = (TOTAL_ITEMS + COLS - 1) / COLS;
        // Try up to TOTAL_ITEMS times to find a non-empty letter
        for (int attempt = 0; attempt < TOTAL_ITEMS; attempt++) {
            col += dx;
            row += dy;
            if (row < 0) row = totalRows - 1;
            if (row >= totalRows) row = 0;
            if (col < 0) col = COLS - 1;
            if (col >= COLS) col = 0;
            int idx = row * COLS + col;
            if (idx >= TOTAL_ITEMS) {
                col = 0;
                idx = row * COLS + col;
            }
            if (letterHasSpecies(idx)) {
                speciesLetterCursor_ = idx;
                return;
            }
            // Continue in the same direction
            if (dx == 0 && dy == 0) break;
        }
    };

    auto confirm = [&]() {
        if (!letterHasSpecies(speciesLetterCursor_)) return;
        if (speciesLetterCursor_ == 0) {
            // "-" = clear species filter
            searchFilter_.speciesId = 0;
            searchFilter_.speciesName.clear();
            showSpeciesLetterPicker_ = false;
        } else {
            buildSpeciesListForLetter(speciesLetterCursor_);
            speciesListCursor_ = 0;
            speciesListScroll_ = 0;
            showSpeciesLetterPicker_ = false;
            showSpeciesListPicker_ = true;
        }
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    move(0, -1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:   move(0, +1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:   move(-1, 0); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:  move(+1, 0); break;
            case SDL_CONTROLLER_BUTTON_B: confirm(); break; // Switch A
            case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                showSpeciesLetterPicker_ = false;
                break;
        }
    }
}

void UI::handleSpeciesListPickerInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        return;
    }

    if (speciesPickerList_.empty()) return;

    constexpr int COLS = 3;
    int col = speciesListCursor_ % COLS;
    int row = speciesListCursor_ / COLS;
    int total = static_cast<int>(speciesPickerList_.size());

    auto move = [&](int dx, int dy) {
        col += dx;
        row += dy;
        int totalRows = (total + COLS - 1) / COLS;
        if (row < 0) row = totalRows - 1;
        if (row >= totalRows) row = 0;
        if (col < 0) col = COLS - 1;
        if (col >= COLS) col = 0;
        int idx = row * COLS + col;
        if (idx >= total) {
            // Wrap to last item in row or first col
            if (dx > 0) col = 0;
            else if (dx < 0) col = (total - 1) % COLS;
            else if (dy > 0) { row = 0; }
            else { row = totalRows - 1; col = std::min(col, (total - 1) % COLS); }
            idx = row * COLS + col;
            if (idx >= total) idx = total - 1;
        }
        speciesListCursor_ = idx;
    };

    auto confirm = [&]() {
        uint16_t id = speciesPickerList_[speciesListCursor_];
        searchFilter_.speciesId = id;
        searchFilter_.speciesName = SpeciesName::get(id);
        showSpeciesListPicker_ = false;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    move(0, -1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:   move(0, +1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:   move(-1, 0); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:  move(+1, 0); break;
            case SDL_CONTROLLER_BUTTON_B: confirm(); break; // Switch A
            case SDL_CONTROLLER_BUTTON_A: // Switch B = back to letter picker
                showSpeciesListPicker_ = false;
                showSpeciesLetterPicker_ = true;
                break;
        }
    }
}

// --- Search/Filter ---

void UI::handleSearchFilterInput(const SDL_Event& event) {

    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        return;
    }

    bool hasAlpha = gameInfo(selectedGame_).hasAlphaForms;

    auto moveFilterCursor = [&](int dir) {
        searchFilterCursor_ = (searchFilterCursor_ + dir + 12) % 12;
        if (!hasAlpha && searchFilterCursor_ == 4)
            searchFilterCursor_ = (searchFilterCursor_ + dir + 12) % 12;
    };

    auto confirmAction = [&]() {
        switch (searchFilterCursor_) {
            case 0:
                if (availableSpecies_.empty())
                    buildAvailableSpeciesList();
                speciesLetterCursor_ = 0;
                speciesLetterScroll_ = 0;
                showSpeciesLetterPicker_ = true;
                break;
            case 1: beginTextInput(TextInputPurpose::SearchOT); break;
            case 2: searchFilter_.filterShiny = !searchFilter_.filterShiny; break;
            case 3: searchFilter_.filterEgg = !searchFilter_.filterEgg; break;
            case 4: searchFilter_.filterAlpha = !searchFilter_.filterAlpha; break;
            case 5:
                searchFilter_.gender = static_cast<GenderFilter>(
                    (static_cast<int>(searchFilter_.gender) + 1) % 4);
                break;
            case 6:
                if (searchLevelFocus_ == 0)
                    beginTextInput(TextInputPurpose::SearchLevelMin);
                else
                    beginTextInput(TextInputPurpose::SearchLevelMax);
                break;
            case 7:
                searchFilter_.perfectIVs = static_cast<PerfectIVFilter>(
                    (static_cast<int>(searchFilter_.perfectIVs) + 1) % 3);
                break;
            case 8:
                searchFilter_.ribbonFilter = static_cast<RibbonFilter>(
                    (static_cast<int>(searchFilter_.ribbonFilter) + 1) % 4);
                break;
            case 9:
                searchFilter_.mode = (searchFilter_.mode == SearchMode::List)
                    ? SearchMode::Highlight : SearchMode::List;
                break;
            case 10: searchFilter_ = SearchFilter{}; break;
            case 11: executeSearch(); break;
        }
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                moveFilterCursor(-1);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                moveFilterCursor(1);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (searchFilterCursor_ == 6) searchLevelFocus_ = 0;
                else if (searchFilterCursor_ == 9) searchFilter_.mode = SearchMode::List;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (searchFilterCursor_ == 6) searchLevelFocus_ = 1;
                else if (searchFilterCursor_ == 9) searchFilter_.mode = SearchMode::Highlight;
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm
                confirmAction();
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                showSearchFilter_ = false;
                break;
        }
    }
}

void UI::handleSearchResultsInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        return;
    }

    auto navigate = [&](int dir) {
        if (searchResults_.empty()) return;
        searchResultCursor_ += dir;
        if (searchResultCursor_ < 0) searchResultCursor_ = 0;
        if (searchResultCursor_ >= (int)searchResults_.size())
            searchResultCursor_ = (int)searchResults_.size() - 1;
        int visibleRows = 12;
        if (searchResultCursor_ < searchResultScroll_)
            searchResultScroll_ = searchResultCursor_;
        if (searchResultCursor_ >= searchResultScroll_ + visibleRows)
            searchResultScroll_ = searchResultCursor_ - visibleRows + 1;
    };

    auto jumpToResult = [&]() {
        if (searchResults_.empty()) return;
        const auto& r = searchResults_[searchResultCursor_];
        cursor_.panel = r.panel;
        cursor_.box = r.box;
        cursor_.col = r.slot % gridColsFor(r.panel);
        cursor_.row = r.slot / gridColsFor(r.panel);
        if (r.panel == Panel::Game)
            gameBox_ = r.box;
        else
            bankBox_ = r.box;
        showSearchResults_ = false;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:   navigate(-1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  navigate(1);  break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                navigate(-10);
                lHeld_ = true;
                bumperRepeatTime_ = SDL_GetTicks();
                bumperMoved_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                navigate(10);
                rHeld_ = true;
                bumperRepeatTime_ = SDL_GetTicks();
                bumperMoved_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = jump
                jumpToResult();
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = close
                showSearchResults_ = false;
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Switch X = back to filter
                showSearchResults_ = false;
                showSearchFilter_ = true;
                break;
        }
    }
    if (event.type == SDL_CONTROLLERBUTTONUP) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  lHeld_ = false; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: rHeld_ = false; break;
        }
    }
}

bool UI::matchesSearchFilter(const Pokemon& pkm,
                             const std::string& filterSpecies,
                             const std::string& filterOT) const {
    if (pkm.isEmpty()) return false;

    auto toLower = [](const std::string& s) {
        std::string out = s;
        for (auto& c : out) c = std::tolower(static_cast<unsigned char>(c));
        return out;
    };

    // Species filter: exact match by ID if set, otherwise substring match
    if (searchFilter_.speciesId > 0) {
        if (pkm.species() != searchFilter_.speciesId)
            return false;
    } else if (!filterSpecies.empty()) {
        std::string name = toLower(SpeciesName::get(pkm.species()));
        if (name.find(filterSpecies) == std::string::npos)
            return false;
    }

    if (!filterOT.empty()) {
        std::string ot = toLower(pkm.otName());
        if (ot.find(filterOT) == std::string::npos)
            return false;
    }

    if (searchFilter_.filterShiny && !pkm.isShiny()) return false;
    if (searchFilter_.filterEgg && !pkm.isEgg()) return false;
    if (searchFilter_.filterAlpha && !pkm.isAlpha()) return false;

    if (searchFilter_.gender != GenderFilter::Any) {
        uint8_t g = pkm.gender();
        if (searchFilter_.gender == GenderFilter::Male && g != 0) return false;
        if (searchFilter_.gender == GenderFilter::Female && g != 1) return false;
        if (searchFilter_.gender == GenderFilter::Genderless && g != 2) return false;
    }

    if (!pkm.isEgg()) {
        uint8_t lv = pkm.level();
        if (searchFilter_.levelMin > 0 && lv < searchFilter_.levelMin) return false;
        if (searchFilter_.levelMax > 0 && lv > searchFilter_.levelMax) return false;
    }

    if (searchFilter_.perfectIVs != PerfectIVFilter::Off) {
        int perfect = 0;
        if (pkm.ivHp()  == 31) perfect++;
        if (pkm.ivAtk() == 31) perfect++;
        if (pkm.ivDef() == 31) perfect++;
        if (pkm.ivSpe() == 31) perfect++;
        if (pkm.ivSpA() == 31) perfect++;
        if (pkm.ivSpD() == 31) perfect++;
        if (searchFilter_.perfectIVs == PerfectIVFilter::AtLeastOne && perfect == 0) return false;
        if (searchFilter_.perfectIVs == PerfectIVFilter::All6 && perfect < 6) return false;
    }

    if (searchFilter_.ribbonFilter != RibbonFilter::Off) {
        auto ribbons = pkm.getRibbonsAndMarks();
        if (searchFilter_.ribbonFilter == RibbonFilter::HasAny) {
            if (ribbons.empty()) return false;
        } else if (searchFilter_.ribbonFilter == RibbonFilter::HasRibbon) {
            bool found = false;
            for (const auto& r : ribbons) { if (!r.isMark) { found = true; break; } }
            if (!found) return false;
        } else if (searchFilter_.ribbonFilter == RibbonFilter::HasMark) {
            bool found = false;
            for (const auto& r : ribbons) { if (r.isMark) { found = true; break; } }
            if (!found) return false;
        }
    }

    return true;
}

void UI::executeSearch() {
    searchResults_.clear();

    auto toLower = [](const std::string& s) {
        std::string out = s;
        for (auto& c : out) c = std::tolower(static_cast<unsigned char>(c));
        return out;
    };
    std::string filterSpecies = toLower(searchFilter_.speciesName);
    std::string filterOT = toLower(searchFilter_.otName);

    auto scanPanel = [&](Panel panel) {
        int boxes, slots;
        if (panel == Panel::Game) {
            if (isDualBankMode()) {
                if (leftBankName_.empty()) return;
                boxes = bankLeft_.boxCount();
                slots = bankLeft_.slotsPerBox();
            } else {
                boxes = save_.boxCount();
                slots = save_.slotsPerBox();
            }
        } else {
            boxes = bank_.boxCount();
            slots = bank_.slotsPerBox();
        }

        for (int b = 0; b < boxes; b++) {
            for (int s = 0; s < slots; s++) {
                Pokemon pkm = getPokemonAt(b, s, panel);
                if (matchesSearchFilter(pkm, filterSpecies, filterOT)) {
                    SearchResult r;
                    r.panel = panel;
                    r.box = b;
                    r.slot = s;
                    r.speciesName = SpeciesName::get(pkm.species());
                    r.level = pkm.level();
                    r.isShiny = pkm.isShiny();
                    r.isEgg = pkm.isEgg();
                    r.isAlpha = pkm.isAlpha();
                    r.gender = pkm.gender();
                    r.otName = pkm.otName();
                    searchResults_.push_back(r);
                }
            }
        }
    };

    scanPanel(Panel::Game);
    scanPanel(Panel::Bank);

    if (searchFilter_.mode == SearchMode::Highlight) {
        searchMatchSet_.clear();
        for (const auto& r : searchResults_) {
            uint64_t key = (static_cast<uint64_t>(r.panel == Panel::Bank ? 1 : 0) << 48)
                         | (static_cast<uint64_t>(r.box) << 16)
                         | static_cast<uint64_t>(r.slot);
            searchMatchSet_.insert(key);
        }
        searchHighlightActive_ = true;
        showSearchFilter_ = false;
    } else {
        searchMatchSet_.clear();
        searchHighlightActive_ = false;
        searchResultCursor_ = 0;
        searchResultScroll_ = 0;
        showSearchFilter_ = false;
        showSearchResults_ = true;
    }
}

void UI::openBoxView(Panel panel) {
    if (showDetail_ || showMenu_ || holding_ || yHeld_)
        return;
    showBoxView_ = true;
    boxViewPanel_ = panel;
    boxViewCursor_ = (panel == Panel::Game) ? gameBox_ : bankBox_;
}

void UI::closeBoxView(bool navigate) {
    showBoxView_ = false;
    zlPressed_ = false;
    zrPressed_ = false;
    if (navigate) {
        if (boxViewPanel_ == Panel::Game) {
            gameBox_ = boxViewCursor_;
            if (cursor_.panel == Panel::Game)
                cursor_.box = boxViewCursor_;
        } else {
            bankBox_ = boxViewCursor_;
            if (cursor_.panel == Panel::Bank)
                cursor_.box = boxViewCursor_;
        }
    }
}

void UI::moveBoxViewCursor(int dx, int dy) {
    int totalBoxes;
    if (boxViewPanel_ == Panel::Game) {
        totalBoxes = (isDualBankMode()) ? bankLeft_.boxCount() : save_.boxCount();
    } else {
        totalBoxes = bank_.boxCount();
    }
    int col = boxViewCursor_ % BV_COLS;
    int row = boxViewCursor_ / BV_COLS;
    int maxRow = (totalBoxes - 1) / BV_COLS;

    col += dx;
    row += dy;

    if (col < 0) col = BV_COLS - 1;
    if (col >= BV_COLS) col = 0;
    if (row < 0) row = maxRow;
    if (row > maxRow) row = 0;

    int newIdx = row * BV_COLS + col;
    if (newIdx >= totalBoxes)
        newIdx = (dx > 0 || dy > 0) ? 0 : totalBoxes - 1;

    boxViewCursor_ = newIdx;
}

bool UI::isSearchMatch(Panel panel, int box, int slot) const {
    uint64_t key = (static_cast<uint64_t>(panel == Panel::Bank ? 1 : 0) << 48)
                 | (static_cast<uint64_t>(box) << 16)
                 | static_cast<uint64_t>(slot);
    return searchMatchSet_.count(key) > 0;
}

void UI::clearSearchHighlight() {
    searchHighlightActive_ = false;
    searchMatchSet_.clear();
    searchResults_.clear();
}

void UI::refreshHighlightSet() {
    if (!searchHighlightActive_) return;
    searchMatchSet_.clear();
    searchResults_.clear();

    auto toLower = [](const std::string& s) {
        std::string out = s;
        for (auto& c : out) c = std::tolower(static_cast<unsigned char>(c));
        return out;
    };
    std::string filterSpecies = toLower(searchFilter_.speciesName);
    std::string filterOT = toLower(searchFilter_.otName);

    auto scanPanel = [&](Panel panel) {
        int boxes, slots;
        if (panel == Panel::Game) {
            if (isDualBankMode()) {
                if (leftBankName_.empty()) return;
                boxes = bankLeft_.boxCount();
                slots = bankLeft_.slotsPerBox();
            } else {
                boxes = save_.boxCount();
                slots = save_.slotsPerBox();
            }
        } else {
            boxes = bank_.boxCount();
            slots = bank_.slotsPerBox();
        }

        for (int b = 0; b < boxes; b++) {
            for (int s = 0; s < slots; s++) {
                Pokemon pkm = getPokemonAt(b, s, panel);
                if (matchesSearchFilter(pkm, filterSpecies, filterOT)) {
                    uint64_t key = (static_cast<uint64_t>(panel == Panel::Bank ? 1 : 0) << 48)
                                 | (static_cast<uint64_t>(b) << 16)
                                 | static_cast<uint64_t>(s);
                    searchMatchSet_.insert(key);
                    searchResults_.push_back({panel, b, s, SpeciesName::get(pkm.species()),
                        pkm.level(), pkm.isShiny(), pkm.isEgg(), pkm.isAlpha(),
                        pkm.gender(), pkm.otName()});
                }
            }
        }
    };

    scanPanel(Panel::Game);
    scanPanel(Panel::Bank);
}

// --- Wondercard List ---

void UI::handleWondercardListInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }

    int count = static_cast<int>(wcList_.size());
    if (count == 0) {
        // Only B to close
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            showWondercardList_ = false;
        return;
    }

    auto scrollIntoView = [&]() {
        constexpr int ROW_H = 36;
        int visibleRows = (550 - 40 - 50) / ROW_H; // matches popup layout
        if (wcListCursor_ < wcListScroll_)
            wcListScroll_ = wcListCursor_;
        else if (wcListCursor_ >= wcListScroll_ + visibleRows)
            wcListScroll_ = wcListCursor_ - visibleRows + 1;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (wcListCursor_ > 0) wcListCursor_--;
                else wcListCursor_ = count - 1;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (wcListCursor_ < count - 1) wcListCursor_++;
                else wcListCursor_ = 0;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: // L = page up
                wcListCursor_ = std::max(0, wcListCursor_ - 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // R = page down
                wcListCursor_ = std::min(count - 1, wcListCursor_ + 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm/inject
                injectWondercard(wcList_[wcListCursor_]);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                showWondercardList_ = false;
                break;
        }
    }
}

void UI::injectWondercard(const WCInfo& info) {
    if (!info.valid) {
        showMessageAndWait(i18n::get(StrKey::InvalidWC), i18n::get(StrKey::InvalidWCBody));
        return;
    }

    // Determine target slot (where cursor is)
    Panel panel = cursor_.panel;
    int box = (panel == Panel::Game) ? gameBox_ : bankBox_;
    int slot = cursor_.slot(gridCols());

    // If !hasOT and target is bank panel: show restriction
    if (!info.hasOT) {
        if (isDualBankMode()) {
            showMessageAndWait(i18n::get(StrKey::CannotInject),
                i18n::get(StrKey::CannotInjectBody));
            return;
        }
        if (panel == Panel::Bank) {
            showMessageAndWait(i18n::get(StrKey::CannotInjectBank),
                i18n::get(StrKey::CannotInjectBankBody));
            return;
        }
    }

    // Check if target slot is empty
    Pokemon existing = getPokemonAt(box, slot, panel);
    if (!existing.isEmpty()) {
        showMessageAndWait(i18n::get(StrKey::SlotOccupied),
            i18n::get(StrKey::SlotOccupiedBody));
        return;
    }

    // Get trainer info
    TrainerInfo trainer;
    if (!info.hasOT || !isDualBankMode()) {
        trainer = save_.getTrainerInfo();
        if (!trainer.valid) {
            trainer.id32 = 0;
            trainer.gender = 0;
            trainer.language = 2;
            trainer.valid = true;
        }
    }
    if (!trainer.valid) {
        trainer.id32 = 0;
        trainer.gender = 0;
        trainer.language = 2;
        trainer.otName = std::u16string(u"Player", 6);
        trainer.valid = true;
    }

    Pokemon pkm;
    uint16_t natId;

    if (isSwSh(selectedGame_)) {
        // WC8 path
        WC8 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        // SW=44, SH=45
        trainer.gameVersion = (selectedGame_ == GameType::Sh) ? 45 : 44;

        pkm = wc.convertToPK8(trainer);
        natId = wc.species(); // already national dex
    } else if (selectedGame_ == GameType::ZA) {
        // WA9 path (Legends: Z-A)
        WA9 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        trainer.gameVersion = 52; // ZA
        pkm = wc.convertToPA9(trainer);
        natId = SpeciesConverter::getNational9(wc.speciesInternal());
    } else if (isBDSP(selectedGame_)) {
        // WB8 path (Brilliant Diamond / Shining Pearl)
        WB8 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        trainer.gameVersion = (selectedGame_ == GameType::BD) ? 48 : 49;
        pkm = wc.convertToPB8(trainer);
        natId = wc.species(); // already national dex
    } else if (selectedGame_ == GameType::LA) {
        // WA8 path (Legends: Arceus)
        WA8 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        trainer.gameVersion = 47; // PLA
        pkm = wc.convertToPA8(trainer);
        natId = wc.species(); // already national dex
    } else if (isLGPE(selectedGame_)) {
        // WB7 path (Let's Go Pikachu/Eevee)
        WB7 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        trainer.gameVersion = (selectedGame_ == GameType::GP) ? 42 : 43;
        pkm = wc.convertToPB7(trainer);
        natId = wc.species(); // already national dex
    } else {
        // WC9 path (Scarlet/Violet)
        WC9 wc;
        if (!wc.loadFromFile(info.path)) {
            showWondercardList_ = false;
            showMessageAndWait(i18n::get(StrKey::Error), i18n::get(StrKey::FailedLoadWC));
            return;
        }

        // SL=50, VL=51
        trainer.gameVersion = (selectedGame_ == GameType::V) ? 51 : 50;

        pkm = wc.convertToPK9(trainer);
        natId = SpeciesConverter::getNational9(wc.speciesInternal());
    }

    // Ensure correct game type for the panel
    if (panel == Panel::Game && !isDualBankMode())
        pkm.gameType_ = selectedGame_;
    else if (isSwSh(selectedGame_))
        pkm.gameType_ = GameType::Sw;
    else if (selectedGame_ == GameType::ZA)
        pkm.gameType_ = GameType::ZA;
    else if (isBDSP(selectedGame_))
        pkm.gameType_ = GameType::BD;
    else if (selectedGame_ == GameType::LA)
        pkm.gameType_ = GameType::LA;
    else if (isLGPE(selectedGame_))
        pkm.gameType_ = selectedGame_;
    else
        pkm.gameType_ = GameType::S;

    // Place the pokemon
    setPokemonAt(box, slot, panel, pkm);

    showWondercardList_ = false;

    std::string panelName = (panel == Panel::Game)
        ? (isDualBankMode() ? i18n::get(StrKey::LocLeft) : i18n::get(StrKey::LocSave))
        : (isDualBankMode() ? i18n::get(StrKey::LocRight) : i18n::get(StrKey::LocBank));
    showMessageAndWait(i18n::get(StrKey::Injected),
        i18n::fmt(StrKey::InjectedBody, SpeciesName::get(natId), panelName,
                  std::to_string(box + 1), std::to_string(slot + 1)));
}

void UI::handlePkImportListInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }

    int count = static_cast<int>(pkImportList_.size());
    if (count == 0) {
        // Only B to close
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            showPkImportList_ = false;
        return;
    }

    auto scrollIntoView = [&]() {
        constexpr int ROW_H = 36;
        int visibleRows = (550 - 40 - 50) / ROW_H; // matches popup layout
        if (pkImportCursor_ < pkImportScroll_)
            pkImportScroll_ = pkImportCursor_;
        else if (pkImportCursor_ >= pkImportScroll_ + visibleRows)
            pkImportScroll_ = pkImportCursor_ - visibleRows + 1;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (pkImportCursor_ > 0) pkImportCursor_--;
                else pkImportCursor_ = count - 1;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (pkImportCursor_ < count - 1) pkImportCursor_++;
                else pkImportCursor_ = 0;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: // L = page up
                pkImportCursor_ = std::max(0, pkImportCursor_ - 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // R = page down
                pkImportCursor_ = std::min(count - 1, pkImportCursor_ + 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = importa e resta (multi)
                if (importPkFile(pkImportList_[pkImportCursor_], false))
                    pkImportList_[pkImportCursor_].imported = true;
                break;
            case SDL_CONTROLLER_BUTTON_START: // + = importa ed esci
                importPkFile(pkImportList_[pkImportCursor_], true);
                break;
            case SDL_CONTROLLER_BUTTON_X: // Switch Y = seleziona/deseleziona
                pkImportList_[pkImportCursor_].selected = !pkImportList_[pkImportCursor_].selected;
                markDirty();
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Switch X = importa selezionati e resta
                importSelectedPkFiles(false);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                showPkImportList_ = false;
                break;
        }
    }
}

void UI::openTradeList() {
    if (!save_.isLoaded() || !TradeEvo::supported(save_.gameType())) {
        showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeFailed));
        return;
    }
    uint32_t t0 = SDL_GetTicks();
    rebuildTradeCandidates();
    uint32_t dt = SDL_GetTicks() - t0;
    DebugLog::line("trade: open candidates=%zu dt=%ums save=%s", tradeCandidates_.size(), dt, gameDisplayNameOf(save_.gameType()));
    if (tradeCandidates_.empty()) {
        showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeNone));
        return;
    }
    tradeCursor_ = 0;
    tradeScroll_ = 0;
    showTradeList_ = true;
}

// Party soltanto (per ora): 6 slot, niente scan dei 420 box — apre istantaneo.
// Estenderemo a tutti i box quando il dex-sync sarà stabile (vedi TODO).
void UI::rebuildTradeCandidates() {
    tradeCandidates_.clear();
    for (int i = 0; i < 6; i++) {
        Pokemon pkm = save_.getPartySlot(i);
        if (pkm.isEmpty() || pkm.isEgg() || pkm.species() == 0) continue;
        if (TradeEvo::baseRuleFor(pkm.species()))
            tradeCandidates_.push_back({-1, i});
    }
    DebugLog::line("trade: candidates %zu (party-only)", tradeCandidates_.size());
}

void UI::doTradeEvolve(int candidateIdx) {
    if (candidateIdx < 0 || candidateIdx >= (int)tradeCandidates_.size()) return;
    TradeCandidate ref = tradeCandidates_[candidateIdx];
    Pokemon pkm = ref.box < 0 ? save_.getPartySlot(ref.slot) : save_.getBoxSlot(ref.box, ref.slot);
    if (pkm.isEmpty() || pkm.isEgg() || pkm.species() == 0) return;
    uint16_t heldRaw = pkm.heldItem();
    uint16_t heldModern = TradeEvo::heldToModern(save_.gameType(), heldRaw);
    DebugLog::line("trade: doEvolve idx=%d spc=%u heldRaw=%u heldModern=%u box=%d slot=%d", candidateIdx, pkm.species(), heldRaw, heldModern, ref.box, ref.slot);
    // Everstone blocca qualsiasi evoluzione anche per scambio
    if (heldModern == 229 || heldRaw == 195) { // 229 modern Everstone, 195 gen3 Everstone
        showMessageAndWait(i18n::get(StrKey::TradeTitle), "Everstone blocca l'evoluzione.");
        return;
    }
    const TradeEvo::TradeRule* rule = TradeEvo::findRule(pkm.species(), heldModern);
    if (!rule) {
        // Nei filtri la specie PUO' evolvere: qui manca lo strumento.
        const TradeEvo::TradeRule* base = TradeEvo::baseRuleFor(pkm.species());
        if (base && base->heldModern != 0) {
            std::string where = ItemLocations::footerLine(base->heldModern);
            std::string msg = i18n::get(StrKey::TradeNeedsItem) + "\n" + ItemLocations::itemName(base->heldModern);
            if (!where.empty()) msg += "\n" + where;
            showMessageAndWait(i18n::get(StrKey::TradeTitle), msg);
        } else {
            showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeNeedsItem));
        }
        return;
    }
    if (TradeEvo::isOutOfRange(save_.gameType(), rule->to)) {
        showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeOutOfRange));
        return;
    }

    // Karrablast 588 ↔ Shelmet 616 : prova doppio se controparte nel save.
    if (TradeEvo::isPairedSpecies(pkm.species())) {
        uint16_t need = TradeEvo::pairedCounterpart(pkm.species());
        TradeCandidate other{-2, -1};
        bool found = false;
        for (int i = 0; i < 6 && !found; i++) {
            if (ref.box < 0 && i == ref.slot) continue;
            Pokemon q = save_.getPartySlot(i);
            if (!q.isEmpty() && !q.isEgg() && q.species() == need) { other = {-1, i}; found = true; }
        }
        // Box search disabilitato per ora (party-only come da TODO): riattiveremo
        // quando il dex-sync sarà stabile.
        if (found) {
            Pokemon pkm2 = other.box < 0 ? save_.getPartySlot(other.slot) : save_.getBoxSlot(other.box, other.slot);
            uint16_t held2Modern = TradeEvo::heldToModern(save_.gameType(), pkm2.heldItem());
            const TradeEvo::TradeRule* rule2 = TradeEvo::findRule(pkm2.species(), held2Modern);
            if (rule2 && !TradeEvo::isOutOfRange(save_.gameType(), rule2->to)) {
                std::string from1 = SpeciesName::get(pkm.species());
                std::string to1 = SpeciesName::get(rule->to);
                std::string from2 = SpeciesName::get(pkm2.species());
                std::string to2 = SpeciesName::get(rule2->to);
                if (!showConfirmDialog(i18n::get(StrKey::TradeConfirmTitle),
                                       i18n::fmt(StrKey::TradeDoubleConfirmBody, from1, from2, to1, to2)))
                    return;
                Pokemon a = pkm, b = pkm2;
                if (!TradeEvo::applyTradeEvolution(a, rule) || !TradeEvo::applyTradeEvolution(b, rule2)) {
                    showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeFailed));
                    return;
                }
                if (ref.box < 0) save_.setPartySlot(ref.slot, a); else save_.setBoxSlot(ref.box, ref.slot, a);
                if (other.box < 0) save_.setPartySlot(other.slot, b); else save_.setBoxSlot(other.box, other.slot, b);
                Pokedex::registerPokemon(save_, a);
                Pokedex::registerPokemon(save_, b);
                persistGameSaveIfDirty();
                showTradeList_ = false;
                playTradeEvolveAnim(pkm.species(), rule->to);
                playTradeEvolveAnim(pkm2.species(), rule2->to);
                showMessageAndWait(i18n::get(StrKey::TradeDoneTitle),
                                   i18n::fmt(StrKey::TradeDoubleDoneBody, from1, to1, from2, to2));
                return;
            }
        }
        // nessuna controparte valida → fallback singolo (hybrid)
    }

    const std::string from = SpeciesName::get(pkm.species());
    const std::string to = SpeciesName::get(rule->to);
    uint16_t fromSpeciesAnim = pkm.species();
    uint16_t toSpeciesAnim = rule->to;
    if (!showConfirmDialog(i18n::get(StrKey::TradeConfirmTitle),
                           i18n::fmt(StrKey::TradeConfirmBody, from, to)))
        return;
    if (!TradeEvo::applyTradeEvolution(pkm, rule)) {
        showMessageAndWait(i18n::get(StrKey::TradeTitle), i18n::get(StrKey::TradeFailed));
        return;
    }
    if (ref.box < 0) save_.setPartySlot(ref.slot, pkm);
    else save_.setBoxSlot(ref.box, ref.slot, pkm);
    Pokedex::registerPokemon(save_, pkm);
    persistGameSaveIfDirty();
    showTradeList_ = false;
    playTradeEvolveAnim(fromSpeciesAnim, toSpeciesAnim);
    showMessageAndWait(i18n::get(StrKey::TradeDoneTitle),
                       i18n::fmt(StrKey::TradeDoneBody, from, to));
}

void UI::playTradeEvolveAnim(uint16_t fromSpecies, uint16_t toSpecies) {
    if (!renderer_) return;
    if (!Settings::tradeAnim()) return; // toggle Aspetto: animazione scambio
    markDirty(); // Forza redraw dopo il ritorno (stesso schema dei modali bloccanti sopra)

    SDL_Texture* fromTex = getSprite(fromSpecies, 0);
    SDL_Texture* toTex = getSprite(toSpecies, 0);
    std::string fromName = SpeciesName::get(fromSpecies);
    std::string toName = SpeciesName::get(toSpecies);

    constexpr int SPR = 176;
    // Ritmo ricalcato su quello dei giochi veri (nessuna fonte pubblica
    // documenta i frame esatti: la sequenza di scambio/evoluzione li' e'
    // volutamente lenta e "cerimoniale", qualche secondo a testa, non uno
    // scatto) -- ogni Pokemon ha un ciclo entra/pausa/esce di ENTER_MS+
    // PAUSE_MS+EXIT_MS, i due cicli in sequenza fanno DUR_MS totali.
    constexpr Uint32 ENTER_MS = 900, PAUSE_MS = 750, EXIT_MS = 900;
    constexpr Uint32 PHASE_MS = ENTER_MS + PAUSE_MS + EXIT_MS;
    constexpr Uint32 DUR_MS = PHASE_MS * 2;
    constexpr Uint32 FADE_MS = 350; // dissolvenza in apertura/chiusura di ciascun ciclo
    Uint32 start = SDL_GetTicks();

    // Palette dedicata dell'effetto (non il tema dell'app: stesso blu/verde
    // dell'animazione di scambio dei giochi veri, un colore per lato).
    const SDL_Color BLUE_SIDE  = {20, 52, 122, 255};
    const SDL_Color GREEN_SIDE = {22, 110, 58, 255};
    const SDL_Color STREAK     = {225, 240, 255, 255};

    // Hash deterministico 0..1 da un intero (niente stato rand persistente).
    auto hash1 = [](int i) {
        float x = std::sin((float)i * 12.9898f) * 43758.5453f;
        return x - std::floor(x);
    };

    // Estremi della diagonale (alto-destra -> basso-sinistra, stessa
    // formula dello sfondo sotto) per i percorsi PARALLELI ad essa: ciascun
    // Pokemon cammina nel proprio triangolo, mai sulla linea -- OFFSET lo
    // scosta di lato (verso alto-sinistra per il blu, verso basso-destra
    // per il verde), la y segue comunque la diagonale cosi' il percorso
    // resta sempre parallelo ad essa senza mai attraversarla.
    constexpr float OFFSET = 230.0f;
    float blY = SCREEN_H * 0.86f, trY = SCREEN_H * 0.14f;
    auto splitXAt = [=](float y) { return SCREEN_W * (1.0f - y / SCREEN_H); };

    // Percorso di un Pokemon nel proprio triangolo: entra (0 -> meta'), si
    // ferma un attimo al centro, poi esce (meta' -> 1) dal lato opposto --
    // mai un salto, sempre in scia alla diagonale.
    auto along = [=](Uint32 localMs) -> float {
        if (localMs < ENTER_MS) {
            float p = (float)localMs / ENTER_MS;
            float ep = p * p * (3 - 2 * p); // smoothstep
            return ep * 0.5f;
        } else if (localMs < ENTER_MS + PAUSE_MS) {
            return 0.5f;
        } else {
            float p = (float)(localMs - ENTER_MS - PAUSE_MS) / EXIT_MS;
            if (p > 1.0f) p = 1.0f;
            float ep = p * p * (3 - 2 * p);
            return 0.5f + ep * 0.5f;
        }
    };
    auto fadeAlpha = [=](Uint32 localMs) -> Uint8 {
        float a;
        if (localMs < FADE_MS) a = (float)localMs / FADE_MS;
        else if (localMs > PHASE_MS - FADE_MS) a = (float)(PHASE_MS - localMs) / FADE_MS;
        else a = 1.0f;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        return (Uint8)(255 * a);
    };

    // Direzione/perpendicolare della diagonale (basso-sx -> alto-dx) per
    // orientare segmenti e crocette lungo il taglio: diagUx/Uy e' il verso
    // di percorrenza, diagVx/Vy la perpendicolare (spargimento laterale).
    // STREAK_SPEED e' in frazioni di diagonale al secondo, piu' alta della
    // velocita' media del Pokemon cosi' che i fasci lo superino.
    const float diagLen = std::sqrt((float)(SCREEN_W * SCREEN_W + SCREEN_H * SCREEN_H));
    const float diagUx = SCREEN_W / diagLen, diagUy = -SCREEN_H / diagLen;
    const float diagVx = SCREEN_H / diagLen, diagVy = SCREEN_W / diagLen;
    constexpr float STREAK_SPEED = 0.9f;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) done = true;
        }

        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed > DUR_MS) elapsed = DUR_MS;
        float secs = elapsed / 1000.0f;

        SDL_SetRenderDrawColor(renderer_, 8, 8, 14, 255);
        SDL_RenderClear(renderer_);

        // Due sfondi separati dalla diagonale (alto-destra -> basso-sinistra):
        // blu nel triangolo alto-sinistra, verde in quello basso-destra.
        for (int y = 0; y < SCREEN_H; y++) {
            int splitX = (int)splitXAt((float)y);
            if (splitX > 0) drawRect(0, y, splitX, 1, BLUE_SIDE);
            if (splitX < SCREEN_W) drawRect(splitX, y, SCREEN_W - splitX, 1, GREEN_SIDE);
        }

        // Particellato a righe lungo la diagonale: segmenti sparsi su tutta
        // la larghezza (non solo vicino al taglio), di lunghezza variabile,
        // che scorrono nella stessa direzione del Pokemon in scena ma a
        // velocita' piu' elevata della sua camminata.
        float dirSign = (elapsed < PHASE_MS) ? 1.0f : -1.0f;
        float localSecs = ((elapsed < PHASE_MS) ? elapsed : (elapsed - PHASE_MS)) / 1000.0f;
        for (int i = 0; i < 16; i++) {
            float baseS = hash1(i * 13 + 5);
            float sPos = baseS + dirSign * localSecs * STREAK_SPEED;
            sPos -= std::floor(sPos);
            float segLen = 70.0f + hash1(i * 9 + 2) * 170.0f;
            float perp = (hash1(i * 17 + 7) - 0.5f) * (SCREEN_W * 1.1f);
            float cx = sPos * SCREEN_W + perp * diagVx;
            float cy = SCREEN_H * (1.0f - sPos) + perp * diagVy;
            float half = segLen * 0.5f;
            int x1 = (int)(cx - diagUx * half), y1 = (int)(cy - diagUy * half);
            int x2 = (int)(cx + diagUx * half), y2 = (int)(cy + diagUy * half);
            Uint8 a = (Uint8)(90 + 90 * hash1(i * 5 + 11));
            SDL_SetRenderDrawColor(renderer_, STREAK.r, STREAK.g, STREAK.b, a);
            SDL_RenderDrawLine(renderer_, x1, y1, x2, y2);
        }
        // Stelline: crocette che scintillano (alpha pulsante), sparse su
        // tutta la fascia attorno alla diagonale cosi' da essere ben visibili.
        for (int i = 0; i < 26; i++) {
            float baseS = hash1(i * 23 + 3);
            float sPos = baseS + dirSign * localSecs * (STREAK_SPEED * 0.5f);
            sPos -= std::floor(sPos);
            float perp = (hash1(i * 11 + 6) - 0.5f) * (SCREEN_W * 0.85f);
            float sx = sPos * SCREEN_W + perp * diagVx;
            float sy = SCREEN_H * (1.0f - sPos) + perp * diagVy;
            if (sx < 5 || sx >= SCREEN_W - 5 || sy < 5 || sy >= SCREEN_H - 5) continue;
            float twinkle = 0.5f + 0.5f * std::sin(secs * 5.0f + i * 1.7f);
            Uint8 a = (Uint8)(120 + 135 * twinkle);
            int arm = (hash1(i * 3 + 1) > 0.6f) ? 6 : 4;
            SDL_SetRenderDrawColor(renderer_, STREAK.r, STREAK.g, STREAK.b, a);
            int ix = (int)sx, iy = (int)sy;
            SDL_RenderDrawLine(renderer_, ix - arm, iy, ix + arm, iy);
            SDL_RenderDrawLine(renderer_, ix, iy - arm, ix, iy + arm);
        }

        SDL_Color shadow = {0, 0, 0, 200};
        if (elapsed < PHASE_MS) {
            // Primo Pokemon (quello che se ne va): corsia blu, dal
            // basso-sinistra verso l'alto-destra della propria corsia.
            Uint32 localMs = elapsed;
            float al = along(localMs);
            float y = blY + (trY - blY) * al;
            int sx = (int)(splitXAt(y) - OFFSET);
            int sy = (int)y;
            Uint8 a = fadeAlpha(localMs);
            if (fromTex && a > 0) {
                SDL_SetTextureAlphaMod(fromTex, a);
                drawSpriteFit(sx - SPR / 2, sy - SPR / 2, SPR, SPR, fromTex);
                SDL_SetTextureAlphaMod(fromTex, 255);
            }
            drawTextCentered(fromName, SCREEN_W / 2 + 2, SCREEN_H - 48, shadow, font_);
            drawTextCentered(fromName, SCREEN_W / 2, SCREEN_H - 50, T().text, font_);
        } else {
            // Secondo Pokemon (l'evoluzione): corsia verde, dal lato e
            // verso opposti -- entra dall'alto-destra verso il basso-sinistra.
            Uint32 localMs = elapsed - PHASE_MS;
            float al = along(localMs);
            float y = trY + (blY - trY) * al;
            int sx = (int)(splitXAt(y) + OFFSET);
            int sy = (int)y;
            Uint8 a = fadeAlpha(localMs);
            if (toTex && a > 0) {
                SDL_SetTextureAlphaMod(toTex, a);
                drawSpriteFit(sx - SPR / 2, sy - SPR / 2, SPR, SPR, toTex);
                SDL_SetTextureAlphaMod(toTex, 255);
            }
            drawTextCentered(toName, SCREEN_W / 2 + 2, SCREEN_H - 48, shadow, font_);
            drawTextCentered(toName, SCREEN_W / 2, SCREEN_H - 50, T().text, font_);
        }

        SDL_RenderPresent(renderer_);
        SDL_Delay(16);

        if (elapsed >= DUR_MS) done = true;
    }
}

void UI::handleTradeListInput(const SDL_Event& event) {
    int count = (int)tradeCandidates_.size();
    constexpr int VISIBLE = 6;
    auto scrollIntoView = [&]() {
        if (tradeCursor_ < tradeScroll_)
            tradeScroll_ = tradeCursor_;
        else if (tradeCursor_ >= tradeScroll_ + VISIBLE)
            tradeScroll_ = tradeCursor_ - VISIBLE + 1;
    };
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (count > 0) {
                    tradeCursor_ = (tradeCursor_ > 0) ? tradeCursor_ - 1 : count - 1;
                    scrollIntoView();
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (count > 0) {
                    tradeCursor_ = (tradeCursor_ < count - 1) ? tradeCursor_ + 1 : 0;
                    scrollIntoView();
                }
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = evolvi
                doTradeEvolve(tradeCursor_);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = chiudi
                showTradeList_ = false;
                break;
        }
    }
}

std::vector<UI::GenMonDef> UI::genMonTable() {
    // Segnalini debug on-demand (tutti Sword-origin L50, importati in banca
    // come OHPKM e poi trasferiti via drop normale). Aggiungere qui quando
    // servono altri: label, specie, livello, 4 mosse.
    static const GenMonDef TABLE[] = {
        { "Pikachu drops (L2)", 25, 50, {800, 801, 802, 803} },
        { "Pikachu mixed", 25, 50, {85, 800, 129, 801} },
        { "Greninja new moves", 658, 50, {800, 801, 802, 803} },
        { "Koraidon dex-cut", 1007, 50, {800, 801, 802, 803} },
        { "Pikachu Gen1-clean", 25, 50, {85, 98, 86, 87} },
        // Regressione fix Pb8 (dex Sinnoh, non Galar): in Sinnoh si, a Galar no.
        { "Pidgey Sinnoh", 16, 5, {33, 16, 0, 0} },
        { "Chimchar Sinnoh", 390, 5, {10, 52, 0, 0} },
        { "Starly Sinnoh", 396, 5, {33, 16, 0, 0} },
    };
    return std::vector<GenMonDef>(TABLE, TABLE + sizeof(TABLE) / sizeof(TABLE[0]));
}

void UI::handleGenMonListInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }

    int count = (int)genMonList_.size();
    if (count == 0) {
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            showGenMonList_ = false;
        return;
    }
    auto scrollIntoView = [&]() {
        constexpr int VISIBLE = 12;
        if (genMonCursor_ < genMonScroll_)
            genMonScroll_ = genMonCursor_;
        else if (genMonCursor_ >= genMonScroll_ + VISIBLE)
            genMonScroll_ = genMonCursor_ - VISIBLE + 1;
    };
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (genMonCursor_ > 0) genMonCursor_--;
                else genMonCursor_ = count - 1;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (genMonCursor_ < count - 1) genMonCursor_++;
                else genMonCursor_ = 0;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                genMonCursor_ = std::max(0, genMonCursor_ - 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                genMonCursor_ = std::min(count - 1, genMonCursor_ + 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = genera e importa, resta
                importGeneratedMon(genMonList_[genMonCursor_]);
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = chiudi
            case SDL_CONTROLLER_BUTTON_START:
                showGenMonList_ = false;
                break;
        }
    }
}

bool UI::importGeneratedMon(const GenMonDef& def) {
    if (activeBankPath_.empty()) {
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), i18n::get(StrKey::ImportPkNeedBank));
        return false;
    }
    PkmHandle* h = OpenHomeNX::generateTestPkm(def.species, def.level,
        def.moves[0], def.moves[1], def.moves[2], def.moves[3]);
    if (!h) {
        DebugLog::line("generate mon: FFI NULL (%s)", def.label);
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), i18n::get(StrKey::CouldNotWrite));
        return false;
    }
    std::vector<uint8_t> blob = OpenHomeNX::getOhpkmBytes(h);
    uint16_t sp = OpenHomeNX::ohpkmSpecies(h);
    OpenHomeNX::freePkm(h);
    if (blob.empty()) {
        DebugLog::line("generate mon: blob vuoto (%s)", def.label);
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), i18n::get(StrKey::CouldNotWrite));
        return false;
    }
    if (bank_.isCrossGen()) {
        for (int b = 0; b < bank_.boxCount(); b++) {
            for (int s = 0; s < bank_.slotsPerBox(); s++) {
                if (bank_.ohpkmAt(b, s).empty()) {
                    bank_.setOhpkmAt(b, s, blob);
                    markDirty();
                    invalidateSlotDisplay(Panel::Bank, b);
                    DebugLog::line("generate mon: %s -> specie %u in banca %d:%d", def.label, sp, b, s);
                    showMessageAndWait(i18n::get(StrKey::ImportPkTitle), def.label);
                    return true;
                }
            }
        }
    } else {
        // Banca nativa: converti esplicitamente nel formato della banca.
        // NON via prepareForPlacement: p ha data azzerati (isEmpty) e
        // prepare uscirebbe subito senza scrivere niente (Smeraldo: import
        // "ok" ma slot vuoto). Transfer diretto + record nativo.
        GameType bkGame = bank_.gameType();
        const int dg = ohTargetGenFor(bkGame);
        if (dg == 0) {
            showMessageAndWait(i18n::get(StrKey::TransferTitle),
                i18n::fmt(StrKey::TransferCantBuild, gameDisplayNameOf(bkGame)));
            return false;
        }
        PkmHandle* h = OpenHomeNX::loadOhpkm(blob);
        if (!h) {
            showMessageAndWait(i18n::get(StrKey::TransferTitle),
                               i18n::get(StrKey::TransferBadOhpkm));
            return false;
        }
        PkmHandle* out = PokemonFFI::transfer(h, static_cast<uint32_t>(dg));
        OpenHomeNX::freePkm(h);
        if (!out) {
            showMessageAndWait(i18n::get(StrKey::TransferTitle),
                i18n::fmt(StrKey::TransferNotInDex, def.label, gameDisplayNameOf(bkGame)));
            return false;
        }
        std::vector<uint8_t> bytes = OpenHomeNX::getPkmBoxBytesForGen(out, static_cast<uint32_t>(dg));
        OpenHomeNX::freePkm(out);
        Pokemon p;
        p.gameType_ = bkGame;
        p.data.fill(0);
        if (bytes.empty() || bytes.size() > p.data.size()) {
            showMessageAndWait(i18n::get(StrKey::TransferTitle),
                               i18n::get(StrKey::TransferNoBytes));
            return false;
        }
        std::memcpy(p.data.data(), bytes.data(), bytes.size());
        for (int b = 0; b < bank_.boxCount(); b++) {
            for (int s = 0; s < bank_.slotsPerBox(); s++) {
                if (bank_.getSlot(b, s).isEmpty()) {
                    bank_.setSlot(b, s, p);
                    markDirty();
                    invalidateSlotDisplay(Panel::Bank, b);
                    DebugLog::line("generate mon: %s -> specie %u in banca nativa %d:%d",
                                   def.label, sp, b, s);
                    showMessageAndWait(i18n::get(StrKey::ImportPkTitle), def.label);
                    return true;
                }
            }
        }
    }
    DebugLog::line("generate mon: banca piena (%s)", def.label);
    showMessageAndWait(i18n::get(StrKey::ImportPkTitle), i18n::get(StrKey::CouldNotWrite));
    return false;
}

std::vector<UI::PkFileInfo> UI::scanPkImportFiles() {
    std::vector<PkFileInfo> out;
    // Radici scansionate: import/ più l'intero albero export/ (un livello di
    // sottocartelle: CrossGen + cartelle gioco), così un file appena esportato
    // da giochi o banche è subito reimportabile senza spostarlo a mano.
    std::vector<std::string> dirs = { basePath_ + "import/", basePath_ + "export/" };
    {
        DIR* ed = opendir((basePath_ + "export/").c_str());
        if (ed) {
            struct dirent* e;
            while ((e = readdir(ed)) != nullptr) {
                std::string n = e->d_name;
                if (n.empty() || n[0] == '.') continue;
                std::string full = basePath_ + "export/" + n;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    dirs.push_back(full + "/");
            }
            closedir(ed);
        }
    }
    for (const std::string& dir : dirs) {
        // Etichetta cartella per disambiguare omonimi ("" per le radici).
        std::string tag;
        if (dir != basePath_ + "import/" && dir != basePath_ + "export/") {
            std::string t = dir;
            while (!t.empty() && t.back() == '/') t.pop_back();
            auto pos = t.find_last_of('/');
            tag = (pos == std::string::npos) ? t : t.substr(pos + 1);
        }
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.empty() || name[0] == '.') continue;
            std::string low = name;
            for (char& c : low) c = std::tolower((unsigned char)c);
            // Extension -> candidate gens. Our own native export writes
            // decrypted party-size records, cross-gen export writes box
            // records: both must list, so no strict size gate here — the
            // Rust parsers (box/party sizes, decrypt-if-encrypted) decide.
            // ".pkm" is ambiguous: try every gen until one parses.
            static const char* kExts[] = {
                ".pk1", ".pk2", ".pk3", ".pk4", ".pk5", ".pk6", ".pk7",
                ".pk8", ".pk9", ".pa8", ".pa9", ".pb7", ".pb8", ".pkm",
            };
            // Gen per extension (0 = ambiguous .pkm: try 1..9 below).
            static const uint32_t kGenForExt[] = {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0,
            };
            static const uint32_t kPkmGens[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
            int extIdx = -1;
            for (int i = 0; i < 14; i++) {
                const char* e = kExts[i];
                size_t el = std::strlen(e);
                if (low.size() > el && low.compare(low.size() - el, el, e) == 0) { extIdx = i; break; }
            }
            if (extIdx < 0) continue;
            PkFileInfo info;
            info.filename = tag.empty() ? name : (name + " · " + tag);
            info.path = dir + name;
            // Read whole file (cap 376B = largest party record); parsers
            // validate size and content, garbage never lists.
            FILE* f = std::fopen(info.path.c_str(), "rb");
            if (!f) continue;
            std::vector<uint8_t> bytes(376);
            size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            if (got == 0 || got > 376) continue;
            bytes.resize(got);
            PkmHandle* h = nullptr;
            uint32_t foundGen = 0;
            if (kGenForExt[extIdx] != 0) {
                h = OpenHomeNX::loadPkmFromGen(bytes, kGenForExt[extIdx]);
                foundGen = kGenForExt[extIdx];
            } else {
                for (int gi = 0; gi < 9 && !h; gi++) {
                    h = OpenHomeNX::loadPkmFromGen(bytes, kPkmGens[gi]);
                    if (h) foundGen = kPkmGens[gi];
                }
            }
            if (!h) continue; // unparseable: not listed
            info.gen = static_cast<int>(foundGen);
            info.species = OpenHomeNX::ohpkmSpecies(h);
            OpenHomeNX::freePkm(h);
            info.valid = (info.species != 0);
            out.push_back(std::move(info));
        }
        closedir(d);
    }
    return out;
}

void UI::openLearnset(const Pokemon& pkm) {
    learnset_.clear();
    learnsetSpecies_ = pkm.species();
    learnsetCursor_ = 0;
    learnsetScroll_ = 0;
    learnsetEquipped_[0] = pkm.move1();
    learnsetEquipped_[1] = pkm.move2();
    learnsetEquipped_[2] = pkm.move3();
    learnsetEquipped_[3] = pkm.move4();
    // Table by the mon's own format (preview format for bank blobs:
    // shows what it can learn HERE).
    int table = learnsetTableFor(pkm.gameType_);
    if (table != 0 && learnsetSpecies_ != 0)
        learnset_ = OpenHomeNX::getLearnset(static_cast<uint32_t>(table), learnsetSpecies_);
    showLearnset_ = true;
}

void UI::handleLearnsetInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
    }

    int count = static_cast<int>(learnset_.size());
    if (count == 0) {
        // Only B to close
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            showLearnset_ = false;
        return;
    }

    auto scrollIntoView = [&]() {
        constexpr int ROW_H = 36;
        int visibleRows = (550 - 40 - 50) / ROW_H; // matches popup layout
        if (learnsetCursor_ < learnsetScroll_)
            learnsetScroll_ = learnsetCursor_;
        else if (learnsetCursor_ >= learnsetScroll_ + visibleRows)
            learnsetScroll_ = learnsetCursor_ - visibleRows + 1;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (learnsetCursor_ > 0) learnsetCursor_--;
                else learnsetCursor_ = count - 1;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (learnsetCursor_ < count - 1) learnsetCursor_++;
                else learnsetCursor_ = 0;
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: // L = page up
                learnsetCursor_ = std::max(0, learnsetCursor_ - 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // R = page down
                learnsetCursor_ = std::min(count - 1, learnsetCursor_ + 10);
                scrollIntoView();
                break;
            case SDL_CONTROLLER_BUTTON_B: // Switch A = close
            case SDL_CONTROLLER_BUTTON_A: // Switch B = close
                showLearnset_ = false;
                break;
        }
    }
}

bool UI::importPkFile(const PkFileInfo& info, bool closeAfter) {
    if (!info.valid) {
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), info.filename);
        return false;
    }
    if (!bank_.isCrossGen()) {
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), i18n::get(StrKey::ImportPkNeedBank));
        return false;
    }
    std::string err;
    if (!importPkFileToBank(info, err)) {
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle),
                           err.empty() ? i18n::get(StrKey::CouldNotWrite) : err);
        return false;
    }
    if (closeAfter) {
        showPkImportList_ = false;
        showMessageAndWait(i18n::get(StrKey::ImportPkTitle), info.filename);
    }
    return true;
}

// Silent core: reads the whole file, parses via FFI, drops the blob in the
// first free bank slot. No dialogs; reason goes to err (empty = generic).
bool UI::importPkFileToBank(const PkFileInfo& info, std::string& err) {
    err.clear();
    FILE* f = std::fopen(info.path.c_str(), "rb");
    if (!f) {
        DebugLog::line("import pk: %s -> fopen fallita", info.path.c_str());
        return false;
    }
    // Whole file (cap 376B): native exports are party-size, cross-gen box-size.
    std::vector<uint8_t> bytes(376);
    size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got == 0 || got > 376) {
        DebugLog::line("import pk: %s -> %zu byte illeggibili", info.path.c_str(), got);
        return false;
    }
    bytes.resize(got);
    PkmHandle* h = OpenHomeNX::loadPkmFromGen(bytes, static_cast<uint32_t>(info.gen));
    if (!h) {
        DebugLog::line("import pk: %s -> parse gen%d fallito", info.path.c_str(), info.gen);
        return false;
    }
    std::vector<uint8_t> blob = OpenHomeNX::getOhpkmBytes(h);
    uint16_t sp = OpenHomeNX::ohpkmSpecies(h);
    OpenHomeNX::freePkm(h);
    if (blob.empty()) {
        DebugLog::line("import pk: %s -> blob vuoto", info.path.c_str());
        return false;
    }
    for (int b = 0; b < bank_.boxCount(); b++) {
        for (int s = 0; s < bank_.slotsPerBox(); s++) {
            if (bank_.ohpkmAt(b, s).empty()) {
                bank_.setOhpkmAt(b, s, blob);
                markDirty();
                invalidateSlotDisplay(Panel::Bank, b);
                DebugLog::line("import pk: %s -> specie %u in banca %d:%d", info.path.c_str(), sp, b, s);
                return true;
            }
        }
    }
    DebugLog::line("import pk: %s -> banca piena", info.path.c_str());
    err = i18n::get(StrKey::CouldNotWrite);
    return false;
}

void UI::importSelectedPkFiles(bool closeAfter) {
    int ok = 0, fail = 0;
    for (auto& info : pkImportList_) {
        if (!info.selected || info.imported)
            continue;
        std::string err;
        if (importPkFileToBank(info, err)) {
            info.imported = true;
            info.selected = false;
            ok++;
        } else {
            fail++;
        }
    }
    markDirty();
    if (ok == 0 && fail == 0)
        return; // nothing selected: stay silent, stay open
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d ok%s", ok, fail > 0 ? " (alcuni falliti, vedi log)" : "");
    if (closeAfter)
        showPkImportList_ = false;
    showMessageAndWait(i18n::get(StrKey::ImportPkTitle), buf);
}

std::string UI::exportCrossGenBlob(const Pokemon& pkm) {
    // A = Gen 1 (.pk1), B = Gen 2 (.pk2).
    const bool gen1 = showConfirmDialog(i18n::get(StrKey::ExportGenTitle),
                                        i18n::get(StrKey::ExportGenBody));
    const uint32_t gen = gen1 ? 1 : 2;
    const char* ext = gen1 ? "pk1" : "pk2";
    const char* genStr = gen1 ? "1" : "2";

    PkmHandle* h = OpenHomeNX::loadOhpkm(pkm.ohpkmBlob_);
    if (!h) {
        DebugLog::line("export blob: loadOhpkm failed (%zu B)", pkm.ohpkmBlob_.size());
        return "";
    }

    // Dex-cut PRIMA del warning mosse + drop warning BEFORE converting
    // (same policy as native drops).
    uint16_t sp = OpenHomeNX::ohpkmSpecies(h);
    std::string dispName = OpenHomeNX::ohpkmNickname(h);
    if (dispName.empty()) dispName = SpeciesName::get(sp);
    if (OpenHomeNX::speciesLegalInGen(h, gen) == 0) {
        showMessageAndWait(i18n::get(StrKey::TransferTitle),
            i18n::fmt(StrKey::TransferNotInDex, dispName, std::string("Gen ") + genStr));
        OpenHomeNX::freePkm(h);
        return "";
    }
    uint32_t dropped = OpenHomeNX::countMovesNotInGen(h, gen);
    if (dropped != UINT32_MAX && dropped > 0 &&
        !showConfirmDialog(i18n::get(StrKey::Gen1DropsTitle),
            i18n::fmt(StrKey::Gen1DropsBody, dispName, std::to_string(dropped), genStr))) {
        OpenHomeNX::freePkm(h);
        return ""; // B: cancel, nothing written
    }

    PkmHandle* out = PokemonFFI::transfer(h, gen);
    OpenHomeNX::freePkm(h);
    if (!out) {
        // Usually dex-cut (species beyond 151/251): logged with the species
        // so the log tells it apart from corrupt data.
        DebugLog::line("export blob: transfer to gen%u failed (species %u)", gen, sp);
        return "";
    }
    std::vector<uint8_t> bytes = OpenHomeNX::getPkmBoxBytesForGen(out, gen);
    sp = OpenHomeNX::ohpkmSpecies(out);
    uint16_t fm = OpenHomeNX::ohpkmForm(out);
    std::string nick = OpenHomeNX::ohpkmNickname(out);
    if (nick.empty()) nick = SpeciesName::get(sp);
    PokemonFFI::free(out);
    if (bytes.empty()) return "";

    char buf[512];
    if (fm != 0)
        std::snprintf(buf, sizeof(buf), "GEN%s - %04u-%u - %s.%s", genStr, sp, fm, nick.c_str(), ext);
    else
        std::snprintf(buf, sizeof(buf), "GEN%s - %04u - %s.%s", genStr, sp, nick.c_str(), ext);
    std::string filename = buf;
    for (char& c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }

    std::string dir = basePath_ + "export/CrossGen/";
    mkdir(dir.c_str(), 0755);
    std::string fullPath = dir + filename;
    FILE* f = std::fopen(fullPath.c_str(), "wb");
    if (!f) return "";
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return filename;
}

std::string UI::exportPokemon(const Pokemon& pkm) {
    if (pkm.isEmpty()) return "";

    // Cross-gen bank mon: the blob is the truth, the preview bytes are not
    // exportable as-is — materialize the chosen Gen1/Gen2 record instead.
    if (!pkm.ohpkmBlob_.empty())
        return exportCrossGenBlob(pkm);

    // Build export directory: basePath/export/{bankFolder}/
    std::string dir = basePath_ + "export/";
    std::string gameDir = dir + bankFolderNameOf(selectedGame_) + "/";

    // PKHeX naming: {species:0000} - {form} - {flags} - {name} - {checksum:X4}{EC:X8}.{ext}
    char buf[512];
    uint16_t sp = pkm.species();
    uint8_t fm = pkm.form();
    const char* ext = pkFileExtension(selectedGame_);

    std::string formStr;
    if (fm != 0) {
        const char* formName = getFormName(sp, fm);
        if (formName)
            formStr = std::string(" - ") + formName;
        else {
            char fb[16];
            std::snprintf(fb, sizeof(fb), " - %02u", fm);
            formStr = fb;
        }
    }

    std::string tags;
    {
        std::string flags;
        if (pkm.isShiny()) flags += "S";
        if (pkm.isAlpha()) flags += "A";
        if (pkm.isEgg())   flags += "E";
        if (!flags.empty()) tags = " - [" + flags + "]";
    }
    std::string nick = SpeciesName::get(sp);

    // Checksum at 0x06 for modern, 0x1C for PK3
    uint16_t chk = (isFRLG(selectedGame_) || isImportedFile(selectedGame_)) ? pkm.readU16(0x1C) : pkm.readU16(0x06);
    uint32_t ec = pkm.encryptionConstant();

    // Short game tag
    const char* gameTag = gameInfo(selectedGame_).gameTag;

    std::snprintf(buf, sizeof(buf), "%s - %04u%s%s - %s - %04X%08X.%s",
                  gameTag, sp, formStr.c_str(), tags.c_str(), nick.c_str(), chk, ec, ext);

    // Sanitize filename: replace filesystem-unsafe chars
    std::string filename = buf;
    for (char& c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }

    std::string fullPath = gameDir + filename;

    // Create directories only when we're about to write
    mkdir(dir.c_str(), 0755);
    mkdir(gameDir.c_str(), 0755);

    // Write decrypted party-size data
    int size = pkPartySize(selectedGame_);
    FILE* f = std::fopen(fullPath.c_str(), "wb");
    if (!f) return "";
    std::fwrite(pkm.data.data(), 1, size, f);
    std::fclose(f);

    return filename;
}
