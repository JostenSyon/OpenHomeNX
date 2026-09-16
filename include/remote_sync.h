#pragma once
#include <string>
#include <vector>
#include <functional>

#include "game_type.h"
#include "import_scan.h" // ImportedGame, per remoteSyncBuildCandidates

// Client minimale per l'API REST del progetto open source "filebrowser"
// (usato come file manager web da ArkOS/JELOS/ROCKNIX su handheld retro tipo
// R36S, tipicamente in porta 80). Riusa curl, gia' linkato per l'updater
// (vedi update_net.cpp) -- nessuna libreria nuova.
//
// Login:  POST http://<host>/api/login
//         body {"username":..,"password":..,"recaptcha":""} (il campo
//         recaptcha vuoto e' comunque richiesto da filebrowser).
//         Risposta: il token JWT -- come stringa grezza (filebrowser
//         classico, text/plain) o come JSON {"token":"..."} a seconda della
//         versione/fork; remoteSyncLogin() gestisce entrambe le forme.
// Liste:  GET  http://<host>/api/resources/<path>/   header "X-Auth: <token>"
//         Risposta JSON: {"isDir":true,"items":[{"name":...,"modified":...,
//         "isDir":...,"size":...}, ...], ...} (root con path vuoto).
//         "modified" e' un timestamp RFC3339 (es. "2026-09-15T21:34:10Z" o
//         con offset "+01:00"): remoteSyncListPath() lo converte gia' in
//         unix time UTC in RemoteEntry::modifiedUnix, senza dipendere da
//         timegm/mktime (fuso locale non affidabile su homebrew).
//
// Scansione LAN: niente vera "device discovery" (nessun mDNS di serie su
// libnx). remoteSyncScanLan() combina tre passi, in questo ordine, su ogni
// indirizzo della sottorete /24 della Switch:
//   1) connessione TCP "vuota" (probeTcpOpen, solo connect, timeout breve)
//      -- capisce in fretta se qualcosa risponde sulla porta 80, scartando
//      la stragrande maggioranza degli indirizzi (nessun device);
//   2) SOLO sugli host che superano il passo 1, un fingerprint di
//      contenuto (fetchFingerprintBody): un GET senza autenticazione su
//      http://<host>/, verificando che il body contenga
//      "window.FileBrowser" (il global iniettato dalla index.html del
//      Filebrowser classico) -- scarta chi accetta la porta 80 ma non e'
//      affatto un Filebrowser (bug reale confermato: un router sulla stessa
//      rete "accettava" il login del passo 3 pur non essendolo);
//   3) solo se anche il fingerprint combacia, il login vero
//      (remoteSyncLogin) con le credenziali fornite.
// Tutto sequenziale, non parallelo. Il fingerprint era stato provato anche
// da solo, su TUTTI i 254 host in parallelo (concorrenza limitata a 3): su
// hardware reale si era rivelato molto piu' lento (oltre un minuto per uno
// scan completo) e non aveva mai trovato il device, probabilmente perche'
// ogni indirizzo mai contattato richiede prima una risoluzione ARP che sul
// Switch sembra bloccare piu' a lungo di quanto imposti CURLOPT_*TIMEOUT.
// Farlo invece in sequenza e solo sui pochi host che hanno gia' superato il
// passo 1 (quindi con l'ARP gia' risolto dal probeTcpOpen appena riuscito)
// evita quel costo, restando rapido.
//
// Riconoscimento giochi: per ora solo dal nome del file (nessun download,
// vedi remoteSyncGuessGameFromName), un filtro di primo livello con falsi
// positivi possibili. Il prossimo passo -- non ancora implementato -- e'
// leggere l'header del rom/save con una richiesta HTTP Range (pochi byte,
// niente download completo) per un'identificazione precisa via game code,
// stesso principio gia' usato in import_scan.cpp per i save Gen3 (RUBY vs
// EMERALD via il byte a 0xac): richiede pero' di confermare che il
// Filebrowser risponda ai Range e i game code esatti per ogni titolo, cosa
// che non e' stata ancora verificata su hardware reale.

// true + outToken pieno su successo; false + err leggibile altrimenti.
bool remoteSyncLogin(const std::string& host, const std::string& user,
                      const std::string& pass, std::string& outToken, std::string& err);

// Callback di avanzamento per remoteSyncScanLan(): riceve una riga di testo
// gia' pronta (stesso stile di UpdateProgressFn in update_net.h -- "Titolo\n
// NN%  (dettagli)", pensata per showWorking(), che legge la percentuale dal
// testo e disegna la barra) una volta ogni ~0.1s durante lo scan. Puo'
// essere nullptr: nessuna UI aggiornata, comportamento identico a prima
// (usato dal worker in background, che non ha un popup da aggiornare).
using RemoteSyncScanProgressFn = std::function<void(const std::string&)>;

// Trova il Filebrowser vero sulla LAN locale (vedi il commento in cima al
// file): scansione sequenziale della sottorete /24 della Switch, tre passi
// per host (TCP -> fingerprint di contenuto -> login vero), finche' un host
// non passa tutti e tre (outHost + outToken pronti all'uso). false + err se
// nessun host risponde sulla porta, o nessuno di quelli che rispondono e'
// un Filebrowser che accetta queste credenziali, o l'IP locale non e'
// disponibile (WiFi spento). onProgress (opzionale) viene chiamata durante
// lo scan con una riga di stato pronta per showWorking(): la scansione
// resta sincrona (nessun thread nuovo -- vedi il commento sopra
// probeTcpOpen in remote_sync.cpp sul perche' non si e' voluto aggiungere
// altra concorrenza proprio a questa funzione), ma il chiamante puo'
// ridisegnare lo schermo ad ogni chiamata cosi' l'utente vede una barra
// che avanza invece di uno schermo fermo per i secondi che dura lo scan
// (stesso schema del progresso di download in update_net.cpp).
bool remoteSyncScanLan(const std::string& user, const std::string& pass,
                        std::string& outHost, std::string& outToken, std::string& err,
                        const RemoteSyncScanProgressFn& onProgress = nullptr);

// Esito di un poll a remoteSyncWorkerPoll(): Pending se il worker in
// background non ha ancora finito (o non e' mai partito), Found/NotFound
// sono terminali e vengono consumati una sola volta da chi li legge (stesso
// principio one-shot di autoUpdateTakeResult in autoupdate.h).
enum class RemoteSyncWorkerResult { Pending, Found, NotFound };

// Scoperta automatica in background, silenziosa: un solo tentativo per
// boot, in un thread separato (stesso pattern di autoupdate.cpp), che
// riprova SOLO il login sull'host gia' salvato -- mai la scansione completa
// della subnet, che resta unicamente nel flusso manuale (vedi il commento
// sopra remoteSyncWorkerMain() in remote_sync.cpp per il perche', e i
// limiti noti sulla concorrenza nifm/curl con gli altri thread dell'app).
// Se remote sync non e' mai stato configurato a mano
// (Settings::remoteSyncHost() vuoto) non fa nulla. Va chiamata una sola
// volta a boot (vedi autoUpdateStart in main.cpp); chiamate successive
// sono no-op.
void remoteSyncWorkerStart();

// Poll non bloccante da chiamare una volta per frame sul MAIN thread (mai
// dal thread in background: legge/consuma solo atomici, zero I/O). Pending
// finche' il thread non ha finito (o se non e' mai partito); su Found
// riempie outHost/outToken e va consumato una sola volta -- il tentativo
// automatico e' unico per boot, dopo il primo Found/NotFound si resta
// Pending per sempre.
RemoteSyncWorkerResult remoteSyncWorkerPoll(std::string& outHost, std::string& outToken);

// Da chiamare al teardown (vedi autoUpdateJoin in main.cpp), prima di
// smontare rete/USB: aspetta che il thread in background sia uscito. No-op
// se il worker non e' mai partito (remote sync non configurato).
void remoteSyncWorkerJoin();

// Una voce restituita da remoteSyncListPath: file o sottocartella.
struct RemoteEntry {
    std::string name;
    bool isDir = false;
    long long modifiedUnix = 0; // 0 se il campo "modified" manca o non si legge
    long long size = 0;
};

// Elenca una cartella del Filebrowser a un path qualsiasi ("" = radice, gli
// slash iniziali/finali sono normalizzati). false se la cartella non esiste
// o la richiesta fallisce -- non e' un errore raro: si provano apposta piu'
// percorsi candidati per sistema (vedi remoteSyncKnownSystems), quindi il
// chiamante deve trattarlo come "prova il prossimo", non come un problema.
bool remoteSyncListPath(const std::string& host, const std::string& token,
                         const std::string& path, std::vector<RemoteEntry>& outEntries,
                         std::string& err);

// Elenca la sola cartella radice ("/"). Thin wrapper su remoteSyncListPath
// per chi vuole solo un conteggio, senza gestire RemoteEntry.
bool remoteSyncListRoot(const std::string& host, const std::string& token,
                         int& outItemCount, std::string& err);

// Un sistema retro noto (GBA/GBC/GB/NDS/...): systemId e' il nome della
// cartella sotto "roms", trovato a runtime case-insensitive (vedi il
// commento sopra remoteSyncBuildCandidates) -- non e' piu' un path fisso:
// "roms/gba/" scritto a mano si e' rivelato fragile su hardware reale (404
// su ogni cartella nota, vedi il log diagnostico), probabilmente per
// maiuscole diverse o perche' il Filebrowser e' gia' scope-ato dentro la
// cartella roms stessa. subPaths sono le sottocartelle DENTRO quella del
// sistema da provare in ordine ("" per la cartella stessa): per ora solo
// NDS ne ha una seconda ("backup", convenzione DraStic per i salvataggi
// .dsv) perche' non e' confermato su hardware reale se ArkOS tenga i save
// di NDS accanto alla ROM o li' -- si prova prima quella vuota, poi
// "backup" solo se la prima non risponde.
struct RemoteSystemPaths {
    const char* systemId;
    std::vector<std::string> subPaths;
};
const std::vector<RemoteSystemPaths>& remoteSyncKnownSystems();

// Prova a indovinare il GameType di un file trovato in remoto dal solo nome
// (nessun download): riconosce nomi/abbreviazioni comuni in italiano e
// inglese. Euristica di primo filtro, non un'identificazione definitiva --
// vedi il commento in cima al file sul prossimo passo (lettura header via
// Range). false se nessun nome noto compare nella stringa.
bool remoteSyncGuessGameFromName(const std::string& fileName, GameType& outGuess);

// Un file trovato in remoto per un dato GameType, con la sua controparte
// locale (se esiste) -- costruito da remoteSyncBuildCandidates incrociando
// le cartelle note remote con l'elenco locale di scanImportPaths().
struct SyncCandidate {
    GameType type = GameType::EMERALD;
    bool hasLocal = false;
    std::string localPath;              // valido se hasLocal
    bool hasRemoteSave = false;
    std::string remoteSavePath;         // path completo (cartella+nome) del save remoto, se hasRemoteSave
    long long remoteSaveModifiedUnix = 0;
    bool hasRemoteRom = false;          // ROM trovata (anche senza save): serve solo per "Invia" ex-novo
    std::string remoteRomBaseName;      // nome ROM senza estensione, per il nome del nuovo save
    std::string remoteDir;              // cartella dove si trova ROM/save (per costruire path nuovi)
};

// true se il nome ha un'estensione da salvataggio nota (.sav/.srm/.dsv), per
// distinguere un save da una ROM nello stesso elenco di cartella.
bool remoteSyncIsSaveFileName(const std::string& fileName);

// Incrocia l'elenco locale (da scanImportPaths, gia' in mano al chiamante --
// nessuna scansione locale qui) con le cartelle remote, restituendo un
// candidato per ogni GameType che esiste da almeno un lato (locale o save
// remoto -- una ROM remota senza save da nessuna parte non produce nulla da
// proporre). Le cartelle remote si scoprono a runtime (radice -> cartella
// "roms" -- o la radice stessa se "roms" non c'e' -- -> cartella per
// sistema, tutte case-insensitive), non da un path fisso: logga sempre il
// contenuto di radice e "roms", cosi' un "nessuna cartella trovata" nei log
// ha subito i nomi veri da aggiungere/correggere in remoteSyncKnownSystems().
// Fa rete (qualche remoteSyncListPath): non e' una funzione pura come le
// altre helper "build" di questo file.
std::vector<SyncCandidate> remoteSyncBuildCandidates(
    const std::string& host, const std::string& token,
    const std::vector<ImportedGame>& localGames, std::string& err);

// Scarica un file remoto (GET /api/raw/<path>) su un percorso locale,
// sovrascrivendolo per intero (nessun resume/parziale). true + file scritto
// su outLocalPath; false + err altrimenti (il file parziale viene rimosso).
bool remoteSyncDownload(const std::string& host, const std::string& token,
                         const std::string& remotePath, const std::string& outLocalPath,
                         std::string& err);

// Carica un file locale su un path remoto (POST /api/resources/<path>
// ?override=true, corpo = bytes grezzi). A differenza di login/list/
// download (verificati o ricalcati sullo stesso pattern gia' confermato via
// DevTools), questo verbo di scrittura di Filebrowser NON e' stato ancora
// testato contro un'istanza reale: se il primo "Invia" su hardware vero
// fallisce con un errore HTTP inatteso (es. 405/409), e' il primo posto da
// controllare.
bool remoteSyncUpload(const std::string& host, const std::string& token,
                      const std::string& localPath, const std::string& remotePath,
                      std::string& err);
