#pragma once
#include <cstdint>
#include "game_type.h"
#include "pokemon.h"

class SaveFile;

// Scambio simulato (self-trade) — tabella unificata.
// Fonte PKHeX EvoCriteria: SOLO trade secco + trade+strumento.
// Level-up/uso strumento esclusi (Gligar ecc.).
namespace TradeEvo {

struct TradeRule {
    uint16_t from;       // national dex base
    uint16_t heldModern; // modern id richiesto (0 = nessuno) — cfr. modern.rs
    uint16_t to;         // national dex risultato
};

// Regola esatta per (specie, heldModern) — prima match con strumento poi secco.
const TradeRule* findRule(uint16_t fromSpecies, uint16_t heldModern);

// Base per specie ignorando held (hint "richiede X").
const TradeRule* baseRuleFor(uint16_t fromSpecies);

// Held raw del save → modern id per logica trade (solo 6 ID trade)
uint16_t heldToModern(GameType g, uint16_t heldRaw);
// Held raw → modern per sola visualizzazione (tutti gli item Gen2/Gen3 → modern, per non perdere il nome corretto)
uint16_t heldToDisplayModern(GameType g, uint16_t heldRaw);

// Dex massimo contenibile nel save (fuori-range = non evolvibile, ! rosso).
int maxDexFor(GameType g);
inline bool isOutOfRange(GameType g, uint16_t to) { return to > (uint16_t)maxDexFor(g); }

// Karrablast 588 ↔ Shelmet 616 : evoluzione accoppiata.
bool isPairedSpecies(uint16_t species);
uint16_t pairedCounterpart(uint16_t species);
int findCounterpartInParty(const SaveFile& save, int excludeIdx, uint16_t neededSpecies);

// Supportato (scrivibile) — V1: Gen1/2/3, poi Switch/DS.
bool supported(GameType g);

// Applica a record decifrato della gen di pkm.gameType_ (GB u8, altro u16).
// Ritorna false senza toccare pkm se fuori-range/canonical fallisce.
bool applyTradeEvolution(::Pokemon& pkm, const TradeRule* rule);

} // namespace TradeEvo
