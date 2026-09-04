#pragma once
#include <string>
#include <vector>

// A file-import source: a folder scanned for save files that don't come from
// a titleId-mounted Switch game (GBA/GB emulator saves on SD or USB). See
// GEN_PLAN.md Fase 2/5 — this is the persistence layer only; the actual
// directory scan / GameType wiring lands separately.
struct ImportPathEntry {
    std::string path;      // e.g. "sdmc:/switch/OpenHomeNX/import/"
    bool        enabled = true;
};

// basePath + "import/" is always present in the returned list (inserted if
// missing from the saved file) — it's the one guaranteed-correct default, so
// it can be disabled but callers should treat it as non-removable in the UI.
std::vector<ImportPathEntry> loadImportPaths(const std::string& basePath);
bool saveImportPaths(const std::string& basePath, const std::vector<ImportPathEntry>& paths);
