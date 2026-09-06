#include "import_scan.h"
#include "save_file.h"
#include "debug_log.h"
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstring>
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
// after the ROM. gameCode 1 is FireRed/LeafGreen — rejected here since those
// already have real titleId-backed GameTypes; import scanning isn't meant to
// duplicate that path.
bool detectGen3Version(const std::string& filename, SaveFile& probe, GameType& outType) {
    uint8_t* sector0 = probe.findGbaSectorData(0);
    if (!sector0)
        return false;
    uint32_t gameCode = static_cast<uint32_t>(sector0[0xac])
                       | (static_cast<uint32_t>(sector0[0xad]) << 8)
                       | (static_cast<uint32_t>(sector0[0xae]) << 16)
                       | (static_cast<uint32_t>(sector0[0xaf]) << 24);
    if (gameCode == 1)
        return false;

    // Filename keywords in all common languages (EN/IT/DE/FR/ES): the GBA
    // save itself carries no game code (bytes at 0xAC are 0 on real saves),
    // so the name is the primary signal — same as PKHeX, which asks the user
    // which game a file is when the bytes cannot decide.
    std::string low = toLower(filename);
    auto has = [&](const char* k) { return low.find(k) != std::string::npos; };
    if (has("sapphire") || has("zaffiro") || has("saphir") || has("zafiro")) { outType = GameType::SAPPHIRE; return true; }
    if (has("ruby") || has("rubino") || has("rubin") || has("rubis") || has("rub")) { outType = GameType::RUBY; return true; }
    if (has("emerald") || has("smeraldo") || has("smaragd") || has("meraude") || has("esmeralda")) { outType = GameType::EMERALD; return true; }
    // No keyword: the save's own game code (ASCII at sector0+0xAC).
    char code[5] = {0};
    code[0] = static_cast<char>(sector0[0xac]);
    code[1] = static_cast<char>(sector0[0xad]);
    code[2] = static_cast<char>(sector0[0xae]);
    code[3] = static_cast<char>(sector0[0xaf]);
    if (std::strcmp(code, "AXPE") == 0) { outType = GameType::SAPPHIRE; return true; }
    if (std::strcmp(code, "AXVE") == 0) { outType = GameType::RUBY;     return true; }
    if (std::strcmp(code, "BPEE") == 0) { outType = GameType::EMERALD;  return true; }
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

// Gen 4/5 (DS .sav dumps, 512KB NDS flash). Layouts are byte-distinguishable
// (DP/Pt/HGSS sizes, BW Game byte), so the filename only breaks the D/P tie
// (byte-identical layouts) — multi-language keywords, DIAMOND default.
bool detectDSVersion(const std::string& filename, const std::string& full, GameType& outType) {
    struct stat st;
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (static_cast<size_t>(st.st_size) != 0x80000)
        return false;
    // Gen5 first: the PlayerData.Game byte is exact (20 = White, 21 = Black).
    {
        SaveFile probe;
        probe.setGameType(GameType::BLACK);
        if (probe.load(full)) {
            uint8_t gb = probe.dsGameByte();
            if (gb == 20) { outType = GameType::WHITE; return true; }
            if (gb == 21) { outType = GameType::BLACK; return true; }
            return false; // loadDS5 rejects non-BW explicitly; be safe
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
        if (rc == 8) { outType = GameType::SOULSILVER; return true; }
        if (rc == 7) { outType = GameType::HEARTGOLD; return true; }
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

void scanDir(const std::string& dir, std::vector<ImportedGame>& out, std::vector<bool>& claimed) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        DebugLog::line("import scan: %s -> opendir failed (errno=%d)", dir.c_str(), errno);
        return;
    }
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

        // Gen 4/5 (DS dumps, 512KB): detection is byte-driven (layouts, Game
        // byte); the filename only breaks the D/P tie inside detectDSVersion.
        GameType dsType = GameType::DIAMOND;
        if (detectDSVersion(entry->d_name, full, dsType)) {
            int idx = static_cast<int>(dsType);
            if (claimed[idx]) {
                DebugLog::line("import scan: skip %s (doppione %s, vince il primo)",
                               entry->d_name, gameInfo(dsType).gameTag);
                continue;
            }
            claimed[idx] = true;
            matched++;
            out.push_back({dsType, full, lastPathSegment(dir)});
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

            int idx = static_cast<int>(type);
            if (claimed[idx]) {
                DebugLog::line("import scan: skip %s (doppione %s, vince il primo)",
                               entry->d_name, gameInfo(type).gameTag);
                continue; // first match per GameType wins
            }
            claimed[idx] = true;
            matched++;
            out.push_back({type, full, lastPathSegment(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(type).gameTag);
            continue;
        }

        // Gen 1 (R/B/Y SRAM): 32KB + valid INT checksum (PKHeX SAV1).
        // Yellow is detectable from the bytes (Pikachu starter / friendship);
        // Red vs Blue are byte-identical, so the filename decides, RED default
        // (same policy as Ruby-over-Sapphire above).
        GameType gbType = GameType::RED;
        if (detectGen1Version(entry->d_name, full, gbType)) {
            int idx = static_cast<int>(gbType);
            if (claimed[idx]) {
                DebugLog::line("import scan: skip %s (doppione %s, vince il primo)",
                               entry->d_name, gameInfo(gbType).gameTag);
                continue;
            }
            claimed[idx] = true;
            matched++;
            out.push_back({gbType, full, lastPathSegment(dir)});
            DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(gbType).gameTag);
            continue;
        }

        // Gen 2 (G/S/C SRAM): checksum-driven version detection.
        GameType gbcType = GameType::GOLD;
        if (detectGen2Version(entry->d_name, full, gbcType)) {
            int idx = static_cast<int>(gbcType);
            if (claimed[idx]) {
                DebugLog::line("import scan: skip %s (doppione %s, vince il primo)",
                               entry->d_name, gameInfo(gbcType).gameTag);
                continue;
            }
            claimed[idx] = true;
            matched++;
            out.push_back({gbcType, full, lastPathSegment(dir)});
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

} // namespace

std::vector<ImportedGame> scanImportPaths(const std::vector<ImportPathEntry>& paths, bool autoCheckUsb) {
    std::vector<ImportedGame> out;
    std::vector<bool> claimed(GAME_TYPE_COUNT, false);

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

    DebugLog::line("import scan: done, %zu game(s) found", out.size());
    return out;
}
