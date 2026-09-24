#include "emulator.h"
#include "path_utils.h"
#include <sys/stat.h>
#include <vector>
#include <switch.h>

#ifdef OH_LINUX
#include <unistd.h>
#include <stdlib.h>
#endif

namespace Emulator {
namespace {

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
#ifdef OH_LINUX
    // R36S / ArkOS: emulatore = RetroArch (installato di sistema).
    static const char* kCandidates[] = {
        "/usr/bin/retroarch",
        "/usr/local/bin/retroarch",
        "/opt/retroarch/retroarch",
    };
    for (auto* c : kCandidates)
        if (fileExists(c)) return c;
    return "";
#else
    static const char* kCandidates[] = {
        "sdmc:/switch/mGBA.nro",
        "sdmc:/switch/mgba.nro",
        "sdmc:/switch/mGBA/mGBA.nro",
        "sdmc:/switch/mgba/mgba.nro",
    };
    for (auto* c : kCandidates)
        if (fileExists(c)) return c;
    return "";
#endif
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
    // isImportedFile() copre solo Ruby/Sapphire/Emerald (mai FR/LG, che
    // hanno anche un GameType nativo con titleId reale) -- stessa
    // combinazione isFRLG()||isImportedFile() gia' usata altrove nel
    // codebase per trattare in modo uniforme tutti i save Gen3 su file.
    // Senza isFRLG() qui, un FireRed/LeafGreen importato da file (l'unico
    // modo di averli su R36S, dove non esiste alcun titolo "installato")
    // non trovava mai la sua ROM: bottone "Avvia" sempre nascosto.
    else if (isImportedFile(g) || isFRLG(g)) exts = {".gba", ".GBA"};
    else return ""; // generazione non emulabile (es. Gen4 DS)
    for (auto& e : exts) {
        std::string cand = base + e;
        if (fileExists(cand)) return cand;
    }
    return "";
}

// Sceglie il core RetroArch in base all'estensione della ROM.
// Su ArkOS/R36S GB, GBC e GBA usano tutti il core mGBA (come da
// es_systems.cfg), che gestisce tutte e tre le generazioni.
static std::string retroarchCoreFor(const std::string& romPath) {
    (void)romPath;
    return "mgba";
}

bool launchInMgba(const std::string& mgbaPath, const std::string& romPath) {
    if (mgbaPath.empty() || romPath.empty()) return false;
#ifdef OH_LINUX
    // R36S: l'app deve cedere il display (DRM master unico) a RetroArch.
    // fork(): il child exec RetroArch via un wrapper che, al termine,
    // rilanciera' OpenHomeNX (il parent ritorna true e l'app esce; lo
    // script di lancio /roms/ports gestisce la riapertura). RetroArch
    // salva i .sav accanto alla ROM (stesso nome), coerente con il
    // rilevamento di findRomForSave().
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child: exec del comando retroarch. Dopo l'uscita di retroarch,
        // /roms/ports/OpenHomeNX/OpenHomeNX.sh rilanciera' l'app (vedi lo
        // script di avvio). Qui basta lanciare retroarch e uscire.
        std::string core = retroarchCoreFor(romPath);
        // Path core di ArkOS (vedi es_systems.cfg). Fallback su path comuni.
        std::string corePath;
        const char* kCores[] = {
            "/home/ark/.config/retroarch/cores/mgba_libretro.so",
            "/home/ark/.config/retroarch/cores/gambatte_libretro.so",
            "/usr/lib/libretro/mgba_libretro.so",
        };
        for (const char* c : kCores)
            if (fileExists(c)) { corePath = c; break; }
        if (corePath.empty()) { corePath = "/home/ark/.config/retroarch/cores/" + core + "_libretro.so"; }
        std::string argv = std::string(mgbaPath) + " -L " + corePath + " \"" + romPath + "\"";
        execl("/bin/sh", "sh", "-c", argv.c_str(), (char*)nullptr);
        _exit(127); // execl fallito
    }
    // Parent: l'app esce (il chiamante fa running=false), il child tiene
    // il display per RetroArch.
    return true;
#else
    if (!envHasNextLoad()) return false; // non chainloadabile (es. da salto Album)
    // Argomenti tra virgolette: argv.c parsa il nextLoad splittando su spazi
    // ma rispetta le virgolette, quindi path con spazi/parentesi (comuni nei
    // nomi rom) restano un unico argomento.
    std::string argv = "\"" + mgbaPath + "\" \"" + romPath + "\"";
    envSetNextLoad(mgbaPath.c_str(), argv.c_str());
    return true;
#endif
}

} // namespace Emulator
