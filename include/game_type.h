#pragma once
#include <cstdint>

// Supported game types (sequential enum used as array index)
enum class GameType { ZA, S, V, Sw, Sh, BD, SP, LA, GP, GE, FR, LG, FR_ES, LG_ES, FR_DE, LG_DE, FR_IT, LG_IT, FR_FR, LG_FR, FR_JA, LG_JA, RUBY, SAPPHIRE, EMERALD, RED, BLUE, YELLOW, GOLD, SILVER, CRYSTAL };
static constexpr int GAME_TYPE_COUNT = 31;

inline bool isSV(GameType g) { return g == GameType::S || g == GameType::V; }
inline bool isSwSh(GameType g) { return g == GameType::Sw || g == GameType::Sh; }
inline bool isBDSP(GameType g) { return g == GameType::BD || g == GameType::SP; }
inline bool isLGPE(GameType g) { return g == GameType::GP || g == GameType::GE; }
inline bool isFRLG_JA(GameType g) { return g == GameType::FR_JA || g == GameType::LG_JA; }
inline bool isFRLG(GameType g) {
    return g == GameType::FR    || g == GameType::LG    ||
           g == GameType::FR_ES || g == GameType::LG_ES ||
           g == GameType::FR_DE || g == GameType::LG_DE ||
           g == GameType::FR_IT || g == GameType::LG_IT ||
           g == GameType::FR_FR || g == GameType::LG_FR ||
           g == GameType::FR_JA || g == GameType::LG_JA;
}

// File-backed games with no Switch titleId: found on SD/USB by scanning
// configured import paths (see import_paths.h, import_scan.h) rather than
// mounted via AccountManager::mountSave(). They share FRLG's exact GBA sector
// container (verified against real ruby/sapphire/emerald.sav fixtures — see
// GEN_PLAN.md Fase 5), so SaveFile::load()/save() route them through the same
// loadGBA()/saveGBA() as FRLG; only the titleId-bound paths (icon fetch from
// NS, AccountManager mount/backup) need to treat them differently.
inline bool isImportedFile(GameType g) {
    return g == GameType::RUBY || g == GameType::SAPPHIRE || g == GameType::EMERALD;
}

// File-backed Gen 1 games (R/B/Y SRAM dumps found by import scan, G1c).
// Same "no titleId" shape as isImportedFile, but a different container
// (GB 32KB SRAM + PokeList1, not GBA sectors) so they need their own branch.
inline bool isGen1File(GameType g) {
    return g == GameType::RED || g == GameType::BLUE || g == GameType::YELLOW;
}

// File-backed Gen 2 games (G/S/C SRAM dumps, G2c). Same shape as Gen 1,
// different container again (PokeList2 + dual checksums + box names).
inline bool isGen2File(GameType g) {
    return g == GameType::GOLD || g == GameType::SILVER || g == GameType::CRYSTAL;
}

// Either GB generation (shared record traits: no PID/IV32/crypto/eggs/HT,
// GB text codec, BE multibyte fields).
inline bool isGbFile(GameType g) {
    return isGen1File(g) || isGen2File(g);
}

// Per-game constant table. One entry per GameType enum value.
struct GameInfo {
    uint64_t    titleId;
    const char* saveFileName;
    const char* displayName;
    const char* bankGroupName;
    const char* bankFolderName;
    const char* gamePathName;
    const char* pkExtension;
    int         pkPartySize;
    int         boxCount;
    int         slotsPerBox;
    int         saveSlotSize;   // slot size in save file (stored + gap)
    int         saveGapSize;    // gap bytes at end of each save slot
    int         bankSlotSize;   // slot size in bank file (decrypted)
    bool        hasWondercards;
    bool        hasAlphaForms;
    const char* wcExtensionHint;
    const char* gameTag;        // short tag for export filenames
};

// Lookup by GameType (direct array index).
inline const GameInfo& gameInfo(GameType g) {
    static constexpr GameInfo INFO[GAME_TYPE_COUNT] = {
        // ZA
        {0x0100F43008C44000, "main",            "Pokemon Legends: Z-A",          "Legends: Z-A",
         "LegendsZA",        "LegendsZA",        "pa9", 0x158, 32, 30, 0x198, 0x40, 0x158,
         true, true, ".wa9", "ZA"},
        // S
        {0x0100A3D008C5C000, "main",            "Pokemon Scarlet",               "Scarlet / Violet",
         "ScarletViolet",    "Scarlet",           "pk9", 0x158, 32, 30, 0x158, 0, 0x158,
         true, false, ".wc9", "SV"},
        // V
        {0x01008F6008C5E000, "main",            "Pokemon Violet",                "Scarlet / Violet",
         "ScarletViolet",    "Violet",            "pk9", 0x158, 32, 30, 0x158, 0, 0x158,
         true, false, ".wc9", "SV"},
        // Sw
        {0x0100ABF008968000, "main",            "Pokemon Sword",                 "Sword / Shield",
         "SwordShield",      "Sword",             "pk8", 0x158, 32, 30, 0x158, 0, 0x158,
         true, false, ".wc8", "SwSh"},
        // Sh
        {0x01008DB008C2C000, "main",            "Pokemon Shield",                "Sword / Shield",
         "SwordShield",      "Shield",            "pk8", 0x158, 32, 30, 0x158, 0, 0x158,
         true, false, ".wc8", "SwSh"},
        // BD
        {0x0100000011D90000, "SaveData.bin",    "Pokemon Brilliant Diamond",     "Brilliant Diamond / Shining Pearl",
         "BDSP",             "BrilliantDiamond",  "pb8", 0x158, 40, 30, 0x158, 0, 0x158,
         true, false, ".wb8", "BDSP"},
        // SP
        {0x010018E011D92000, "SaveData.bin",    "Pokemon Shining Pearl",         "Brilliant Diamond / Shining Pearl",
         "BDSP",             "ShiningPearl",      "pb8", 0x158, 40, 30, 0x158, 0, 0x158,
         true, false, ".wb8", "BDSP"},
        // LA
        {0x01001F5010DFA000, "main",            "Pokemon Legends: Arceus",       "Legends: Arceus",
         "LegendsArceus",    "LegendsArceus",     "pa8", 0x178, 32, 30, 0x168, 0, 0x178,
         true, true, ".wa8", "LA"},
        // GP
        {0x010003F003A34000, "savedata.bin",    "Pokemon Let's Go Pikachu",      "Let's Go Pikachu / Let's Go Eevee",
         "LetsGo",           "LetsGoPikachu",     "pb7", 0x104, 40, 25, 0x104, 0, 0x104,
         true, false, ".wb7/.wb7full", "LGPE"},
        // GE
        {0x0100187003A36000, "savedata.bin",    "Pokemon Let's Go Eevee",        "Let's Go Pikachu / Let's Go Eevee",
         "LetsGo",           "LetsGoEevee",       "pb7", 0x104, 40, 25, 0x104, 0, 0x104,
         true, false, ".wb7/.wb7full", "LGPE"},
        // FR
        {0x0100554023408000, "FireRed_e.sav",   "Pokemon FireRed",               "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed",           "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG
        {0x010034D02340E000, "LeafGreen_e.sav", "Pokemon LeafGreen",             "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen",         "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // FR_ES
        {0x0100EB702342C000, "FireRed_s.sav",   "Pokemon FireRed (ES)",          "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed_ES",        "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG_ES
        {0x01002B5023434000, "LeafGreen_s.sav",  "Pokemon LeafGreen (ES)",        "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen_ES",      "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // FR_DE
        {0x01007F8023416000, "FireRed_d.sav",    "Pokemon FireRed (DE)",          "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed_DE",        "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG_DE
        {0x0100FD6023430000, "LeafGreen_d.sav",  "Pokemon LeafGreen (DE)",        "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen_DE",      "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // FR_IT
        {0x010092302342A000, "FireRed_i.sav",    "Pokemon FireRed (IT)",          "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed_IT",        "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG_IT
        {0x01005C7023432000, "LeafGreen_i.sav",  "Pokemon LeafGreen (IT)",        "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen_IT",      "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // FR_FR
        {0x01004B3023412000, "FireRed_f.sav",    "Pokemon FireRed (FR)",          "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed_FR",        "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG_FR
        {0x010087C02342E000, "LeafGreen_f.sav",  "Pokemon LeafGreen (FR)",        "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen_FR",      "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // FR_JA
        {0x01006FA0233F8000, "FireRed_j.sav",    "Pokemon FireRed (JA)",          "FireRed / LeafGreen",
         "FireRedLeafGreen", "FireRed_JA",        "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // LG_JA
        {0x0100F1E0233FA000, "LeafGreen_j.sav",  "Pokemon LeafGreen (JA)",        "FireRed / LeafGreen",
         "FireRedLeafGreen", "LeafGreen_JA",      "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "FRLG"},
        // RUBY (imported file, no titleId — sentinel below is never a real
        // Nintendo titleId, which always sits above 0x0100000000010000)
        {0x1,                "",                 "Pokemon Ruby",                  "Pokemon Ruby",
         "Ruby",             "Ruby",              "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "Ruby"},
        // SAPPHIRE
        {0x2,                "",                 "Pokemon Sapphire",              "Pokemon Sapphire",
         "Sapphire",         "Sapphire",          "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "Sapphire"},
        // EMERALD
        {0x3,                "",                 "Pokemon Emerald",                "Pokemon Emerald",
         "Emerald",          "Emerald",           "pk3", 100,   14, 30, 80,    0, 80,
         false, false, "", "Emerald"},
        // RED (Gen 1 SRAM dump, no titleId — sentinels continue past EMERALD's)
        {0x4,                "",                 "Pokemon Red",                    "Pokemon Red",
         "Red",              "Red",               "pk1", 33,    12, 20, 55,    0, 55,
         false, false, "", "Red"},
        // BLUE
        {0x5,                "",                 "Pokemon Blue",                   "Pokemon Blue",
         "Blue",             "Blue",              "pk1", 33,    12, 20, 55,    0, 55,
         false, false, "", "Blue"},
        // YELLOW
        {0x6,                "",                 "Pokemon Yellow",                 "Pokemon Yellow",
         "Yellow",           "Yellow",            "pk1", 33,    12, 20, 55,    0, 55,
         false, false, "", "Yellow"},
        // GOLD (Gen 2 SRAM dump, sentinels continue; 14 boxes x 20, 54B slots)
        {0x7,                "",                 "Pokemon Gold",                   "Pokemon Gold",
         "Gold",             "Gold",              "pk2", 32,    14, 20, 54,    0, 54,
         false, false, "", "Gold"},
        // SILVER
        {0x8,                "",                 "Pokemon Silver",                 "Pokemon Silver",
         "Silver",           "Silver",            "pk2", 32,    14, 20, 54,    0, 54,
         false, false, "", "Silver"},
        // CRYSTAL
        {0x9,                "",                 "Pokemon Crystal",                "Pokemon Crystal",
         "Crystal",          "Crystal",           "pk2", 32,    14, 20, 54,    0, 54,
         false, false, "", "Crystal"},
    };
    return INFO[static_cast<int>(g)];
}

// Returns the paired game (same bank folder), or the game itself if unpaired
inline GameType pairedGame(GameType g) {
    switch (g) {
        case GameType::S:  return GameType::V;
        case GameType::V:  return GameType::S;
        case GameType::Sw: return GameType::Sh;
        case GameType::Sh: return GameType::Sw;
        case GameType::BD: return GameType::SP;
        case GameType::SP: return GameType::BD;
        case GameType::GP: return GameType::GE;
        case GameType::GE: return GameType::GP;
        case GameType::FR: return GameType::LG;
        case GameType::LG: return GameType::FR;
        case GameType::FR_ES: return GameType::LG_ES;
        case GameType::LG_ES: return GameType::FR_ES;
        case GameType::FR_DE: return GameType::LG_DE;
        case GameType::LG_DE: return GameType::FR_DE;
        case GameType::FR_IT: return GameType::LG_IT;
        case GameType::LG_IT: return GameType::FR_IT;
        case GameType::FR_FR: return GameType::LG_FR;
        case GameType::LG_FR: return GameType::FR_FR;
        case GameType::FR_JA: return GameType::LG_JA;
        case GameType::LG_JA: return GameType::FR_JA;
        default: return g;
    }
}

// Convenience accessors (thin wrappers so existing callers don't change)
inline uint64_t    titleIdOf(GameType g)        { return gameInfo(g).titleId; }
inline const char* saveFileNameOf(GameType g)   { return gameInfo(g).saveFileName; }
inline const char* gameDisplayNameOf(GameType g){ return gameInfo(g).displayName; }
inline const char* bankGroupNameOf(GameType g)  { return gameInfo(g).bankGroupName; }
inline const char* bankFolderNameOf(GameType g) { return gameInfo(g).bankFolderName; }
inline const char* gamePathNameOf(GameType g)   { return gameInfo(g).gamePathName; }
inline const char* pkFileExtension(GameType g)  { return gameInfo(g).pkExtension; }
inline int         pkPartySize(GameType g)      { return gameInfo(g).pkPartySize; }

// National generation of a game's Pokemon format.
inline int genOf(GameType g) {
    if (isFRLG(g) || isImportedFile(g)) return 3;
    if (isLGPE(g)) return 7;
    if (isSV(g) || g == GameType::ZA) return 9;
    return 8; // SwSh, BDSP, Legends Arceus
}

// Target generation to hand to openhome_transfer_pkm() when converting a
// Pokemon INTO this game, or 0 if the OH engine cannot materialize this
// game's stored format.
//
// genOf() alone is not enough: gen 8 has three incompatible layouts
// (PK8 SwSh / PB8 BDSP / PA8 Legends Arceus) and gen 9 has SV and Z-A, while
// openhome_transfer_pkm only ever produces Pk7 / Pk8 / Pk9. Returning the bare
// generation would let a SwSh-shaped Pk8 be written into a BDSP or Arceus slot.
// Only SwSh (Pk8) and SV (Pk9) are claimed here; everything else returns 0 so
// the caller fails explicitly instead of writing a wrong-format Pokemon.
// Gen 7 is never a valid destination: the engine emits Pk7 (Alola SM/USUM),
// and this app has no SM/USUM GameType (GP/GE are LGPE, a different layout).
inline int ohTargetGenFor(GameType g) {
    if (isGen1File(g)) return 1;  // Pk1 (R/B/Y)
    if (isGen2File(g)) return 2;  // Pk2 (G/S/C)
    if (isSwSh(g)) return 8;
    if (isSV(g))   return 9;
    if (isFRLG(g) || isImportedFile(g)) return 3;
    if (g == GameType::LA) return 10; // PA8 (Legends: Arceus)
    if (g == GameType::ZA) return 11; // PA9 (Legends: Z-A)
    if (isBDSP(g))         return 12; // PB8 (BDSP)
    if (isLGPE(g))         return 13; // PB7 (Let's Go)
    return 0;
}

// Generation to hand to openhome_load_pkm_from_gen() when reading THIS game's
// stored Pokemon as the SOURCE of a cross-gen transfer, or 0 if the OH engine
// cannot parse this game's record. Mirrors ohTargetGenFor: only Pk8 (SwSh),
// Pk9 (SV) and Pk3 (FRLG) are real. BDSP / Legends Arceus / LGPE / Z-A store
// PB8 / PA8 / PB7 / PA9, which openhome_transfer_pkm never consumes — genOf()
// would wrongly report 8/9/7 for them and the bytes would be mis-parsed.
inline int ohSourceGenFor(GameType g) {
    if (isGen1File(g)) return 1;  // Pk1 (R/B/Y)
    if (isGen2File(g)) return 2;  // Pk2 (G/S/C)
    if (isSwSh(g)) return 8;
    if (isSV(g))   return 9;
    if (isFRLG(g) || isImportedFile(g)) return 3;
    if (g == GameType::LA) return 10; // PA8 (Legends: Arceus)
    if (g == GameType::ZA) return 11; // PA9 (Legends: Z-A)
    if (isBDSP(g))         return 12; // PB8 (BDSP)
    if (isLGPE(g))         return 13; // PB7 (Let's Go)
    return 0;
}

// Leading bytes of Pokemon::data that make up the stored record for the given
// OH generation (the rest of the 376-byte array is padding). Pokemon::data is
// laid out largest-format-first, so a prefix is the exact record.
inline int ohRecordBytesFor(int gen) {
    switch (gen) {
        case 1:  return 33;   // Pk1 (box record; party 44 = box33 + level + stats)
        case 2:  return 32;   // Pk2 (box record; party 73)
        case 3:  return 80;   // Pk3
        case 8:  return 344;  // Pk8  (box == party)
        case 9:  return 344;  // Pk9  (Rust core: box == party == 344)
        case 10: return 360;  // Pa8  (box; PKHeX party record is 376)
        case 11: return 344;  // Pa9  (shares the Pk9 record: box == party == 344)
        case 12: return 344;  // Pb8  (PK8-shaped: box == party == 344)
        case 13: return 260;  // Pb7  (LGPE: box == party == 260)
        default: return 0;
    }
}

// Level-up learnset table for the move viewer (mirrors the Rust
// learnset_table() ids in openhome_switch). 0 = none.
inline int learnsetTableFor(GameType g) {
    if (g == GameType::RED || g == GameType::BLUE) return 1;   // RB
    if (g == GameType::YELLOW) return 2;                        // Y
    if (g == GameType::GOLD || g == GameType::SILVER) return 3; // GS
    if (g == GameType::CRYSTAL) return 4;                       // C
    if (g == GameType::RUBY || g == GameType::SAPPHIRE) return 5; // RS
    if (g == GameType::EMERALD) return 6;                       // E
    if (isFRLG(g)) return 7;                                    // FR
    if (isLGPE(g)) return 8;                                    // GG
    if (isSwSh(g)) return 9;                                    // SWSH
    if (isBDSP(g)) return 10;                                   // BDSP
    if (g == GameType::LA) return 11;                           // LA
    if (isSV(g)) return 12;                                     // SV
    if (g == GameType::ZA) return 13;                           // ZA
    return 0;
}

// Generation id for openhome_*_from_gen/transfer matching an OriginalBackup
// tag, or 0 if unknown. Inverse of ohBackupTagForGen (same numbering).
inline int ohGenForBackupTag(int tag) {
    switch (tag) {
        case 1:  return 1;   // Pk1
        case 2:  return 2;   // Pk2
        case 3:  return 3;   // Pk3
        case 7:  return 7;   // Pk7
        case 8:  return 13;  // Pb7
        case 9:  return 8;   // Pk8
        case 10: return 10;  // Pa8
        case 11: return 12;  // Pb8
        case 12: return 9;   // Pk9
        case 13: return 11;  // Pa9
        default: return 0;
    }
}

// pkm_rs::ohpkm::v2_sections::pkm_bytes::Tag id for the given OH generation, as
// it appears in the 2-byte LE prefix of an OriginalBackup blob. 0 if the gen
// has no single tag here.
inline int ohBackupTagForGen(int gen) {
    switch (gen) {
        case 1:  return 1;   // Tag::Pk1
        case 2:  return 2;   // Tag::Pk2
        case 3:  return 3;   // Tag::Pk3
        case 8:  return 9;   // Tag::Pk8
        case 9:  return 12;  // Tag::Pk9
        case 10: return 10;  // Tag::Pa8
        case 11: return 13;  // Tag::Pa9
        case 12: return 11;  // Tag::Pb8
        case 13: return 8;   // Tag::Pb7
        default: return 0;
    }
}
