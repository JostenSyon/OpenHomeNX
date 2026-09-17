#include "emulator.h"
#include <sys/stat.h>
#include <vector>
#include <switch.h>

namespace Emulator {
namespace {

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Taglia l'estensione (l'ultimo ".nome" dopo l'ultimo '/'), se c'e'.
std::string stripExt(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return path; // nessuna estensione
    return path.substr(0, dot);
}

} // namespace

std::string findMgba() {
    static const char* kCandidates[] = {
        "sdmc:/switch/mGBA.nro",
        "sdmc:/switch/mgba.nro",
        "sdmc:/switch/mGBA/mGBA.nro",
        "sdmc:/switch/mgba/mgba.nro",
    };
    for (auto* c : kCandidates)
        if (fileExists(c)) return c;
    return "";
}

std::string findRomForSave(const std::string& savePath, GameType g) {
    if (savePath.empty()) return "";
    std::string base = stripExt(savePath);
    // Stesso nome del save (richiesto da mGBA per l'abbinamento), cambia
    // solo l'estensione. Provo minuscolo e MAIUSCOLO: la SD e' quasi
    // sempre case-insensitive ma non sempre case-preserving.
    std::vector<std::string> exts;
    if (isGen1File(g)) exts = {".gb", ".GB"};
    else if (isGen2File(g)) exts = {".gbc", ".GBC", ".gb", ".GB"};
    else if (isImportedFile(g)) exts = {".gba", ".GBA"};
    else return ""; // generazione non emulabile con mGBA (es. Gen4 DS)
    for (auto& e : exts) {
        std::string cand = base + e;
        if (fileExists(cand)) return cand;
    }
    return "";
}

bool launchInMgba(const std::string& mgbaPath, const std::string& romPath) {
    if (mgbaPath.empty() || romPath.empty()) return false;
    if (!envHasNextLoad()) return false; // non chainloadabile (es. da salto Album)
    // Argomenti tra virgolette: argv.c parsa il nextLoad splittando su spazi
    // ma rispetta le virgolette, quindi path con spazi/parentesi (comuni nei
    // nomi rom) restano un unico argomento.
    std::string argv = "\"" + mgbaPath + "\" \"" + romPath + "\"";
    envSetNextLoad(mgbaPath.c_str(), argv.c_str());
    return true;
}

} // namespace Emulator
