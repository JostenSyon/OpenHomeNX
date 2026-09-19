#pragma once
#include "save_file_ffi.h"
#include "pokemon.h"
#include "game_type.h"
#include "wondercard.h"
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

// SaveFile - manages Pokemon save files (Gen8/Gen9).
// SCBlock-based for ZA/SV/SwSh, flat binary for BDSP.
class SaveFile {
public:
    static constexpr int COLS_PER_BOX  = 6;
    static constexpr int ROWS_PER_BOX  = 5;

    void setGameType(GameType game);  // must call before load()

    bool load(const std::string& path);
    bool save(const std::string& path);

    // Gen4 layout picked by loadDS4 (DP/Pt/HGSS sizes differ).
    enum class Ds4Layout { DP, PT, HGSS };
    Ds4Layout dsLayout() const { return ds4Layout_; }
    // Partizione attiva scelta da loadDS4 (0/1): serve per localizzare il
    // blocco Generale da fuori (es. Pokedex::getDexStatus() read-only).
    int dsPartition() const { return dsPart_; }
    // Dimensione di una partizione Gen4 (DS_PARTITION e' privato): esposta
    // per non duplicare il numero altrove (es. pokedex.cpp).
    static constexpr size_t dsPartitionBytes() { return DS_PARTITION; }
    // Offset assoluto del bitfield "caught" del Pokedex Gen2 (G/S vs
    // Crystal): unica fonte di verita', GbcLayout resta privato a
    // save_file.cpp. 0 se il gameType_ corrente non e' Gen2.
    size_t gen2DexCaughtOffset() const;
    // HGSS ROMCode (Trainer1+0x1C: 7 = HeartGold, 8 = SoulSilver), 0 if N/A.
    uint8_t dsRomCode() const { return dsRomCode_; }
    // Gen5 PlayerData.Game byte (20 = White, 21 = Black), 0 if N/A.
    uint8_t dsGameByte() const { return dsGameByte_; }
    // DS save identity for the status-bar strip (OT/TID/party). Empty for
    // every other game family — the strip hides itself.
    const std::vector<Pokemon>& dsParty() const { return dsParty_; }
    const std::string& dsOtName() const { return dsOtName_; }
    uint16_t dsTid() const { return dsTid_; }
    // Party slot access (Switch + DS/GB families). For SCBlock/BDSP/DXY/DSM these
    // modify the underlying save block/raw and keep dsParty_ in sync; for the
    // others they just proxy dsParty_. Returns empty Pokemon for out-of-range or
    // empty slot. set/clear mark dirty.
    Pokemon getPartySlot(int idx) const;
    void setPartySlot(int idx, const Pokemon& pkm);
    void clearPartySlot(int idx);
    // Debug: piazza un Caterpie segnaposto (L5, PK3 valido) in slot 0 quando
    // la squadra e vuota — nessun gioco accetta party 0 (Smeraldo spawnava
    // glitch). Solo GBA (FRLG + R/S/E importati); altrove torna false e la UI
    // rimanda a sistemare a mano. Identita OT copiata dal primo mon dei box.
    bool placeCaterpiePlaceholder();
    // Segnaposto universale (debug) per le altre famiglie: Magikarp L5
    // Splash via generatore FFI + transfer, costruito come un drop normale
    // (stessi byte che produrrebbe prepareForPlacement). GBA usa Caterpie.
    bool placePlaceholder();
    // LGPE: il party sono 6 pointer nella lista piatta dei box — il mon vive in
    // UNA sola cella box. Servono per tenere pointer/cella in sync (senza: la
    // cella resta sporca -> cloni nei box; o il pointer penzola -> mon perso).
    int  lgpeFlatOfParty(int i) const;      // flat box index del pointer i, o -1
    void lgpeZeroFlatSlot(int flat);        // azzera i dati della cella (pointer invariato)
    void refreshPartyEntryFromPointer(int idx); // rileggi dsParty_[idx] dalla sua cella
    bool hasParty() const { for (auto &p: dsParty_) if (!p.isEmpty()) return true; return false; }
    // Vera se la strip era gia vuota al load (save vergine inizio gioco):
    // l'exit-hook salta il segnaposto, vuoto e legittimo e non forzato.
    bool wasPartyEmptyAtLoad() const { return partyEmptyAtLoad_; }
    // Normalizza un save Delta/iPhone (+16B metadata) a raw 128K permanente
    // (mGBA & co. vogliono 131072B esatti). Rileva la finestra valida (testa
    // o coda) e riscrive solo quella, byte-identica. Mai silenzioso: info
    // descrive l'esito. True = file riscritto.
    static bool normalizeDeltaSave(const std::string& path, std::string& info);

    Pokemon getBoxSlot(int box, int slot) const;
    void setBoxSlot(int box, int slot, Pokemon pkm);
    void clearBoxSlot(int box, int slot);

    std::string getBoxName(int box) const;

    bool isLoaded() const { return loaded_; }
    // True once a mutator has changed persistent content since the last
    // load()/save(). Lets the UI skip rewriting an untouched save on B.
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; invalidateAllBoxCache(); }
    GameType gameType() const { return gameType_; }

    // Access SCBlock by key (for SCBlock-based games: ZA/SV/SwSh/LA) — via FFI wrapper M3a
    SCBlock* findBlock(uint32_t key) { return SaveFileFFI::findBlock(blocks_, key); }

    // Access raw save data (for flat binary games: BDSP/LGPE/FRLG)
    uint8_t* rawData() { return rawData_.data(); }
    size_t rawDataSize() const { return rawData_.size(); }

    // Find GBA sector data by section ID in the active save slot.
    // Returns pointer to the sector's 0x1000-byte region, or nullptr.
    uint8_t* findGbaSectorData(int sectionId);

    // National Dex flag for R/S/E (struct Pokedex @ section0+0x18: order@0,
    // mode@1, nationalMagic@2 -- il vero gate, deve valere 0xDA -- SaveBlock2
    // offset assoluto 0x1A; scritto su entrambe le slot)
    bool isNationalDexEnabled() const;
    void setNationalDexEnabled();
    // Inverso esatto di setNationalDexEnabled() -- stesso mirroring pret
    // (event_data.c DisableNationalPokedex): azzera nationalMagic, mode,
    // VAR_NATIONAL_DEX e FLAG_SYS_NATIONAL_DEX su entrambe le slot. Non
    // tocca i bitfield caught/seen (restano intatti, riappaiono in vista
    // National se lo si riabilita) -- e' l'unica azione che il gioco vero
    // stesso compie (in pratica solo a New Game), quindi sicura da rifare
    // a mano: reversibile, nessun altro sistema dipende dal fatto che sia
    // irreversibile.
    void disableNationalDex();

    // Borsa Gen3 GBA (RSE/FRLG, settore 1 = SaveBlock1). Slot da 4B
    // {id u16 LE, count u16 LE}. Layout verificato: pret global.h di
    // pokeruby/pokeemerald/pokefirered + span PKHeX SAV3* + Bulbapedia:
    // RS(20/20/16/64/46) E(30/30/16/64/46) FRLG(42/30/13/58/43).
    enum class GbaBagPocket { Items = 0, Key, Balls, TmHm, Berries, Count };
    struct GbaBagSlot { GbaBagPocket pocket; int slot; uint16_t id; uint16_t count; };
    bool gbaBagSupported() const;
    int gbaBagPocketSlots(GbaBagPocket p) const; // 0 se non supportato
    // Tutti gli slot (anche vuoti id==0) per lettura/scansione.
    std::vector<GbaBagSlot> readGbaBag() const;
    // Scrive uno slot (set dirty). False se pocket/slot fuori range.
    bool writeGbaBagSlot(GbaBagPocket p, int slot, uint16_t id, uint16_t count);
    // Flag evento Gen3 (FRLG/RSE), 1 bit per flag (flag/8, flag%8) dentro
    // SaveBlock1.flags[]. Offset assoluto e sezione GBA (settore 1-4)
    // dipendono dal gioco -- FRLG/RSE hanno struct SaveBlock1 diverse
    // (flags[] a 0xEE0 su FRLG, 0x1270 su Emerald, 0x1220 su Ruby/Sapphire;
    // 2026-09-19: un offset unico riusato da FRLG su Emerald/RS scriveva
    // nel settore sbagliato, vedi gbaFlagLocation() in save_file.cpp).
    // Per FRLG Aurora/Mistico imposta 0x2A7/0x84B e 0x2A8/0x84A, per Smeraldo ecc. vedi docs.
    bool setGbaFlag(uint16_t flag);
    bool isGbaFlagSet(uint16_t flag) const;
    // Chiave di sicurezza Gen3 (settore 0, trainer info): RS non la usa
    // (conteggi in chiaro), Emerald/FRLG la XORano su monete/gettoni/
    // conteggi zaino (Bulbapedia "Save data structure (Generation III)").
    // Ritorna solo i 16 bit bassi (quelli usati per gli item), 0 se non serve.
    uint16_t gbaSecurityKeyLow16() const;

    // Borsa Gen4/5 DS (PKHeX PlayerBag4DP/4Pt/4HGSS/5BW/5B2W2). Slot da 4B
    // {id u16 LE, count u16 LE}, in chiaro (niente XOR). Gen4: base relativa
    // al blocco General della partizione attiva (dsPart_); Gen5: blocco 25
    // fisso 0x18400. Slot gestiti = liste legali PKHeX (padding preservato,
    // mai toccato); CRC sistemati da saveDS4/saveDS5.
    enum class DsBagPocket { Items = 0, Key, TmHm, Mail, Medicine, Berries, Balls, Battle, Count };
    struct DsBagSlot { DsBagPocket pocket; int slot; uint16_t id; uint16_t count; };
    bool dsBagSupported() const;
    int dsBagPocketSlots(DsBagPocket p) const; // 0 se non supportato (es. Mail su Gen5)
    // Tutti gli slot gestiti (anche vuoti id==0) per lettura/scansione.
    std::vector<DsBagSlot> readDsBag() const;
    // Scrive uno slot (set dirty). False se pocket/slot fuori range.
    bool writeDsBagSlot(DsBagPocket p, int slot, uint16_t id, uint16_t count);

    // Borsa Gen1/2 GB (PKHeX PlayerBag1/PlayerBag2 + InventoryPouchGB).
    // Niente slot fissi: tasche normali = liste compattate [count][id,count]*
    // + 0xFF (Key: [count][id]* + 0xFF, count sempre 1); TM Gen2 = array fisso
    // 57B (count per indice lista legale, 0 = assente). Gli slot esposti qui
    // sono POSIZIONI nella lista compattata (per vista/audit); la scrittura
    // riscrive la tasca intera compattata come PKHeX SetPouch. Gen1: solo
    // Items (le MT sono oggetti normali); PC fuori perimetro. Solo INT
    // (i JP hanno offset diversi e lo scan li rifiuta).
    enum class GbBagPocket { Items = 0, Key, TmHm, Balls, Count };
    struct GbBagSlot { GbBagPocket pocket; int slot; uint16_t id; uint16_t count; };
    bool gbBagSupported() const;
    // Tutte le voci presenti (niente vuoti: le liste sono compattate).
    std::vector<GbBagSlot> readGbBag() const;
    // Riscrive una tasca intera da lista compattata (set dirty). False se
    // tasca assente per il gioco o count oltre i max gestiti.
    bool writeGbBagPocket(GbBagPocket p,
                          const std::vector<std::pair<uint16_t, uint16_t>>& items);
    // Ordine id legali della tasca TM Gen2 (array fisso); vuoto altrove.
    std::vector<uint16_t> gbBagTmOrder() const;

    // Get trainer info from save file (SV/ZA only, SCBlock-based)
    TrainerInfo getTrainerInfo() const;

    // Get save file language (shorthand for getTrainerInfo().language)
    uint8_t saveLanguage() const { auto ti = getTrainerInfo(); return ti.valid ? ti.language : 0; }

    // Debug: verify encrypt(decrypt(file)) == file. Call right after load().
    // Returns "OK" if round-trip matches, or a description of the mismatch.
    std::string verifyRoundTrip();

    // Debug PK vs OH: for a SwSh save with the Rust OH handle loaded, compare
    // per-slot the box bytes from the PK path (loadFromEncrypted) vs the OH
    // path (Rust getSlot + getPkmBoxBytes) over the region the app persists
    // (sizeBoxSlot_ - gapBoxSlot_). Returns "" if not applicable (no OH handle)
    // or if identical, otherwise a short summary suitable for a message box.
    // Full per-slot detail also goes to stderr.
    std::string debugCompareEnginesParity() const;

    // Dynamic box count and slots per box
    int boxCount() const { return boxCount_; }
    int slotsPerBox() const { return slotsPerBox_; }

    // LGPE party slot detection and management
    static constexpr uint16_t LGPE_SLOT_EMPTY = 1001;
    bool isLGPEPartySlot(int box, int slot) const;
    int  lgpePartyIndexOf(int box, int slot) const;  // returns party idx 0-5, or -1
    void setLGPEPartyPointer(int partyIdx, uint16_t flatSlot);
    const std::array<uint16_t, 6>& lgpePartyIndices() const { return lgpePartyIndices_; }
    void setLGPEPartyIndices(const std::array<uint16_t, 6>& v);

private:
    std::vector<SCBlock> blocks_;

    // Cached pointers into block data (SCBlock) or raw data (BDSP)
    uint8_t* boxData_       = nullptr;
    size_t   boxDataLen_    = 0;
    uint8_t* boxLayoutData_ = nullptr;
    size_t   boxLayoutLen_  = 0;

    std::string filePath_;
    bool loaded_ = false;
    // Set by any content mutator, cleared by load() and a successful save().
    bool dirty_ = false;
    // Strip vuota al load (save vergine): vedi load().
    bool partyEmptyAtLoad_ = true;

    // Game-specific parameters
    GameType gameType_  = GameType::ZA;
    int boxCount_       = 32;
    int slotsPerBox_    = 30;
    int gapBoxSlot_     = 0x40;
    int sizeBoxSlot_    = PokeCrypto::SIZE_9PARTY + 0x40;

    // SCBlock keys
    uint32_t kbox_ = 0x0d66012c;                        // LA uses 0x47E1CEAB
    static constexpr uint32_t KBOX_LAYOUT = 0x19722c89; // same for all games

    // BDSP save format constants
    static constexpr int BDSP_BOX_COUNT       = 40;
    static constexpr int BDSP_BOX_OFFSET      = 0x14EF4;
    static constexpr int BDSP_LAYOUT_OFFSET   = 0x148AA;
    static constexpr int BDSP_LAYOUT_SIZE     = 0x64A;
    static constexpr int BDSP_HASH_OFFSET     = 0xE9818;
    static constexpr int BDSP_HASH_SIZE       = 16;

    // LGPE save format constants
    static constexpr int LGPE_SAVE_SIZE      = 0x100000;  // 1 MB
    static constexpr int LGPE_BOX_OFFSET     = 0x05C00;   // PokeListPokemon block
    static constexpr int LGPE_BOX_SIZE       = 0x3F7A0;   // 1000 * 260
    static constexpr int LGPE_HEADER_OFFSET  = 0x05A00;   // PokeListHeader block
    static constexpr int LGPE_BOX_COUNT      = 40;
    static constexpr int LGPE_SLOTS_PER_BOX  = 25;
    static constexpr int LGPE_BLOCK_INFO_OFS = 0xB8600;   // boGG from PKHeX (block info base)
    static constexpr int LGPE_NUM_BLOCKS     = 21;

    struct LGPEBlock { int offset; int length; };
    static constexpr LGPEBlock LGPE_BLOCKS[LGPE_NUM_BLOCKS] = {
        {0x00000, 0x00D90}, {0x00E00, 0x00200}, {0x01000, 0x00168},
        {0x01200, 0x01800}, {0x02A00, 0x020E8}, {0x04C00, 0x00930},
        {0x05600, 0x00004}, {0x05800, 0x00130}, {0x05A00, 0x00012},
        {0x05C00, 0x3F7A0}, {0x45400, 0x00008}, {0x45600, 0x00E90},
        {0x46600, 0x010A4}, {0x47800, 0x000F0}, {0x47A00, 0x06010},
        {0x4DC00, 0x00200}, {0x4DE00, 0x00098}, {0x4E000, 0x00068},
        {0x4E200, 0x69780}, {0xB7A00, 0x000B0}, {0xB7C00, 0x00940},
    };

    static uint16_t crc16NoInvert(const uint8_t* data, size_t len);

    // GBA save format constants (sector-based, for FRLG)
    static constexpr int GBA_SAVE_SIZE       = 0x20000;  // 128KB
    static constexpr int GBA_SECTOR_SIZE     = 0x1000;   // 4KB per sector
    static constexpr int GBA_SECTOR_USED     = 0xF80;    // usable data per sector
    static constexpr int GBA_SECTOR_COUNT    = 14;       // sectors per save slot
    static constexpr int GBA_STORAGE_FIRST   = 5;        // first sector ID for box storage
    static constexpr int GBA_STORAGE_LAST    = 13;       // last sector ID for box storage
    static constexpr int GBA_STORAGE_SECTORS = 9;        // sectors for box storage
    static constexpr int GBA_BOX_COUNT       = 14;
    static constexpr int GBA_SLOTS_PER_BOX   = 30;
    static constexpr int GBA_BOXNAME_LEN     = 9;        // 8 chars + terminator (Gen3 encoded)

    // GBA sector footer offsets (within each 0x1000 sector)
    static constexpr int GBA_OFS_SECTOR_ID   = 0xFF4;    // u16 sector ID
    static constexpr int GBA_OFS_CHECKSUM    = 0xFF6;    // u16 checksum
    static constexpr int GBA_OFS_SAVE_INDEX  = 0xFFC;    // u32 save counter

    // GBA CheckSum32: sum u32s in data, fold to u16
    static uint16_t checkSum32GBA(const uint8_t* data, size_t len);

    // GBA assembled storage buffer (sectors 5-13 concatenated)
    std::vector<uint8_t> gbaStorage_;
    int gbaActiveSlot_ = 0;
    // GBA party location from the loadGBA brute scan (count at large+off,
    // 6x100B slots at large+off+pad). -1 = not found (party read-only then).
    int gbaPartyLargeOff_ = -1;
    int gbaPartyPad_ = 4;
    // Map a Large offset (sectors 1-3 concatenated) to an absolute rawData_
    // offset in the given save slot (default active). -1 if unmappable.
    long gbaLargeToRaw(size_t largeOff, int slot = -1) const;
    // 16B extra di alcuni emulatori (conservati e riattaccati in scrittura).
    std::vector<uint8_t> gbaXtra_;
    bool gbaXtraAtEnd_ = true;

    // LGPE party slot tracking (indices into flat 1000-slot list)
    std::array<uint16_t, 6> lgpePartyIndices_{};
    int lgpePartyCount_ = 0;

    // BDSP and LGPE raw save data (flat binary, no SCBlocks)
    std::vector<uint8_t> rawData_;

    // Original file data for round-trip verification (cleared after verify)
    std::vector<uint8_t> originalFileData_;

    // Decrypted Pokemon cache: avoids re-decrypting on every getBoxSlot() call.
    // Maps box index -> vector of decrypted Pokemon (one per slot).
    static constexpr int BOX_CACHE_MAX = 4; // max cached boxes
    mutable std::unordered_map<int, std::vector<Pokemon>> boxCache_;
    void invalidateBoxCache(int box) const { boxCache_.erase(box); }
    void invalidateAllBoxCache() const { boxCache_.clear(); }
    const std::vector<Pokemon>& getCachedBox(int box) const;

    // Rust save handle (OH path) — freed automatically via unique_ptr deleter.
    // Declared after blocks_/boxData_ so it's destroyed first (LIFO),
    // but order doesn't matter since openhome_free_save only frees the Rust wrapper.
    struct SaveHandleDeleter {
        void operator()(SaveHandle* h) const {
            if (h) openhome_free_save(h);
        }
    };
    std::unique_ptr<SaveHandle, SaveHandleDeleter> saveHandleRust_;

    bool loadSCBlock(const std::string& path);
    bool saveSCBlock(const std::string& path);
    bool loadBDSP(const std::string& path);
    bool saveBDSP(const std::string& path);
    bool loadLGPE(const std::string& path);
    bool saveLGPE(const std::string& path);
    bool loadGBA(const std::string& path);
    bool saveGBA(const std::string& path);
    // Gen 1 (R/B/Y SRAM, G1c/G1d): unpacks the 12 PokeList1 boxes into gbStorage_
    // (flat 55B slots: 33B record + 11B OT + 11B nick); saveGB() packs back +
    // file checksum. Party is preserved byte-wise (never parsed).
    bool loadGB(const std::string& path);
    bool saveGB(const std::string& path);
    // Flat unpacked Gen1 box storage (12*20*55B), backing boxData_.
    std::vector<uint8_t> gbStorage_;
    // Per-box trust from loadGB(): only the current box + (when the save was
    // flushed) the stored boxes were parsed. Untrusted boxes are preserved
    // byte-wise on save (never zeroed): we don't write what we didn't read.
    bool gbBoxTrusted_[12] = {false};
    std::vector<uint8_t> gbStoredOrig_;
    // Gen1 party snapshot (404B @0x2F2C: count+species+FF+6x44B+OT+nick) for
    // tail-preserving write-back. Empty when party wasn't found at load.
    std::vector<uint8_t> gbPartySnap_;
    // Gen 2 (G/S/C SRAM, G2c): same model, 14 boxes x 20 x 54B stride
    // (32B record + 11B OT + 11B nick). No current-box mirror in Gen2
    // (Stadium desyncs it): only stored regions are truth.
    bool loadGBC(const std::string& path);
    bool saveGBC(const std::string& path);
    std::vector<uint8_t> gbcStorage_;
    bool gbcBoxTrusted_[14] = {false};
    std::vector<uint8_t> gbcStoredOrig_;
    // Gen2 party base offset (pbase from layout) + 428B snapshot for
    // tail-preserving write-back. pbase < 0 when not found at load.
    int gbcPartyBase_ = -1;
    std::vector<uint8_t> gbcPartySnap_;
    bool gbcIsCrystal_ = false;
    int gbcBoxNamesBase_ = -1; // 9B box-name stride base in rawData_ (-1 none)

    // Gen 4/5 (DS .sav dumps, 512KB NDS flash). Read-only v1 (save later:
    // Gen5 block footers are intricate): boxes copied slot-verified into
    // dsStorage_ (flat 136B slots, bad-checksum slots zeroed + logged).
    // Party/box-names v2 (UI falls back to "Box N", party hidden as GBA).
    static constexpr size_t DS_SAVE_SIZE = 0x80000;  // 512KB
    static constexpr int DS_PARTITION   = 0x40000;  // 256KB per partition
    static constexpr int DS_BOX_SLOTS   = 30;
    static constexpr int DS_SLOT_SIZE   = 136;       // PK4/PK5 box record
    bool loadDS4(const std::string& path);
    bool saveDS4(const std::string& path);
    bool loadDS5(const std::string& path);
    bool saveDS5(const std::string& path);
    // Gen 6/7 (decrypted 3DS dumps, Citra/Checkpoint style; cartridge-encrypted
    // dumps are rejected explicitly). Flat images without checksums: writable.
    bool loadDXY(const std::string& path);
    bool saveDXY(const std::string& path);
    // Gen 6 ORAS (PKHeX SAV6AO, stesso record Pk6 di XY ma blocchi spostati:
    // box 0x33000 31x30x232, party 0x14200 6x260, MyStatus 0x14000+4 = 26/27,
    // nomi 0x04400 31x0x22). Stesso modello flat di loadDXY (niente checksum).
    bool loadDSORAS(const std::string& path);
    bool saveDSORAS(const std::string& path);
    bool loadDSM(const std::string& path);
    bool saveDSM(const std::string& path);
    // Gen 7 USUM (PKHeX SAV7USUM, stesso record Pk7 di SM ma blocchi spostati:
    // box 0x05200 32x30x232, party 0x01600 6x260, MyStatus 0x01400+4 = 32/33,
    // nomi 0x04C00 32x0x22). Stesso modello flat di loadDSM (niente checksum).
    bool loadDSUSUM(const std::string& path);
    bool saveDSUSUM(const std::string& path);
    std::vector<uint8_t> dsStorage_;
    Ds4Layout ds4Layout_ = Ds4Layout::DP;
    int dsPart_ = 0; // active Gen4 partition picked by loadDS4
    // Inizio regione borsa DS nei dati grezzi (assoluto in rawData_), o -1 se
    // gioco/layout incoerenti (mai scrivere all'offset sbagliato).
    long dsBagBase() const;
    uint8_t dsRomCode_ = 0;
    uint8_t dsGameByte_ = 0;
    std::vector<Pokemon> dsParty_;
    std::string dsOtName_;
    uint16_t dsTid_ = 0;

    static bool isBDSPSize(size_t size);

    int getBoxOffset(int box) const {
        return sizeBoxSlot_ * box * slotsPerBox_;
    }
    int getBoxSlotOffset(int box, int slot) const {
        return getBoxOffset(box) + slot * sizeBoxSlot_;
    }
};
