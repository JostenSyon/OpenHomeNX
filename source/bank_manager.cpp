#include "bank_manager.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_map>

bool BankManager::init(const std::string& basePath, GameType game) {
    basePath_ = basePath;
    game_ = game;
    allMode_ = false;

    // Create banks/ parent directory
    std::string banksParent = basePath + "banks/";
    mkdir(banksParent.c_str(), 0755);

    // Game-specific subdirectory (paired games share a folder)
    std::string folderName = bankFolderNameOf(game);
#ifdef OH_LINUX
    for (auto& c : folderName) c = (char)tolower((unsigned char)c);
#endif
    banksDir_ = banksParent + folderName + "/";

    mkdir(banksDir_.c_str(), 0755);

    // Migrate legacy bank.bin only for ZA
    if (game == GameType::ZA)
        migrateLegacy();

    refresh();
    return true;
}

void BankManager::refreshAll() {
    bankList_.clear();
    const std::string& basePath = basePath_;

    // Prima girava su una lista fissa di 7 GameType "rappresentativi" (solo
    // le famiglie native Switch: LetsGo/SwSh/BDSP/LA/SV/ZA/FRLG) — qualsiasi
    // gioco import da file (GBA/GB/GBC/DS/3DS: Ruby/Sapphire/Emerald,
    // Red/Blue/Yellow, Gold/Silver/Crystal, Diamond/Pearl/Platinum/HGSS,
    // Black/White/B2W2, X/Y/OmegaRuby/AlphaSapphire/Sun/Moon/UltraSun/
    // UltraMoon) non era in quella lista, quindi la sua cartella banche non
    // veniva mai guardata -- appariva come "nessuna banca" anche con banche
    // gia' create su disco. Stesso bug su Switch, solo meno visibile li'
    // perche' la maggior parte usa titoli nativi; su R36S la libreria e'
    // fatta solo di giochi import, quindi sempre riproducibile. Fix: invece
    // di elencare a mano i GameType, si legge davvero cosa c'e' sotto
    // banks/ e si risolve ogni sottocartella al suo GameType tramite
    // bankFolderNameOf() (case-insensitive, copre anche il lowercasing
    // R36S) -- funziona per ogni gioco presente e futuro senza lista da
    // mantenere.
    std::unordered_map<std::string, GameType> folderToGame;
    for (int i = 0; i < GAME_TYPE_COUNT; i++) {
        GameType g = static_cast<GameType>(i);
        std::string key = bankFolderNameOf(g);
        for (auto& c : key) c = (char)tolower((unsigned char)c);
        folderToGame.emplace(key, g); // il primo GameType vince per cartelle condivise (es. FR/LG)
    }

    std::string banksParent = basePath + "banks/";
    DIR* parent = opendir(banksParent.c_str());
    if (!parent) return;

    struct dirent* pe;
    while ((pe = readdir(parent)) != nullptr) {
        std::string folderName = pe->d_name;
        if (folderName == "." || folderName == "..") continue;
        std::string dir = banksParent + folderName + "/";
        struct stat pst;
        if (stat(dir.c_str(), &pst) != 0 || !S_ISDIR(pst.st_mode)) continue;

        std::string key = folderName;
        for (auto& c : key) c = (char)tolower((unsigned char)c);
        auto it = folderToGame.find(key);
        if (it == folderToGame.end()) continue; // cartella sconosciuta (non un bankFolderName valido)
        GameType g = it->second;

        DIR* d = opendir(dir.c_str());
        if (!d) continue;

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() < 5 || name.substr(name.size() - 4) != ".bin")
                continue;

            std::string fullPath = dir + name;

            BankInfo info;
            info.name = name.substr(0, name.size() - 4);
            info.fullPath = fullPath;
            info.valid = Bank::isValidFile(fullPath);
            info.crossGen = info.valid && Bank::isCrossGenFile(fullPath);
            info.occupiedSlots = info.valid ? countOccupied(fullPath) : 0;
            info.game = g;
            bankList_.push_back(info);
        }
        closedir(d);
    }
    closedir(parent);

    // Sort by game card order then alphabetically by name
    auto gameOrder = [](GameType g) -> int {
        // Paired games share the same order index
        if (isLGPE(g)) return 0;
        if (isSwSh(g)) return 1;
        if (isBDSP(g)) return 2;
        if (g == GameType::LA) return 3;
        if (isSV(g))   return 4;
        if (g == GameType::ZA) return 5;
        if (isFRLG(g) || isImportedFile(g) || isGen1File(g) || isGen2File(g)) return 6;
        return 7;
    };
    std::sort(bankList_.begin(), bankList_.end(), [&](const BankInfo& a, const BankInfo& b) {
        if (a.crossGen != b.crossGen) return a.crossGen; // Cross-gen section first
        if (!a.crossGen) {
            int oa = gameOrder(a.game), ob = gameOrder(b.game);
            if (oa != ob) return oa < ob;
        }
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
        return la < lb;
    });
}

bool BankManager::initAll(const std::string& basePath) {
    basePath_ = basePath;
    allMode_ = true;
    refreshAll();
    return true;
}

bool BankManager::migrateLegacy() {
    std::string legacyPath = basePath_ + "bank.bin";
    std::string newPath = banksDir_ + "Default.bin";

    // Check if legacy file exists
    struct stat st;
    if (stat(legacyPath.c_str(), &st) != 0)
        return false;  // no legacy file

    // Don't overwrite if Default.bin already exists
    if (stat(newPath.c_str(), &st) == 0)
        return false;

    return std::rename(legacyPath.c_str(), newPath.c_str()) == 0;
}

void BankManager::refresh() {
    bankList_.clear();

    DIR* dir = opendir(banksDir_.c_str());
    if (!dir)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Only .bin files
        if (name.size() < 5)
            continue;
        if (name.substr(name.size() - 4) != ".bin")
            continue;

        std::string stem = name.substr(0, name.size() - 4);
        std::string fullPath = banksDir_ + name;

        BankInfo info;
        info.name = stem;
        info.fullPath = fullPath;
        info.valid = Bank::isValidFile(fullPath);
        info.crossGen = info.valid && Bank::isCrossGenFile(fullPath);
        info.occupiedSlots = info.valid ? countOccupied(fullPath) : 0;
        info.game = game_;
        bankList_.push_back(info);
    }
    closedir(dir);

    // Sort alphabetically (case-insensitive)
    std::sort(bankList_.begin(), bankList_.end(), [](const BankInfo& a, const BankInfo& b) {
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
        return la < lb;
    });
}

const std::vector<BankInfo>& BankManager::list() const {
    return bankList_;
}

int BankManager::countOccupied(const std::string& filePath) {
    Bank temp;
    if (!temp.load(filePath))
        return 0;

    int count = 0;
    for (int box = 0; box < temp.boxCount(); box++) {
        for (int slot = 0; slot < temp.slotsPerBox(); slot++) {
            // Cross-gen banks keep OHPKM blobs, not decrypted PK records —
            // getSlot() is always empty for them, so count the blobs instead.
            if (temp.isCrossGen()) {
                if (!temp.ohpkmAt(box, slot).empty())
                    count++;
            } else if (!temp.getSlot(box, slot).isEmpty()) {
                count++;
            }
        }
    }
    return count;
}

int BankManager::countBanks(const std::string& basePath, GameType game) {
    std::string dir = basePath + "banks/" + bankFolderNameOf(game) + "/";
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() >= 5 && name.substr(name.size() - 4) == ".bin" &&
            Bank::isValidFile(dir + name))
            count++;
    }
    closedir(d);
    return count;
}

bool BankManager::createBank(const std::string& name, bool crossGen) {
    std::string safe = sanitizeName(name);
    if (safe.empty())
        return false;

    std::string path = banksDir_ + safe + ".bin";

    // Don't overwrite existing bank
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return false;

    // Create an empty bank file
    Bank empty;
    if (crossGen)
        empty.makeCrossGen();
    if (!empty.save(path))
        return false;

    if (allMode_) refreshAll();
    else refresh();
    return true;
}

bool BankManager::deleteBank(const std::string& name) {
    // Find the bank entry so we know both its path and which game it belongs to
    const BankInfo* info = nullptr;
    for (const auto& b : bankList_) {
        if (b.name == name) { info = &b; break; }
    }
    if (!info)
        return false;

    // Soft delete: move the file into a trash area instead of removing it,
    // preserving the per-game folder layout so it stays recoverable:
    //   banks/<game>/Name.bin  ->  banks/trash/<game>/Name.bin
    std::string trashParent = basePath_ + "banks/" + TRASH_DIR + "/";
    mkdir(trashParent.c_str(), 0755);
    std::string trashDir = trashParent + bankFolderNameOf(info->game) + "/";
    mkdir(trashDir.c_str(), 0755);

    // Avoid clobbering a previously deleted bank of the same name
    std::string destPath = trashDir + name + ".bin";
    struct stat st;
    for (int i = 2; stat(destPath.c_str(), &st) == 0; i++)
        destPath = trashDir + name + " (" + std::to_string(i) + ").bin";

    if (std::rename(info->fullPath.c_str(), destPath.c_str()) != 0)
        return false;

    if (allMode_) refreshAll();
    else refresh();
    return true;
}

bool BankManager::bankExists(const std::string& name) const {
    std::string safe = sanitizeName(name);
    if (safe.empty())
        return false;

    std::string path = banksDir_ + safe + ".bin";
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool BankManager::renameBank(const std::string& oldName, const std::string& newName) {
    std::string safe = sanitizeName(newName);
    if (safe.empty())
        return false;

    std::string oldPath = pathFor(oldName);
    if (oldPath.empty())
        return false;

    std::string newPath = banksDir_ + safe + ".bin";

    // Don't overwrite existing bank
    struct stat st;
    if (stat(newPath.c_str(), &st) == 0)
        return false;

    if (std::rename(oldPath.c_str(), newPath.c_str()) != 0)
        return false;

    if (allMode_) refreshAll();
    else refresh();
    return true;
}

std::string BankManager::loadBank(const std::string& name, Bank& bank) {
    std::string path = pathFor(name);
    if (path.empty())
        return "";

    bank.load(path);
    return path;
}

std::string BankManager::pathFor(const std::string& name) const {
    for (const auto& info : bankList_) {
        if (info.name == name)
            return info.fullPath;
    }
    return "";
}

int BankManager::totalVisualRows() const {
    if (!allMode_ || bankList_.empty()) return (int)bankList_.size();
    int headers = 0;
    for (int i = 0; i < (int)bankList_.size(); i++) {
        if (i == 0 || !bankSameSection(bankList_[i], bankList_[i - 1]))
            headers++;
    }
    return (int)bankList_.size() + headers;
}

std::string BankManager::sanitizeName(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());

    for (char c : raw) {
        // Strip invalid filesystem characters
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        result += c;
    }

    // Trim leading/trailing whitespace
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos)
        return "";
    size_t end = result.find_last_not_of(' ');
    result = result.substr(start, end - start + 1);

    // Limit to 32 characters
    if (result.size() > 32)
        result = result.substr(0, 32);

    return result;
}
