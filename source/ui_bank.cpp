#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sys/statvfs.h>

#include <switch.h>

// --- Bank Selector ---

void UI::drawBankSelectorFrame() {
    SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
    SDL_RenderClear(renderer_);

    // Show split view: bank selector on one side, other panel visible
    // - Normal mode: save on left, selector on right (when save is loaded)
    // - Applet/All-banks mode: other bank on opposite side (when EITHER bank
    //   is loaded — the second pick must stay split, not go fullscreen)
    bool splitView = isDualBankMode() ? (!activeBankName_.empty() || !leftBankName_.empty())
                                      : save_.isLoaded();

    // Determine selector area
    int selCenterX = SCREEN_W / 2;
    int selAreaX = 0;
    if (splitView) {
        if (isDualBankMode() && bankSelTarget_ == Panel::Game) {
            // Dual mode: selector on left, keep right bank visible
            selCenterX = PANEL_X_L + PANEL_W / 2;
            selAreaX = PANEL_X_L;
            auto truncName = [](const std::string& s, size_t max) -> std::string {
                if (s.size() <= max) return s;
                std::string t = s.substr(0, max - 3);
                t += "(..)";
                return t;
            };
            if (!activeBankName_.empty()) {
                std::string rightBoxName = truncName(activeBankName_, 16) + " - " + bank_.getBoxName(bankBox_);
                drawPanel(PANEL_X_R, rightBoxName, bankBox_, bank_.boxCount(),
                          false, nullptr, &bank_, bankBox_, Panel::Bank);
            } else {
                drawPanel(PANEL_X_R, i18n::get(StrKey::NoBankLoaded), 0, 1,
                          false, nullptr, nullptr, 0, Panel::Bank);
            }
        } else {
            // Normal mode or dual switching right bank: selector on right
            selCenterX = PANEL_X_R + PANEL_W / 2;
            selAreaX = PANEL_X_R;
            auto truncName = [](const std::string& s, size_t max) -> std::string {
                if (s.size() <= max) return s;
                std::string t = s.substr(0, max - 3);
                t += "(..)";
                return t;
            };
            // Draw left panel (save or left bank)
            if (isDualBankMode()) {
                if (!leftBankName_.empty()) {
                    std::string leftBoxName = truncName(leftBankName_, 16) + " - " + bankLeft_.getBoxName(gameBox_);
                    drawPanel(PANEL_X_L, leftBoxName, gameBox_, bankLeft_.boxCount(),
                              false, nullptr, &bankLeft_, gameBox_, Panel::Game);
                } else {
                    drawPanel(PANEL_X_L, i18n::get(StrKey::NoBankLoaded), 0, 1,
                              false, nullptr, nullptr, 0, Panel::Game);
                }
            } else {
                std::string gameBoxName = save_.getBoxName(gameBox_);
                drawPanel(PANEL_X_L, gameBoxName, gameBox_, save_.boxCount(),
                          false, &save_, nullptr, gameBox_, Panel::Game);
            }
        }
    }

    // Title (la lista grouped dipende dal manager, non dal flag UI dual)
    if (allBanksMode_ || bankManager_.isAllMode()) {
        if (activeBankName_.empty())
            drawTextCentered(i18n::get(StrKey::AllBanks), selCenterX,
                             splitView ? 25 : 40, T().text, font_);
        else {
            std::string side = (bankSelTarget_ == Panel::Game) ? i18n::get(StrKey::Left) : i18n::get(StrKey::Right);
            drawTextCentered(i18n::fmt(StrKey::SelectSideBank, side),
                             selCenterX, 25, T().text, font_);
        }
    } else if (appletMode_) {
        std::string side = (bankSelTarget_ == Panel::Game) ? i18n::get(StrKey::Left) : i18n::get(StrKey::Right);
        drawTextCentered(i18n::fmt(StrKey::SelectSideBank, side),
                         selCenterX, 25, T().text, font_);
    } else {
        drawTextCentered(i18n::get(StrKey::SelectBank), selCenterX,
                         splitView ? 25 : 40, T().text, font_);
    }

    const auto& banks = bankManager_.list();

    if (banks.empty()) {
        drawTextCentered(i18n::get(StrKey::NoBanksFound),
                         selCenterX, SCREEN_H / 2 - 10, T().textDim, font_);
        drawTextCentered(i18n::get(StrKey::PressXCreate),
                         selCenterX, SCREEN_H / 2 + 15, T().textDim, fontSmall_);
    } else if (bankManager_.isAllMode()) {
        // All-banks mode: grouped list with game headers
        int LIST_W = splitView ? (PANEL_W - 30) : 800;
        int LIST_X = splitView ? (selAreaX + 15) : (SCREEN_W - LIST_W) / 2;
        int LIST_Y = splitView ? 50 : 80;
        int LIST_BOTTOM = 580;
        int ROW_H = 50;
        int HDR_H = 32;
        int visiblePixels = LIST_BOTTOM - LIST_Y;

        // Build visual row list: headers interleaved with bank entries
        struct VisualRow { bool isHeader; int bankIdx; GameType game; bool crossGen; };
        std::vector<VisualRow> vrows;
        for (int i = 0; i < (int)banks.size(); i++) {
            if (i == 0 || !bankSameSection(banks[i], banks[i - 1]))
                vrows.push_back({true, -1, banks[i].game, banks[i].crossGen});
            vrows.push_back({false, i, banks[i].game, banks[i].crossGen});
        }

        // Find visual row of cursor
        int cursorVisRow = 0;
        for (int r = 0; r < (int)vrows.size(); r++) {
            if (!vrows[r].isHeader && vrows[r].bankIdx == bankSelCursor_) {
                cursorVisRow = r;
                break;
            }
        }

        // Pre-compute pixel Y positions for all visual rows
        std::vector<int> rowYPositions(vrows.size() + 1);
        rowYPositions[0] = 0;
        for (int i = 0; i < (int)vrows.size(); i++)
            rowYPositions[i + 1] = rowYPositions[i] + (vrows[i].isHeader ? HDR_H : ROW_H);
        int totalPixels = rowYPositions.back();

        // Clamp scroll to keep cursor visible
        int cursorY = rowYPositions[cursorVisRow];
        int cursorH = ROW_H;
        if (bankSelScroll_ > cursorY)
            bankSelScroll_ = cursorY;
        if (cursorY + cursorH > bankSelScroll_ + visiblePixels)
            bankSelScroll_ = cursorY + cursorH - visiblePixels;

        // If cursor's group header is just above and close, scroll up to show it
        if (cursorVisRow > 0 && vrows[cursorVisRow - 1].isHeader) {
            int hdrY = rowYPositions[cursorVisRow - 1];
            if (hdrY >= bankSelScroll_ - HDR_H && hdrY < bankSelScroll_)
                bankSelScroll_ = hdrY;
        }

        if (bankSelScroll_ > totalPixels - visiblePixels)
            bankSelScroll_ = std::max(0, totalPixels - visiblePixels);
        if (bankSelScroll_ < 0) bankSelScroll_ = 0;

        // Scroll arrows
        if (bankSelScroll_ > 0)
            drawTextCentered("^", selCenterX, LIST_Y - 12, T().arrow, font_);
        if (bankSelScroll_ + visiblePixels < totalPixels)
            drawTextCentered("v", selCenterX, LIST_BOTTOM + 4, T().arrow, font_);

        // Render visible rows
        for (int r = 0; r < (int)vrows.size(); r++) {
            int rowY = rowYPositions[r] - bankSelScroll_ + LIST_Y;
            int rowH = vrows[r].isHeader ? HDR_H : ROW_H;

            if (rowY + rowH <= LIST_Y) continue;  // above visible area
            if (rowY >= LIST_BOTTOM) break;        // below visible area

            if (vrows[r].isHeader) {
                // Group header: "Cross-gen" or the game name, with separator line
                std::string gameName = vrows[r].crossGen ? "Cross-gen"
                                                         : bankGroupNameOf(vrows[r].game);
                drawText(gameName, LIST_X + 10, rowY + HDR_H / 2 - 7,
                         T().text, fontSmall_);
                // Separator line after the text
                int textW = getTextEntry(gameName, fontSmall_, T().text).w;
                drawRect(LIST_X + 10 + textW + 10, rowY + HDR_H / 2,
                         LIST_W - 30 - textW, 1, T().text);
            } else {
                int idx = vrows[r].bankIdx;

                // Highlighted row
                if (idx == bankSelCursor_) {
                    drawRect(LIST_X, rowY, LIST_W, ROW_H - 4, T().menuHighlight);
                    drawRectOutline(LIST_X, rowY, LIST_W, ROW_H - 4, T().cursor, 2);
                }

                // Bank name (left-aligned, indented under header) — dimmed if invalid
                drawText(banks[idx].name, LIST_X + 30, rowY + (ROW_H - 4) / 2 - 9,
                         banks[idx].valid ? T().text : T().textDim, font_);

                // Right-aligned: slot count, or invalid marker for stray files
                if (!banks[idx].valid) {
                    std::string invStr = i18n::get(StrKey::InvalidBankFile);
                    const auto& se = getTextEntry(invStr, font_, T().red);
                    if (se.tex)
                        drawText(invStr, LIST_X + LIST_W - 20 - se.w,
                                 rowY + (ROW_H - 4) / 2 - 9, T().red, font_);
                } else {
                    int maxSlots = isLGPE(banks[idx].game) ? 1000 :
                                   isBDSP(banks[idx].game) ? 1200 : 960;
                    std::string slotStr = std::to_string(banks[idx].occupiedSlots) +
                                          "/" + std::to_string(maxSlots);
                    const auto& se = getTextEntry(slotStr, font_, T().textDim);
                    if (se.tex)
                        drawText(slotStr, LIST_X + LIST_W - 20 - se.w,
                                 rowY + (ROW_H - 4) / 2 - 9, T().textDim, font_);
                }
            }
        }
    } else {
        // Normal mode: flat bank list
        int LIST_W = splitView ? (PANEL_W - 30) : 800;
        int LIST_X = splitView ? (selAreaX + 15) : (SCREEN_W - LIST_W) / 2;
        int LIST_Y = splitView ? 50 : 80;
        int LIST_BOTTOM = 580;
        int ROW_H = 50;
        int visibleRows = (LIST_BOTTOM - LIST_Y) / ROW_H;

        // Clamp scroll
        int maxScroll = std::max(0, (int)banks.size() - visibleRows);
        if (bankSelScroll_ > maxScroll) bankSelScroll_ = maxScroll;
        if (bankSelScroll_ < 0) bankSelScroll_ = 0;

        // Scroll arrows
        if (bankSelScroll_ > 0) {
            drawTextCentered("^", selCenterX, LIST_Y - 12, T().arrow, font_);
        }
        if (bankSelScroll_ + visibleRows < (int)banks.size()) {
            drawTextCentered("v", selCenterX, LIST_BOTTOM + 4, T().arrow, font_);
        }

        for (int i = 0; i < visibleRows && (bankSelScroll_ + i) < (int)banks.size(); i++) {
            int idx = bankSelScroll_ + i;
            int rowY = LIST_Y + i * ROW_H;

            // Highlighted row
            if (idx == bankSelCursor_) {
                drawRect(LIST_X, rowY, LIST_W, ROW_H - 4, T().menuHighlight);
                drawRectOutline(LIST_X, rowY, LIST_W, ROW_H - 4, T().cursor, 2);
            }

            drawText(banks[idx].name, LIST_X + 20, rowY + (ROW_H - 4) / 2 - 9,
                     banks[idx].valid ? T().text : T().textDim, font_);

            // Right-aligned: slot count, or invalid marker for stray files
            if (!banks[idx].valid) {
                std::string invStr = i18n::get(StrKey::InvalidBankFile);
                const auto& se = getTextEntry(invStr, font_, T().red);
                if (se.tex) drawText(invStr, LIST_X + LIST_W - 20 - se.w,
                         rowY + (ROW_H - 4) / 2 - 9, T().red, font_);
            } else {
                int maxSlots = isLGPE(selectedGame_) ? 1000 : isBDSP(selectedGame_) ? 1200 : 960;
                std::string slotStr = std::to_string(banks[idx].occupiedSlots) + "/" + std::to_string(maxSlots);
                const auto& se = getTextEntry(slotStr, font_, T().textDim);
                if (se.tex) drawText(slotStr, LIST_X + LIST_W - 20 - se.w, rowY + (ROW_H - 4) / 2 - 9,
                         T().textDim, font_);
            }
        }
    }

    // Status bar
    if (bankRightCrossGen_)
        drawStatusBar("A: Open   X: New bank   Y: Rename   -: Delete   B: Back   +: About");
    else if (bankManager_.isAllMode())
        drawStatusBar(i18n::get(StrKey::StatusBankAll));
    else
        drawStatusBar(i18n::get(StrKey::StatusBankNormal));

    // Profile | Game name | Core (bottom right, gold)
    {
        std::string label;
        if (allBanksMode_)
            label = activeBankName_.empty() ? "All Banks" :
                    "All Banks | " + std::string(gameDisplayNameOf(selectedGame_));
        else if (appletMode_)
            label = "Dual Bank | " + std::string(gameDisplayNameOf(selectedGame_));
        else {
            if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
                label = account_.profiles()[selectedProfile_].nickname + " | ";
            label += gameDisplayNameOf(selectedGame_);
        }
        label += " | ";
        label += useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            label += " | DBG";
        const auto& e = getTextEntry(label, fontSmall_, T().goldLabel);
        if (e.tex) drawText(label, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }

    // Delete confirmation overlay
    if (showDeleteConfirm_) {
        drawDeleteConfirmPopup();
    }

}

void UI::handleBankSelectorInput(bool& running) {
    const auto& banks = bankManager_.list();
    int bankCount = (int)banks.size();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return;
        }

        if (event.type == SDL_CONTROLLERBUTTONDOWN)
            markDirty();

        // Delete confirmation takes priority
        if (showDeleteConfirm_) {
            handleDeleteConfirmEvent(event);
            continue;
        }

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
                    if (bankCount > 0) {
                        int prev = bankSelCursor_;
                        bankSelCursor_ = (bankSelCursor_ - 1 + bankCount) % bankCount;
                        if (bankManager_.isAllMode()) {
                            if (bankSelCursor_ > prev)
                                bankSelScroll_ = bankManager_.totalVisualRows() * 50; // wrap to bottom, draw clamps
                        } else {
                            if (bankSelCursor_ > prev) {
                                int visibleRows = (580 - 80) / 50;
                                bankSelScroll_ = std::max(0, bankSelCursor_ - visibleRows + 1);
                            } else if (bankSelCursor_ < bankSelScroll_) {
                                bankSelScroll_ = bankSelCursor_;
                            }
                        }
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    if (bankCount > 0) {
                        int prev = bankSelCursor_;
                        bankSelCursor_ = (bankSelCursor_ + 1) % bankCount;
                        if (bankManager_.isAllMode()) {
                            if (bankSelCursor_ < prev)
                                bankSelScroll_ = 0; // wrap to top
                        } else {
                            if (bankSelCursor_ < prev) {
                                bankSelScroll_ = 0;
                            } else {
                                int visibleRows = (580 - 80) / 50;
                                if (bankSelCursor_ >= bankSelScroll_ + visibleRows)
                                    bankSelScroll_ = bankSelCursor_ - visibleRows + 1;
                            }
                        }
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_B: // Switch A = open
                    if (bankCount > 0)
                        openSelectedBank();
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                    DebugLog::line("bank: B in selector activeBank=%d allMode=%d",
                        !activeBankName_.empty(), (int)allBanksMode_);
                    if (!activeBankName_.empty()) {
                        // B in banca torna sempre a TUTTE le banche con header per-gioco.
                        activeBankName_.clear();
                        activeBankPath_.clear();
                        bankSelTarget_ = Panel::Bank;
                        bankManager_.initAll(basePath_);
                        allBanksMode_ = true;
                        bankRightCrossGen_ = false;
                        bankSelCursor_ = 0;
                        bankSelScroll_ = 0;
                        screen_ = AppScreen::BankSelector;
                        return;
                    } else {
                        if (!allBanksMode_) {
                            account_.unmountSave();
                            int cnt = BankManager::countBanks(basePath_, selectedGame_);
                            gameBankCounts_[selectedGame_] = cnt;
                            gameBankCounts_[pairedGame(selectedGame_)] = cnt;
                        }
                        allBanksMode_ = false;
                        screen_ = AppScreen::GameSelector;
                    }
                    // Cancelled the cross-gen selector: restore single-game list.
                    if (bankRightCrossGen_) {
                        bankRightCrossGen_ = false;
                        bankManager_.init(basePath_, selectedGame_);
                    }
                    return;
                case SDL_CONTROLLER_BUTTON_Y: { // Switch X = new
                    // A = cross-gen, Y = solo questo gioco, B = annulla per
                    // davvero (prima B sceglieva "banca normale" invece di
                    // uscire senza fare nulla, come B fa ovunque altrove).
                    if (!bankManager_.isAllMode()) {
                        int kind = pickNewBankKind(i18n::get(StrKey::CreateBankTitle),
                            i18n::get(StrKey::CreateBankBody));
                        if (kind >= 0) {
                            newBankCrossGen_ = (kind == 0);
                            beginTextInput(TextInputPurpose::CreateBank);
                        }
                    } else if (bankRightCrossGen_) {
                        // New bank is created for the currently loaded game;
                        // drop back to its single-game list first. Restored
                        // after la creazione in UI::commitTextInput() --
                        // altrimenti la vista restava sul singolo gioco
                        // finche' non si usciva e rientrava.
                        bankRightCrossGen_ = false;
                        bankCreateWasCrossGenView_ = true;
                        bankManager_.init(basePath_, selectedGame_);
                        int kind = pickNewBankKind(i18n::get(StrKey::CreateBankTitle),
                            i18n::get(StrKey::CreateBankBody));
                        if (kind >= 0) {
                            newBankCrossGen_ = (kind == 0);
                            beginTextInput(TextInputPurpose::CreateBank);
                        } else {
                            // Annullato: nessun commitTextInput arrivera' mai
                            // a consumare bankCreateWasCrossGenView_, quindi
                            // il ripristino va fatto subito qui.
                            bankCreateWasCrossGenView_ = false;
                            bankRightCrossGen_ = true;
                            bankManager_.initAll(basePath_);
                        }
                        return;
                    }
                    break;
                }
                case SDL_CONTROLLER_BUTTON_X: // Switch Y = rename / theme
                    if (bankManager_.isAllMode()) {
                        showThemeSelector_ = true;
                        themeSelCursor_ = themeIndex_;
                        themeSelOriginal_ = themeIndex_;
                    } else if (bankCount > 0) {
                        beginTextInput(TextInputPurpose::RenameBank);
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_BACK: // - = delete (era +: "-" si legge
                    // piu' naturalmente come "togli/elimina" di "+" che
                    // suggerisce "aggiungi" -- scambiati su richiesta.
                    // L'hint "-: Delete" compare anche nella vista cross-gen
                    // di default (bankRightCrossGen_, box a sinistra + lista
                    // banche a destra) ma il controllo bloccava sempre lì
                    // perche' quella vista usa bankManager_.initAll() ->
                    // isAllMode()==true. deleteBank() lavora per fullPath
                    // per-voce e non dipende da banksDir_, quindi funziona
                    // gia' bene in questa modalita': va permesso. Resta
                    // bloccato solo nel picker esplicito "Tutte le banche"
                    // (allBanksMode_ senza bankRightCrossGen_), il cui hint
                    // infatti non promette Delete.
                    if (bankCount > 0 && (bankRightCrossGen_ || !bankManager_.isAllMode()))
                        showDeleteConfirm_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_START: // + = about (era -)
                    showAbout_ = true;
                    break;
            }
        }

    }

    // Joystick repeat navigation
    if ((stickDirY_ != 0) && bankCount > 0 && !showDeleteConfirm_) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            if (stickDirY_ < 0) {
                int prev = bankSelCursor_;
                bankSelCursor_ = (bankSelCursor_ - 1 + bankCount) % bankCount;
                if (bankManager_.isAllMode()) {
                    if (bankSelCursor_ > prev)
                        bankSelScroll_ = bankManager_.totalVisualRows() * 50;
                } else {
                    if (bankSelCursor_ > prev) {
                        int visibleRows = (580 - 80) / 50;
                        bankSelScroll_ = std::max(0, bankSelCursor_ - visibleRows + 1);
                    } else if (bankSelCursor_ < bankSelScroll_) {
                        bankSelScroll_ = bankSelCursor_;
                    }
                }
            } else {
                int prev = bankSelCursor_;
                bankSelCursor_ = (bankSelCursor_ + 1) % bankCount;
                if (bankManager_.isAllMode()) {
                    if (bankSelCursor_ < prev)
                        bankSelScroll_ = 0;
                } else {
                    if (bankSelCursor_ < prev) {
                        bankSelScroll_ = 0;
                    } else {
                        int visibleRows = (580 - 80) / 50;
                        if (bankSelCursor_ >= bankSelScroll_ + visibleRows)
                            bankSelScroll_ = bankSelCursor_ - visibleRows + 1;
                    }
                }
            }
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    }
}

void UI::openSelectedBank() {
    const auto& banks = bankManager_.list();
    if (bankSelCursor_ < 0 || bankSelCursor_ >= (int)banks.size())
        return;

    // Stray .bin files that aren't real bank files can't be opened
    if (!banks[bankSelCursor_].valid)
        return;

    // (Cross-gen + Let's Go used to be refused here: the box grid was global.
    // gridColsFor(Panel) now sizes each panel from its own source, so a
    // 25-slot Let's Go save renders fine next to the 30-slot cross-gen bank.)

    const std::string& name = banks[bankSelCursor_].name;

    // Prevent loading same bank on both sides in dual mode
    if (isDualBankMode()) {
        if (bankSelTarget_ == Panel::Game && name == activeBankName_
            && banks[bankSelCursor_].game == selectedGame_) {
            showMessageAndWait(i18n::get(StrKey::AlreadyOpen),
                i18n::get(StrKey::BankAlreadyRight));
            return;
        }
        if (bankSelTarget_ == Panel::Bank && name == leftBankName_
            && banks[bankSelCursor_].game == selectedGame_) {
            showMessageAndWait(i18n::get(StrKey::AlreadyOpen),
                i18n::get(StrKey::BankAlreadyLeft));
            return;
        }
    }

    // In all-banks mode, update UI game from the selected bank
    if (allBanksMode_ && bankSelTarget_ == Panel::Bank) {
        selectedGame_ = banks[bankSelCursor_].game;
        invalidateAllSlotDisplays();
    }

    // No fullscreen modal when opening from an already-composed split view
    // (save left + list right): it would wipe the screen for a fraction of a
    // second. Keep it for fullscreen transitions (game -> grid).
    bool fromSplit = isDualBankMode() ? (!activeBankName_.empty() || !leftBankName_.empty())
                                      : save_.isLoaded();
    if (!fromSplit)
        showWorking(i18n::get(StrKey::LoadingBank));

    if (isDualBankMode() && bankSelTarget_ == Panel::Game) {
        leftBankPath_ = bankManager_.loadBank(name, bankLeft_);
        bankLeft_.setGameType(banks[bankSelCursor_].game);
        leftBankName_ = name;
    } else {
        activeBankPath_ = bankManager_.loadBank(name, bank_);
        bank_.setGameType(banks[bankSelCursor_].game);
        activeBankName_ = name;
    }

    // In dual mode, after loading the right bank, chain to left bank selector
    // (only if there's at least one other bank to choose from). The list stays
    // on ALL banks for the second pick too: narrowing to one game hid the other
    // banks (and single-bank folders skipped dual entirely, landing on MainView
    // with an empty left side).
    if (isDualBankMode() && bankSelTarget_ == Panel::Bank && leftBankName_.empty()) {
        DebugLog::line("bank: dual chain right->left, list=%d", (int)bankManager_.list().size());
        if ((int)bankManager_.list().size() > 1) {
            bankSelTarget_ = Panel::Game;
            bankSelCursor_ = 0;
            bankSelScroll_ = 0;
            return;  // stay on BankSelector screen
        }
    }

    // Cross-gen selection done: put the manager back to the save's game so
    // create / rename / the next selector open behave normally. `bank_` keeps
    // its own gameType() (set above) so the right panel stays cross-gen.
    if (bankRightCrossGen_) {
        bankRightCrossGen_ = false;
        bankManager_.init(basePath_, selectedGame_);
    }

    screen_ = AppScreen::MainView;
    invalidateAllSlotDisplays();

    // Reset main view state
    cursor_ = Cursor{};
    cursor_.panel = bankSelTarget_;
    gameBox_ = 0;
    bankBox_ = 0;
    showDetail_ = false;
    showMenu_ = false;
    holding_ = false;
    heldPkm_ = Pokemon{};
    selectedSlots_.clear();
    heldMulti_.clear();
    heldMultiSlots_.clear();
}

// --- Delete Confirmation ---

void UI::drawDeleteConfirmPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 500;
    constexpr int POP_H = 180;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().red, 2);

    const auto& banks = bankManager_.list();
    std::string bankName = (bankSelCursor_ >= 0 && bankSelCursor_ < (int)banks.size())
        ? banks[bankSelCursor_].name : "";

    drawTextCentered(i18n::fmt(StrKey::DeleteBankConfirm, bankName),
                     popX + POP_W / 2, popY + 50, T().text, font_);
    drawTextCentered(i18n::get(StrKey::CannotUndo),
                     popX + POP_W / 2, popY + 85, T().red, fontSmall_);
    drawTextCentered(i18n::get(StrKey::AConfirmBCancel),
                     popX + POP_W / 2, popY + POP_H - 25, T().textDim, fontSmall_);
}

void UI::handleDeleteConfirmEvent(const SDL_Event& event) {
    auto tryDelete = [&]() {
        const auto& banks = bankManager_.list();
        if (bankSelCursor_ < 0 || bankSelCursor_ >= (int)banks.size())
            return;
        const std::string& name = banks[bankSelCursor_].name;
        // Cannot delete a bank that is currently loaded
        if (name == activeBankName_ || (isDualBankMode() && name == leftBankName_)) {
            showDeleteConfirm_ = false;
            showMessageAndWait(i18n::get(StrKey::CannotDelete), i18n::get(StrKey::BankCurrentlyLoaded));
            return;
        }
        // Same split-view rule as openSelectedBank: no fullscreen flash when
        // the save is already composed on the left panel.
        if (!(isDualBankMode() ? (!activeBankName_.empty() || !leftBankName_.empty()) : save_.isLoaded()))
            showWorking(i18n::get(StrKey::DeletingBank));
        bankManager_.deleteBank(name);
        int newCount = (int)bankManager_.list().size();
        if (bankSelCursor_ >= newCount && newCount > 0)
            bankSelCursor_ = newCount - 1;
        showDeleteConfirm_ = false;
    };

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm
                tryDelete();
                break;
            case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                showDeleteConfirm_ = false;
                break;
        }
    }
}

// --- Text Input ---

void UI::beginTextInput(TextInputPurpose purpose) {
    textInputPurpose_ = purpose;
    textInputBuffer_.clear();
    textInputCursorPos_ = 0;

    if (purpose == TextInputPurpose::RenameBank) {
        const auto& banks = bankManager_.list();
        if (bankSelCursor_ >= 0 && bankSelCursor_ < (int)banks.size()) {
            renamingBankName_ = banks[bankSelCursor_].name;
            textInputBuffer_ = renamingBankName_;
            textInputCursorPos_ = (int)textInputBuffer_.size();
        }
    } else if (purpose == TextInputPurpose::RenameBoxName) {
        if (renamingBoxBank_) {
            textInputBuffer_ = renamingBoxBank_->getBoxName(renamingBoxIdx_);
            textInputCursorPos_ = (int)textInputBuffer_.size();
        }
    } else if (purpose == TextInputPurpose::SearchSpecies) {
        textInputBuffer_ = searchFilter_.speciesName;
        textInputCursorPos_ = (int)textInputBuffer_.size();
    } else if (purpose == TextInputPurpose::SearchOT) {
        textInputBuffer_ = searchFilter_.otName;
        textInputCursorPos_ = (int)textInputBuffer_.size();
    } else if (purpose == TextInputPurpose::SearchLevelMin) {
        if (searchFilter_.levelMin > 0)
            textInputBuffer_ = std::to_string(searchFilter_.levelMin);
        textInputCursorPos_ = (int)textInputBuffer_.size();
    } else if (purpose == TextInputPurpose::SearchLevelMax) {
        if (searchFilter_.levelMax > 0)
            textInputBuffer_ = std::to_string(searchFilter_.levelMax);
        textInputCursorPos_ = (int)textInputBuffer_.size();
    }

    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetStringLenMax(&kbd, 32);
    if (purpose == TextInputPurpose::CreateBank)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::EnterBankName).c_str());
    else if (purpose == TextInputPurpose::RenameBank)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::RenameBank).c_str());
    else if (purpose == TextInputPurpose::RenameBoxName) {
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::RenameBox).c_str());
        swkbdConfigSetStringLenMax(&kbd, 16);
    } else if (purpose == TextInputPurpose::SearchSpecies)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::SpeciesNameInput).c_str());
    else if (purpose == TextInputPurpose::SearchOT)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::OtNameInput).c_str());
    else if (purpose == TextInputPurpose::SearchLevelMin)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::MinLevel).c_str());
    else if (purpose == TextInputPurpose::SearchLevelMax)
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::MaxLevel).c_str());
    else if (purpose == TextInputPurpose::ImportPathEntry) {
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::ImportPathInputHdr).c_str());
        swkbdConfigSetStringLenMax(&kbd, 127); // paths run longer than a bank/box name
    } else if (purpose == TextInputPurpose::EditUpdateUrl) {
        textInputBuffer_ = customUrlAny(basePath_);
        textInputCursorPos_ = (int)textInputBuffer_.size();
        swkbdConfigSetHeaderText(&kbd, i18n::get(StrKey::SetEditUrlHdr).c_str());
        swkbdConfigSetStringLenMax(&kbd, 127);
    }
#ifdef OH_LINUX
    // Build Linux (R36S): non c'e' la tastiera virtuale di Switch (swkbd).
    // Forniamo un input automatico: per creare banca un default univoco, per
    // gli altri purpose il buffer gia' preimpostato. Cosi' la creazione
    // banca funziona anche senza tastiera.
    if (purpose == TextInputPurpose::CreateBank) {
        int n = 1;
        while (bankManager_.bankExists("Banca " + std::to_string(n))) n++;
        textInputBuffer_ = "Banca " + std::to_string(n);
        textInputCursorPos_ = (int)textInputBuffer_.size();
    }
    commitTextInput(textInputBuffer_);
    return;
#endif
    if (purpose == TextInputPurpose::RenameBank && !renamingBankName_.empty())
        swkbdConfigSetInitialText(&kbd, renamingBankName_.c_str());
    else if (!textInputBuffer_.empty())
        swkbdConfigSetInitialText(&kbd, textInputBuffer_.c_str());
    if (purpose == TextInputPurpose::SearchLevelMin || purpose == TextInputPurpose::SearchLevelMax) {
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        swkbdConfigSetStringLenMax(&kbd, 3);
    }
    char result[160] = {}; // must fit the longest allowed input (import paths, 127 chars)
    Result rc = swkbdShow(&kbd, result, sizeof(result));
    swkbdClose(&kbd);
    if (R_SUCCEEDED(rc) && result[0])
        commitTextInput(result);
    else if (R_SUCCEEDED(rc))
        commitTextInput("");
    else if (bankCreateWasCrossGenView_) {
        // Tastiera di sistema annullata (B) durante "crea banca" dalla
        // vista cross-gen: commitTextInput() non arriva mai a consumare
        // il flag di ripristino, quindi va fatto qui.
        bankCreateWasCrossGenView_ = false;
        bankRightCrossGen_ = true;
        bankManager_.initAll(basePath_);
    }
}

void UI::commitTextInput(const std::string& text) {
    if (textInputPurpose_ == TextInputPurpose::CreateBank) {
        // Ripristina la vista cross-gen se "crea banca" e' partita da li'
        // (vedi bankCreateWasCrossGenView_) -- prima di qualunque return
        // anticipato qui sotto (spazio insufficiente, nome duplicato),
        // altrimenti quei casi lascerebbero comunque la vista bloccata sul
        // singolo gioco. banksDir_ resta quello giusto (settato da
        // bankManager_.init() prima di arrivare qui), initAll() non lo tocca.
        if (bankCreateWasCrossGenView_) {
            bankCreateWasCrossGenView_ = false;
            bankRightCrossGen_ = true;
            bankManager_.initAll(basePath_);
        }
        {
            Bank temp;
            if (newBankCrossGen_)
                temp.makeCrossGen();
            else
                temp.setGameType(selectedGame_);
            size_t needed = temp.fileSize();
            struct statvfs vfs;
            if (statvfs("sdmc:/", &vfs) == 0) {
                size_t freeSpace = (size_t)vfs.f_bavail * vfs.f_bsize;
                if (freeSpace < needed) {
                    showMessageAndWait(i18n::get(StrKey::NotEnoughSpace),
                        i18n::fmt(StrKey::FreeNeedSpace, formatSize(freeSpace), formatSize(needed)));
                    newBankCrossGen_ = false;
                    return;
                }
            }
        }
        // Reject duplicate names with clear feedback instead of failing silently
        if (bankManager_.bankExists(text)) {
            showMessageAndWait(i18n::get(StrKey::BankNameExists),
                i18n::get(StrKey::BankNameExistsBody));
            newBankCrossGen_ = false;
            return;
        }
        if (!(isDualBankMode() ? (!activeBankName_.empty() || !leftBankName_.empty()) : save_.isLoaded()))
            showWorking(i18n::get(StrKey::CreatingBank));
        bool cross = newBankCrossGen_;
        newBankCrossGen_ = false;
        if (bankManager_.createBank(text, cross)) {
            // Select the newly created bank
            const auto& banks = bankManager_.list();
            for (int i = 0; i < (int)banks.size(); i++) {
                if (banks[i].name == text) {
                    bankSelCursor_ = i;
                    break;
                }
            }
        }
    } else if (textInputPurpose_ == TextInputPurpose::RenameBank) {
        // Reject renaming onto another existing bank with clear feedback.
        // (An unchanged name is a harmless no-op, so don't warn in that case.)
        if (text != renamingBankName_ && bankManager_.bankExists(text)) {
            showMessageAndWait(i18n::get(StrKey::BankNameExists),
                i18n::get(StrKey::BankNameExistsBody));
            return;
        }
        if (!(isDualBankMode() ? (!activeBankName_.empty() || !leftBankName_.empty()) : save_.isLoaded()))
            showWorking(i18n::get(StrKey::RenamingBank));
        if (bankManager_.renameBank(renamingBankName_, text)) {
            // Select the renamed bank
            const auto& banks = bankManager_.list();
            for (int i = 0; i < (int)banks.size(); i++) {
                if (banks[i].name == text) {
                    bankSelCursor_ = i;
                    break;
                }
            }
        }
    } else if (textInputPurpose_ == TextInputPurpose::RenameBoxName) {
        if (renamingBoxBank_ && !text.empty())
            renamingBoxBank_->setBoxName(renamingBoxIdx_, text);
    } else if (textInputPurpose_ == TextInputPurpose::SearchSpecies) {
        searchFilter_.speciesName = text;
    } else if (textInputPurpose_ == TextInputPurpose::SearchOT) {
        searchFilter_.otName = text;
    } else if (textInputPurpose_ == TextInputPurpose::SearchLevelMin) {
        int val = text.empty() ? 0 : std::atoi(text.c_str());
        searchFilter_.levelMin = (val < 0) ? 0 : (val > 100 ? 100 : val);
    } else if (textInputPurpose_ == TextInputPurpose::SearchLevelMax) {
        int val = text.empty() ? 0 : std::atoi(text.c_str());
        searchFilter_.levelMax = (val < 0) ? 0 : (val > 100 ? 100 : val);
    } else if (textInputPurpose_ == TextInputPurpose::ImportPathEntry) {
        if (!text.empty()) {
            bool exists = false;
            for (const auto& e : importPaths_)
                if (e.path == text) { exists = true; break; }
            if (!exists) {
                importPaths_.push_back({text, true});
                saveImportPaths(basePath_, importPaths_);
                importSettingsCursor_ = (int)importPaths_.size(); // row 0 is the autocheck toggle
            }
        }
    } else if (textInputPurpose_ == TextInputPurpose::EditUpdateUrl) {
        if (!text.empty()) {
            // Toglie spazi e attiva subito (scrive update.cfg, toglie .off).
            std::string u = text;
            while (!u.empty() && (u.front() == ' ' || u.front() == '\t')) u.erase(u.begin());
            while (!u.empty() && (u.back() == ' ' || u.back() == '\t' || u.back() == '/')) u.pop_back();
            if (!u.empty() && writeUpdateCfgUrl(basePath_, u)) {
                DebugLog::line("settings: url -> %s", u.c_str());
                showMessageAndWait(i18n::get(StrKey::SetTitle), u);
            } else {
                showMessageAndWait(i18n::get(StrKey::SetTitle), "SCRITTURA FALLITA:\n" + basePath_ + "update.cfg");
            }
        }
    }
}
