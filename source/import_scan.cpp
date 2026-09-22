#include "import_scan.h"
#include "save_file.h"
#include "debug_log.h"
#include "rominfo.h"
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Ruby and Sapphire share gameCode 0 at sector-0 offset 0xac — real GBA
// cartridges never recorded which of the two a save came from. OpenHome's own
// G3SaveBackup (upstream reference) has the exact same ambiguity and falls
// back to Ruby when nothing else disambiguates it; we do the same, preferring
// a filename hint first since emulator save files are almost always named
// after the ROM. gameCode 1 is FireRed/LeafGreen: questi hanno anche
// GameType nativi con titleId reale (NSO GBA), quindi in teoria "gia'
// coperti" — ma solo per chi possiede quel titolo Switch. Chi ha solo la
// ROM GBA (praticamente sempre su R36S/ArkOS, dove non esiste alcun
// concetto di titolo Switch installato) restava senza alcuna riga per
// FireRed/LeafGreen: prima qui veniva scartato a prescindere. Ora viene
// classificato come qualunque altro Gen3 -- l'eventuale doppione quando
// ENTRAMBI (titolo nativo + ROM import) sono presenti si evita a monte,
// in UI::appendImportedGames() (stesso bankGroupName gia' in availableGames_).
bool detectGen3Version(const std::string& filename, SaveFile& probe, GameType& outType) {
    uint8_t* sector0 = probe.findGbaSectorData(0);
    if (!sector0)
        return false;
    uint32_t gameCode = static_cast<uint32_t>(sector0[0xac])
                       | (static_cast<uint32_t>(sector0[0xad]) << 8)
                       | (static_cast<uint32_t>(sector0[0xae]) << 16)
                       | (static_cast<uint32_t>(sector0[0xaf]) << 24);

    // Filename keywords in all common languages (EN/IT/DE/FR/ES): the GBA
    // save itself carries no game code (bytes at 0xAC are 0 on real saves),
    // so the name is the primary signal — same as PKHeX, which asks the user
    // which game a file is when the bytes cannot decide.
    std::string low = toLower(filename);
    auto has = [&](const char* k) { return low.find(k) != std::string::npos; };
    if (has("sapphire") || has("zaffiro") || has("saphir") || has("zafiro")) { outType = GameType::SAPPHIRE; return true; }
    if (has("ruby") || has("rubino") || has("rubin") || has("rubis") || has("rub")) { outType = GameType::RUBY; return true; }
    if (has("emerald") || has("smeraldo") || has("smaragd") || has("meraude") || has("esmeralda")) { outType = GameType::EMERALD; return true; }
    if (has("leafgreen") || has("verdefoglia") || has("verde foglia") || has("blattgruen") || has("feuille") || has("hoja")) { outType = GameType::LG; return true; }
    if (has("firered") || has("rossofuoco") || has("rosso fuoco") || has("feuerrot") || has("rougefeu") || has("rojofuego")) { outType = GameType::FR; return true; }
    // No keyword: the save's own game code (ASCII at sector0+0xAC).
    char code[5] = {0};
    code[0] = static_cast<char>(sector0[0xac]);
    code[1] = static_cast<char>(sector0[0xad]);
    code[2] = static_cast<char>(sector0[0xae]);
    code[3] = static_cast<char>(sector0[0xaf]);
    if (std::strcmp(code, "AXPE") == 0) { outType = GameType::SAPPHIRE; return true; }
    if (std::strcmp(code, "AXVE") == 0) { outType = GameType::RUBY;     return true; }
    if (std::strcmp(code, "BPEE") == 0) { outType = GameType::EMERALD;  return true; }
    if (std::strcmp(code, "BPRE") == 0) { outType = GameType::FR;       return true; }
    if (std::strcmp(code, "BPGE") == 0) { outType = GameType::LG;       return true; }
    if (gameCode == 1) { outType = GameType::FR; return true; } // FR/LG ambigui: FR come default, stessa politica di Ruby
    outType = (gameCode == 0) ? GameType::RUBY : GameType::EMERALD;
    return true;
}

// Gen 1 (R/B/Y SRAM dump, PKHeX SAV1 INT offsets): 32KB + valid checksum.
// Yellow is byte-detectable (Pikachu starter 0x54 at 0x29C3, else nonzero
// Pikachu friendship at 0x271C). Red vs Blue are byte-identical: filename
// hint first (EN + IT), RED default — same policy as Ruby-over-Sapphire.
bool detectGen1Version(const std::string& filename, const std::string& full, GameType& outType) {
    std::ifstream file(full, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) != 0x8000)
        return false;
    file.seekg(0);
    std::vector<uint8_t> d(0x8000);
    file.read(reinterpret_cast<char*>(d.data()), d.size());
    if (!file)
        return false;

    uint8_t s = 0;
    for (int i = 0x2598; i < 0x3523; i++) s += d[i];
    if (d[0x3523] != static_cast<uint8_t>(~s))
        return false; // not a valid INT Gen1 save (JP saves land here too)

    if (d[0x29C3] == 0x54 || d[0x271C] != 0) {
        outType = GameType::YELLOW;
        return true;
    }
    std::string low = toLower(filename);
    if (low.find("blue") != std::string::npos || low.find("blu") != std::string::npos) {
        outType = GameType::BLUE;
        return true;
    }
    // "red"/"rosso"/"yellow"/"giallo" or anything else: Yellow was already
    // excluded by the bytes above, so this is Red (default, documented).
    outType = GameType::RED;
    return true;
}

// Gen 2 (G/S/C SRAM dump, PKHeX SAV2 INT offsets): 32KB + one of the two
// checksum pairs valid (Crystal: sum 0x2009..0x2B82 at 0x2D0D+0x1F0D LE;
// GS: sum 0x2009..0x2D68 at 0x2D69+0x7E6D LE). Checksum-first detection:
// bytes decide between Crystal and GS/Gold/Silver; inside GS the filename
// decides Gold vs Silver (byte-identical), GOLD default.
bool detectGen2Version(const std::string& filename, const std::string& full, GameType& outType) {
    std::ifstream file(full, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    if (static_cast<size_t>(file.tellg()) != 0x8000)
        return false;
    file.seekg(0);
    std::vector<uint8_t> d(0x8000);
    file.read(reinterpret_cast<char*>(d.data()), d.size());
    if (!file)
        return false;

    auto u16le = [&](int o) -> uint16_t {
        return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
    };
    uint16_t sc = 0;
    for (int i = 0x2009; i <= 0x2B82; i++) sc += d[i];
    bool crystal = (u16le(0x2D0D) == sc && u16le(0x1F0D) == sc);
    uint16_t sg = 0;
    for (int i = 0x2009; i <= 0x2D68; i++) sg += d[i];
    bool gs = (u16le(0x2D69) == sg && u16le(0x7E6D) == sg);
    if (!crystal && !gs)
        return false; // neither valid (JP/KR saves land here too)

    if (crystal && !gs) {
        outType = GameType::CRYSTAL;
        return true;
    }
    // GS (or ambiguous tie): filename decides Gold vs Silver.
    std::string low = toLower(filename);
    if (low.find("silver") != std::string::npos || low.find("argento") != std::string::npos ||
        low.find("silber") != std::string::npos || low.find("argent") != std::string::npos) {
        outType = GameType::SILVER;
        return true;
    }
    outType = GameType::GOLD; // default (also on crystal+gs tie without silver hint)
    if (crystal) {
        if (low.find("crystal") != std::string::npos || low.find("cristal") != std::string::npos ||
            low.find("cristallo") != std::string::npos || low.find("kristall") != std::string::npos)
            outType = GameType::CRYSTAL;
    }
    return true;
}

// Last path segment before the filename — e.g. "ums0:/roms/saves/x.sav" ->
// "saves". Falls back to the whole dir if there's no '/' to split on.
std::string lastPathSegment(const std::string& dir) {
    std::string d = dir;
    while (!d.empty() && d.back() == '/')
        d.pop_back();
    auto pos = d.find_last_of('/');
    return pos == std::string::npos ? d : d.substr(pos + 1);
}

// On-tile source badge with device prefix ("USB:saves" vs "SD:import"), so
// two copies of the same game from different devices are distinguishable.
std::string sourceTagFor(const std::string& dir) {
    std::string tag = lastPathSegment(dir);
    if (dir.rfind("ums", 0) == 0)
        return "USB:" + tag;
    if (dir.rfind("sdmc:", 0) == 0)
        return "SD:" + tag;
    return tag;
}

// Gen 4/5 (DS .sav dumps, 512KB NDS flash). Layouts are byte-distinguishable
// (DP/Pt/HGSS sizes, BW Game byte), so the filename only breaks the D/P tie
// (byte-identical layouts) — multi-language keywords, DIAMOND default.
bool detectDSVersion(const std::string& filename, const std::string& full, GameType& outType) {
    struct stat st;
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (static_cast<size_t>(st.st_size) != 0x80000)
        return false;
    // Gen5 first: the PlayerData.Game byte is exact (20 = White, 21 = Black,
    // 22 = White2, 23 = Black2 — same head offsets in BW and B2W2 maps).
    {
        SaveFile probe;
        probe.setGameType(GameType::BLACK);
        if (probe.load(full)) {
            uint8_t gb = probe.dsGameByte();
            if (gb == 20) { outType = GameType::WHITE; return true; }
            if (gb == 21) { outType = GameType::BLACK; return true; }
            if (gb == 22) { outType = GameType::WHITE2; return true; }
            if (gb == 23) { outType = GameType::BLACK2; return true; }
            return false; // loadDS5 rejects other Game bytes explicitly
        }
    }
    // Gen4: any layout loads under a placeholder; refine below.
    SaveFile probe;
    probe.setGameType(GameType::DIAMOND);
    if (!probe.load(full))
        return false;
    if (probe.dsLayout() == SaveFile::Ds4Layout::PT) {
        outType = GameType::PLATINUM;
        return true;
    }
    if (probe.dsLayout() == SaveFile::Ds4Layout::HGSS) {
        uint8_t rc = probe.dsRomCode();
        if (rc == 8 || rc == 241) { outType = GameType::SOULSILVER; return true; }
        if (rc == 7) { outType = GameType::HEARTGOLD; return true; }
        // 241 osservato su un vero SoulSilver DeSmuME (il byte non è un
        // GameVersion pulito su tutti gli emulatori): vale come SoulSilver,
        // la prova layout HGSS c'è già. Altri valori -> nome file.
        DebugLog::line("import scan: %s -> ROMCode %u ambiguo, uso il nome",
                       filename.c_str(), rc);
        std::string low = toLower(filename);
        if (low.find("soulsilver") != std::string::npos) { outType = GameType::SOULSILVER; return true; }
        if (low.find("heartgold") != std::string::npos) { outType = GameType::HEARTGOLD; return true; }
        outType = GameType::HEARTGOLD;
        return true;
    }
    std::string low = toLower(filename);
    auto has = [&](const char* k) { return low.find(k) != std::string::npos; };
    if (has("pearl") || has("perla") || has("perle")) { outType = GameType::PEARL; return true; }
    outType = GameType::DIAMOND; // default (also on diamond/diamante/diamant)
    return true;
}

// Gen 6 XY / Gen 7 SM+USUM (decrypted 3DS dumps, Citra/Checkpoint style).
// Detection is fully byte-driven: fixed MyStatus offsets carry exact Game
// bytes (XY: 24/25, SM: 30/31, USUM: 32/33), then box slots must decrypt to
// valid checksums. Encrypted cartridge dumps fail the Game byte explicitly.
// I marker delle tre famiglie vivono a offset diversi (0x14000 / 0x01200 /
// 0x01400): ogni ramo rifiuta se un'altra famiglia dichiara il file, cosi'
// un byte coincidente altrove non dirotta mai il detect (i loader 3DS non
// hanno checksum e tornerebbero true anche a box vuoti).
bool detect3DSVersion(const std::string& full, GameType& outType) {
    std::ifstream file(full, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    auto markerAt = [&](size_t off) -> int {
        if (size < off + 8) return -1;
        std::vector<uint8_t> status(8);
        file.clear();
        file.seekg(off);
        file.read(reinterpret_cast<char*>(status.data()), 8);
        if (!file) return -1;
        return status[4];
    };
    int mXY = markerAt(0x14000);
    int mSM = markerAt(0x01200);
    int mUSUM = markerAt(0x01400);
    // XY e ORAS condividono l'offset MyStatus (0x14000+4: 24/25 vs 26/27,
    // valori disgiunti). mXY qui sotto vale per entrambi: il ramo ORAS
    // richiede 26/27, quello XY 24/25.
    // USUM prima: il suo marker (32/33) non collide mai con SM (max count 6).
    // Ogni ramo richiede il proprio marker E nessun altro: un file con due
    // marker (corrotto/patologico) non viene attribuito a nessun gioco,
    // mai al gioco sbagliato.
    if (size >= 0x05200 + static_cast<size_t>(32) * 30 * 232) {
        if ((mUSUM == 32 || mUSUM == 33) && mXY != 24 && mXY != 25 && mXY != 26 && mXY != 27 && mSM != 30 && mSM != 31) {
            SaveFile probe;
            probe.setGameType(mUSUM == 32 ? GameType::ULTRA_SUN : GameType::ULTRA_MOON);
            if (probe.load(full)) {
                outType = mUSUM == 32 ? GameType::ULTRA_SUN : GameType::ULTRA_MOON;
                return true;
            }
        }
    }
    // XY boxes end at 0x22600 + 31*30*232; SM at 0x04E00 + 32*30*232.
    if (size >= 0x22600 + static_cast<size_t>(31) * 30 * 232) {
        if ((mXY == 24 || mXY == 25) && mSM != 30 && mSM != 31 && mUSUM != 32 && mUSUM != 33) {
            SaveFile probe;
            probe.setGameType(mXY == 24 ? GameType::X : GameType::Y);
            if (probe.load(full)) {
                outType = mXY == 24 ? GameType::X : GameType::Y;
                return true;
            }
        }
    }
    // ORAS: Box 0x33000 (31x30x232), stesso MyStatus di XY (26/27).
    if (size >= 0x33000 + static_cast<size_t>(31) * 30 * 232) {
        if ((mXY == 26 || mXY == 27) && mSM != 30 && mSM != 31 && mUSUM != 32 && mUSUM != 33) {
            SaveFile probe;
            probe.setGameType(mXY == 27 ? GameType::OMEGA_RUBY : GameType::ALPHA_SAPPHIRE);
            if (probe.load(full)) {
                outType = mXY == 27 ? GameType::OMEGA_RUBY : GameType::ALPHA_SAPPHIRE;
                return true;
            }
        }
    }
    if (size >= 0x04E00 + static_cast<size_t>(32) * 30 * 232) {
        if ((mSM == 30 || mSM == 31) && mUSUM != 32 && mUSUM != 33) {
            SaveFile probe;
            probe.setGameType(mSM == 30 ? GameType::SUN : GameType::MOON);
            if (probe.load(full)) {
                outType = mSM == 30 ? GameType::SUN : GameType::MOON;
                return true;
            }
        }
    }
    return false;
}

void scanDir(const std::string& dir, std::vector<ImportedGame>& out, std::set<std::string>& claimed) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        DebugLog::line("import scan: %s -> opendir failed (errno=%d)", dir.c_str(), errno);
        return;
    }
    // Claim key = game type + source tag: the SAME game from two devices
    // (SD "roms" + USB "roms") shows two tiles with different badges;
    // the same folder scanned twice still dedupes.
    auto tryClaim = [&](GameType t, const std::string& tag) -> bool {
        std::string key = std::to_string(static_cast<int>(t)) + '\x1f' + tag;
        if (claimed.count(key) != 0) {
            DebugLog::line("import scan: skip %s %s (doppione, vince il primo)",
                           tag.c_str(), gameInfo(t).gameTag);
            return false;
        }
        claimed.insert(key);
        return true;
    };
    int filesSeen = 0, regularFiles = 0, gbaSized = 0, matched = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.')
            continue;
        filesSeen++;
        std::string full = dir;
        if (!full.empty() && full.back() != '/')
            full += '/';
        full += entry->d_name;

        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        regularFiles++;

        // Gen 6 XY / Gen 7 SM first (large files; byte-driven detection).
        GameType ds3Type = GameType::X;
        if (detect3DSVersion(full, ds3Type)) {
            if (!tryClaim(ds3Type, sourceTagFor(dir))) {
                continue;
            }
            matched++;
            out.push_back({ds3Type, full, sourceTagFor(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(ds3Type).gameTag);
            continue;
        }

        // Gen 4/5 (DS dumps, 512KB): detection is byte-driven (layouts, Game
        // byte); the filename only breaks the D/P tie inside detectDSVersion.
        GameType dsType = GameType::DIAMOND;
        if (detectDSVersion(entry->d_name, full, dsType)) {
            if (!tryClaim(dsType, sourceTagFor(dir))) {
                continue;
            }
            matched++;
            out.push_back({dsType, full, sourceTagFor(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(dsType).gameTag);
            continue;
        }

        // Any isImportedFile() GameType routes SaveFile::load() through the
        // same loadGBA() — which one doesn't matter yet, detectGen3Version()
        // figures out the real type from the loaded bytes below.
        SaveFile probe;
        probe.setGameType(GameType::EMERALD);
        if (probe.load(full)) {
            gbaSized++;

            GameType type;
            if (!detectGen3Version(entry->d_name, probe, type)) {
                DebugLog::line("import scan: skip %s (%lld byte, GBA ma versione ignota)",
                               entry->d_name, (long long)st.st_size);
                continue;
            }

            if (!tryClaim(type, sourceTagFor(dir))) {
                continue; // same game+source already listed
            }
            matched++;
            out.push_back({type, full, sourceTagFor(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(type).gameTag);
            continue;
        }
        // Load fallito su taglia solo-GBA: diagnosi mirata (le altre taglie
        // hanno i loro detect dopo; ROM/stati finiscono nel generico sotto).
        {
            long long sz = (long long)st.st_size;
            if (sz == 0x20000 || sz == 0x20010) {
                DebugLog::line("import scan: skip %s (%lld byte, load fallito: settori/checksum?)",
                               entry->d_name, sz);
                continue;
            }
        }

        // Gen 1 (R/B/Y SRAM): 32KB + valid INT checksum (PKHeX SAV1).
        // Yellow is detectable from the bytes (Pikachu starter / friendship);
        // Red vs Blue are byte-identical, so the filename decides, RED default
        // (same policy as Ruby-over-Sapphire above).
        GameType gbType = GameType::RED;
        if (detectGen1Version(entry->d_name, full, gbType)) {
            if (!tryClaim(gbType, sourceTagFor(dir))) {
                continue;
            }
            matched++;
            out.push_back({gbType, full, sourceTagFor(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(gbType).gameTag);
            continue;
        }

        // Gen 2 (G/S/C SRAM): checksum-driven version detection.
        GameType gbcType = GameType::GOLD;
        if (detectGen2Version(entry->d_name, full, gbcType)) {
            if (!tryClaim(gbcType, sourceTagFor(dir))) {
                continue;
            }
            matched++;
            out.push_back({gbcType, full, sourceTagFor(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(gbcType).gameTag);
            continue;
        }
        // Niente ha matchato: stampa nome + dimensione così si vede
        // letteralmente cosa c'è nel disco (es. .sav con size errata).
        DebugLog::line("import scan: skip %s (%lld byte, save non riconosciuto)",
                       entry->d_name, (long long)st.st_size);
    }
    closedir(d);
    DebugLog::line("import scan: %s -> %d entr(y/ies), %d file(s), %d GBA-sized, %d matched",
                    dir.c_str(), filesSeen, regularFiles, gbaSized, matched);
}

// RomInfo::detect()'s Info.game ("emerald", "firered", ...) -> GameType.
// Unica implementazione: prima duplicata a mano in tre punti diversi
// (ui.cpp boot, ui_selectors.cpp isGameLaunchableAt, e il commento in
// ui_settings.cpp) -- ognuna rifaceva anche la scansione directory da capo
// ad ogni chiamata invece di leggere il risultato di uno scan gia' fatto.
// GameType::EMERALD di fallback per un "game" riconosciuto da RomInfo ma non
// (ancora) elencato qui non dovrebbe mai capitare: i valori possibili sono
// tutti quelli assegnati in rominfo.cpp (GBA: emerald/firered/leafgreen/
// ruby/sapphire; GB/GBC: red/blue/yellow/gold/silver/crystal).
GameType gameTypeFromRomInfoGame(const std::string& g) {
    if (g == "emerald")   return GameType::EMERALD;
    if (g == "ruby")      return GameType::RUBY;
    if (g == "sapphire")  return GameType::SAPPHIRE;
    if (g == "firered")   return GameType::FR;
    if (g == "leafgreen") return GameType::LG;
    if (g == "red")       return GameType::RED;
    if (g == "blue")      return GameType::BLUE;
    if (g == "yellow")    return GameType::YELLOW;
    if (g == "gold")      return GameType::GOLD;
    if (g == "silver")    return GameType::SILVER;
    if (g == "crystal")   return GameType::CRYSTAL;
    return GameType::EMERALD; // fallback difensivo, non dovrebbe accadere
}

// Seconda passata (Settings::showRomsWithoutSave()): ROM GBA/GB/GBC senza
// alcun save trovato sopra. Stesso `claimed` della passata save -- una ROM
// il cui gioco ha gia' un save reale (in questa o altra cartella) non
// produce mai un doppione orfano.
void scanRomsWithoutSave(const std::vector<ImportPathEntry>& paths,
                         std::vector<ImportedGame>& out, std::set<std::string>& claimed) {
    std::vector<std::string> dirs;
    for (const auto& e : paths)
        if (e.enabled && !e.path.empty() && e.path.rfind("usb:", 0) != 0)
            dirs.push_back(e.path);
    for (const auto& rom : RomInfo::scanRoms(dirs)) {
        RomInfo::Info info;
        if (!RomInfo::detect(rom, info) || info.game.empty())
            continue; // NDS/3DS (kind riconosciuto, game no) o ROM ignota
        GameType type = gameTypeFromRomInfoGame(info.game);
        size_t slash = rom.find_last_of('/');
        std::string tag = sourceTagFor(slash == std::string::npos ? rom : rom.substr(0, slash));
        std::string key = std::to_string(static_cast<int>(type)) + '\x1f' + tag;
        if (claimed.count(key) != 0)
            continue; // gia' coperto da un save reale (o da un'altra ROM prima)
        claimed.insert(key);
        out.push_back({type, rom, tag, /*hasSave=*/false});
        DebugLog::line("import scan: %s -> %s (senza save)", rom.c_str(), gameInfo(type).gameTag);
    }
}

} // namespace

std::vector<ImportedGame> scanImportPaths(const std::vector<ImportPathEntry>& paths, bool autoCheckUsb,
                                          bool includeRomsWithoutSave) {
    std::vector<ImportedGame> out;
    std::set<std::string> claimed;

    DebugLog::line("import scan: %zu configured path(s), autoCheckUsb=%d", paths.size(), (int)autoCheckUsb);
    for (const auto& entry : paths) {
        if (!entry.enabled) {
            DebugLog::line("import scan: %s -> disabled, skipped", entry.path.c_str());
            continue;
        }

        if (entry.path.rfind("usb:", 0) == 0) {
#ifdef OH_USB_UPDATE
            // Device name is only known at mount time (umsN: numbering can
            // change between insertions) — apply the configured suffix to
            // every currently mounted UMS device instead of a fixed path.
            std::string suffix = entry.path.substr(4); // keep leading '/'
            u32 n = usbHsFsGetMountedDeviceCount();
            if (n == 0) {
                DebugLog::line("import scan: %s -> no USB device mounted, skipped", entry.path.c_str());
                if (DebugLog::enabled() && usbHsFsGetPhysicalDeviceCount() > 0)
                    DebugLog::line("import scan: drive present but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
                continue;
            }
            if (n > 8) n = 8;
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            for (u32 i = 0; i < got; i++)
                scanDir(std::string(devs[i].name) + suffix, out, claimed);
#else
            DebugLog::line("import scan: %s -> built without OH_USB_UPDATE, skipped", entry.path.c_str());
#endif
            continue;
        }
        scanDir(entry.path, out, claimed);
    }

    if (autoCheckUsb) {
#ifdef OH_USB_UPDATE
        u32 n = usbHsFsGetMountedDeviceCount();
        if (n == 0) {
            DebugLog::line("import scan: autocheck USB -> no device mounted, skipped");
            if (DebugLog::enabled() && usbHsFsGetPhysicalDeviceCount() > 0)
                DebugLog::line("import scan: drive present but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
        } else {
            if (n > 8) n = 8;
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            for (u32 i = 0; i < got; i++) {
                std::string dev = devs[i].name;
                // "saves" first: if the same game sits in both, the more
                // deliberate save-folder copy wins over the one that might
                // just be an emulator's auto-generated companion file.
                scanDir(dev + "/roms/saves", out, claimed);
                scanDir(dev + "/roms", out, claimed);
            }
        }
#else
        DebugLog::line("import scan: autocheck USB enabled but built without OH_USB_UPDATE, skipped");
#endif
    }

    if (includeRomsWithoutSave)
        scanRomsWithoutSave(paths, out, claimed);

    DebugLog::line("import scan: done, %zu game(s) found", out.size());
    return out;
}
