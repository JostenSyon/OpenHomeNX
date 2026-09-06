#pragma once
// Gen 1 (R/B/Y) record tables — GENERATED from
// rust/pkm_rs/src/conversion/gen1_pokemon_index.rs and
// rust/pkm_rs/src/conversion/gameboy_string_encoding.rs
// (the same sources OpenHome's pkm_rs uses). Do not hand-edit.
//
// Box record layout (33B): [0]=species (internal index), [1-2]=HP LE,
// [3]=level, [4]=status, [5-6]=types, [7]=catch rate, [8-11]=moves,
// [12-13]=TID LE, [14-16]=EXP BE, [17-26]=stat exp LE, [27-28]=DVs,
// [29-32]=PP (low 6 bits) + PP Up (high 2 bits). Party record 44B =
// box33 + nickname11 (OT name lives separately in the list).
#include <cstdint>
#include <cstddef>
#include <string>

namespace Gen1 {

// Internal index -> national dex (0 = unmapped; mirrors unwrap_or(0)).
inline constexpr uint8_t kInternalToNdex[256] = {
      0, 112, 115,  32,  35,  21, 100,  34,  80,   2, 103, 108, 102,  88,  94,  29,
     31, 104, 111, 131,  59, 151, 130,  90,  72,  92, 123, 120,   9, 127, 114,   0,
      0,  58,  95,  22,  16,  79,  64,  75, 113,  67, 122, 106, 107,  24,  47,  54,
     96,  76,   0, 126,   0, 125,  82, 109,   0,  56,  86,  50, 128,   0,   0,   0,
     83,  48, 149,   0,   0,   0,  84,  60, 124, 146, 144, 145, 132,  52,  98,   0,
      0,   0,  37,  38,  25,  26,   0,   0, 147, 148, 140, 141, 116, 117,   0,   0,
     27,  28, 138, 139,  39,  40, 133, 136, 135, 134,  66,  41,  23,  46,  61,  62,
     13,  14,  15,   0,  85,  57,  51,  49,  87,   0,   0,  10,  11,  12,  68,   0,
     55,  97,  42, 150, 143, 129,   0,   0,  89,   0,  99,  91,   0, 101,  36, 110,
     53, 105,   0,  93,  63,  65,  17,  18, 121,   1,   3,  73,   0, 118, 119,   0,
      0,   0,   0,  77,  78,  19,  20,  33,  30,  74, 137, 142,   0,  81,   0,   0,
      4,   7,   5,   8,   6,   0,   0,   0,   0,  43,  44,  45,  69,  70,  71,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// National dex (0..151) -> internal index (0 = unmapped).
inline constexpr uint8_t kNdexToInternal[152] = {
      0, 153,   9, 154, 176, 178, 180, 177, 179,  28, 123, 124, 125, 112, 113, 114,
     36, 150, 151, 165, 166,   5,  35, 108,  45,  84,  85,  96,  97,  15, 168,  16,
      3, 167,   7,   4, 142,  82,  83, 100, 101, 107, 130, 185, 186, 187, 109,  46,
     65, 119,  59, 118,  77, 144,  47, 128,  57, 117,  33,  20,  71, 110, 111, 148,
     38, 149, 106,  41, 126, 188, 189, 190,  24, 155, 169,  39,  49, 163, 164,  37,
      8, 173,  54,  64,  70, 116,  58, 120,  13, 136,  23, 139,  25, 147,  14,  34,
     48, 129,  78, 138,   6, 141,  12,  10,  17, 145,  43,  44,  11,  55, 143,  18,
      1,  40,  30,   2,  92,  93, 157, 158,  27, 152,  42,  26,  72,  53,  51,  29,
     60, 133,  22,  19,  76, 102, 105, 104, 103, 170,  98,  99,  90,  91, 171, 132,
     74,  75,  73,  88,  89,  66, 131,  21
};

// GB text byte -> UTF-8 (nullptr = unmapped). 0x50 is the terminator.
// NOTE: byte 242 duplicates 232 ('.'), same as the Rust source.
inline constexpr const char* kGbCharUtf8[256] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, " ",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
    "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "(", ")", ":", ";", "[", "]",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "\xe0", "\xe8", "\xe9", "\xf9", "\xc0", "\xc1",
    "\xc4", "\xd6", "\xdc", "\xe4", "\xf6", "\xfc", "\xc8", "\xc9", "\xcc", "\xcd", "\xd1", "\xd2", "\xd3", "\xd9", "\xda", "\xe1",
    "\xec", "\xed", "\xf1", "\xf2", "\xf3", "\xfa", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "\u2019", "\u1d18", "\u1d0d", "-", nullptr, nullptr, "?", "!", ".", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "\u2642",
    nullptr, "\xd7", ".", "/", ",", "\u2640", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

inline uint8_t internalToNdex(uint8_t i) { return kInternalToNdex[i]; }
inline uint8_t ndexToInternal(uint8_t n) { return n < 152 ? kNdexToInternal[n] : 0; }
inline uint8_t recSpecies(const uint8_t* rec) { return kInternalToNdex[rec[0]]; }
inline uint8_t recLevel(const uint8_t* rec) { return rec[3]; }
inline uint8_t recMove(const uint8_t* rec, int slot) { return rec[8 + slot]; }
inline uint16_t recTid(const uint8_t* rec) {
    return static_cast<uint16_t>(rec[0xC] | (rec[0xD] << 8));
}
inline uint32_t recExp(const uint8_t* rec) {
    return (static_cast<uint32_t>(rec[0xE]) << 16) | (static_cast<uint32_t>(rec[0xF]) << 8) | rec[0x10];
}

// Decode a 0x50-terminated GB string (at most maxLen bytes) to UTF-8.
inline std::string decodeGbString(const uint8_t* s, size_t maxLen) {
    std::string out;
    for (size_t i = 0; i < maxLen; i++) {
        if (s[i] == 0x50) break;
        const char* u = kGbCharUtf8[s[i]];
        if (u) out += u;
    }
    return out;
}

// Encode UTF-8 into GB bytes, 0x50-padded to maxLen. Returns bytes written
// (always maxLen). Unknown chars become '?'.
inline uint8_t encodeGbChar(uint32_t cp) {
    switch (cp) {
        case 0x20: return 127; // ' '
        case 0x21: return 231; // '!'
        case 0x28: return 154; // '('
        case 0x29: return 155; // ')'
        case 0x2C: return 244; // ','
        case 0x2D: return 227; // '-'
        case 0x2E: return 232; // '.'
        case 0x2F: return 243; // '/'
        case 0x30: return 246; // '0'
        case 0x31: return 247; // '1'
        case 0x32: return 248; // '2'
        case 0x33: return 249; // '3'
        case 0x34: return 250; // '4'
        case 0x35: return 251; // '5'
        case 0x36: return 252; // '6'
        case 0x37: return 253; // '7'
        case 0x38: return 254; // '8'
        case 0x39: return 255; // '9'
        case 0x3A: return 156; // ':'
        case 0x3B: return 157; // ';'
        case 0x3F: return 230; // '?'
        case 0x41: return 128; // 'A'
        case 0x42: return 129; // 'B'
        case 0x43: return 130; // 'C'
        case 0x44: return 131; // 'D'
        case 0x45: return 132; // 'E'
        case 0x46: return 133; // 'F'
        case 0x47: return 134; // 'G'
        case 0x48: return 135; // 'H'
        case 0x49: return 136; // 'I'
        case 0x4A: return 137; // 'J'
        case 0x4B: return 138; // 'K'
        case 0x4C: return 139; // 'L'
        case 0x4D: return 140; // 'M'
        case 0x4E: return 141; // 'N'
        case 0x4F: return 142; // 'O'
        case 0x50: return 143; // 'P'
        case 0x51: return 144; // 'Q'
        case 0x52: return 145; // 'R'
        case 0x53: return 146; // 'S'
        case 0x54: return 147; // 'T'
        case 0x55: return 148; // 'U'
        case 0x56: return 149; // 'V'
        case 0x57: return 150; // 'W'
        case 0x58: return 151; // 'X'
        case 0x59: return 152; // 'Y'
        case 0x5A: return 153; // 'Z'
        case 0x5B: return 158; // '['
        case 0x5D: return 159; // ']'
        case 0x61: return 160; // 'a'
        case 0x62: return 161; // 'b'
        case 0x63: return 162; // 'c'
        case 0x64: return 163; // 'd'
        case 0x65: return 164; // 'e'
        case 0x66: return 165; // 'f'
        case 0x67: return 166; // 'g'
        case 0x68: return 167; // 'h'
        case 0x69: return 168; // 'i'
        case 0x6A: return 169; // 'j'
        case 0x6B: return 170; // 'k'
        case 0x6C: return 171; // 'l'
        case 0x6D: return 172; // 'm'
        case 0x6E: return 173; // 'n'
        case 0x6F: return 174; // 'o'
        case 0x70: return 175; // 'p'
        case 0x71: return 176; // 'q'
        case 0x72: return 177; // 'r'
        case 0x73: return 178; // 's'
        case 0x74: return 179; // 't'
        case 0x75: return 180; // 'u'
        case 0x76: return 181; // 'v'
        case 0x77: return 182; // 'w'
        case 0x78: return 183; // 'x'
        case 0x79: return 184; // 'y'
        case 0x7A: return 185; // 'z'
        case 0xC0: return 190; // '\xc0'
        case 0xC1: return 191; // '\xc1'
        case 0xC4: return 192; // '\xc4'
        case 0xC8: return 198; // '\xc8'
        case 0xC9: return 199; // '\xc9'
        case 0xCC: return 200; // '\xcc'
        case 0xCD: return 201; // '\xcd'
        case 0xD1: return 202; // '\xd1'
        case 0xD2: return 203; // '\xd2'
        case 0xD3: return 204; // '\xd3'
        case 0xD6: return 193; // '\xd6'
        case 0xD7: return 241; // '\xd7'
        case 0xD9: return 205; // '\xd9'
        case 0xDA: return 206; // '\xda'
        case 0xDC: return 194; // '\xdc'
        case 0xE0: return 186; // '\xe0'
        case 0xE1: return 207; // '\xe1'
        case 0xE4: return 195; // '\xe4'
        case 0xE8: return 187; // '\xe8'
        case 0xE9: return 188; // '\xe9'
        case 0xEC: return 208; // '\xec'
        case 0xED: return 209; // '\xed'
        case 0xF1: return 210; // '\xf1'
        case 0xF2: return 211; // '\xf2'
        case 0xF3: return 212; // '\xf3'
        case 0xF6: return 196; // '\xf6'
        case 0xF9: return 189; // '\xf9'
        case 0xFA: return 213; // '\xfa'
        case 0xFC: return 197; // '\xfc'
        case 0x1D0D: return 226; // '\u1d0d'
        case 0x1D18: return 225; // '\u1d18'
        case 0x2019: return 224; // '\u2019'
        case 0x2640: return 245; // '\u2640'
        case 0x2642: return 239; // '\u2642'
        default: return 230; // '?'
    }
}
inline size_t encodeGbString(const char* utf8, uint8_t* out, size_t maxLen) {
    size_t w = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p && w < maxLen;) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) { cp = (*p++ & 0x1F) << 6; cp |= (*p++ & 0x3F); }
        else if ((*p & 0xF0) == 0xE0) { cp = (*p++ & 0x0F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
        else { p++; continue; }
        out[w++] = encodeGbChar(cp);
    }
    while (w < maxLen) out[w++] = 0x50;
    return w;
}

// Compile-time anchors (Bulbasaur int 0x99, Mew int 21, Rhydon int 1,
// Pikachu int 0x54): any table corruption breaks the build.
static_assert(kInternalToNdex[153] == 1);
static_assert(kInternalToNdex[21] == 151);
static_assert(kInternalToNdex[1] == 112);
static_assert(kInternalToNdex[84] == 25);
static_assert(kNdexToInternal[1] == 153);
static_assert(kNdexToInternal[151] == 21);
static_assert(kNdexToInternal[25] == 84);

} // namespace Gen1

// Gen 2 (Oro/Argento/Cristallo) record helpers. The GB text codec above is
// shared (same charset). Species needs NO index table: Pk2 stores the
// national dex directly (unlike Gen 1 internal indices).
// Box record layout (32B, all BE, from rust/pkm_rs/src/gen2/pk2.rs):
// [0]=species (ndex), [1]=held item, [2-5]=moves, [6-7]=TID,
// [8-10]=EXP, [11-20]=stat exp, [21-22]=DVs, [23-26]=PP+PPUp,
// [27]=friendship, [28]=pokerus, [29]=met time/level, [30]=OT gender/met
// location, [31]=level. Party record 73B.
namespace Gen2 {

inline uint8_t recSpecies(const uint8_t* rec) { return rec[0]; }
inline uint8_t recLevel(const uint8_t* rec) { return rec[31]; }
inline uint8_t recMove(const uint8_t* rec, int slot) { return rec[2 + slot]; }
inline uint8_t recHeldItem(const uint8_t* rec) { return rec[1]; }
inline uint16_t recTid(const uint8_t* rec) {
    return static_cast<uint16_t>((rec[6] << 8) | rec[7]);
}
inline uint32_t recExp(const uint8_t* rec) {
    return (static_cast<uint32_t>(rec[8]) << 16) | (static_cast<uint32_t>(rec[9]) << 8) | rec[10];
}
// DVs at [21-22] BE, same split as Gen 1 (Gen 2 still has 4 DVs + derived HP).
inline uint16_t recDvsRaw(const uint8_t* rec) {
    return static_cast<uint16_t>((rec[21] << 8) | rec[22]);
}
inline int recDvAtk(const uint8_t* rec) { return (recDvsRaw(rec) >> 12) & 0xF; }
inline int recDvDef(const uint8_t* rec) { return (recDvsRaw(rec) >> 8) & 0xF; }
inline int recDvSpe(const uint8_t* rec) { return (recDvsRaw(rec) >> 4) & 0xF; }
inline int recDvSpc(const uint8_t* rec) { return recDvsRaw(rec) & 0xF; }
inline int recDvHp(const uint8_t* rec) {
    return ((recDvAtk(rec) & 1) << 3) | ((recDvDef(rec) & 1) << 2) |
           ((recDvSpe(rec) & 1) << 1) | (recDvSpc(rec) & 1);
}
// Shiny: same Bank/VC DV rule as Gen 1.
inline bool recIsShiny(const uint8_t* rec) {
    if (recDvDef(rec) != 10 || recDvSpe(rec) != 10 || recDvSpc(rec) != 10)
        return false;
    int a = recDvAtk(rec);
    return a == 2 || a == 3 || a == 6 || a == 7 ||
           a == 10 || a == 11 || a == 14 || a == 15;
}

} // namespace Gen2
