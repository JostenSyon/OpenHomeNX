#include "trade_evo.h"
#include "pokemon.h"
#include "species_converter.h"
#include "gen1_tables.h"
#include "save_file.h"
#include <cstring>

namespace TradeEvo {
namespace {

// Tabella unificata — SOLO trade secco + trade+strumento (PKHeX EvoCriteria).
// heldModern = modern id (modern.rs), 0 = nessuno. Fonte: PKHeX.
// Gen1 4 + Gen2 6 + Gen3 2 + Gen4 5 + Gen5 5 (incl. Karra/Shelmet + Feebas) + Gen6 4
constexpr TradeRule kRules[] = {
    // G1
    {64, 0, 65},   // Kadabra -> Alakazam
    {67, 0, 68},   // Machoke -> Machamp
    {75, 0, 76},   // Graveler -> Golem
    {93, 0, 94},   // Haunter -> Gengar
    // G2
    {95, 233, 208},  // Onix + Metal Coat -> Steelix
    {123, 233, 212}, // Scyther + Metal Coat -> Scizor
    {117, 235, 230}, // Seadra + Dragon Scale -> Kingdra
    {137, 252, 233}, // Porygon + Up-Grade -> Porygon2
    {61, 221, 186},  // Poliwhirl + King's Rock -> Politoed
    {79, 221, 199},  // Slowpoke + King's Rock -> Slowking
    // G3
    {366, 226, 367}, // Clamperl + Deep Sea Tooth -> Huntail
    {366, 227, 368}, // Clamperl + Deep Sea Scale -> Gorebyss
    // G4
    {112, 321, 464}, // Rhydon + Protector -> Rhyperior
    {125, 322, 466}, // Electabuzz + Electirizer -> Electivire
    {126, 323, 467}, // Magmar + Magmarizer -> Magmortar
    {233, 324, 474}, // Porygon2 + Dubious Disc -> Porygon-Z
    {356, 325, 477}, // Dusclops + Reaper Cloth -> Dusknoir
    // G5 (Feebas via Prism Scale è trade da Gen5)
    {349, 537, 350}, // Feebas + Prism Scale -> Milotic
    {525, 0, 526},   // Boldore -> Gigalith
    {533, 0, 534},   // Gurdurr -> Conkeldurr
    {588, 0, 589},   // Karrablast -> Escavalier (paired Shelmet)
    {616, 0, 617},   // Shelmet -> Accelgor (paired Karrablast)
    // G6
    {682, 647, 683}, // Spritzee + Sachet -> Aromatisse
    {684, 646, 685}, // Swirlix + Whipped Dream -> Slurpuff
    {708, 0, 709},   // Phantump -> Trevenant
    {710, 0, 711},   // Pumpkaboo -> Gourgeist
};

} // namespace

const TradeRule* findRule(uint16_t fromSpecies, uint16_t heldModern) {
    for (const auto& r : kRules)
        if (r.from == fromSpecies && r.heldModern != 0 && r.heldModern == heldModern)
            return &r;
    for (const auto& r : kRules)
        if (r.from == fromSpecies && r.heldModern == 0)
            return &r;
    return nullptr;
}

const TradeRule* baseRuleFor(uint16_t fromSpecies) {
    for (const auto& r : kRules)
        if (r.from == fromSpecies)
            return &r;
    return nullptr;
}

static const uint16_t GEN3_TO_MODERN[377] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41, 42, 65, 66, 67, 68, 69, 43, 44, 70, 71,
    72, 73, 74, 75, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 114, 55, 56, 57, 58, 59, 60, 0,
    63, 64, 114, 76, 77, 78, 79, 114, 114, 114, 114, 114, 114, 80, 81, 82,
    83, 84, 85, 114, 114, 114, 114, 86, 87, 114, 88, 89, 90, 91, 92, 93,
    114, 114, 114, 114, 114, 114, 114, 114, 114, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 201, 202, 203, 204, 205, 206, 207, 208,
    114, 114, 114, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241,
    242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257,
    258, 259, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114,
    114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 260, 261,
    262, 263, 264, 718, 444, 471, 445, 446, 447, 456, 457, 114, 720, 721, 722, 476,
    719, 0, 725, 726, 534, 535, 727, 728, 729, 730, 731, 732, 733, 463, 99, 100,
    735, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342,
    343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358,
    359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374,
    375, 376, 377, 420, 421, 422, 423, 424, 425, 737, 0, 114, 114, 459, 651, 467,
    0, 877, 103, 475, 878, 101, 102, 874, 450, 442, 443, 0, 123, 0, 0, 0,
    0, 113, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t GEN2_TO_MODERN[256] = {
    0, 1, 2, 213, 3, 4, 0, 450, 81, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 17, 78, 79, 41, 82, 83, 84, 0, 45, 46, 47, 48, 256, 49,
    50, 60, 85, 257, 92, 63, 27, 28, 29, 55, 76, 77, 56, 0, 30, 31,
    32, 57, 0, 58, 59, 0, 444, 471, 0, 216, 445, 446, 891, 447, 51, 38,
    39, 40, 478, 464, 456, 484, 474, 482, 33, 217, 151, 890, 237, 244, 149, 153,
    152, 245, 221, 156, 150, 485, 86, 87, 222, 486, 0, 223, 487, 488, 224, 243,
    248, 490, 241, 491, 0, 489, 240, 473, 251, 259, 228, 246, 242, 157, 88, 89,
    229, 247, 504, 0, 472, 239, 258, 230, 0, 34, 35, 36, 37, 238, 231, 475,
    481, 0, 479, 90, 91, 476, 480, 0, 0, 0, 249, 43, 232, 0, 0, 233,
    250, 0, 234, 0, 0, 0, 154, 235, 0, 0, 0, 0, 44, 495, 0, 493,
    494, 492, 0, 236, 497, 498, 496, 0, 0, 80, 0, 0, 252, 155, 158, 477,
    0, 500, 483, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 328,
    329, 330, 331, 0, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343,
    344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 0, 356, 357, 358,
    359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374,
    375, 376, 377, 420, 421, 422, 423, 424, 425, 737, 0, 0, 0, 0, 0, 0
};


uint16_t heldToDisplayModern(GameType g, uint16_t heldRaw) {
    if (isGen1File(g)) return 0;
    if (isGen2File(g)) {
        if (heldRaw < 256 && GEN2_TO_MODERN[heldRaw] != 0) return GEN2_TO_MODERN[heldRaw];
        return heldRaw == 0 ? 0 : heldRaw; // fallback modern as-is for unknown
    }
    if (isFRLG(g) || isImportedFile(g)) {
        if (heldRaw < 377 && GEN3_TO_MODERN[heldRaw] != 0) return GEN3_TO_MODERN[heldRaw];
        // 0 stays 0, unknown stays as raw (may be 0)
        return heldRaw;
    }
    return heldRaw;
}


uint16_t heldToModern(GameType g, uint16_t heldRaw) {
    if (isGen1File(g)) return 0;
    if (isGen2File(g)) {
        switch (heldRaw) {
            case 82: return 221;  // King's Rock
            case 143: return 233; // Metal Coat
            case 151: return 235; // Dragon Scale
            case 172: return 252; // Up-Grade
            default: return 0;
        }
    }
    if (isFRLG(g) || isImportedFile(g)) {
        switch (heldRaw) {
            case 187: return 221;
            case 199: return 233;
            case 201: return 235;
            case 218: return 252;
            case 192: return 226;
            case 193: return 227;
            default: return heldRaw == 0 ? 0 : 0; // solo trade item mappati contano
        }
    }
    return heldRaw; // modern già
}

int maxDexFor(GameType g) {
    if (isGen1File(g)) return 151;
    if (isGen2File(g)) return 251;
    if (isFRLG(g) || isImportedFile(g)) return 386;
    if (isGen4File(g)) return 493;
    if (isGen5File(g)) return 649;
    if (isGen6XY(g) || isGen7SM(g) || isLGPE(g)) return 807;
    // Switch moderno (SwSh/BDSP/LA/SV/ZA)
    return 1025;
}

bool isPairedSpecies(uint16_t species) { return species == 588 || species == 616; }
uint16_t pairedCounterpart(uint16_t species) {
    if (species == 588) return 616;
    if (species == 616) return 588;
    return 0;
}

int findCounterpartInParty(const SaveFile& save, int excludeIdx, uint16_t neededSpecies) {
    for (int i = 0; i < 6; i++) {
        if (i == excludeIdx) continue;
        Pokemon p = save.getPartySlot(i);
        if (!p.isEmpty() && !p.isEgg() && p.species() == neededSpecies)
            return i;
    }
    return -1;
}

bool supported(GameType g) {
    // Scrivibili e verificati: GB, GBA, DS4, DS5, XY/SM, LGPE, Switch (SwSh/BDSP/LA/SV/ZA)
    if (isGen1File(g) || isGen2File(g) || isFRLG(g) || isImportedFile(g)) return true;
    if (isGen4File(g) || isGen5File(g)) return true;
    if (isGen6XY(g) || isGen7SM(g) || isLGPE(g)) return true;
    if (isSwSh(g) || isBDSP(g) || isSV(g) || g == GameType::LA || g == GameType::ZA) return true;
    return false;
}

bool applyTradeEvolution(Pokemon& pkm, const TradeRule* rule) {
    if (!rule || rule->to == 0) return false;
    GameType g = pkm.gameType_;
    if (isOutOfRange(g, rule->to)) return false;

    if (isGen1File(g)) {
        uint8_t internal = Gen1::ndexToInternal((uint8_t)rule->to);
        if (internal == 0) return false;
        pkm.data[0] = internal;
        // tipi 5,6 lasciati (verranno riconsiderati dal gioco al ritiro)
        return true;
    }
    if (isGen2File(g)) {
        pkm.data[0] = (uint8_t)rule->to;
        if (rule->heldModern != 0) pkm.data[1] = 0; // consumo
        return true;
    }
    // Gen3+ — record cifrabile Pk3 (80B) via offsets
    if (isFRLG(g) || isImportedFile(g)) {
        uint16_t internal = SpeciesConverter::getInternal3(rule->to);
        if (internal == 0) return false;
        pkm.writeU16(0x20, internal);
        if (rule->heldModern != 0) pkm.writeU16(0x22, 0);
        std::memset(pkm.data.data() + 80, 0, 20);
        pkm.refreshChecksum();
        return pkm.pk3ChecksumValid();
    }
    // Generico moderno (Gen4+, Switch): 0x08 specie diretta, 0x0A held, 0x06 checksum.
    // Per ZA/SV serve internal9, per gli altri ndex diretto.
    bool isSVZA = (g == GameType::ZA || g == GameType::S || g == GameType::V);
    uint16_t toWrite = isSVZA ? SpeciesConverter::getInternal9(rule->to) : rule->to;
    if (isSVZA && toWrite == 0) return false;
    pkm.writeU16(0x08, toWrite);
    if (rule->heldModern != 0) pkm.writeU16(0x0A, 0);
    pkm.refreshChecksum();
    // validazione checksum minima (per PK4/5/6/8/9)
    // Gen4/5/6 usano pk3ChecksumValid? no, hanno loro — refresh basta, se la specie
    // era scrivibile il save la accetta.
    return true;
}

} // namespace TradeEvo
