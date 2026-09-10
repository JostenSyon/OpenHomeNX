#include "account.h"
#include "debug_log.h"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>

bool AccountManager::init() {
    return R_SUCCEEDED(accountInitialize(AccountServiceType_Administrator));
}

void AccountManager::shutdown() {
    unmountSave();
    freeTextures();
    accountExit();
}

bool AccountManager::loadProfiles(SDL_Renderer* renderer) {
    AccountUid accounts[8];
    int32_t total = 0;
    Result rc = accountListAllUsers(accounts, 8, &total);
    if (R_FAILED(rc) || total <= 0)
        return false;

    users_.clear();
    for (int i = 0; i < total; i++) {
        UserProfile profile;
        if (loadProfile(accounts[i], renderer, profile))
            users_.push_back(std::move(profile));
    }
    return !users_.empty();
}

bool AccountManager::loadProfile(AccountUid uid, SDL_Renderer* renderer, UserProfile& out) {
    out.uid = uid;

    AccountProfile profile;
    if (R_FAILED(accountGetProfile(&profile, uid)))
        return false;

    // Get nickname
    AccountProfileBase base;
    if (R_FAILED(accountProfileGet(&profile, nullptr, &base))) {
        accountProfileClose(&profile);
        return false;
    }
    out.nickname = base.nickname;
    out.pathSafeName = sanitizeForPath(base.nickname);

    // Load icon (JPEG)
    u32 imgSize = 0;
    if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
        std::vector<uint8_t> jpegBuf(imgSize);
        u32 actualSize = 0;
        if (R_SUCCEEDED(accountProfileLoadImage(&profile, jpegBuf.data(), imgSize, &actualSize))) {
            SDL_RWops* rw = SDL_RWFromMem(jpegBuf.data(), (int)actualSize);
            if (rw) {
                SDL_Surface* surf = IMG_Load_RW(rw, 1); // 1 = auto-close rw
                if (surf) {
                    // Quadrata originale per il menu profili.
                    out.iconTexture = SDL_CreateTextureFromSurface(renderer, surf);
                    // Copia con maschera circolare (raggio 46%) per la home.
                    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
                    SDL_FreeSurface(surf);
                    surf = rgba;
                }
                if (surf) {
                    int side = surf->w < surf->h ? surf->w : surf->h;
                    int r = (int)(0.46 * side);
                    int cx = surf->w / 2, cy = surf->h / 2;
                    int r2 = r * r;
                    SDL_LockSurface(surf);
                    Uint32* px = (Uint32*)surf->pixels;
                    int stride = surf->pitch / 4;
                    for (int y = 0; y < surf->h; y++) {
                        int dy = y - cy;
                        for (int x = 0; x < surf->w; x++) {
                            int dx = x - cx;
                            if (dx * dx + dy * dy > r2)
                                px[y * stride + x] &= 0x00FFFFFF; // alpha=0 (RGBA32 LE)
                        }
                    }
                    SDL_UnlockSurface(surf);
                    out.iconTextureRound = SDL_CreateTextureFromSurface(renderer, surf);
                    if (out.iconTextureRound)
                        SDL_SetTextureBlendMode(out.iconTextureRound, SDL_BLENDMODE_BLEND);
                    SDL_FreeSurface(surf);
                }
            }
        }
    }

    accountProfileClose(&profile);
    return true;
}

std::string AccountManager::sanitizeForPath(const char* nickname) {
    std::string result;
    for (const char* p = nickname; *p; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        if (c >= 0x20)
            result += c;
    }
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos) return "User";
    size_t end = result.find_last_not_of(' ');
    result = result.substr(start, end - start + 1);
    if (result.empty()) return "User";
    if (result.size() > 32) result = result.substr(0, 32);
    return result;
}

// Enumerate every account savedata that exists for `uid` and cache the set of
// application ids. One pass, standard batched FsSaveDataInfoReader loop.
void AccountManager::populateSaveCache(AccountUid uid) const {
    cachedAppIds_.clear();
    cachedUid_ = uid;
    saveCachePopulated_ = true;

    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        DebugLog::line("games: fsOpenSaveDataInfoReader failed");
        return;
    }

    constexpr s64 BATCH = 64;
    FsSaveDataInfo infos[BATCH];
    for (;;) {
        s64 read = 0;
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, infos, BATCH, &read)) || read <= 0)
            break;
        for (s64 i = 0; i < read; i++) {
            if (infos[i].save_data_type != FsSaveDataType_Account)
                continue;
            if (std::memcmp(&infos[i].uid, &uid, sizeof(AccountUid)) != 0)
                continue;
            cachedAppIds_.insert(infos[i].application_id);
        }
        if (read < BATCH)
            break;
    }
    fsSaveDataInfoReaderClose(&reader);
    DebugLog::line("games: %zu account save(s) for this profile", cachedAppIds_.size());
}

std::set<uint64_t> AccountManager::presentApplications() const {
    std::set<uint64_t> out;

    // (a) every account savedata on the console, any user.
    FsSaveDataInfoReader reader;
    if (R_SUCCEEDED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        constexpr s64 BATCH = 64;
        FsSaveDataInfo infos[BATCH];
        for (;;) {
            s64 read = 0;
            if (R_FAILED(fsSaveDataInfoReaderRead(&reader, infos, BATCH, &read)) || read <= 0)
                break;
            for (s64 i = 0; i < read; i++)
                if (infos[i].save_data_type == FsSaveDataType_Account && infos[i].application_id)
                    out.insert(infos[i].application_id);
            if (read < BATCH)
                break;
        }
        fsSaveDataInfoReaderClose(&reader);
    }

    // (b) every installed application.
    if (R_SUCCEEDED(nsInitialize())) {
        constexpr s32 BATCH = 32;
        NsApplicationRecord recs[BATCH];
        s32 offset = 0;
        for (;;) {
            s32 count = 0;
            if (R_FAILED(nsListApplicationRecord(recs, BATCH, offset, &count)) || count <= 0)
                break;
            for (s32 i = 0; i < count; i++)
                out.insert(recs[i].application_id);
            offset += count;
            if (count < BATCH)
                break;
        }
        nsExit();
    }

    DebugLog::line("games: %zu present application(s)", out.size());
    return out;
}

bool AccountManager::hasSaveData(int profileIndex, GameType game) const {
    if (profileIndex < 0 || profileIndex >= (int)users_.size())
        return false;

    const AccountUid uid = users_[profileIndex].uid;
    if (!saveCachePopulated_ || std::memcmp(&cachedUid_, &uid, sizeof(AccountUid)) != 0)
        populateSaveCache(uid);

    return cachedAppIds_.count(titleIdOf(game)) > 0;
}

std::string AccountManager::mountSave(int profileIndex, GameType game) {
    if (profileIndex < 0 || profileIndex >= (int)users_.size())
        return "";

    if (mounted_)
        unmountSave();

    FsSaveDataAttribute attr{};
    attr.application_id = titleIdOf(game);
    attr.uid = users_[profileIndex].uid;
    attr.save_data_type = FsSaveDataType_Account;

    Result rc = fsOpenSaveDataFileSystem(&saveFs_, FsSaveDataSpaceId_User, &attr);
    if (R_FAILED(rc))
        return "";

    int devRc = fsdevMountDevice("save", saveFs_);
    if (devRc < 0) {
        fsFsClose(&saveFs_);
        return "";
    }

    mounted_ = true;
    return "save:/";
}

void AccountManager::unmountSave() {
    if (mounted_) {
        fsdevUnmountDevice("save");
        mounted_ = false;
    }
}

void AccountManager::commitSave() {
    if (mounted_)
        fsdevCommitDevice("save");
}

bool AccountManager::backupSaveDir(const std::string& srcDir, const std::string& dstDir) {
    // Create backup directory recursively
    std::string path;
    for (size_t i = 0; i < dstDir.size(); i++) {
        path += dstDir[i];
        if (dstDir[i] == '/' && i > 0)
            mkdir(path.c_str(), 0755);
    }
    mkdir(dstDir.c_str(), 0755);

    DIR* dir = opendir(srcDir.c_str());
    if (!dir)
        return false;

    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;

        std::string srcPath = srcDir + entry->d_name;
        std::string dstPath = dstDir + entry->d_name;

        // Check if it's a directory
        struct stat st;
        if (stat(srcPath.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            backupSaveDir(srcPath + "/", dstPath + "/");
            continue;
        }

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
    if (!dir)
        return 0;

    size_t total = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;

        std::string path = dirPath + entry->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
            total += calculateDirSize(path + "/");
        else
            total += st.st_size;
    }

    closedir(dir);
    return total;
}

void AccountManager::freeTextures() {
    for (auto& user : users_) {
        if (user.iconTexture) {
            SDL_DestroyTexture(user.iconTexture);
            user.iconTexture = nullptr;
        }
        if (user.iconTextureRound) {
            SDL_DestroyTexture(user.iconTextureRound);
            user.iconTextureRound = nullptr;
        }
    }
}
