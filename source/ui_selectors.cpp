#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "nro_version.h"
#include "app_version.h"
#include "update_net.h"

#include <cerrno>
#include <cmath>
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
    long backupMb = 256;   // tetto CUMULATIVO auto-backup titoli installati
    long backupMbSd = 32;  // tetto cumulativo save file-backed (SD, piccoli)
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

static bool readQuickMenu(const std::string& basePath);
static void writeQuickMenu(const std::string& basePath, bool on);

// Righe popup save (X con debug): Normalize solo per GBA (RSE/FRLG, anche
// via USB se con +16B) — per gli altri giochi non serve e resta nascosta.
static std::vector<std::string> saveMenuRows(GameType g) {
    std::vector<std::string> r = { "Backup save", "Browse backups", "Clean old backups" };
    if (isImportedFile(g) || isFRLG(g)) r.push_back("Normalize save");
    r.push_back("Send save");
    r.push_back("Close");
    return r;
}
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
    gameSelOnAllBanks_ = false;
    gameSelOnSettings_ = false;
    gameSelOnEject_ = false;
    gameSelOnAvatar_ = false;
    gameSelOnPack_ = false;
    gameSelOnChevron_ = 0;
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();
    screen_ = AppScreen::GameSelector;
}

// --- File import (save SD/USB da emulatori o dump) ---

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

    // Avatar utente in alto a sinistra: A torna al selettore profili.
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount()) {
        constexpr int AV = 56;
        constexpr int AVX = 36, AVY = 30;
        if (gameSelOnAvatar_) {
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
    }
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
        bool sel = (i == gameSelCursor_ && !gameSelOnAllBanks_ && !gameSelOnSettings_ && !gameSelOnEject_ && !gameSelOnAvatar_ && !gameSelOnPack_ && gameSelOnChevron_ == 0);
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

        auto it = gameIconCache_.find(availableGames_[i]);
        if (it != gameIconCache_.end() && it->second) {
            SDL_Rect dst = {iconX, iconY, IS, IS};
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
            drawRoundRect(iconX, iconY, IS, IS, 10, bg);
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
                        // del 5% (percentuali su IS: scala col grow, stessa
                        // velocita dell'outline). L'eccesso viene
                        // tagliato netto sul bordo riquadro via clip.
                        // Ruby: Groudon centrato orizzontalmente.
                        // Sapphire: Kyogre centrato, +10% dimensione e +5pp in
                        // basso rispetto alla base. Logo identico per tutti.
                        const int SPR = (117 * IS) / 128;
                        const int SHIFT_X = (IS * 15) / 100;
                        const int SHIFT_Y = (IS * 5) / 100;
                        int shiftX = SHIFT_X;
                        int spr = SPR;
                        int shiftY = SHIFT_Y;
                        if (availableGames_[i] == GameType::RUBY)
                            shiftX = 0;
                        if (availableGames_[i] == GameType::SAPPHIRE) {
                            shiftX = (IS * 5) / 100;
                            spr = (SPR * 110) / 100;
                            shiftY = (IS * 15) / 100;
                        }
                        float scale = std::min((float)spr / texW, (float)spr / texH);
                        int dstW = (int)(texW * scale);
                        int dstH = (int)(texH * scale);
                        SDL_Rect dst = {iconX + (IS - dstW) / 2 + shiftX,
                                        iconY + IS - dstH + shiftY, dstW, dstH};
                        // Clip rientrata del raggio: la foto non sbava fuori
                        // dagli angoli arrotondati del tile.
                        constexpr int CCR = 10;
                        SDL_Rect clip = {iconX + CCR, iconY + CCR, IS - 2 * CCR, IS - 2 * CCR};
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
                        int logoH = (54 * IS) / 128;
                        int dstW = (int)(texW * ((float)logoH / texH));
                        if (dstW > IS) dstW = IS;
                        SDL_Rect dst = {iconX + (IS - dstW) / 2, iconY, dstW, logoH};
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

    // Riga bassa: zaino (sx, fisso) + banche (centro, fisso) + espelli USB
    // (dx, animato). Etichetta banche solo quando evidenziata.
    {
        constexpr int R = 34;
        constexpr int ICON_R = 24;
        constexpr int BTN_Y = SCREEN_H - 110;
        constexpr int ROW_DX = 104; // distanza fissa tra i pulsanti
        constexpr int VCX = SCREEN_W / 2;
        constexpr int PCX = SCREEN_W / 2 - ROW_DX;
#ifdef OH_USB_UPDATE
        bool ejectVisible = usbHsFsGetMountedDeviceCount() > 0;
#else
        bool ejectVisible = false;
#endif
        float ejectTarget = (float)SCREEN_W / 2 + ROW_DX;
        float ejectAlphaT = ejectVisible ? 255.0f : 0.0f;
        if (ejectBtnX_ < 0) ejectBtnX_ = ejectTarget;
        if (ejectVisible) ejectAnimStage_ = 0; // chiavetta tornata: annulla sequenza
        if (ejectAnimStage_ == 1) {
            // Appena espulso: prima sparisce l'eject.
            ejectAlphaT = 0.0f;
            if (ejectBtnA_ == 0.0f) ejectAnimStage_ = 2;
        } else if (ejectAnimStage_ == 2) {
            ejectAlphaT = 0.0f;
            ejectAnimStage_ = 0;
        } else if (ejectVisible) {
            ejectAnimStage_ = 0;
        }
        if (ejectBtnA_ == 0 && ejectVisible) ejectBtnX_ = ejectTarget + 60.0f; // entra da destra
        auto approach = [](float cur, float tgt) {
            float d = tgt - cur;
            if (d > -1.0f && d < 1.0f) return tgt;
            return cur + d * 0.25f;
        };
        float ne = approach(ejectBtnX_, ejectTarget);
        float na = approach(ejectBtnA_, ejectAlphaT);
        if (ne != ejectBtnX_ || na != ejectBtnA_) markDirty();
        ejectBtnX_ = ne; ejectBtnA_ = na;
        auto drawIcon = [&](SDL_Texture* tex, int cx) {
            SDL_SetTextureColorMod(tex, T().text.r, T().text.g, T().text.b);
            SDL_Rect dst = {cx - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        };
        // Zaino (WIP: messaggio finche non c'e l'injector eventi/strumenti).
        // Texture gia a misura (48px): blit 1:1, niente scaling = niente aliasing.
        drawRoundSelect(PCX, BTN_Y, R + 1, gameSelOnPack_);
        if (iconPack_) {
            SDL_SetTextureColorMod(iconPack_, T().text.r, T().text.g, T().text.b);
            SDL_Rect dst = {PCX - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
            SDL_RenderCopy(renderer_, iconPack_, nullptr, &dst);
            SDL_SetTextureColorMod(iconPack_, 255, 255, 255);
        }
        // Banche
        drawRoundSelect(VCX, BTN_Y, R + 1, gameSelOnAllBanks_);
        if (iconVault_) drawIcon(iconVault_, VCX);
        if (gameSelOnAllBanks_) {
            drawTextCentered(i18n::get(StrKey::ViewAllBanks), SCREEN_W / 2,
                             BTN_Y + R + 17, T().text, font_);
        }
        if (ejectBtnA_ > 1.0f && iconEject_) {
            int ecx = (int)(ejectBtnX_ + 0.5f);
            drawRoundSelect(ecx, BTN_Y, R + 1, gameSelOnEject_);
            SDL_SetTextureAlphaMod(iconEject_, (Uint8)ejectBtnA_);
            SDL_SetTextureColorMod(iconEject_, T().text.r, T().text.g, T().text.b);
            SDL_Rect dst = {ecx - ICON_R, BTN_Y - ICON_R, ICON_R * 2, ICON_R * 2};
            SDL_RenderCopy(renderer_, iconEject_, nullptr, &dst);
            SDL_SetTextureAlphaMod(iconEject_, 255);
            SDL_SetTextureColorMod(iconEject_, 255, 255, 255);
        } else if (!ejectVisible) {
            gameSelOnEject_ = false;
        }
    }

    // Ingranaggio impostazioni in basso a destra, stessa riga delle banche.
    // Apre lo stesso menu del tasto + (menu dedicato in futuro).
    {
        constexpr int GR = 26;
        int gcx = SCREEN_W - 64;
        int gcy = SCREEN_H - 110;
        drawRoundSelect(gcx, gcy, GR + 1, gameSelOnSettings_);
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
        bool leftFocused = gameSelOnChevron_ == -1;
        bool rightFocused = gameSelOnChevron_ == 1;
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
    // Vault / eject / gear (riga bassa fissa)
    constexpr float BTN_Y = SCREEN_H - 110;
    if (dist2(px, py, SCREEN_W / 2, BTN_Y) < 45 * 45) {
        gameSelOnAllBanks_ = true;
        gameSelOnAvatar_ = gameSelOnPack_ = false;
        gameSelOnEject_ = gameSelOnSettings_ = false;
        gameSelOnChevron_ = 0;
        enterAllBanksMode();
        return;
    }
    if (dist2(px, py, SCREEN_W / 2 - 104, BTN_Y) < 45 * 45) {
        gameSelOnPack_ = true;
        gameSelOnAvatar_ = gameSelOnAllBanks_ = false;
        gameSelOnEject_ = gameSelOnSettings_ = false;
        gameSelOnChevron_ = 0;
        showMessageAndWait(i18n::get(StrKey::SetTitle),
            i18n::get(StrKey::BagSoon)); // WIP: injector eventi/strumenti
        return;
    }
    if (ejectBtnA_ > 128 && dist2(px, py, ejectBtnX_, BTN_Y) < 45 * 45) {
        ejectUsbDevices();
        return;
    }
    if (dist2(px, py, SCREEN_W - 64, BTN_Y) < 40 * 40) {
        openSettings(); // il gear apre SEMPRE le impostazioni
        markDirty();
        return;
    }
    // Frecce pagine
    int numGames = (int)availableGames_.size();
    int totalPages = (numGames + 12 - 1) / 12;
    if (totalPages > 1) {
        if (dist2(px, py, 34, SCREEN_H / 2) < 34 * 34 && gameSelPage_ > 0) {
            gameSelPage_--;
            gameSelCursor_ = gameSelPage_ * 12;
            gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
            gameSelOnEject_ = gameSelOnSettings_ = false;
            gameSelOnChevron_ = 0;
            markDirty();
            return;
        }
        if (dist2(px, py, SCREEN_W - 34, SCREEN_H / 2) < 34 * 34 && gameSelPage_ < totalPages - 1) {
            gameSelPage_++;
            gameSelCursor_ = gameSelPage_ * 12;
            gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
            gameSelOnEject_ = gameSelOnSettings_ = false;
            gameSelOnChevron_ = 0;
            markDirty();
            return;
        }
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
            gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
            gameSelOnEject_ = gameSelOnSettings_ = false;
            gameSelOnChevron_ = 0;
            selectGame(availableGames_[i], importedOccurrence(i));
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
    gameSelOnEject_ = false;
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
                gameSelOnSettings_ = false;
                gameSelOnEject_ = false;
                gameSelOnAvatar_ = false;
                gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
            }
            if (dy < 0) {
                gameSelOnChevron_ = 0;
            }
            return;
        }

        if (gameSelOnPack_) {
            // Sullo zaino: destra torna alle banche, su torna in griglia
            if (dx > 0) {
                gameSelOnPack_ = false;
                gameSelOnAllBanks_ = true;
            } else if (dy < 0) {
                gameSelOnPack_ = false;
                int totalRows = (pageCount + COLS - 1) / COLS;
                int lastRowStart = (totalRows - 1) * COLS;
                int lastRowItems = pageCount - lastRowStart;
                int col = (gameSelCursor_ - pageStart) % COLS;
                if (col >= lastRowItems) col = lastRowItems - 1;
                gameSelCursor_ = pageStart + lastRowStart + col;
            }
            return;
        }

        if (gameSelOnAllBanks_) {
            // Riga bassa: sinistra = zaino, destra = eject/gear, su = griglia
            if (dx < 0) {
                gameSelOnAllBanks_ = false;
                gameSelOnPack_ = true;
                return;
            }
            if (dx > 0) {
                gameSelOnAllBanks_ = false;
#ifdef OH_USB_UPDATE
                if (usbHsFsGetMountedDeviceCount() > 0)
                    gameSelOnEject_ = true;
                else
                    gameSelOnSettings_ = true;
#else
                gameSelOnSettings_ = true;
#endif
                return;
            }
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

        if (gameSelOnEject_) {
            // Sull'espelli: sinistra torna alle banche, destra al gear, su in griglia
            if (dx < 0) {
                gameSelOnEject_ = false;
                gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
            } else if (dx > 0) {
                gameSelOnEject_ = false;
                gameSelOnSettings_ = true;
            } else if (dy < 0) {
                gameSelOnEject_ = false;
                int totalRows = (pageCount + COLS - 1) / COLS;
                int lastRowStart = (totalRows - 1) * COLS;
                int lastRowItems = pageCount - lastRowStart;
                int col = (gameSelCursor_ - pageStart) % COLS;
                if (col >= lastRowItems) col = lastRowItems - 1;
                gameSelCursor_ = pageStart + lastRowStart + col;
            }
            return;
        }

        if (gameSelOnSettings_) {
            // Sull'ingranaggio: sinistra torna a eject (se c'e) o banche
            if (dx < 0) {
                gameSelOnSettings_ = false;
#ifdef OH_USB_UPDATE
                if (usbHsFsGetMountedDeviceCount() > 0)
                    gameSelOnEject_ = true;
                else
                    gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
#else
                gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
#endif
            } else if (dy < 0) {
                gameSelOnSettings_ = false;
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
            gameSelOnSettings_ = false;
            gameSelOnEject_ = false;
            gameSelOnAvatar_ = false;
            gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
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

        // Wrap rows (up from top goes to avatar, down from avatar to grid)
        if (gameSelOnAvatar_) {
            if (dy > 0) gameSelOnAvatar_ = false;
            return;
        }
        if (row < 0) {
            if (selectedProfile_ >= 0) {
                gameSelOnAvatar_ = true;
            } else {
                gameSelOnSettings_ = false;
                gameSelOnEject_ = false;
                gameSelOnAllBanks_ = true;
                gameSelOnPack_ = false;
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
            if (!showBackupList_ && !showSaveMenu_ && !showGameSelMenu_ && !showSettings_) {
                if (dx < -120 && std::fabs(dy) < 200) {
                    if (totalPages > 1 && gameSelPage_ < totalPages - 1) {
                        gameSelPage_++;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
                        gameSelOnEject_ = gameSelOnSettings_ = false;
                        gameSelOnChevron_ = 0;
                        markDirty();
                    }
                } else if (dx > 120 && std::fabs(dy) < 200) {
                    if (totalPages > 1 && gameSelPage_ > 0) {
                        gameSelPage_--;
                        gameSelCursor_ = gameSelPage_ * GAMES_PER_PAGE;
                        gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
                        gameSelOnEject_ = gameSelOnSettings_ = false;
                        gameSelOnChevron_ = 0;
                        markDirty();
                    }
                } else if (!touchMoved_) {
                    selectorTap(px, py, running);
                }
            }
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
                int smN = (int)saveMenuRows(saveMenuGame_).size();
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
                        std::string sel = saveMenuRows(saveMenuGame_)[saveMenuCursor_];
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
                        } else if (sel == "Normalize save") {
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
                            case GameSelMenuAction::SendLog:
                                sendLogNow();
                                break;
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
                            case GameSelMenuAction::OpenSettings:
                                showGameSelMenu_ = false;
                                openSettings();
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
                    else if (gameSelOnPack_)
                        showMessageAndWait(i18n::get(StrKey::SetTitle),
                            i18n::get(StrKey::BagSoon)); // WIP: injector eventi/strumenti
                    else if (gameSelOnAvatar_) {
                        gameSelOnAvatar_ = false;
                        freeGameIcons();
                        account_.unmountSave();
                        screen_ = AppScreen::ProfileSelector;
                    }
                    else if (gameSelOnEject_)
                        ejectUsbDevices();
                    else if (gameSelOnSettings_) {
                        openSettings(); // il gear apre SEMPRE le impostazioni
                    }
                    else
                        selectGame(availableGames_[gameSelCursor_], importedOccurrence(gameSelCursor_));
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Switch B = back
                    if (gameSelOnAvatar_) { gameSelOnAvatar_ = false; break; }
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
                    showThemeSelector_ = true;
                    themeSelCursor_ = themeIndex_;
                    themeSelOriginal_ = themeIndex_;
                    break;
                case SDL_CONTROLLER_BUTTON_Y: // Switch X = save menu (debug) / eject USB
                    if (DebugLog::enabled() && !gameSelOnAllBanks_ && !gameSelOnSettings_ && !gameSelOnEject_ && !gameSelOnAvatar_ && !gameSelOnPack_ && gameSelOnChevron_ == 0 &&
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
                case SDL_CONTROLLER_BUTTON_START: // + : menu rapido se ON, impostazioni se OFF
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
    } else     if (showSaveMenu_ && stickDirY_ != 0) {
        uint32_t now = SDL_GetTicks();
        uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
        if (now - stickMoveTime_ >= delay) {
            int smN = (int)saveMenuRows(saveMenuGame_).size();
            saveMenuCursor_ = (saveMenuCursor_ + (stickDirY_ > 0 ? 1 : smN - 1)) % smN;
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
    } else if (!showGameSelMenu_ && !showSaveMenu_ && !showBackupList_ && !showSettings_ && (stickDirX_ != 0 || stickDirY_ != 0)) {
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
            std::string base = backupListEntries_[i].label;
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
    prunePoolToCap(true);
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
        // Senza commit l'unmount scarta le scritture (Horizon): restore
        // fantasma che dice ok ma non cambia niente (Violetto 2026-09-09).
        if (ok) account_.commitSave();
        account_.unmountSave();
        DebugLog::line("save restore (account): %s (%s)", entry.c_str(), ok ? "ok" : "FAIL");
        return ok;
    }
    return false;
}

void UI::drawSaveMenuPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    std::vector<std::string> rows = saveMenuRows(saveMenuGame_);
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
    std::vector<GameSelMenuAction> v = { GameSelMenuAction::SwitchCore, GameSelMenuAction::DebugLog };
    // Invio log/save solo con override rete attivo (GitHub non riceve upload).
    if (DebugLog::enabled()) {
        UpdateCfg cfg;
        readUpdateCfg(basePath_, cfg);
        if (!cfg.url.empty()) {
            v.push_back(GameSelMenuAction::SendLog);
            v.push_back(GameSelMenuAction::SendSave);
        }
    }
    v.push_back(GameSelMenuAction::ImportSettings);
    v.push_back(GameSelMenuAction::CheckUpdate);
    v.push_back(GameSelMenuAction::OpenSettings);
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
            case GameSelMenuAction::OpenSettings:     label = i18n::get(StrKey::SetTitle); break;
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
    showGameSelMenu_ = false;
    if (langList_.empty()) langList_ = i18n::availableLangs();
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
    cfg.clear();
    off.clear();
    const std::string dirs[] = { basePath, "sdmc:/switch/OpenHomeNX/" };
    for (auto& d : dirs) {
        struct stat st;
        if (cfg.empty() && stat((d + "update.cfg").c_str(), &st) == 0) cfg = d + "update.cfg";
        if (off.empty() && stat((d + "update.cfg.off").c_str(), &st) == 0) off = d + "update.cfg.off";
    }
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

// Scrive url= in update.cfg preservando le altre chiavi; attiva (toglie .off).
bool UI::writeUpdateCfgUrl(const std::string& basePath, const std::string& url) {
    std::string cfg, off;
    findUpdateCfgFiles(basePath, cfg, off);
    std::string dst = cfg.empty() ? basePath + "update.cfg" : cfg;
    std::ifstream f(dst);
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    if (f.good()) {
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("url=", 0) == 0) { line = "url=" + url; found = true; }
            lines.push_back(line);
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
        case 1: return 3; // Tema, Lingua, Zoom
        case 2: return 1; // Core
        case 3: return 4; // Cartelle, Scansiona, Max, Pulisci
        case 4: {
            // Sorgente/edit custom solo con debug: l'utente normale resta su GitHub.
            int n = 2;
            if (DebugLog::enabled() && hasCustomUrlFile(basePath_)) n = 3;
            return n; // Update, Sorgente [, Modifica]
        }
        case 5: return sendAvailable() ? 3 : 2; // Debug, Menu + [, Invia log]
        default: return 2; // Versione, Crediti
    }
}

static std::string readDefaultUser(const std::string& basePath);
std::string UI::settingsRowLabel(int cat, int row) const {
    if (cat == 0) return i18n::get(StrKey::SetDefaultUser);
    if (cat == 1) {
        if (row == 0) return i18n::get(StrKey::SetTheme);
        if (row == 1) return i18n::get(StrKey::SetLanguage);
        return i18n::get(StrKey::SetZoom);
    }
    if (cat == 2) return i18n::get(StrKey::SetCore);
    if (cat == 3) {
        if (row == 0) return i18n::get(StrKey::SetSavePaths);
        if (row == 1) return i18n::get(StrKey::SetScan);
        if (row == 2) return i18n::get(StrKey::SetBackupMax);
        return i18n::get(StrKey::SetBackupClean);
    }
    if (cat == 4) {
        if (row == 0) return i18n::get(StrKey::SetCheckUpdate);
        if (row == 1) return i18n::get(StrKey::SetSource);
        return i18n::get(StrKey::SetEditUrl);
    }
    if (cat == 5) {
        if (row == 0) return i18n::get(StrKey::SetDebugToggle);
        if (row == 1) return i18n::get(StrKey::SetDbgMenu);
        return i18n::get(StrKey::SendLogTitle);
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
        if (row == 0) return getThemeName(themeIndex_);
        if (row == 1) return langDisplayName(i18n::currentLang());
        return std::to_string(zoomGrow_) + "px";
    }
    if (cat == 2)
        return useOpenHome() ? i18n::get(StrKey::SetCoreOh) : i18n::get(StrKey::SetCorePk);
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
        if (row == 0) return "";
        if (row == 1) {
            // Solo GitHub/Custom, mai l'IP (quello sta nel prefill dell'edit).
            UpdateCfg cfg;
            readUpdateCfg(basePath_, cfg);
            return cfg.url.empty() ? "GitHub" : "Custom";
        }
        return "";
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
    std::ifstream f(basePath + "defaultuser.txt");
    std::string nick;
    if (f.good() && std::getline(f, nick)) {
        if (!nick.empty() && nick.back() == '\r') nick.pop_back();
        return nick;
    }
    return "";
}

// Menu debug rapido: ON = il gear apre il menu + classico, OFF = le impostazioni.
static bool readQuickMenu(const std::string& basePath) {
    std::ifstream f(basePath + "quickmenu.txt");
    std::string v;
    if (f.good() && std::getline(f, v)) return v == "1";
    return false;
}

static void writeQuickMenu(const std::string& basePath, bool on) {
    if (!on) {
        std::remove((basePath + "quickmenu.txt").c_str());
        return;
    }
    FILE* f = std::fopen((basePath + "quickmenu.txt").c_str(), "w");
    if (f) { std::fputs("1", f); std::fclose(f); }
}

static bool writeBackupMb(const std::string& basePath, long mb) {
    std::string path = basePath + "update.cfg";
    std::ifstream f(path);
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    if (f.good()) {
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto eq = line.find('=');
            std::string k = (eq == std::string::npos) ? line : line.substr(0, eq);
            if (k == "backup_mb") { line = "backup_mb=" + std::to_string(mb); found = true; }
            lines.push_back(line);
        }
    }
    if (!found) lines.push_back("backup_mb=" + std::to_string(mb));
    std::ofstream o(path, std::ios::trunc);
    if (!o.good()) return false;
    for (auto& l : lines) o << l << "\n";
    return true;
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
        if (next.empty()) {
            std::remove((basePath_ + "defaultuser.txt").c_str());
        } else {
            FILE* f = std::fopen((basePath_ + "defaultuser.txt").c_str(), "w");
            if (f) { std::fputs(next.c_str(), f); std::fclose(f); }
        }
    } else if (cat == 1) {
        if (row == 0) {
            themeIndex_ = (themeIndex_ + dir + THEME_COUNT) % THEME_COUNT;
            theme_ = &getTheme(themeIndex_);
            saveThemeIndex(basePath_, themeIndex_);
            clearTextCache();
        } else if (row == 1) {
            int n = (int)langList_.size();
            if (n > 0) {
                int cur = 0;
                for (int i = 0; i < n; i++)
                    if (langList_[i] == i18n::currentLang()) cur = i;
                std::string nl = langList_[(cur + dir + n) % n];
                i18n::init(nl);
                clearTextCache();
                FILE* f = std::fopen((basePath_ + "language.txt").c_str(), "w");
                if (f) { std::fputs(nl.c_str(), f); std::fclose(f); }
            }
        } else {
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
        }
    } else if (cat == 2) {
        setCryptoEngine(useOpenHome() ? CryptoEngine::PK : CryptoEngine::OH);
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
            if (!DebugLog::enabled()) return; // solo display senza debug
            // Switch GitHub <-> custom senza ridigitare: se non esiste alcun
            // file, apre direttamente l'edit per crearlo.
            std::string cfg, off;
            findUpdateCfgFiles(basePath_, cfg, off);
            if (cfg.empty() && off.empty()) {
                beginTextInput(TextInputPurpose::EditUpdateUrl);
            } else if (!cfg.empty()) {
                std::string dst = cfg + ".off";
                if (std::rename(cfg.c_str(), dst.c_str()) == 0)
                    DebugLog::line("settings: sorgente -> GitHub (%s disattivato)", cfg.c_str());
                else
                    showMessageAndWait(i18n::get(StrKey::SetTitle), std::string("rename FAIL:\n") + cfg);
            } else {
                std::string dst = off.substr(0, off.size() - 4);
                if (std::rename(off.c_str(), dst.c_str()) == 0)
                    DebugLog::line("settings: sorgente -> custom (%s)", dst.c_str());
                else
                    showMessageAndWait(i18n::get(StrKey::SetTitle), std::string("rename FAIL:\n") + off);
            }
        } else {
            beginTextInput(TextInputPurpose::EditUpdateUrl);
        }
    } else if (cat == 5) {
        if (row == 0) {
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
        } else {
            sendLogNow();
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
        if (setCat_ == 1 && r == 2) {
            // Slider zoom 0..16px con pallino.
            int bw = 120, bh = 8;
            int bx = popX + POP_W - 36 - bw;
            int by = rowY + ROW_H - 12;
            drawRect(bx, by, bw, bh, T().textDim);
            int dx = bx + (int)(bw * zoomGrow_ / 16.0) ;
            if (dx < bx) dx = bx;
            if (dx > bx + bw) dx = bx + bw;
            auto dot = [&](int cx, int cy, int rr, SDL_Color c) {
                SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                for (int dy = -rr; dy <= rr; dy++) {
                    int ddx = static_cast<int>(std::sqrt((double)(rr * rr - dy * dy)));
                    SDL_RenderDrawLine(renderer_, cx - ddx, cy + dy, cx + ddx, cy + dy);
                }
            };
            dot(dx, by + bh / 2, 7, T().selected);
        }
    }
    drawTextCentered(i18n::get(StrKey::SetFooter), popX + POP_W / 2, popY + POP_H - 20, T().textDim, fontSmall_);
}

void UI::handleSettingsInput(const SDL_Event& event, bool& running) {
    // Stick analogico: su/giu come il D-pad (con repeat), sulla colonna attiva.
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
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
                if (!setFocusLeft_) setFocusLeft_ = true;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (setFocusLeft_) { setFocusLeft_ = false; setRow_ = 0; }
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
