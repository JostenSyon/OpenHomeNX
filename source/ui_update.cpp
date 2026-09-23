#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "nro_version.h"
#include "app_version.h"
#include "update_net.h"
#include "remote_sync.h"
#include "forwarder.h"
#include "settings_cfg.h"
#include "emulator.h"
#include "trade_evo.h"
#include "boxart.h"
#include "job.h"
#include "rominfo.h"
#include "pokedex.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#ifdef OH_LINUX
#include <unistd.h>
#include <sys/wait.h>
#endif
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif


namespace {
// update.cfg accanto all'NRO (o in sdmc:/switch/OpenHomeNX/). Righe key=value:
//   url=http://192.168.1.50:8000            (radice con latest.json + il .nro)
//   token=<PAT>                             (solo repo privati, header Bearer)
//   auto=1                                  (check update in parallelo al boot)
//   channel=stable|beta                     (canale update, default stable)
// Senza `url=` il check update usa le GitHub releases pubbliche
// (githubReleasesUrl sotto); Send log/save richiedono comunque `url=`
// (GitHub non riceve upload).
// REGOLA HOME (bug 2026-09-12): url+channel+backup vivono in UN solo file,
// quello con url= (attivo o .off che sia). Scrivere altrove crea un'esca
// che lo switch GitHub/Custom sovrascrive perdendo l'url per sempre.
// Etichetta corta per la sorgente update nei popup: l'URL intero sborda
// dalle card (github.../download = 60+ caratteri). Il fetch usa sempre
// l'URL completo, qui solo display.
static std::string updateSourceLabel(const std::string& url) {    auto gh = url.find("github.com/");
    if (gh != std::string::npos) {
        std::string rest = url.substr(gh + 11);
        auto slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string repo = rest.substr(slash + 1);
            auto end = repo.find('/');
            if (end != std::string::npos) repo = repo.substr(0, end);
            return "GitHub (" + rest.substr(0, slash) + "/" + repo + ")";
        }
        return "GitHub";
    }
    std::string h = url;
    auto proto = h.find("://");
    if (proto != std::string::npos) h = h.substr(proto + 3);
    auto slash = h.find('/');
    if (slash != std::string::npos) h = h.substr(0, slash);
    if (!h.empty() && h.size() <= 48) return "Rete locale (" + h + ")";
    const std::string& t = h.empty() ? url : h;
    return t.size() <= 48 ? t : "..." + t.substr(t.size() - 45);
}
// struct UpdateCfg: ora in include/ui.h (condivisa fra piu' file dopo lo split).
} // namespace

// Da qui in poi FUORI dal namespace anonimo (non piu' solo senza `static`):
// updateCfgPaths/fileHasKey/updateCfgHome/parseUpdateCfgFile/readUpdateCfg
// servono anche a ui_selectors.cpp/ui_backups.cpp/ui_settings.cpp dopo lo
// split, e l'anonymous namespace da solo le avrebbe rese comunque
// interne a questo file (il linkage non dipende solo da `static`).

// Cerca update.cfg/.off nelle due dir note (duplica findUpdateCfgFiles,
// che è membro UI definito più sotto e qui non visibile come free).
void updateCfgPaths(const std::string& basePath, std::string& cfg, std::string& off) {
    cfg.clear();
    off.clear();
    const std::string dirs[] = { basePath, "sdmc:/switch/OpenHomeNX/" };
    for (const auto& d : dirs) {
        struct stat st;
        if (cfg.empty() && stat((d + "update.cfg").c_str(), &st) == 0) cfg = d + "update.cfg";
        if (off.empty() && stat((d + "update.cfg.off").c_str(), &st) == 0) off = d + "update.cfg.off";
    }
}

// true se il file contiene una riga url= (anche vuota? no: chiave presente).
bool fileHasKey(const std::string& path, const std::string& want) {
    std::ifstream f(path);
    if (!f.good()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        std::string k = (eq == std::string::npos) ? line : line.substr(0, eq);
        while (!k.empty() && (k.front() == ' ' || k.front() == '\t')) k.erase(k.begin());
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (k == want) return true;
    }
    return false;
}

// Il file home della config (vedi REGOLA HOME sopra): quello con url=,
// attivo o spento che sia; altrimenti l'attivo; altrimenti path da creare.
std::string updateCfgHome(const std::string& basePath) {
    std::string cfg, off;
    updateCfgPaths(basePath, cfg, off);
    if (!off.empty() && fileHasKey(off, "url") && (cfg.empty() || !fileHasKey(cfg, "url")))
        return off;
    if (!cfg.empty())
        return cfg;
    if (!off.empty())
        return off;
    return basePath + "update.cfg";
}

// Parsa un file cfg in out; se keysOnlyChannel, prende solo channel
// (per l'altro file: non deve mai sovrascrivere la home).
void parseUpdateCfgFile(const std::string& path, UpdateCfg& out, bool channelOnly) {
    std::ifstream f(path);
    if (!f.good()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.pop_back();
        };
        trim(k); trim(v);
        if (channelOnly) {
            if (k == "channel" && out.channel.empty()) out.channel = v;
            continue;
        }
        if (k == "url") out.url = v;
        else if (k == "token") out.token = v;
        else if (k == "channel") out.channel = v;
        else if (k == "backup_mb") out.backupMb = std::atol(v.c_str());
        else if (k == "backup_mb_sd") out.backupMbSd = std::atol(v.c_str());
        else if (k == "auto") out.autoOn = (v == "1" || v == "on" || v == "yes" || v == "true");
    }
}

bool readUpdateCfg(const std::string& basePath, UpdateCfg& out) {
    // Precedenza: prima il file ATTIVO per intero (solo lui decide url e
    // modalità GitHub/custom: l'url di un .off non deve mai riattivarsi da
    // solo), poi l'altro file SOLO per il channel mancante. Un'esca senza
    // url non sovrascrive mai nulla (bug 2026-09-12).
    std::string cfg, off;
    updateCfgPaths(basePath, cfg, off);
    if (!cfg.empty()) parseUpdateCfgFile(cfg, out, false);
    if (!off.empty()) {
        parseUpdateCfgFile(off, out, true); // channel: fallback se l'attivo non lo ha
        // 2026-09-19: "auto" (check aggiornamenti al boot) è una preferenza
        // dell'utente indipendente dalla sorgente (url/GitHub vs custom) --
        // ma updateCfgHome() sceglie come "home" il file con l'url quando
        // esiste (per non riattivare mai un url da un .off), quindi
        // writeUpdateCfgKey("auto", ...) in modalità GitHub finiva scritto
        // nel .off (l'unico file con url in quel momento). channelOnly sopra
        // legge SOLO channel da quel file, ignorando "auto": il toggle
        // risultava sempre bloccato su ON, mai spegnibile con sorgente
        // GitHub, perché la scrittura c'era ma la lettura non la vedeva mai.
        // Fix: se l'attivo non specifica "auto" esplicitamente, recuperalo
        // dal .off con un parse isolato (mai in "out" direttamente, per non
        // fargli sovrascrivere url/channel/token già decisi sopra).
        if (cfg.empty() || !fileHasKey(cfg, "auto")) {
            UpdateCfg offAuto;
            parseUpdateCfgFile(off, offAuto, false);
            out.autoOn = offAuto.autoOn;
        }
    }
    return !out.url.empty();
}

// Consolidate an update left as OpenHomeNX.nro.new by the previous run.
// After envSetNextLoad(.new) + restart we are executing from .new, so the
// canonical .nro is not in use and can be overwritten with a plain copy
// (a rename of the in-use file is what failed before). MUST run at every
// boot, not only when the user opens "Check for update" — otherwise .nro
// stays stale (hbmenu keeps launching the old build) and the residual
// nextLoad keeps relaunching .new, which looks like a double restart.
bool UI::finalizePendingUpdate() {
    const std::string runningNro = basePath_ + "OpenHomeNX.nro";
    const std::string pending = runningNro + ".new";
    struct stat st;
    if (stat(pending.c_str(), &st) != 0)
        return false;                   // nothing pending

    // NOTE: never call envSetNextLoad("", "") to "clear" a pending nextLoad.
    // Setting it to an empty path makes hbloader try to chainload "" on the
    // next exit, which is the fatal-error ("ugly crash") screen the user saw.
    // And a pending nextLoad->.new is NOT consumed by chainloading (verified
    // 2026-09-08: without the bounce every exit relaunched .new) — it must be
    // overwritten with the canonical .nro, which is exactly what the bounce does.

    std::string pendVer;
    if (!readNroDisplayVersion(pending, pendVer)) {
        // NACP non leggibile: NON cancellare (era la causa di "aggiorna, riavvia,
        // ma sono ancora alla vecchia versione" — un read transitorio buttava via
        // l'update). Se il file ha una dimensione plausibile lo finalizziamo lo
        // stesso; lo scartiamo solo se è vuoto/minuscolo o se la copia fallisce.
        if (st.st_size < 1024 * 1024) {
            DebugLog::line("update: pending %s illeggibile e troppo piccolo (%lld B) -> rimuovo",
                           pending.c_str(), (long long)st.st_size);
            std::remove(pending.c_str());
            return false;
        }
        DebugLog::line("update: pending %s NACP illeggibile (%lld B) -> finalizzo comunque",
                       pending.c_str(), (long long)st.st_size);
        pendVer = "?";
        if (copyFileTo(pending, runningNro)) {
            DebugLog::line("update: finalized (blind) %s -> %s", pending.c_str(), runningNro.c_str());
            if (std::remove(pending.c_str()) != 0)
                DebugLog::line("update: .new is the running image, cleaned next boot");
            if (envHasNextLoad()) { envSetNextLoad(runningNro.c_str(), runningNro.c_str()); return true; }
            return false;
        }
        if (envHasNextLoad()) envSetNextLoad(pending.c_str(), pending.c_str());
        return false;
    }

    // Same version in canonical and pending: either a stale leftover (a previous
    // finalize succeeded but .new couldn't be deleted) or a genuine same-version
    // reinstall. Tell them apart WITHOUT trusting byte size (NRO sizes are
    // page-aligned: different builds often compare equal, so size match proves
    // nothing): if .new can be unlinked it was stale — drop it. If not, we ARE
    // the throw-away .new, so fall through and finalize for real (copy + bounce).
    // NOTE: version compare only, never size. Same-version reinstall MUST apply.
    std::string nroVer;
    bool nroReadable = readNroDisplayVersion(runningNro, nroVer);
    if (nroReadable && nroVer == pendVer) {
        if (std::remove(pending.c_str()) == 0) {
            DebugLog::line("update: canonical already v%s, stale .new removed",
                           nroVer.c_str());
            return false;
        }
        DebugLog::line("update: same-version reinstall v%s, finalizing for real (size ignored)",
                       nroVer.c_str());
        // fall through to copyFileTo below: canonical isn't in use from here,
        // so the copy lands; the bounce + next-boot cleanup handle the rest.
    }

    if (copyFileTo(pending, runningNro)) {
        DebugLog::line("update: finalized pending %s -> %s v%s (copy)",
                       pending.c_str(), runningNro.c_str(), pendVer.c_str());
        // Try to drop the sidecar. If we ARE .new (Horizon refuses to unlink
        // the running image) it stays and the next canonical boot removes it
        // via the same-version branch above (remove succeeds from there).
        if (std::remove(pending.c_str()) != 0)
            DebugLog::line("update: .new is the running image, cleaned next boot");
        // Bounce into the canonical .nro (now the new build) behind the
        // "Updating…" mask. This *is* a second restart, but the throw-away
        // .new boot that runs it is stripped to the bone (see main.cpp:
        // no net/USB/text-data/splash), so it's a quick flash, not a full
        // second app launch. The bounce also overwrites the stale
        // nextLoad->.new: without it every exit chainloads .new again
        // (verified 2026-09-08: exit rebooted instead of quitting).
        if (envHasNextLoad()) {
            envSetNextLoad(runningNro.c_str(), runningNro.c_str());
            return true;
        }
        return false;
    }

    // We are running from the canonical .nro (hbloader ignored the nextLoad,
    // or the user relaunched from hbmenu), so it is in use and can't be
    // overwritten. Re-arm the nextLoad so the next restart lands on .new,
    // where this boot-time finalize can consolidate it. Keep .new.
    DebugLog::line("update: canonical %s in use, re-arming nextLoad -> %s",
                   runningNro.c_str(), pending.c_str());
    if (envHasNextLoad()) envSetNextLoad(pending.c_str(), pending.c_str());
    return false;
}

// Called from main() BEFORE net/USB/text-data/splash when OpenHomeNX.nro.new is
// present. Consolidates the update into OpenHomeNX.nro; if a bounce into the
// fresh .nro is armed, flashes the "Updating…" card and returns true so main()
// exits straight away (libnx then chainloads the nextLoad). Returns false when
// there is nothing to bounce (a stale .new was just cleared) — main() then
// continues a normal boot. Needs init() (renderer) already done.
bool UI::tryUpdateBounce(const std::string& basePath) {
    basePath_ = basePath;
    // Card PRIMA della copia finalize (18MB a schermo nero sembravano un hang).
    showWorking(i18n::get(StrKey::UpdateUpdating));
    if (!finalizePendingUpdate())
        return false;
    // finalizePendingUpdate() armed envSetNextLoad(the real .nro). Draw one
    // frame of the card so the chainload isn't a black gap, then let main exit.
    showWorking(i18n::get(StrKey::UpdateUpdating));
    SDL_Delay(150);
    return true;
}

// Prima di scaricare dalla rete, rimuove eventuali .nro stantii lasciati da
// update precedenti — SOLO file che si chiamano esattamente "OpenHomeNX.nro"
// (mai update.nro, mai l'nro in uso). Evita che un vecchio file in update/ o
// in root venga consumato/installato al posto del download fresco.
static void removeStaleLocalUpdates(const std::string& basePath, const std::string& runningNro) {
    std::vector<std::string> paths = {
        basePath + "update/OpenHomeNX.nro",
        "sdmc:/switch/OpenHomeNX/update/OpenHomeNX.nro",
        "sdmc:/OpenHomeNX.nro",
    };
    for (auto& p : paths) {
        if (p == runningNro) continue;
        auto slash = p.rfind('/');
        std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        if (base != "OpenHomeNX.nro") continue; // safety: solo quel basename
        if (std::remove(p.c_str()) == 0)
            DebugLog::line("update: rimosso stale %s", p.c_str());
    }
}

void UI::pumpJobCancel(BackgroundJob& job, bool& cancel, const std::string& idle) {
    std::string hint = i18n::get(StrKey::JobCancelHint);
    std::string cancelling = i18n::get(StrKey::JobCancelling);
    std::string line;
    while (!job.done()) {
        job.poll(line);
        if (!cancel) {
            std::string msg = line.empty() ? idle : line;
            size_t nl = msg.find('\n');
            if (nl != std::string::npos) msg.insert(nl, "  " + hint);
            else msg += "  " + hint;
            showWorking(msg);
        } else {
            showWorking(cancelling);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                cancel = true;
                SDL_PushEvent(&e); // non mangiarla: la vede il loop esterno
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                       e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                cancel = true;
            }
        }
        SDL_Delay(16);
    }
    job.join();
}

UI::NetCancelResult UI::fetchInfoCancel(const std::string& url, const std::string& token,
                                       RemoteUpdateInfo& info, std::string& err,
                                       const char* infoFile) {
    bool cancel = false;
    bool ok = false;
    BackgroundJob job;
    if (!job.start([&](BackgroundJob&) {
            ok = updateNetFetchInfo(url, token, info, err, &cancel, infoFile);
        })) {
        DebugLog::line("update: job.start fallita, fetch sincrona");
        return updateNetFetchInfo(url, token, info, err, nullptr, infoFile) ? NetCancelResult::Ok
                                                                            : NetCancelResult::Failed;
    }
    pumpJobCancel(job, cancel, i18n::fmt(StrKey::UpdateContacting, updateSourceLabel(url)));
    if (cancel) { DebugLog::line("update: fetch info annullata"); return NetCancelResult::Cancelled; }
    return ok ? NetCancelResult::Ok : NetCancelResult::Failed;
}

UI::NetCancelResult UI::fetchBetaCancel(const std::string& token,
                                       std::string& outBase, std::string& outTag, std::string& err) {
    bool cancel = false;
    bool ok = false;
    BackgroundJob job;
    if (!job.start([&](BackgroundJob&) {
            ok = updateNetFetchBetaBase("JostenSyon", "OpenHomeNX", token, outBase, outTag, err, &cancel);
        })) {
        DebugLog::line("update: job.start fallita, fetch beta sincrona");
        return updateNetFetchBetaBase("JostenSyon", "OpenHomeNX", token, outBase, outTag, err)
                   ? NetCancelResult::Ok
                   : NetCancelResult::Failed;
    }
    pumpJobCancel(job, cancel, i18n::fmt(StrKey::UpdateContacting, "GitHub beta"));
    if (cancel) { DebugLog::line("update: fetch beta annullata"); return NetCancelResult::Cancelled; }
    return ok ? NetCancelResult::Ok : NetCancelResult::Failed;
}

UI::NetCancelResult UI::downloadFileCancel(const std::string& url, const std::string& token,
                                         const std::string& dst, const std::string& sha,
                                         const std::string& ver, std::string& err) {
    bool cancel = false;
    bool ok = false;
    BackgroundJob job;
    if (!job.start([&](BackgroundJob& j) {
            ok = updateNetDownload(url, token, dst, sha, err,
                [&](const std::string& s){ j.report(s); }, &cancel);
        })) {
        DebugLog::line("update: job.start fallita, download sincrono");
        return updateNetDownload(url, token, dst, sha, err,
                   [this](const std::string& s){ showWorking(s); })
                   ? NetCancelResult::Ok
                   : NetCancelResult::Failed;
    }
    pumpJobCancel(job, cancel, i18n::fmt(StrKey::UpdateDownloading, ver));
    if (cancel) {
        DebugLog::line("update: download v%s annullato, binario vecchio intatto", ver.c_str());
        return NetCancelResult::Cancelled;
    }
    return ok ? NetCancelResult::Ok : NetCancelResult::Failed;
}

#ifdef OH_LINUX
// Scompatta uno zip sopra la root (percorsi home/ark/... dentro l'archivio).
// fork+exec di /usr/bin/unzip (presente su ArkOS), niente shell.
static bool unzipToRoot(const std::string& zipPath, std::string& err) {
    pid_t pid = fork();
    if (pid < 0) { err = "fork fallita"; return false; }
    if (pid == 0) {
        execl("/usr/bin/unzip", "unzip", "-o", "-q", zipPath.c_str(), "-d", "/", (char*)nullptr);
        _exit(127); // execl fallita
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        err = "unzip rc=" + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}

// Updater R36S: manifest latest-r36s.json (stessa release GitHub, asset
// dedicato), zip scaricato con B-annulla, sha verificata dal download,
// unzip in place (binario + romfs + script, niente staleness), exit 42 per
// il rilancio dal launcher. Il binario in esecuzione non viene toccato
// finche' unzip non lo sovrascrive a freddo (inode nuovo, processo al sicuro).
bool UI::checkForUpdateR36S(const std::string& curVer) {
    UpdateCfg cfg;
    readUpdateCfg(basePath_, cfg);
    std::string netUrl;
    if (!cfg.url.empty()) {
        netUrl = cfg.url;
    } else if (cfg.channel == "beta") {
        std::string betaBase, betaTag, betaErr;
        if (!updateNetEnsureReady()) {
            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                i18n::fmt(StrKey::UpdateNetOff, "GitHub beta"));
            return false;
        }
        NetCancelResult bfr = fetchBetaCancel(cfg.token, betaBase, betaTag, betaErr);
        if (bfr == NetCancelResult::Cancelled) return false;
        if (bfr == NetCancelResult::Failed) {
            if (betaErr == "none") {
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateBetaNone, curVer));
            } else {
                DebugLog::line("update: beta resolve fallito: %s", betaErr.c_str());
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateUnreachable, betaErr, "GitHub beta"));
            }
            return false;
        }
        netUrl = betaBase;
    } else {
        netUrl = githubReleasesUrl("JostenSyon", "OpenHomeNX");
    }
    DebugLog::line("update: r36s url=%s token=%s", netUrl.c_str(), cfg.token.empty() ? "no" : "yes");
    if (!updateNetEnsureReady()) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateNetOff, updateSourceLabel(netUrl)));
        return false;
    }
    RemoteUpdateInfo info;
    std::string err;
    NetCancelResult ifr = fetchInfoCancel(netUrl, cfg.token, info, err, "latest-r36s.json");
    if (ifr == NetCancelResult::Cancelled) return false;
    if (ifr == NetCancelResult::Failed) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateUnreachable, err, updateSourceLabel(netUrl)));
        return false;
    }
    int cmp = compareVersionStrings(info.version, curVer);
    DebugLog::line("update: r36s remoto v%s cmp=%d", info.version.c_str(), cmp);
    if (cmp > 0) {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailNetTitle),
                i18n::fmt(StrKey::UpdateAvailNetBody, info.version, curVer, updateSourceLabel(netUrl))))
            return false;
    } else if (cmp == 0 && DebugLog::enabled()) {
        // Reinstall stessa versione (debug): come Switch — confronta gli
        // sha live solo per dirtelo, ma chiede sempre se reinstallare.
        std::string localShort = "?", remoteShort = "?";
        if (!info.sha256.empty()) {
            showWorking(i18n::fmt(StrKey::UpdateContacting, "sha…"));
            std::string local = sha256HexFile(basePath_ + "OpenHomeNX");
            localShort = local.empty() ? "?" : local.substr(0, 8);
            remoteShort = info.sha256.substr(0, 8);
            DebugLog::line("update: r36s sha local=%s remote=%.16s same=%d",
                local.empty() ? "(unreadable)" : local.c_str(),
                info.sha256.c_str(), local == info.sha256 ? 1 : 0);
        }
        if (!showConfirmDialog(i18n::get(StrKey::UpdateSameDbgTitle),
                i18n::fmt(StrKey::UpdateSameDbgBody, curVer, localShort,
                          info.version, remoteShort)))
            return false;
    } else {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateLatestBody, curVer, info.version));
        return false;
    }
    const std::string dst = basePath_ + "update/OpenHomeNX-r36s.zip";
    NetCancelResult dlr = downloadFileCancel(info.nroUrl, cfg.token, dst, info.sha256, info.version, err);
    if (dlr == NetCancelResult::Cancelled) return false;
    if (dlr == NetCancelResult::Failed) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateDlFailed, err));
        return false;
    }
    showWorking(i18n::get(StrKey::UpdateUpdating));
    if (!unzipToRoot(dst, err)) {
        DebugLog::line("update: r36s unzip fallito: %s", err.c_str());
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateDlFailed, err));
        return false;
    }
    std::remove(dst.c_str());
    DebugLog::line("update: r36s zip v%s installato, exit 42", info.version.c_str());
    showWorking(i18n::fmt(StrKey::UpdateUpdatingTo, info.version));
    SDL_Delay(700);
    setExitCode(42);
    return true; // caller stops the loop -> main() returns 42 -> launcher rilancia
}
#endif

bool UI::checkForUpdate(bool usbOnly) {
    const std::string runningNro = basePath_ + "OpenHomeNX.nro";
    finalizePendingUpdate();
    const std::string curVer =
#ifdef APP_VERSION
        APP_VERSION;
#else
        "0.0.0";
#endif
    DebugLog::line("update: check start, running v%s, base=%s, applet=%d",
                   curVer.c_str(), basePath_.c_str(), (int)appletMode_);
#ifdef OH_LINUX
    // R36S: niente NRO locali ne' chainloader — manifest zip + unzip + exit 42.
    return checkForUpdateR36S(curVer);
#endif

    // Candidate NRO locations, checked in order. USB drives (FAT/exFAT) come
    // first when built with OH_USB_UPDATE; the SD "update/" folder always works.
    std::vector<std::string> candidates;
#ifdef OH_USB_UPDATE
    {
        // Instant check only — never wait/retry here. USB drives are handled
        // by the hotplug poll in run() (which re-scans import paths and, once
        // per session, calls this same function right after a rising edge —
        // by then the drive is already mounted, so n reflects it immediately).
        // Waiting here too used to make the manual "Check for Update" menu
        // entry slow for no reason: this menu isn't when a drive gets
        // detected, only when the user asks "is there an update", and that
        // question should answer from SD/network without a multi-second USB
        // stall (explicit user request 2026-09-05).
        u32 phys = usbHsFsGetPhysicalDeviceCount();
        u32 n = usbHsFsGetMountedDeviceCount();
        DebugLog::line("update: USB physical=%u mounted=%u", phys, n);
        if (DebugLog::enabled() && phys > 0 && n == 0)
            DebugLog::line("update: USB drive seen but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
        if (n > 0) {
            if (n > 8) n = 8;
            std::vector<UsbHsFsDevice> devs(n);
            u32 got = usbHsFsListMountedDevices(devs.data(), n);
            for (u32 i = 0; i < got; i++) {
                DebugLog::line("update: UMS[%u] name='%s' fs=%u cap=%llu",
                               i, devs[i].name, (unsigned)devs[i].fs_type,
                               (unsigned long long)devs[i].capacity);
                std::string mnt = devs[i].name; // e.g. "ums0:"
                candidates.push_back(mnt + "/OpenHomeNX.nro");
                candidates.push_back(mnt + "/switch/OpenHomeNX/OpenHomeNX.nro");
            }
        }
    }
#endif
    // Auto-check USB: solo candidati USB, niente SD e niente rete dopo.
    if (!usbOnly) {
        candidates.push_back(basePath_ + "update/OpenHomeNX.nro");
        candidates.push_back("sdmc:/switch/OpenHomeNX/update/OpenHomeNX.nro");
        // SD root (richiesta utente: butta direttamente in sdmc:/)
        candidates.push_back("sdmc:/OpenHomeNX.nro");
        candidates.push_back("sdmc:/OpenHomeNX/update.nro");
    }
    // Dedup (basePath_ è spesso già sdmc:/switch/OpenHomeNX/) e mai il file in uso.
    {
        std::vector<std::string> uniq;
        for (auto& c : candidates)
            if (c != runningNro && std::find(uniq.begin(), uniq.end(), c) == uniq.end())
                uniq.push_back(c);
        candidates.swap(uniq);
    }

    std::string foundPath, foundVer;
    int foundCmp = 0;
    for (const auto& c : candidates) {
        std::string v;
        bool ok = readNroDisplayVersion(c, v);
        int cmp = ok ? compareVersionStrings(v, curVer) : 0;
        DebugLog::line("update: try '%s' -> read=%d ver='%s' cmp=%d",
                       c.c_str(), (int)ok, ok ? v.c_str() : "", cmp);
        if (ok) {
            foundPath = c;
            foundVer = v;
            foundCmp = cmp;
            if (cmp > 0) break; // prefer newer, ma tieni anche older per prompt
        }
    }
    DebugLog::line("update: result found='%s' v%s cmp=%d",
                   foundPath.empty() ? "(none)" : foundPath.c_str(),
                   foundVer.c_str(), foundCmp);

    if (usbOnly && foundCmp <= 0) {
        // Auto-trigger su inserimento: parla solo se l'USB ha davvero un
        // update (newer). Vuoto/older/same = silenzio totale, niente rete,
        // niente dialoghi informativi.
        DebugLog::line("update: USB auto-check, nothing newer -> silent");
        return false;
    }

    // Layer 1 — sorgente di rete. Solo se nessuna build LOCALE più recente è
    // già stata trovata (una .nro locale più nuova vince senza toccare la rete).
    // URL: update.cfg `url=` se presente, altrimenti le release GitHub
    // pubbliche. Mai in modo usbOnly.
    bool fromNet = false;
    if (!usbOnly && foundCmp <= 0) {
        UpdateCfg cfg;
        readUpdateCfg(basePath_, cfg);
        std::string netUrl;
        if (!cfg.url.empty()) {
            netUrl = cfg.url; // custom vince sempre (anche in beta)
        } else if (cfg.channel == "beta") {
            // Canale beta: prima risolvi la pre-release corrente via API.
            // Mai fallback silenzioso sullo stabile: se fallisce lo dici.
            std::string betaBase, betaTag, betaErr;
            if (!updateNetEnsureReady()) {
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateNetOff, "GitHub beta"));
                return false;
            }
            NetCancelResult bfr = fetchBetaCancel(cfg.token, betaBase, betaTag, betaErr);
            if (bfr == NetCancelResult::Cancelled) return false;
            if (bfr == NetCancelResult::Failed) {
                if (betaErr == "none") {
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateBetaNone, curVer));
                } else {
                    DebugLog::line("update: beta resolve fallito: %s", betaErr.c_str());
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateUnreachable, betaErr, "GitHub beta"));
                }
                return false;
            }
            netUrl = betaBase;
        } else {
            netUrl = githubReleasesUrl("JostenSyon", "OpenHomeNX");
        }
        {
            DebugLog::line("update: net url=%s token=%s", netUrl.c_str(),
                           cfg.token.empty() ? "no" : "yes");
            if (!updateNetEnsureReady()) {
                DebugLog::line("update: rete non disponibile, salto Layer 1");
                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                    i18n::fmt(StrKey::UpdateNetOff, updateSourceLabel(netUrl)));
            } else {
                RemoteUpdateInfo info;
                std::string err;
                NetCancelResult ifr = fetchInfoCancel(netUrl, cfg.token, info, err);
                if (ifr == NetCancelResult::Cancelled) return false;
                if (ifr == NetCancelResult::Failed) {
                    DebugLog::line("update: fetch info fallito: %s", err.c_str());
                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                        i18n::fmt(StrKey::UpdateUnreachable, err, updateSourceLabel(netUrl)));
                } else {
                    int cmp = compareVersionStrings(info.version, curVer);
                    DebugLog::line("update: remoto v%s cmp=%d", info.version.c_str(), cmp);
                    if (cmp > 0) {
                        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailNetTitle),
                                i18n::fmt(StrKey::UpdateAvailNetBody, info.version, curVer, updateSourceLabel(netUrl))))
                            return false;
                        removeStaleLocalUpdates(basePath_, runningNro);
                        const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                        NetCancelResult dlr = downloadFileCancel(info.nroUrl, cfg.token, dst,
                                                                info.sha256, info.version, err);
                        if (dlr == NetCancelResult::Cancelled) return false;
                        if (dlr == NetCancelResult::Failed) {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateDlFailed, err));
                            return false;
                        }
                        foundPath = dst;
                        foundVer = info.version;
                        foundCmp = 1;
                        fromNet = true;
                    } else if (cmp < 0) {
                        // Downgrade: la rete e piu vecchia. Mai silenzioso:
                        // solo debug offre installazione esplicita.
                        DebugLog::line("update: downgrade remoto v%s < v%s",
                            info.version.c_str(), curVer.c_str());
                        if (DebugLog::enabled()) {
                            if (!showConfirmDialog(i18n::get(StrKey::UpdateDowngradeTitle),
                                    i18n::fmt(StrKey::UpdateDowngradeBody, info.version, curVer,
                                              updateSourceLabel(netUrl))))
                                return false;
                            removeStaleLocalUpdates(basePath_, runningNro);
                            const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                            NetCancelResult dlr = downloadFileCancel(info.nroUrl, cfg.token, dst,
                                                                    info.sha256, info.version, err);
                            if (dlr == NetCancelResult::Cancelled) return false;
                            if (dlr == NetCancelResult::Failed) {
                                showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                    i18n::fmt(StrKey::UpdateDlFailed, err));
                                return false;
                            }
                            foundPath = dst;
                            foundVer = info.version;
                            foundCmp = -1;
                            fromNet = true;
                        } else {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateLatestBody, curVer, info.version));
                            return false;
                        }
                    } else {
                        // La rete ha risposto e non c'è niente di più recente:
                        // con debug attivo confronta lo SHA live solo per
                        // dirtelo (stessi bit o no), ma chiede SEMPRE se
                        // reinstallare — mai skip automatico.
                        if (DebugLog::enabled()) {
                            std::string localShort = "?", remoteShort = "?";
                            if (!info.sha256.empty()) {
                                showWorking(i18n::fmt(StrKey::UpdateContacting, "sha…"));
                                std::string local = sha256HexFile(runningNro);
                                localShort = local.empty() ? "?" : local.substr(0, 8);
                                remoteShort = info.sha256.substr(0, 8);
                                DebugLog::line("update: sha local=%s remote=%.16s same=%d",
                                    local.empty() ? "(unreadable)" : local.c_str(),
                                    info.sha256.c_str(), local == info.sha256 ? 1 : 0);
                            }
                            if (showConfirmDialog(i18n::get(StrKey::UpdateSameDbgTitle),
                                    i18n::fmt(StrKey::UpdateSameDbgBody, curVer, localShort,
                                              info.version, remoteShort))) {
                                removeStaleLocalUpdates(basePath_, runningNro);
                                const std::string dst = basePath_ + "update/OpenHomeNX.nro";
                                NetCancelResult dlr = downloadFileCancel(info.nroUrl, cfg.token, dst,
                                                                        info.sha256, info.version, err);
                                if (dlr == NetCancelResult::Cancelled) return false;
                                if (dlr == NetCancelResult::Failed) {
                                    showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                        i18n::fmt(StrKey::UpdateDlFailed, err));
                                    return false;
                                }
                                foundPath = dst; foundVer = info.version; foundCmp = 0; fromNet = true;
                            } else {
                                return false;
                            }
                        } else {
                            showMessageAndWait(i18n::get(StrKey::UpdateTitle),
                                i18n::fmt(StrKey::UpdateLatestBody, curVer, info.version));
                            return false;
                        }
                    }
                }
            }
        }
    }

    if (foundPath.empty()) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle),
            i18n::fmt(StrKey::UpdateNoBuildBody, curVer));
        return false;
    }

    if (fromNet) {
        // già confermato prima del download — niente doppio prompt
    } else if (foundCmp > 0) {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateAvailTitle),
                i18n::fmt(StrKey::UpdateAvailBody, foundVer, curVer, foundPath)))
            return false;
    } else if (foundCmp == 0) {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateSameTitle),
                i18n::fmt(StrKey::UpdateSameBody, foundVer, curVer, foundPath)))
            return false;
    } else {
        if (!showConfirmDialog(i18n::get(StrKey::UpdateDowngradeTitle),
                i18n::fmt(StrKey::UpdateDowngradeBody, foundVer, curVer, foundPath)))
            return false;
    }

    showWorking(i18n::get(StrKey::UpdateUpdating));
    const std::string tmp = runningNro + ".new";
    std::remove(tmp.c_str());
    if (!copyFileTo(foundPath, tmp)) {
        std::remove(tmp.c_str());
        showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::get(StrKey::UpdateCopyFailed));
        return false;
    }

    // Consume-once: an update dropped somewhere on the SD (root or an update/
    // folder) is removed now that its bytes are safe in the pending .new file,
    // so it doesn't re-trigger the prompt on every boot. Never touch the
    // running NRO itself, nor files on a USB drive (external master copy).
    if (foundPath != runningNro && foundPath.rfind("sdmc:/", 0) == 0) {
        if (std::remove(foundPath.c_str()) == 0)
            DebugLog::line("update: consumed source %s (removed)", foundPath.c_str());
        else
            DebugLog::line("update: could not remove source %s", foundPath.c_str());
    }
    // Preferred path: overwrite the canonical .nro *in place* from the good
    // copy we just wrote. fopen("wb") truncates+rewrites the same directory
    // entry, so it works even while a forwarder holds the file open — unlike
    // rename()/remove(), which return EBUSY on FAT for the in-use NRO. The
    // running code is already in RAM, so truncating the on-disk file is safe.
    // This gives a SINGLE restart: exit -> forwarder relaunches its target
    // .nro, now the new build. No .new sidecar left to trip a second restart.
    if (copyFileTo(tmp, runningNro)) {
        DebugLog::line("update: overwrote %s in place -> v%s", runningNro.c_str(), foundVer.c_str());
        std::remove(tmp.c_str());
        if (envHasNextLoad()) envSetNextLoad(runningNro.c_str(), runningNro.c_str());
        // Auto-bounce, no button press: mirrors the boot-time finalize screen so
        // the whole update is a couple of "Updating…" frames, not taps.
        showWorking(i18n::fmt(StrKey::UpdateUpdatingTo, foundVer));
        SDL_Delay(700);
        return true;
    }

    // Fallback: in-place overwrite refused. Chainload the .new sidecar and let
    // the boot-time finalizePendingUpdate() consolidate .nro on the next run.
    // On a forwarder this may cost a second restart, but the update still lands.
    DebugLog::line("update: in-place overwrite of %s failed, using .new sidecar", runningNro.c_str());
    if (envHasNextLoad()) {
        envSetNextLoad(tmp.c_str(), tmp.c_str());
        DebugLog::line("update: nextLoad -> %s", tmp.c_str());
        // Auto-bounce. The .new instance's boot-time finalize shows its own
        // brief "Updating…" and bounces again into the real .nro — no taps.
        showWorking(i18n::fmt(StrKey::UpdateUpdatingTo, foundVer));
        SDL_Delay(700);
        return true; // caller stops the loop -> main() returns -> hbloader relaunches
    }
    std::remove(runningNro.c_str());
    if (std::rename(tmp.c_str(), runningNro.c_str()) != 0) {
        showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::get(StrKey::UpdateReplaceFailed));
        return false;
    }
    showMessageAndWait(i18n::get(StrKey::UpdateTitle), i18n::fmt(StrKey::UpdateInstalled, foundVer));
    return false;
}
