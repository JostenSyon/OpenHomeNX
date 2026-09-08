#pragma once
#include "game_type.h"
#include "import_paths.h"
#include <string>
#include <vector>

// One save file found while scanning enabled import paths.
struct ImportedGame {
    GameType    type;      // isImportedFile() (Gen3 GBA) or isGen1File() (Gen1 SRAM) slot
    std::string filePath;  // absolute path, ready for SaveFile::load()
    std::string sourceTag; // last path segment before the filename ("saves",
                            // "roms", ...) — shown as a small on-tile badge
                            // when more than one source could plausibly hold
                            // the same game, so the user can tell them apart.
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
std::vector<ImportedGame> scanImportPaths(const std::vector<ImportPathEntry>& paths, bool autoCheckUsb);
