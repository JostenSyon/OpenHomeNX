#pragma once
#include "game_type.h"
#include <SDL2/SDL.h>
#include <switch.h>
#include <string>
#include <vector>
#include <set>
#include <cstdint>

struct UserProfile {
    std::string nickname;
    std::string pathSafeName;
    AccountUid uid{};
    SDL_Texture* iconTexture = nullptr;
    SDL_Texture* iconTextureRound = nullptr; // maschera circolare (home)
};

class AccountManager {
public:
    bool init();
    void shutdown();

    // Load all Switch profiles (names + icons). Returns false if no profiles.
    bool loadProfiles(SDL_Renderer* renderer);

    // Check if a profile has save data for a given game.
    bool hasSaveData(int profileIndex, GameType game) const;

    // Application ids that are "present" on the console: any account savedata
    // (any user) plus every installed application. Used to filter the game
    // list in applet mode, where there is no selected profile and per-title
    // save probes give false positives.
    std::set<uint64_t> presentApplications() const;

    // Mount save filesystem for profile + game. Returns "save:/" on success, "" on failure.
    std::string mountSave(int profileIndex, GameType game);
    void unmountSave();

    // Commit writes to mounted save (required after save_.save() on Switch).
    void commitSave();

    // Copy all files from a save directory to a backup directory.
    static bool backupSaveDir(const std::string& srcDir, const std::string& dstDir);

    // Recursively calculate total size of all files in a directory.
    static size_t calculateDirSize(const std::string& dir);

    const std::vector<UserProfile>& profiles() const { return users_; }
    int profileCount() const { return (int)users_.size(); }

    void freeTextures();

private:
    std::vector<UserProfile> users_;
    bool mounted_ = false;

    FsFileSystem saveFs_{};

    // hasSaveData() probed one fake title id at a time with
    // fsOpenSaveDataFileSystem, which gives false positives under hbloader's
    // title-override context (games that aren't installed showed up) and false
    // negatives from a forwarder. Instead we enumerate the account savedata
    // that actually exists for the profile once and cache the application ids.
    mutable std::set<uint64_t> cachedAppIds_;
    mutable AccountUid cachedUid_{};
    mutable bool saveCachePopulated_ = false;
    void populateSaveCache(AccountUid uid) const;

    bool loadProfile(AccountUid uid, SDL_Renderer* renderer, UserProfile& out);
    static std::string sanitizeForPath(const char* nickname);
};
