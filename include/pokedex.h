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
// TODO(dex-gen6-7-b2w2): tools/test save/upstream/{b2w2,xy,oras,usum}_blank.main
// arrived all-zero (512KB of zero bytes, and 512KB is the Gen4/5 DS save
// size anyway — Gen6/7 3DS "main" dumps are a different size, e.g. Gen7
// moon-sm.sav is 0x6BE00), so they were unusable and got discarded. Ask for
// real PKHeX "Export SAV" fixtures (Black2 or White2; X or Y; Omega Ruby or
// Alpha Sapphire; Ultra Sun or Ultra Moon) before attempting these — see
// tools/dex_research/README.md for the method once fixtures exist.
struct DexStatus {
    bool supported = false; // false: game/format not covered yet (show "--")
    int caught = 0;
    int total = 0;
};
DexStatus getDexStatus(SaveFile& save);

} // namespace Pokedex
