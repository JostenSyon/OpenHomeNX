#include "save_file.h"
#include "save_file_ffi.h"
#include "poke_crypto.h"
#include "pokemon_ffi.h"
#include "species_converter.h"
#include "gen1_tables.h"
#include "handler_update.h"
#include "openhome_ffi.h"
#include "pokedex.h"
#include "binary_io.h"
#include "md5.h"
#include "debug_log.h"
#include <cctype>
#include <fstream>
#include <cerrno>
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
    boxDataLen_ = 0;
    boxLayoutData_ = nullptr;
    boxLayoutLen_ = 0;
    // Identity strip (header OT + party) must not survive across game switches
    // — the “party of Red seen on Sword” bug was exactly this stale state.
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    invalidateAllBoxCache();
    // Full per-family reset: SaveFile is reused across game switches and EVERY
    // stale buffer misroutes the next game's writes. Proven by the lost-Rhyhorn
    // bug (Sword blocks_ survived into an LGPE load → setPartySlot wrote the
    // LGPE party into the dead Sword SCBlock → edits silently lost on save).
    blocks_.clear();
    rawData_.clear();
    originalFileData_.clear();
    dsStorage_.clear();
    ds4Layout_ = Ds4Layout::DP;
    dsRomCode_ = 0;
    dsGameByte_ = 0;
    gbaStorage_.clear();
    gbaActiveSlot_ = 0;
    gbaXtra_.clear();
    gbaXtraAtEnd_ = true;
    lgpePartyIndices_.fill(LGPE_SLOT_EMPTY);
    lgpePartyCount_ = 0;
    gbStorage_.clear();
    gbStoredOrig_.clear();
    for (bool& t : gbBoxTrusted_) t = false;
    gbPartySnap_.clear();
    gbcStorage_.clear();
    gbcStoredOrig_.clear();
    for (bool& t : gbcBoxTrusted_) t = false;
    gbcIsCrystal_ = false;
    gbcBoxNamesBase_ = -1;
    gbcPartyBase_ = -1;
    gbcPartySnap_.clear();
    gbaPartyLargeOff_ = -1;
    gbaPartyPad_ = 4;
    dsPart_ = 0;

    DebugLog::line("load: path=%s gameType=%d engine=%s",
                   path.c_str(), (int)gameType_, useOpenHome() ? "OH" : "PK");

    // Drop any Rust OH handle from a PREVIOUS save/game. `SaveFile` is reused
    // across game switches; without this a stale SwSh handle would survive into
    // a BDSP/SV/LGPE load (loadBDSP/loadLGPE/loadGBA don't touch it) and
    // getCachedBox()/parity would read the wrong game's boxes.
    if (saveHandleRust_)
        saveHandleRust_.reset();

    bool ok = false;
    if (isFRLG(gameType_) || isImportedFile(gameType_))
        ok = loadGBA(path);
    else if (isGen1File(gameType_))
        ok = loadGB(path);
    else if (isGen2File(gameType_))
        ok = loadGBC(path);
    else if (isGen4File(gameType_))
        ok = loadDS4(path);
    else if (isGen5File(gameType_))
        ok = loadDS5(path);
    else if (isGen6XY(gameType_))
        ok = loadDXY(path);
    else if (isGen6ORAS(gameType_))
        ok = loadDSORAS(path);
    else if (isGen7SM(gameType_))
        ok = loadDSM(path);
    else if (isGen7USUM(gameType_))
        ok = loadDSUSUM(path);
    else if (isBDSP(gameType_))
        ok = loadBDSP(path);
    else if (isLGPE(gameType_))
        ok = loadLGPE(path);
    else
        ok = loadSCBlock(path);
    // Save vergine (zero mon in strip al load): l'exit-hook non deve mai
    // forzare il segnaposto — vuoto e lo stato legittimo iniziale (Violetto
    // inizio gioco). Solo chi SVUOTA una strip piena passa dal dialogo.
    if (ok)
        partyEmptyAtLoad_ = !hasParty();
    return ok;
}

bool SaveFile::save(const std::string& path) {
    if (!loaded_)
        return false;

    bool ok;
    if (isGen1File(gameType_))
        ok = saveGB(path);
    else if (isGen2File(gameType_))
        ok = saveGBC(path);
    else if (isGen4File(gameType_))
        ok = saveDS4(path);
    else if (isGen6XY(gameType_))
        ok = saveDXY(path);
    else if (isGen6ORAS(gameType_))
        ok = saveDSORAS(path);
    else if (isGen7SM(gameType_))
        ok = saveDSM(path);
    else if (isGen7USUM(gameType_))
        ok = saveDSUSUM(path);
    else if (isGen5File(gameType_))
        ok = saveDS5(path);
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

    // --- Party extraction for SCBlock saves (Switch: Sw/Sh, SV, ZA, LA) ---
    // PKHeX: SWSH/LA KParty=0x2985fe5d, SV/ZA KParty=0x3AA1A9AD. DecryptArray
    // is dispatched by gameType (Pokemon::loadFromEncrypted).
    dsParty_.clear();
    {
        uint32_t partyKeys[2] = {0x2985fe5d, 0x3AA1A9AD};
        SCBlock* partyBlock = nullptr;
        for (uint32_t k : partyKeys) {
            partyBlock = SaveFileFFI::findBlock(blocks_, k);
            if (partyBlock) break;
        }
        if (partyBlock && partyBlock->data.size() >= 4) {
            int pSize = gameInfo(gameType_).pkPartySize;
            if (pSize <= 0) pSize = 0x158;
            size_t n = partyBlock->data.size() / static_cast<size_t>(pSize);
            if (n > 6) n = 6;
            dsParty_.assign(6, Pokemon{});
            int valid = 0;
            for (size_t i = 0; i < n; i++) {
                const uint8_t* raw = partyBlock->data.data() + i * pSize;
                bool allZero = true;
                for (int b = 0; b < 8 && b < pSize; b++) if (raw[b] != 0) { allZero = false; break; }
                Pokemon p;
                p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, pSize);
                if (!p.isEmpty() && p.species() != 0) {
                    dsParty_[i] = p;
                    valid++;
                } else if (!allZero) {
                    DebugLog::line("loadSCBlock: %s party slot %zu decrypt empty (EC %08x)", path.c_str(), i, p.encryptionConstant());
                }
            }
            DebugLog::line("loadSCBlock: %s -> party %d/%zu (block %zu bytes, slot %d)", path.c_str(), valid, n, partyBlock->data.size(), pSize);
        } else {
            dsParty_.assign(6, Pokemon{});
            DebugLog::line("loadSCBlock: %s -> no party block found", path.c_str());
        }
    }
    // Header OT/TID for the title strip (like DS saves): reuse getTrainerInfo().
    // dsOtName_/dsTid_ were cleared in load(), so fill them now for SCBlock games.
    {
        TrainerInfo ti = getTrainerInfo();
        if (ti.valid) {
            // Convert u16string to UTF-8 (ASCII fast path, fallback 2/3 byte)
            std::string out;
            out.reserve(ti.otName.size());
            for (char16_t c : ti.otName) {
                if (c == 0) break;
                if (c < 0x80) out.push_back(static_cast<char>(c));
                else if (c < 0x800) { out.push_back(char(0xC0 | (c>>6))); out.push_back(char(0x80 | (c & 0x3F))); }
                else { out.push_back(char(0xE0 | (c>>12))); out.push_back(char(0x80 | ((c>>6)&0x3F))); out.push_back(char(0x80 | (c & 0x3F))); }
            }
            if (!out.empty()) dsOtName_ = out;
            dsTid_ = static_cast<uint16_t>(ti.id32 & 0xFFFF);
            DebugLog::line("loadSCBlock: %s -> OT '%s' TID %u", path.c_str(), dsOtName_.c_str(), dsTid_);
        }
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
    // Compact party: no gaps in the 6 slots — game slids left on save. Shrink
    // dsParty_ gaps so we never write an empty in the middle.
    if (!blocks_.empty()) {
        uint32_t keys[2] = {0x2985fe5d, 0x3AA1A9AD};
        SCBlock* pb = nullptr;
        for (uint32_t k: keys) { pb = SaveFileFFI::findBlock(blocks_, k); if (pb) break; }
        if (pb) {
            int pSize = gameInfo(gameType_).pkPartySize;
            if (pSize <= 0) pSize = 0x158;
            std::vector<Pokemon> compact;
            for (auto &pp: dsParty_) if (!pp.isEmpty()) compact.push_back(pp);
            if (compact.size() != dsParty_.size() || [&](){for(size_t i=0;i<compact.size();i++) if(compact[i].species()!=dsParty_[i].species()) return true; return false;}()) {
                // rewrite block packed
                std::memset(pb->data.data(), 0, pb->data.size());
                for (size_t i=0;i<compact.size() && i* (size_t)pSize < pb->data.size(); ++i) {
                    compact[i].getEncrypted(pb->data.data()+ i* pSize);
                }
                dsParty_.assign(6, Pokemon{});
                for (size_t i=0;i<compact.size() && i<6; ++i) dsParty_[i]=compact[i];
                DebugLog::line("saveSCBlock: compacted party %zu -> block", compact.size());
            }
        }
    }
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

    // Party: 6 x 0x158 at 0x14098 (SAV8BS PartyInfo, PKHeX)
    dsParty_.assign(6, Pokemon{});
    {
        constexpr size_t PARTY_OFF = 0x14098;
        constexpr int PARTY_SLOTS = 6;
        int pSize = gameInfo(gameType_).pkPartySize; // 0x158
        if (PARTY_OFF + PARTY_SLOTS * pSize <= rawData_.size()) {
            int valid = 0;
            for (int i = 0; i < PARTY_SLOTS; i++) {
                const uint8_t* raw = rawData_.data() + PARTY_OFF + i * pSize;
                Pokemon p; p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, pSize);
                if (!p.isEmpty() && p.species() != 0) { dsParty_[i]=p; valid++; }
            }
            DebugLog::line("loadBDSP: %s -> party %d/6", path.c_str(), valid);
        }
        TrainerInfo ti = getTrainerInfo();
        if (ti.valid) {
            std::string out; out.reserve(ti.otName.size());
            for (char16_t c: ti.otName) { if(c==0) break; if(c<0x80) out.push_back(char(c)); else if(c<0x800){out.push_back(char(0xC0|(c>>6))); out.push_back(char(0x80|(c&0x3F)));} else {out.push_back(char(0xE0|(c>>12))); out.push_back(char(0x80|((c>>6)&0x3F))); out.push_back(char(0x80|(c&0x3F)));}}
            if(!out.empty()) dsOtName_ = out;
            dsTid_ = uint16_t(ti.id32 & 0xFFFF);
        }
    }

    loaded_ = true;
    return true;
}

bool SaveFile::saveBDSP(const std::string& path) {
    if (rawData_.empty())
        return false;

    // Compact party: pack left, no empty in middle (like game does on save)
    {
        constexpr size_t OFF = 0x14098;
        int pSize = gameInfo(gameType_).pkPartySize;
        std::vector<Pokemon> compact;
        for (auto &pp: dsParty_) if (!pp.isEmpty()) compact.push_back(pp);
        if (compact.size() < 6) {
            // rewrite raw party region packed
            std::memset(rawData_.data()+OFF, 0, 6 * pSize);
            for (size_t i=0;i<compact.size(); ++i) compact[i].getEncrypted(rawData_.data()+OFF + i * pSize);
            dsParty_.assign(6, Pokemon{});
            for (size_t i=0;i<compact.size() && i<6; ++i) dsParty_[i]=compact[i];
        }
    }

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

Pokemon SaveFile::getPartySlot(int idx) const {
    if (idx < 0 || idx >= (int)dsParty_.size()) return Pokemon{};
    return dsParty_[idx];
}
void SaveFile::setPartySlot(int idx, const Pokemon& pkm) {
    if (idx < 0 || idx >= 6) return;
    Pokemon toWrite = pkm;
    toWrite.gameType_ = gameType_;
    DebugLog::line("setPartySlot idx %d %s (%u) %s", idx, toWrite.displayName().c_str(), toWrite.species(), toWrite.isEmpty()?"empty":"");
    // Normalize compact vectors (GB/GBA/DS push_back) to fixed-6 WITHOUT
    // losing content: the old assign(6) wiped every member except the incoming
    // one (proven by log: LeafGreen party=1 -> clear slot 0 -> party=0 with a
    // mon still present). Positions preserved, empties padded.
    if ((int)dsParty_.size() != 6) {
        std::vector<Pokemon> fixed(6);
        for (size_t i = 0, j = 0; i < dsParty_.size() && j < 6; i++)
            if (!dsParty_[i].isEmpty()) fixed[j++] = dsParty_[i];
        dsParty_ = std::move(fixed);
    }
    // UNIVERSAL (tutte le famiglie tranne LGPE): mai persistere una squadra
    // vuota — nessuno stato valido di gioco la produce (Smeraldo count-0 ->
    // glitch; vale per posizionali SCBlock/BDSP/XY/SM, count byte GB/GBC/DS/
    // GBA, pointer LGPE). La memoria segue la mano (strip si svuota, B
    // annulla), il disco tiene l'ultimo party valido; l'exit-hook offre il
    // segnaposto (Caterpie GBA, Magikarp altrove) o blocca l'uscita. Copre
    // TUTTI i persist, anche bank-switch senza uscita dal gioco.
    // LGPE ESENTE: non ha count, ha pointer — e pointer EMPTY e legale. Il
    // guard qui creava divergenza memoria/disco (pointer tenuto + cella
    // tenuta + copia in mano = duplicatrice infinita): pick libera sempre
    // pointer+cella subito, exit dialog resta come rete.
    if (!isLGPE(gameType_)) {
        bool anyLeft = !toWrite.isEmpty();
        for (size_t i = 0; i < dsParty_.size() && !anyLeft; i++)
            if ((int)i != idx && !dsParty_[i].isEmpty()) anyLeft = true;
        if (!anyLeft) {
            if (idx >= 0 && idx < (int)dsParty_.size()) dsParty_[idx] = toWrite;
            dirty_ = true; invalidateAllBoxCache();
            DebugLog::line("setPartySlot: party emptied in memory, disk keeps last valid party");
            return;
        }
    }
    // Persist to underlying storage per family — block index == party index.
    // The SCBlock branch is gated on the game REALLY being SCBlock-based, not
    // just on blocks_ being non-empty: a stale blocks_ from a previous game
    // used to swallow LGPE/DS writes into a dead block (lost-Rhyhorn bug).
    // load() clears everything, this gate is the second lock on the door.
    bool scGame = isSwSh(gameType_) || isSV(gameType_) ||
                  gameType_ == GameType::LA || gameType_ == GameType::ZA;
    if (scGame && !blocks_.empty()) {
        uint32_t keys[2] = {0x2985fe5d, 0x3AA1A9AD};
        SCBlock* pb = nullptr;
        for (uint32_t k: keys) { pb = SaveFileFFI::findBlock(blocks_, k); if (pb) break; }
        if (pb) {
            int pSize = gameInfo(gameType_).pkPartySize;
            if (pSize <= 0) pSize = 0x158;
            if ((size_t)(idx * pSize + pSize) <= pb->data.size()) {
                if (toWrite.isEmpty()) std::memset(pb->data.data() + idx * pSize, 0, pSize);
                else toWrite.getEncrypted(pb->data.data() + idx * pSize);
                DebugLog::line("setPartySlot block %08x idx %d pSize %d -> %s", pb->key, idx, pSize, toWrite.isEmpty()?"zero":"enc");
            }
        }
    } else if (isBDSP(gameType_)) {
        constexpr size_t OFF = 0x14098;
        int pSize = gameInfo(gameType_).pkPartySize;
        if (OFF + (size_t)idx * pSize + pSize <= rawData_.size()) {
            uint8_t* dst = rawData_.data() + OFF + idx * pSize;
            if (toWrite.isEmpty()) std::memset(dst, 0, pSize);
            else toWrite.getEncrypted(dst);
        }
    } else if (gameType_ == GameType::X || gameType_ == GameType::Y ||
               gameType_ == GameType::OMEGA_RUBY || gameType_ == GameType::ALPHA_SAPPHIRE) {
        constexpr size_t OFF = 0x14200; constexpr int PS=260; // XY e ORAS: stesso blocco party
        if (OFF + (size_t)idx*PS + PS <= rawData_.size()) {
            uint8_t* dst = rawData_.data()+OFF+idx*PS;
            if (toWrite.isEmpty()) std::memset(dst,0,PS); else toWrite.getEncrypted(dst);
        }
    } else if (gameType_ == GameType::SUN || gameType_ == GameType::MOON) {
        constexpr size_t OFF = 0x01400; constexpr int PS=260;
        if (OFF + (size_t)idx*PS + PS <= rawData_.size()) {
            uint8_t* dst = rawData_.data()+OFF+idx*PS;
            if (toWrite.isEmpty()) std::memset(dst,0,PS); else toWrite.getEncrypted(dst);
        }
    } else if (gameType_ == GameType::ULTRA_SUN || gameType_ == GameType::ULTRA_MOON) {
        constexpr size_t OFF = 0x01600; constexpr int PS=260;
        if (OFF + (size_t)idx*PS + PS <= rawData_.size()) {
            uint8_t* dst = rawData_.data()+OFF+idx*PS;
            if (toWrite.isEmpty()) std::memset(dst,0,PS); else toWrite.getEncrypted(dst);
        }
    } else if (isFRLG(gameType_) || isImportedFile(gameType_)) {
        // GBA: rewrite the packed party (count + 6x100B) at the scanned offset,
        // in BOTH save slots like the box storage. Without this, party edits
        // lived only in dsParty_ and were lost on save even with dirty_ set
        // (lost-Espeon bug, Smeraldo).
        // Battle tail [80..100] is PLAINTEXT in Gen3 saves (crypt/shuffle cover
        // only [32..80)): party-origin mons already carry it in data[80..100]
        // (preserved verbatim, incl. hurt HP); box-origin mons have zeros there
        // (boxes store no battle state) and get a fresh FFI-computed tail
        // (= in-game withdraw behavior). Never write zero tails.
        if (gbaPartyLargeOff_ >= 0) {
            // Compact WITH the just-written slot applied (dsParty_[idx] is
            // only updated in the tail below).
            std::vector<Pokemon> cur = dsParty_;
            if ((int)cur.size() <= idx) cur.resize(idx + 1);
            cur[idx] = toWrite;
            std::vector<Pokemon> compact;
            for (auto& pp : cur)
                if (!pp.isEmpty() && pp.species() != 0) compact.push_back(pp);
            // (Il caso compact vuoto e intercettato dal guard universale sopra:
            // qui compact non e mai vuoto, count 0 mai scritto.)
            for (int slot = 0; slot < 2; slot++) {
                long co = gbaLargeToRaw(static_cast<size_t>(gbaPartyLargeOff_), slot);
                if (co >= 0)
                    rawData_[static_cast<size_t>(co)] = static_cast<uint8_t>(compact.size());
                for (int i = 0; i < 6; i++) {
                    long so = gbaLargeToRaw(static_cast<size_t>(gbaPartyLargeOff_) +
                                            static_cast<size_t>(gbaPartyPad_) +
                                            static_cast<size_t>(i) * 100, slot);
                    if (so < 0) continue;
                    uint8_t* dst = rawData_.data() + so;
                    std::memset(dst, 0, 100);
                    if (i >= static_cast<int>(compact.size())) continue;
                    Pokemon w = compact[i];
                    w.gameType_ = gameType_;
                    w.getEncrypted(dst); // 80B PK3 record, checksum refreshed
                    static const uint8_t ZERO20[20] = {};
                    if (std::memcmp(w.data.data() + 80, ZERO20, 20) == 0) {
                        uint8_t tail[20] = {};
                        if (!OpenHomeNX::pk3PartyTail(w.data.data(), tail)) {
                            DebugLog::line("setPartySlot GBA: tail compute failed spc=%u",
                                           w.species());
                            continue; // slot resta azzerato: visibile, mai mezza scrittura
                        }
                        std::memcpy(dst + 80, tail, 20);
                    } else {
                        std::memcpy(dst + 80, w.data.data() + 80, 20); // coda originale
                    }
                }
            }
            DebugLog::line("setPartySlot GBA party=%zu both slots (large+%x pad %d)",
                           compact.size(), gbaPartyLargeOff_, gbaPartyPad_);
        } else {
            DebugLog::line("setPartySlot GBA: no party offset (read-only party)");
        }
    } else if (isLGPE(gameType_)) {
        if (toWrite.isEmpty()) {
            // Pointer e cella sempre insieme (LGPE e esente dal guard:
            // pointer EMPTY e legale, quindi si libera subito — mai
            // divergenza memoria/disco, mai duplicatrice).
            int oldFlat = lgpeFlatOfParty(idx);
            setLGPEPartyPointer(idx, LGPE_SLOT_EMPTY);
            if (oldFlat >= 0)
                lgpeZeroFlatSlot(oldFlat);
        } else {
            uint16_t ptr = (idx < (int)lgpePartyIndices_.size()) ? lgpePartyIndices_[idx] : LGPE_SLOT_EMPTY;
            int total = LGPE_BOX_COUNT * LGPE_SLOTS_PER_BOX;
            if (ptr >= total || ptr == LGPE_SLOT_EMPTY) {
                int pSize = PokeCrypto::SIZE_6PARTY; int found=-1;
                for(int i=0;i<total;i++){ uint32_t ec; std::memcpy(&ec, boxData_+i*pSize,4); if(ec==0){found=i;break;} }
                if(found>=0) ptr=(uint16_t)found; else ptr=0;
                setLGPEPartyPointer(idx, ptr);
                DebugLog::line("setPartySlot LGPE idx %d -> new cell %d", idx, ptr);
            } else {
                DebugLog::line("setPartySlot LGPE idx %d -> cell %d (kept)", idx, ptr);
            }
            size_t off=(size_t)ptr*PokeCrypto::SIZE_6PARTY;
            if(off+PokeCrypto::SIZE_6PARTY <= boxDataLen_){ toWrite.getEncrypted(boxData_+off); }
        }
    }
    // Registra nel Pokedex (2026-09-18): setBoxSlot lo fa da sempre, ma
    // setPartySlot non l'ha MAI fatto -- qualsiasi Pokemon piazzato
    // direttamente in squadra (trasferimento in party, non in una box)
    // restava fuori dal Pokedex per sempre (bug osservato: Espeon in
    // squadra ma assente dal dex). registerPokemon() e' idempotente e
    // gia' scarta da sola vuoti/uova.
    if (!toWrite.isEmpty())
        Pokedex::registerPokemon(*this, toWrite);

    dsParty_[idx]=toWrite;
    dirty_=true; invalidateAllBoxCache();
}
void SaveFile::clearPartySlot(int idx) {
    Pokemon empty; empty.gameType_ = gameType_;
    setPartySlot(idx, empty);
}

// Gen3 text encoder, ASCII subset (PKHeX StringConverter G3: A-Z 0xBB..,
// 0-9 0xA1.., spazio 0x00, terminatore 0xFF). Basta per nickname/OT fallback.
static uint8_t gen3EncodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(0xBB + (c - 'A'));
    if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(0xD5 + (c - 'a'));
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(0xA1 + (c - '0'));
    return 0x00;
}
static void gen3EncodeName(const std::string& s, uint8_t* dst, size_t len) {
    size_t i = 0;
    for (; i < s.size() && i < len; i++) dst[i] = gen3EncodeChar(s[i]);
    if (i < len) dst[i++] = 0xFF;
    for (; i < len; i++) dst[i] = 0x00;
}

// I 4 sottoblocchi PK3 in memoria (decifrati) sono SEMPRE in ordine canonico
// G,A,E,M — lo shuffle per PID%24 esiste solo nel record cifrato su disco
// (PokeCrypto::encryptArray3 lo applica in uscita). Qui NON si shuffla.

bool SaveFile::placeCaterpiePlaceholder() {
    if (!isFRLG(gameType_) && !isImportedFile(gameType_)) return false;
    // Identita OT: copia raw (nome Gen3 + lingua + TID/SID) dal primo mon dei
    // box — il segnaposto risulta nativo del save. Fallback: TID sezione 0.
    uint32_t otId = 0;
    uint8_t ot[7] = {};
    uint8_t lang = 2;
    bool haveId = false;
    for (int b = 0; b < GBA_BOX_COUNT && !haveId; b++) {
        const auto& box = getCachedBox(b);
        for (auto& m : box) {
            if (!m.isEmpty() && m.data.size() >= 0x1C) {
                std::memcpy(&otId, m.data.data() + 4, 4);
                std::memcpy(ot, m.data.data() + 0x14, 7);
                lang = m.data.data()[0x12];
                haveId = true;
                break;
            }
        }
    }
    if (!haveId) {
        if (uint8_t* sec0 = findGbaSectorData(0)) {
            otId = static_cast<uint32_t>(readU16LE(sec0 + 0x0A)) |
                   (static_cast<uint32_t>(readU16LE(sec0 + 0x0C)) << 16);
            gen3EncodeName(dsOtName_, ot, 7);
        }
    }
    // Caterpie fisso L5: PID nonzero arbitrario, mosse Tackle/String Shot,
    // stat da formula Gen3 con base 45/30/35/45/20/20 (IV/EV 0):
    // HP 19, Atk 8, Def 8, Spe 9, SpA 7, SpD 7. Exp Medium-Fast L5 = 125.
    Pokemon p; p.gameType_ = gameType_;
    p.data.fill(0); // std::array: azzera tutto, uso i primi 100B (PK3 party)
    uint8_t* d = p.data.data();
    const uint32_t pid = 0xC0FFEE10;
    auto w16 = [&](size_t o, uint16_t v) { d[o] = v & 0xFF; d[o+1] = (v >> 8) & 0xFF; };
    auto w32 = [&](size_t o, uint32_t v) { w16(o, v & 0xFFFF); w16(o+2, (v >> 16) & 0xFFFF); };
    w32(0x00, pid);
    w32(0x04, otId);
    std::string nick = SpeciesName::get(10);
    if (nick.empty()) nick = "Caterpie";
    for (char& c : nick) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    gen3EncodeName(nick, d + 8, 10);
    d[0x12] = lang;
    d[0x13] = 0;
    std::memcpy(d + 0x14, ot, 7);
    d[0x1B] = 0;
    d[0x1C] = 0; d[0x1D] = 0; // checksum: la calcola refreshChecksum()
    d[0x1E] = 0; d[0x1F] = 0;
    uint8_t G[12] = {}, A[12] = {}, E[12] = {}, M[12] = {};
    uint16_t internal = SpeciesConverter::getInternal3(10);
    G[0] = internal & 0xFF; G[1] = (internal >> 8) & 0xFF;
    G[4] = 125; // exp L5 (Medium Fast: 125), byte alti 0
    G[9] = 70;  // friendship base Caterpie
    A[0] = 33; A[2] = 81;             // Tackle, String Shot
    A[8] = 35; A[9] = 40;             // PP
    M[2] = 5;                         // met level 5, OT gender 0
    uint8_t game = 3;                 // PKHeX GameVersion: E=3,R=2,S=1,FR=4,LG=5
    if (gameType_ == GameType::RUBY) game = 2;
    else if (gameType_ == GameType::SAPPHIRE) game = 1;
    else if (isFRLG(gameType_))
        game = (gameType_ == GameType::LG || gameType_ == GameType::LG_ES ||
                gameType_ == GameType::LG_DE || gameType_ == GameType::LG_IT ||
                gameType_ == GameType::LG_FR || gameType_ == GameType::LG_JA) ? 5 : 4;
    M[3] = static_cast<uint8_t>(game | (4 << 4)); // game + Ball 4 (Poke Ball)
    std::memcpy(d + 0x20, G, 12); // canonico: G,A,E,M (vedi nota sopra)
    std::memcpy(d + 0x2C, A, 12);
    std::memcpy(d + 0x38, E, 12);
    std::memcpy(d + 0x44, M, 12);
    w32(0x50, 0);                     // status
    d[0x54] = 5; d[0x55] = 0;         // level, pokerus
    w16(0x56, 19); w16(0x58, 19);     // curHP, maxHP
    w16(0x5A, 8); w16(0x5C, 8);       // atk, def
    w16(0x5E, 9);                     // spe
    w16(0x60, 7); w16(0x62, 7);       // spa, spd
    p.refreshChecksum();
    if (!p.pk3ChecksumValid()) {
        DebugLog::line("placeCaterpiePlaceholder: self-check FAILED, abort");
        return false;
    }
    setPartySlot(0, p);
    DebugLog::line("placeCaterpiePlaceholder: Caterpie L5 in slot 0 (pid %08x)", pid);
    return true;
}

// Magikarp L5 Splash esiste in tutte le gen (1-9 + LGPE/BDSP/LA/ZA) con mosse
// legali ovunque: il segnaposto ideale fuori GBA.
bool SaveFile::placePlaceholder() {
    if (isFRLG(gameType_) || isImportedFile(gameType_))
        return placeCaterpiePlaceholder();
    int gen = ohTargetGenFor(gameType_);
    if (gen <= 0) return false;
    PkmHandle* h = OpenHomeNX::generateTestPkm(129, 5, 150, 0, 0, 0);
    if (!h) return false;
    PkmHandle* out = PokemonFFI::transfer(h, static_cast<uint32_t>(gen));
    std::string gen1Ot, gen1Nick;
    if (out && gen == 1) {
        gen1Ot = OpenHomeNX::ohpkmTrainerName(out);
        gen1Nick = OpenHomeNX::ohpkmNickname(out);
    }
    std::vector<uint8_t> bytes;
    if (out) bytes = OpenHomeNX::getPkmBoxBytesForGen(out, static_cast<uint32_t>(gen));
    OpenHomeNX::freePkm(h);
    if (out) OpenHomeNX::freePkm(out);
    Pokemon p;
    p.gameType_ = gameType_;
    p.data.fill(0);
    if (bytes.empty() || bytes.size() > p.data.size()) {
        DebugLog::line("placePlaceholder: no bytes gen %d", gen);
        return false;
    }
    std::memcpy(p.data.data(), bytes.data(), bytes.size());
    if (gen == 1) fillGen1Names(p, gen1Ot, gen1Nick);
    // Prima cella libera della strip (NON slot 0 fisso): nelle famiglie
    // posizionali lo slot 0 potrebbe tenere un altro mon (sovrascritto =
    // perso) mentre il fantasma sta altrove. Qui c'e sempre posto (hasParty
    // falso prima della chiamata).
    int slot = 0;
    while (slot < 6 && slot < (int)dsParty_.size() && !dsParty_[slot].isEmpty()) slot++;
    if (slot >= 6) {
        DebugLog::line("placePlaceholder: strip senza buchi (impossibile)");
        return false;
    }
    setPartySlot(slot, p);
    bool ok = hasParty();
    DebugLog::line("placePlaceholder: Magikarp L5 gen %d -> slot %d (%s)", gen, slot, ok ? "ok" : "FAILED");
    return ok;
}

int SaveFile::lgpeFlatOfParty(int i) const {
    if (!isLGPE(gameType_) || i < 0 || i >= 6) return -1;
    uint16_t v = lgpePartyIndices_[i];
    int total = LGPE_BOX_COUNT * LGPE_SLOTS_PER_BOX;
    if (v == LGPE_SLOT_EMPTY || v >= total) return -1;
    return (int)v;
}

void SaveFile::lgpeZeroFlatSlot(int flat) {
    if (!isLGPE(gameType_)) return;
    int total = LGPE_BOX_COUNT * LGPE_SLOTS_PER_BOX;
    if (flat < 0 || flat >= total) return;
    size_t off = (size_t)flat * PokeCrypto::SIZE_6PARTY;
    if (off + PokeCrypto::SIZE_6PARTY <= boxDataLen_) {
        std::memset(boxData_ + off, 0, PokeCrypto::SIZE_6PARTY);
        invalidateAllBoxCache();
        dirty_ = true;
        DebugLog::line("lgpeZeroFlatSlot %d (box %d slot %d)", flat, flat / LGPE_SLOTS_PER_BOX, flat % LGPE_SLOTS_PER_BOX);
    }
}

void SaveFile::refreshPartyEntryFromPointer(int idx) {
    if (!isLGPE(gameType_) || idx < 0 || idx >= 6) return;
    if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
    Pokemon p; p.gameType_ = gameType_;
    int flat = lgpeFlatOfParty(idx);
    if (flat >= 0) {
        size_t off = (size_t)flat * PokeCrypto::SIZE_6PARTY;
        if (off + PokeCrypto::SIZE_6PARTY <= boxDataLen_) {
            p.loadFromEncrypted(boxData_ + off, PokeCrypto::SIZE_6PARTY);
            if (p.isEmpty() || p.species() == 0) { p = Pokemon{}; p.gameType_ = gameType_; }
        }
    }
    dsParty_[idx] = p;
}

const std::vector<Pokemon>& SaveFile::getCachedBox(int box) const {
    auto it = boxCache_.find(box);
    if (it != boxCache_.end())
        return it->second;

    DebugLog::line("getCachedBox: box=%d engine=%s", box,
                   useOpenHome() ? "OH" : "PK");

    // Mai cachare il vuoto: senza dati (load fallito, gioco cambiato a meta)
    // servi uno scratch non cachato — evita viste "box vuoto" appiccicose
    // che restano fino al prossimo invalidate (Smeraldo 2026-09-09: box
    // apparso solo dopo click in banca).
    bool haveData = (boxData_ != nullptr && boxDataLen_ > 0) ||
        (useOpenHome() && saveHandleRust_ &&
         openhome_get_box_count(saveHandleRust_.get()) > 0);
    if (!haveData) {
        thread_local std::vector<Pokemon> scratch;
        scratch.assign(slotsPerBox_ > 0 ? slotsPerBox_ : 30, Pokemon{});
        for (auto& p : scratch) p.gameType_ = gameType_;
        return scratch;
    }

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
    // Mai scrivere fuori box: l'offset e lineare e uno slot oltre
    // slotsPerBox_ cadrebbe nel box successivo (mon "teletrasportato").
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= slotsPerBox_) {
        DebugLog::line("setBoxSlot: REFUSED box=%d slot=%d (boxCount=%d spb=%d)",
                       box, slot, boxCount_, slotsPerBox_);
        return;
    }

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
    if (box < 0 || box >= boxCount_ || slot < 0 || slot >= slotsPerBox_) {
        DebugLog::line("clearBoxSlot: REFUSED box=%d slot=%d (boxCount=%d spb=%d)",
                       box, slot, boxCount_, slotsPerBox_);
        return;
    }

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
    DebugLog::line("loadLGPE: %s -> ptrs [%u,%u,%u,%u,%u,%u]", path.c_str(),
        lgpePartyIndices_[0], lgpePartyIndices_[1], lgpePartyIndices_[2],
        lgpePartyIndices_[3], lgpePartyIndices_[4], lgpePartyIndices_[5]);

    // Party: slots referenced by PokeListHeader indices (party format 0x104, same as boxes)
    dsParty_.assign(6, Pokemon{});
    {
        int pSize = PokeCrypto::SIZE_6PARTY; // 0x104 for LGPE
        for (int i = 0; i < 6; i++) {
            uint16_t idx = lgpePartyIndices_[i];
            if (idx >= 1000) continue;
            size_t off = static_cast<size_t>(idx) * pSize;
            if (off + pSize > boxDataLen_) continue;
            const uint8_t* raw = boxData_ + off;
            Pokemon p; p.gameType_ = gameType_;
            p.loadFromEncrypted(raw, pSize);
            if (!p.isEmpty() && p.species()!=0) dsParty_[i]=p;
        }
        DebugLog::line("loadLGPE: %s -> party %zu/6 (lgpePartyCount %d)", path.c_str(), dsParty_.size(), lgpePartyCount_);
        TrainerInfo ti = getTrainerInfo();
        if (ti.valid) {
            std::string out; out.reserve(ti.otName.size());
            for(char16_t c: ti.otName){ if(c==0) break; if(c<0x80) out.push_back(char(c)); else if(c<0x800){out.push_back(char(0xC0|(c>>6))); out.push_back(char(0x80|(c&0x3F)));} else {out.push_back(char(0xE0|(c>>12))); out.push_back(char(0x80|((c>>6)&0x3F))); out.push_back(char(0x80|(c&0x3F)));}}
            if(!out.empty()) dsOtName_=out;
            dsTid_=uint16_t(ti.id32 & 0xFFFF);
        }
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

    // Identity strip: OT @0x2598 (11B GB), TID BE @0x2605, party @0x2F2C
    // (count + 6 x 44B records, head 33B = box layout).
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    dsOtName_ = Gen1::decodeGbString(rawData_.data() + 0x2598, 11);
    dsTid_ = static_cast<uint16_t>((rawData_[0x2605] << 8) | rawData_[0x2606]);
    {
        int pcount = rawData_[0x2F2C];
        if (pcount > 6) pcount = 6;
        for (int i = 0; i < pcount; i++) {
            const uint8_t* rec = rawData_.data() + 0x2F2C + 8 + static_cast<size_t>(i) * 44;
            if (rec[0] == 0)
                continue;
            uint8_t buf[55] = {};
            std::memcpy(buf, rec, 33);
            Pokemon p;
            p.gameType_ = gameType_;
            p.loadFromEncrypted(buf, sizeof(buf));
            if (!p.isEmpty())
                dsParty_.push_back(p);
        }
        DebugLog::line("loadGB: %s -> OT '%s' TID %u party %zu",
                       path.c_str(), dsOtName_.c_str(), dsTid_, dsParty_.size());
        // Snapshot the raw party region (404B: count+species+FF+6x44B+OT+nick)
        // for tail-preserving write-back in saveGB().
        gbPartySnap_.assign(rawData_.data() + GB_PARTY,
                            rawData_.data() + GB_PARTY + 404);
    }

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

    // Party write-back (was read-only: edits died in dsParty_). Packed
    // PokeList1: [count][species x6][0xFF][6x44B rec][OT 6x11][nick 6x11].
    // Tails: snapshot-match on the 33B head (exact, incl. hurt HP), else
    // FFI-computed fresh (withdraw behavior). Abort (false) if a tail can't
    // be built — never write a corrupt party.
    if (!gbPartySnap_.empty()) {
        std::vector<Pokemon> team;
        for (auto& pp : dsParty_)
            if (!pp.isEmpty() && pp.species() != 0) team.push_back(pp);
        if (team.size() > 6) team.resize(6);
        uint8_t region[404] = {};
        region[0] = static_cast<uint8_t>(team.size());
        bool used[6] = {};
        bool partyOk = true;
        for (size_t i = 0; i < team.size() && partyOk; i++) {
            const Pokemon& m = team[i];
            uint8_t internal = m.data[0]; // GB internal species index
            int ndex = Gen1::internalToNdex(internal);
            if (ndex < 1 || ndex > 151) { partyOk = false; break; }
            region[1 + i] = internal;
            uint8_t* rec = region + 8 + i * 44;
            std::memcpy(rec, m.data.data(), 33);
            int hit = -1;
            for (int j = 0; j < 6; j++) {
                if (used[j]) continue;
                if (std::memcmp(gbPartySnap_.data() + 8 + j * 44, m.data.data(), 33) == 0) {
                    hit = j; used[j] = true; break;
                }
            }
            if (hit >= 0) {
                std::memcpy(rec + 33, gbPartySnap_.data() + 8 + hit * 44 + 33, 11);
            } else {
                uint8_t tail[11] = {};
                if (!OpenHomeNX::gen1PartyTail(static_cast<uint16_t>(ndex), m.data.data(), tail)) {
                    DebugLog::line("saveGB: party tail compute failed spc=%d", ndex);
                    partyOk = false; break;
                }
                std::memcpy(rec + 33, tail, 11);
            }
            std::memcpy(region + 8 + 264 + i * 11, m.data.data() + 33, 11); // OT GB
            std::memcpy(region + 8 + 264 + 66 + i * 11, m.data.data() + 44, 11); // nick GB
        }
        if (!partyOk)
            return false;
        if (team.size() < 6)
            region[1 + team.size()] = 0xFF; // species terminator
        else
            region[7] = 0xFF;
        std::memcpy(rawData_.data() + GB_PARTY, region, sizeof(region));
        dsParty_.assign(6, Pokemon{});
        for (size_t i = 0; i < team.size() && i < 6; i++)
            dsParty_[i] = team[i];
        DebugLog::line("saveGB: party %zu written", team.size());
    }

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

size_t SaveFile::gen2DexCaughtOffset() const {
    if (!isGen2File(gameType_)) return 0;
    return static_cast<size_t>(gameType_ == GameType::CRYSTAL ? GBC_C.dexCaught : GBC_GS.dexCaught);
}

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
    // Alcuni emulatori/handheld aggiungono 48 byte RTC in coda (es. TGB Dual, alcuni core RetroArch)
    // rendendo il file 32816 invece di 32768. Gestiamo entrambi, come per GBA Delta 16B.
    constexpr size_t RTC_EXTRA = 48;
    if (fileSize != GB2_SAVE_SIZE && fileSize != GB2_SAVE_SIZE + RTC_EXTRA)
        return false;

    file.seekg(0);
    std::vector<uint8_t> buf(fileSize);
    file.read(reinterpret_cast<char*>(buf.data()), fileSize);
    file.close();
    // Se 32816, trova la finestra valida (head o tail) provando i checksum
    if (fileSize == GB2_SAVE_SIZE + RTC_EXTRA) {
        // Prova head (primi 32768)
        bool headOk = false, tailOk = false;
        {
            bool isCrystalTmp = (gameType_ == GameType::CRYSTAL);
            const GbcLayout& LTmp = isCrystalTmp ? GBC_C : GBC_GS;
            headOk = gbcChecksumValid(buf.data(), LTmp);
            tailOk = gbcChecksumValid(buf.data() + RTC_EXTRA, LTmp);
        }
        if (headOk) {
            rawData_.assign(buf.data(), buf.data() + GB2_SAVE_SIZE);
        } else if (tailOk) {
            rawData_.assign(buf.data() + RTC_EXTRA, buf.data() + RTC_EXTRA + GB2_SAVE_SIZE);
            DebugLog::line("loadGBC: %s 32816 RTC tail window used", path.c_str());
        } else {
            // Nessuna finestra valida, prova comunque head (verrà scartato dal checksum sotto)
            rawData_.assign(buf.data(), buf.data() + GB2_SAVE_SIZE);
            DebugLog::line("loadGBC: %s 32816 no valid window, using head", path.c_str());
        }
    } else {
        rawData_.assign(buf.data(), buf.data() + GB2_SAVE_SIZE);
    }

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

    // Identity strip: OT @0x200B (11B GB), TID BE @0x2009, party @0x288A
    // (GS) / 0x2865 (Crystal): count + 6 x 48B records, head 32B = box.
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    dsOtName_ = Gen1::decodeGbString(rawData_.data() + 0x200B, 11);
    dsTid_ = static_cast<uint16_t>((rawData_[0x2009] << 8) | rawData_[0x200A]);
    {
        size_t pbase = gbcIsCrystal_ ? 0x2865 : 0x288A;
        int pcount = rawData_[pbase];
        if (pcount > 6) pcount = 6;
        for (int i = 0; i < pcount; i++) {
            const uint8_t* rec = rawData_.data() + pbase + 8 + static_cast<size_t>(i) * 48;
            if (rec[0] == 0)
                continue;
            uint8_t buf[54] = {};
            std::memcpy(buf, rec, 32);
            Pokemon p;
            p.gameType_ = gameType_;
            p.loadFromEncrypted(buf, sizeof(buf));
            if (!p.isEmpty())
                dsParty_.push_back(p);
        }
        DebugLog::line("loadGBC: %s -> OT '%s' TID %u party %zu",
                       path.c_str(), dsOtName_.c_str(), dsTid_, dsParty_.size());
        // Snapshot the raw party region (428B) for tail-preserving write-back.
        gbcPartyBase_ = static_cast<int>(pbase);
        gbcPartySnap_.assign(rawData_.data() + pbase, rawData_.data() + pbase + 428);
    }

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

    // Party write-back (was read-only). Packed PokeList2: [count][species
    // x6][0xFF][6x48B rec][OT 6x11][nick 6x11]. Tails: snapshot-match on the
    // 32B head, else FFI-computed fresh. Abort on failure, never corrupt.
    if (gbcPartyBase_ >= 0 && !gbcPartySnap_.empty()) {
        std::vector<Pokemon> team;
        for (auto& pp : dsParty_)
            if (!pp.isEmpty() && pp.species() != 0) team.push_back(pp);
        if (team.size() > 6) team.resize(6);
        uint8_t region[428] = {};
        region[0] = static_cast<uint8_t>(team.size());
        bool used[6] = {};
        bool partyOk = true;
        for (size_t i = 0; i < team.size() && partyOk; i++) {
            const Pokemon& m = team[i];
            uint16_t sp = m.species();
            if (sp < 1 || sp > 251) { partyOk = false; break; }
            region[1 + i] = static_cast<uint8_t>(sp); // ndex diretto, no tabella
            uint8_t* rec = region + 8 + i * 48;
            std::memcpy(rec, m.data.data(), 32);
            int hit = -1;
            for (int j = 0; j < 6; j++) {
                if (used[j]) continue;
                if (std::memcmp(gbcPartySnap_.data() + 8 + j * 48, m.data.data(), 32) == 0) {
                    hit = j; used[j] = true; break;
                }
            }
            if (hit >= 0) {
                std::memcpy(rec + 32, gbcPartySnap_.data() + 8 + hit * 48 + 32, 16);
            } else {
                uint8_t tail[16] = {};
                if (!OpenHomeNX::gen2PartyTail(sp, m.data.data(), tail)) {
                    DebugLog::line("saveGBC: party tail compute failed spc=%u", sp);
                    partyOk = false; break;
                }
                std::memcpy(rec + 32, tail, 16);
            }
            std::memcpy(region + 8 + 288 + i * 11, m.data.data() + 32, 11); // OT GB
            std::memcpy(region + 8 + 288 + 66 + i * 11, m.data.data() + 43, 11); // nick GB
        }
        if (!partyOk)
            return false;
        if (team.size() < 6)
            region[1 + team.size()] = 0xFF;
        else
            region[7] = 0xFF;
        std::memcpy(rawData_.data() + gbcPartyBase_, region, sizeof(region));
        dsParty_.assign(6, Pokemon{});
        for (size_t i = 0; i < team.size() && i < 6; i++)
            dsParty_[i] = team[i];
        DebugLog::line("saveGBC: party %zu written", team.size());
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

bool SaveFile::normalizeDeltaSave(const std::string& path, std::string& info) {
    // GBA_XTRA vive dentro loadGBA: qui costante locale gemella (16B, deve
    // restare uguale — i Delta aggiungono esattamente 16B ai 128K raw).
    static constexpr size_t XTRA = 16;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { info = "cannot open"; return false; }
    size_t n = static_cast<size_t>(f.tellg());
    if (n == GBA_SAVE_SIZE) { info = "already clean 128K"; return false; }
    if (n != GBA_SAVE_SIZE + XTRA) { info = "size not Delta-like"; return false; }
    std::vector<uint8_t> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), n);
    if (!f) { info = "read failed"; return false; }
    f.close();
    auto windowOk = [&](size_t base) -> bool {
        if (base + 2 * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE > d.size())
            return false;
        for (int slot = 0; slot < 2; slot++) {
            int bitTrack = 0;
            for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                size_t o = base + static_cast<size_t>(slot * GBA_SECTOR_COUNT + i) *
                           GBA_SECTOR_SIZE + GBA_OFS_SECTOR_ID;
                uint16_t id = readU16LE(d.data() + o);
                if (id < GBA_SECTOR_COUNT)
                    bitTrack |= (1 << id);
            }
            if (bitTrack == 0x3FFF)
                return true;
        }
        return false;
    };
    size_t base = 0;
    const char* where = nullptr;
    if (windowOk(0)) { base = 0; where = "head"; }
    else if (windowOk(XTRA)) { base = XTRA; where = "tail"; }
    else { info = "16B extra but no valid sector window (not touched)"; return false; }
    FILE* w = std::fopen(path.c_str(), "wb");
    if (!w) { info = "cannot rewrite"; return false; }
    size_t written = std::fwrite(d.data() + base, 1, GBA_SAVE_SIZE, w);
    std::fclose(w);
    if (written != GBA_SAVE_SIZE) { info = "short write"; return false; }
    char b[128];
    std::snprintf(b, sizeof(b), "stripped 16B (%s), now clean 128K", where);
    info = b;
    return true;
}

bool SaveFile::loadGBA(const std::string& path) {
    // Delta/iPhone: normalizza permanente (loggato) prima di leggere, cosi
    // il file diventa compatibile mGBA & co. L'auto-backup all'apertura ha
    // gia salvato l'originale. Se fallisce, logga il perche (size anomala
    // ma finestre invalide = file spurio, es. metadati AppleDouble).
    {
        std::string info;
        if (normalizeDeltaSave(path, info))
            DebugLog::line("loadGBA: Delta normalize %s: %s", path.c_str(), info.c_str());
        else if (info != "already clean 128K" && info != "cannot open")
            DebugLog::line("loadGBA: normalize no (%s): %s", path.c_str(), info.c_str());
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    gbaXtra_.clear();
    gbaXtraAtEnd_ = true;
    auto fileSize = static_cast<size_t>(file.tellg());
    // Alcuni emulatori aggiungono 16B (header/footer metadata) ai 128KB raw:
    // si prova la finestra a offset 0 e a offset 16, vince la prima con tutti
    // i 14 settori presenti. I 16B vengono conservati per la riscrittura.
    static constexpr size_t GBA_XTRA = 16;
    if (fileSize != GBA_SAVE_SIZE && fileSize != GBA_SAVE_SIZE + GBA_XTRA)
        return false;

    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    file.close();

    gbaXtra_.clear();
    gbaXtraAtEnd_ = true;
    if (fileSize == GBA_SAVE_SIZE + GBA_XTRA) {
        auto sectorsOk = [&](size_t base) -> bool {
            if (base + 2 * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE > fileSize)
                return false;
            // Basta uno dei due slot con tutti i 14 settori (come il loader).
            for (int slot = 0; slot < 2; slot++) {
                int bitTrack = 0;
                for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                    size_t o = base + static_cast<size_t>(slot * GBA_SECTOR_COUNT + i) *
                               GBA_SECTOR_SIZE + GBA_OFS_SECTOR_ID;
                    uint16_t id = readU16LE(rawData_.data() + o);
                    if (id < GBA_SECTOR_COUNT)
                        bitTrack |= (1 << id);
                }
                if (bitTrack == 0x3FFF)
                    return true;
            }
            return false;
        };
        if (sectorsOk(0)) {
            gbaXtra_.assign(rawData_.end() - GBA_XTRA, rawData_.end());
            gbaXtraAtEnd_ = true;
            rawData_.resize(GBA_SAVE_SIZE);
        } else if (sectorsOk(GBA_XTRA)) {
            gbaXtra_.assign(rawData_.begin(), rawData_.begin() + GBA_XTRA);
            gbaXtraAtEnd_ = false;
            rawData_.erase(rawData_.begin(), rawData_.begin() + GBA_XTRA);
        } else {
            DebugLog::line("loadGBA: %s -> 16B extra ma settori non validi", path.c_str());
            return false;
        }
        DebugLog::line("loadGBA: %s -> 16B extra %s, rimossi per la lettura",
                       path.c_str(), gbaXtraAtEnd_ ? "in coda" : "in testa");
    }

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

    if (!valid[0] && !valid[1]) {
        // Diagnosi remota: bitTrack + primi ID settore per slot, così dal
        // log si capisce il layout (emulatori alternativi, overdump...).
        std::string detail;
        for (int slot = 0; slot < 2; slot++) {
            int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
            int bitTrack = 0;
            std::string ids;
            for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
                uint16_t id = readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID);
                if (id < GBA_SECTOR_COUNT)
                    bitTrack |= (1 << id);
                if (i < 4) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "%04x ", id);
                    ids += b;
                }
            }
            char b[64];
            std::snprintf(b, sizeof(b), "slot%d track=%04x ids=%s", slot, bitTrack, ids.c_str());
            if (!detail.empty()) detail += " ";
            detail += b;
        }
        DebugLog::line("loadGBA: %s -> settori invalidi (%s)", path.c_str(), detail.c_str());
        return false;
    }
    if (!valid[0]) gbaActiveSlot_ = 1;
    else if (!valid[1]) gbaActiveSlot_ = 0;
    else gbaActiveSlot_ = (counter[1] > counter[0]) ? 1 : 0;

    // Recupero Pokedex per fork (2026-09-18 v2): il tentativo precedente
    // (scartare TUTTO il banco col contatore piu' alto se aveva meno
    // catture) era sbagliato -- dai log reali si e' visto che dopo il
    // chain-load mGBA continua ad avanzare il SUO banco (contatore sempre
    // piu' alto, progressi di gioco reali: nuove catture, oggetti, ecc.)
    // mentre l'altro banco resta congelato al momento del fork, con in
    // pancia SOLO le catture fatte fino a quel momento (es. il trasferimento
    // OpenHomeNX). Scartare il banco col contatore piu' alto buttava via
    // tutti i progressi successivi (ogni nuova catture veniva ignorata).
    // Fix corretto: il banco attivo resta quello col contatore piu' alto
    // (e' il piu' recente), ma i flag Pokedex "caught"/"seen" sono
    // permanenti quindi va bene unire (OR bit a bit) quelli dell'altro
    // banco in quello attivo -- si recupera cosi' l'eventuale specie
    // presente solo nel banco piu' vecchio (es. dopo un trasferimento)
    // senza perdere nessun progresso successivo. Il prossimo saveGBA()
    // ricopiera' il banco attivo (ora con l'unione) sull'altro banco.
    bool gbaPokedexMergeDirty = false;
    if (valid[0] && valid[1]) {
        constexpr int kPokedexOfs = 0x18, kCaughtOfs = 0x10, kSeenOfs = 0x44;
        constexpr int kMaxSpecies = 386;
        constexpr int kFlagBytes = (kMaxSpecies + 7) / 8; // 49 byte, bit = specie-1

        auto sectorOfs = [&](int slot, int sectionId) -> int {
            int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
            for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                int off = slotBase + i * GBA_SECTOR_SIZE;
                if (readU16LE(rawData_.data() + off + GBA_OFS_SECTOR_ID) == sectionId)
                    return off;
            }
            return -1;
        };
        auto sector0Ofs = [&](int slot) -> int { return sectorOfs(slot, 0); };

        int otherSlot = 1 - gbaActiveSlot_;
        int activeSec0 = sector0Ofs(gbaActiveSlot_);
        int otherSec0 = sector0Ofs(otherSlot);
        if (activeSec0 >= 0 && otherSec0 >= 0) {
            // Guardia identita' allenatore (2026-09-18 v4, DANNO REALE
            // osservato: dopo "Nuova Partita" un banco aveva ancora il
            // trainer/dex della partita PRECEDENTE mentre l'altro aveva
            // gia' quello nuovo -- unire ciecamente i flag caught/seen ha
            // resuscitato l'intero Pokedex della partita vecchia dentro
            // quella nuova, +27/+31 specie di colpo). Se OT name (8B @0x00)
            // o TID/SID (4B @0x0A) non coincidono tra i due banchi sono due
            // trainer/partite diverse: NON uniamo mai il Pokedex tra loro,
            // qualunque sia il contatore -- unire dex di trainer diversi
            // non e' mai corretto, a differenza del fork stesso trainer.
            bool sameTrainer =
                std::memcmp(rawData_.data() + activeSec0, rawData_.data() + otherSec0, 8) == 0 &&
                std::memcmp(rawData_.data() + activeSec0 + 0x0A, rawData_.data() + otherSec0 + 0x0A, 4) == 0;

            int mergedCaught = 0, mergedSeen = 0;
            bool mergedNatDex = false;
            if (sameTrainer) {
            uint8_t* activeCaught = rawData_.data() + activeSec0 + kPokedexOfs + kCaughtOfs;
            uint8_t* activeSeen   = rawData_.data() + activeSec0 + kPokedexOfs + kSeenOfs;
            const uint8_t* otherCaught = rawData_.data() + otherSec0 + kPokedexOfs + kCaughtOfs;
            const uint8_t* otherSeen   = rawData_.data() + otherSec0 + kPokedexOfs + kSeenOfs;
            for (int b = 0; b < kFlagBytes; b++) {
                uint8_t oldC = activeCaught[b];
                uint8_t oldS = activeSeen[b];
                uint8_t newC = static_cast<uint8_t>(oldC | otherCaught[b]);
                uint8_t newS = static_cast<uint8_t>(oldS | otherSeen[b]);
                mergedCaught += __builtin_popcount(static_cast<unsigned>(newC & ~oldC));
                mergedSeen   += __builtin_popcount(static_cast<unsigned>(newS & ~oldS));
                activeCaught[b] = newC;
                activeSeen[b] = newS;
            }

            // Copie ridondanti seen1/seen2 (Emerald, SaveBlock1 +0x988/
            // +0x3B24) o dexSeen2/dexSeen3 (Ruby/Sapphire, SaveBlock1
            // +0x938/+0x3A8C) in sezione 1/4 -- vedi Pokedex::registerRSE
            // (source/pokedex.cpp) per il perche': senza queste tre/quattro
            // copie coerenti tra loro, GetSetPokedexFlag() del gioco vero
            // azzera owned/seen al primo accesso. Se il banco "congelato"
            // le aveva gia' impostate (catture organiche precedenti al
            // fork), vanno unite qui esattamente come owned/seen, altrimenti
            // il recupero sopra resterebbe comunque incoerente e il gioco
            // lo azzererebbe di nuovo da solo.
            if (isImportedFile(gameType_)) {
                int sec1Ofs, sec4Ofs;
                if (gameType_ == GameType::RUBY || gameType_ == GameType::SAPPHIRE) {
                    sec1Ofs = 0x938; sec4Ofs = 0x3A8C - 0x2E80;
                } else {
                    sec1Ofs = 0x988; sec4Ofs = 0x3B24 - 0x2E80;
                }
                int activeSec1 = sectorOfs(gbaActiveSlot_, 1), otherSec1 = sectorOfs(otherSlot, 1);
                int activeSec4 = sectorOfs(gbaActiveSlot_, 4), otherSec4 = sectorOfs(otherSlot, 4);
                if (activeSec1 >= 0 && otherSec1 >= 0) {
                    uint8_t* a = rawData_.data() + activeSec1 + sec1Ofs;
                    const uint8_t* o = rawData_.data() + otherSec1 + sec1Ofs;
                    for (int b = 0; b < kFlagBytes; b++) a[b] |= o[b];
                }
                if (activeSec4 >= 0 && otherSec4 >= 0) {
                    uint8_t* a = rawData_.data() + activeSec4 + sec4Ofs;
                    const uint8_t* o = rawData_.data() + otherSec4 + sec4Ofs;
                    for (int b = 0; b < kFlagBytes; b++) a[b] |= o[b];
                }
            }

            // Stesso problema, stesso rimedio, per il flag National Dex
            // (2026-09-18 v3, corretto v5): setNationalDexEnabled() scrive
            // gia' entrambi i banchi quando viene chiamata, ma se lo sblocco
            // avviene mentre l'altro banco e' quello "congelato" dal fork
            // mGBA, un ciclo saveGBA() successivo puo' rispecchiare il banco
            // SENZA il flag sull'altro, perdendolo su entrambi. IMPORTANTE
            // (2026-09-18 v5): il byte giusto da controllare/unire e' 0x1A
            // (nationalMagic, deve valere 0xDA -- struct Pokedex di
            // pokeemerald: order@0, mode@1, nationalMagic@2, unknown2@3, ...),
            // NON 0x1A-1=0x19 (mode, solo la vista Hoenn/National correntemente
            // selezionata: settarlo da solo non sblocca nulla per il gioco,
            // che controlla SOLO nationalMagic==0xDA per decidere se il
            // National Dex e' disponibile).
            constexpr int kNatDexOfs = 0x1A;
            uint8_t* activeNatDex = rawData_.data() + activeSec0 + kNatDexOfs;
            const uint8_t* otherNatDex = rawData_.data() + otherSec0 + kNatDexOfs;
            mergedNatDex = (*activeNatDex != 0xDA && *otherNatDex == 0xDA);
            if (mergedNatDex) {
                *activeNatDex = 0xDA;
                (rawData_.data() + activeSec0 + 0x19)[0] = 1; // mode segue nationalMagic
            }

            // 2026-09-19: nationalMagic da solo non e' il vero gate (vedi
            // isNationalDexEnabled/setNationalDexEnabled) -- il gioco
            // controlla anche VAR_NATIONAL_DEX==0x302 e FLAG_SYS_NATIONAL_DEX,
            // che vivono in sezione 2 (SaveBlock1), non in sezione 0. Stesso
            // rischio di "banco congelato" del merge sopra: uniamo anche
            // questi se l'altro banco li aveva gia' impostati.
            if (isImportedFile(gameType_)) {
                int flagOfs, varOfs;
                if (gameType_ == GameType::RUBY || gameType_ == GameType::SAPPHIRE) {
                    flagOfs = 0x3A6; varOfs = 0x44C;
                } else {
                    flagOfs = 0x402; varOfs = 0x4A8;
                }
                constexpr int kBit = 6; // FLAG_SYS_NATIONAL_DEX & 7
                int activeSec2 = sectorOfs(gbaActiveSlot_, 2), otherSec2 = sectorOfs(otherSlot, 2);
                if (activeSec2 >= 0 && otherSec2 >= 0) {
                    uint8_t* aFlag = rawData_.data() + activeSec2 + flagOfs;
                    const uint8_t* oFlag = rawData_.data() + otherSec2 + flagOfs;
                    uint16_t oVar = readU16LE(rawData_.data() + otherSec2 + varOfs);
                    if (!(*aFlag & (1 << kBit)) && (*oFlag & (1 << kBit))) {
                        *aFlag |= (1 << kBit);
                        mergedNatDex = true;
                    }
                    if (oVar == 0x302 && readU16LE(rawData_.data() + activeSec2 + varOfs) != 0x302) {
                        writeU16LE(rawData_.data() + activeSec2 + varOfs, 0x302);
                        mergedNatDex = true;
                    }
                }
            }
            }

            if (mergedCaught > 0 || mergedSeen > 0 || mergedNatDex) {
                DebugLog::line("loadGBA: %s -> Pokedex banco %d unito nel banco attivo %d "
                               "(+%d catture, +%d viste, natDex %s recuperati dal banco piu' vecchio)",
                               path.c_str(), otherSlot, gbaActiveSlot_, mergedCaught, mergedSeen,
                               mergedNatDex ? "SI" : "no");
                gbaPokedexMergeDirty = true;
            } else if (!sameTrainer) {
                DebugLog::line("loadGBA: %s -> banchi con trainer diversi (nuova partita?), "
                               "nessuna unione Pokedex", path.c_str());
            }
        }
    }

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

    // Identity strip: OT name = first 8 bytes of section 0 (Small object,
    // PKHeX SAV3: "OT name is the first 8 bytes of Small", Gen3-encoded).
    // Party: Large sectors 1-3 concatenated, brute scan for count 1..6 with
    // valid PK3 party slots (100B each, EC-encrypted, checksum validated).
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    if (uint8_t* sec0 = findGbaSectorData(0)) {
        dsOtName_ = decodeGen3String(sec0, 0, 8, false);
        // TID for GBA: first 4 bytes after OT? Use Small's TID at offset 0x0A within sector 0? Fallback 0.
        // Keep dsTid 0 for GBA (not used in header beyond OT), but log.
        DebugLog::line("loadGBA: %s -> OT '%s'", path.c_str(), dsOtName_.c_str());
    }
    // GBA party brute scan on Large (sectors 1-3)
    {
        // Assemble Large from sectors 1,2,3 if present
        std::vector<uint8_t> large;
        large.reserve(3 * GBA_SECTOR_USED);
        for (int id = 1; id <= 3; ++id) {
            if (uint8_t* s = findGbaSectorData(id)) {
                large.insert(large.end(), s, s + GBA_SECTOR_USED);
            }
        }
        // Scan for party: offset where large[off]==count 1..6 and next count*100 bytes are valid PK3s
        int foundOff = -1, foundCount = 0;
        for (size_t off = 0; off + 100 < large.size() && foundOff==-1; ++off) {
            uint8_t cnt = large[off];
            if (cnt == 0 || cnt > 6) continue;
            // need count PK3s consecutive starting at off+4 (skip count + 3 pad?) Try both +4 and +8 alignment
            for (int pad : {4, 8}) {
                if (off + pad + cnt*100 > large.size()) continue;
                bool ok = true;
                for (int i=0;i<cnt;i++) {
                    const uint8_t* raw = large.data() + off + pad + i*100;
                    Pokemon p; p.gameType_=gameType_;
                    // party slots are 100B encrypted
                    p.loadFromEncrypted(raw, 100);
                    if (p.isEmpty() || p.species()==0) { ok=false; break; }
                    // CHECKSUM GATE (Smeraldo 2026-09-08): decrypt alone
                    // accepts garbage — species after decrypt is ~random, so
                    // bag/record bytes parsed as phantom party members
                    // (spc=131/259, EC=3a010080/00000002). A zeroed real
                    // party then re-scanned onto phantoms, and picking one
                    // zeroed 600B of real save data. Verify PK3 checksum.
                    if (!p.pk3ChecksumValid()) { ok=false; break; }
                    // ensure species plausible 1..386 for Gen3
                    if (p.species() > 386) { ok=false; break; }
                }
                if (ok) { foundOff = (int)off; foundCount = cnt; break; }
            }
        }
        if (foundOff >= 0) {
            size_t base = foundOff + 4; // pad 4 as found
            // Try pad 4 vs 8: re-evaluate which pad gave success (re-scan)
            // Re-detect pad
            int pad = 4;
            for (int tryPad : {4,8}) {
                bool ok=true;
                for(int i=0;i<foundCount;i++){
                    Pokemon p; p.gameType_=gameType_;
                    p.loadFromEncrypted(large.data()+foundOff+tryPad+i*100,100);
                    if(p.isEmpty() || !p.pk3ChecksumValid()) ok=false;
                }
                if(ok){ pad=tryPad; break;}
            }
            for(int i=0;i<foundCount;i++){
                Pokemon p; p.gameType_=gameType_;
                p.loadFromEncrypted(large.data()+foundOff+pad+i*100,100);
                if(!p.isEmpty() && p.pk3ChecksumValid()) dsParty_.push_back(p);
            }
            gbaPartyLargeOff_ = foundOff;
            gbaPartyPad_ = pad;
            DebugLog::line("loadGBA: %s -> party %d at large+%x pad %d", path.c_str(), foundCount, foundOff, pad);
        } else {
            DebugLog::line("loadGBA: %s -> party not found (scanned %zu)", path.c_str(), large.size());
        }
    }

    // Persistenza immediata (2026-09-18 v2): l'unione Pokedex sopra vive
    // solo in RAM finche' non viene scritta su disco. Se OpenHomeNX si
    // limita a SFOGLIARE il save (import scan) e poi l'utente lancia mGBA
    // direttamente in chain-load, mGBA legge il file COSI' COM'E' su SD --
    // non vede mai la correzione fatta solo in memoria. Quindi appena
    // l'unione ha davvero recuperato qualcosa, la ri-salviamo subito qui
    // (saveGBA mischia il banco attivo, ora corretto, sull'altro banco e
    // ricalcola i checksum): cosi' il file su disco e' gia' sano PRIMA
    // che l'utente apra mGBA, senza bisogno di nessuna azione manuale.
    if (gbaPokedexMergeDirty) {
        if (saveGBA(path))
            DebugLog::line("loadGBA: %s -> unione Pokedex salvata su disco", path.c_str());
        else
            DebugLog::line("loadGBA: %s -> unione Pokedex NON salvata (saveGBA fallita)", path.c_str());
    }

    loaded_ = true;
    return true;
}

bool SaveFile::saveGBA(const std::string& path) {
    if (rawData_.empty() || gbaStorage_.empty()) {
        DebugLog::line("saveGBA: dati vuoti (rawData %zu, gbaStorage %zu) per %s",
                       rawData_.size(), gbaStorage_.size(), path.c_str());
        return false;
    }

    // Write modified storage back to sectors in BOTH save slots
    // 2026-09-19: oltre a non essere mai identico tra i due banchi (fix
    // precedente), sull'hardware vero il contatore del banco attivo
    // AVANZA di 1 ad ogni salvataggio nativo -- qui restava congelato tra
    // due nostri saveGBA() consecutivi (mai toccato se non dal gioco
    // stesso), stato che una console reale non produce mai (il contatore
    // e' un numero di generazione monotono, mai fermo tra due salvataggi).
    // Un nostro saveGBA() e' a tutti gli effetti un nuovo salvataggio, cosi'
    // lo incrementiamo anche noi: il banco attivo passa da N a N+1, il
    // banco specchiato riceve N (il valore che l'attivo aveva PRIMA di
    // questo giro) -- resta sempre esattamente 1 indietro, mai un
    // pareggio, esattamente come produce l'hardware vero quando scrive nel
    // banco "nuovo" e lascia l'altro fermo al suo ultimo valore.
    uint32_t activeBase_ = gbaActiveSlot_ * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
    uint32_t activeCounterOld = readU32LE(rawData_.data() + activeBase_ + GBA_OFS_SAVE_INDEX);
    uint32_t activeCounterNew = activeCounterOld + 1;

    for (int slot = 0; slot < 2; slot++) {
        int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;

        if (slot == gbaActiveSlot_) {
            for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
                writeU32LE(rawData_.data() + sectorOfs + GBA_OFS_SAVE_INDEX, activeCounterNew);
            }
        } else {
            // Copy sector structure from the active slot onto the mirror slot
            // (contenuto identico su entrambi i banchi -- rete di sicurezza
            // del merge cross-banco, vedi loadGBA), poi il contatore un
            // passo indietro rispetto al nuovo valore dell'attivo.
            int activeBase = gbaActiveSlot_ * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
            std::memcpy(rawData_.data() + slotBase,
                        rawData_.data() + activeBase,
                        GBA_SECTOR_COUNT * GBA_SECTOR_SIZE);
            for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
                int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
                writeU32LE(rawData_.data() + sectorOfs + GBA_OFS_SAVE_INDEX, activeCounterOld);
            }
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
    if (!f) {
        DebugLog::line("saveGBA: fopen fallito per %s (errno %d: %s)",
                       path.c_str(), errno, std::strerror(errno));
        return false;
    }

    size_t written;
    if (!gbaXtra_.empty()) {
        // Riattacca i 16B dell'emulatore dalla stessa parte (file invariato
        // per l'emulatore a parte i box modificati).
        std::vector<uint8_t> out;
        out.reserve(rawData_.size() + gbaXtra_.size());
        if (gbaXtraAtEnd_) {
            out.insert(out.end(), rawData_.begin(), rawData_.end());
            out.insert(out.end(), gbaXtra_.begin(), gbaXtra_.end());
        } else {
            out.insert(out.end(), gbaXtra_.begin(), gbaXtra_.end());
            out.insert(out.end(), rawData_.begin(), rawData_.end());
        }
        written = std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        if (written != out.size())
            DebugLog::line("saveGBA: scrittura incompleta %s (%zu/%zu byte)",
                           path.c_str(), written, out.size());
        return written == out.size();
    }
    written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    if (written != rawData_.size())
        DebugLog::line("saveGBA: scrittura incompleta %s (%zu/%zu byte)",
                       path.c_str(), written, rawData_.size());
    return written == rawData_.size();
}

long SaveFile::gbaLargeToRaw(size_t largeOff, int slot) const {
    if (rawData_.size() < GBA_SAVE_SIZE)
        return -1;
    if (slot < 0) slot = gbaActiveSlot_;
    if (slot < 0 || slot > 1)
        return -1;
    // Large = sectors 1,2,3 concatenated in that order (see loadGBA).
    size_t secIdx = largeOff / GBA_SECTOR_USED;
    size_t within = largeOff % GBA_SECTOR_USED;
    if (secIdx > 2)
        return -1;
    int wantId = static_cast<int>(secIdx) + 1;
    int slotBase = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
    for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
        int sectorOfs = slotBase + i * GBA_SECTOR_SIZE;
        if (readU16LE(rawData_.data() + sectorOfs + GBA_OFS_SECTOR_ID) == wantId)
            return static_cast<long>(sectorOfs) + static_cast<long>(within);
    }
    return -1;
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

// 2026-09-19 v6: nationalMagic==0xDA da solo NON basta -- e' il bug
// residuo per cui Mew (non-Hoenn) resta invisibile nel dex in game anche
// dopo il fix v5 e dopo il fix seen1/seen2 (specie Hoenn come Pikachu
// funzionano gia', Mew no). Fonte: pret/pokeemerald e pret/pokeruby
// src/event_data.c, IsNationalPokedexEnabled():
//   nationalMagic==0xDA && VarGet(VAR_NATIONAL_DEX)==0x302 &&
//   FlagGet(FLAG_SYS_NATIONAL_DEX)
// VAR_NATIONAL_DEX e FLAG_SYS_NATIONAL_DEX NON vivono nel Pokedex struct
// di sezione 0 (SaveBlock2): sono in gSaveBlock1Ptr->vars[]/flags[]
// (SaveBlock1, sezione 2 per entrambi i giochi). Senza questi due,
// CreatePokedexList() (src/pokedex.c) forza dexMode a DEX_MODE_HOENN ad
// ogni apertura del Pokedex indipendentemente da nationalMagic o dal
// nostro mode=1 -- le specie non-Hoenn restano quindi invisibili anche
// con caught+seen (+ copie ridondanti) corretti su disco.
// Offset calcolati da include/constants/{flags,vars}.h + global.h (SYSTEM_FLAGS,
// FLAGS_COUNT/vars[] field offset) e verificati byte-per-byte:
//   Emerald:       FLAG_SYS_NATIONAL_DEX=0x896 -> SaveBlock1+0x1382 (bit 6)
//                  VAR_NATIONAL_DEX=0x4046      -> SaveBlock1+0x1428 (u16)
//   Ruby/Sapphire: FLAG_SYS_NATIONAL_DEX=0x836 -> SaveBlock1+0x1326 (bit 6)
//                  VAR_NATIONAL_DEX=0x4046      -> SaveBlock1+0x13CC (u16)
// Entrambi cadono in sezione 2 (SaveBlock1 offset [0xF80,0x1F00)) per
// entrambi i giochi -- offset relativo = assoluto - 0xF80.
namespace {
bool gbaNationalDexFlagVarOfs(GameType g, int& flagOfs, int& varOfs) {
    if (g == GameType::RUBY || g == GameType::SAPPHIRE) {
        flagOfs = 0x3A6; varOfs = 0x44C; return true;
    }
    if (g == GameType::EMERALD) {
        flagOfs = 0x402; varOfs = 0x4A8; return true;
    }
    return false;
}
constexpr int kNatDexFlagBit = 6; // FLAG_SYS_NATIONAL_DEX & 7 (0x896 e 0x836 hanno entrambi bit 6)
} // namespace

bool SaveFile::isNationalDexEnabled() const {
    if (!isImportedFile(gameType_)) return false;
    uint8_t* sec0 = const_cast<SaveFile*>(this)->findGbaSectorData(0);
    if (!sec0) return false;
    if (sec0[0x1A] != 0xDA) return false; // nationalMagic: gate primario (v5)

    int flagOfs, varOfs;
    if (!gbaNationalDexFlagVarOfs(gameType_, flagOfs, varOfs)) return false;
    uint8_t* sec2 = const_cast<SaveFile*>(this)->findGbaSectorData(2);
    if (!sec2) return false;
    bool flagSet = (sec2[flagOfs] & (1 << kNatDexFlagBit)) != 0;
    uint16_t varVal = readU16LE(sec2 + varOfs);
    return flagSet && varVal == 0x302;
}

void SaveFile::setNationalDexEnabled() {
    if (!isImportedFile(gameType_)) return;
    int flagOfs, varOfs;
    if (!gbaNationalDexFlagVarOfs(gameType_, flagOfs, varOfs)) return;
    // Scrivi su entrambe le slot (come fa il gioco quando sblocca il National Dex)
    //
    // 2026-09-19: bug reale trovato analizzando un save utente in cui
    // Espeon (transfer non-Hoenn) risultava caught+seen su disco ma
    // nationalMagic/FLAG/VAR restavano a 0 nonostante il log mostrasse
    // "National Dex sbloccato" -- la guardia sotto era
    // "ofs + GBA_SAVE_SIZE > rawData_.size()" (GBA_SAVE_SIZE = 0x20000,
    // l'INTERO save, non un settore). Con rawData_ tipicamente esattamente
    // 0x20000 byte, "ofs + 0x20000 > 0x20000" e' vero per QUALSIASI ofs>0
    // -- il loop scrive quindi SOLO l'eventuale settore a ofs==0 (il primo
    // settore fisico dello slot 0) e salta silenziosamente tutti gli altri
    // 27, sui due slot. Se in quel momento lo slot ATTIVO (quello da cui
    // findGbaSectorData(0) legge/scrive caught/seen in registerRSE) e'
    // lo slot 1, l'unica scrittura che sopravvive alla guardia finisce
    // nello slot 0 (quello "specchiato"/scartato da saveGBA(), che
    // sovrascrive lo slot non attivo con una copia dello slot attivo) --
    // persa al primo saveGBA(), mentre caught/seen (scritti nello slot
    // attivo) sopravvivono. Era chiaramente un refuso: il confronto voleva
    // controllare che un SINGOLO SETTORE (GBA_SECTOR_SIZE, 0x1000) non
    // sfori il buffer, non l'intero save.
    for (int slot = 0; slot < 2; slot++) {
        int base = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
        for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
            int ofs = base + i * GBA_SECTOR_SIZE;
            if (ofs + GBA_SECTOR_SIZE > (int)rawData_.size()) continue;
            uint16_t sid = readU16LE(rawData_.data() + ofs + GBA_OFS_SECTOR_ID);
            if (sid == 0) {
                rawData_[ofs + 0x19] = 1;    // mode: forza vista National (0=Hoenn,1=National)
                rawData_[ofs + 0x1A] = 0xDA; // nationalMagic: gate primario (v5, vedi isNationalDexEnabled)
            } else if (sid == 2) {
                rawData_[ofs + flagOfs] |= (1 << kNatDexFlagBit); // FLAG_SYS_NATIONAL_DEX
                writeU16LE(rawData_.data() + ofs + varOfs, 0x302); // VAR_NATIONAL_DEX
            }
        }
    }
    dirty_ = true;
}

void SaveFile::disableNationalDex() {
    if (!isImportedFile(gameType_)) return;
    int flagOfs, varOfs;
    if (!gbaNationalDexFlagVarOfs(gameType_, flagOfs, varOfs)) return;
    for (int slot = 0; slot < 2; slot++) {
        int base = slot * GBA_SECTOR_COUNT * GBA_SECTOR_SIZE;
        for (int i = 0; i < GBA_SECTOR_COUNT; i++) {
            int ofs = base + i * GBA_SECTOR_SIZE;
            if (ofs + GBA_SECTOR_SIZE > (int)rawData_.size()) continue;
            uint16_t sid = readU16LE(rawData_.data() + ofs + GBA_OFS_SECTOR_ID);
            if (sid == 0) {
                rawData_[ofs + 0x19] = 0; // mode: torna a vista Hoenn
                rawData_[ofs + 0x1A] = 0; // nationalMagic
            } else if (sid == 2) {
                rawData_[ofs + flagOfs] &= static_cast<uint8_t>(~(1 << kNatDexFlagBit)); // FLAG_SYS_NATIONAL_DEX
                writeU16LE(rawData_.data() + ofs + varOfs, 0); // VAR_NATIONAL_DEX
            }
        }
    }
    dirty_ = true;
}

// --- Borsa Gen3 GBA ---
// Offsets relativi all'inizio dati settore 1 (SaveBlock1) + slot count.
// Fonti: pret include/global.h (pokeruby/pokeemerald/pokefirered) incrociato
// con gli span PKHeX SaveBlock3Large{RS,E,FRLG}::Inventory e Bulbapedia
// (items pocket 20/30/42). Totali: 216/236/216 slot = 0x360/0x3B0/0x360.
namespace {
struct GbaBagLayout { int base[5]; int slots[5]; };
bool gbaBagLayoutFor(GameType g, GbaBagLayout& out) {
    if (g == GameType::RUBY || g == GameType::SAPPHIRE) {
        out = {{0x560, 0x5B0, 0x600, 0x640, 0x740}, {20, 20, 16, 64, 46}};
        return true;
    }
    if (g == GameType::EMERALD) {
        out = {{0x560, 0x5D8, 0x650, 0x690, 0x790}, {30, 30, 16, 64, 46}};
        return true;
    }
    if (isFRLG(g)) {
        out = {{0x310, 0x3B8, 0x430, 0x464, 0x54C}, {42, 30, 13, 58, 43}};
        return true;
    }
    return false;
}
} // namespace

// Chiave di sicurezza Gen3: XORata su monete/gettoni/conteggi zaino.
// RS non la usa (conteggi in chiaro). Emerald/FRLG si': offset dentro il
// settore 0 (trainer info) da Bulbapedia "Save data structure (Generation
// III)" sez. Security key (Emerald 0x00AC, FRLG 0x0AF8; esiste anche una
// copia a 0x01F4/0x0F20 usata li' solo come controllo incrociato futuro).
// Senza questo XOR i conteggi letti/scritti sono spazzatura (es. x30556
// invece di x1): mai leggere/scrivere il pocket senza passarci.
uint16_t SaveFile::gbaSecurityKeyLow16() const {
    if (gameType_ == GameType::RUBY || gameType_ == GameType::SAPPHIRE) return 0;
    int off;
    if (gameType_ == GameType::EMERALD) off = 0x00AC;
    else if (isFRLG(gameType_)) off = 0x0AF8;
    else return 0; // non Gen3 GBA: qui non dovrebbe mai arrivarci
    uint8_t* sec0 = const_cast<SaveFile*>(this)->findGbaSectorData(0);
    if (!sec0) return 0;
    return static_cast<uint16_t>(readU32LE(sec0 + off) & 0xFFFF);
}

namespace {
// PlayTime<T> di PKHeX (Gen6+ fino a LA incluso): 4 byte, hours u16 LE @0,
// minutes u8 @2, seconds u8 @3 (il resto del blocco, se piu' grande, e'
// padding/altri campi non correlati). Stesso helper per LGPE/BDSP (flat) e
// SwSh/LA (SCBlock) -- vedi playTimeSeconds() sotto per i rispettivi
// offset/key.
long playTime4Byte(const uint8_t* d, size_t len) {
    if (!d || len < 4) return -1;
    uint16_t hours = readU16LE(d);
    uint8_t minutes = d[2], seconds = d[3];
    if (minutes > 59 || seconds > 59) return -1; // dati non plausibili, non fidarsi
    return static_cast<long>(hours) * 3600 + static_cast<long>(minutes) * 60 + seconds;
}
} // namespace

long SaveFile::playTimeSeconds() const {
    if (!loaded_) return -1;

    // GBA R/S/E/FR/LG: Section0 (SaveBlock2/trainer info), offset 0x0E --
    // stesso layout in tutti e cinque i giochi (playTimeHours u16 LE @0x0E,
    // playTimeMinutes u8 @0x10, playTimeSeconds u8 @0x11 -- playTimeVBlanks
    // @0x12 ignorato, sub-secondo, irrilevante per un confronto "chi ha piu'
    // progressi"). Verificato contro pret/pokeruby, pret/pokeemerald,
    // pret/pokefirered include/global.h: vive nella parte iniziale,
    // condivisa, di SaveBlock2, prima che i giochi divergano piu' avanti
    // nella struct.
    if (isImportedFile(gameType_) || isFRLG(gameType_)) {
        // 2026-09-19: bug reale, mio -- il refactor che ha introdotto
        // playTime4Byte() per riuso su LGPE/BDSP/SwSh/LA aveva perso il "+
        // 0x0E" qui: passava sec0 (inizio sezione0, cioe' l'OT name) invece
        // di sec0+0x0E (playTimeHours). Leggeva quindi byte dell'OT come
        // hours/minutes/seconds, quasi sempre > 59 -> il controllo di
        // plausibilita' in playTime4Byte scartava tutto silenziosamente,
        // sempre -1. La mia stessa verifica su save reali era fatta in
        // Python leggendo l'offset giusto direttamente dal file: mai
        // passata per questo codice, quindi non l'ha mai vista.
        uint8_t* sec0 = const_cast<SaveFile*>(this)->findGbaSectorData(0);
        return sec0 ? playTime4Byte(sec0 + 0x0E, 4) : -1;
    }

    // LGPE (Let's Go Pikachu/Eevee): flat, blocco fisso #10 "PlayTime" a
    // offset assoluto 0x45400 (PKHeX BelugaBlockIndex.PlayTime -- stesso
    // offset gia' presente in LGPE_BLOCKS qui sotto, indice 10).
    if (isLGPE(gameType_)) {
        constexpr size_t kOfs = 0x45400;
        if (rawDataSize() < kOfs + 4) return -1;
        return playTime4Byte(const_cast<SaveFile*>(this)->rawData() + kOfs, 4);
    }

    // BDSP: flat, offset fisso 0x79C04 (PKHeX SAV8BS: Played = new
    // PlayTime8b(this, Raw.Slice(0x79C04, 0x04))).
    if (isBDSP(gameType_)) {
        constexpr size_t kOfs = 0x79C04;
        if (rawDataSize() < kOfs + 4) return -1;
        return playTime4Byte(const_cast<SaveFile*>(this)->rawData() + kOfs, 4);
    }

    // SwSh: SCBlock key 0x8cbbfd90 "Time Played" (PKHeX
    // SaveBlockAccessor8SWSH.KPlayTime), stesso layout PlayTime<T> a 4 byte.
    if (isSwSh(gameType_)) {
        SCBlock* b = const_cast<SaveFile*>(this)->findBlock(0x8cbbfd90);
        return b ? playTime4Byte(b->data.data(), b->data.size()) : -1;
    }

    // Legends Arceus: SCBlock key 0xC4FA7C8C "Time Played" (PKHeX
    // SaveBlockAccessor8LA.KPlayTime), stesso layout a 4 byte.
    if (gameType_ == GameType::LA) {
        SCBlock* b = const_cast<SaveFile*>(this)->findBlock(0xC4FA7C8C);
        return b ? playTime4Byte(b->data.data(), b->data.size()) : -1;
    }

    // SV: SCBlock key 0xEDAFF794 "Time Played" (PKHeX
    // SaveBlockAccessor9SV.KPlayTime), MA layout diverso da PlayTime<T>:
    // hours/minutes/seconds sono ciascuno un i32 LE separato (12 byte
    // totali), non u16+u8+u8 (PKHeX PlayTime9, unico caso Gen9 con questo
    // formato piu' largo).
    if (isSV(gameType_)) {
        SCBlock* b = const_cast<SaveFile*>(this)->findBlock(0xEDAFF794);
        if (!b || b->data.size() < 12) return -1;
        int32_t hours   = static_cast<int32_t>(readU32LE(b->data.data() + 0));
        int32_t minutes = static_cast<int32_t>(readU32LE(b->data.data() + 4));
        int32_t seconds = static_cast<int32_t>(readU32LE(b->data.data() + 8));
        if (hours < 0 || minutes < 0 || seconds < 0 || minutes > 59 || seconds > 59) return -1;
        return static_cast<long>(hours) * 3600 + static_cast<long>(minutes) * 60 + seconds;
    }

    // Legends Z-A: SCBlock key 0xCE3AF8F2 "KPlayedSeconds" (PKHeX
    // SaveBlockAccessor9ZA), double LE a 8 byte = secondi totali gia'
    // accumulati (non piu' un campo hours/minutes/seconds separato come
    // le generazioni precedenti -- PKHeX PlayTime9a.RawSeconds).
    if (gameType_ == GameType::ZA) {
        SCBlock* b = const_cast<SaveFile*>(this)->findBlock(0xCE3AF8F2);
        if (!b || b->data.size() < 8) return -1;
        double secs = 0.0;
        std::memcpy(&secs, b->data.data(), 8);
        if (!(secs >= 0.0) || secs > 1e9) return -1; // sanity: mai negativo, mai assurdo
        return static_cast<long>(secs);
    }

    // Gen1 (R/B/Y): SRAM flat, offset assoluto 0x2CED (PKHeX SAV1Offsets.INT
    // -- stesso set INT di DexCaught 0x25A3/DexSeen 0x25B6 gia' usato in
    // registerGen1(), quindi stesso layout confermato). Diverso dal 4-byte
    // standard: hours e' UN SOLO byte (max 255, con un secondo byte
    // "PlayedMaximum" @+1 quando satura), non un u16 -- minutes @+2,
    // seconds @+3.
    if (isGen1File(gameType_)) {
        constexpr size_t kOfs = 0x2CED;
        if (rawDataSize() < kOfs + 4) return -1;
        const uint8_t* d = const_cast<SaveFile*>(this)->rawData() + kOfs;
        uint8_t hours = d[0], minutes = d[2], seconds = d[3];
        if (minutes > 59 || seconds > 59) return -1;
        return static_cast<long>(hours) * 3600 + static_cast<long>(minutes) * 60 + seconds;
    }

    // Gen2 (G/S/C): SRAM flat, offset assoluto per versione (PKHeX
    // SAV2Offsets, ramo Internazionale -- stesso ramo gia' confermato da
    // GBC_GS/GBC_C in questo file, i cui offset combaciano esattamente con
    // quelli INT di PKHeX). A differenza di Gen1/GBA, PlayedHours qui e' un
    // u16 BIG-ENDIAN (non little-endian): PKHeX usa ReadUInt16BigEndian.
    if (isGen2File(gameType_)) {
        size_t ofs = gbcIsCrystal_ ? 0x2052 : 0x2053;
        if (rawDataSize() < ofs + 4) return -1;
        const uint8_t* d = const_cast<SaveFile*>(this)->rawData() + ofs;
        uint16_t hours = static_cast<uint16_t>((d[0] << 8) | d[1]); // big-endian
        uint8_t minutes = d[2], seconds = d[3];
        if (minutes > 59 || seconds > 59) return -1;
        return static_cast<long>(hours) * 3600 + static_cast<long>(minutes) * 60 + seconds;
    }

    return -1;
}

bool SaveFile::gbaBagSupported() const {
    if (!loaded_) return false;
    GbaBagLayout L;
    return gbaBagLayoutFor(gameType_, L);
}

int SaveFile::gbaBagPocketSlots(GbaBagPocket p) const {
    GbaBagLayout L;
    if (!gbaBagLayoutFor(gameType_, L)) return 0;
    int i = static_cast<int>(p);
    if (i < 0 || i >= 5) return 0;
    return L.slots[i];
}

std::vector<SaveFile::GbaBagSlot> SaveFile::readGbaBag() const {
    std::vector<GbaBagSlot> out;
    if (!loaded_) return out;
    GbaBagLayout L;
    if (!gbaBagLayoutFor(gameType_, L)) return out;
    uint8_t* sec1 = const_cast<SaveFile*>(this)->findGbaSectorData(1);
    if (!sec1) return out;
    const uint16_t key = gbaSecurityKeyLow16(); // RS=0 (chiaro), E/FRLG=vero XOR
    for (int p = 0; p < 5; p++) {
        for (int s = 0; s < L.slots[p]; s++) {
            const uint8_t* d = sec1 + L.base[p] + s * 4;
            GbaBagSlot e;
            e.pocket = static_cast<GbaBagPocket>(p);
            e.slot = s;
            e.id = readU16LE(d);
            e.count = static_cast<uint16_t>(readU16LE(d + 2) ^ key);
            out.push_back(e);
        }
    }
    return out;
}

bool SaveFile::writeGbaBagSlot(GbaBagPocket p, int slot, uint16_t id, uint16_t count) {
    if (!loaded_) return false;
    GbaBagLayout L;
    if (!gbaBagLayoutFor(gameType_, L)) return false;
    int i = static_cast<int>(p);
    if (i < 0 || i >= 5) return false;
    if (slot < 0 || slot >= L.slots[i]) return false;
    uint8_t* sec1 = findGbaSectorData(1);
    if (!sec1) return false;
    // Bounds dentro il settore dati (0x1000): mai oltre.
    size_t off = static_cast<size_t>(L.base[i]) + static_cast<size_t>(slot) * 4;
    if (off + 4 > GBA_SECTOR_USED) return false;
    const uint16_t key = gbaSecurityKeyLow16(); // stesso XOR della lettura
    writeU16LE(sec1 + off, id);
    writeU16LE(sec1 + off + 2, static_cast<uint16_t>(count ^ key));
    dirty_ = true; // saveGBA specchia gli slot + ricalcola i checksum
    return true;
}

namespace {
// 2026-09-19: setGbaFlag()/isGbaFlagSet() assumevano SEMPRE settore 1 con
// flags[] a SaveBlock1+0xEE0 -- vero SOLO per FRLG (pret/pokefirered
// include/global.h: struct SaveBlock1 flags[] @ 0x0EE0, dentro il settore
// 1 [0,0xF80)). Ruby/Sapphire ed Emerald hanno flags[] a un offset
// DIVERSO nel loro SaveBlock1 (struct diversa, stesso problema gia' visto
// per seen1/seen2 e per VAR/FLAG National Dex): Emerald 0x1270, Ruby/
// Sapphire 0x1220 -- entrambi cadono in SETTORE 2, non 1. Con l'offset
// FRLG riusato su Emerald/RS: Aurora/Mistico/Old Sea Map/Ship Southern
// Island finivano scritti nel settore SBAGLIATO (corrompendo un campo
// SaveBlock1 non correlato), e l'Eon Ticket di Ruby/Sapphire (flag 0x853)
// calcolava un offset che sfora GBA_SECTOR_USED nel settore 1 -- setGbaFlag
// ritornava false silenziosamente (il chiamante non controllava il valore
// di ritorno) e non scriveva MAI nulla. Anche per FRLG stesso solo meta'
// del problema era coperta: i flag RECEIVED (0x2A7/0x2A8) cadono davvero
// in settore 1, ma i flag SHIP_BIRTH_ISLAND/SHIP_NAVEL_ROCK (0x84B/0x84A,
// necessari perche' la nave ti ci porti davvero) cadono in settore 2 --
// scritti anche loro nel settore sbagliato.
// Fix: localizza settore+offset dinamicamente in base al gioco, invece di
// assumere sempre settore1+0xEE0.
bool gbaFlagLocation(GameType g, uint16_t flag, int& sectionId, size_t& byteOfs) {
    int flagsBase;
    if (isFRLG(g)) flagsBase = 0x0EE0;
    else if (g == GameType::RUBY || g == GameType::SAPPHIRE) flagsBase = 0x1220;
    else if (g == GameType::EMERALD) flagsBase = 0x1270;
    else return false;
    constexpr int kSectorUsed = 0xF80; // = SaveFile::GBA_SECTOR_USED (private, non raggiungibile da qui)
    int abs = flagsBase + (flag >> 3);
    sectionId = 1 + abs / kSectorUsed;
    byteOfs = static_cast<size_t>(abs % kSectorUsed);
    return true;
}
} // namespace

bool SaveFile::isGbaFlagSet(uint16_t flag) const {
    if (!gbaBagSupported()) return false;
    int sectionId; size_t byteOfs;
    if (!gbaFlagLocation(gameType_, flag, sectionId, byteOfs)) return false;
    uint8_t* sec = const_cast<SaveFile*>(this)->findGbaSectorData(sectionId);
    if (!sec || byteOfs >= GBA_SECTOR_USED) return false;
    return (sec[byteOfs] >> (flag & 7)) & 1;
}

bool SaveFile::setGbaFlag(uint16_t flag) {
    if (!gbaBagSupported()) return false;
    int sectionId; size_t byteOfs;
    if (!gbaFlagLocation(gameType_, flag, sectionId, byteOfs)) return false;
    uint8_t* sec = findGbaSectorData(sectionId);
    if (!sec || byteOfs >= GBA_SECTOR_USED) return false;
    sec[byteOfs] |= (1u << (flag & 7));
    dirty_ = true;
    return true;
}

// --- Borsa Gen4/5 DS ---
// Layout PKHeX PlayerBag4DP/4Pt/4HGSS/5BW/5B2W2: offset tasca (relativi alla
// base borsa) + slot gestiti (= liste legali ItemStorage, vedi
// tools/gen_items_gen45.py). Base Gen4 relativa al General della partizione
// attiva, Gen5 assoluta (blocco 25). Validato byte a byte sulle fixture
// tools/test save/upstream/oh_*.sav (bad_id=0 over_max=0 ovunque).
namespace {
struct DsBagLayout { int base; int off[8]; int slots[8]; };
// Tabella borsa dal GIOCO (non dal layout rilevato): detectDSVersion distingue
// Pt/HGSS dai byte, solo il tie D/P e' da filename — e D/P condividono la tabella.
// Se il layout rilevato contraddice il gioco, niente tabella (mai scrivere
// all'offset sbagliato): esplicito, mai silenzioso.
bool dsBagLayoutFor(GameType g, SaveFile::Ds4Layout l4, DsBagLayout& out) {
    auto layoutOk = [&]() -> bool {
        switch (l4) {
            case SaveFile::Ds4Layout::DP: return g == GameType::DIAMOND || g == GameType::PEARL;
            case SaveFile::Ds4Layout::PT: return g == GameType::PLATINUM;
            case SaveFile::Ds4Layout::HGSS: return g == GameType::HEARTGOLD || g == GameType::SOULSILVER;
        }
        return false;
    };
    if (g == GameType::DIAMOND || g == GameType::PEARL) {
        if (!layoutOk()) return false;
        out = {0x624,
               {0x000, 0x294, 0x35C, 0x4EC, 0x51C, 0x5BC, 0x6BC, 0x6F8},
               {161, 37, 100, 12, 38, 64, 15, 13}};
        return true;
    }
    if (g == GameType::PLATINUM) {
        if (!layoutOk()) return false;
        out = {0x630,
               {0x000, 0x294, 0x35C, 0x4EC, 0x51C, 0x5BC, 0x6BC, 0x6F8},
               {162, 40, 100, 12, 38, 64, 15, 13}};
        return true;
    }
    if (g == GameType::HEARTGOLD || g == GameType::SOULSILVER) {
        if (!layoutOk()) return false;
        out = {0x644,
               {0x000, 0x294, 0x35C, 0x4F0, 0x520, 0x5C0, 0x6C0, 0x720},
               {162, 38, 100, 12, 38, 64, 24, 13}};
        return true;
    }
    if (g == GameType::BLACK || g == GameType::WHITE ||
        g == GameType::BLACK2 || g == GameType::WHITE2) {
        const bool b2w2 = (g == GameType::BLACK2 || g == GameType::WHITE2);
        out = {0x18400, // blocco 25, uguale BW/B2W2
               {0x000, 0x4D8, 0x624, 0, 0x7D8, 0x898, 0, 0},
               {261, b2w2 ? 27 : 19, 101, 0, 47, 64, 0, 0}};
        return true;
    }
    return false;
}
} // namespace

// Inizio regione borsa nei dati grezzi (assoluto in rawData_), o -1.
long SaveFile::dsBagBase() const {
    DsBagLayout L;
    if (!dsBagLayoutFor(gameType_, ds4Layout_, L)) return -1;
    if (isGen4File(gameType_)) {
        if (dsPart_ < 0 || dsPart_ > 1) return -1;
        return static_cast<long>(dsPart_) * DS_PARTITION + L.base;
    }
    return L.base; // Gen5: blocco 25 fisso
}

bool SaveFile::dsBagSupported() const {
    if (!loaded_) return false;
    return dsBagBase() >= 0;
}

int SaveFile::dsBagPocketSlots(DsBagPocket p) const {
    DsBagLayout L;
    if (!dsBagLayoutFor(gameType_, ds4Layout_, L)) return 0;
    int i = static_cast<int>(p);
    if (i < 0 || i >= 8) return 0;
    return L.slots[i];
}

std::vector<SaveFile::DsBagSlot> SaveFile::readDsBag() const {
    std::vector<DsBagSlot> out;
    if (!loaded_) return out;
    DsBagLayout L;
    if (!dsBagLayoutFor(gameType_, ds4Layout_, L)) return out;
    long base = dsBagBase();
    if (base < 0) return out;
    for (int p = 0; p < 8; p++) {
        for (int s = 0; s < L.slots[p]; s++) {
            size_t o = static_cast<size_t>(base) + static_cast<size_t>(L.off[p]) +
                       static_cast<size_t>(s) * 4;
            if (o + 4 > rawData_.size()) return out;
            DsBagSlot e;
            e.pocket = static_cast<DsBagPocket>(p);
            e.slot = s;
            e.id = readU16LE(rawData_.data() + o);
            e.count = readU16LE(rawData_.data() + o + 2);
            out.push_back(e);
        }
    }
    return out;
}

bool SaveFile::writeDsBagSlot(DsBagPocket p, int slot, uint16_t id, uint16_t count) {
    if (!loaded_) return false;
    DsBagLayout L;
    if (!dsBagLayoutFor(gameType_, ds4Layout_, L)) return false;
    int i = static_cast<int>(p);
    if (i < 0 || i >= 8) return false;
    if (slot < 0 || slot >= L.slots[i]) return false;
    long base = dsBagBase();
    if (base < 0) return false;
    size_t o = static_cast<size_t>(base) + static_cast<size_t>(L.off[i]) +
               static_cast<size_t>(slot) * 4;
    if (o + 4 > rawData_.size()) return false;
    writeU16LE(rawData_.data() + o, id);
    writeU16LE(rawData_.data() + o + 2, count);
    dirty_ = true; // CRC sistemati da saveDS4/saveDS5
    return true;
}

// --- Borsa Gen1/2 GB ---
// Layout PKHeX PlayerBag1/PlayerBag2 (SAV1Offsets INT, SAV2Offsets INT):
// Gen1 Items @0x25C9 x20; Gen2 GS: TM @0x23E6 x57 fisso, Items @0x241F x20,
// Key @0x2449 x26, Balls @0x2464 x12; Crystal +1 ovunque. Validato sulle
// fixture Rossa/Gialla/Oro/Argento/Cristallo (bad_id=0 over_max=0).
namespace {
// Ordine id legali tasca TM Gen2 (PKHeX ItemStorage2.Machine, 50 TM + 7 HM):
// l'indice IN QUESTO ARRAY e' l'offset del count nell'array fisso 57B.
constexpr uint16_t GB_TM_ORDER[57] = {
    191, 192, 193, 194, 196, 197, 198, 199, 200, 201,
    202, 203, 204, 205, 206, 207, 208, 209, 210, 211,
    212, 213, 214, 215, 216, 217, 218, 219, 221, 222,
    223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
    233, 234, 235, 236, 237, 238, 239, 240, 241, 242,
    243, 244, 245, 246, 247, 248, 249,
};
struct GbBagPouch { SaveFile::GbBagPocket pocket; int off; int slots; bool fixed; };
struct GbBagLayout { GbBagPouch pouches[4]; int n; };
bool gbBagLayoutFor(GameType g, bool crystal, GbBagLayout& out) {
    using P = SaveFile::GbBagPocket;
    if (g == GameType::RED || g == GameType::BLUE || g == GameType::YELLOW) {
        out.n = 1;
        out.pouches[0] = {P::Items, 0x25C9, 20, false};
        return true;
    }
    if (g == GameType::GOLD || g == GameType::SILVER || g == GameType::CRYSTAL) {
        const int d = crystal ? 1 : 0; // Crystal sposta tutto di +1
        out.n = 4;
        out.pouches[0] = {P::TmHm, 0x23E6 + d, 57, true};
        out.pouches[1] = {P::Items, 0x241F + d, 20, false};
        out.pouches[2] = {P::Key, 0x2449 + d, 26, false};
        out.pouches[3] = {P::Balls, 0x2464 + d, 12, false};
        return true;
    }
    return false;
}
} // namespace

bool SaveFile::gbBagSupported() const {
    if (!loaded_) return false;
    GbBagLayout L;
    return gbBagLayoutFor(gameType_, gbcIsCrystal_, L);
}

std::vector<uint16_t> SaveFile::gbBagTmOrder() const {
    if (!isGen2File(gameType_)) return {};
    return std::vector<uint16_t>(std::begin(GB_TM_ORDER), std::end(GB_TM_ORDER));
}

std::vector<SaveFile::GbBagSlot> SaveFile::readGbBag() const {
    std::vector<GbBagSlot> out;
    if (!loaded_) return out;
    GbBagLayout L;
    if (!gbBagLayoutFor(gameType_, gbcIsCrystal_, L)) return out;
    for (int pi = 0; pi < L.n; pi++) {
        const auto& P = L.pouches[pi];
        if (static_cast<size_t>(P.off) >= rawData_.size()) return out;
        const uint8_t* base = rawData_.data() + P.off;
        if (P.fixed) {
            // TM: array fisso, count per indice legale (0 = assente).
            size_t need = static_cast<size_t>(P.slots);
            if (static_cast<size_t>(P.off) + need > rawData_.size()) return out;
            int pos = 0;
            for (int i = 0; i < P.slots; i++) {
                if (base[i] == 0) continue;
                GbBagSlot e;
                e.pocket = P.pocket;
                e.slot = pos++;
                e.id = GB_TM_ORDER[i];
                e.count = base[i];
                out.push_back(e);
            }
            continue;
        }
        // count-driven (come PKHeX GetPouch, con sanity su count assurdi).
        int n = base[0];
        if (n < 0 || n > P.slots) continue; // count corrotto: tasca illeggibile, mai crash
        for (int i = 0; i < n; i++) {
            GbBagSlot e;
            e.pocket = P.pocket;
            e.slot = i;
            if (P.pocket == GbBagPocket::Key) {
                e.id = base[1 + i];
                e.count = 1;
            } else {
                e.id = base[1 + i * 2];
                e.count = base[1 + i * 2 + 1];
            }
            out.push_back(e);
        }
    }
    return out;
}

bool SaveFile::writeGbBagPocket(GbBagPocket p,
                                const std::vector<std::pair<uint16_t, uint16_t>>& items) {
    if (!loaded_) return false;
    GbBagLayout L;
    if (!gbBagLayoutFor(gameType_, gbcIsCrystal_, L)) return false;
    const GbBagPouch* found = nullptr;
    for (int pi = 0; pi < L.n; pi++)
        if (L.pouches[pi].pocket == p) { found = &L.pouches[pi]; break; }
    if (!found) return false; // tasca assente per questo gioco
    uint8_t* base = rawData_.data() + found->off;
    if (found->fixed) {
        // TM: riscrittura integrale 57B per indice legale (come PKHeX SetPouch).
        if (static_cast<size_t>(found->off) + 57 > rawData_.size()) return false;
        uint8_t full[57] = {};
        for (auto& kv : items) {
            if (kv.second > 0xFF) return false;
            int idx = -1;
            for (int i = 0; i < 57; i++)
                if (GB_TM_ORDER[i] == kv.first) { idx = i; break; }
            if (idx < 0) return false; // id fuori lista legale: mai scrivere a caso
            if (full[idx] != 0) return false; // duplicato in input: abortito
            full[idx] = static_cast<uint8_t>(kv.second);
        }
        size_t total = 0;
        for (int i = 0; i < 57; i++) if (full[i] != 0) total++;
        if (total > static_cast<size_t>(found->slots)) return false;
        std::memcpy(base, full, 57);
        dirty_ = true; // checksum sistemati da saveGB/saveGBC
        return true;
    }
    if ((int)items.size() > found->slots) return false;
    size_t region = 1 + static_cast<size_t>(found->slots) * 2 + 1;
    if (static_cast<size_t>(found->off) + region > rawData_.size()) return false;
    if (found->pocket == GbBagPocket::Key) {
        region = 1 + static_cast<size_t>(found->slots) + 1;
        if (static_cast<size_t>(found->off) + region > rawData_.size()) return false;
        base[0] = static_cast<uint8_t>(items.size());
        for (size_t i = 0; i < items.size(); i++) {
            // Key: solo id u8 (il count e' sempre 1 alla lettura).
            if (items[i].first > 0xFF) return false;
            base[1 + i] = static_cast<uint8_t>(items[i].first);
        }
        base[1 + items.size()] = 0xFF;
    } else {
        base[0] = static_cast<uint8_t>(items.size());
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].first > 0xFF || items[i].second > 0xFF) return false;
            base[1 + i * 2] = static_cast<uint8_t>(items[i].first);
            base[1 + i * 2 + 1] = static_cast<uint8_t>(items[i].second);
        }
        base[1 + items.size() * 2] = 0xFF;
    }
    dirty_ = true;
    return true;
}

// --- Gen 4/5 (DS .sav dumps, PKHeX SAV4*/SAV5BW.cs) ---

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

// Gen4 layouts (DP/Pt/HGSS): General + Storage block sizes differ per game
// (PKHeX SAV4*.cs). Shared by loadDS4 (detect) and saveDS4 (write-back).
struct Ds4SaveLayout {
    SaveFile::Ds4Layout id;
    int gSize, sSize, sStart, footer, boxBase, boxStride;
};
static constexpr Ds4SaveLayout DS4_LAYOUTS[3] = {
    { SaveFile::Ds4Layout::DP,   0xC100, 0x121E0, 0xC100, 0x14, 4, 0xFF0 },
    { SaveFile::Ds4Layout::PT,   0xCF2C, 0x121E4, 0xCF2C, 0x14, 4, 0xFF0 },
    { SaveFile::Ds4Layout::HGSS, 0xF628, 0x12310, 0xF700, 0x10, 0, 0x1000 },
};
static const Ds4SaveLayout* ds4LayoutFor(SaveFile::Ds4Layout id) {
    for (const auto& L : DS4_LAYOUTS)
        if (L.id == id) return &L;
    return nullptr;
}

bool SaveFile::loadDS4(const std::string& path) {
    // Gen4 NDS flash (PKHeX SAV4*.cs): 512KB = 2 partitions x 256KB.
    dsRomCode_ = 0;
    dsGameByte_ = 0;
    // block + Storage block; layouts differ per game (see DS4_LAYOUTS), so
    // bytes alone pick the layout: footer magic (INT/KOR) on both blocks of
    // some partition, plus slots that decrypt to valid checksums.
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

    const Ds4SaveLayout* best = nullptr;
    int bestPart = 0;
    int bestScore = -1;
    int bestSlots = 0;
    for (const Ds4SaveLayout& L : DS4_LAYOUTS) {
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
    dsPart_ = bestPart; // active partition for saveDS4 write-back
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
    // Identity strip: OT/TID + decrypted party from the General block.
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    {
        size_t gBase = static_cast<size_t>(bestPart) * DS_PARTITION;
        int trainer1 = (best->id == Ds4Layout::PT) ? 0x68 : 0x64;
        int party = (best->id == Ds4Layout::PT) ? 0xA0 : 0x98;
        const uint8_t* gBlk = rawData_.data() + gBase;
        uint16_t codes[8];
        for (int i = 0; i < 8; i++)
            codes[i] = readU16LE(gBlk + trainer1 + i * 2);
        dsOtName_ = OpenHomeNX::gen4DecodeString(codes, 8);
        dsTid_ = readU16LE(gBlk + trainer1 + 0x10);
        int count = gBlk[party - 4];
        if (count > 6) count = 6;
        DebugLog::line("loadDS4: %s -> partyCount byte=%d (gBase=%zx party=%x)", path.c_str(), count, gBase, party);
        for (int i = 0; i < count; i++) {
            Pokemon p;
            p.gameType_ = gameType_;
            p.loadFromEncrypted(gBlk + party + i * 236, 236);
            DebugLog::line("  party[%d] ec=%08x species=%u empty=%d", i, p.encryptionConstant(), p.species(), p.isEmpty()?1:0);
            if (!p.isEmpty())
                dsParty_.push_back(p);
            else
                DebugLog::line("  party[%d] discarded as empty", i);
        }
        if (count==0) DebugLog::line("loadDS4: %s -> party count 0, header bytes %02x %02x %02x %02x", path.c_str(), gBlk[party-4], gBlk[party-3], gBlk[party-2], gBlk[party-1]);
        DebugLog::line("loadDS4: %s -> OT '%s' TID %u party %zu",
                       path.c_str(), dsOtName_.c_str(), dsTid_, dsParty_.size());
    }
    loaded_ = true;
    return true;
}

bool SaveFile::saveDS4(const std::string& path) {
    // Gen4 write-back (PKHeX SAV4qd logic): boxes + party into the ACTIVE
    // partition only, then recompute both block CRCs (same ranges loadDS4
    // validates). Save index untouched (partition stays active). Corrupt slots
    // hidden at load come back as zeros — the game reads them as empty too.
    if (!loaded_ || rawData_.size() != DS_SAVE_SIZE || dsStorage_.empty())
        return false;
    const Ds4SaveLayout* L = ds4LayoutFor(ds4Layout_);
    if (!L || dsPart_ < 0 || dsPart_ > 1)
        return false;
    size_t gBase = static_cast<size_t>(dsPart_) * DS_PARTITION;
    size_t sBase = gBase + static_cast<size_t>(L->sStart);
    if (sBase + static_cast<size_t>(L->sSize) > DS_SAVE_SIZE)
        return false;

    // Boxes: dsStorage_ (which setBoxSlot mutates) back to the Storage block.
    const int BOXES = 18;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < DS_BOX_SLOTS; s++) {
            size_t o = (sBase + static_cast<size_t>(L->boxBase)) +
                       static_cast<size_t>(b) * static_cast<size_t>(L->boxStride) +
                       static_cast<size_t>(s) * DS_SLOT_SIZE;
            const uint8_t* src = dsStorage_.data() +
                (static_cast<size_t>(b) * DS_BOX_SLOTS + s) * DS_SLOT_SIZE;
            std::memcpy(rawData_.data() + o, src, DS_SLOT_SIZE);
        }

    // Party: compact, count byte + full 236B records (no tail math — same
    // bytes the loader decrypted). Positional, counts untouched otherwise.
    {
        int trainer1 = (L->id == Ds4Layout::PT) ? 0x68 : 0x64;
        int party = (L->id == Ds4Layout::PT) ? 0xA0 : 0x98;
        (void)trainer1;
        if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
        std::vector<Pokemon> team;
        for (auto& pp : dsParty_)
            if (!pp.isEmpty() && pp.species() != 0) team.push_back(pp);
        if (team.size() > 6) team.resize(6);
        uint8_t* gBlk = rawData_.data() + gBase;
        gBlk[party - 4] = static_cast<uint8_t>(team.size());
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = gBlk + party + i * 236;
            if (i >= static_cast<int>(team.size())) {
                std::memset(dst, 0, 236);
            } else {
                Pokemon w = team[i];
                w.gameType_ = gameType_;
                w.refreshChecksum();
                PokemonFFI::encryptArray45(w.data.data(), 236, dst);
            }
        }
        dsParty_.assign(6, Pokemon{});
        for (size_t i = 0; i < team.size() && i < 6; i++)
            dsParty_[i] = team[i];
    }

    // Block CRCs over [0, size-footer), stored u16 LE at [size-2].
    {
        uint8_t* gBlk = rawData_.data() + gBase;
        uint8_t* sBlk = rawData_.data() + sBase;
        uint16_t gCks = crc16CcittFalse(gBlk, static_cast<size_t>(L->gSize) - static_cast<size_t>(L->footer));
        uint16_t sCks = crc16CcittFalse(sBlk, static_cast<size_t>(L->sSize) - static_cast<size_t>(L->footer));
        writeU16LE(gBlk + L->gSize - 2, gCks);
        writeU16LE(sBlk + L->sSize - 2, sCks);
    }

    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f)
        f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    size_t written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    DebugLog::line("saveDS4: %s -> OK (layout %d part %d)", path.c_str(), (int)L->id, dsPart_);
    return written == rawData_.size();
}

bool SaveFile::loadDS5(const std::string& path) {
    // Gen5 BW (PKHeX SAV5BW): flat 512KB, no partitions.
    dsRomCode_ = 0;
    dsGameByte_ = 0;
    // Gen5 BW/B2W2 (PKHeX SAV5BW/SAV5B2W2): flat 512KB, no partitions.
    // Boxes at 0x400 + box*0x1000 (30 x 136B slots), party at 0x18E08
    // (count at 0x18E04, 6 x 220B), PlayerData block at 0x19400 (OT at +4,
    // Game at +0x1F: 20 = White, 21 = Black, 22 = White2, 23 = Black2 —
    // same head offsets in both block maps).
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
    if (game < 20 || game > 23) {
        DebugLog::line("loadDS5: %s -> Game byte %u ignoto (20-23 attesi)", path.c_str(), game);
        return false;
    }
    dsGameByte_ = game;
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
    DebugLog::line("loadDS5: %s -> Game %u (%s), slot validi %d", path.c_str(), game,
                   game == 20 ? "White" : game == 21 ? "Black" : game == 22 ? "White2" : "Black2",
                   validSlots);
    dsGameByte_ = game;
    // Identity strip: OT (direct UTF-16LE, FFFF-terminated) + party.
    dsParty_.clear();
    dsOtName_.clear();
    dsTid_ = 0;
    {
        const uint8_t* pBlk = rawData_.data() + 0x19400;
        std::string ot;
        for (int i = 0; i < 8; i++) {
            uint16_t ch = readU16LE(pBlk + 4 + i * 2);
            if (ch == 0xFFFF || ch == 0)
                break;
            if (ch < 0x80) ot += static_cast<char>(ch);
            else if (ch < 0x800) {
                ot += static_cast<char>(0xC0 | (ch >> 6));
                ot += static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                ot += static_cast<char>(0xE0 | (ch >> 12));
                ot += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                ot += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        dsOtName_ = ot;
        dsTid_ = readU16LE(pBlk + 0x14);
        int count = rawData_[0x18E04];
        if (count > 6) count = 6;
        for (int i = 0; i < count; i++) {
            Pokemon p;
            p.gameType_ = gameType_;
            p.loadFromEncrypted(rawData_.data() + 0x18E08 + i * 220, 220);
            if (!p.isEmpty())
                dsParty_.push_back(p);
        }
        DebugLog::line("loadDS5: %s -> OT '%s' TID %u party %zu",
                       path.c_str(), dsOtName_.c_str(), dsTid_, dsParty_.size());
    }
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = nullptr; // TODO v2: nomi box (blocco 0) + party
    boxLayoutLen_ = 0;
    loaded_ = true;
    return true;
}

// Gen5 block map (PKHeX SaveBlockAccessor5BW / SaveBlockAccessor5B2W2):
// {data offset, data length, checksum offset, checksum mirror}. Checksum =
// CRC16-CCITT over [off, off+len), stored u16 LE at chk AND mirror
// (PKHeX BlockInfoNDS). BW (Game 20/21) e B2W2 (22/23) condividono box,
// party e testa — solo la coda della mappa cambia.
struct Ds5Block { uint32_t off, len, chk, mirror; };
// Blocchi fissi BW esclusi box 1..24 e nomi box (indice 0): in ordine PKHeX,
// checksum-block per ultimo (i mirror ci scrivono dentro: va ricalcolato dopo).
static constexpr Ds5Block DS5_FIXED_BW[] = {
    {0x00000, 0x03E0, 0x003E2, 0x23F00}, // 00 nomi box
    {0x18400, 0x09C0, 0x18DC2, 0x23F32}, // 25 zaino
    {0x18E00, 0x0534, 0x19336, 0x23F34}, // 26 party
    {0x19400, 0x0068, 0x1946A, 0x23F36}, // 27 trainer data
    {0x19500, 0x009C, 0x1959E, 0x23F38}, // 28 posizione
    {0x19600, 0x1338, 0x1A93A, 0x23F3A}, // 29 unity tower
    {0x1AA00, 0x07C4, 0x1B1C6, 0x23F3C}, // 30 pal pad player
    {0x1B200, 0x0D54, 0x1BF56, 0x23F3E}, // 31 pal pad friend
    {0x1C000, 0x002C, 0x1C02E, 0x23F40}, // 32 skin info
    {0x1C100, 0x0658, 0x1C75A, 0x23F42}, // 33 badge data
    {0x1C800, 0x0A94, 0x1D296, 0x23F44}, // 34 mystery gift
    {0x1D300, 0x01AC, 0x1D4AE, 0x23F46}, // 35 dream world
    {0x1D500, 0x03EC, 0x1D8EE, 0x23F48}, // 36 chatter
    {0x1D900, 0x005C, 0x1D95E, 0x23F4A}, // 37 adventure info
    {0x1DA00, 0x01E0, 0x1DBE2, 0x23F4C}, // 38 record
    {0x1DC00, 0x00A8, 0x1DCAA, 0x23F4E}, // 39 ???
    {0x1DD00, 0x0460, 0x1E162, 0x23F50}, // 40 mail
    {0x1E200, 0x1400, 0x1F602, 0x23F52}, // 41 overworld
    {0x1F700, 0x02A4, 0x1F9A6, 0x23F54}, // 42 musical
    {0x1FA00, 0x02DC, 0x1FCDE, 0x23F56}, // 43 forest/city
    {0x1FD00, 0x034C, 0x2004E, 0x23F58}, // 44 IR
    {0x20100, 0x03EC, 0x204EE, 0x23F5A}, // 45 event work
    {0x20500, 0x00F8, 0x205FA, 0x23F5C}, // 46 GTS
    {0x20600, 0x02FC, 0x208FE, 0x23F5E}, // 47 regulation
    {0x20900, 0x0094, 0x20996, 0x23F60}, // 48 gimmick
    {0x20A00, 0x035C, 0x20D5E, 0x23F62}, // 49 battle box
    {0x20E00, 0x01CC, 0x20FCE, 0x23F64}, // 50 daycare
    {0x21000, 0x0168, 0x2116A, 0x23F66}, // 51 boulder
    {0x21200, 0x00EC, 0x212EE, 0x23F68}, // 52 badge/money
    {0x21300, 0x01B0, 0x214B2, 0x23F6A}, // 53 entralink
    {0x21500, 0x001C, 0x2151E, 0x23F6C}, // 54 ???
    {0x21600, 0x04D4, 0x21AD6, 0x23F6E}, // 55 pokedex
    {0x21B00, 0x0034, 0x21B36, 0x23F70}, // 56 encount
    {0x21C00, 0x003C, 0x21C3E, 0x23F72}, // 57 subway play
    {0x21D00, 0x01AC, 0x21EAE, 0x23F74}, // 58 subway score
    {0x21F00, 0x0B90, 0x22A92, 0x23F76}, // 59 subway wifi
    {0x22B00, 0x009C, 0x22B9E, 0x23F78}, // 60 online record
    {0x22C00, 0x0850, 0x23452, 0x23F7A}, // 61 entralink forest
    {0x23500, 0x0028, 0x2352A, 0x23F7C}, // 62 ???
    {0x23600, 0x0284, 0x23886, 0x23F7E}, // 63 questions
    {0x23900, 0x0010, 0x23912, 0x23F80}, // 64 unity tower
    {0x23A00, 0x005C, 0x23A5E, 0x23F82}, // 65 battle institute
    {0x23B00, 0x016C, 0x23C6E, 0x23F84}, // 66 ???
    {0x23D00, 0x0040, 0x23D42, 0x23F86}, // 67 ???
    {0x23E00, 0x00FC, 0x23EFE, 0x23F88}, // 68 ???
    {0x23F00, 0x008C, 0x23F9A, 0x23F9A}, // 69 checksum (chk==mirror)
};
static constexpr Ds5Block DS5_FIXED_B2W2[] = {
    {0x00000, 0x03E0, 0x003E2, 0x25F00}, // 00 nomi box
    {0x18400, 0x09EC, 0x18DEE, 0x25F32}, // 25 zaino
    {0x18E00, 0x0534, 0x19336, 0x25F34}, // 26 party
    {0x19400, 0x00B0, 0x194B2, 0x25F36}, // 27 trainer data
    {0x19500, 0x00A8, 0x195AA, 0x25F38}, // 28 posizione
    {0x19600, 0x1338, 0x1A93A, 0x25F3A}, // 29 unity tower
    {0x1AA00, 0x07C4, 0x1B1C6, 0x25F3C}, // 30 pal pad player
    {0x1B200, 0x0D54, 0x1BF56, 0x25F3E}, // 31 pal pad friend
    {0x1C000, 0x0094, 0x1C096, 0x25F40}, // 32 options/skin
    {0x1C100, 0x0658, 0x1C75A, 0x25F42}, // 33 trainer card
    {0x1C800, 0x0A94, 0x1D296, 0x25F44}, // 34 mystery gift
    {0x1D300, 0x01AC, 0x1D4AE, 0x25F46}, // 35 dream world
    {0x1D500, 0x03EC, 0x1D8EE, 0x25F48}, // 36 chatter
    {0x1D900, 0x005C, 0x1D95E, 0x25F4A}, // 37 adventure
    {0x1DA00, 0x01E0, 0x1DBE2, 0x25F4C}, // 38 record
    {0x1DC00, 0x00A8, 0x1DCAA, 0x25F4E}, // 39 ???
    {0x1DD00, 0x0460, 0x1E162, 0x25F50}, // 40 mail
    {0x1E200, 0x1400, 0x1F602, 0x25F52}, // 41 overworld
    {0x1F700, 0x02A4, 0x1F9A6, 0x25F54}, // 42 musical
    {0x1FA00, 0x00E0, 0x1FAE2, 0x25F56}, // 43 forest/fused
    {0x1FB00, 0x034C, 0x1FE4E, 0x25F58}, // 44 IR
    {0x1FF00, 0x04E0, 0x203E2, 0x25F5A}, // 45 event work
    {0x20400, 0x00F8, 0x204FA, 0x25F5C}, // 46 GTS
    {0x20500, 0x02FC, 0x207FE, 0x25F5E}, // 47 regulation
    {0x20800, 0x0094, 0x20896, 0x25F60}, // 48 gimmick
    {0x20900, 0x035C, 0x20C5E, 0x25F62}, // 49 battle box
    {0x20D00, 0x01D4, 0x20ED6, 0x25F64}, // 50 daycare
    {0x20F00, 0x01E0, 0x210E2, 0x25F66}, // 51 boulder
    {0x21100, 0x00F0, 0x211F2, 0x25F68}, // 52 misc
    {0x21200, 0x01B4, 0x213B6, 0x25F6A}, // 53 entralink
    {0x21400, 0x04DC, 0x218DE, 0x25F6C}, // 54 pokedex
    {0x21900, 0x0034, 0x21936, 0x25F6E}, // 55 encount
    {0x21A00, 0x003C, 0x21A3E, 0x25F70}, // 56 subway play
    {0x21B00, 0x01AC, 0x21CAE, 0x25F72}, // 57 subway score
    {0x21D00, 0x0B90, 0x22892, 0x25F74}, // 58 subway wifi
    {0x22900, 0x00AC, 0x229AE, 0x25F76}, // 59 online record
    {0x22A00, 0x0850, 0x23252, 0x25F78}, // 60 entralink forest
    {0x23300, 0x0284, 0x23586, 0x25F7A}, // 61 questions
    {0x23600, 0x0010, 0x23612, 0x25F7C}, // 62 unity tower
    {0x23700, 0x00A8, 0x237AA, 0x25F7E}, // 63 battle institute/PWT
    {0x23800, 0x016C, 0x2396E, 0x25F80}, // 64 ???
    {0x23A00, 0x0080, 0x23A82, 0x25F82}, // 65 ???
    {0x23B00, 0x00FC, 0x23BFE, 0x25F84}, // 66 hollow/rival
    {0x23C00, 0x16A8, 0x252AA, 0x25F86}, // 67 join avenue
    {0x25300, 0x0498, 0x2579A, 0x25F88}, // 68 medal
    {0x25800, 0x0060, 0x25862, 0x25F8A}, // 69 key system
    {0x25900, 0x00FC, 0x259FE, 0x25F8C}, // 70 festa
    {0x25A00, 0x03E4, 0x25DE6, 0x25F8E}, // 71 pokestar
    {0x25E00, 0x00F0, 0x25EF2, 0x25F90}, // 72 ???
    {0x25F00, 0x0094, 0x25FA2, 0x25FA2}, // 73 checksum (chk==mirror)
};

bool SaveFile::saveDS5(const std::string& path) {
    if (!loaded_ || rawData_.size() != DS_SAVE_SIZE || dsStorage_.empty())
        return false;
    if (dsGameByte_ < 20 || dsGameByte_ > 23)
        return false;
    const bool b2w2 = dsGameByte_ >= 22;

    // 1. Box: dsStorage_ (mutato da setBoxSlot) nei 24 blocchi da 0x1000.
    //    Gli slot occupano 0xFF0, il footer 0x10 resta finche' i CRC lo fissano.
    static constexpr int BOXES = 24;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < DS_BOX_SLOTS; s++) {
            size_t o = 0x400 + static_cast<size_t>(b) * 0x1000 +
                       static_cast<size_t>(s) * DS_SLOT_SIZE;
            const uint8_t* src = dsStorage_.data() +
                (static_cast<size_t>(b) * DS_BOX_SLOTS + s) * DS_SLOT_SIZE;
            std::memcpy(rawData_.data() + o, src, DS_SLOT_SIZE);
        }

    // 2. Party: compattato, count + 6x220 cifrati (stessi byte del loader).
    {
        std::vector<Pokemon> team;
        for (auto& pp : dsParty_)
            if (!pp.isEmpty() && pp.species() != 0) team.push_back(pp);
        if (team.size() > 6) team.resize(6);
        rawData_[0x18E04] = static_cast<uint8_t>(team.size());
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = rawData_.data() + 0x18E08 + i * 220;
            if (i >= static_cast<int>(team.size())) {
                std::memset(dst, 0, 220);
            } else {
                Pokemon w = team[i];
                w.gameType_ = gameType_;
                w.refreshChecksum();
                PokemonFFI::encryptArray45(w.data.data(), 220, dst);
            }
        }
        dsParty_.assign(6, Pokemon{});
        for (size_t i = 0; i < team.size() && i < 6; i++)
            dsParty_[i] = team[i];
    }

    // 3. CRC di TUTTI i blocchi (come PKHeX SetChecksums: gli altri blocchi
    //    tornano identici, i nostri si sistemano; checksum-block per ultimo).
    auto crcOne = [&](const Ds5Block& B) {
        if (B.off + B.len > rawData_.size() ||
            B.chk + 2 > rawData_.size() || B.mirror + 2 > rawData_.size())
            return;
        uint16_t c = crc16CcittFalse(rawData_.data() + B.off, B.len);
        writeU16LE(rawData_.data() + B.chk, c);
        writeU16LE(rawData_.data() + B.mirror, c);
    };
    const Ds5Block* fixed = b2w2 ? DS5_FIXED_B2W2 : DS5_FIXED_BW;
    size_t nFixed = b2w2 ? sizeof(DS5_FIXED_B2W2) / sizeof(Ds5Block)
                         : sizeof(DS5_FIXED_BW) / sizeof(Ds5Block);
    const uint32_t mirrorBase = b2w2 ? 0x25F02 : 0x23F02;
    crcOne(fixed[0]); // nomi box
    for (int b = 0; b < BOXES; b++) {
        Ds5Block bb{static_cast<uint32_t>(0x400 + b * 0x1000), 0xFF0,
                    static_cast<uint32_t>(0x13F2 + b * 0x1000),
                    mirrorBase + static_cast<uint32_t>(b) * 2};
        crcOne(bb);
    }
    for (size_t i = 1; i < nFixed; i++)
        crcOne(fixed[i]); // checksum-block per ultimo

    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f)
        f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    size_t written = std::fwrite(rawData_.data(), 1, rawData_.size(), f);
    std::fclose(f);
    DebugLog::line("saveDS5: %s -> OK (game %u %s)", path.c_str(), dsGameByte_,
                   b2w2 ? "B2W2" : "BW");
    return written == rawData_.size();
}

bool SaveFile::loadDXY(const std::string& path) {
    // Gen6 XY decrypted dump (PKHeX SAV6XY): boxes at 0x22600 (31 x 30 x
    // 232B EC-encrypted slots), MyStatus block at 0x14000 (Game byte at +4:
    // 24 = X, 25 = Y; OT trash at +0x48). Min size gate only (no exact size:
    // Citra/Checkpoint dumps vary); slots prove themselves by checksum.
    static constexpr size_t BOX_BASE = 0x22600;
    static constexpr int BOXES = 31;
    static constexpr int SLOT = 232;
    static constexpr size_t MIN_SIZE = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) < MIN_SIZE)
        return false;
    // Full file, not MIN_SIZE: Citra/Checkpoint dumps vary in tail length and
    // truncating here would destroy it on the first save (saveDXY rewrites
    // rawData_ verbatim). All offsets below are absolute, unaffected.
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    if (!file)
        return false;

    uint8_t game = rawData_[0x14000 + 4];
    if (game != 24 && game != 25) {
        DebugLog::line("loadDXY: %s -> Game byte %u non XY (dump cifrato?)", path.c_str(), game);
        return false;
    }
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[232] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[232];
        PokeCrypto::decryptArray6(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 232; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };
    int validSlots = 0;
    for (int b = 0; b < BOXES && validSlots < 3; b++)
        for (int s = 0; s < 30 && validSlots < 3; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            if (slotValid(rawData_.data() + o))
                validSlots++;
        }
    if (validSlots <= 0)
        DebugLog::line("loadDXY: %s -> nessuno slot valido (save vuoto?)", path.c_str());
    dsStorage_.assign(static_cast<size_t>(BOXES) * 30 * SLOT, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < 30; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * 30 + s) * SLOT;
            static const uint8_t ZERO[232] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue;
            }
            std::memcpy(dst, slot, SLOT);
        }
    if (badSlots > 0)
        DebugLog::line("loadDXY: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    DebugLog::line("loadDXY: %s -> Game %u (%s)", path.c_str(), game, game == 24 ? "X" : "Y");
    // Party: 6 x 260 at 0x14200 (SAV6XY Party = 0x14200, PKHeX)
    dsParty_.assign(6, Pokemon{});
    {
        constexpr size_t PARTY_OFF = 0x14200;
        constexpr int PARTY_SLOTS = 6;
        constexpr int PARTY_SIZE = 260; // SIZE_6PARTY, PokeCrypto
        if (PARTY_OFF + PARTY_SLOTS * PARTY_SIZE <= rawData_.size()) {
            int valid = 0;
            for (int i = 0; i < PARTY_SLOTS; i++) {
                const uint8_t* raw = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
                Pokemon p; p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, PARTY_SIZE);
                if (!p.isEmpty() && p.species()!=0) { dsParty_[i]=p; valid++; }
            }
            DebugLog::line("loadDXY: %s -> party %d/6", path.c_str(), valid);
        }
        if (dsOtName_.empty()) {
            for (auto &pp: dsParty_) if(!pp.isEmpty()){ dsOtName_=pp.otName(); break; }
        }
    }
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    // Box names: block 12 @0x04400, 31 x 0x22 UTF-16LE (generic stride).
    boxLayoutData_ = rawData_.data() + 0x04400;
    boxLayoutLen_ = BOXES * 0x22;
    loaded_ = true;
    return true;
}

bool SaveFile::loadDSM(const std::string& path) {
    // Gen7 SM decrypted dump (PKHeX SAV7SM): BoxPokemon at 0x04E00 (32 x 30
    // x 232B slots), MyStatus at 0x01200 (Game at +4: 30 = Sun, 31 = Moon;
    // OT at +0x38). Box names in BOX block at 0x04800 (32 x 0x22 UTF-16LE).
    static constexpr size_t BOX_BASE = 0x04E00;
    static constexpr int BOXES = 32;
    static constexpr int SLOT = 232;
    static constexpr size_t MIN_SIZE = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) < MIN_SIZE)
        return false;
    // Full file, see loadDXY: truncating would destroy the dump tail on save.
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    if (!file)
        return false;

    uint8_t game = rawData_[0x01200 + 4];
    if (game != 30 && game != 31) {
        DebugLog::line("loadDSM: %s -> Game byte %u non SM (dump cifrato?)", path.c_str(), game);
        return false;
    }
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[232] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[232];
        PokeCrypto::decryptArray6(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 232; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };
    int validSlots = 0;
    for (int b = 0; b < BOXES && validSlots < 3; b++)
        for (int s = 0; s < 30 && validSlots < 3; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            if (slotValid(rawData_.data() + o))
                validSlots++;
        }
    if (validSlots <= 0)
        DebugLog::line("loadDSM: %s -> nessuno slot valido (save vuoto?)", path.c_str());
    dsStorage_.assign(static_cast<size_t>(BOXES) * 30 * SLOT, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < 30; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * 30 + s) * SLOT;
            static const uint8_t ZERO[232] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue;
            }
            std::memcpy(dst, slot, SLOT);
        }
    if (badSlots > 0)
        DebugLog::line("loadDSM: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    DebugLog::line("loadDSM: %s -> Game %u (%s)", path.c_str(), game, game == 30 ? "Sun" : "Moon");
    // Party: block 04 PokePartySave at 0x01400, 6 x 260 (SAV7SM BlockInfoSM[04])
    dsParty_.assign(6, Pokemon{});
    {
        constexpr size_t PARTY_OFF = 0x01400;
        constexpr int PARTY_SLOTS = 6;
        constexpr int PARTY_SIZE = 260;
        if (PARTY_OFF + PARTY_SLOTS * PARTY_SIZE <= rawData_.size()) {
            int valid = 0;
            for (int i = 0; i < PARTY_SLOTS; i++) {
                const uint8_t* raw = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
                Pokemon p; p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, PARTY_SIZE);
                if (!p.isEmpty() && p.species()!=0) { dsParty_[i]=p; valid++; }
            }
            DebugLog::line("loadDSM: %s -> party %d/6", path.c_str(), valid);
        }
        if (dsOtName_.empty()) { for(auto &pp: dsParty_) if(!pp.isEmpty()){ dsOtName_=pp.otName(); break; } }
        if (dsOtName_.empty() && !dsParty_.empty()) dsOtName_ = dsParty_[0].otName();
    }
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = rawData_.data() + 0x04800;
    boxLayoutLen_ = BOXES * 0x22;
    loaded_ = true;
    return true;
}

// --- Gen 6/7 decrypted-dump save (PKHeX SAV6XY/SAV7SM) ---
//
// Decrypted Citra/Checkpoint dumps are flat memory images with NO checksums,
// so writing back is safe: boxes from dsStorage_ (slot-verified copies),
// party positionally (full 260B records, no tail math — same bytes the loader
// decrypted). Box names live in rawData_ via boxLayoutData_ and persist
// through it. Counts/other blocks untouched (minimal intervention).

bool SaveFile::saveDXY(const std::string& path) {
    if (!loaded_ || rawData_.empty() || dsStorage_.empty())
        return false;
    static constexpr size_t BOX_BASE = 0x22600;
    static constexpr int BOXES = 31;
    static constexpr int SLOT = 232;
    static constexpr size_t PARTY_OFF = 0x14200;
    static constexpr int PARTY_SIZE = 260;
    size_t need = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    if (rawData_.size() < need || dsStorage_.size() < need - BOX_BASE)
        return false;
    std::memcpy(rawData_.data() + BOX_BASE, dsStorage_.data(), need - BOX_BASE);
    if (PARTY_OFF + 6 * PARTY_SIZE <= rawData_.size()) {
        if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
            const Pokemon& m = dsParty_[i];
            if (m.isEmpty() || m.species() == 0) {
                std::memset(dst, 0, PARTY_SIZE);
            } else {
                Pokemon w = m;
                w.gameType_ = gameType_;
                // Full 260B party record (getEncrypted copre solo i 232B box).
                w.refreshChecksum();
                PokemonFFI::encryptArray6(w.data.data(), PARTY_SIZE, dst);
            }
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveDXY: %s -> OK", path.c_str());
    return true;
}

bool SaveFile::saveDSM(const std::string& path) {
    if (!loaded_ || rawData_.empty() || dsStorage_.empty())
        return false;
    static constexpr size_t BOX_BASE = 0x04E00;
    static constexpr int BOXES = 32;
    static constexpr int SLOT = 232;
    static constexpr size_t PARTY_OFF = 0x01400;
    static constexpr int PARTY_SIZE = 260;
    size_t need = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    if (rawData_.size() < need || dsStorage_.size() < need - BOX_BASE)
        return false;
    std::memcpy(rawData_.data() + BOX_BASE, dsStorage_.data(), need - BOX_BASE);
    if (PARTY_OFF + 6 * PARTY_SIZE <= rawData_.size()) {
        if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
            const Pokemon& m = dsParty_[i];
            if (m.isEmpty() || m.species() == 0) {
                std::memset(dst, 0, PARTY_SIZE);
            } else {
                Pokemon w = m;
                w.gameType_ = gameType_;
                // Full 260B party record (getEncrypted copre solo i 232B box).
                w.refreshChecksum();
                PokemonFFI::encryptArray6(w.data.data(), PARTY_SIZE, dst);
            }
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveDSM: %s -> OK", path.c_str());
    return true;
}

bool SaveFile::loadDSUSUM(const std::string& path) {
    // Gen7 USUM decrypted dump (PKHeX SAV7USUM): BoxPokemon at 0x05200
    // (32 x 30 x 232B Pk7 slots, stesso record di SM), MyStatus at 0x01400
    // (Game at +4: 32 = Ultra Sun, 33 = Ultra Moon), PokePartySave at 0x01600
    // (6 x 260). Box names in BOX block at 0x04C00 (32 x 0x22 UTF-16LE).
    // Stesso modello flat di loadDSM (niente checksum). Validato su
    // tools/test save/upstream/oh_ultrasun.sav (960/960 slot + party 6/6).
    static constexpr size_t BOX_BASE = 0x05200;
    static constexpr int BOXES = 32;
    static constexpr int SLOT = 232;
    static constexpr size_t MIN_SIZE = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) < MIN_SIZE)
        return false;
    // Full file, see loadDXY: truncating would destroy the dump tail on save.
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    if (!file)
        return false;

    uint8_t game = rawData_[0x01400 + 4];
    if (game != 32 && game != 33) {
        DebugLog::line("loadDSUSUM: %s -> Game byte %u non USUM (dump cifrato?)", path.c_str(), game);
        return false;
    }
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[232] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[232];
        PokeCrypto::decryptArray6(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 232; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };
    int validSlots = 0;
    for (int b = 0; b < BOXES && validSlots < 3; b++)
        for (int s = 0; s < 30 && validSlots < 3; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            if (slotValid(rawData_.data() + o))
                validSlots++;
        }
    if (validSlots <= 0)
        DebugLog::line("loadDSUSUM: %s -> nessuno slot valido (save vuoto?)", path.c_str());
    dsStorage_.assign(static_cast<size_t>(BOXES) * 30 * SLOT, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < 30; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * 30 + s) * SLOT;
            static const uint8_t ZERO[232] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue;
            }
            std::memcpy(dst, slot, SLOT);
        }
    if (badSlots > 0)
        DebugLog::line("loadDSUSUM: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    DebugLog::line("loadDSUSUM: %s -> Game %u (%s)", path.c_str(), game, game == 32 ? "Ultra Sun" : "Ultra Moon");
    // Party: block 04 PokePartySave at 0x01600, 6 x 260 (SaveBlockAccessor7USUM[04])
    dsParty_.assign(6, Pokemon{});
    {
        constexpr size_t PARTY_OFF = 0x01600;
        constexpr int PARTY_SLOTS = 6;
        constexpr int PARTY_SIZE = 260;
        if (PARTY_OFF + PARTY_SLOTS * PARTY_SIZE <= rawData_.size()) {
            int valid = 0;
            for (int i = 0; i < PARTY_SLOTS; i++) {
                const uint8_t* raw = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
                Pokemon p; p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, PARTY_SIZE);
                if (!p.isEmpty() && p.species()!=0) { dsParty_[i]=p; valid++; }
            }
            DebugLog::line("loadDSUSUM: %s -> party %d/6", path.c_str(), valid);
        }
        if (dsOtName_.empty()) { for(auto &pp: dsParty_) if(!pp.isEmpty()){ dsOtName_=pp.otName(); break; } }
        if (dsOtName_.empty() && !dsParty_.empty()) dsOtName_ = dsParty_[0].otName();
    }
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = rawData_.data() + 0x04C00;
    boxLayoutLen_ = BOXES * 0x22;
    loaded_ = true;
    return true;
}

bool SaveFile::saveDSUSUM(const std::string& path) {
    if (!loaded_ || rawData_.empty() || dsStorage_.empty())
        return false;
    static constexpr size_t BOX_BASE = 0x05200;
    static constexpr int BOXES = 32;
    static constexpr int SLOT = 232;
    static constexpr size_t PARTY_OFF = 0x01600;
    static constexpr int PARTY_SIZE = 260;
    size_t need = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    if (rawData_.size() < need || dsStorage_.size() < need - BOX_BASE)
        return false;
    std::memcpy(rawData_.data() + BOX_BASE, dsStorage_.data(), need - BOX_BASE);
    if (PARTY_OFF + 6 * PARTY_SIZE <= rawData_.size()) {
        if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
            const Pokemon& m = dsParty_[i];
            if (m.isEmpty() || m.species() == 0) {
                std::memset(dst, 0, PARTY_SIZE);
            } else {
                Pokemon w = m;
                w.gameType_ = gameType_;
                // Full 260B party record (getEncrypted copre solo i 232B box).
                w.refreshChecksum();
                PokemonFFI::encryptArray6(w.data.data(), PARTY_SIZE, dst);
            }
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveDSUSUM: %s -> OK", path.c_str());
    return true;
}

bool SaveFile::loadDSORAS(const std::string& path) {
    // Gen6 ORAS decrypted dump (PKHeX SAV6AO): Box block 56 at 0x33000
    // (31 x 30 x 232B Pk6 slots, stesso record di XY), MyStatus block 17 at
    // 0x14000 (Game at +4: 26 = Alpha Sapphire, 27 = Omega Ruby),
    // PokePartySave block 18 at 0x14200 (6 x 260). Box names in BOX block 12
    // at 0x04400 (31 x 0x22 UTF-16LE). Stesso modello flat di loadDXY
    // (niente checksum). Validato su tools/test save/upstream/oh_omegaruby.sav
    // (930/930 slot + party 2/6).
    static constexpr size_t BOX_BASE = 0x33000;
    static constexpr int BOXES = 31;
    static constexpr int SLOT = 232;
    static constexpr size_t MIN_SIZE = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) < MIN_SIZE)
        return false;
    // Full file, see loadDXY: truncating would destroy the dump tail on save.
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    rawData_.resize(fileSize);
    file.read(reinterpret_cast<char*>(rawData_.data()), fileSize);
    if (!file)
        return false;

    uint8_t game = rawData_[0x14000 + 4];
    if (game != 26 && game != 27) {
        DebugLog::line("loadDSORAS: %s -> Game byte %u non ORAS (dump cifrato?)", path.c_str(), game);
        return false;
    }
    auto slotValid = [&](const uint8_t* slot) -> bool {
        static const uint8_t ZERO[232] = {};
        if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
            return false;
        uint8_t dec[232];
        PokeCrypto::decryptArray6(slot, sizeof(dec), dec);
        uint32_t sum = 0;
        for (int i = 8; i < 232; i += 2)
            sum += static_cast<uint32_t>(dec[i] | (dec[i + 1] << 8));
        uint16_t stored = static_cast<uint16_t>(dec[6] | (dec[7] << 8));
        return (sum & 0xFFFF) == stored;
    };
    int validSlots = 0;
    for (int b = 0; b < BOXES && validSlots < 3; b++)
        for (int s = 0; s < 30 && validSlots < 3; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            if (slotValid(rawData_.data() + o))
                validSlots++;
        }
    if (validSlots <= 0)
        DebugLog::line("loadDSORAS: %s -> nessuno slot valido (save vuoto?)", path.c_str());
    dsStorage_.assign(static_cast<size_t>(BOXES) * 30 * SLOT, 0);
    int badSlots = 0;
    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < 30; s++) {
            size_t o = BOX_BASE + static_cast<size_t>(b) * 30 * SLOT + static_cast<size_t>(s) * SLOT;
            const uint8_t* slot = rawData_.data() + o;
            uint8_t* dst = dsStorage_.data() + (static_cast<size_t>(b) * 30 + s) * SLOT;
            static const uint8_t ZERO[232] = {};
            if (std::memcmp(slot, ZERO, sizeof(ZERO)) == 0)
                continue;
            if (!slotValid(slot)) {
                badSlots++;
                continue;
            }
            std::memcpy(dst, slot, SLOT);
        }
    if (badSlots > 0)
        DebugLog::line("loadDSORAS: %s -> %d slot corrotti nascosti", path.c_str(), badSlots);
    DebugLog::line("loadDSORAS: %s -> Game %u (%s)", path.c_str(), game, game == 27 ? "Omega Ruby" : "Alpha Sapphire");
    // Party: block 18 PokePartySave at 0x14200, 6 x 260 (come XY)
    dsParty_.assign(6, Pokemon{});
    {
        constexpr size_t PARTY_OFF = 0x14200;
        constexpr int PARTY_SLOTS = 6;
        constexpr int PARTY_SIZE = 260;
        if (PARTY_OFF + PARTY_SLOTS * PARTY_SIZE <= rawData_.size()) {
            int valid = 0;
            for (int i = 0; i < PARTY_SLOTS; i++) {
                const uint8_t* raw = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
                Pokemon p; p.gameType_ = gameType_;
                p.loadFromEncrypted(raw, PARTY_SIZE);
                if (!p.isEmpty() && p.species()!=0) { dsParty_[i]=p; valid++; }
            }
            DebugLog::line("loadDSORAS: %s -> party %d/6", path.c_str(), valid);
        }
        if (dsOtName_.empty()) { for(auto &pp: dsParty_) if(!pp.isEmpty()){ dsOtName_=pp.otName(); break; } }
        if (dsOtName_.empty() && !dsParty_.empty()) dsOtName_ = dsParty_[0].otName();
    }
    boxData_ = dsStorage_.data();
    boxDataLen_ = dsStorage_.size();
    boxLayoutData_ = rawData_.data() + 0x04400;
    boxLayoutLen_ = BOXES * 0x22;
    loaded_ = true;
    return true;
}

bool SaveFile::saveDSORAS(const std::string& path) {
    if (!loaded_ || rawData_.empty() || dsStorage_.empty())
        return false;
    static constexpr size_t BOX_BASE = 0x33000;
    static constexpr int BOXES = 31;
    static constexpr int SLOT = 232;
    static constexpr size_t PARTY_OFF = 0x14200;
    static constexpr int PARTY_SIZE = 260;
    size_t need = BOX_BASE + static_cast<size_t>(BOXES) * 30 * SLOT;
    if (rawData_.size() < need || dsStorage_.size() < need - BOX_BASE)
        return false;
    std::memcpy(rawData_.data() + BOX_BASE, dsStorage_.data(), need - BOX_BASE);
    if (PARTY_OFF + 6 * PARTY_SIZE <= rawData_.size()) {
        if ((int)dsParty_.size() != 6) dsParty_.assign(6, Pokemon{});
        for (int i = 0; i < 6; i++) {
            uint8_t* dst = rawData_.data() + PARTY_OFF + i * PARTY_SIZE;
            const Pokemon& m = dsParty_[i];
            if (m.isEmpty() || m.species() == 0) {
                std::memset(dst, 0, PARTY_SIZE);
            } else {
                Pokemon w = m;
                w.gameType_ = gameType_;
                // Full 260B party record (getEncrypted copre solo i 232B box).
                w.refreshChecksum();
                PokemonFFI::encryptArray6(w.data.data(), PARTY_SIZE, dst);
            }
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(rawData_.data()), rawData_.size());
    file.close();
    DebugLog::line("saveDSORAS: %s -> OK", path.c_str());
    return true;
}
