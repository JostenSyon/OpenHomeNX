#include "save_file.h"
#include "save_file_ffi.h"
#include "poke_crypto.h"
#include "gen1_tables.h"
#include "handler_update.h"
#include "openhome_ffi.h"
#include "pokedex.h"
#include "binary_io.h"
#include "md5.h"
#include "debug_log.h"
#include <fstream>
#include <cstdio>
#include <cstring>

void SaveFile::setGameType(GameType game) {
    gameType_ = game;
    invalidateAllBoxCache();
    auto& info   = gameInfo(game);
    gapBoxSlot_  = info.saveGapSize;
    sizeBoxSlot_ = info.saveSlotSize;
    boxCount_    = info.boxCount;
    slotsPerBox_ = info.slotsPerBox;
    kbox_        = (game == GameType::LA) ? 0x47E1CEAB : 0x0d66012c;
}

bool SaveFile::isBDSPSize(size_t size) {
    return size == 0xE9828  // v1.0
        || size == 0xEDC20  // v1.1
        || size == 0xEED8C  // v1.2
        || size == 0xEF0A4; // v1.3
}

bool SaveFile::load(const std::string& path) {
    filePath_ = path;
    loaded_ = false;
    dirty_ = false;   // fresh state; the load* helpers write buffers directly, not via the marked mutators
    boxData_ = nullptr;
    boxLayoutData_ = nullptr;
    invalidateAllBoxCache();

    DebugLog::line("load: path=%s gameType=%d engine=%s",
                   path.c_str(), (int)gameType_, useOpenHome() ? "OH" : "PK");

    // Drop any Rust OH handle from a PREVIOUS save/game. `SaveFile` is reused
    // across game switches; without this a stale SwSh handle would survive into
    // a BDSP/SV/LGPE load (loadBDSP/loadLGPE/loadGBA don't touch it) and
    // getCachedBox()/parity would read the wrong game's boxes.
    if (saveHandleRust_)
        saveHandleRust_.reset();

    if (isFRLG(gameType_) || isImportedFile(gameType_))
        return loadGBA(path);
    if (isGen1File(gameType_))
        return loadGB(path);
    if (isGen2File(gameType_))
        return loadGBC(path);
    if (isGen4File(gameType_))
        return loadDS4(path);
    if (isGen5File(gameType_))
        return loadDS5(path);
    if (isBDSP(gameType_))
        return loadBDSP(path);
    if (isLGPE(gameType_))
        return loadLGPE(path);
    return loadSCBlock(path);
}

bool SaveFile::save(const std::string& path) {
    if (!loaded_)
        return false;

    bool ok;
    if (isGen1File(gameType_))
        ok = saveGB(path);
    else if (isGen2File(gameType_))
        ok = saveGBC(path);
    else if (isGen45File(gameType_)) {
        // Read-only v1: never write what the layout code cannot re-checksum
        // (Gen5 block footers especially). Explicit failure, never silent.
        DebugLog::line("save: Gen4/5 read-only v1, rifiuto scrittura %s", path.c_str());
        ok = false;
    }
    else if (isFRLG(gameType_) || isImportedFile(gameType_))
        ok = saveGBA(path);
    else if (isBDSP(gameType_))
        ok = saveBDSP(path);
    else if (isLGPE(gameType_))
        ok = saveLGPE(path);
    else
        ok = saveSCBlock(path);

    if (ok)
        dirty_ = false;
    return ok;
}

bool SaveFile::loadSCBlock(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> fileData(fileSize);
    file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    file.close();

    // Save original file data for round-trip verification (decrypt modifies in-place)
    originalFileData_ = fileData;

    // Decrypt into SCBlocks (via FFI wrapper, M3a)
    blocks_ = SaveFileFFI::decrypt(fileData.data(), fileData.size());

    // Find box data block
    SCBlock* boxBlock = SaveFileFFI::findBlock(blocks_, kbox_);
    if (!boxBlock)
        return false;
    boxData_ = boxBlock->data.data();
    boxDataLen_ = boxBlock->data.size();

    // Find box layout block (box names)
    SCBlock* layoutBlock = SaveFileFFI::findBlock(blocks_, KBOX_LAYOUT);
    if (layoutBlock) {
        boxLayoutData_ = layoutBlock->data.data();
        boxLayoutLen_ = layoutBlock->data.size();
    }

    loaded_ = true;

    // Create Rust save handle for OH path (used by getCachedBox).
    // ONLY for SwSh — è l'unico formato che il core Rust legge davvero.
    // Per gli altri (SV/BDSP/LA/LGPE/FRLG) l'handle resta nullptr e
    // getCachedBox + il parity check usano il path PK. (openhome_load_save
    // ritorna comunque NULL per non-SwSh, questa è la guardia esplicita.)
    if (useOpenHome() && isSwSh(gameType_))
        saveHandleRust_.reset(SaveFileFFI::load(path));

    return true;
}

std::string SaveFile::debugCompareEnginesParity() const {
    if (!saveHandleRust_) return std::string();

    DebugLog::line("PK vs OH Parity scan started");

    // Region the app actually reads/persists per slot (stored bytes, no gap).
    const int region = sizeBoxSlot_ - gapBoxSlot_;
    int confrontati = 0, identici = 0, differenti = 0, ohVuoti = 0;
    std::string detail;

    for (int box = 0; box < boxCount_; ++box) {
        for (int s = 0; s < slotsPerBox_; ++s) {
            int off = getBoxSlotOffset(box, s);
            if (off + sizeBoxSlot_ > static_cast<int>(boxDataLen_)) continue;

            Pokemon pk;
            pk.gameType_ = gameType_;
            pk.loadFromEncrypted(boxData_ + off, region);

            PkmHandle* h = SaveFileFFI::getSlot(saveHandleRust_.get(), box, s);
            std::vector<uint8_t> oh;
            if (h) { oh = OpenHomeNX::getPkmBoxBytes(h); OpenHomeNX::freePkm(h); }

            if (pk.isEmpty() && oh.empty()) continue;

            if (oh.empty()) {
                // PK ha un Pokémon, OH non restituisce dati (slot mancante o
                // errore di conversione Rust) — conta come divergenza.
                confrontati++; differenti++; ohVuoti++;
                fprintf(stderr, "[PARITY] box %d slot %d: OH nessun dato, PK non vuoto\n", box, s);
                DebugLog::line("b%d s%d: OH vuoto", box, s);
                if (detail.size() < 400) {
                    char line[64];
                    std::snprintf(line, sizeof(line), "b%d s%d: OH vuoto\n", box, s);
                    detail += line;
                }
                continue;
            }

            confrontati++;
            size_t N = std::min(oh.size(), static_cast<size_t>(region));
            int ndiff = 0, firstOff = -1;
            int diffOff[8], diffOh[8], diffPk[8];
            int nDiffs = 0;
            for (size_t i = 0; i < N; ++i) {
                if (oh[i] != pk.data[i]) {
                    ndiff++;
                    if (firstOff < 0) firstOff = static_cast<int>(i);
                    if (nDiffs < 8) {
                        diffOff[nDiffs] = static_cast<int>(i);
                        diffOh[nDiffs] = oh[i];
                        diffPk[nDiffs] = pk.data[i];
                        nDiffs++;
                    }
                }
            }

            if (ndiff == 0) { identici++; continue; }

            differenti++;
            fprintf(stderr, "[PARITY] box %d slot %d: %d/%zu byte differ, primo off %d\n",
                    box, s, ndiff, N, firstOff);
                        DebugLog::line("b%d s%d: %d byte @%d+ (region=%d)", box, s, ndiff, firstOff, (int)N);
            for (size_t i = 0; i < N; ++i) {
                if (oh[i] != pk.data[i])
                    DebugLog::line("  +0x%X OH=%02X PK=%02X", (int)i, oh[i], pk.data[i]);
            }
            if (detail.size() < 400) {
                char line[128];
                std::snprintf(line, sizeof(line), "b%d s%d: %d byte @%d+\n", box, s, ndiff, firstOff);
                detail += line;
                for (int d = 0; d < nDiffs; d++) {
                    char dline[96];
                    std::snprintf(dline, sizeof(dline), "  +0x%X OH=%02X PK=%02X\n",
                                  diffOff[d], diffOh[d], diffPk[d]);
                    detail += dline;
                }
            }
        }
    }

    fprintf(stderr, "[PARITY] totale: %d confrontati, %d identici, %d differenti (OH vuoti: %d)\n",
            confrontati, identici, differenti, ohVuoti);
    DebugLog::line("totale: %d confrontati, %d identici, %d differenti (OH vuoti: %d)",
                   confrontati, identici, differenti, ohVuoti);

    if (differenti == 0) return std::string();
    char head[192];
    std::snprintf(head, sizeof(head),
        "Regione %d B/slot\nConfrontati: %d\nIdentici: %d\nDifferenti: %d\n",
        region, confrontati, identici, differenti);
    std::string out = head;
    out += "\nPrimi slot divergenti:\n"; out += detail;
    return out;
}

bool SaveFile::saveSCBlock(const std::string& path) {
    std::vector<uint8_t> encrypted = SaveFileFFI::encrypt(blocks_);

    // Open for in-place writing (r+b) to avoid truncating the file.
    // The Switch save filesystem journal can break if we truncate + rewrite.
    // Our encrypted output is always the exact same size as the original.
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) {
        // File doesn't exist yet — create it
        f = std::fopen(path.c_str(), "wb");
    }
    if (!f)
        return false;

    size_t written = std::fwrite(encrypted.data(), 1, encrypted.size(), f);
    std::fclose(f);
    return written == encrypted.size();
}

bool SaveFile::loadBDSP(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (!isBDSPSize(fileSize))
        return false;

    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    file.close();

    // Box data at fixed offset (party format, 0x158 per slot, 40 boxes * 30 slots)
    size_t boxDataEnd = BDSP_BOX_OFFSET +
        static_cast<size_t>(BDSP_BOX_COUNT) * slotsPerBox_ * PokeCrypto::SIZE_9PARTY;
    if (boxDataEnd > rawData_.size())
        return false;

    boxData_ = rawData_.data() + BDSP_BOX_OFFSET;
    boxDataLen_ = boxDataEnd - BDSP_BOX_OFFSET;

    // Box layout at fixed offset
    if (BDSP_LAYOUT_OFFSET + BDSP_LAYOUT_SIZE <= static_cast<int>(rawData_.size())) {
        boxLayoutData_ = rawData_.data() + BDSP_LAYOUT_OFFSET;
        boxLayoutLen_ = BDSP_LAYOUT_SIZE;
    }

    loaded_ = true;
    return true;
}

bool SaveFile::saveBDSP(const std::string& path) {
    if (rawData_.empty())
        return false;

    // Recalculate MD5 checksum
    // Clear existing hash, compute MD5 of entire save, write hash back
    if (rawData_.size() >= static_cast<size_t>(BDSP_HASH_OFFSET + BDSP_HASH_SIZE)) {
        std::memset(rawData_.data() + BDSP_HASH_OFFSET, 0, BDSP_HASH_SIZE);
        MD5::hash(rawData_.data(), rawData_.size(), rawData_.data() + BDSP_HASH_OFFSET);
    }

    // Open for in-place writing to avoid truncation issues on Switch save filesystem
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) {
        f = std::fopen(path.c_str(), "wb");
    }
    if (!f)
        return false;

    size_t written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    return written == rawData_.size();
}

const std::vector<Pokemon>& SaveFile::getCachedBox(int box) const {
    auto it = boxCache_.find(box);
    if (it != boxCache_.end())
        return it->second;

    DebugLog::line("getCachedBox: box=%d engine=%s", box,
                   useOpenHome() ? "OH" : "PK");

    // Evict oldest if cache is full
    if (static_cast<int>(boxCache_.size()) >= BOX_CACHE_MAX)
        boxCache_.clear();

    std::vector<Pokemon> slots(slotsPerBox_);
    int dataSize = sizeBoxSlot_ - gapBoxSlot_;

    if (useOpenHome() && saveHandleRust_ && openhome_get_box_count(saveHandleRust_.get()) > 0) {
        // OH path: extract stored bytes from Rust OHPKM per slot.
        // Fallback to PK when Rust handle is Raw (SV/ZA/FRLG/LGPE/BDSP — no save loader).
        for (int s = 0; s < slotsPerBox_; s++) {
            PkmHandle* pkm = SaveFileFFI::getSlot(saveHandleRust_.get(), box, s);
            if (!pkm) {
                // Empty slot or error — leave as default-constructed Pokemon
                continue;
            }
            slots[s].gameType_ = gameType_;
            std::vector<uint8_t> bytes = OpenHomeNX::getPkmBoxBytes(pkm);
            if (!bytes.empty() && bytes.size() <= slots[s].data.size()) {
                std::memcpy(slots[s].data.data(), bytes.data(), bytes.size());
            }
            OpenHomeNX::freePkm(pkm);
        }
    } else {
        // PK path: decrypt from raw save data (original behavior, invariable)
        for (int s = 0; s < slotsPerBox_; s++) {
            int offset = getBoxSlotOffset(box, s);
            if (offset + sizeBoxSlot_ > static_cast<int>(boxDataLen_))
                continue;
            slots[s].gameType_ = gameType_;
            slots[s].loadFromEncrypted(boxData_ + offset, dataSize);
        }
    }
    return boxCache_.emplace(box, std::move(slots)).first->second;
}

Pokemon SaveFile::getBoxSlot(int box, int slot) const {
    if (!loaded_ || !boxData_)
        return Pokemon{};
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= slotsPerBox_)
        return Pokemon{};

    const auto& cached = getCachedBox(box);
    return cached[slot];
}

void SaveFile::setBoxSlot(int box, int slot, Pokemon pkm) {
    if (!loaded_ || !boxData_)
        return;

    int offset = getBoxSlotOffset(box, slot);
    if (offset + sizeBoxSlot_ > static_cast<int>(boxDataLen_))
        return;

    // Ensure correct game type, refresh checksum, encrypt and write
    pkm.gameType_ = gameType_;

    // Adapt handling-trainer data to this save's trainer, like PKHeX's
    // SetPKM -> UpdateHandler does on every Pokemon written into a save
    if (!pkm.isEmpty()) {
        TrainerInfo trainer = getTrainerInfo();
        if (trainer.valid)
            updatePokemonHandler(pkm, trainer);
    }

    pkm.getEncrypted(boxData_ + offset);
    // Zero the gap bytes (if any)
    if (gapBoxSlot_ > 0)
        std::memset(boxData_ + offset + (sizeBoxSlot_ - gapBoxSlot_), 0, gapBoxSlot_);

    // Register in Pokedex (non-empty, non-egg Pokemon only)
    if (!pkm.isEmpty())
        Pokedex::registerPokemon(*this, pkm);

    invalidateBoxCache(box);
    dirty_ = true;

    // Invalidate the Rust OH handle: it was created from the original file
    // at load time and is now stale. Subsequent getCachedBox() calls will
    // fall back to the PK path which reads the mutated boxData_ directly.
    if (saveHandleRust_)
        saveHandleRust_.reset();
}

void SaveFile::clearBoxSlot(int box, int slot) {
    if (!loaded_ || !boxData_)
        return;

    int offset = getBoxSlotOffset(box, slot);
    if (offset + sizeBoxSlot_ > static_cast<int>(boxDataLen_))
        return;

    if (isFRLG(gameType_) || isImportedFile(gameType_) || isLGPE(gameType_) || isGen1File(gameType_) || isGen2File(gameType_)) {
        // FRLG/LGPE/GB: empty slots are all-zero bytes (not encrypted blank).
        std::memset(boxData_ + offset, 0, sizeBoxSlot_);
    } else {
        // Write encrypted blank PKM (matching PKHeX behavior) instead of raw zeros.
        // A blank PKM has all-zero decrypted data; we encrypt it so the slot
        // contains valid PokeCrypto-encrypted "empty" data.
        Pokemon blank;
        blank.gameType_ = gameType_;
        blank.getEncrypted(boxData_ + offset);
        // Zero the gap bytes (if any)
        if (gapBoxSlot_ > 0)
            std::memset(boxData_ + offset + (sizeBoxSlot_ - gapBoxSlot_), 0, gapBoxSlot_);
    }

    invalidateBoxCache(box);
    dirty_ = true;

    // Invalidate the Rust OH handle after mutation (same as setBoxSlot).
    if (saveHandleRust_)
        saveHandleRust_.reset();
}

bool SaveFile::isLGPEPartySlot(int box, int slot) const {
    if (!isLGPE(gameType_)) return false;
    uint16_t flatIdx = static_cast<uint16_t>(box * slotsPerBox_ + slot);
    for (int i = 0; i < 6; i++) {
        if (lgpePartyIndices_[i] == flatIdx) return true;
    }
    return false;
}

int SaveFile::lgpePartyIndexOf(int box, int slot) const {
    if (!isLGPE(gameType_)) return -1;
    uint16_t flatIdx = static_cast<uint16_t>(box * slotsPerBox_ + slot);
    for (int i = 0; i < 6; i++) {
        if (lgpePartyIndices_[i] == flatIdx) return i;
    }
    return -1;
}

void SaveFile::setLGPEPartyPointer(int partyIdx, uint16_t flatSlot) {
    if (partyIdx < 0 || partyIdx >= 6) return;
    dirty_ = true;
    lgpePartyIndices_[partyIdx] = flatSlot;
    // Also update rawData_ header so save compaction stays consistent
    if (isLGPE(gameType_) && !rawData_.empty()) {
        size_t ofs = LGPE_HEADER_OFFSET + partyIdx * 2;
        if (ofs + 2 <= rawData_.size())
            std::memcpy(rawData_.data() + ofs, &flatSlot, 2);
    }
}

void SaveFile::setLGPEPartyIndices(const std::array<uint16_t, 6>& v) {
    dirty_ = true;
    lgpePartyIndices_ = v;
    if (isLGPE(gameType_) && !rawData_.empty()) {
        for (int i = 0; i < 6; i++) {
            size_t ofs = LGPE_HEADER_OFFSET + i * 2;
            if (ofs + 2 <= rawData_.size())
                std::memcpy(rawData_.data() + ofs, &v[i], 2);
        }
    }
}

// Gen3 English character table (same as in pokemon.cpp — G3_EN)
static const uint16_t G3_EN_SAVE[256] = {
    0x0020, 0x00C0, 0x00C1, 0x00C2, 0x00C7, 0x00C8, 0x00C9, 0x00CA,
    0x00CB, 0x00CC, 0x3053, 0x00CE, 0x00CF, 0x00D2, 0x00D3, 0x00D4,
    0x0152, 0x00D9, 0x00DA, 0x00DB, 0x00D1, 0x00DF, 0x00E0, 0x00E1,
    0x306D, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x307E,
    0x00EE, 0x00EF, 0x00F2, 0x00F3, 0x00F4, 0x0153, 0x00F9, 0x00FA,
    0x00FB, 0x00F1, 0x00BA, 0x00AA, 0x2469, 0x0026, 0x002B, 0x3042,
    0x3043, 0x3045, 0x3047, 0x3049, 0x3083, 0x003D, 0x003B, 0x304C,
    0x304E, 0x3050, 0x3052, 0x3054, 0x3056, 0x3058, 0x305A, 0x305C,
    0x305E, 0x3060, 0x3062, 0x3065, 0x3067, 0x3069, 0x3070, 0x3073,
    0x3076, 0x3079, 0x307C, 0x3071, 0x3074, 0x3077, 0x307A, 0x307D,
    0x3063, 0x00BF, 0x00A1, 0x2483, 0x2484, 0x30AA, 0x30AB, 0x30AD,
    0x30AF, 0x30B1, 0x00CD, 0x0025, 0x0028, 0x0029, 0x30BB, 0x30BD,
    0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC,
    0x00E2, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x00ED,
    0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
    0x30EA, 0x2191, 0x2193, 0x2190, 0xFF0B, 0x30F2, 0x30F3, 0x30A1,
    0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x2482, 0x003C, 0x003E, 0x30AC,
    0x30AE, 0x30B0, 0x30B2, 0x30B4, 0x30B6, 0x30B8, 0x30BA, 0x30BC,
    0x30BE, 0x30C0, 0x30C2, 0x30C5, 0x30C7, 0x30C9, 0x30D0, 0x30D3,
    0x30D6, 0x30D9, 0x30DC, 0x30D1, 0x30D4, 0x30D7, 0x30DA, 0x30DD,
    0x30C3, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036,
    0x0037, 0x0038, 0x0039, 0x0021, 0x003F, 0x002E, 0x002D, 0xFF65,
    0x246C, 0x201C, 0x201D, 0x2018, 0x0027, 0x2642, 0x2640, 0x0024,
    0x002C, 0x2467, 0x002F, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045,
    0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D,
    0x004E, 0x004F, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055,
    0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x0061, 0x0062, 0x0063,
    0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B,
    0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073,
    0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x25BA,
    0x003A, 0x00C4, 0x00D6, 0x00DC, 0x00E4, 0x00F6, 0x00FC, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

// Gen3 Japanese character table (same as in pokemon.cpp — G3_JP)
static const uint16_t G3_JP_SAVE[256] = {
    0x3000, 0x3042, 0x3044, 0x3046, 0x3048, 0x304A, 0x304B, 0x304D,
    0x304F, 0x3051, 0x3053, 0x3055, 0x3057, 0x3059, 0x305B, 0x305D,
    0x305F, 0x3061, 0x3064, 0x3066, 0x3068, 0x306A, 0x306B, 0x306C,
    0x306D, 0x306E, 0x306F, 0x3072, 0x3075, 0x3078, 0x307B, 0x307E,
    0x307F, 0x3080, 0x3081, 0x3082, 0x3084, 0x3086, 0x3088, 0x3089,
    0x308A, 0x308B, 0x308C, 0x308D, 0x308F, 0x3092, 0x3093, 0x3041,
    0x3043, 0x3045, 0x3047, 0x3049, 0x3083, 0x3085, 0x3087, 0x304C,
    0x304E, 0x3050, 0x3052, 0x3054, 0x3056, 0x3058, 0x305A, 0x305C,
    0x305E, 0x3060, 0x3062, 0x3065, 0x3067, 0x3069, 0x3070, 0x3073,
    0x3076, 0x3079, 0x307C, 0x3071, 0x3074, 0x3077, 0x307A, 0x307D,
    0x3063, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD,
    0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD,
    0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC,
    0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE,
    0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
    0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F2, 0x30F3, 0x30A1,
    0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30AC,
    0x30AE, 0x30B0, 0x30B2, 0x30B4, 0x30B6, 0x30B8, 0x30BA, 0x30BC,
    0x30BE, 0x30C0, 0x30C2, 0x30C5, 0x30C7, 0x30C9, 0x30D0, 0x30D3,
    0x30D6, 0x30D9, 0x30DC, 0x30D1, 0x30D4, 0x30D7, 0x30DA, 0x30DD,
    0x30C3, 0xFF10, 0xFF11, 0xFF12, 0xFF13, 0xFF14, 0xFF15, 0xFF16,
    0xFF17, 0xFF18, 0xFF19, 0xFF01, 0xFF1F, 0x3002, 0x30FC, 0x30FB,
    0x2026, 0x300E, 0x300F, 0x300C, 0x300D, 0x2642, 0x2640, 0x5186,
    0xFF0E, 0x00D7, 0xFF0F, 0xFF21, 0xFF22, 0xFF23, 0xFF24, 0xFF25,
    0xFF26, 0xFF27, 0xFF28, 0xFF29, 0xFF2A, 0xFF2B, 0xFF2C, 0xFF2D,
    0xFF2E, 0xFF2F, 0xFF30, 0xFF31, 0xFF32, 0xFF33, 0xFF34, 0xFF35,
    0xFF36, 0xFF37, 0xFF38, 0xFF39, 0xFF3A, 0xFF41, 0xFF42, 0xFF43,
    0xFF44, 0xFF45, 0xFF46, 0xFF47, 0xFF48, 0xFF49, 0xFF4A, 0xFF4B,
    0xFF4C, 0xFF4D, 0xFF4E, 0xFF4F, 0xFF50, 0xFF51, 0xFF52, 0xFF53,
    0xFF54, 0xFF55, 0xFF56, 0xFF57, 0xFF58, 0xFF59, 0xFF5A, 0x25BA,
    0xFF1A, 0x00C4, 0x00D6, 0x00DC, 0x00E4, 0x00F6, 0x00FC, 0x2191,
    0x2193, 0x2190, 0x2192, 0xFF0B, 0x0000, 0x0000, 0x0000, 0x0000,
};

static std::string decodeGen3String(const uint8_t* p, int maxBytes, bool jp = false) {
    std::string result;
    const uint16_t* table = jp ? G3_JP_SAVE : G3_EN_SAVE;
    for (int i = 0; i < maxBytes; i++) {
        uint8_t b = p[i];
        if (b == 0xFF) break;
        uint16_t ch = table[b];
        if (ch == 0) break;
        if (ch < 0x80) {
            result += static_cast<char>(ch);
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 | (ch >> 6));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (ch >> 12));
            result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return result;
}

std::string SaveFile::getBoxName(int box) const {
    if (box < 0 || box >= boxCount_)
        return "Box " + std::to_string(box + 1);

    if (isFRLG(gameType_) || isImportedFile(gameType_)) {
        // FRLG: box names stored after all pokemon data in Gen3 encoding
        if (!boxLayoutData_)
            return "Box " + std::to_string(box + 1);
        int nameOfs = box * GBA_BOXNAME_LEN;
        if (nameOfs + GBA_BOXNAME_LEN > static_cast<int>(boxLayoutLen_))
            return "Box " + std::to_string(box + 1);
        std::string name = decodeGen3String(boxLayoutData_ + nameOfs, GBA_BOXNAME_LEN, isFRLG_JA(gameType_));
        if (name.empty())
            return "Box " + std::to_string(box + 1);
        return name;
    }

    if (isGen2File(gameType_)) {
        // Gen2 box names: 9B GB-encoded at the per-version base (real names,
        // unlike Gen1). boxLayoutData_ stays null; decode straight from raw.
        if (gbcBoxNamesBase_ < 0)
            return "Box " + std::to_string(box + 1);
        std::string name = Gen1::decodeGbString(rawData_.data() + gbcBoxNamesBase_ + box * 9, 9);
        if (name.empty())
            return "Box " + std::to_string(box + 1);
        return name;
    }

    if (!boxLayoutData_)
        return "Box " + std::to_string(box + 1);

    // Box names are stored as UTF-16LE, 0x22 bytes per name
    constexpr int NAME_SIZE = 0x22;
    int nameOfs = box * NAME_SIZE;
    if (nameOfs + NAME_SIZE > static_cast<int>(boxLayoutLen_)) {
        return "Box " + std::to_string(box + 1);
    }

    // Read UTF-16LE string
    std::string result;
    const uint8_t* p = boxLayoutData_ + nameOfs;
    for (int i = 0; i < NAME_SIZE / 2; i++) {
        uint16_t ch;
        std::memcpy(&ch, p + i * 2, 2);
        if (ch == 0)
            break;
        if (ch < 0x80) {
            result += static_cast<char>(ch);
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 | (ch >> 6));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (ch >> 12));
            result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }

    if (result.empty())
        return "Box " + std::to_string(box + 1);

    return result;
}

std::string SaveFile::verifyRoundTrip() {
    if (originalFileData_.empty())
        return "No original data";

    // Re-encrypt blocks (no modifications have been made yet)
    std::vector<uint8_t> encrypted = SaveFileFFI::encrypt(blocks_);

    std::string result;

    if (encrypted.size() != originalFileData_.size()) {
        result = "SIZE MISMATCH: encrypted=" + std::to_string(encrypted.size())
               + " original=" + std::to_string(originalFileData_.size());
    } else {
        // Compare byte-by-byte
        size_t diffCount = 0;
        size_t firstDiff = 0;
        for (size_t i = 0; i < encrypted.size(); i++) {
            if (encrypted[i] != originalFileData_[i]) {
                if (diffCount == 0)
                    firstDiff = i;
                diffCount++;
            }
        }

        if (diffCount == 0) {
            result = "OK";
        } else {
            // Check if the difference is only in the hash (last 32 bytes)
            size_t hashStart = originalFileData_.size() - 32;
            bool onlyHashDiffers = true;
            for (size_t i = 0; i < hashStart; i++) {
                if (encrypted[i] != originalFileData_[i]) {
                    onlyHashDiffers = false;
                    break;
                }
            }

            char buf[256];
            if (onlyHashDiffers) {
                std::snprintf(buf, sizeof(buf),
                    "HASH ONLY: payload matches but hash differs");
            } else {
                std::snprintf(buf, sizeof(buf),
                    "DIFF: %zu bytes differ, first at 0x%zX (enc=0x%02X orig=0x%02X)",
                    diffCount, firstDiff,
                    encrypted[firstDiff], originalFileData_[firstDiff]);
            }
            result = buf;
        }
    }

    // Free the original data
    originalFileData_.clear();
    originalFileData_.shrink_to_fit();

    return result;
}

// --- Trainer Info (for wondercard injection) ---

TrainerInfo SaveFile::getTrainerInfo() const {
    TrainerInfo info;
    info.valid = false;

    // LGPE uses flat binary — MyStatus7b at offset 0x01000
    if (isLGPE(gameType_)) {
        constexpr size_t STATUS_OFF = 0x01000;
        if (rawData_.size() < STATUS_OFF + 0x60)
            return info;
        const uint8_t* s = rawData_.data() + STATUS_OFF;

        // ID32 at 0x00
        std::memcpy(&info.id32, s + 0x00, 4);

        // Gender at 0x05
        info.gender = s[0x05];

        // Language at 0x35
        info.language = s[0x35];

        // OT Name at 0x38 (UTF-16LE, up to 12 chars)
        for (int i = 0; i < 12; i++) {
            uint16_t ch;
            std::memcpy(&ch, s + 0x38 + i * 2, 2);
            if (ch == 0) break;
            info.otName += static_cast<char16_t>(ch);
        }

        // Game version: GP=42, GE=43
        info.gameVersion = (gameType_ == GameType::GP) ? 42 : 43;

        info.valid = !info.otName.empty();
        return info;
    }

    // BDSP uses flat binary (not SCBlock) — handle before SCBlock check
    if (isBDSP(gameType_)) {
        // MyStatus8b at raw offset 0x79BB4, ConfigSave8b at 0x79B74
        constexpr size_t STATUS_OFF = 0x79BB4;
        constexpr size_t CONFIG_OFF = 0x79B74;
        if (rawData_.size() < STATUS_OFF + 0x50)
            return info;
        const uint8_t* s = rawData_.data() + STATUS_OFF;
        const uint8_t* c = rawData_.data() + CONFIG_OFF;

        // OT Name at MyStatus8b+0x00 (UTF-16LE, up to 13 chars)
        for (int i = 0; i < 13; i++) {
            uint16_t ch;
            std::memcpy(&ch, s + i * 2, 2);
            if (ch == 0) break;
            info.otName += static_cast<char16_t>(ch);
        }

        // ID32 at MyStatus8b+0x1C
        std::memcpy(&info.id32, s + 0x1C, 4);

        // Gender at MyStatus8b+0x24 — raw byte is Male boolean (1=Male, 0=Female)
        // PKHeX Gender convention: 0=Male, 1=Female — invert
        info.gender = s[0x24] ? 0 : 1;

        // Language at ConfigSave8b+0x04 (i32, but we only need low byte)
        info.language = static_cast<uint8_t>(c[0x04]);

        // Game version: BD=48, SP=49
        info.gameVersion = (gameType_ == GameType::BD) ? 48 : 49;

        info.valid = !info.otName.empty();
        return info;
    }

    // Only for SCBlock-based games (SV, ZA, SwSh)
    if (blocks_.empty())
        return info;

    if (gameType_ == GameType::LA) {
        // PLA MyStatus8a block key: 0xf25c070e (same key as SWSH, different offsets)
        const SCBlock* block = SaveFileFFI::findBlock(blocks_, 0xf25c070e);
        if (!block || block->data.size() < 0x50)
            return info;

        const uint8_t* d = block->data.data();

        // ID32 at 0x10
        std::memcpy(&info.id32, d + 0x10, 4);

        // Gender at 0x15
        info.gender = d[0x15];

        // Language at 0x17
        info.language = d[0x17];

        // OT Name at 0x20 (UTF-16LE, up to 13 chars)
        for (int i = 0; i < 13; i++) {
            uint16_t ch;
            std::memcpy(&ch, d + 0x20 + i * 2, 2);
            if (ch == 0) break;
            info.otName += static_cast<char16_t>(ch);
        }

        info.gameVersion = 47; // PLA
        info.valid = !info.otName.empty();
        return info;
    }

    if (isSwSh(gameType_)) {
        // SWSH MyStatus8 block key: 0xf25c070e
        const SCBlock* block = SaveFileFFI::findBlock(blocks_, 0xf25c070e);
        if (!block || block->data.size() < 0xCA)
            return info;

        const uint8_t* d = block->data.data();

        // ID32 at 0xA0
        std::memcpy(&info.id32, d + 0xA0, 4);

        // Gender at 0xA5
        info.gender = d[0xA5];

        // Language at 0xA7
        info.language = d[0xA7];

        // OT Name at 0xB0 (UTF-16LE, up to 13 chars)
        for (int i = 0; i < 13; i++) {
            uint16_t ch;
            std::memcpy(&ch, d + 0xB0 + i * 2, 2);
            if (ch == 0) break;
            info.otName += static_cast<char16_t>(ch);
        }

        // Game version: SW=44, SH=45
        info.gameVersion = (gameType_ == GameType::Sw) ? 44 : 45;

        info.valid = !info.otName.empty();
        return info;
    }

    // SV/ZA: KMyStatus block key: 0xE3E89BD1
    const SCBlock* block = SaveFileFFI::findBlock(blocks_, 0xE3E89BD1);
    if (!block || block->data.size() < 0x30)
        return info;

    const uint8_t* d = block->data.data();

    // ID32 at 0x00
    std::memcpy(&info.id32, d + 0x00, 4);

    // Gender at 0x05
    info.gender = d[0x05];

    // Language at 0x07
    info.language = d[0x07];

    // OT Name at 0x10 (UTF-16LE)
    for (int i = 0; i < 13; i++) {
        uint16_t ch;
        std::memcpy(&ch, d + 0x10 + i * 2, 2);
        if (ch == 0) break;
        info.otName += static_cast<char16_t>(ch);
    }

    // Game version: ZA=52, SL=50, VL=51
    if (gameType_ == GameType::ZA)
        info.gameVersion = 52;
    else
        info.gameVersion = (gameType_ == GameType::V) ? 51 : 50;

    info.valid = !info.otName.empty();
    return info;
}

// --- CRC16NoInvert (for LGPE BEEF block checksums) ---

static const uint16_t CRC16_TABLE[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};

uint16_t SaveFile::crc16NoInvert(const uint8_t* data, size_t len) {
    uint16_t chk = 0;
    for (size_t i = 0; i < len; i++)
        chk = CRC16_TABLE[(uint8_t)(data[i] ^ chk)] ^ (chk >> 8);
    return chk;
}

// --- LGPE save format (flat binary with BEEF block checksums) ---

bool SaveFile::loadLGPE(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != LGPE_SAVE_SIZE)
        return false;

    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    file.close();

    // Box data at fixed offset (party format, 0x104 per slot, 40 boxes * 25 slots)
    size_t boxDataEnd = LGPE_BOX_OFFSET + LGPE_BOX_SIZE;
    if (boxDataEnd > rawData_.size())
        return false;

    boxData_ = rawData_.data() + LGPE_BOX_OFFSET;
    boxDataLen_ = LGPE_BOX_SIZE;

    // LGPE has no custom box names
    boxLayoutData_ = nullptr;
    boxLayoutLen_ = 0;

    // Parse party pointers from PokeListHeader (6 x u16)
    lgpePartyCount_ = 0;
    for (int i = 0; i < 6; i++) {
        uint16_t idx;
        std::memcpy(&idx, rawData_.data() + LGPE_HEADER_OFFSET + i * 2, 2);
        lgpePartyIndices_[i] = idx;
        if (idx < 1000) lgpePartyCount_++;
    }

    loaded_ = true;
    return true;
}

bool SaveFile::saveLGPE(const std::string& path) {
    if (rawData_.empty())
        return false;

    // --- CompressStorage ---
    // LGPE stores Pokemon as a sequential flat list; gaps are not legal.
    // Pack all occupied slots to the beginning (matching PKHeX CompressStorage).
    int totalSlots = LGPE_BOX_COUNT * LGPE_SLOTS_PER_BOX;
    int slotSize = PokeCrypto::SIZE_6PARTY;

    // Build old→new index mapping for pointer updates
    std::vector<int> oldToNew(totalSlots, -1);
    int writeIdx = 0;

    for (int i = 0; i < totalSlots; i++) {
        int offset = i * slotSize;
        if (offset + slotSize > static_cast<int>(boxDataLen_))
            break;

        // EC (first 4 bytes) is unencrypted; EC==0 means empty slot
        uint32_t ec;
        std::memcpy(&ec, boxData_ + offset, 4);
        if (ec == 0)
            continue;

        oldToNew[i] = writeIdx;
        if (writeIdx != i) {
            int dstOffset = writeIdx * slotSize;
            std::memmove(boxData_ + dstOffset, boxData_ + offset, slotSize);
        }
        writeIdx++;
    }

    // Zero remaining slots after the last occupied one
    for (int i = writeIdx; i < totalSlots; i++) {
        int offset = i * slotSize;
        if (offset + slotSize <= static_cast<int>(boxDataLen_))
            std::memset(boxData_ + offset, 0, slotSize);
    }

    // --- Update PokeListHeader ---
    // Layout: 6 party pointers (u16) + 1 starter pointer (u16) + count (u16)
    // Update party and starter pointers using the index mapping
    for (int i = 0; i < 7; i++) {
        size_t ptrOfs = LGPE_HEADER_OFFSET + i * 2;
        if (ptrOfs + 2 > rawData_.size())
            break;

        uint16_t oldIdx;
        std::memcpy(&oldIdx, rawData_.data() + ptrOfs, 2);

        if (oldIdx < static_cast<uint16_t>(totalSlots) && oldToNew[oldIdx] >= 0) {
            uint16_t newIdx = static_cast<uint16_t>(oldToNew[oldIdx]);
            std::memcpy(rawData_.data() + ptrOfs, &newIdx, 2);
            // Keep in-memory party indices in sync (first 6 entries are party)
            if (i < 6) lgpePartyIndices_[i] = newIdx;
        }
    }

    // Update count
    uint16_t count = static_cast<uint16_t>(writeIdx);
    size_t countOfs = LGPE_HEADER_OFFSET + 7 * 2;
    if (countOfs + 2 <= rawData_.size())
        std::memcpy(rawData_.data() + countOfs, &count, 2);

    // Recalculate CRC16NoInvert checksums for all 21 blocks
    for (int i = 0; i < LGPE_NUM_BLOCKS; i++) {
        uint16_t chk = crc16NoInvert(
            rawData_.data() + LGPE_BLOCKS[i].offset,
            LGPE_BLOCKS[i].length);
        // Checksum stored at: BLOCK_INFO_OFS + 0x14 + i*8 + 6
        size_t chkOffset = LGPE_BLOCK_INFO_OFS + 0x14 + i * 8 + 6;
        if (chkOffset + 2 <= rawData_.size())
            std::memcpy(rawData_.data() + chkOffset, &chk, 2);
    }

    // Write in-place (like BDSP)
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) {
        f = std::fopen(path.c_str(), "wb");
    }
    if (!f)
        return false;

    size_t written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    return written == rawData_.size();
}

// --- GB save format (Gen 1 R/B/Y SRAM, PKHeX SAV1.cs) ---

// Offsets below are the INT set (SAV1Offsets.INT); v1 supports INT saves
// only (user saves are Italian). JP saves fail the INT checksum and are
// rejected without touching anything.
namespace {
constexpr size_t GB_SAVE_SIZE = 0x8000;
constexpr int GB_BOX_COUNT = 12;
constexpr int GB_SLOTS_PER_BOX = 20;
constexpr int GB_SLOT_STRIDE = 55; // 33B record + 11B OT GB + 11B nick GB
constexpr int GB_OT = 0x2598;
constexpr int GB_CHECKSUM = 0x3523;
constexpr int GB_PARTY = 0x2F2C;
constexpr int GB_CURBOX = 0x30C0;
constexpr int GB_CURBOXIDX = 0x284C;
constexpr int GB_BOX_LIST = 0x462; // ((11*2)+33+1)*20+2
int gbStoredBoxBase(int box) {
    return box < 6 ? 0x4000 + box * GB_BOX_LIST : 0x6000 + (box - 6) * GB_BOX_LIST;
}
bool gbChecksumValid(const uint8_t* d) {
    uint8_t s = 0;
    for (int i = GB_OT; i < GB_CHECKSUM; i++) s += d[i];
    return d[GB_CHECKSUM] == static_cast<uint8_t>(~s);
}
// Unpack one PokeList1 ([count][species x cap+1][records][OT x cap][nick x cap])
// into flat 55B slots. Returns count, or -1 when the list is corrupt.
// An erased (0xFF) count means an empty box, not corruption.
int gbUnpackList(const uint8_t* list, int cap, int recSize, uint8_t* out) {
    int n = list[0];
    if (n == 0xFF) n = 0;
    if (n < 0 || n > cap) return -1;
    const uint8_t* recs = list + 1 + cap + 1;
    const uint8_t* ots = recs + recSize * cap;
    const uint8_t* nicks = ots + 11 * cap;
    for (int i = 0; i < n; i++) {
        uint8_t* dst = out + i * GB_SLOT_STRIDE;
        std::memcpy(dst, recs + i * recSize, recSize);
        std::memcpy(dst + 33, ots + i * 11, 11);
        std::memcpy(dst + 44, nicks + i * 11, 11);
    }
    return n;
}
// Pack flat 55B slots back into one PokeList1 region (mirror of gbUnpackList).
// Only non-empty slots (species byte != 0) are stored; the rest is zeroed.
void gbPackList(uint8_t* list, int cap, const uint8_t* flat) {
    int idx[GB_SLOTS_PER_BOX], n = 0;
    for (int s = 0; s < cap; s++)
        if (flat[s * GB_SLOT_STRIDE] != 0) idx[n++] = s;
    // Zero the whole region first (no stale bytes past the terminator).
    std::memset(list, 0, static_cast<size_t>(GB_BOX_LIST));
    list[0] = static_cast<uint8_t>(n);
    for (int i = 0; i < n; i++) list[1 + i] = flat[idx[i] * GB_SLOT_STRIDE];
    list[1 + n] = 0xFF;
    uint8_t* recs = list + 1 + cap + 1;
    uint8_t* ots = recs + 33 * cap;
    uint8_t* nicks = ots + 11 * cap;
    for (int i = 0; i < n; i++) {
        const uint8_t* f = flat + idx[i] * GB_SLOT_STRIDE;
        std::memcpy(recs + i * 33, f, 33);
        std::memcpy(ots + i * 11, f + 33, 11);
        std::memcpy(nicks + i * 11, f + 44, 11);
    }
}
} // namespace

bool SaveFile::loadGB(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != GB_SAVE_SIZE)
        return false;

    file.seekg(0);
    rawData_.resize(GB_SAVE_SIZE);
    file.read(reinterpret_cast<char*>(rawData_.data()), GB_SAVE_SIZE);
    file.close();

    if (!gbChecksumValid(rawData_.data())) {
        DebugLog::line("loadGB: %s -> bad INT checksum, rejected", path.c_str());
        return false;
    }

    // Current box: bit7 of the index byte tells whether the stored boxes were
    // ever flushed (PKHeX BoxesInitialized). An unflushed stored box may hold
    // garbage counts, so only the current box is trusted from storage then.
    int cur = rawData_[GB_CURBOXIDX] & 0x7F;
    if (cur < 0 || cur >= GB_BOX_COUNT) cur = 0;
    bool flushed = (rawData_[GB_CURBOXIDX] & 0x80) != 0;

    gbStorage_.assign(GB_BOX_COUNT * GB_SLOTS_PER_BOX * GB_SLOT_STRIDE, 0);
    gbStoredOrig_.assign(GB_BOX_COUNT * GB_BOX_LIST, 0);
    for (int b = 0; b < GB_BOX_COUNT; b++) {
        uint8_t* dst = gbStorage_.data() + b * GB_SLOTS_PER_BOX * GB_SLOT_STRIDE;
        gbBoxTrusted_[b] = false;
        if (b == cur) {
            if (gbUnpackList(rawData_.data() + GB_CURBOX, GB_SLOTS_PER_BOX, 33, dst) < 0)
                return false;
            gbBoxTrusted_[b] = true;
        } else if (flushed) {
            if (gbUnpackList(rawData_.data() + gbStoredBoxBase(b), GB_SLOTS_PER_BOX, 33, dst) < 0)
                return false;
            gbBoxTrusted_[b] = true;
        }
        // Snapshot the stored bytes either way: untrusted boxes are written
        // back verbatim on save (never zeroed).
        std::memcpy(gbStoredOrig_.data() + b * GB_BOX_LIST,
                    rawData_.data() + gbStoredBoxBase(b), GB_BOX_LIST);
        // else: leave zeros (empty) — never trust unflushed storage.
    }

    boxData_ = gbStorage_.data();
    boxDataLen_ = gbStorage_.size();
    boxLayoutData_ = nullptr; // Gen 1 has no custom box names ("Box N")
    boxLayoutLen_ = 0;

    loaded_ = true;
    DebugLog::line("loadGB: %s -> OK (curbox %d, flushed %d)", path.c_str(), cur, (int)flushed);
    return true;
}

bool SaveFile::saveGB(const std::string& path) {
    if (!loaded_)
        return false;

    // Pack every box from the flat buffer back into its PokeList1 region.
    // The current box is mirrored into the live CurrentBox region as well
    // (PKHeX SAV1.GetFinalData): the game reads party-adjacent state from it.
    // Boxes never trusted at load (unflushed storage) and still empty are
    // restored byte-wise from the load snapshot instead of packed: never
    // write what we didn't read.
    int cur = rawData_[GB_CURBOXIDX] & 0x7F;
    if (cur < 0 || cur >= GB_BOX_COUNT) cur = 0;
    bool anyContent = false;
    for (int b = 0; b < GB_BOX_COUNT; b++) {
        const uint8_t* src = gbStorage_.data() + b * GB_SLOTS_PER_BOX * GB_SLOT_STRIDE;
        bool hasContent = false;
        for (int s = 0; s < GB_SLOTS_PER_BOX; s++)
            if (src[s * GB_SLOT_STRIDE] != 0) { hasContent = true; break; }
        if (hasContent) anyContent = true;
        if (b == cur || gbBoxTrusted_[b] || hasContent) {
            if (b == cur) {
                gbPackList(rawData_.data() + GB_CURBOX, GB_SLOTS_PER_BOX, src);
                gbPackList(rawData_.data() + gbStoredBoxBase(b), GB_SLOTS_PER_BOX, src);
            } else {
                gbPackList(rawData_.data() + gbStoredBoxBase(b), GB_SLOTS_PER_BOX, src);
            }
        } else {
            std::memcpy(rawData_.data() + gbStoredBoxBase(b),
                        gbStoredOrig_.data() + b * GB_BOX_LIST, GB_BOX_LIST);
        }
    }
    if (anyContent)
        rawData_[GB_CURBOXIDX] |= 0x80; // boxes-initialized flag

    // Pokedex seen+caught for every boxed species (PKHeX SetDex-on-deposit
    // equivalent, applied at save time): bit (species-1).
    for (int b = 0; b < GB_BOX_COUNT; b++) {
        const uint8_t* src = gbStorage_.data() + b * GB_SLOTS_PER_BOX * GB_SLOT_STRIDE;
        for (int s = 0; s < GB_SLOTS_PER_BOX; s++) {
            int ndex = Gen1::internalToNdex(src[s * GB_SLOT_STRIDE]);
            if (ndex < 1 || ndex > 151) continue;
            int bit = ndex - 1;
            rawData_[0x25B6 + (bit >> 3)] |= (1 << (bit & 7)); // seen
            rawData_[0x25A3 + (bit >> 3)] |= (1 << (bit & 7)); // caught
        }
    }

    // File checksum over OT..ChecksumOfs (PKHeX GetRBYChecksum).
    uint8_t cks = 0;
    for (int i = GB_OT; i < GB_CHECKSUM; i++) cks += rawData_[i];
    rawData_[GB_CHECKSUM] = static_cast<uint8_t>(~cks);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveGB: %s -> OK", path.c_str());
    return true;
}

// --- GB(C) save format (Gen 2 G/S/C SRAM, PKHeX SAV2.cs) ---

// INT offsets per version (SAV2Offsets.INT). v1 supports INT saves only;
// JP/KR fail the INT checksums and are rejected without touching anything.
struct GbcLayout {
    int party, curBoxIdx, boxNames, dexCaught, dexSeen, curBox;
    int cksEnd, cksPos1, cksPos2;
};
constexpr GbcLayout GBC_GS = {0x288A, 0x2724, 0x2727, 0x2A4C, 0x2A6C, 0x2D6C, 0x2D68, 0x2D69, 0x7E6D};
constexpr GbcLayout GBC_C = {0x2865, 0x2700, 0x2703, 0x2A27, 0x2A47, 0x2D10, 0x2B82, 0x2D0D, 0x1F0D};
constexpr size_t GB2_SAVE_SIZE = 0x8000;
constexpr int GB2_BOX_COUNT = 14;
constexpr int GB2_SLOTS_PER_BOX = 20;
constexpr int GB2_SLOT_STRIDE = 54; // 32B record + 11B OT + 11B nick
constexpr int GB2_BOX_LIST = 0x44E; // ((11*2)+32+1)*20+2
constexpr int GB2_BOX_STRIDE = 0x450; // list + 2B gap (preserved verbatim, never parsed)
int gbcStoredBoxBase(int box) {
    return box < 7 ? 0x4000 + box * GB2_BOX_STRIDE : 0x6000 + (box - 7) * GB2_BOX_STRIDE;
}
bool gbcChecksumValid(const uint8_t* d, const GbcLayout& L) {
    uint16_t s = 0;
    for (int i = 0x2009; i <= L.cksEnd; i++) s += d[i];
    auto rd = [&](int o) -> uint16_t { return d[o] | (d[o + 1] << 8); };
    return rd(L.cksPos1) == s && rd(L.cksPos2) == s;
}
// Unpack one PokeList2 ([count][species x cap+1][records][OT x cap][nick x cap])
// into flat 54B slots. Returns count, or -1 when corrupt. Erased (0xFF)
// count means an empty box, not corruption.
int gbcUnpackList(const uint8_t* list, int cap, uint8_t* out) {
    int n = list[0];
    if (n == 0xFF) n = 0;
    if (n < 0 || n > cap) return -1;
    const uint8_t* recs = list + 1 + cap + 1;
    const uint8_t* ots = recs + 32 * cap;
    const uint8_t* nicks = ots + 11 * cap;
    for (int i = 0; i < n; i++) {
        uint8_t* dst = out + i * GB2_SLOT_STRIDE;
        std::memcpy(dst, recs + i * 32, 32);
        std::memcpy(dst + 32, ots + i * 11, 11);
        std::memcpy(dst + 43, nicks + i * 11, 11);
    }
    return n;
}
// Mirror of gbcUnpackList: only non-empty slots are stored, rest zeroed.
// Mirror of gbUnpackList: only slots with ANY nonzero record byte are
// stored (not just species != 0): Gen2 eggs ride in the header marker
// (0xFD) with an opaque body, and must survive the round-trip even though
// the UI can't display them yet (shows baby species or nothing).
static bool gbcSlotPresent(const uint8_t* flat, int s) {
    for (int i = 0; i < 32; i++)
        if (flat[s * GB2_SLOT_STRIDE + i] != 0) return true;
    return false;
}
void gbcPackList(uint8_t* list, int cap, const uint8_t* flat) {
    int idx[GB2_SLOTS_PER_BOX], n = 0;
    for (int s = 0; s < cap; s++)
        if (gbcSlotPresent(flat, s)) idx[n++] = s;
    std::memset(list, 0, static_cast<size_t>(GB2_BOX_LIST));
    list[0] = static_cast<uint8_t>(n);
    for (int i = 0; i < n; i++) list[1 + i] = flat[idx[i] * GB2_SLOT_STRIDE];
    list[1 + n] = 0xFF;
    uint8_t* recs = list + 1 + cap + 1;
    uint8_t* ots = recs + 32 * cap;
    uint8_t* nicks = ots + 11 * cap;
    for (int i = 0; i < n; i++) {
        const uint8_t* f = flat + idx[i] * GB2_SLOT_STRIDE;
        std::memcpy(recs + i * 32, f, 32);
        std::memcpy(ots + i * 11, f + 32, 11);
        std::memcpy(nicks + i * 11, f + 43, 11);
    }
}

bool SaveFile::loadGBC(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != GB2_SAVE_SIZE)
        return false;

    file.seekg(0);
    rawData_.resize(GB2_SAVE_SIZE);
    file.read(reinterpret_cast<char*>(rawData_.data()), GB2_SAVE_SIZE);
    file.close();

    // Version from the caller's game type (scan detects via checksums first,
    // so a mismatch here fails closed on the checksum below instead of
    // misparsing). The +2B inter-box gaps are never parsed.
    gbcIsCrystal_ = (gameType_ == GameType::CRYSTAL);
    const GbcLayout& L = gbcIsCrystal_ ? GBC_C : GBC_GS;
    if (!gbcChecksumValid(rawData_.data(), L)) {
        DebugLog::line("loadGBC: %s -> bad checksum for %s, rejected",
                       path.c_str(), gbcIsCrystal_ ? "Crystal" : "GS");
        return false;
    }

    // Unlike Gen1 there is no current-box mirror and no initialized flag:
    // stored regions are always authoritative. Clamp (never crash) + log.
    gbcStorage_.assign(GB2_BOX_COUNT * GB2_SLOTS_PER_BOX * GB2_SLOT_STRIDE, 0);
    gbcStoredOrig_.assign(GB2_BOX_COUNT * GB2_BOX_LIST, 0);
    for (int b = 0; b < GB2_BOX_COUNT; b++) {
        uint8_t* dst = gbcStorage_.data() + b * GB2_SLOTS_PER_BOX * GB2_SLOT_STRIDE;
        const uint8_t* src = rawData_.data() + gbcStoredBoxBase(b);
        std::memcpy(gbcStoredOrig_.data() + b * GB2_BOX_LIST, src, GB2_BOX_LIST);
        if (gbcUnpackList(src, GB2_SLOTS_PER_BOX, dst) < 0) {
            DebugLog::line("loadGBC: %s box %d corrupt count, emptied", path.c_str(), b);
            gbcBoxTrusted_[b] = false;
        } else {
            gbcBoxTrusted_[b] = true;
        }
    }
    gbcBoxNamesBase_ = L.boxNames;

    boxData_ = gbcStorage_.data();
    boxDataLen_ = gbcStorage_.size();
    boxLayoutData_ = nullptr;
    boxLayoutLen_ = 0;

    loaded_ = true;
    DebugLog::line("loadGBC: %s -> OK (%s)", path.c_str(), gbcIsCrystal_ ? "Crystal" : "GS");
    return true;
}

bool SaveFile::saveGBC(const std::string& path) {
    if (!loaded_)
        return false;

    const GbcLayout& L = gbcIsCrystal_ ? GBC_C : GBC_GS;
    int curBox = rawData_[L.curBoxIdx];
    if (curBox < 0 || curBox >= GB2_BOX_COUNT) curBox = 0;
    for (int b = 0; b < GB2_BOX_COUNT; b++) {
        const uint8_t* src = gbcStorage_.data() + b * GB2_SLOTS_PER_BOX * GB2_SLOT_STRIDE;
        bool hasContent = false;
        for (int s = 0; s < GB2_SLOTS_PER_BOX; s++)
            if (gbcSlotPresent(src, s)) { hasContent = true; break; }
        if (gbcBoxTrusted_[b] || hasContent) {
            gbcPackList(rawData_.data() + gbcStoredBoxBase(b), GB2_SLOTS_PER_BOX, src);
            // Mirror the current box into the live region too (PKHeX parity;
            // the game/Stadium ignore it, but keep it in sync anyway).
            if (b == curBox)
                gbcPackList(rawData_.data() + L.curBox, GB2_SLOTS_PER_BOX, src);
        } else {
            std::memcpy(rawData_.data() + gbcStoredBoxBase(b),
                        gbcStoredOrig_.data() + b * GB2_BOX_LIST, GB2_BOX_LIST);
        }
    }

    // Pokedex seen+caught for every boxed species (bit species-1).
    for (int b = 0; b < GB2_BOX_COUNT; b++) {
        const uint8_t* src = gbcStorage_.data() + b * GB2_SLOTS_PER_BOX * GB2_SLOT_STRIDE;
        for (int s = 0; s < GB2_SLOTS_PER_BOX; s++) {
            int ndex = src[s * GB2_SLOT_STRIDE];
            if (ndex < 1 || ndex > 251) continue;
            int bit = ndex - 1;
            rawData_[L.dexSeen + (bit >> 3)] |= (1 << (bit & 7));
            rawData_[L.dexCaught + (bit >> 3)] |= (1 << (bit & 7));
        }
    }

    // File checksums (u16 LE sum) at both positions (PKHeX SetChecksums).
    uint16_t cks = 0;
    for (int i = 0x2009; i <= L.cksEnd; i++) cks += rawData_[i];
    rawData_[L.cksPos1] = cks & 0xFF;
    rawData_[L.cksPos1 + 1] = (cks >> 8) & 0xFF;
    rawData_[L.cksPos2] = cks & 0xFF;
    rawData_[L.cksPos2 + 1] = (cks >> 8) & 0xFF;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveGBC: %s -> OK", path.c_str());
    return true;
}

// --- GBA save format (sector-based, for FRLG) ---

uint16_t SaveFile::checkSum32GBA(const uint8_t* data, size_t len) {
    uint32_t chk = 0;
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint32_t v;
        std::memcpy(&v, data + i, 4);
        chk += v;
    }
    return static_cast<uint16_t>(chk + (chk >> 16));
}

bool SaveFile::loadGBA(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != GBA_SAVE_SIZE)
        return false;

    file.seekg(0);
    rawData_.resize(GBA_SAVE_SIZE);
    file.read(reinterpret_cast<char*>(rawData_.data()), GBA_SAVE_SIZE);
    file.close();

    // Determine active save slot by comparing save counters at sector 0
    // Each slot = 14 sectors of 0x1000 bytes
    uint32_t counter[2] = {0, 0};
    bool valid[2] = {false, false};

    for (int slot = 0; slot < 2; slot++) {
        int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
        int bitTrack = 0;
        for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
            int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
            uint16_t id = readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID);
            if (id < GBA_SECTOR_COUNT)
                bitTrack |= (1 << id);
            if (id == 0)
                counter[slot] = readU32LE(rawData_.data() + sectorOfs + GBA_OFS_SAVE_INDEX);
        }
        valid[slot] = (bitTrack == 0x3FFF); // all 14 sectors present
    }

    if (!valid[0] && !valid[1])
        return false;
    if (!valid[0]) gbaActiveSlot_ = 1;
    else if (!valid[1]) gbaActiveSlot_ = 0;
    else gbaActiveSlot_ = (counter[1] > counter[0]) ? 1 : 0;

    // Build contiguous storage buffer from sectors 5-13 of active slot
    gbaStorage_.resize(GBA_STORAGE_SECTORS * GBA_SECTOR_USED);
    std::memset(gbaStorage_.data(), 0, gbaStorage_.size());

    int slotBase = gbaActiveSlot_ * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
    for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
        int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
        uint16_t id = readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID);
        if (id >= GBA_STORAGE_FIRST && id <= GBA_STORAGE_LAST) {
            int storageIdx = id - GBA_STORAGE_FIRST;
            std::memcpy(gbaStorage_.data() + storageIdx * GBA_SECTOR_USED,
                        rawData_.data() + sectorOfs,
                        GBA_SECTOR_USED);
        }
    }

    // Box data starts at offset 4 in storage (first 4 bytes = current box index)
    boxData_ = gbaStorage_.data() + 4;
    boxDataLen_ = gbaStorage_.size() - 4;

    // Box names start after all pokemon data:
    // 14 boxes * 30 slots * 80 bytes = 33600 bytes from boxData_
    int boxNameOfs = GBA_BOX_COUNT * GBA_SLOTS_PER_BOX * PokeCrypto::SIZE_3STORED;
    if (boxNameOfs + GBA_BOX_COUNT * GBA_BOXNAME_LEN <= static_cast<int>(boxDataLen_)) {
        boxLayoutData_ = boxData_ + boxNameOfs;
        boxLayoutLen_ = GBA_BOX_COUNT * GBA_BOXNAME_LEN;
    } else {
        boxLayoutData_ = nullptr;
        boxLayoutLen_ = 0;
    }

    loaded_ = true;
    return true;
}

bool SaveFile::saveGBA(const std::string& path) {
    if (rawData_.empty() || gbaStorage_.empty())
        return false;

    // Write modified storage back to sectors in BOTH save slots
    for (int slot = 0; slot < 2; slot++) {
        int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;

        // Copy sector structure from active slot if writing to the other slot
        if (slot != gbaActiveSlot_) {
            int activeBase = gbaActiveSlot_ * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
            std::memcpy(rawData_.data() + slotBase,
                        rawData_.data() + activeBase,
                        GBA_SECTOR_COUNT * GBA_SECTOR_SIZE);
        }

        // Write storage data back to sectors 5-13
        for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
            int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
            uint16_t id = readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID);
            if (id >= GBA_STORAGE_FIRST && id <= GBA_STORAGE_LAST) {
                int storageIdx = id - GBA_STORAGE_FIRST;
                std::memcpy(rawData_.data() + sectorOfs,
                            gbaStorage_.data() + storageIdx * GBA_SECTOR_USED,
                            GBA_SECTOR_USED);
            }
        }

        // Recalculate checksums for all 14 sectors
        for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
            int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
            uint16_t chk = checkSum32GBA(rawData_.data() + sectorOfs, GBA_SECTOR_USED);
            writeU16LE(rawData_.data() + sectorOfs + GBA_OFS_CHECKSUM, chk);
        }
    }

    // Write in-place
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f)
        f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;

    size_t written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    return written == rawData_.size();
}

uint8_t* SaveFile::findGbaSectorData(int sectionId) {
    if (rawData_.size() < GBA_SAVE_SIZE)
        return nullptr;
    int slotBase = gbaActiveSlot_ * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
    for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
        int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
        uint16_t id = readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID);
        if (id == static_cast<uint16_t>(sectionId))
            return rawData_.data() + sectorOfs;
    }
    return nullptr;
}

// --- Gen 4/5 (DS .sav dumps, PKHeX SAV4*/SAV5BW.cs) — read-only v1 ---

// CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF, no xorout): PKHeX
// Checksums.CRC16_CCITT, block checksum for Gen4/Gen5 saves. Verified
// against a real Diamond save (General block -> stored u16).
static uint16_t crc16CcittFalse(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

bool SaveFile::loadDS4(const std::string& path) {
    // Gen4 NDS flash (PKHeX SAV4*.cs): 512KB = 2 partitions x 256KB.
    dsRomCode_ = 0;
    dsGameByte_ = 0;
    // block + Storage block; layouts differ per game (DP/Pt/HGSS sizes
    // below), so bytes alone pick the layout: footer magic (INT/KOR) on both
    // blocks of some partition, plus slots that decrypt to valid checksums.
    struct Layout { Ds4Layout id; int gSize; int sSize; int sStart; int footer; int boxBase; int boxStride; };
    static constexpr Layout LAYOUTS[3] = {
        { Ds4Layout::DP,   0xC100, 0x121E0, 0xC100, 0x14, 4, 0xFF0 },
        { Ds4Layout::PT,   0xCF2C, 0x121E4, 0xCF2C, 0x14, 4, 0xFF0 },
        { Ds4Layout::HGSS, 0xF628, 0x12310, 0xF700, 0x10, 0, 0x1000 },
    };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) != DS_SAVE_SIZE)
        return false;
    file.seekg(0);
    rawData_.resize(DS_SAVE_SIZE);
    file.read(reinterpret_cast<char*>(rawData_.data()), DS_SAVE_SIZE);
    if (!file)
        return false;

    auto u32le = [&](size_t o) -> uint32_t {
        return static_cast<uint32_t>(rawData_[o]) |
               (static_cast<uint32_t>(rawData_[o + 1]) << 8) |
               (static_cast<uint32_t>(rawData_[o + 2]) << 16) |
               (static_cast<uint32_t>(rawData_[o + 3]) << 24);
    };
    auto magicOk = [&](const uint8_t* blk, int len) -> bool {
        uint32_t m = static_cast<uint32_t>(blk[len - 8]) |
                     (static_cast<uint32_t>(blk[len - 7]) << 8) |
                     (static_cast<uint32_t>(blk[len - 6]) << 16) |
                     (static_cast<uint32_t>(blk[len - 5]) << 24);
        return m == 0x20060623 || m == 0x20070903; // INT / KOR
    };
    // A box slot holds a real mon when it decrypts to a valid PK4 checksum.
    // Zero slots are skipped before decrypt (crypting zeros != zeros).
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[136] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[136];
        PokeCrypto::decryptArray45(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 136; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };

    const Layout* best = nullptr;
    int bestPart = 0;
    int bestScore = -1;
    int bestSlots = 0;
    for (const Layout& L : LAYOUTS) {
        for (int p = 0; p < 2; p++) {
            size_t gBase = static_cast<size_t>(p) * DS_PARTITION;
            size_t sBase = gBase + static_cast<size_t>(L.sStart);
            if (sBase + static_cast<size_t>(L.sSize) > DS_SAVE_SIZE)
                continue;
            const uint8_t* gBlk = rawData_.data() + gBase;
            const uint8_t* sBlk = rawData_.data() + sBase;
            if (!magicOk(gBlk, L.gSize) || !magicOk(sBlk, L.sSize))
                continue;
            // Block CRCs must validate (PKHeX GetBlockChecksumValid).
            uint16_t gStored = static_cast<uint16_t>(gBlk[L.gSize - 2] | (gBlk[L.gSize - 1] << 8));
            uint16_t sStored = static_cast<uint16_t>(sBlk[L.sSize - 2] | (sBlk[L.sSize - 1] << 8));
            if (crc16CcittFalse(gBlk, static_cast<size_t>(L.gSize) - static_cast<size_t>(L.footer)) != gStored)
                continue;
            if (crc16CcittFalse(sBlk, static_cast<size_t>(L.sSize) - static_cast<size_t>(L.footer)) != sStored)
                continue;
            int n = 0;
            for (int b = 0; b < 18 && n < 3; b++)
                for (int s = 0; s < DS_BOX_SLOTS && n < 3; s++) {
                    size_t o = (sBase + static_cast<size_t>(L.boxBase)) +
                               static_cast<size_t>(b) * static_cast<size_t>(L.boxStride) +
                               static_cast<size_t>(s) * DS_SLOT_SIZE;
                    if (slotValid(rawData_.data() + o))
                        n++;
                }
            // Score prefers partitions with real mons, but CRC+magic alone
            // accept (an empty fresh save has no slots yet).
            int score = 1000 + n;
            if (score > bestScore) {
                bestScore = score;
                bestSlots = n;
                best = &L;
                bestPart = p;
            }
        }
    }
    if (!best) {
        DebugLog::line("loadDS4: %s -> nessun layout valido (magic/crc)", path.c_str());
        return false;
    }
    // Active partition: higher save index (u32 at block end - footer size);
    // ties keep the probed partition. Logged, assumption documented.
    {
        size_t g0 = 0, g1 = DS_PARTITION;
        uint32_t i0 = u32le(g0 + static_cast<size_t>(best->gSize) - static_cast<size_t>(best->footer));
        uint32_t i1 = u32le(g1 + static_cast<size_t>(best->gSize) - static_cast<size_t>(best->footer));
        if (i0 != i1)
            bestPart = (i1 > i0) ? 1 : 0;
        DebugLog::line("loadDS4: %s -> layout %d part %d (idx %u vs %u, slot validi %d)",
                       path.c_str(), (int)best->id, bestPart, i0, i1, bestSlots);
    }
    ds4Layout_ = best->id;
    // HGSS exact game from the ROMCode byte (General+Trainer1+0x1C).
    dsRomCode_ = 0;
    if (best->id == Ds4Layout::HGSS) {
        size_t gBase = static_cast<size_t>(bestPart) * DS_PARTITION;
        dsRomCode_ = rawData_[gBase + 0x64 + 0x1C];
    }
    size_t sBase = static_cast<size_t>(bestPart) * DS_PARTITION + static_cast<size_t>(best->sStart);
    const int BOXES = 18;
    dsStorage_.assign(static_cast<size_t>(BOXES) * DS_BOX_SLOTS * DS_SLOT_SIZE, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < DS_BOX_SLOTS; s++) {
            size_t o = (sBase + static_cast<size_t>(best->boxBase)) +
                       static_cast<size_t>(b) * static_cast<size_t>(best->boxStride) +
                       static_cast<size_t>(s) * DS_SLOT_SIZE;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * DS_BOX_SLOTS + s) * DS_SLOT_SIZE;
            static const uint8_t ZERO[136] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue; // corrupt: hidden + counted, never shown as garbage
            }
            std::memcpy(dst, slot, DS_SLOT_SIZE);
        }
    if (badSlots > 0)
        DebugLog::line("loadDS4: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = nullptr; // TODO v2: nomi box (Gen4 codec via FFI)
    boxLayoutLen_ = 0;
    loaded_ = true;
    return true;
}

bool SaveFile::loadDS5(const std::string& path) {
    // Gen5 BW (PKHeX SAV5BW): flat 512KB, no partitions.
    dsRomCode_ = 0;
    dsGameByte_ = 0;
    // Gen5 BW (PKHeX SAV5BW): flat 512KB, no partitions. Boxes at
    // 0x400 + box*0x1000 (30 x 136B slots), party at 0x18E08 (count at
    // 0x18E04, 6 x 220B), PlayerData block at 0x19400 (OT at +4, Game at
    // +0x1F: 20 = White, 21 = Black). B2W2 has another block map (later).
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) != DS_SAVE_SIZE)
        return false;
    file.seekg(0);
    rawData_.resize(DS_SAVE_SIZE);
    file.read(reinterpret_cast<char*>(rawData_.data()), DS_SAVE_SIZE);
    if (!file)
        return false;

    uint8_t game = rawData_[0x19400 + 0x1F];
    if (game != 20 && game != 21) {
        DebugLog::line("loadDS5: %s -> Game byte %u non BW (B2W2 dopo)", path.c_str(), game);
        return false;
    }
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[136] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[136];
        PokeCrypto::decryptArray45(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 136; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };
    int validSlots = 0;
    for (int b = 0; b < 24 && validSlots < 3; b++)
        for (int s = 0; s < DS_BOX_SLOTS && validSlots < 3; s++) {
            size_t o = 0x400 + static_cast<size_t>(b) * 0x1000 + static_cast<size_t>(s) * DS_SLOT_SIZE;
            if (slotValid(rawData_.data() + o))
                validSlots++;
        }
    if (validSlots <= 0)
        DebugLog::line("loadDS5: %s -> nessuno slot valido (save vuoto o box vuoti)", path.c_str());
    const int BOXES = 24;
    dsStorage_.assign(static_cast<size_t>(BOXES) * DS_BOX_SLOTS * DS_SLOT_SIZE, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < DS_BOX_SLOTS; s++) {
            size_t o = 0x400 + static_cast<size_t>(b) * 0x1000 + static_cast<size_t>(s) * DS_SLOT_SIZE;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * DS_BOX_SLOTS + s) * DS_SLOT_SIZE;
            static const uint8_t ZERO[136] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue;
            }
            std::memcpy(dst, slot, DS_SLOT_SIZE);
        }
    if (badSlots > 0)
        DebugLog::line("loadDS5: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    DebugLog::line("loadDS5: %s -> Game %u (%s), slot validi %d",
                   path.c_str(), game, game == 20 ? "White" : "Black", validSlots);
    dsGameByte_ = game;
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = nullptr; // TODO v2: nomi box (blocco 0) + party
    boxLayoutLen_ = 0;
    loaded_ = true;
    return true;
}
