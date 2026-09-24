#include "import_paths.h"
#include <fstream>
#include <sys/stat.h>

// File format: one entry per line, "<0|1><TAB><path>". Simple and
// grep-friendly, matching the project's other flat config files
// (theme.cfg is a single byte; update.cfg is key=value — this one needs a
// list, hence one line per entry instead).
std::vector<ImportPathEntry> loadImportPaths(const std::string& basePath) {
    std::vector<ImportPathEntry> out;
    const std::string defaultPath = basePath + "import/";

    std::ifstream f(basePath + "import_paths.cfg");
    if (f.good()) {
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() < 2)
                continue;
            size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0)
                continue;
            bool enabled = (line[0] == '1');
            std::string path = line.substr(tab + 1);
            if (path.empty())
                continue;
            out.push_back({path, enabled});
        }
    }

    bool hasDefault = false;
    for (const auto& e : out)
        if (e.path == defaultPath) { hasDefault = true; break; }
    if (!hasDefault)
        out.insert(out.begin(), ImportPathEntry{defaultPath, true});

#ifdef OH_LINUX
    // Build Linux (R36S): oltre alla cartella import/, scansiona di default
    // le cartelle ROM di ArkOS (GBA/GBC/GB), dove RetroArch salva i .srm
    // accanto alle ROM. Se il file import_paths.cfg le gia' elenca, niente
    // doppioni. Le cartelle inesistenti vengono ignorate da scanImportPaths.
    {
        const char* kRomsDirs[] = { "/roms/gba", "/roms/gbc", "/roms/gb" };
        for (const char* d : kRomsDirs) {
            bool present = false;
            for (const auto& e : out)
                if (e.path == d) { present = true; break; }
            if (!present)
                out.push_back({d, true});
        }
    }
#endif

    // Best-effort: make sure the default folder actually exists so the user
    // has somewhere obvious to drop files into. Never fatal if it fails.
    mkdir(defaultPath.c_str(), 0755);

    return out;
}

bool saveImportPaths(const std::string& basePath, const std::vector<ImportPathEntry>& paths) {
    std::ofstream f(basePath + "import_paths.cfg", std::ios::trunc);
    if (!f.good())
        return false;
    for (const auto& e : paths)
        f << (e.enabled ? '1' : '0') << '\t' << e.path << '\n';
    return true;
}
