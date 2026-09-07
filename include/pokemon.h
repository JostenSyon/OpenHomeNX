#pragma once
#include "poke_crypto.h"
#include "game_type.h"
#include "gen1_tables.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <utility>

// EXP table: 6 growth rates x 100 levels (defined in pokemon.cpp)
extern const uint32_t EXP_TABLE[6][100];

// Per-format offset table for Pokemon data fields.
// -1 means the field needs special handling (see accessor comments).
struct PokemonOffsets {
    int speciesInternal;  // readU16
    int heldItem;         // readU16
    int pid;              // readU32
    int nature;           // byte read, -1 = pid % 25 (PK3)
    int fateful;          // byte offset, -1 = u32 bit (PK3)
    int fatefulBit;       // bit position within byte
    int genderByte;       // byte offset, -1 = PID-based (PK3)
    int genderShift;      // right-shift before & 3
    int form;             // byte offset, -1 = always 0 (PK3)
    int formShift;        // right-shift (3 for PB7, 0 otherwise)
    int ball;             // byte offset, -1 = u16 bits (PK3)
    int ability;          // offset, -1 = always 0 (PK3)
    bool abilityIsU8;     // true for PB7
    int evBase;           // evHp = base+0 .. evSpD = base+5
    int tid;              // readU16
    int sid;              // readU16
    int moveBase;         // move1 = base, +2, +4, +6
    int iv32;             // readU32
    int nickname;         // UTF16 offset, -1 = Gen3 encoding
    int otName;           // UTF16 offset, -1 = Gen3 encoding
    int levelByte;        // byte offset, -1 = computed from EXP
    int expOfs;           // u32 offset for EXP, -1 = N/A
    int alphaByte;        // byte for alpha, -1 = always false
    bool alphaIsNonZero;  // true = !=0 (PA9), false = bit5 (PA8)
    int languageByte;     // byte offset for language field
    int formArgument;     // u32 offset (Alcremie decoration), -1 = N/A
    int canGmaxByte;      // byte with Gmax flag (bit 4), -1 = N/A
    int heightScalar;     // u8 offset (LGPE size), -1 = N/A
    int weightScalar;     // u8 offset (LGPE size), -1 = N/A
    int htName;           // handling-trainer name, UTF16 offset, -1 = no HT (Gen3)
};

// Returns the offset table for a given game format.
inline const PokemonOffsets& pokemonOffsetsFor(GameType g) {
    //                              spec  held  pid   nat  fate fBit gend gShf form fShf ball  abi  aU8  ev    tid   sid   move  iv32  nick  ot    lvl   exp   alph aNZ  lang  fArg  gmax hSca wSca htName
    static constexpr PokemonOffsets PK3 = {0x20, 0x22, 0x00, -1,  -1,  31,  -1,  0,   -1,  0,   -1,  -1,  false, 0x38, 0x04, 0x06, 0x2C, 0x48, -1,   -1,   -1,   0x24, -1,  false, 0x12, -1,   -1,   -1,  -1,  -1};
    // PK4 (Gen4 DPPt/HGSS, decrypted 236B): nature = pid%25, ball = max(0x83,0x86)
    // via branch, names via Gen4 codec branch (rows -1), level from EXP.
    static constexpr PokemonOffsets PK4 = {0x08, 0x0A, 0x00, -1,  0x40, 0,  0x40, 1,  0x40, 3,  0x83, 0x15, true, 0x18, 0x0C, 0x0E, 0x28, 0x38, -1,   -1,   -1,   0x10, -1,  false, 0x17, -1,   -1,   -1,  -1,  -1};
    // PK5 (Gen5 BW, decrypted 220B): nature byte at 0x41, ball at 0x83,
    // names direct UTF-16LE (0xFFFF-terminated) via branch.
    static constexpr PokemonOffsets PK5 = {0x08, 0x0A, 0x00, 0x41, 0x40, 0,  0x40, 1,  0x40, 3,  0x83, 0x15, true, 0x18, 0x0C, 0x0E, 0x28, 0x38, -1,   -1,   -1,   0x10, -1,  false, 0x17, -1,   -1,   -1,  -1,  -1};
    static constexpr PokemonOffsets PB7 = {0x08, 0x0A, 0x18, 0x1C, 0x1D, 0,  0x1D, 1,  0x1D, 3,  0xDC, 0x14, true, 0x1E, 0x0C, 0x0E, 0x5A, 0x74, 0x40, 0xB0, 0xEC, -1,   -1,  false, 0xE3, 0x3C, -1,   0x3A,0x3B, 0x78};
    static constexpr PokemonOffsets PK8 = {0x08, 0x0A, 0x1C, 0x20, 0x22, 0,  0x22, 2,  0x24, 0,  0x124, 0x14, false, 0x26, 0x0C, 0x0E, 0x72, 0x8C, 0x58, 0xF8, 0x148, -1,  -1,  false, 0xE2, 0xE4, 0x16, -1,  -1,  0xA8};
    static constexpr PokemonOffsets PA8 = {0x08, 0x0A, 0x1C, 0x20, 0x22, 0,  0x22, 2,  0x24, 0,  0x137, 0x14, false, 0x26, 0x0C, 0x0E, 0x54, 0x94, 0x60, 0x110, -1,   0x10, 0x16, false, 0xF2, 0xE4, -1,   -1,  -1,  0xB8};
    static constexpr PokemonOffsets PA9 = {0x08, 0x0A, 0x1C, 0x20, 0x22, 0,  0x22, 1,  0x24, 0,  0x124, 0x14, false, 0x26, 0x0C, 0x0E, 0x72, 0x8C, 0x58, 0xF8, 0x148, -1,  0x23, true,  0xD5, 0xD0, -1,   -1,  -1,  0xA8};
    // PK1 (Gen1 33B + OT/nick): every accessor branches on isGen1File()
    // BEFORE consulting ofs(), so these values are only a backstop. All -1
    // (with evBase 0) so a missed branch degrades to defaults, never to
    // another format's garbage. CRITICAL: without this row Gen1 fell through
    // to PK8, making sameStoredFormat() claim SwSh==Red and silently dumping
    // raw Pk8 bytes into Gen1 slots (NidoranF shown as Gyarados, 2026-09-06).
    static constexpr PokemonOffsets PK1 = {-1,  -1,  -1,  -1,  -1,  0,   -1,  0,   -1,  0,   -1,  -1,  false, 0,    -1,  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,  false, -1,   -1,   -1,   -1,  -1,  -1};
    if (isGen1File(g)) return PK1;
    if (isGen4File(g)) return PK4;
    if (isGen5File(g)) return PK5;
    // XY and SM reuse the PB7 row: identical decrypted layout (species 0x08,
    // moves 0x5A, OT 0xB0, ball 0xDC...) and 232B box records. Only Gen6
    // shininess differs (EC-based, see isShiny); SM matches PB7's PID rule.
    if (isGen6XY(g) || isGen7SM(g)) return PB7;
    if (isFRLG(g) || isImportedFile(g)) return PK3;
    if (isLGPE(g)) return PB7;
    if (g == GameType::LA) return PA8;
    if (isSV(g) || g == GameType::ZA) return PA9;
    return PK8; // SwSh, BDSP
}

// True if two game types share the same stored PKM byte layout, i.e. a Pokemon
// read from `a` can be written into `b` without reinterpreting its bytes.
// Derived from pokemonOffsetsFor's own dispatch (same offset table == same
// layout), so it cannot drift if that dispatch changes.
inline bool sameStoredFormat(GameType a, GameType b) {
    return &pokemonOffsetsFor(a) == &pokemonOffsetsFor(b);
}

// Pokemon data structure for Gen3/Gen6/Gen8/Gen8a/Gen9 (PK3/PB7/PK8/PA8/PA9).
// Ported from PKHeX.Core/PKM/PA9.cs, G8PKM.cs, PA8.cs, PK3.cs
//
// Gen 1 (R/B/Y, isGen1File) reuses the same array with its own layout:
// [0:33] box record, [33:44] OT name (GB, 11B), [44:55] nickname (GB, 11B).
// Gen1 has no PID/IV32/checksum/held-item/egg/gender — every accessor below
// branches explicitly so the PK8 offset table is never consulted for it.
struct Pokemon {
    std::array<uint8_t, PokeCrypto::MAX_PARTY_SIZE> data{};
    GameType gameType_ = GameType::ZA;

    // OHPKM OriginalBackup of the pre-conversion original, kept when this mon
    // was produced by a cross-gen transfer: [tag u16 LE][verbatim stored
    // record]. Empty when the mon was never converted. Lets a return trip to
    // the origin format restore the exact original bytes (lossless), and is
    // persisted by the cross-gen bank format (Plan C2). Not part of the fixed
    // on-disk slot record.
    std::vector<uint8_t> ohBackup_;

    // Full OHPKM record, set while a mon is "in hand" after being picked up from
    // a cross-gen bank (approach B). When non-empty, prepareForPlacement builds
    // the destination format straight from this (the OHPKM carries its own
    // OriginalBackup, so a return to the origin format stays lossless).
    std::vector<uint8_t> ohpkmBlob_;

    // --- Helpers ---
    const PokemonOffsets& ofs() const { return pokemonOffsetsFor(gameType_); }

    uint16_t readU16(int o) const {
        uint16_t v;
        std::memcpy(&v, data.data() + o, 2);
        return v;
    }
    uint32_t readU32(int o) const {
        uint32_t v;
        std::memcpy(&v, data.data() + o, 4);
        return v;
    }
    void writeU16(int o, uint16_t v) {
        std::memcpy(data.data() + o, &v, 2);
    }
    void writeU32(int o, uint32_t v) {
        std::memcpy(data.data() + o, &v, 4);
    }

    // --- Block A (offset-table driven) ---

    uint32_t encryptionConstant() const { return readU32(0x00); }

    uint16_t speciesInternal() const {
        if (isGbFile(gameType_)) return data[0]; // Gen1: internal index, Gen2: ndex directly
        return readU16(ofs().speciesInternal);
    }

    // National species ID (converted via SpeciesConverter)
    uint16_t species() const;

    uint16_t heldItem() const {
        if (isGen1File(gameType_)) return 0; // byte 7 is catch rate, no items
        if (isGen2File(gameType_)) return data[1]; // Gen2 held item byte (raw Gen2 id)
        return readU16(ofs().heldItem);
    }

    uint32_t pid() const {
        if (isGbFile(gameType_)) return 0; // no PID/personality value
        return readU32(ofs().pid);
    }

    // Nature: byte read for modern, pid % 25 for PK3, 0 (Hardy) for Gen1
    uint8_t nature() const {
        if (isGbFile(gameType_)) return 0;
        int o = ofs().nature;
        return o >= 0 ? data[o] : static_cast<uint8_t>(pid() % 25);
    }

    // FatefulEncounter: bit in byte for modern, bit 31 of u32@0x4C for PK3
    bool fatefulEncounter() const {
        if (isGbFile(gameType_)) return false;
        auto& o = ofs();
        if (o.fateful < 0) return (readU32(0x4C) >> 31) & 1;
        return (data[o.fateful] >> o.fatefulBit) & 1;
    }

    // Gender: byte bits for modern, PID-based for PK3 (impl in pokemon.cpp)
    uint8_t gender() const;

    // Form: byte for modern, always 0 for PK3/Gen1
    uint8_t form() const {
        if (isGbFile(gameType_)) return 0;
        auto& o = ofs();
        return o.form >= 0 ? static_cast<uint8_t>(data[o.form] >> o.formShift) : 0;
    }

    // Ball: byte for modern, bits 11-14 of u16@0x46 for PK3, none on GB
    // (0 = no icon drawn, see ui_render ball display).
    uint8_t ball() const {
        if (isGbFile(gameType_)) return 0;
        if (isGen4File(gameType_)) return std::max(data[0x83], data[0x86]); // DPPT vs HGSS byte
        int o = ofs().ball;
        return o >= 0 ? data[o] : static_cast<uint8_t>((readU16(0x46) >> 11) & 0xF);
    }

    // Ability: u16 for modern, u8 for PB7, 0 for PK3/Gen1
    uint16_t ability() const {
        if (isGbFile(gameType_)) return 0;
        auto& o = ofs();
        if (o.ability < 0) return 0;
        return o.abilityIsU8 ? static_cast<uint16_t>(data[o.ability]) : readU16(o.ability);
    }

    // EVs (u8 API cannot hold GB stat-exp u16: report 0, documented)
    uint8_t evHp()  const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 0]; }
    uint8_t evAtk() const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 1]; }
    uint8_t evDef() const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 2]; }
    uint8_t evSpe() const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 3]; }
    uint8_t evSpA() const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 4]; }
    uint8_t evSpD() const { return isGbFile(gameType_) ? 0 : data[ofs().evBase + 5]; }

    // TID/SID (GB TIDs are big-endian; no SID on GB)
    uint16_t tid() const {
        if (isGen1File(gameType_))
            return static_cast<uint16_t>((data[0x0C] << 8) | data[0x0D]);
        if (isGen2File(gameType_))
            return static_cast<uint16_t>((data[0x06] << 8) | data[0x07]);
        return readU16(ofs().tid);
    }
    uint16_t sid() const {
        if (isGbFile(gameType_)) return 0;
        return readU16(ofs().sid);
    }

    // Display TID/SID: Gen7+ uses 6-digit/4-digit format, Gen3/Gen4/Gen5/GB raw 16-bit
    uint32_t displayTid() const {
        if (isFRLG(gameType_) || isImportedFile(gameType_) || isGbFile(gameType_) || isGen45File(gameType_)) return tid();
        uint32_t combined = (static_cast<uint32_t>(sid()) << 16) | tid();
        return combined % 1000000;
    }
    uint32_t displaySid() const {
        if (isFRLG(gameType_) || isImportedFile(gameType_) || isGbFile(gameType_) || isGen45File(gameType_)) return sid();
        uint32_t combined = (static_cast<uint32_t>(sid()) << 16) | tid();
        return combined / 1000000;
    }

    // --- String fields (impl in pokemon.cpp) ---

    std::string nickname() const;
    std::string otName() const;
    std::string htName() const;

    // True if this format stores a handling-trainer block (GB/Gen3: no).
    bool hasHandlingTrainer() const {
        if (isGbFile(gameType_)) return false;
        return ofs().htName >= 0;
    }

    // Moves (Gen1 u8 @0x08, Gen2 u8 @0x02, modern u16; no PP data here)
    uint16_t move1() const {
        if (isGen1File(gameType_)) return data[0x08];
        if (isGen2File(gameType_)) return data[0x02];
        return readU16(ofs().moveBase + 0);
    }
    uint16_t move2() const {
        if (isGen1File(gameType_)) return data[0x09];
        if (isGen2File(gameType_)) return data[0x03];
        return readU16(ofs().moveBase + 2);
    }
    uint16_t move3() const {
        if (isGen1File(gameType_)) return data[0x0A];
        if (isGen2File(gameType_)) return data[0x04];
        return readU16(ofs().moveBase + 4);
    }
    uint16_t move4() const {
        if (isGen1File(gameType_)) return data[0x0B];
        if (isGen2File(gameType_)) return data[0x05];
        return readU16(ofs().moveBase + 6);
    }

    // IV32: bit-packed IV word (Gen1: none — DVs exposed via iv* below)
    uint32_t iv32() const {
        if (isGbFile(gameType_)) return 0;
        return readU32(ofs().iv32);
    }
    bool isEgg() const {
        if (isGbFile(gameType_)) return false; // Gen1: no eggs; Gen2 eggs TBD (verify on fixture)
        return ((iv32() >> 30) & 1) == 1;
    }
    bool isNicknamed() const {
        if (isFRLG(gameType_) || isImportedFile(gameType_)) return true;
        if (isGbFile(gameType_)) return gbIsNicknamed();
        return ((iv32() >> 31) & 1) == 1;
    }
    // GB nickname vs species name (exact match = not nicknamed). Outlined:
    // needs SpeciesName (species_converter.h), unavailable in this header.
    bool gbIsNicknamed() const;

    // IVs (from iv32 bit-packed — same layout for all formats).
    // Gen1: mapped from 0-15 DVs (SpA/SpD share Special).
    int gen1DvAtk() const { return (data[0x1B] >> 4) & 0xF; }
    int gen1DvDef() const { return data[0x1B] & 0xF; }
    int gen1DvSpe() const { return (data[0x1C] >> 4) & 0xF; }
    int gen1DvSpc() const { return data[0x1C] & 0xF; }
    // Gen2: same 0-15 DV split, different offsets ([0x15-0x16] BE).
    int gen2DvAtk() const { return (data[0x15] >> 4) & 0xF; }
    int gen2DvDef() const { return data[0x15] & 0xF; }
    int gen2DvSpe() const { return (data[0x16] >> 4) & 0xF; }
    int gen2DvSpc() const { return data[0x16] & 0xF; }
    int gen2DvHp()  const {
        return ((gen2DvAtk() & 1) << 3) | ((gen2DvDef() & 1) << 2) |
               ((gen2DvSpe() & 1) << 1) | (gen2DvSpc() & 1);
    }
    int gen1DvHp()  const {
        return ((gen1DvAtk() & 1) << 3) | ((gen1DvDef() & 1) << 2) |
               ((gen1DvSpe() & 1) << 1) | (gen1DvSpc() & 1);
    }
    int ivHp()  const {
        if (isGen1File(gameType_)) return gen1DvHp();
        if (isGen2File(gameType_)) return gen2DvHp();
        return (iv32() >>  0) & 0x1F;
    }
    int ivAtk() const {
        if (isGen1File(gameType_)) return gen1DvAtk();
        if (isGen2File(gameType_)) return gen2DvAtk();
        return (iv32() >>  5) & 0x1F;
    }
    int ivDef() const {
        if (isGen1File(gameType_)) return gen1DvDef();
        if (isGen2File(gameType_)) return gen2DvDef();
        return (iv32() >> 10) & 0x1F;
    }
    int ivSpe() const {
        if (isGen1File(gameType_)) return gen1DvSpe();
        if (isGen2File(gameType_)) return gen2DvSpe();
        return (iv32() >> 15) & 0x1F;
    }
    int ivSpA() const {
        if (isGen1File(gameType_)) return gen1DvSpc();
        if (isGen2File(gameType_)) return gen2DvSpc();
        return (iv32() >> 20) & 0x1F;
    }
    int ivSpD() const {
        if (isGen1File(gameType_)) return gen1DvSpc();
        if (isGen2File(gameType_)) return gen2DvSpc();
        return (iv32() >> 25) & 0x1F;
    }

    // Level (impl in pokemon.cpp — byte read or computed from EXP)
    uint8_t level() const;

    // --- Utility ---

    bool isEmpty() const {
        if (isGbFile(gameType_)) return data[0] == 0; // no EC field
        return encryptionConstant() == 0 && speciesInternal() == 0;
    }

    std::string displayName() const;

    void refreshChecksum();
    void loadFromEncrypted(const uint8_t* encrypted, size_t len);
    void getEncrypted(uint8_t* outBuf);

    // Language: byte at format-specific offset
    uint8_t language() const {
        int o = ofs().languageByte;
        return (o >= 0) ? data[o] : 0;
    }

    // FormArgument: u32 (Alcremie decoration in low byte)
    uint32_t formArgument() const {
        int o = ofs().formArgument;
        return (o >= 0) ? readU32(o) : 0;
    }

    // CanGigantamax: PK8 byte 0x16 bit 4
    bool canGigantamax() const {
        int o = ofs().canGmaxByte;
        return (o >= 0) && (data[o] & 0x10) != 0;
    }

    // Height/Weight scalars (LGPE size tracking, u8 0-255)
    uint8_t heightScalar() const {
        int o = ofs().heightScalar;
        return (o >= 0) ? data[o] : 0;
    }
    uint8_t weightScalar() const {
        int o = ofs().weightScalar;
        return (o >= 0) ? data[o] : 0;
    }

    // IsAlpha: PA9 → 0x23 != 0, PA8 → 0x16 bit 5, others → false
    bool isAlpha() const {
        auto& o = ofs();
        if (o.alphaByte < 0) return false;
        return o.alphaIsNonZero ? (data[o.alphaByte] != 0)
                                : ((data[o.alphaByte] >> 5) & 1);
    }

    // Ribbon/mark info
    struct RibbonInfo {
        const char* name;     // Display name
        const char* filename; // romfs filename (without path/extension)
        bool isMark;
    };

    // Returns list of all set ribbons/marks
    std::vector<RibbonInfo> getRibbonsAndMarks() const;

    // Shiny: XOR == 0 for Gen3, XOR < 16 for modern, DV rule on GB.
    // Gen6 XY is EC-based (>>4, same data) instead of PID-based.
    // (Bank/VC rule: Atk DV in {2,3,6,7,10,11,14,15}, Def/Spe/Spc == 10).
    bool isShiny() const {
        if (isGen2File(gameType_)) return Gen2::recIsShiny(data.data());
        if (isGen1File(gameType_)) {
            if (gen1DvDef() != 10 || gen1DvSpe() != 10 || gen1DvSpc() != 10)
                return false;
            int a = gen1DvAtk();
            return a == 2 || a == 3 || a == 6 || a == 7 ||
                   a == 10 || a == 11 || a == 14 || a == 15;
        }
        uint32_t p = pid();
        uint16_t t = tid();
        uint16_t s = sid();
        if (isGen6XY(gameType_)) {
            uint32_t ec = readU32(0x00);
            return ((((ec >> 16) ^ (ec & 0xFFFF) ^ t ^ s) >> 4) == 0);
        }
        uint32_t xor_val = (p >> 16) ^ (p & 0xFFFF) ^ t ^ s;
        if (isFRLG(gameType_) || isImportedFile(gameType_)) return xor_val == 0;
        return xor_val < 16;
    }
};

// Decode a Gen3-encoded string (PKHeX StringConverter3) to UTF-8.
// Public for save_file.cpp (GBA save OT names); implemented in pokemon.cpp.
std::string decodeGen3String(const uint8_t* base, int offset, int maxBytes, bool jp = false);
// Used after a cross-gen conversion materializes the 33B record: the record
// alone carries no names, so they travel explicitly (OT stays the original
// trainer, like a real transfer). Unknown chars become '?', padded 0x50.
inline void fillGen1Names(Pokemon& pkm, const std::string& ot, const std::string& nick) {
    Gen1::encodeGbString(ot.c_str(), pkm.data.data() + 33, 11);
    Gen1::encodeGbString(nick.c_str(), pkm.data.data() + 44, 11);
}

// Same for Gen2 (32B record + OT/nick, 54B stride).
inline void fillGen2Names(Pokemon& pkm, const std::string& ot, const std::string& nick) {
    Gen1::encodeGbString(ot.c_str(), pkm.data.data() + 32, 11);
    Gen1::encodeGbString(nick.c_str(), pkm.data.data() + 43, 11);
}
