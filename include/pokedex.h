#pragma once

// Pokedex registration — updates the save file's Pokedex when a Pokemon is
// placed into a box slot.  Ported from PKHeX.Core Zukan* classes.
//
// Supported games:
//   ZA       — Zukan9a   (SCBlock 0x2D87BE5C, 0x84-byte entries)
//   SV       — Zukan9    (Paldea 0x0DEAAEBD / Kitakami 0xF5D7C0E2)
//   SwSh     — Zukan8    (SCBlock 0x4716c404, 0x30-byte entries)
//   FRLG     — SAV3      (sector-based caught/seen bitfields)
//   BDSP     — Zukan8b   (flat binary, offset 0x7A328)
//   LGPE     — Zukan7b   (flat binary, block at 0x02A00)
//   LA       — skipped   (research-task based, not applicable)

struct Pokemon;
class SaveFile;

namespace Pokedex {

// Register a Pokemon in the save file's Pokedex.
// Skips eggs and empty slots.  Safe to call for any GameType.
void registerPokemon(SaveFile& save, const Pokemon& pkm);

// Read-only Pokedex summary (caught/total) for the UI preview (Galleria).
// Pure peek: never touches block/raw data, only counts bits already there —
// completely separate from registerPokemon() above, zero risk of altering
// what registerPokemon() does or the blocks it writes.
//
// Covered (supported=true): ZA, SV, SwSh, BDSP, LGPE, FRLG, R/S/E (share
// FRLG's caught-bitfield offset, see getDexStatus() in pokedex.cpp), Gen1
// R/B/Y, Gen2 G/S/C, Gen4 DP/Pt/HGSS, Gen5 Black/White only (dex block
// offsets found empirically against real saves in tools/test save/ — see
// getDexStatusGen4()/getDexStatusGen5()).
// Not covered yet (supported=false): Gen5 Black2/White2 (offset unverified,
// no B2W2 fixture on hand), Gen6/7 3DS (general-block offset never mapped
// in this repo, box-only today), LA (research-task Pokedex, not a simple
// caught bit — needs its own design).
//
// TODO(dex-gen6-7-b2w2): restano XY, SM e B2W2. Fixture reali arrivate dopo:
// oh_ultrasun.sav (US, 960 slot) e oh_omegaruby.sav (OR, 930 slot) hanno
// permesso register+getDexStatus per USUM/ORAS (verifica empirica boxed-caught
// 807/807 e 125/125). Per XY/SM/B2W2 servono ancora dump reali con dex
// popolato — vedi tools/dex_research/README.md per il metodo.
struct DexStatus {
    bool supported = false; // false: game/format not covered yet (show "--")
    int caught = 0;
    int total = 0;
};
DexStatus getDexStatus(SaveFile& save);

} // namespace Pokedex
