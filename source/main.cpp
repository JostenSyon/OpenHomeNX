#include "ui.h"
#include "led.h"
#include "species_converter.h"
#include "i18n.h"
#include "debug_log.h"
#include "account.h"
#include "update_net.h"
#include "autoupdate.h"
#include "remote_sync.h"
#include "app_version.h"
#include "settings_cfg.h"

#include <switch.h>
#include <string>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>

#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif

// Se il sistema chiede di terminare l'app (es. si salta a un altro homebrew
// tramite Album mentre OpenHomeNX gira come Applicazione a pieno titolo, per
// i permessi fs sui save di sistema) e nessuno intercetta la richiesta, dopo
// un breve timeout HOS termina il processo con la forza mentre siamo in
// stato indefinito: e' quello che sembra mandare in crash hbl - che vive nel
// nostro stesso processo - in uscita (segnalato dall'utente + crash report
// Atmosphere, 2026-09-13: "User Break" dentro hbl proprio a cavallo del
// nostro exit(), innescato probabile aprendo Album/R per saltare a DBI con
// OpenHomeNX ancora aperto). Intercettando OnExitRequest usciamo dal loop in
// modo normale (SDL_QUIT, la stessa strada di un B/+ premuto dall'utente):
// backup, salvataggi, unmount e shutdown puliti prima che HOS forzi la mano.
static AppletHookCookie s_exitHookCookie;
static void onAppletExitRequest(AppletHookType hook, void* /*param*/) {
    if (hook == AppletHookType_OnExitRequest) {
        SDL_Event e{};
        e.type = SDL_QUIT;
        SDL_PushEvent(&e);
    }
}

int main(int argc, char* argv[]) {
    romfsInit();

    // Determine base path — everything (banks/, save mount "main",
    // settings.cfg, debug.enable, update/) lives next to the NRO, so it
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

    Settings::init(basePath); // settings.cfg (+ migrate legacy) prima di led/lingua
    ledInitWithPath(basePath.c_str());
    DebugLog::init(basePath);

    // Language must be known before any UI that shows translated strings
    // (e.g. the bounce "Updating..." card). Detect early — same logic as
    // the original block further below, but before ui.init/tryUpdateBounce.
    {
        std::string lang = Settings::language();
        if (lang.empty()) {
            // Nessuna scelta salvata: usa la lingua di sistema.
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
                default:                lang = "en"; break;
            }
            setExit();
        }
        i18n::init(lang);
    }

    // A pending self-update leaves OpenHomeNX.nro.new next to the NRO. This
    // boot only exists to consolidate it into the real .nro and bounce into
    // that behind the "Updating…" card — so bring up JUST the renderer, do the
    // bounce, and exit before any of the cold-boot cost (net, USB probe,
    // text-data parse, splash fade, game-icon load).
    bool pendingUpdate = false;
    {
        struct stat pst;
        pendingUpdate = (stat((basePath + "OpenHomeNX.nro.new").c_str(), &pst) == 0);
    }

    UI ui;
    uint32_t bootT0 = SDL_GetTicks();
    auto bootMark = [&](const char* what) {
        DebugLog::line("boot: +%ums %s", SDL_GetTicks() - bootT0, what);
    };
    if (!ui.init()) {
        romfsExit();
        return 1;
    }
    bootMark("ui.init");

    // SDL e' pronto solo da qui in poi: registrato dopo ui.init() cosi'
    // SDL_PushEvent() nella callback trova sempre una coda eventi valida
    // (l'hook puo' scattare da un thread di sistema in qualsiasi momento).
    appletHook(&s_exitHookCookie, onAppletExitRequest, nullptr);

    if (pendingUpdate && ui.tryUpdateBounce(basePath)) {
        ui.shutdown();
        ledExit();
        // Senza questo, l'hook OnExitRequest registrato sopra resta agganciato
        // alla sessione applet condivisa con chi ci ha chainloadati (hbloader/
        // forwarder) mentre il chainload verso il .nro vero sta per sostituire
        // questo stesso codice "usa e getta" -- sospetta causa del mancato
        // riavvio dopo un update (regressione 0.3.2, hook introdotto li').
        appletUnhook(&s_exitHookCookie);
        romfsExit();
        return 0;   // libnx exit -> loader chainloads the fresh OpenHomeNX.nro
    }

    // Show the splash as early as possible — right after ui.init() (window +
    // fonts + icons, all fast) — and keep it up (no fade) while the setup
    // below runs, so there is never a black gap between logo and app.
    ui.showSplash(2500, false);
    bootMark("splash on");

    // Rete per l'updater remoto (Layer 1). Non su un boot-bounce di update, e
    // mai fatale: se fallisce, "Check for update" resta solo SD/USB.
    bool netReady = false;

    {
        Result netRc = socketInitializeDefault();
        netReady = R_SUCCEEDED(netRc);
        DebugLog::line("socketInitializeDefault -> 0x%08X (net %s) pending=%d",
                       (unsigned)netRc, netReady ? "on" : "off", pendingUpdate ? 1 : 0);
        updateNetSetReady(netReady);
    }
    bootMark("rete");

    // Auto-check update in parallelo (solo se `auto=1` in update.cfg, mai sul
    // boot-bounce): il thread fa fetch+confronto, il boot continua subito.
    // Il prompt appare in home giochi/utenti quando il risultato è pronto.
    {
        std::string url, token, channel;
        if (!pendingUpdate && netReady && readUpdateAutoCfg(basePath, url, token, channel)) {
            bool beta = (channel == "beta") && url.empty();
            if (url.empty() && !beta)
                url = githubReleasesUrl("JostenSyon", "OpenHomeNX");
            DebugLog::line("autoupdate: background check -> %s", beta ? "beta" : url.c_str());
            autoUpdateStart(url, token, APP_VERSION, beta);
        }
    }

    // Scoperta automatica in background del device remoto (Box Remoto /
    // DevSync): NON si avvia qui apposta -- partirebbe in concorrenza con
    // l'autoupdate appena lanciato sopra, competendo per le stesse sessioni
    // di rete e rischiando di allungare proprio il check/download
    // dell'aggiornamento (bug reale, gia' visto). Parte invece da
    // UI::run(), un solo tentativo, ma solo DOPO che l'autoupdate si e'
    // sistemato (autoUpdateSettled(): mai partito, o finito comunque vada)
    // -- l'aggiornamento ha sempre la precedenza.

#ifdef OH_USB_UPDATE
    // USB Mass Storage host: lets "Check for update" scan an inserted USB drive
    // for a newer OpenHomeNX.nro. FAT/exFAT only (ISC build). Spawns one bg
    // thread; failure is non-fatal (the SD update/ folder still works).
    Result usbRc = usbHsFsInitialize(0);
    DebugLog::line("usbHsFsInitialize -> 0x%08X", (unsigned)usbRc);
    // Diagnostica a boot (solo con debug attivo, così non rallenta l'avvio
    // normale): log immediato + un retry a +2s per vedere se l'interfaccia UMS
    // si popola da sola senza intervento utente.
    if (DebugLog::enabled() && !pendingUpdate) {
        u32 phys = usbHsFsGetPhysicalDeviceCount();
        u32 n = usbHsFsGetMountedDeviceCount();
        DebugLog::line("boot USB physical=%u mounted=%u", phys, n);
        // A single fixed 2s wait wasn't enough for slower devices (e.g. an
        // NVMe enclosure doing link training + SCSI init): the library's own
        // example uses a 3s settle + an indefinite event wait, never one fixed
        // timeout. Retry up to 3x3s, bailing out early the moment something
        // mounts, so a fast drive still shows up in ~0-3s and a slow one gets
        // up to 9s total instead of being declared "not there" after 2s.
        // NEVER wait when no drive is present (phys==0): nothing can mount
        // and each wait is 3s of dead boot time (9s total — was the slow boot
        // with debug on and no USB inserted).
        UEvent* ev = usbHsFsGetStatusChangeUserEvent();
        for (int attempt = 1; phys > 0 && n == 0 && ev && attempt <= 3; attempt++) {
            waitSingle(waiterForUEvent(ev), 3000000000ULL); // 3s
            phys = usbHsFsGetPhysicalDeviceCount();
            n = usbHsFsGetMountedDeviceCount();
            DebugLog::line("boot USB +%ds (retry %d/3) physical=%u mounted=%u",
                           attempt * 3, attempt, phys, n);
        }
        if (phys > 0 && n == 0)
            DebugLog::line("boot USB: drive present but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
    }
    bootMark("usb");
#endif

    std::string savePath = basePath + "main";

    // Language already initialized early (before tryUpdateBounce) so the
    // "Updating..." card is correctly translated.

    // Load text data
    SpeciesName::load("romfs:/data/species_en.txt");
    MoveName::load("romfs:/data/moves_en.txt");
    NatureName::load("romfs:/data/natures_en.txt");
    AbilityName::load("romfs:/data/abilities_en.txt");
    ItemName::load("romfs:/data/items_en.txt");
    bootMark("nomi caricati");

    // Detect applet mode on Switch — bank-only access without save data
    {
        AppletType at = appletGetAppletType();
        if (at != AppletType_Application && at != AppletType_SystemApplication)
            ui.setAppletMode(true);
    }

    // Everything is ready: fade the splash out straight into the app.
    ui.showSplash(0, true);
    bootMark("splash off, run");

    // Run main loop — game selection, bank selection, and save loading all handled inside
    ui.run(basePath, savePath);

    // Backup all'uscita se il gioco e stato modificato (copre anche i quit
    // senza passaggio dal selettore; idempotente via sidecar).
    ui.backupOnExitIfNeeded();

    // Cleanup — prima i worker in background (se mai partiti): niente
    // socket/stringhe toccate durante lo smontaggio rete/USB.
    autoUpdateJoin();
    remoteSyncWorkerJoin();
    ui.shutdown();
    ledExit();

#ifdef OH_USB_UPDATE
    // Smonta tutto prima di usbHsFsExit: uscire con un device montato
    // crasha/hang (visto in uscita con chiavetta inserita).
    {
        u32 n = usbHsFsGetMountedDeviceCount();
        if (n > 8) n = 8;
        if (n > 0) {
            UsbHsFsDevice devs[8];
            u32 got = usbHsFsListMountedDevices(devs, n);
            for (u32 i = 0; i < got; i++)
                usbHsFsUnmountDevice(&devs[i], true);
            DebugLog::line("exit: unmounted %u usb device(s)", got);
        }
    }
    usbHsFsExit();
#endif

    if (netReady) socketExit();
    nifmExit(); // no-op se updateNetLinkStr() non ha mai inizializzato nifm:u

    appletUnhook(&s_exitHookCookie);
    romfsExit();
    DebugLog::line("exit: shutdown complete");
    return 0;
}
