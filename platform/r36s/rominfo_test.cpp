// Harness di verifica RomInfo su ROM reali (solo test, non linkato nell'app).
#include "rominfo.h"
#include "debug_log.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    DebugLog::init("/tmp/");
    std::vector<std::string> dirs;
    for (int i = 1; i < argc; i++) dirs.push_back(argv[i]);
    if (dirs.empty()) dirs = {"/roms/gba", "/roms/gbc", "/roms/gb"};
    auto roms = RomInfo::scanRoms(dirs);
    printf("ROM trovate: %zu\n", roms.size());
    for (const auto& r : roms) {
        RomInfo::Info info;
        bool ok = RomInfo::detect(r, info);
        std::string canon = ok ? RomInfo::canonicalName(info) : "";
        const char* base = strrchr(r.c_str(), '/');
        base = base ? base + 1 : r.c_str();
        printf("%-45s -> kind=%s game=%s lang=%s [%s]\n", base,
               info.kind.c_str(), info.game.c_str(), info.lang.c_str(),
               canon.empty() ? "NO-RENAME" : canon.c_str());
    }
    return 0;
}
