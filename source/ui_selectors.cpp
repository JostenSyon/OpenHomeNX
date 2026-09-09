#include "ui.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "nro_version.h"
#include "app_version.h"
#include "update_net.h"

#include <cerrno>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

namespace {
// update.cfg accanto all'NRO (o in sdmc:/switch/OpenHomeNX/). Righe key=value:
//   url=http://192.168.1.50:8000            (radice con latest.json + il .nro)
//   token=<PAT>                             (solo repo privati, header Bearer)
//   auto=1                                  (check update in parallelo al boot)
// Senza `url=` il check update usa le GitHub releases pubbliche
// (githubReleasesUrl sotto); Send log/save richiedono comunque `url=`
// (GitHub non riceve upload).
struct UpdateCfg {
    std::string url, token;
    long backupMb = 256;   // tetto auto-backup per gioco, titoli installati
    long backupMbSd = 32;  // idem, save file-backed (SD, piccoli)
};

bool readUpdateCfg(const std::string& basePath, UpdateCfg& out) {
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
            if (k == "url") out.url = v;
            else if (k == "token") out.token = v;
            else if (k == "backup_mb") out.backupMb = std::atol(v.c_str());
            else if (k == "backup_mb_sd") out.backupMbSd = std::atol(v.c_str());
        }
        if (!out.url.empty()) return true;
    }
    return false;
}
} // namespace
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif
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
    appendImportedGames();

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

// --- File import (docs/archive/GEN_PLAN.md Fase 2/5: emulator saves on SD/USB) ---

void UI::appendImportedGames() {
    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_);
    for (const auto& ig : importedGames_)
        availableGames_.push_back(ig.type);
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
                                               isGen45File(g) || isGen6XY(g) || isGen7SM(g); }),
        availableGames_.end());

    importedGames_ = scanImportPaths(importPaths_, autoCheckUsb_);
    for (const auto& ig : importedGames_)
        availableGames_.push_back(ig.type);

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
        } else if (isImportedFile(availableGames_[i]) || isGen1File(availableGames_[i]) || isGen2File(availableGames_[i])) {
            // No NS control data (no titleId) — a fixed per-game background
            // (Bulbapedia color templates, same values pkm_rs_types uses for
            // OriginGame::color()) plus the OpenHome logo PNG, letterboxed to
            // fit without stretching.
            SDL_Color bg;
            switch (availableGames_[i]) {
                case GameType::RUBY:     bg = {0xCD, 0x22, 0x36, 255}; break;
                case GameType::SAPPHIRE: bg = {0x3D, 0x51, 0xA7, 255}; break;
                case GameType::DIAMOND:  bg = {0x7E, 0xC8, 0xE8, 255}; break;
                case GameType::PEARL:    bg = {0xE8, 0xA0, 0xC0, 255}; break;
                case GameType::PLATINUM: bg = {0x90, 0x90, 0x98, 255}; break;
                case GameType::HEARTGOLD: bg = {0xE8, 0xB8, 0x28, 255}; break;
                case GameType::SOULSILVER: bg = {0x98, 0xB8, 0xD8, 255}; break;
                case GameType::BLACK:    bg = {0x28, 0x28, 0x30, 255}; break;
                case GameType::WHITE:    bg = {0xE8, 0xE8, 0xE8, 255}; break;
                case GameType::BLACK2:   bg = {0x18, 0x18, 0x20, 255}; break;
                case GameType::WHITE2:   bg = {0xF8, 0xF8, 0xF8, 255}; break;
                case GameType::X:        bg = {0x20, 0x60, 0xC0, 255}; break;
                case GameType::Y:        bg = {0xC0, 0x30, 0x30, 255}; break;
                case GameType::SUN:      bg = {0xE8, 0x70, 0x20, 255}; break;
                case GameType::MOON:     bg = {0x30, 0x30, 0x60, 255}; break;
                case GameType::RED:      bg = {0xE0, 0x20, 0x20, 255}; break;
                case GameType::BLUE:     bg = {0x20, 0x60, 0xE0, 255}; break;
                case GameType::YELLOW:   bg = {0xE8, 0xC8, 0x10, 255}; break;
                case GameType::GOLD:     bg = {0xD8, 0xA8, 0x20, 255}; break;
                case GameType::SILVER:   bg = {0xA0, 0xB0, 0xC0, 255}; break;
                case GameType::CRYSTAL:  bg = {0x40, 0xC0, 0xE0, 255}; break;
                case GameType::EMERALD: default: bg = {0x50, 0xC8, 0x78, 255}; break;
            }
            drawRect(iconX, iconY, ICON_SIZE, ICON_SIZE, bg);
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
                    SDL_Rect dst = {iconX, iconY, ICON_SIZE, ICON_SIZE};
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
                            SDL_Rect dst = {iconX, iconY, ICON_SIZE, ICON_SIZE};
                            SDL_RenderCopy(renderer_, artIt->second, &src, &dst);
                        } else {
                        // Base +15% di dimensione, a destra del 15% e in basso
                        // del 5% (percentuali su ICON_SIZE). L'eccesso viene
                        // tagliato netto sul bordo riquadro via clip.
                        // Ruby: Groudon centrato orizzontalmente.
                        // Sapphire: Kyogre centrato, +10% dimensione e +5pp in
                        // basso rispetto alla base. Logo identico per tutti.
                        constexpr int SPR = 117;
                        constexpr int SHIFT_X = (128 * 15) / 100;
                        constexpr int SHIFT_Y = (128 * 5) / 100;
                        int shiftX = SHIFT_X;
                        int spr = SPR;
                        int shiftY = SHIFT_Y;
                        if (availableGames_[i] == GameType::RUBY)
                            shiftX = 0;
                        if (availableGames_[i] == GameType::SAPPHIRE) {
                            shiftX = (128 * 5) / 100;
                            spr = (SPR * 110) / 100;
                            shiftY = (128 * 15) / 100;
                        }
                        float scale = std::min((float)spr / texW, (float)spr / texH);
                        int dstW = (int)(texW * scale);
                        int dstH = (int)(texH * scale);
                        SDL_Rect dst = {iconX + (ICON_SIZE - dstW) / 2 + shiftX,
                                        iconY + ICON_SIZE - dstH + shiftY, dstW, dstH};
                        SDL_Rect clip = {iconX, iconY, ICON_SIZE, ICON_SIZE};
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
                        constexpr int LOGO_H = 54;
                        int dstW = (int)(texW * ((float)LOGO_H / texH));
                        if (dstW > ICON_SIZE) dstW = ICON_SIZE;
                        SDL_Rect dst = {iconX + (ICON_SIZE - dstW) / 2, iconY, dstW, LOGO_H};
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
                        float scale = std::min((float)ICON_SIZE / texW, (float)ICON_SIZE / texH);
                        int dstW = (int)(texW * scale);
                        int dstH = (int)(texH * scale);
                        SDL_Rect dst = {iconX + (ICON_SIZE - dstW) / 2, iconY + (ICON_SIZE - dstH) / 2, dstW, dstH};
                        SDL_RenderCopy(renderer_, logoIt->second, nullptr, &dst);
                        drewLogo = true;
                    }
                }
                if (!drewLogo) {
                    // No logo asset (Gen1 file games): centered game tag.
                    const char* tag = gameInfo(availableGames_[i]).gameTag;
                    const auto& te = getTextEntry(tag, font_, T().text);
                    drawText(tag, iconX + (ICON_SIZE - te.w) / 2, iconY + (ICON_SIZE - te.h) / 2,
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
                int badgeX = iconX + 2, badgeY = iconY + ICON_SIZE - badgeH - 2;
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
                SDL_Rect badgeRect = {badgeX, badgeY, badgeW, badgeH};
                SDL_RenderFillRect(renderer_, &badgeRect);
                drawText(tag, badgeX + 4, badgeY + 2, T().text, fontSmall_);
            }
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
                default: break;
            }
            drawTextCentered(abbr, iconX + ICON_SIZE / 2, iconY + ICON_SIZE / 2,
                             T().text, font_);
        }

        // Game name below icon (RSE show the logo on top too, but the
        // text label below stays for readability at a glance).
        {
            std::string name = gameDisplayNameOf(availableGames_[i]);
            // Strip "Pokemon " prefix for brevity
            if (name.substr(0, 8) == "Pokemon ")
                name = name.substr(8);
            if (name.length() > 20) name = name.substr(0, 19) + ".";
            drawTextCentered(name, cardX + CARD_W / 2, cardY + ICON_SIZE + 30,
                             T().text, fontSmall_);
        }

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
    if (selectedProfile_ >= 0) {
        drawStatusBar((totalPages > 1 ? i18n::get(StrKey::StatusGameBackPage)
                                      : i18n::get(StrKey::StatusGameBack)) + ejectHint + saveHint);
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
                                      : i18n::get(StrKey::StatusGameQuit)) + ejectHint + saveHint);
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
                        if (count > 0) {
                            backupListCursor_ = std::max(0, backupListCursor_ - 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        if (count > 0) {
                            backupListCursor_ = std::min(count - 1, backupListCursor_ + 10);
                            scrollIntoView();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_B: { // Switch A = restore selezionato
                        if (count == 0) break;
                        std::string e = backupListEntries_[backupListCursor_];
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
                        showBackupList_ = false;
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

        // Debug save popup intercepts input (sotto il menu +)
        if (showSaveMenu_) {
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                markDirty();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        saveMenuCursor_ = (saveMenuCursor_ + 5) % 6;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        saveMenuCursor_ = (saveMenuCursor_ + 1) % 6;
                        break;
                    case SDL_CONTROLLER_BUTTON_B: { // Switch A = conferma
                        if (saveMenuCursor_ == 0) {
                            std::string out;
                            showSaveMenu_ = false;
                            if (backupGameSave(saveMenuGame_, out))
                                showMessageAndWait("Save backup", std::string("OK:\n") + out);
                            else
                                showMessageAndWait("Save backup", "FAILED (vedi debug.log)");
                        } else if (saveMenuCursor_ == 1) {
                            openBackupList(saveMenuGame_);
                        } else if (saveMenuCursor_ == 2) {
                            // Pulisci: applica il tetto retroattivamente (mai i manuali).
                            bool fb = !importedSavePath(saveMenuGame_, saveMenuOcc_).empty();
                            showSaveMenu_ = false;
                            uint64_t freed = pruneBackupsToCap(saveMenuGame_, fb);
                            char msg[128];
                            std::snprintf(msg, sizeof(msg), "Liberati %.1f MB di auto-backup.",
                                          freed / 1048576.0);
                            showMessageAndWait("Clean old backups", msg);
                        } else if (saveMenuCursor_ == 3) {
                            // Normalizza Delta: via i 16B extra, file raw 128K.
                            std::string p = importedSavePath(saveMenuGame_, saveMenuOcc_);
                            showSaveMenu_ = false;
                            if (p.empty()) {
                                showMessageAndWait("Normalize save", "Solo save SD (niente titoli installati).");
                            } else {
                                std::string info;
                                SaveFile::normalizeDeltaSave(p, info);
                                showMessageAndWait("Normalize save", info);
                            }
                        } else if (saveMenuCursor_ == 4) {
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
                            case GameSelMenuAction::SendLog: {
                                UpdateCfg cfg;
                                std::string err;
                                if (!readUpdateCfg(basePath_, cfg) || cfg.url.empty()) {
                                    showMessageAndWait(i18n::get(StrKey::SendLogTitle), i18n::get(StrKey::SendLogNoUrl));
                                } else if (!updateNetAvailable()) {
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
                                break;
                            }
                            case GameSelMenuAction::SendSave:
                                if (gameSelOnAllBanks_ || gameSelOnChevron_ != 0 ||
                                    gameSelCursor_ < 0 || gameSelCursor_ >= (int)availableGames_.size())
                                    showMessageAndWait(i18n::get(StrKey::SendSaveTitle), i18n::get(StrKey::SendSaveNoGame));
                                else
                                    sendSaveFor(availableGames_[gameSelCursor_],
                                                importedOccurrence(gameSelCursor_));
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
                        selectGame(availableGames_[gameSelCursor_], importedOccurrence(gameSelCursor_));
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                    DebugLog::line("nav: B in games profile=%d -> %s", selectedProfile_,
                        selectedProfile_ >= 0 ? "ProfileSelector" : "QUIT");
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
                case SDL_CONTROLLER_BUTTON_Y: // Switch X = save menu (debug) / eject USB
                    if (DebugLog::enabled() && !gameSelOnAllBanks_ && gameSelOnChevron_ == 0 &&
                        gameSelCursor_ >= 0 && gameSelCursor_ < (int)availableGames_.size()) {
                        openSaveMenu(availableGames_[gameSelCursor_],
                                     importedOccurrence(gameSelCursor_));
                        break;
                    }
#ifdef OH_USB_UPDATE
                {
                    u32 n = usbHsFsGetMountedDeviceCount();
                    if (n > 8) n = 8;
                    std::vector<UsbHsFsDevice> devs(n > 0 ? n : 1);
                    u32 got = n > 0 ? usbHsFsListMountedDevices(devs.data(), n) : 0;
                    int ok = 0;
                    for (u32 i = 0; i < got; i++)
                        if (usbHsFsUnmountDevice(&devs[i], true)) ok++;
                    DebugLog::line("usb eject: unmounted %d/%u device(s)", ok, got);
                    rescanImportedGames();
                    markDirty();
                }
#endif
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
    } else     if (showSaveMenu_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            saveMenuCursor_ = (saveMenuCursor_ + (stickDirY_ > 0 ? 1 : 5)) % 6;
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
    } else if (!showGameSelMenu_ && !showSaveMenu_ && !showBackupList_ && (stickDirX_ != 0 || stickDirY_ != 0)) {
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
        const std::string netUrl = cfg.url.empty()
            ? githubReleasesUrl("JostenSyon", "OpenHomeNX")
            : cfg.url;
        {
            DebugLog::line("update: net url=%s token=%s", netUrl.c_str(),
                           cfg.token.empty() ? "no" : "yes");
            if (!updateNetAvailable()) {
                DebugLog::line("update: rete non disponibile, salto Layer 1");
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateNetOff, netUrl));
            } else {
                showWorking(i18n::fmt(StrKey::UpdateContacting, netUrl));
                RemoteUpdateInfo info;
                std::string err;
                if (!updateNetFetchInfo(netUrl, cfg.token, info, err)) {
                    DebugLog::line("update: fetch info fallito: %s", err.c_str());
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateUnreachable, err, netUrl));
                } else {
                    int cmp = compareVersionStrings(info.version, curVer);
                    DebugLog::line("update: remoto v%s cmp=%d", info.version.c_str(), cmp);
                    if (cmp > 0) {
                        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailNetTitle),
                                i18n::fmt(StrKey::UpdateAvailNetBody, info.version, curVer, netUrl)))
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
                    } else {
                        // La rete ha risposto e non c'è niente di più recente:
                        // con debug attivo offri reinstall per testare l'updater anche a pari versione.
                        if (DebugLog::enabled()) {
                            if (showConfirmDialog(i18n::get(StrKey::UpdateSameDbgTitle),
                                    i18n::fmt(StrKey::UpdateSameDbgBody, curVer, info.version))) {
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

std::vector<std::string> UI::collectBackupEntries(GameType g) {
    // manuali + auto-apertura + auto legacy (backups/<profilo>/<gioco>/).
    std::vector<std::string> dirs = { manualBackupDir(g), autoBackupDir(g) };
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
        dirs.push_back(basePath_ + "backups/" +
                       account_.profiles()[selectedProfile_].pathSafeName +
                       "/" + gamePathNameOf(g) + "/");
    std::vector<std::string> entries;
    for (auto& dir : dirs)
        for (auto& n : listDirNames(dir))
            entries.push_back(dir + n);
    // Sotto-dir legacy con timestamp dentro (aperture titoli installati):
    // includi ricorsione di un livello per quelle.
    size_t base = entries.size();
    for (size_t i = 0; i < base; i++) {
        struct stat st;
        if (stat(entries[i].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            std::string sub = entries[i] + "/";
            // Solo se non e gia un backup-dir diretto (file dentro = backup).
            bool hasFile = false;
            for (auto& n : listDirNames(sub)) {
                struct stat st2;
                std::string full = sub + n;
                if (stat(full.c_str(), &st2) == 0 && !S_ISDIR(st2.st_mode)) {
                    entries.push_back(full);
                    hasFile = true;
                }
            }
            (void)hasFile;
        }
    }
    std::sort(entries.begin(), entries.end(), std::greater<std::string>());
    // Deduplica mantenendo l'ordine (stesso file da due dir mai, ma gratis).
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    return entries;
}

void UI::openBackupList(GameType g) {
    backupListGame_ = g;
    backupListEntries_ = collectBackupEntries(g);
    backupListCursor_ = 0;
    backupListScroll_ = 0;
    showBackupList_ = true;
    showSaveMenu_ = false;
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
            std::string n = backupListEntries_[i];
            auto slash = n.find_last_of('/');
            std::string base = (slash == std::string::npos) ? n : n.substr(slash + 1);
            if (base.size() > 52) base = base.substr(0, 51) + "~";
            drawText(base, popX + 30, rowY + 6, T().text, fontSmall_);
        }
    }
    drawTextCentered("A: restore  B: back", popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::autoBackupFileSave(GameType g, const std::string& path) {
    struct stat sst;
    uint64_t sz = 0;
    if (stat(path.c_str(), &sst) == 0) sz = (uint64_t)sst.st_size;
    if (!autoBackupNeeded(g, true, path, sz)) return;
    std::string dir = autoBackupDir(g);
    ensureDirRecursive(dir);
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    std::string dst = dir + backupTimestamp() + "_" + base;
    if (!copyFileTo(path, dst)) {
        DebugLog::line("auto backup FAILED: %s", path.c_str());
        return;
    }
    DebugLog::line("auto backup: %s -> %s", path.c_str(), dst.c_str());
    pruneBackupsToCap(g, true);
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
bool UI::autoBackupNeeded(GameType g, bool fileBacked, const std::string& src, uint64_t srcSize) {
    auto entries = autoBackupEntries(g);
    if (entries.empty()) return true;
    struct stat bst;
    if (stat(entries[0].c_str(), &bst) != 0) return true;
    time_t now = time(nullptr);
    constexpr long THROTTLE_SEC = 30 * 60;
    if (now - bst.st_mtime < THROTTLE_SEC) {
        DebugLog::line("auto backup throttled: %s (ultimo %lds fa)",
                       gameInfo(g).gameTag, (long)(now - bst.st_mtime));
        return false;
    }
    if (fileBacked) {
        struct stat sst;
        if (stat(src.c_str(), &sst) == 0 &&
            (uint64_t)sst.st_size == entryDiskSize(entries[0]) &&
            sst.st_mtime <= bst.st_mtime) {
            DebugLog::line("auto backup skipped (invariato): %s", src.c_str());
            return false;
        }
    } else if (srcSize == entryDiskSize(entries[0])) {
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
    } else if (!updateNetAvailable()) {
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

bool UI::backupGameSave(GameType g, std::string& out) {
    std::string dir = manualBackupDir(g);
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
        std::string mnt = account_.mountSave(selectedProfile_, g);
        if (mnt.empty()) return false;
        bool ok = AccountManager::backupSaveDir(mnt, dst);
        account_.unmountSave();
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
        account_.unmountSave();
        DebugLog::line("save restore (account): %s (%s)", entry.c_str(), ok ? "ok" : "FAIL");
        return ok;
    }
    return false;
}

void UI::drawSaveMenuPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    static const char* rows[] = { "Backup save", "Browse backups", "Clean old backups", "Normalize save", "Send save", "Close" };
    constexpr int NROWS = 6;
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
    std::vector<GameSelMenuAction> v = { GameSelMenuAction::SwitchCore, GameSelMenuAction::DebugLog };
    if (DebugLog::enabled()) {
        v.push_back(GameSelMenuAction::SendLog);
        v.push_back(GameSelMenuAction::SendSave);
    }
    v.push_back(GameSelMenuAction::ImportSettings);
    v.push_back(GameSelMenuAction::CheckUpdate);
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
            case GameSelMenuAction::SendLog:         label = "Send log"; break;
            case GameSelMenuAction::SendSave:        label = "Send save"; break;
            case GameSelMenuAction::ImportSettings:  label = "Import settings"; break;
            case GameSelMenuAction::CheckUpdate:     label = "Check for update"; break;
            case GameSelMenuAction::Exit:             label = "Exit"; break;
        }
        // drawTextCentered() takes the text's vertical CENTRE; match it to the
        // highlight box centre (box: top=rowY, height=rowH-4).
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }
}
