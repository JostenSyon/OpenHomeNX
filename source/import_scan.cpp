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
#include <cerrno>

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

    std::string low = toLower(filename);
    if (low.find("sapphire") != std::string::npos) { outType = GameType::SAPPHIRE; return true; }
    if (low.find("ruby")     != std::string::npos) { outType = GameType::RUBY;     return true; }
    if (low.find("emerald")  != std::string::npos) { outType = GameType::EMERALD;  return true; }
    outType = (gameCode == 0) ? GameType::RUBY : GameType::EMERALD;
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

        // Any isImportedFile() GameType routes SaveFile::load() through the
        // same loadGBA() — which one doesn't matter yet, detectGen3Version()
        // figures out the real type from the loaded bytes below.
        SaveFile probe;
        probe.setGameType(GameType::EMERALD);
        if (!probe.load(full))
            continue; // wrong size or bad sector layout — not a Gen3 GBA save
        gbaSized++;

        GameType type;
        if (!detectGen3Version(entry->d_name, probe, type))
            continue;

        int idx = static_cast<int>(type);
        if (claimed[idx])
            continue; // first match per GameType wins
        claimed[idx] = true;
        matched++;
        out.push_back({type, full});
        DebugLog::line("import scan: %s -> %s", full.c_str(), gameInfo(type).gameTag);
    }
    closedir(d);
    DebugLog::line("import scan: %s -> %d entr(y/ies), %d file(s), %d GBA-sized, %d matched",
                    dir.c_str(), filesSeen, regularFiles, gbaSized, matched);
}

} // namespace

std::vector<ImportedGame> scanImportPaths(const std::vector<ImportPathEntry>& paths) {
    std::vector<ImportedGame> out;
    std::vector<bool> claimed(GAME_TYPE_COUNT, false);

    DebugLog::line("import scan: %zu configured path(s)", paths.size());
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
    DebugLog::line("import scan: done, %zu game(s) found", out.size());
    return out;
}
