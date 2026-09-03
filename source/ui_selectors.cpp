#include "ui.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "nro_version.h"
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
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
            drawRect(cardX, cardY, CARD_W, CARD_H, T().menuHighlight);
            drawRectOutline(cardX, cardY, CARD_W, CARD_H, T().cursor, 3);
        } else {
            drawRect(cardX, cardY, CARD_W, CARD_H, T().panelBg);
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

    if (availableGames_.empty()) {
        showMessageAndWait(i18n::get(StrKey::NoSaveData),
            i18n::get(StrKey::NoSaveDataBody));
        return;
    }

    refreshBankCounts();

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    gameSelOnAllBanks_ = false;
    gameSelOnChevron_ = 0;
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();
    screen_ = AppScreen::GameSelector;
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
        // Try loading from cache first
        char hexId[32];
        std::snprintf(hexId, sizeof(hexId), "%016lX", titleIdOf(game));
        std::string cachePath = cacheDir + hexId + ".jpg";

        SDL_Surface* surf = IMG_Load(cachePath.c_str());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_FreeSurface(surf);
            if (tex) {
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
            if (!surf) {
                DebugLog::line("icons: %s icon decode failed (%zu B): %s",
                    gameInfo(game).gameTag, iconSize, IMG_GetError());
                continue;
            }
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_FreeSurface(surf);
            if (tex) {
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
}

// --- Game Selector ---

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

    int numGames = (int)availableGames_.size();
    constexpr int COLS = 6;
    constexpr int ROWS_PER_PAGE = 2;
    constexpr int GAMES_PER_PAGE = COLS * ROWS_PER_PAGE;
    constexpr int CARD_W = 160;
    constexpr int CARD_H = 200;
    constexpr int CARD_GAP = 20;
    constexpr int ICON_SIZE = 128;

    int totalPages = (numGames + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;
    int pageStart = gameSelPage_ * GAMES_PER_PAGE;
    int pageEnd = std::min(pageStart + GAMES_PER_PAGE, numGames);
    int pageCount = pageEnd - pageStart;

    int rows = (pageCount + COLS - 1) / COLS;
    int totalH = rows * CARD_H + (rows - 1) * CARD_GAP;
    int gridStartY = (SCREEN_H - totalH) / 2;

    for (int i = pageStart; i < pageEnd; i++) {
        int idx = i - pageStart;
        int r = idx / COLS;
        int c = idx % COLS;

        // Center each row: count items in this row
        int rowItems = std::min(COLS, pageCount - r * COLS);
        int rowW = rowItems * CARD_W + (rowItems - 1) * CARD_GAP;
        int rowStartX = (SCREEN_W - rowW) / 2;

        int cardX = rowStartX + c * (CARD_W + CARD_GAP);
        int cardY = gridStartY + r * (CARD_H + CARD_GAP);

        // Card background
        if (i == gameSelCursor_ && !gameSelOnAllBanks_ && gameSelOnChevron_ == 0) {
            drawRect(cardX, cardY, CARD_W, CARD_H, T().menuHighlight);
            drawRectOutline(cardX, cardY, CARD_W, CARD_H, T().cursor, 3);
        } else {
            drawRect(cardX, cardY, CARD_W, CARD_H, T().panelBg);
        }

        // Icon
        int iconX = cardX + (CARD_W - ICON_SIZE) / 2;
        int iconY = cardY + 10;

        auto it = gameIconCache_.find(availableGames_[i]);
        if (it != gameIconCache_.end() && it->second) {
            SDL_Rect dst = {iconX, iconY, ICON_SIZE, ICON_SIZE};
            SDL_RenderCopy(renderer_, it->second, nullptr, &dst);
        } else {
            // Colored placeholder with game abbreviation
            drawRect(iconX, iconY, ICON_SIZE, ICON_SIZE, T().iconPlaceholder);
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
            }
            drawTextCentered(abbr, iconX + ICON_SIZE / 2, iconY + ICON_SIZE / 2,
                             T().text, font_);
        }

        // Game name below icon
        std::string name = gameDisplayNameOf(availableGames_[i]);
        // Strip "Pokemon " prefix for brevity
        if (name.substr(0, 8) == "Pokemon ")
            name = name.substr(8);
        if (name.length() > 20) name = name.substr(0, 19) + ".";
        drawTextCentered(name, cardX + CARD_W / 2, cardY + ICON_SIZE + 30,
                         T().text, fontSmall_);

        // Bank count under game name
        auto bc = gameBankCounts_.find(availableGames_[i]);
        int bankCount = (bc != gameBankCounts_.end()) ? bc->second : 0;
        std::string bankStr = "(" + std::to_string(bankCount) + ")";
        drawTextCentered(bankStr, cardX + CARD_W / 2, cardY + ICON_SIZE + 50,
                         T().textDim, fontSmall_);
    }

    // "View All Banks" option below the grid
    {
        int lastRow = (pageCount - 1) / COLS;
        int gridBottomY = gridStartY + (lastRow + 1) * (CARD_H + CARD_GAP);
        int allBanksY = gridBottomY + 10;

        std::string label = i18n::get(StrKey::ViewAllBanks);
        const auto& te = getTextEntry(label, font_, T().text);
        int labelW = te.w + 40;  // padding
        int labelH = 36;
        int labelX = (SCREEN_W - labelW) / 2;

        if (gameSelOnAllBanks_) {
            drawRect(labelX, allBanksY, labelW, labelH, T().menuHighlight);
            drawRectOutline(labelX, allBanksY, labelW, labelH, T().cursor, 2);
        } else {
            drawRect(labelX, allBanksY, labelW, labelH, T().panelBg);
        }
        drawTextCentered(label, SCREEN_W / 2, allBanksY + labelH / 2,
                         T().text, font_);
    }

    // Chevron buttons for page navigation
    if (totalPages > 1) {
        constexpr int BTN_W = 40;
        constexpr int BTN_H = 60;
        int btnY = SCREEN_H / 2 - BTN_H / 2;

        // Left button
        int leftX = 10;
        bool canLeft = gameSelPage_ > 0;
        bool leftFocused = gameSelOnChevron_ == -1;
        drawRect(leftX, btnY, BTN_W, BTN_H, leftFocused ? T().menuHighlight : T().panelBg);
        if (leftFocused)
            drawRectOutline(leftX, btnY, BTN_W, BTN_H, T().cursor, 3);
        drawTextCentered("<", leftX + BTN_W / 2, btnY + BTN_H / 2,
                         canLeft ? T().text : T().textDim, font_);

        // Right button
        int rightX = SCREEN_W - BTN_W - 10;
        bool canRight = gameSelPage_ < totalPages - 1;
        bool rightFocused = gameSelOnChevron_ == 1;
        drawRect(rightX, btnY, BTN_W, BTN_H, rightFocused ? T().menuHighlight : T().panelBg);
        if (rightFocused)
            drawRectOutline(rightX, btnY, BTN_W, BTN_H, T().cursor, 3);
        drawTextCentered(">", rightX + BTN_W / 2, btnY + BTN_H / 2,
                         canRight ? T().text : T().textDim, font_);
    }

    if (selectedProfile_ >= 0) {
        drawStatusBar(totalPages > 1 ? i18n::get(StrKey::StatusGameBackPage)
                                     : i18n::get(StrKey::StatusGameBack));
        std::string profileLabel = account_.profiles()[selectedProfile_].nickname;
        profileLabel += " | ";
        profileLabel += useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            profileLabel += " | DBG";
        const auto& e = getTextEntry(profileLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(profileLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    } else {
        drawStatusBar(totalPages > 1 ? i18n::get(StrKey::StatusGameQuitPage)
                                     : i18n::get(StrKey::StatusGameQuit));
        // Show core even without profile so feedback is always visible
        std::string coreLabel = useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            coreLabel += " | DBG";
        const auto& e = getTextEntry(coreLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(coreLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }
    if (appletMode_) {
        std::string modeLabel = std::string(i18n::get(StrKey::DualBankMode)) + " | " + (useOpenHome() ? "OH" : "PK");
        if (DebugLog::enabled())
            modeLabel += " | DBG";
        const auto& e = getTextEntry(modeLabel, fontSmall_, T().goldLabel);
        if (e.tex) drawText(modeLabel, SCREEN_W - e.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }
}

void UI::handleGameSelectorInput(bool& running) {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    constexpr int COLS = 6;

    constexpr int GAMES_PER_PAGE = 12;

    int totalPages = (numGames + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;

    auto moveGrid = [&](int dx, int dy) {
        int pageStart = gameSelPage_ * GAMES_PER_PAGE;
        int pageEnd = std::min(pageStart + GAMES_PER_PAGE, numGames);
        int pageCount = pageEnd - pageStart;

        // On a chevron button
        if (gameSelOnChevron_ != 0) {
            if (dx != 0) {
                if (gameSelOnChevron_ == -1 && dx > 0) {
                    // Right from left chevron → back to grid col 0
                    gameSelOnChevron_ = 0;
                } else if (gameSelOnChevron_ == 1 && dx < 0) {
                    // Left from right chevron → back to grid last col
                    gameSelOnChevron_ = 0;
                    int localIdx = gameSelCursor_ - pageStart;
                    int row = localIdx / COLS;
                    int rowItems = std::min(COLS, pageCount - row * COLS);
                    gameSelCursor_ = pageStart + row * COLS + rowItems - 1;
                }
            }
            if (dy > 0) {
                gameSelOnChevron_ = 0;
                gameSelOnAllBanks_ = true;
            }
            if (dy < 0) {
                gameSelOnChevron_ = 0;
            }
            return;
        }

        if (gameSelOnAllBanks_) {
            // On "All Banks" row: up goes back to grid, left/right ignored
            if (dy < 0) {
                gameSelOnAllBanks_ = false;
                // Place cursor on bottom row of current page
                int totalRows = (pageCount + COLS - 1) / COLS;
                int lastRowStart = (totalRows - 1) * COLS;
                int lastRowItems = pageCount - lastRowStart;
                int col = (gameSelCursor_ - pageStart) % COLS;
                if (col >= lastRowItems) col = lastRowItems - 1;
                gameSelCursor_ = pageStart + lastRowStart + col;
            }
            return;
        }

        int localIdx = gameSelCursor_ - pageStart;
        int col = localIdx % COLS;
        int row = localIdx / COLS;
        int totalRows = (pageCount + COLS - 1) / COLS;

        col += dx;
        row += dy;

        // Moving down past the last row goes to "All Banks"
        if (row >= totalRows) {
            gameSelOnAllBanks_ = true;
            return;
        }

        // Navigate to chevrons when going past grid edges (only if page exists)
        if (totalPages > 1) {
            if (col < 0 && gameSelPage_ > 0) {
                gameSelOnChevron_ = -1;
                return;
            }
            int rowItems = std::min(COLS, pageCount - row * COLS);
            if (col >= rowItems && gameSelPage_ < totalPages - 1) {
                gameSelOnChevron_ = 1;
                return;
            }
        }

        // Wrap columns within the row (single-page fallback)
        int rowItems = std::min(COLS, pageCount - row * COLS);
        if (rowItems <= 0) rowItems = COLS;
        if (col < 0) col = rowItems - 1;
        if (col >= rowItems) col = 0;

        // Wrap rows (up from top goes to "All Banks")
        if (row < 0) {
            gameSelOnAllBanks_ = true;
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

        // Game selector menu intercepts input
        if (showGameSelMenu_) {
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        gameSelMenuCursor_ = (gameSelMenuCursor_ + 4 - 1) % 4; // Core, Debug log, Update, Exit
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        gameSelMenuCursor_ = (gameSelMenuCursor_ + 1) % 4;
                        break;
                    case SDL_CONTROLLER_BUTTON_B: // Switch A = conferma
                        if (gameSelMenuCursor_ == 0) {
                            // Switch Core: toggle PK/OH
                            g_cryptoEngine = (g_cryptoEngine == CryptoEngine::PK) ? CryptoEngine::OH : CryptoEngine::PK;
                            saveCryptoEngine(basePath_, g_cryptoEngine);
                            showGameSelMenu_ = false;
                        } else if (gameSelMenuCursor_ == 1) {
                            // Debug log: toggle on/off, resta nel menu per feedback visivo
                            DebugLog::setEnabled(!DebugLog::enabled());
                        } else if (gameSelMenuCursor_ == 2) {
                            // Check for a newer NRO and (if found) install + relaunch
                            showGameSelMenu_ = false;
                            if (checkForUpdate()) {
                                running = false;
                                return;
                            }
                        } else {
                            // Exit — chiudi menu e esci
                            showGameSelMenu_ = false;
                            running = false;
                            return;
                        }
                        break;
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
                    if (gameSelOnChevron_ == -1 && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnChevron_ = 0;
                    } else if (gameSelOnChevron_ == 1 && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnChevron_ = 0;
                    } else if (gameSelOnAllBanks_)
                        enterAllBanksMode();
                    else
                        selectGame(availableGames_[gameSelCursor_]);
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                    if (selectedProfile_ >= 0) {
                        freeGameIcons();
                        account_.unmountSave();
                        screen_ = AppScreen::ProfileSelector;
                    } else {
                        running = false;
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_X: // Switch Y = theme
                    showThemeSelector_ = true;
                    themeSelCursor_ = themeIndex_;
                    themeSelOriginal_ = themeIndex_;
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: { // L = previous page
                    if (totalPages > 1 && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnAllBanks_ = false;
                        gameSelOnChevron_ = 0;
                    }
                    break;
                }
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: { // R = next page
                    if (totalPages > 1 && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnAllBanks_ = false;
                        gameSelOnChevron_ = 0;
                    }
                    break;
                }
                case SDL_CONTROLLER_BUTTON_BACK: // - = about
                    showAbout_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_START: // + (open game selector menu)
                    showGameSelMenu_ = true;
                    gameSelMenuCursor_ = 0;
                    break;
            }
        }
    }

    // Joystick repeat navigation
    if (showGameSelMenu_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            gameSelMenuCursor_ = (gameSelMenuCursor_ + (stickDirY_ > 0 ? 1 : 4 - 1)) % 4;
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    } else if (!showGameSelMenu_ && (stickDirX_ != 0 || stickDirY_ != 0)) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            if (stickDirX_ != 0) moveGrid(stickDirX_, 0);
            if (stickDirY_ != 0) moveGrid(0, stickDirY_);
            stickMoveTime_ = now;
            stickMoved_ = true;
            markDirty();
        }
    }
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
    bankSelTarget_ = Panel::Bank;
    leftBankName_.clear();
    leftBankPath_.clear();
    activeBankName_.clear();
    activeBankPath_.clear();

    screen_ = AppScreen::BankSelector;
}

bool UI::checkForUpdate() {
    const std::string runningNro = basePath_ + "OpenHomeNX.nro";
    // Finalizza update pendente lasciato da envSetNextLoad(.new) al giro precedente.
    // Dopo il restart via nextLoad siamo in esecuzione da .new, quindi l'originale non è in uso e si può sovrascrivere con copy (non rename di file in uso).
    {
        const std::string pending = runningNro + ".new";
        struct stat st;
        if (stat(pending.c_str(), &st) == 0) {
            std::string pendVer;
            if (readNroDisplayVersion(pending, pendVer)) {
                std::string curVerTmp =
#ifdef APP_VERSION
                    APP_VERSION;
#else
                    "0.0.0";
#endif
                if (pendVer == curVerTmp) {
                    // Siamo appena stati rilanciati da .new (stessa versione). Consolidiamo l'update copiando .new -> originale.
                    if (copyFileTo(pending, runningNro)) {
                        DebugLog::line("update: finalized pending %s -> %s v%s (copy)", pending.c_str(), runningNro.c_str(), pendVer.c_str());
                        std::remove(pending.c_str());
                        // Pulisci il nextLoad residuo per evitare il doppio restart su + -> Exit
                        if (envHasNextLoad()) envSetNextLoad("", "");
                    } else {
                        DebugLog::line("update: finalize copy failed %s -> %s", pending.c_str(), runningNro.c_str());
                    }
                }
            }
        }
    }
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
        // Event-driven: libusbhsfs enumera in background; una singola query
        // sincrona subito dopo il boot dà 0. Aspettiamo l'evento di status change.
        UEvent* ev = usbHsFsGetStatusChangeUserEvent();
        u32 phys = usbHsFsGetPhysicalDeviceCount();
        u32 n = usbHsFsGetMountedDeviceCount();
        DebugLog::line("update: USB physical=%u mounted=%u (initial)", phys, n);
        if (n == 0 && ev) {
            // Attesa enumerazione: 10 iterazioni da 1s = ~10s totali.
            // Timeout lungo per dare tempo all'enclosure NVMe di inizializzarsi; hbmenu/DBI fanno lo stesso wait via UEvent.
            showWorking("Scanning USB…");
            for (int i = 0; i < 10 && n == 0; ++i) {
                waitSingle(waiterForUEvent(ev), 1000000000ULL); // 1s per iterazione
                phys = usbHsFsGetPhysicalDeviceCount();
                n = usbHsFsGetMountedDeviceCount();
                DebugLog::line("update: USB wait %d: physical=%u mounted=%u", i, phys, n);
            }
            // Diagnosi: phys>0 & mounted==0 => drive visto ma non montato
            // (filesystem non supportato da questa build FAT/exFAT-only, es. NTFS/ext4).
            // phys==0 => non visto affatto (alimentazione/enclosure/contesto usb:hs).
            if (n == 0) {
                DebugLog::line("update: USB give up after 10s (physical=%u) — %s",
                               phys, phys > 0 ? "seen but not mounted (fs?)" : "not seen at all");
            }
            // Prompt per riscansione se ancora 0: l'utente può inserire ora il drive.
            if (n == 0) {
                const char* body = phys > 0
                    ? "USB drive seen but not mounted.\nFilesystem must be FAT32 or exFAT.\nA = scan again   B = continue with SD."
                    : "No USB drive detected.\nInsert USB now and press A to scan again,\nB to continue with SD.";
                if (showConfirmDialog("USB not found", body)) {
                    // Riscansione immediata dopo prompt (l'utente ha premuto A)
                    phys = usbHsFsGetPhysicalDeviceCount();
                    n = usbHsFsGetMountedDeviceCount();
                    DebugLog::line("update: USB rescan after prompt physical=%u mounted=%u", phys, n);
                    if (n == 0 && ev) {
                        // Un ultimo wait breve dopo l'inserimento
                        waitSingle(waiterForUEvent(ev), 2000000000ULL); // 2s
                        phys = usbHsFsGetPhysicalDeviceCount();
                        n = usbHsFsGetMountedDeviceCount();
                        DebugLog::line("update: USB post-prompt wait physical=%u mounted=%u", phys, n);
                    }
                }
            }
        }
        if (n > 0) {
            if (n > 8) n = 8;
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            // Retry se List ne ritorna meno di n (race enumerazione)
            if (got < n && ev) {
                waitSingle(waiterForUEvent(ev), 200000000ULL); // 200ms breve
                got = usbHsFsListMountedDevices(devs.data(), n);
                DebugLog::line("update: USB List retry got=%u (expected %u)", got, n);
            }
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
    candidates.push_back(basePath_ + "update/OpenHomeNX.nro");
    candidates.push_back("sdmc:/switch/OpenHomeNX/update/OpenHomeNX.nro");
    // SD root (richiesta utente: butta direttamente in sdmc:/)
    candidates.push_back("sdmc:/OpenHomeNX.nro");
    candidates.push_back("sdmc:/OpenHomeNX/update.nro");
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

    if (foundPath.empty()) {
        showMessageAndWait("Update", "No build found.\nCurrent version: v" + curVer +
            "\n\nDrop OpenHomeNX.nro into sdmc:/, sdmc:/switch/OpenHomeNX/update/ or USB.");
        return false;
    }

    if (foundCmp > 0) {
        if (!showConfirmDialog("Update available",
                "Found v" + foundVer + " (running v" + curVer + ")\nFrom: " + foundPath + "\nInstall and restart?"))
            return false;
    } else if (foundCmp == 0) {
        if (!showConfirmDialog("Same version",
                "Found v" + foundVer + " (same as running v" + curVer + ")\nFrom: " + foundPath + "\nInstall anyway?"))
            return false;
    } else {
        if (!showConfirmDialog("Downgrade?",
                "Found v" + foundVer + " (older than running v" + curVer + ")\nFrom: " + foundPath + "\nInstall anyway?"))
            return false;
    }

    showWorking("Updating...");
    const std::string tmp = runningNro + ".new";
    std::remove(tmp.c_str());
    if (!copyFileTo(foundPath, tmp)) {
        std::remove(tmp.c_str());
        showMessageAndWait("Update", "Copy failed. The current app is untouched.");
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
    // Se hbloader supporta next-load, non toccare il file in uso: basta
    // puntare al .new e riavviare. Evita il fail "rename while in use" visto
    // in debug.log (copiato con .new ma rename fallito).
    if (envHasNextLoad()) {
        envSetNextLoad(tmp.c_str(), tmp.c_str());
        DebugLog::line("update: nextLoad -> %s", tmp.c_str());
        showMessageAndWait("Update", "Installed v" + foundVer + ".\nRestarting...");
        return true; // caller stops the loop -> main() returns -> hbloader relaunches
    }
    std::remove(runningNro.c_str());
    if (std::rename(tmp.c_str(), runningNro.c_str()) != 0) {
        showMessageAndWait("Update", "Could not replace the app file (in use?).\nTry closing the app first.");
        return false;
    }
    showMessageAndWait("Update", "Installed v" + foundVer +
        ".\nClose and reopen the app to use it.");
    return false;
}

void UI::drawGameSelMenuPopup() {
    // Semi-transparent dark overlay
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 300;
    constexpr int POP_H = 240; // 4 rows (Core / Debug / Update / Exit)
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::MenuTitle), popX + POP_W / 2, popY + 22, T().text, font_);

    const char* labels[] = { "Switch Core", "Debug log", "Check for update", "Exit" };
    constexpr int MENU_ITEMS = 4;
    int rowH = 36;
    int startY = popY + 50;

    for (int i = 0; i < MENU_ITEMS; i++) {
        int rowY = startY + i * rowH;
        if (i == gameSelMenuCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        std::string label = labels[i];
        if (i == 0)
            label += useOpenHome() ? " (OH)" : " (PK)";
        else if (i == 1)
            label += DebugLog::enabled() ? " (on)" : " (off)";
        drawTextCentered(label, popX + POP_W / 2, rowY + rowH / 2 - 8, T().text, font_);
    }
}
