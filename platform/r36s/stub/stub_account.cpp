// OpenHomeNX - implementazione AccountManager per build Linux/macOS.
// Su Linux/R36S non esistono i profili utente Switch né i save di sistema
// montati da libnx: i save sono file su path locale. loadProfiles() non trova
// profili (l'app procede in modalita' "applet"/file-backed), mountSave() fallisce
// sempre e il codice usa i path locali. backupSaveDir/calculateDirSize restano
// reali (sono gia' POSIX e servono per i backup locali).
#include "account.h"
#include "debug_log.h"
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>

bool AccountManager::init() {
    DebugLog::line("account: build Linux, profili Switch non disponibili");
    return true;
}

void AccountManager::shutdown() {
    unmountSave();
    freeTextures();
}

bool AccountManager::loadProfiles(SDL_Renderer*) {
    users_.clear();
    return false; // nessun profilo su Linux
}

bool AccountManager::loadProfile(AccountUid, SDL_Renderer*, UserProfile&) { return false; }

std::string AccountManager::sanitizeForPath(const char* nickname) {
    std::string result;
    for (const char* p = nickname; *p; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        if (c >= 0x20) result += c;
    }
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos) return "User";
    size_t end = result.find_last_not_of(' ');
    result = result.substr(start, end - start + 1);
    if (result.empty()) return "User";
    if (result.size() > 32) result = result.substr(0, 32);
    return result;
}

void AccountManager::populateSaveCache(AccountUid) const {}

std::set<uint64_t> AccountManager::presentApplications() const {
    return {};
}

bool AccountManager::hasSaveData(int profileIndex, GameType) const {
    return false;
}

std::string AccountManager::mountSave(int, GameType) {
    return ""; // mai montato su Linux
}

void AccountManager::unmountSave() {
    mounted_ = false;
}

bool AccountManager::commitSave() {
    return true; // niente da committare
}

bool AccountManager::backupSaveDir(const std::string& srcDir, const std::string& dstDir) {
    std::string path;
    for (size_t i = 0; i < dstDir.size(); i++) {
        path += dstDir[i];
        if (dstDir[i] == '/' && i > 0)
            mkdir(path.c_str(), 0755);
    }
    mkdir(dstDir.c_str(), 0755);

    DIR* dir = opendir(srcDir.c_str());
    if (!dir) return false;

    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string srcPath = srcDir + entry->d_name;
        std::string dstPath = dstDir + entry->d_name;
        struct stat st;
        if (stat(srcPath.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { backupSaveDir(srcPath + "/", dstPath + "/"); continue; }
        std::ifstream src(srcPath, std::ios::binary);
        if (!src.is_open()) { ok = false; continue; }
        std::ofstream dst(dstPath, std::ios::binary);
        if (!dst.is_open()) { ok = false; continue; }
        constexpr size_t BUF_SIZE = 65536;
        char buf[BUF_SIZE];
        while (src.read(buf, BUF_SIZE) || src.gcount() > 0)
            dst.write(buf, src.gcount());
        if (!dst.good()) ok = false;
    }
    closedir(dir);
    return ok;
}

size_t AccountManager::calculateDirSize(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return 0;
    size_t total = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string path = dirPath + entry->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += calculateDirSize(path + "/");
        else total += st.st_size;
    }
    closedir(dir);
    return total;
}

void AccountManager::freeTextures() {
    for (auto& user : users_) {
        if (user.iconTexture) { SDL_DestroyTexture(user.iconTexture); user.iconTexture = nullptr; }
        if (user.iconTextureRound) { SDL_DestroyTexture(user.iconTextureRound); user.iconTextureRound = nullptr; }
    }
}