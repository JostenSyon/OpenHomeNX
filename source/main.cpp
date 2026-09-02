#include "ui.h"
#include "led.h"
#include "species_converter.h"
#include "i18n.h"
#include "debug_log.h"
#include "account.h"

#include <switch.h>
#include <string>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>

#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif

int main(int argc, char* argv[]) {
    romfsInit();

#ifdef OH_USB_UPDATE
    // USB Mass Storage host: lets "Check for update" scan an inserted USB drive
    // for a newer OpenHomeNX.nro. FAT/exFAT only (ISC build). Spawns one bg
    // thread; failure is non-fatal (the SD update/ folder still works).
    usbHsFsInitialize(0);
#endif

    // Determine base path — everything (banks/, save mount "main", crypto.cfg,
    // debug.enable, themes, language.txt, update/) lives next to the NRO, so it
    // follows wherever OpenHomeNX.nro is placed (e.g. sdmc:/switch/OpenHomeNX/).
    constexpr const char* kDefaultBasePath = "sdmc:/switch/OpenHomeNX/";
    std::string basePath;
    if (argc > 0 && argv[0]) {
        basePath = argv[0];
        auto lastSlash = basePath.rfind('/');
        if (lastSlash != std::string::npos)
            basePath = basePath.substr(0, lastSlash + 1);
        else
            basePath = kDefaultBasePath;
    } else {
        basePath = kDefaultBasePath;
    }

    // One-time migration from the old pkHouse folder: if we're running from a
    // fresh location that has no banks yet but the legacy folder has data, copy
    // it over (non-destructive — the old folder is left untouched).
    {
        const std::string legacy = "sdmc:/switch/pkHouse/";
        struct stat st;
        bool newHasBanks = (stat((basePath + "banks").c_str(), &st) == 0);
        bool legacyHasBanks = (stat((legacy + "banks").c_str(), &st) == 0);
        if (basePath != legacy && !newHasBanks && legacyHasBanks)
            AccountManager::backupSaveDir(legacy, basePath);
    }

    ledInitWithPath(basePath.c_str());
    DebugLog::init(basePath);

    std::string savePath = basePath + "main";

    // Detect language: check override file first, then system setting
    {
        std::string lang = "en";
        std::string overridePath = basePath + "language.txt";
        std::ifstream ifs(overridePath);
        if (ifs.good()) {
            std::string line;
            if (std::getline(ifs, line) && !line.empty())
                lang = line;
        } else {
            setInitialize();
            u64 langCode;
            setGetSystemLanguage(&langCode);
            SetLanguage sysLang;
            setMakeLanguage(langCode, &sysLang);
            switch (sysLang) {
                case SetLanguage_JA:    lang = "ja"; break;
                case SetLanguage_FR:
                case SetLanguage_FRCA:  lang = "fr"; break;
                case SetLanguage_DE:    lang = "de"; break;
                case SetLanguage_ES:
                case SetLanguage_ES419: lang = "es"; break;
                case SetLanguage_IT:    lang = "it"; break;
                case SetLanguage_NL:    lang = "nl"; break;
                case SetLanguage_PT:
                case SetLanguage_PTBR:  lang = "pt"; break;
                case SetLanguage_RU:    lang = "ru"; break;
                // Korean and Chinese: translation files exist but are disabled
                // because PlSharedFontType_Standard lacks CJK/Korean glyphs.
                // case SetLanguage_KO:    lang = "ko"; break;
                // case SetLanguage_ZHCN:
                // case SetLanguage_ZHHANS:lang = "zh-Hans"; break;
                // case SetLanguage_ZHTW:
                // case SetLanguage_ZHHANT:lang = "zh-Hant"; break;
                default:                lang = "en"; break;
            }
            setExit();
        }
        i18n::init(lang);
    }

    // Load text data
    SpeciesName::load("romfs:/data/species_en.txt");
    MoveName::load("romfs:/data/moves_en.txt");
    NatureName::load("romfs:/data/natures_en.txt");
    AbilityName::load("romfs:/data/abilities_en.txt");
    ItemName::load("romfs:/data/items_en.txt");

    // Initialize UI first so we can show errors
    UI ui;
    if (!ui.init()) {
        romfsExit();
        return 1;
    }

    // Show splash screen while loading
    ui.showSplash();

    // Detect applet mode on Switch — bank-only access without save data
    {
        AppletType at = appletGetAppletType();
        if (at != AppletType_Application && at != AppletType_SystemApplication)
            ui.setAppletMode(true);
    }

    // Run main loop — game selection, bank selection, and save loading all handled inside
    ui.run(basePath, savePath);

    // Cleanup
    ui.shutdown();
    ledExit();

#ifdef OH_USB_UPDATE
    usbHsFsExit();
#endif

    romfsExit();
    return 0;
}
