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
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif


bool UI::remoteSyncEnsureLogin(const std::string& title, std::string& host,
                                std::string& user, std::string& pass, std::string& token) {
    host = Settings::remoteSyncHost();
    user = Settings::remoteSyncUser();
    pass = Settings::remoteSyncPass();
    std::string err;
    bool ok = false;

    // 1) Host gia' noto (stesso device di prima): riprova diretto, e' il
    //    caso comune -- zero attesa di scansione. Se il token è ancora valido
    //    (JWT 2h) lo riusiamo senza rifare login.
    if (!host.empty()) {
        if (remoteSyncGetCachedToken(host, token)) {
            ok = true;
            DebugLog::line("remote sync: token cached riusato per %s (no login)", host.c_str());
        } else {
            ok = remoteSyncLogin(host, user, pass, token, err);
            if (!ok) {
                DebugLog::line("remote sync: login FALLITO su host noto %s: %s", host.c_str(), err.c_str());
            }
        }
        if (ok) {
            // Login ok (o token riusato) ma potrebbe essere un altro device (router che risponde
            // 200 con token fake) -- verifico subito che la root sia listabile,
            // altrimenti invalido e passo alla scansione (gestisce IP cambiato
            // es. 192.168.4.5 → 192.168.4.28).
            std::vector<RemoteEntry> probeEntries;
            std::string probeErr;
            if (!remoteSyncListPath(host, token, "", probeEntries, probeErr)) {
                DebugLog::line("remote sync: host noto %s login ok ma root non listabile (%s) → invalido, provo scansione", host.c_str(), probeErr.c_str());
                ok = false;
            }
        }
    }

    // 2) Host sconosciuto o non risponde piu' (es. IP cambiato via DHCP):
    //    scansione automatica della LAN che prova il login vero su OGNI
    //    host che risponde sulla porta 80, non solo il primo -- su una rete
    //    con piu' web-server (router, NAS, ecc.) il primo a rispondere e'
    //    quasi sempre il router e non e' un Filebrowser: fermarsi li' era
    //    il bug segnalato (login sempre rifiutato, nessun modo di andare
    //    oltre). Si ferma solo al primo che risponde 200 al login.
    //    Annullabile con B (vedi remote_sync.cpp).
    std::string lastScanErr;
    if (!ok) {
        std::string scanErr, foundHost, foundToken;
        // Popup con barra di avanzamento durante lo scan (che resta
        // sincrono -- nessun thread nuovo, vedi il commento su onProgress
        // in remote_sync.h): stesso schema gia' usato per il progresso di
        // download degli aggiornamenti (vedi le chiamate a showWorking nel
        // flusso di UpdateNetDownload piu' sotto in questo file), cosi'
        // l'utente vede la ricerca avanzare invece di uno schermo fermo
        // per i secondi che dura.
        if (remoteSyncScanLan(user, pass, foundHost, foundToken, scanErr,
                               [this](const std::string& s) { showWorking(s); })) {
            host = foundHost;
            token = foundToken;
            ok = true;
        } else {
            DebugLog::line("remote sync: scansione LAN senza esito: %s", scanErr.c_str());
            lastScanErr = scanErr;
        }
    }

    // 3) Ancora niente: si chiede l'host a mano -- ma solo se l'utente vuole.
    //    Prima la scansione spammava subito IP/user/pass anche se l'utente
    //    aveva appena premuto B per annullare. Ora chiediamo conferma.
    if (!ok) {
        // Se la scansione è stata annullata con B, torna subito senza chiedere altro
        if (lastScanErr.find("annullato") != std::string::npos) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled));
            return false;
        }
        if (!showConfirmDialog(title, "Scansione non ha trovato dispositivi.\nVuoi inserire un IP fisso manualmente?")) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled));
            return false;
        }
        std::string typed = promptTextBlocking(i18n::get(StrKey::DevSyncHostPrompt), host, 63);
        if (typed.empty()) { showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled)); return false; }
        host = typed;
        ok = remoteSyncLogin(host, user, pass, token, err);
    }

    // 4) ...poi le credenziali, solo se anche l'host appena confermato (che
    //    sia quello scansionato o quello digitato) rifiuta user/pass default.
    if (!ok) {
        user = promptTextBlocking(i18n::get(StrKey::DevSyncUserPrompt), user, 31);
        if (user.empty()) { showMessageAndWait(title, i18n::get(StrKey::DevSyncCancelled)); return false; }
        pass = promptTextBlocking(i18n::get(StrKey::DevSyncPassPrompt), "", 31);
        ok = remoteSyncLogin(host, user, pass, token, err);
    }

    if (!ok) {
        showMessageAndWait(title, i18n::fmt(StrKey::DevSyncLoginFailed, err));
        DebugLog::line("remote sync: login FALLITO (host=%s): %s", host.c_str(), err.c_str());
        return false;
    }

    // Login riuscito: salva sempre host/credenziali che hanno funzionato --
    // che fossero gia' salvati, trovati dalla scansione o appena digitati --
    // cosi' la prossima volta si riparte dal passo 1 (istantaneo).
    Settings::setRemoteSyncHost(host);
    Settings::setRemoteSyncUser(user);
    Settings::setRemoteSyncPass(pass);
    return true;
}

// Picker per scegliere il gioco tra i candidati trovati sul R36S.
// Ritorna indice del candidato scelto o -1 se annullato. Popup bloccante
// con lista + highlight, stesso stile dei menu impostazioni (round rect).
int UI::pickRemoteSyncGame(const std::vector<SyncCandidate>& candidates) {
    if (!renderer_ || candidates.empty()) return -1;
    markDirty();
    int sel = 0;
    int result = -2; // -2 = picking, -1 = cancel, >=0 = picked
    const int POP_W = 640, POP_H = 420;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    // Analog stick debounce
    uint32_t lastStickMs = 0;
    while (result == -2) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = -1;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    sel = (sel - 1 + (int)candidates.size()) % (int)candidates.size();
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    sel = (sel + 1) % (int)candidates.size();
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) // Switch A = conferma
                    result = sel;
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B = annulla
                    result = -1;
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    uint32_t now = SDL_GetTicks();
                    if (now - lastStickMs > 180) {
                        if (event.caxis.value < -12000) {
                            sel = (sel - 1 + (int)candidates.size()) % (int)candidates.size();
                            lastStickMs = now;
                        } else if (event.caxis.value > 12000) {
                            sel = (sel + 1) % (int)candidates.size();
                            lastStickMs = now;
                        }
                    }
                }
            }
        }
        // Popup overlay (non full-screen) come drawSaveMenuPopup
        drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
        drawRect(popX, popY, POP_W, POP_H, T().panelBg);
        drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
        drawTextCentered(i18n::get(StrKey::DevSyncPickerTitle), popX + POP_W / 2, popY + 18, T().text, fontLarge_);
        const int ROW_H = 44, listY = popY + 60;
        int vis = std::min<int>((int)candidates.size(), 7);
        for (int i = 0; i < vis; i++) {
            int idx = i;
            // Se ci sono più di 7, centra la selezione (semplice windowing)
            if ((int)candidates.size() > 7) {
                int start = std::clamp(sel - 3, 0, (int)candidates.size() - 7);
                idx = start + i;
                if (idx >= (int)candidates.size()) break;
            }
            const auto& c = candidates[idx];
            const GameInfo& gi = gameInfo(c.type);
            int rowY = listY + i * ROW_H;
            if (idx == sel) {
                drawRoundRect(popX + 12, rowY, POP_W - 24, ROW_H - 6, 8, T().menuHighlight);
                drawRoundRectOutline(popX + 12, rowY, POP_W - 24, ROW_H - 6, 8, T().cursor, 2);
            }
            std::string label = gi.displayName;
            bool isSwitchLocal = c.hasLocal && c.localPath.rfind("save:/", 0) == 0;
            label += isSwitchLocal ? " [SW]" : (c.hasLocal ? " [ROM]" : " [R36S]");
            drawText(label, popX + 28, rowY + 10, T().text, font_);
        }
        drawTextCentered(i18n::get(StrKey::DevSyncPickerHint), popX + POP_W / 2, popY + POP_H - 24, T().textDim, fontSmall_);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    markDirty();
    return result;
}

// Overlay con 3 grossi bottoni arrotondati (stesso stile Trade/Bank).
// Ritorna 0=Invia, 1=Ricevi, 2=Sincronizza, -1=annulla. Blocca con loop eventi.
int UI::pickRemoteSyncAction(const SyncCandidate& c) {
    if (!renderer_) return -1;
    markDirty();
    const GameInfo& gi = gameInfo(c.type);
    int sel = 0;
    // Determina quali azioni sono sensate (per disabilitare visivamente)
    bool canSend = c.hasLocal;
    bool canReceive = c.hasRemoteSave;
    // Sincronizza ha senso solo se entrambi presenti (check identità fatto dopo)
    bool canSync = c.hasLocal && c.hasRemoteSave;
    int result = -2;
    const int POP_W = 520, POP_H = 360;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    const int BTN_W = 340, BTN_H = 56, BTN_R = 12;
    const int BTN_X = popX + (POP_W - BTN_W) / 2;
    // Label brevi (senza {0}) per i bottoni
    const char* labels[3] = { StrKey::DevSyncActionSend, StrKey::DevSyncActionReceive, StrKey::DevSyncActionSync };
    bool enabled[3] = { canSend, canReceive, canSync };
    // Parti dal primo abilitato (se "Invia" è disabilitato vai su "Ricevi")
    for (int k = 0; k < 3; k++) if (enabled[k]) { sel = k; break; }
    auto nextSel = [&](int dir) {
        for (int step = 1; step <= 3; step++) {
            int cand = (sel + dir * step + 3) % 3;
            if (enabled[cand]) { sel = cand; break; }
        }
    };
    uint32_t lastStickMs = 0;
    while (result == -2) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = -1;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    nextSel(-1);
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    nextSel(1);
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) { // Switch A
                    if (enabled[sel]) result = sel;
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B
                    result = -1;
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    uint32_t now = SDL_GetTicks();
                    if (now - lastStickMs > 180) {
                        if (event.caxis.value < -12000) { nextSel(-1); lastStickMs = now; }
                        else if (event.caxis.value > 12000) { nextSel(1); lastStickMs = now; }
                    }
                }
            }
        }
        drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
        drawRect(popX, popY, POP_W, POP_H, T().panelBg);
        drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
        drawTextCentered(i18n::fmt(StrKey::DevSyncActionTitle, gi.displayName), popX + POP_W / 2, popY + 18, T().text, font_);
        for (int i = 0; i < 3; i++) {
            int y = popY + 70 + i * (BTN_H + 18);
            bool focused = (i == sel);
            SDL_Color bg = focused ? T().menuHighlight : T().bg;
            SDL_Color fg = enabled[i] ? T().text : T().textDim;
            if (!enabled[i] && focused) bg = T().panelBg;
            drawRoundRect(BTN_X, y, BTN_W, BTN_H, BTN_R, bg);
            drawRoundRectOutline(BTN_X, y, BTN_W, BTN_H, BTN_R, focused ? T().cursor : T().textDim, focused ? 2 : 1);
            std::string txt = i18n::get(labels[i]);
            if (!enabled[i]) txt += " (--)";
            // Centra testo nel bottone
            auto e = getTextEntry(txt, font_, fg);
            drawText(txt, BTN_X + (BTN_W - e.w) / 2, y + (BTN_H - e.h) / 2, fg, font_);
        }
        drawTextCentered("A: scegli  B: annulla", popX + POP_W / 2, popY + POP_H - 22, T().textDim, fontSmall_);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    markDirty();
    return result;
}

void UI::remoteSyncTestRow() {
    std::string title = i18n::get(StrKey::DevSyncTitle);
    if (!updateNetEnsureReady()) {
        showMessageAndWait(title, i18n::get(StrKey::SendLogNetOff));
        return;
    }

    std::string host, user, pass, token;
    if (!remoteSyncEnsureLogin(title, host, user, pass, token))
        return;

    // "Monta" il device per il resto della UI (icona barra di stato, icone
    // dock RemoteBox/DevSync via dockStateItemVisible): senza questo, la
    // scansione manuale da qui trova il device e lo usa per la sync ma lo
    // lascia "invisibile" altrove, perche' quei campi li scrive solo il
    // worker in background al boot (vedi ui.cpp). Stesso aggiornamento,
    // solo replicato qui.
    remoteDeviceAvailable_ = true;
    remoteDeviceHost_ = host;
    remoteDeviceToken_ = token;
    markDirty();

    // Elenco locale (bank/import) da confrontare con quello remoto -- stessa
    // scansione gia' usata dal selettore giochi, nessuna logica duplicata.
    // Aggiunge anche i save Switch montati (FRLG Switch) che altrimenti
    // verrebbero visti come "solo ROM" e non offrirebbero l'invio.
    std::vector<ImportedGame> localGames = scanImportPaths(importPaths_, autoCheckUsb_);
    {
        // Switch saves: presentApplications() + hasSaveData() -> se il gioco
        // Switch esiste, considera che ha un save locale anche se non c'è un
        // file in sdmc:/roms/. Il path per il sync sarà gestito come file
        // temporaneo estratto dal mount save:/ (compatibile perché il formato
        // save è identico tra Switch e ROM per FRLG).
        std::set<uint64_t> present = account_.presentApplications();
        for (GameType g : {GameType::FR, GameType::LG, GameType::FR_ES, GameType::LG_ES, GameType::FR_DE, GameType::LG_DE, GameType::FR_IT, GameType::LG_IT, GameType::FR_FR, GameType::LG_FR, GameType::FR_JA, GameType::LG_JA}) {
            if (!present.count(titleIdOf(g))) continue;
            // Controlla se c'è almeno un profilo con save per questo gioco
            bool hasAny = false;
            for (int p = 0; p < account_.profileCount(); p++) {
                if (account_.hasSaveData(p, g)) { hasAny = true; break; }
            }
            if (!hasAny) continue;
            // Se non è già presente come file-backed, aggiungilo come Switch save
            bool already = false;
            for (auto& ig : localGames) if (ig.type == g) { already = true; break; }
            if (!already) {
                ImportedGame ig;
                ig.type = g;
                ig.filePath = std::string("save:/") + saveFileNameOf(g); // marker per Switch save
                ig.sourceTag = "Switch";
                localGames.push_back(ig);
                DebugLog::line("remote sync: Switch save aggiunto per %s (%s)", gameInfo(g).displayName, ig.filePath.c_str());
            }
        }
    }

    std::string buildErr;
    std::vector<SyncCandidate> candidates = remoteSyncBuildCandidates(host, token, localGames, buildErr);
    if (candidates.empty()) {
        showMessageAndWait(title, i18n::get(StrKey::DevSyncNoCandidates));
        DebugLog::line("remote sync: nessun candidato su %s", host.c_str());
        return;
    }

    // Cartella per gli eventuali download di controllo/ricezione -- creata al
    // volo, mai fatale se fallisce (i download successivi falliranno da soli
    // e verranno segnalati normalmente).
    std::string tmpDir = basePath_ + "remote_sync_tmp/";
    mkdir(tmpDir.c_str(), 0755);

    int sent = 0, received = 0, skipped = 0, failed = 0;

    // NOTA: se localPath e' in realta' un marker "save:/..." (save Switch
    // nativo), il percorso reale e' gia' stato risolto una sola volta,
    // subito dopo aver scelto il candidato -- vedi "switchSaveRealPath" /
    // "switchLocalPath" piu' sotto. Qui non si guarda piu' save_/dirty_:
    // vedi il commento estenso sopra remoteSyncTestRow() per i due bug che
    // questo evita (save di un altro gioco inviato per errore; dirty_
    // azzerato per un save scorrelato dalla sync).
    auto doUpload = [&](const std::string& localPath, const std::string& remotePath) -> bool {
        std::string opErr;
        bool okUp = remoteSyncUpload(host, token, localPath, remotePath, opErr);
        if (okUp) sent++; else failed++;
        DebugLog::line("remote sync: invia %s -> %s (%s)", localPath.c_str(),
                       okUp ? "OK" : "FALLITO", opErr.c_str());
        return okUp;
    };
    // Scrive srcFile nel save Switch nativo di "gtype": mount in scrittura ->
    // copia byte grezzi -> commitSave() esplicito, il cui risultato e' SEMPRE
    // controllato (senza commitSave() Horizon scarta la scrittura all'unmount,
    // stesso bug gia' corretto altrove in questo file -- vedi il commento su
    // UI::restoreBackupEntry piu' sopra) -> unmount sempre, anche se la
    // copia/commit e' fallita. Mai scrittura diretta su "save:/..." (che non
    // e' montato quando arriviamo qui).
    auto writeToNativeSave = [&](const std::string& srcFile, GameType gtype) -> bool {
        const char* tag = gameInfo(gtype).gameTag;
        int profileIdx = -1;
        if (selectedProfile_ >= 0 && account_.hasSaveData(selectedProfile_, gtype))
            profileIdx = selectedProfile_;
        else {
            for (int p = 0; p < account_.profileCount(); p++)
                if (account_.hasSaveData(p, gtype)) { profileIdx = p; break; }
        }
        if (profileIdx < 0) {
            DebugLog::line("remote sync: nessun profilo con save Switch per %s (scrittura)", tag);
            return false;
        }
        std::string mnt = account_.mountSave(profileIdx, gtype);
        if (mnt.empty()) {
            DebugLog::line("remote sync: mountSave (scrittura) FALLITO per %s (profilo %d)", tag, profileIdx);
            return false;
        }
        std::string dst = mnt + saveFileNameOf(gtype);
        bool okCopy = false;
        {
            std::ifstream in(srcFile, std::ios::binary);
            if (in.is_open()) {
                std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out << in.rdbuf();
                    okCopy = out.good();
                }
            }
        }
        bool okCommit = okCopy && account_.commitSave();
        if (okCopy && !okCommit)
            DebugLog::line("remote sync: commitSave FALLITO dopo scrittura save nativo %s", tag);
        account_.unmountSave();
        return okCommit;
    };
    auto doDownload = [&](const std::string& remotePath, const std::string& localPath, GameType gtype) -> bool {
        std::string opErr;
        bool okDown;
        if (localPath.rfind("save:/", 0) == 0) {
            // Save Switch nativo: scarica in un tmp, poi scrivilo nel mount
            // nativo con writeToNativeSave() -- mai in scrittura diretta sul
            // marker "save:/..." (non montato in questo momento).
            std::string tmpRecv = tmpDir + std::string(gameInfo(gtype).gameTag) + "_switch_recv.tmp";
            okDown = remoteSyncDownload(host, token, remotePath, tmpRecv, opErr);
            if (okDown && !writeToNativeSave(tmpRecv, gtype)) {
                okDown = false;
                opErr = "scrittura save nativo fallita";
            }
            std::remove(tmpRecv.c_str());
        } else {
            okDown = remoteSyncDownload(host, token, remotePath, localPath, opErr);
        }
        if (okDown) received++; else failed++;
        DebugLog::line("remote sync: ricevi %s -> %s (%s)", remotePath.c_str(),
                       okDown ? "OK" : "FALLITO", opErr.c_str());
        return okDown;
    };
    // Copia locale pura (nessuna rete): usata quando il file remoto e' gia'
    // stato scaricato per il controllo allenatore/TID (tmpPath) e la
    // sincronizzazione lo conferma come la copia da tenere -- riscaricarlo
    // sarebbe una richiesta di rete identica e inutile.
    auto copyLocalAsReceived = [&](const std::string& srcTmp, const std::string& dstLocal) -> bool {
        std::ifstream in(srcTmp, std::ios::binary);
        bool okCopy = false;
        if (in.is_open()) {
            std::ofstream out(dstLocal, std::ios::binary | std::ios::trunc);
            if (out.is_open()) {
                out << in.rdbuf();
                okCopy = out.good();
            }
        }
        if (okCopy) received++; else failed++;
        DebugLog::line("remote sync: sincronizza (copia da verifica gia' scaricata) %s -> %s (%s)",
                       srcTmp.c_str(), dstLocal.c_str(), okCopy ? "OK" : "FALLITO");
        return okCopy;
    };

    // Nuovo flusso: picker gioco + 3 bottoni (evita spam). B nel picker torna
    // alla schermata precedente (dock), B nei 3 bottoni torna al picker (1
    // passo indietro). L'intero corpo qui sotto (fino al riepilogo/cleanup
    // in fondo alla funzione) e' avvolto in questo while(true): finita
    // un'azione (completata, annullata con B sul dialogo di conferma, o
    // fallita) si torna alla lista giochi invece di uscire dalla funzione --
    // altrimenti l'utente doveva rientrare dalla dock per ogni singola
    // sincronizzazione, anche solo per annullarne una.
    while (true) {
    SyncCandidate c;
    const GameInfo* giPtr = nullptr;
    int action = -1;
    while (true) {
        int pickedIdx = pickRemoteSyncGame(candidates);
        if (pickedIdx < 0) return;
        c = candidates[pickedIdx];
        giPtr = &gameInfo(c.type);
        action = pickRemoteSyncAction(c);
        if (action < 0) continue; // B nei bottoni -> torna al picker
        break;
    }
    const GameInfo& gi = *giPtr;
    sent = received = skipped = failed = 0;

    // Se il candidato locale e' un save Switch nativo (marker "save:/...",
    // aggiunto per FRLG quando manca un file in sdmc:/roms/), qui non c'e'
    // ancora nessun mount reale: risolvilo UNA SOLA VOLTA in un file
    // temporaneo con i byte grezzi letti dal mount nativo del gioco ESATTO
    // scelto (c.type) -- mount -> copia file-a-file -> unmount immediato,
    // mai attraverso save_/dirty_ (vedi nota su doUpload piu' sopra). Usato
    // sia per l'invio (doUpload) sia per il controllo identita' nel flusso
    // "Sincronizza" piu' sotto; il tmp viene ripulito in fondo alla funzione.
    std::string switchSaveRealPath;
    if (c.hasLocal && c.localPath.rfind("save:/", 0) == 0) {
        int profileIdx = -1;
        if (selectedProfile_ >= 0 && account_.hasSaveData(selectedProfile_, c.type))
            profileIdx = selectedProfile_;
        else {
            for (int p = 0; p < account_.profileCount(); p++)
                if (account_.hasSaveData(p, c.type)) { profileIdx = p; break; }
        }
        if (profileIdx < 0) {
            DebugLog::line("remote sync: nessun profilo con save Switch per %s", gi.gameTag);
        } else {
            std::string mnt = account_.mountSave(profileIdx, c.type);
            if (mnt.empty()) {
                DebugLog::line("remote sync: mountSave FALLITO per %s (profilo %d)", gi.gameTag, profileIdx);
            } else {
                std::string src = mnt + saveFileNameOf(c.type);
                std::string dst = tmpDir + std::string(gi.gameTag) + "_switch_native.tmp";
                // Chiudi gli stream PRIMA di unmountSave(): distruggere un
                // ifstream ancora aperto su un device smontato ("save")
                // abortisce in _close_r (Data Abort su null, visto su HW).
                {
                    std::ifstream in(src, std::ios::binary);
                    if (in.is_open()) {
                        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) {
                            out << in.rdbuf();
                            if (out.good()) switchSaveRealPath = dst;
                        }
                    }
                }
                account_.unmountSave();
                DebugLog::line("remote sync: save Switch nativo %s risolto in tmp (%s): %s",
                               gi.gameTag, switchSaveRealPath.empty() ? "FALLITO" : "OK", dst.c_str());
            }
        }
    }
    std::string switchLocalPath = switchSaveRealPath.empty() ? c.localPath : switchSaveRealPath;

    // Logica centralizzata per le 3 azioni — il save deve avere esattamente lo stesso base della ROM
    auto getExtLocal = [](const std::string& p) -> std::string {
        size_t dot = p.find_last_of('.');
        return (dot == std::string::npos) ? std::string(".sav") : p.substr(dot);
    };
    auto getBaseLocal = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of('/');
        std::string f = (slash == std::string::npos) ? p : p.substr(slash + 1);
        size_t dot = f.find_last_of('.');
        return (dot == std::string::npos) ? f : f.substr(0, dot);
    };
    // Helper per trovare la ROM locale corrispondente al save (solo per file-backed gb/gbc/gba/nds + FRLG)
    // Mai stat() su marker "save:/" (save Switch non montato qui): su Horizon
    // ha causato crash. Per i marker si usa remoteRomBaseName/gameTag.
    auto findLocalRom = [&](const std::string& savePath, GameType type) -> std::string {
        if (!(isFRLG(type) || isImportedFile(type) || isGen1File(type) || isGen2File(type) || isGen4File(type) || isGen5File(type)))
            return std::string();
        if (savePath.rfind("save:/", 0) == 0) return std::string();
        size_t slash = savePath.find_last_of('/');
        std::string dir = (slash == std::string::npos) ? "" : savePath.substr(0, slash + 1);
        std::string file = (slash == std::string::npos) ? savePath : savePath.substr(slash + 1);
        size_t dot = file.find_last_of('.');
        std::string base = (dot == std::string::npos) ? file : file.substr(0, dot);
        const char* exts[] = {".gba",".gbc",".gb",".nds"};
        for (auto ext : exts) {
            std::string cand = dir + base + ext;
            struct stat st2;
            if (stat(cand.c_str(), &st2) == 0 && S_ISREG(st2.st_mode)) return cand;
        }
        return std::string();
    };
    // Costruisci il path remoto del save facendo combaciare esattamente il base con la ROM
    // Se il locale e' un marker save:/ non usare il suo base (es. "FireRed_i"):
    // usa la ROM remota o il gameTag.
    std::string wantRomBase;
    bool localIsSaveMarker = c.localPath.rfind("save:/", 0) == 0;
    if (!c.remoteRomBaseName.empty()) wantRomBase = c.remoteRomBaseName;
    else if (!localIsSaveMarker) {
        std::string lr = findLocalRom(c.localPath, c.type);
        if (!lr.empty()) wantRomBase = getBaseLocal(lr);
        else if (!c.localPath.empty()) wantRomBase = getBaseLocal(c.localPath);
        else wantRomBase = std::string(gameInfo(c.type).gameTag);
    } else {
        wantRomBase = std::string(gameInfo(c.type).gameTag);
    }
    for (char& ch : wantRomBase) if (ch == '/' || ch == '\\') ch = '_';
    // Estensioni corrette: Switch (locale) usa .sav per file-backed, R36S (remoto) usa .srm/.dsv
    auto getLocalSaveExtFor = [&](GameType t) -> std::string {
        if (isGen4File(t) || isGen5File(t)) return ".sav"; // NDS su Switch
        if (isFRLG(t) || isImportedFile(t) || isGen1File(t) || isGen2File(t)) return ".sav";
        return ".sav";
    };
    auto getRemoteSaveExtFor = [&](GameType t) -> std::string {
        if (isGen4File(t) || isGen5File(t)) return ".dsv"; // DraStic su R36S
        if (isFRLG(t) || isImportedFile(t) || isGen1File(t) || isGen2File(t)) return ".srm";
        return ".sav";
    };
    std::string localExtWanted = getLocalSaveExtFor(c.type);
    std::string remoteExtWanted = getRemoteSaveExtFor(c.type);
    // Se il save remoto esistente ha base diversa dalla ROM, usa il base della ROM per far combaciare
    std::string remoteTargetPath;
    if (c.hasRemoteSave) {
        std::string curSaveBase = getBaseLocal(c.remoteSavePath);
        std::string curLower = curSaveBase, wantLower = wantRomBase;
        for (char& ch : curLower) ch = (char)std::tolower((unsigned char)ch);
        for (char& ch : wantLower) ch = (char)std::tolower((unsigned char)ch);
        if (curLower != wantLower && !wantRomBase.empty()) {
            size_t slash = c.remoteSavePath.find_last_of('/');
            std::string dir = (slash == std::string::npos) ? c.remoteDir : c.remoteSavePath.substr(0, slash + 1);
            if (dir.empty()) dir = c.remoteDir;
            remoteTargetPath = dir + wantRomBase + remoteExtWanted;
        } else {
            // Mantieni il path esistente ma assicurati l'estensione sia quella corretta per il remoto (.srm/.dsv)
            std::string curExt = getExtLocal(c.remoteSavePath);
            if (curExt != remoteExtWanted) {
                size_t slash = c.remoteSavePath.find_last_of('/');
                std::string dir = (slash == std::string::npos) ? "" : c.remoteSavePath.substr(0, slash + 1);
                remoteTargetPath = dir + curSaveBase + remoteExtWanted;
            } else {
                remoteTargetPath = c.remoteSavePath;
            }
        }
        } else {
            remoteTargetPath = c.remoteDir + wantRomBase + remoteExtWanted;
        }
    bool didSomething = false;
    if (action == 0) { // Invia
        if (!c.hasLocal) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else if (showConfirmDialog(i18n::fmt(StrKey::DevSyncSendTitle, gi.displayName), i18n::get(StrKey::DevSyncSendOnlyLocalBody))) {
            DebugLog::line("remote sync: invia confermato %s -> %s", switchLocalPath.c_str(), remoteTargetPath.c_str());
            bool ok = doUpload(switchLocalPath, remoteTargetPath);
            // Se sul remoto manca la ROM e il gioco è file-backed, invia anche la ROM
            if (ok && !c.hasRemoteRom) {
                std::string localRom = findLocalRom(c.localPath, c.type);
                if (!localRom.empty()) {
                    size_t dot = localRom.find_last_of('.');
                    std::string ext = (dot == std::string::npos) ? ".gba" : localRom.substr(dot);
                    std::string romBase = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                    // Pulisci romBase
                    for (char& ch : romBase) if (ch == '/' || ch == '\\') ch = '_';
                    std::string remoteRomPath = c.remoteDir + romBase + ext;
                    std::string romErr;
                    if (remoteSyncUpload(host, token, localRom, remoteRomPath, romErr)) {
                        DebugLog::line("remote sync: ROM inviata %s -> %s", localRom.c_str(), remoteRomPath.c_str());
                    } else {
                        DebugLog::line("remote sync: ROM non inviata %s: %s", localRom.c_str(), romErr.c_str());
                    }
                }
            }
            didSomething = ok;
        }
    } else if (action == 1) { // Ricevi
        if (!c.hasRemoteSave) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else {
            std::string body = c.hasLocal ? i18n::get(StrKey::DevSyncChooseReceiveBody) : i18n::get(StrKey::DevSyncReceiveOnlyRemoteBody);
            if (showConfirmDialog(i18n::fmt(StrKey::DevSyncReceiveTitle, gi.displayName), body)) {
                bool ok = false;
                // Helper per estrarre estensione da un path (include punto)
                auto getExt = [](const std::string& p) -> std::string {
                    size_t dot = p.find_last_of('.');
                    if (dot == std::string::npos) return std::string(".sav");
                    return p.substr(dot);
                };
                if (c.hasLocal) {
                    // Quando ricevi su un save esistente, assicurati che il nome del save
                    // corrisponda esattamente al nome della ROM (stessa base). Se differiscono,
                    // rinomina il save locale per far combaciare con la ROM.
                    std::string localRom = findLocalRom(c.localPath, c.type);
                    std::string wantBase;
                    if (!localRom.empty()) {
                        size_t slash = localRom.find_last_of('/');
                        std::string romFile = (slash == std::string::npos) ? localRom : localRom.substr(slash + 1);
                        size_t dot = romFile.find_last_of('.');
                        wantBase = (dot == std::string::npos) ? romFile : romFile.substr(0, dot);
                    } else if (!c.remoteRomBaseName.empty()) {
                        wantBase = c.remoteRomBaseName;
                    }
                    std::string destPath = c.localPath;
                    if (!wantBase.empty()) {
                        size_t slash = destPath.find_last_of('/');
                        std::string dir = (slash == std::string::npos) ? "" : destPath.substr(0, slash + 1);
                        std::string ext = getLocalSaveExtFor(c.type);
                        std::string curBase;
                        {
                            std::string file = (slash == std::string::npos) ? destPath : destPath.substr(slash + 1);
                            size_t dot = file.find_last_of('.');
                            curBase = (dot == std::string::npos) ? file : file.substr(0, dot);
                        }
                        if (curBase != wantBase) {
                            destPath = dir + wantBase + ext;
                            DebugLog::line("remote sync: rinomino save locale per match ROM: %s -> %s", c.localPath.c_str(), destPath.c_str());
                        } else {
                            // Usa estensione corretta per Switch (.sav) anche se remoto era .srm
                            size_t dot2 = destPath.find_last_of('.');
                            std::string curExt = (dot2 == std::string::npos) ? "" : destPath.substr(dot2);
                            if (curExt != ext) destPath = dir + wantBase + ext;
                        }
                    }
                    ok = doDownload(c.remoteSavePath, destPath, c.type);
                    // Se il destPath è diverso dal vecchio c.localPath e il download è ok, rimuovi il vecchio file orfano
                    if (ok && destPath != c.localPath) {
                        std::remove(c.localPath.c_str());
                        DebugLog::line("remote sync: vecchio save rimosso %s", c.localPath.c_str());
                    }
                    if (ok) { rescanImportedGames(); markDirty(); }
                } else {
                    // Per file-backed (gba/gbc/gb/nds + FRLG) salva in sdmc:/roms/ insieme alla ROM,
                    // non nella cartella dell'app. Per gli altri usa il primo import path.
                    std::string localDest;
                    bool fileBacked = isFRLG(c.type) || isImportedFile(c.type) || isGen1File(c.type) || isGen2File(c.type) || isGen4File(c.type) || isGen5File(c.type);
                    if (fileBacked) localDest = "sdmc:/roms/";
                    else localDest = importPaths_.empty() ? (basePath_ + "import/") : importPaths_.front().path;
                    if (!localDest.empty() && localDest.back() != '/') localDest += "/";
                    mkdir(localDest.c_str(), 0755);
                    std::string baseName = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                    // Pulisci baseName da caratteri non validi per filesystem locale se serve
                    std::string safeBase = baseName;
                    for (char& ch : safeBase) if (ch == '/' || ch == '\\') ch = '_';
                    std::string ext = getLocalSaveExtFor(c.type);
                    std::string saveDest = localDest + safeBase + ext;
                    ok = doDownload(c.remoteSavePath, saveDest, c.type);
                    // Se manca il gioco (hasLocal==false) e c'è una ROM remota, chiedi se scaricare anche la ROM
                    // così il save diventa subito utilizzabile. Il save deve avere esattamente lo stesso base della ROM.
                    if (ok && c.hasRemoteRom) {
                        // Controlla se la ROM locale già esiste (con lo stesso base)
                        std::string romCheckPath = localDest + safeBase + ".gba";
                        bool romExists = false;
                        {
                            // Prova le estensioni note per vedere se una ROM con quel base esiste già
                            const char* tryExts[] = {".gba",".gbc",".gb",".nds"};
                            for (auto ext : tryExts) {
                                std::string cand = localDest + safeBase + ext;
                                struct stat st2;
                                if (stat(cand.c_str(), &st2) == 0) { romExists = true; break; }
                            }
                        }
                        if (!romExists) {
                            if (showConfirmDialog(i18n::get(StrKey::DevSyncAskRomTitle), i18n::fmt(StrKey::DevSyncAskRomBody, safeBase))) {
                                std::vector<RemoteEntry> dirEntries;
                                std::string listErr;
                                if (remoteSyncListPath(host, token, c.remoteDir, dirEntries, listErr)) {
                                    std::string romFile;
                                    std::string wantBaseLower = safeBase;
                                    for (char& ch : wantBaseLower) ch = (char)std::tolower((unsigned char)ch);
                                    for (auto& e : dirEntries) {
                                        if (e.isDir) continue;
                                        if (!remoteSyncIsRomFileName(e.name)) continue;
                                        std::string baseLower = e.name;
                                        for (char& ch : baseLower) ch = (char)std::tolower((unsigned char)ch);
                                        size_t dot = baseLower.find_last_of('.');
                                        std::string baseOnly = (dot == std::string::npos) ? baseLower : baseLower.substr(0, dot);
                                        if (baseOnly == wantBaseLower) { romFile = e.name; break; }
                                    }
                                    if (!romFile.empty()) {
                                        std::string romDest = localDest + safeBase + romFile.substr(romFile.find_last_of('.'));
                                        std::string romErr;
                                        if (remoteSyncDownload(host, token, c.remoteDir + romFile, romDest, romErr)) {
                                            DebugLog::line("remote sync: ROM ricevuta %s -> %s", (c.remoteDir + romFile).c_str(), romDest.c_str());
                                        } else {
                                            DebugLog::line("remote sync: ROM non ricevuta %s: %s", romFile.c_str(), romErr.c_str());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                didSomething = ok;
                if (ok) {
                    // Aggiorna subito la lista giochi senza dover riavviare l'app
                    rescanImportedGames();
                    markDirty();
                }
                if (!ok) failed = 1; // assicurati che il riepilogo mostri fallito
            }
        }
    } else if (action == 2) { // Sincronizza
        if (!c.hasLocal || !c.hasRemoteSave) {
            showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoLocalOrRemote));
        } else {
            // Verifica identità allenatore prima di confrontare date (come prima)
            DebugLog::line("remote sync: sincronizza controllo identita' %s...", gi.gameTag);
            std::string tmpPath = tmpDir + gi.gameTag + "_check.tmp";
            std::string dlErr;
            bool tmpOk = remoteSyncDownload(host, token, c.remoteSavePath, tmpPath, dlErr);
            std::string localOt;
            bool sameIdentity = false;
            // localProbe/remoteProbe dichiarati qui (non dentro l'if sotto)
            // perche' servono anche piu' avanti per il confronto
            // playtime/dex -- stesso oggetto gia' caricato per il controllo
            // identita', zero costo di rete o parsing in piu'.
            SaveFile localProbe, remoteProbe;
            if (tmpOk) {
                localProbe.setGameType(c.type);
                remoteProbe.setGameType(c.type);
                if (localProbe.load(switchLocalPath) && remoteProbe.load(tmpPath)) {
                    localOt = localProbe.dsOtName();
                    sameIdentity = !localOt.empty() && localOt == remoteProbe.dsOtName() && localProbe.dsTid() == remoteProbe.dsTid();
                }
            } else {
                DebugLog::line("remote sync: download di controllo fallito per %s: %s", gi.gameTag, dlErr.c_str());
            }
            if (!sameIdentity) {
                showMessageAndWait(title, i18n::get(StrKey::DevSyncSyncNoIdentity));
                std::remove(tmpPath.c_str());
            } else {
                // 2026-09-19: la sola data di modifica del file NON dice chi ha
                // davvero piu' progressi -- una copia via USB, un semplice
                // caricamento in un emulatore, o un orologio di sistema sballato
                // bastano a confonderla, e su questi due save (stesso allenatore,
                // appena verificato sopra) la differenza reale che conta e'
                // quanto si e' giocato. localProbe/remoteProbe sono gia' caricati
                // per il controllo identita': stesso costo di rete, nessun
                // download in piu'.
                // Priorita': 1) tempo di gioco (solo GBA per ora, vedi
                // SaveFile::playTimeSeconds) 2) Pokedex catturati (copre anche
                // GB/GBC/NDS, vedi Pokedex::getDexStatus) 3) mtime del file, come
                // ultima spiaggia quando nessuno dei due segnali e' disponibile o
                // i due save risultano identici su entrambi.
                long localPt = localProbe.playTimeSeconds();
                long remotePt = remoteProbe.playTimeSeconds();
                Pokedex::DexStatus localDex = Pokedex::getDexStatus(localProbe);
                Pokedex::DexStatus remoteDex = Pokedex::getDexStatus(remoteProbe);
                struct stat st;
                long long localModified = 0;
                if (stat(switchLocalPath.c_str(), &st) == 0) localModified = (long long)st.st_mtime;
                bool remoteNewer = false;
                bool alreadySynced = false;
                std::string howDecided;
                const char* criterionKey = nullptr;
                if (localPt >= 0 && remotePt >= 0 && localPt != remotePt) {
                    remoteNewer = remotePt > localPt;
                    howDecided = "playtime";
                    criterionKey = StrKey::DevSyncCriterionPlaytime;
                } else if (localDex.supported && remoteDex.supported && localDex.caught != remoteDex.caught) {
                    remoteNewer = remoteDex.caught > localDex.caught;
                    howDecided = "dex";
                    criterionKey = StrKey::DevSyncCriterionDex;
                } else if (localPt >= 0 && remotePt >= 0 && localDex.supported && remoteDex.supported) {
                    // Stesso tempo di gioco E stessa Pokedex catturata su
                    // entrambi i lati: sono davvero identici, il mtime non
                    // serve (e sarebbe pure fuorviante -- un save appena
                    // inviato al dispositivo remoto prende la data di invio
                    // ed e' sempre "piu' recente" anche senza alcun
                    // progresso reale).
                    alreadySynced = true;
                    howDecided = "identical";
                } else {
                    remoteNewer = c.remoteSaveModifiedUnix > localModified;
                    howDecided = "mtime";
                    criterionKey = StrKey::DevSyncCriterionMtime;
                }
                DebugLog::line("remote sync: sincronizza direzione=%s per %s (local pt=%ld dex=%d, remote pt=%ld dex=%d)",
                               howDecided.c_str(), gi.gameTag, localPt, localDex.caught, remotePt, remoteDex.caught);

                SyncSideInfo localInfo, remoteInfo;
                localInfo.trainer = localOt;
                localInfo.playTimeSeconds = localPt;
                localInfo.dexCaught = localDex.caught;
                localInfo.dexTotal = localDex.total;
                localInfo.dexSupported = localDex.supported;
                localInfo.modifiedUnix = localModified;
                remoteInfo.trainer = remoteProbe.dsOtName();
                remoteInfo.playTimeSeconds = remotePt;
                remoteInfo.dexCaught = remoteDex.caught;
                remoteInfo.dexTotal = remoteDex.total;
                remoteInfo.dexSupported = remoteDex.supported;
                remoteInfo.modifiedUnix = c.remoteSaveModifiedUnix;

                if (alreadySynced) {
                    // Stessi due riquadri della conferma normale (colpo
                    // d'occhio coerente), ma solo per mostrare "=" -- A/B
                    // chiudono senza fare nulla, non c'e' nessun trasferimento.
                    showSyncCompareDialog(gi.displayName, localInfo, remoteInfo, false, nullptr, true);
                } else if (showSyncCompareDialog(gi.displayName, localInfo, remoteInfo, remoteNewer, criterionKey)) {
                    if (remoteNewer) {
                        // Usa copia già scaricata (tmpPath) — evita seconda richiesta di rete
                        // e soprattutto non confrontare più dopo: il mtime locale va sovrascritto ora.
                        // Save Switch nativo: scrivi nel mount nativo (mai su "save:/..." diretto,
                        // non montato qui), stesso helper usato da doDownload piu' sopra.
                        if (c.localPath.rfind("save:/", 0) == 0) {
                            bool okNative = writeToNativeSave(tmpPath, c.type);
                            if (okNative) received++; else failed++;
                            DebugLog::line("remote sync: sincronizza (save nativo, da verifica gia' scaricata) %s (%s)",
                                           gi.gameTag, okNative ? "OK" : "FALLITO");
                        } else {
                            copyLocalAsReceived(tmpPath, c.localPath);
                        }
                    } else {
                        bool ok = doUpload(switchLocalPath, remoteTargetPath);
                        if (ok && !c.hasRemoteRom) {
                            std::string localRom = findLocalRom(c.localPath, c.type);
                            if (!localRom.empty()) {
                                size_t dot = localRom.find_last_of('.');
                                std::string ext = (dot == std::string::npos) ? ".gba" : localRom.substr(dot);
                                std::string romBase = c.remoteRomBaseName.empty() ? std::string(gi.gameTag) : c.remoteRomBaseName;
                                for (char& ch : romBase) if (ch == '/' || ch == '\\') ch = '_';
                                std::string remoteRomPath = c.remoteDir + romBase + ext;
                                std::string romErr;
                                if (remoteSyncUpload(host, token, localRom, remoteRomPath, romErr))
                                    DebugLog::line("remote sync: ROM sincronizzata %s -> %s", localRom.c_str(), remoteRomPath.c_str());
                            }
                        }
                    }
                    didSomething = true;
                }
                std::remove(tmpPath.c_str());
                // Nota: playtime/dex/mtime sopra sono tutti letti PRIMA del
                // transfer (localProbe/localModified originali) -- dopo il
                // transfer il file locale ha mtime = now, non va mai riletto
                // per decisioni nello stesso giro.
            }
        }
    }

    // Summary per singola azione: non mostrare popup "inviato/saltati" se l'utente ha
    // appena premuto B per annullare — con B deve tornare subito alla lista
    // giochi da sincronizzare (non uscire dalla dock), senza spam. Logga solo,
    // mostra solo se fallito.
    skipped = didSomething ? 0 : 1;
    if (failed > 0) {
        showMessageAndWait(title, i18n::fmt(StrKey::DevSyncFlowSummary, std::to_string(sent), std::to_string(received), std::to_string(skipped), std::to_string(failed)));
    }
    DebugLog::line("remote sync: flusso completato su %s (inviati=%d ricevuti=%d saltati=%d falliti=%d)",
                   host.c_str(), sent, received, skipped, failed);
    // Ripulisci il tmp del save Switch nativo risolto sopra (se creato) --
    // mai lasciato sul dispositivo dopo che il flusso e' terminato, come
    // ogni altro tmp di questa funzione.
    if (!switchSaveRealPath.empty()) std::remove(switchSaveRealPath.c_str());

    // Si torna alla lista (vedi while(true) sopra): i dati del candidato appena
    // usato (hasLocal/hasRemoteSave/mtime) sono adesso vecchi se l'azione ha
    // scritto qualcosa (es. "Ricevi" crea il locale, "Sincronizza" puo'
    // aggiornare uno dei due lati) -- senza un refresh i bottoni Invia/Ricevi
    // /Sincronizza della prossima selezione userebbero stato pre-azione. Se il
    // refresh fallisce (rete a singhiozzo) tiene la lista precedente piuttosto
    // che svuotarla e forzare un'uscita non richiesta.
    if (didSomething) {
        std::string refreshErr;
        std::vector<SyncCandidate> refreshed = remoteSyncBuildCandidates(host, token, localGames, refreshErr);
        if (!refreshed.empty()) candidates = refreshed;
        else DebugLog::line("remote sync: refresh candidati fallito dopo l'azione: %s", refreshErr.c_str());
    }
    } // while(true) -- torna alla lista giochi, vedi commento sopra il picker
}

// --- Box Remoto: apre save gia' presenti sul dispositivo remoto dentro lo
// stesso selettore giochi/banca locale. Il save scelto si scarica in un
// file temporaneo che si comporta come un ImportedGame qualunque (stessa
// UI::selectGame(), stesso bank/edit di sempre); in uscita dal gioco (non
// dal box) UI::returnToGameSelector() chiede conferma e rispedisce il file
// al dispositivo remoto solo se e' stato davvero modificato.

void UI::openRemoteBox() {
    std::string title = i18n::get(StrKey::RemoteBoxTitle);
    if (!updateNetEnsureReady()) {
        showMessageAndWait(title, i18n::get(StrKey::SendLogNetOff));
        return;
    }
    if (isDualBankMode()) {
        showMessageAndWait(title, i18n::get(StrKey::RemoteBoxNotInDualMode));
        return;
    }

    std::string host, user, pass, token;
    if (!remoteSyncEnsureLogin(title, host, user, pass, token))
        return;

    // Stesso motivo di remoteSyncTestRow() sopra: tiene "montato" il device
    // per icona barra di stato/dock anche qui (in pratica il Box Remoto e'
    // raggiungibile solo quando gia' visibile, ma un token rinnovato da
    // remoteSyncEnsureLogin() va comunque ripubblicato).
    remoteDeviceAvailable_ = true;
    remoteDeviceHost_ = host;
    remoteDeviceToken_ = token;

    std::vector<ImportedGame> localGames = scanImportPaths(importPaths_, autoCheckUsb_);
    std::string buildErr;
    std::vector<SyncCandidate> candidates = remoteSyncBuildCandidates(host, token, localGames, buildErr);

    // Cartella per i save remoti scaricati mentre il box e' aperto -- ripulita
    // (i singoli file) alla chiusura in closeRemoteBox().
    std::string tmpDir = basePath_ + "remote_box_tmp/";
    mkdir(tmpDir.c_str(), 0755);

    remoteBoxEntries_.clear();
    std::vector<ImportedGame> boxGames;
    for (auto& c : candidates) {
        // Solo save che esistono gia' sul dispositivo remoto: il box apre
        // partite esistenti, non ne crea di nuove da una sola ROM (per
        // quello c'e' "Invia" nel flusso Invia/Ricevi/Sincronizza).
        if (!c.hasRemoteSave) continue;
        const GameInfo& gi = gameInfo(c.type);
        std::string tmpPath = tmpDir + gi.gameTag + "_box.tmp";
        std::string dlErr;
        if (!remoteSyncDownload(host, token, c.remoteSavePath, tmpPath, dlErr)) {
            DebugLog::line("box remoto: download fallito per %s: %s", gi.gameTag, dlErr.c_str());
            continue;
        }
        RemoteBoxEntry entry;
        entry.type = c.type;
        entry.tmpPath = tmpPath;
        entry.host = host;
        entry.token = token;
        entry.remoteSavePath = c.remoteSavePath;
        {
            struct stat st;
            if (stat(tmpPath.c_str(), &st) == 0) {
                entry.snapSize = (long long)st.st_size;
                entry.snapMtime = (long long)st.st_mtime;
            }
        }
        remoteBoxEntries_.push_back(entry);

        ImportedGame ig;
        ig.type = c.type;
        ig.filePath = tmpPath;
        ig.sourceTag = "R36S";
        boxGames.push_back(ig);
    }

    if (boxGames.empty()) {
        showMessageAndWait(title, i18n::get(StrKey::RemoteBoxNoSaves));
        return;
    }

    // Salva lo stato locale della griglia -- si ripristina alla chiusura del
    // box (closeRemoteBox), esattamente come si trovava prima di entrare.
    savedAvailableGames_ = availableGames_;
    savedImportedGames_ = importedGames_;

    importedGames_ = boxGames;
    availableGames_.clear();
    for (auto& ig : importedGames_)
        availableGames_.push_back(ig.type);

    remoteBoxActive_ = true;

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    selPageShown_ = 0;
    selSlide_ = 0.0f;
    galSelShown_ = -1;
    galSlide_ = 0.0f;
    gsSetFocus(GSFocus::Grid);
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();
    screen_ = AppScreen::GameSelector;

    showMessageAndWait(title, i18n::fmt(StrKey::RemoteBoxEntered, std::to_string((int)boxGames.size())));
    DebugLog::line("box remoto: aperto, %d save da %s", (int)boxGames.size(), host.c_str());
}

void UI::closeRemoteBox() {
    availableGames_ = savedAvailableGames_;
    importedGames_ = savedImportedGames_;
    savedAvailableGames_.clear();
    savedImportedGames_.clear();

    // Save modificati (dimensione/mtime diversi dall'istantanea presa al
    // download, vedi RemoteBoxEntry) che nessuno ha ancora rispedito al
    // device remoto -- capita normalmente uscendo dal save con B, che porta
    // alla lista banche (UI::actionCancel) e NON passa da
    // UI::returnToGameSelector() a meno di usare il menu "Cambia gioco":
    // senza questo controllo, chiudere il box li scartava in silenzio (bug
    // segnalato: il save modificato viene scritto in locale correttamente,
    // solo mai rispedito -- "i pokemon inviati non appaiono poi nel
    // dispositivo"). Stessa policy di UI::returnToGameSelector(): mai un
    // invio automatico silenzioso, sempre una conferma prima -- qui
    // aggregata in un solo popup invece di uno per save.
    std::vector<size_t> pending;
    for (size_t i = 0; i < remoteBoxEntries_.size(); i++) {
        struct stat st;
        if (stat(remoteBoxEntries_[i].tmpPath.c_str(), &st) != 0) continue;
        if ((long long)st.st_size != remoteBoxEntries_[i].snapSize ||
            (long long)st.st_mtime != remoteBoxEntries_[i].snapMtime)
            pending.push_back(i);
    }
    if (!pending.empty()) {
        std::string title = i18n::get(StrKey::RemoteBoxSendTitle);
        if (showConfirmDialog(title, i18n::fmt(StrKey::RemoteBoxCloseSendBody,
                                               std::to_string(pending.size())))) {
            int sent = 0, failed = 0;
            for (size_t idx : pending) {
                auto& e = remoteBoxEntries_[idx];
                std::string upErr;
                if (remoteSyncUpload(e.host, e.token, e.tmpPath, e.remoteSavePath, upErr)) {
                    sent++;
                    DebugLog::line("box remoto: invio OK alla chiusura (%s)", e.tmpPath.c_str());
                } else {
                    failed++;
                    DebugLog::line("box remoto: invio FALLITO alla chiusura (%s): %s",
                                   e.tmpPath.c_str(), upErr.c_str());
                }
            }
            showMessageAndWait(title, i18n::fmt(StrKey::RemoteBoxCloseSendResult,
                                                std::to_string(sent), std::to_string(failed)));
        } else {
            DebugLog::line("box remoto: invio alla chiusura rifiutato dall'utente (%d save), modifiche solo locali",
                           (int)pending.size());
        }
    }

    for (auto& e : remoteBoxEntries_)
        std::remove(e.tmpPath.c_str());
    remoteBoxEntries_.clear();
    remoteBoxActive_ = false;

    gameSelCursor_ = 0;
    gameSelPage_ = 0;
    selPageShown_ = 0;
    selSlide_ = 0.0f;
    galSelShown_ = -1;
    galSlide_ = 0.0f;
    gsSetFocus(GSFocus::Grid);
    showWorking(i18n::get(StrKey::LoadingGameIcons));
    loadGameIcons();

    DebugLog::line("box remoto: chiuso, ripristinata lista locale");
}
