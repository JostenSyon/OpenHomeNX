#include "bank.h"
#include <fstream>
#include <cstring>

Bank::Bank() {
    slots_.resize(boxCount_ * slotsPerBox_);
    boxNames_.resize(boxCount_);
}

void Bank::makeCrossGen() {
    crossGen_    = true;
    gameType_    = GameType::S;   // pivot only — slots are OHPKM, not PK9
    boxCount_    = 32;
    slotsPerBox_ = 30;
    slotSize_    = PokeCrypto::SIZE_9PARTY;
    slots_.clear();
    ohpkmSlots_.assign((size_t)boxCount_ * slotsPerBox_, {});
    boxNames_.assign(boxCount_, std::string());
    dirty_       = true;
}

const std::vector<uint8_t>& Bank::ohpkmAt(int box, int slot) const {
    static const std::vector<uint8_t> kEmpty;
    if (!crossGen_) return kEmpty;
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= (int)ohpkmSlots_.size()) return kEmpty;
    return ohpkmSlots_[idx];
}

void Bank::setOhpkmAt(int box, int slot, std::vector<uint8_t> blob) {
    if (!crossGen_) return;
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= (int)ohpkmSlots_.size()) return;
    if (blob.size() > OHPKM_MAX_BLOB) return;
    ohpkmSlots_[idx] = std::move(blob);
    dirty_ = true;
}

void Bank::clearOhpkmAt(int box, int slot) {
    if (!crossGen_) return;
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= (int)ohpkmSlots_.size()) return;
    ohpkmSlots_[idx].clear();
    dirty_ = true;
}

void Bank::setGameType(GameType g) {
    gameType_ = g;
    auto& info   = gameInfo(g);
    boxCount_    = info.boxCount;
    slotsPerBox_ = info.slotsPerBox;
    slotSize_    = info.bankSlotSize;
    slots_.resize(boxCount_ * slotsPerBox_);
    boxNames_.resize(boxCount_);
}

bool Bank::isValidFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    // Verify magic
    char magic[8];
    if (!file.read(magic, 8))
        return false;
    if (std::memcmp(magic, MAGIC, 8) != 0)
        return false;

    // Verify version is one we understand
    uint32_t version = 0;
    if (!file.read(reinterpret_cast<char*>(&version), 4))
        return false;
    switch (version) {
        case VERSION_FRLG:
        case VERSION_GB:
        case VERSION_LGPE:
        case VERSION_LA:
        case VERSION_40BOX:
        case VERSION_32BOX:
        case VERSION_CROSSGEN:
            return true;
        default:
            return false;
    }
}

bool Bank::isCrossGenFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    char magic[8];
    if (!file.read(magic, 8) || std::memcmp(magic, MAGIC, 8) != 0)
        return false;
    uint32_t version = 0;
    if (!file.read(reinterpret_cast<char*>(&version), 4))
        return false;
    return version == VERSION_CROSSGEN;
}

bool Bank::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        // File doesn't exist - start with empty bank
        dirty_ = false;
        return true;
    }

    // Read and verify header
    char magic[8];
    file.read(magic, 8);
    if (std::memcmp(magic, MAGIC, 8) != 0) {
        return false; // Invalid file
    }

    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), 4);

    // This Bank object is reused across opens. Reset the cross-gen state up
    // front so a normal bank loaded right after a cross-gen one is not still
    // treated as OHPKM storage (which rendered every later bank empty / 0/960).
    crossGen_ = false;
    ohpkmSlots_.clear();

    if (version == VERSION_CROSSGEN) {
        crossGen_    = true;
        gameType_    = GameType::S;
        boxCount_    = 32;
        slotsPerBox_ = 30;
        slotSize_    = PokeCrypto::SIZE_9PARTY;
        slots_.clear();
        const int total = boxCount_ * slotsPerBox_;
        ohpkmSlots_.assign(total, {});
        boxNames_.assign(boxCount_, std::string());

        file.seekg(HEADER_SIZE);
        uint32_t slotCount = 0;
        if (!file.read(reinterpret_cast<char*>(&slotCount), 4)) { dirty_ = false; return true; }
        int n = (int)slotCount < total ? (int)slotCount : total;
        for (int i = 0; i < n; i++) {
            uint32_t len = 0;
            if (!file.read(reinterpret_cast<char*>(&len), 4)) { dirty_ = false; return true; }
            if (len == 0) continue;
            if (len > OHPKM_MAX_BLOB) { dirty_ = false; return true; } // corrupt — stop cleanly
            std::vector<uint8_t> blob(len);
            if (!file.read(reinterpret_cast<char*>(blob.data()), len)) { dirty_ = false; return true; }
            ohpkmSlots_[i] = std::move(blob);
        }
        for (int i = 0; i < boxCount_; i++) {
            char nameBuf[BOX_NAME_SIZE] = {};
            if (!file.read(nameBuf, BOX_NAME_SIZE)) break;
            int len = 0;
            while (len < BOX_NAME_SIZE && nameBuf[len] != '\0') len++;
            boxNames_[i] = std::string(nameBuf, len);
        }
        dirty_ = false;
        return true;
    }

    int fileBoxCount;
    int fileSlotSize;
    int fileSlotsPerBox;
    if (version == VERSION_FRLG) {
        fileBoxCount = 14;
        fileSlotSize = PokeCrypto::SIZE_3STORED;
        fileSlotsPerBox = 30;
    } else if (version == VERSION_GB) {
        fileBoxCount = 12;
        fileSlotSize = 55; // 33B record + 11B OT + 11B nick (see pokemon.h)
        fileSlotsPerBox = 20;
    } else if (version == VERSION_LGPE) {
        fileBoxCount = 40;
        fileSlotSize = PokeCrypto::SIZE_6PARTY;
        fileSlotsPerBox = 25;
    } else if (version == VERSION_LA) {
        fileBoxCount = 32;
        fileSlotSize = PokeCrypto::SIZE_8APARTY;
        fileSlotsPerBox = 30;
    } else if (version == VERSION_40BOX) {
        fileBoxCount = 40;
        fileSlotSize = PokeCrypto::SIZE_9PARTY;
        fileSlotsPerBox = 30;
    } else if (version == VERSION_32BOX) {
        fileBoxCount = 32;
        fileSlotSize = PokeCrypto::SIZE_9PARTY;
        fileSlotsPerBox = 30;
    } else {
        return false; // Unsupported version
    }

    // Use the file's parameters
    boxCount_ = fileBoxCount;
    slotSize_ = fileSlotSize;
    slotsPerBox_ = fileSlotsPerBox;
    slots_.resize(boxCount_ * slotsPerBox_);

    // Skip reserved
    file.seekg(HEADER_SIZE);

    // Read all slots (decrypted data)
    int total = totalSlots();
    for (int i = 0; i < total; i++) {
        file.read(reinterpret_cast<char*>(slots_[i].data.data()), slotSize_);
    }

    // Read box names if present (appended after slot data)
    boxNames_.resize(boxCount_);
    for (int i = 0; i < boxCount_; i++) {
        char nameBuf[BOX_NAME_SIZE] = {};
        if (!file.read(nameBuf, BOX_NAME_SIZE))
            break; // Old file without names — leave remaining as empty
        // Find null terminator or use full buffer
        int len = 0;
        while (len < BOX_NAME_SIZE && nameBuf[len] != '\0') len++;
        boxNames_[i] = std::string(nameBuf, len);
    }

    // Optional OHBKP section: per-slot OHPKM OriginalBackup blobs. Absent on
    // banks that never held a cross-gen mon; a truncated/corrupt section just
    // stops the read (the slot data above is already loaded).
    if (file.good()) {
        char bmagic[8] = {};
        if (file.read(bmagic, 8) && std::memcmp(bmagic, OHBKP_MAGIC, 8) == 0) {
            uint32_t count = 0;
            if (file.read(reinterpret_cast<char*>(&count), 4)) {
                const uint32_t total = static_cast<uint32_t>(totalSlots());
                for (uint32_t k = 0; k < count; k++) {
                    uint32_t slotIdx = 0, len = 0;
                    if (!file.read(reinterpret_cast<char*>(&slotIdx), 4)) break;
                    if (!file.read(reinterpret_cast<char*>(&len), 4)) break;
                    if (len == 0 || len > OHBKP_MAX_BLOB) break; // corrupt
                    std::vector<uint8_t> blob(len);
                    if (!file.read(reinterpret_cast<char*>(blob.data()), len)) break;
                    if (slotIdx < total)
                        slots_[slotIdx].ohBackup_ = std::move(blob);
                }
            }
        }
    }

    dirty_ = false;
    return true;
}

bool Bank::save(const std::string& path) {
    // One-level rolling backup of the previous file, so the last save can be
    // recovered (renamed .bak -> .bin) if a conversion result is unwanted.
    {
        std::ifstream prev(path, std::ios::binary);
        if (prev.is_open()) {
            std::ofstream bak(path + ".bak", std::ios::binary | std::ios::trunc);
            if (bak.is_open())
                bak << prev.rdbuf();
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    if (crossGen_) {
        file.write(MAGIC, 8);
        uint32_t ver = VERSION_CROSSGEN;
        file.write(reinterpret_cast<const char*>(&ver), 4);
        uint32_t reserved = 0;
        file.write(reinterpret_cast<const char*>(&reserved), 4);

        uint32_t slotCount = static_cast<uint32_t>(ohpkmSlots_.size());
        file.write(reinterpret_cast<const char*>(&slotCount), 4);
        for (const std::vector<uint8_t>& b : ohpkmSlots_) {
            uint32_t len = static_cast<uint32_t>(b.size());
            file.write(reinterpret_cast<const char*>(&len), 4);
            if (len)
                file.write(reinterpret_cast<const char*>(b.data()), len);
        }
        for (int i = 0; i < boxCount_; i++) {
            char nameBuf[BOX_NAME_SIZE] = {};
            if (i < (int)boxNames_.size())
                std::memcpy(nameBuf, boxNames_[i].c_str(),
                            std::min((int)boxNames_[i].size(), BOX_NAME_SIZE));
            file.write(nameBuf, BOX_NAME_SIZE);
        }
        bool ok = file.good();
        if (ok)
            dirty_ = false;
        return ok;
    }

    // Write header
    file.write(MAGIC, 8);
    uint32_t ver = fileVersion();
    file.write(reinterpret_cast<const char*>(&ver), 4);
    uint32_t reserved = 0;
    file.write(reinterpret_cast<const char*>(&reserved), 4);

    // Write all slots
    int total = totalSlots();
    for (int i = 0; i < total; i++) {
        file.write(reinterpret_cast<const char*>(slots_[i].data.data()), slotSize_);
    }

    // Write box names (16 bytes each, null-padded)
    for (int i = 0; i < boxCount_; i++) {
        char nameBuf[BOX_NAME_SIZE] = {};
        if (i < (int)boxNames_.size()) {
            std::memcpy(nameBuf, boxNames_[i].c_str(),
                        std::min((int)boxNames_[i].size(), BOX_NAME_SIZE));
        }
        file.write(nameBuf, BOX_NAME_SIZE);
    }

    // Optional OHBKP section: only present when a slot carries an OriginalBackup.
    uint32_t bkCount = 0;
    for (int i = 0; i < total; i++)
        if (!slots_[i].ohBackup_.empty()) bkCount++;
    if (bkCount > 0) {
        file.write(OHBKP_MAGIC, 8);
        file.write(reinterpret_cast<const char*>(&bkCount), 4);
        for (int i = 0; i < total; i++) {
            const std::vector<uint8_t>& b = slots_[i].ohBackup_;
            if (b.empty()) continue;
            uint32_t slotIdx = static_cast<uint32_t>(i);
            uint32_t len = static_cast<uint32_t>(b.size());
            file.write(reinterpret_cast<const char*>(&slotIdx), 4);
            file.write(reinterpret_cast<const char*>(&len), 4);
            file.write(reinterpret_cast<const char*>(b.data()), len);
        }
    }

    bool ok = file.good();
    if (ok)
        dirty_ = false;
    return ok;
}

Pokemon Bank::getSlot(int box, int slot) const {
    if (crossGen_) return Pokemon{}; // cross-gen slots are OHPKM, use ohpkmAt()
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= totalSlots())
        return Pokemon{};
    Pokemon pkm = slots_[idx];
    pkm.gameType_ = gameType_;
    return pkm;
}

void Bank::setSlot(int box, int slot, const Pokemon& pkm) {
    if (crossGen_) return;
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= totalSlots())
        return;
    slots_[idx] = pkm;
    dirty_ = true;
}

void Bank::clearSlot(int box, int slot) {
    if (crossGen_) { clearOhpkmAt(box, slot); return; }
    int idx = slotIndex(box, slot);
    if (idx < 0 || idx >= totalSlots())
        return;
    slots_[idx] = Pokemon{};
    dirty_ = true;
}

std::string Bank::getBoxName(int box) const {
    if (box >= 0 && box < (int)boxNames_.size() && !boxNames_[box].empty())
        return boxNames_[box];
    return "Bank " + std::to_string(box + 1);
}

void Bank::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= (int)boxNames_.size())
        return;
    if ((int)name.size() > BOX_NAME_SIZE)
        boxNames_[box] = name.substr(0, BOX_NAME_SIZE);
    else
        boxNames_[box] = name;
    dirty_ = true;
}
