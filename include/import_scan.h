#pragma once
#include "game_type.h"
#include "import_paths.h"
#include <string>
#include <vector>

// One save file (or, with hasSave=false, one orphan ROM with no save yet)
// found while scanning enabled import paths.
struct ImportedGame {
    GameType    type;      // isImportedFile() (Gen3 GBA) or isGen1File() (Gen1 SRAM) slot
    std::string filePath;  // hasSave: absolute save path, ready for SaveFile::load().
                            // !hasSave: absolute ROM path directly (no save to load).
    std::string sourceTag; // last path segment before the filename ("saves",
                            // "roms", ...) — shown as a small on-tile badge
                            // when more than one source could plausibly hold
                            // the same game, so the user can tell them apart.
    bool hasSave = true;   // false = ROM found with no matching save (Settings::
                            // showRomsWithoutSave()) — launch-only entry, no
                            // box/party/items to edit until a save exists.
};

// Scans every enabled entry in `paths` for a Gen3 GBA save (128KB, valid
// sector layout per SaveFile::loadGBA) and identifies which of
// Ruby/Sapphire/Emerald each one is. An entry whose path starts with "usb:"
// is resolved against every currently mounted UMS device (the suffix after
// "usb:" is appended to each "umsN:" mount point) since USB device names are
// only known at mount time, not when the path was configured.
//
// When `autoCheckUsb` is true, every currently mounted UMS device is ALSO
// probed at "<device>:/roms/saves/" then "<device>:/roms/" — no configured
// path needed. "roms/saves" is checked first so, if the same game exists in
// both (e.g. a ROM's companion .sav next to it AND a copy under a dedicated
// saves folder), the more deliberate "saves" location wins.
//
// Only the first match per GameType is kept, across configured paths AND the
// autocheck probe combined — configured paths are scanned first.
//
// Gen1/Gen2 saves are not detected here — no Pk1/Pk2 reader exists yet
// (docs/archive/GEN_PLAN.md Fase 3/4). A file that isn't a 128KB Gen3 save, or is a
// FireRed/LeafGreen save (already reachable via their real titleId-backed
// GameType), is silently skipped: scanning a folder full of unrelated files
// is the normal case, not an error.
//
// includeRomsWithoutSave (Settings::showRomsWithoutSave()): after the normal
// save scan above, also looks for GBA/GB/GBC ROM files (header-identified via
// RomInfo::detect(), not save-shaped) that have NO matching save among the
// entries just found — pushed as hasSave=false ImportedGame (rom path in
// filePath, launch-only). Same `claimed` dedup as the save scan, so a game
// that already has a real save never gets a duplicate orphan entry. NDS/3DS
// ROMs are skipped here: RomInfo::detect() identifies the console for them
// but not which specific game (no per-title code table yet).
std::vector<ImportedGame> scanImportPaths(const std::vector<ImportPathEntry>& paths, bool autoCheckUsb,
                                          bool includeRomsWithoutSave = false);
