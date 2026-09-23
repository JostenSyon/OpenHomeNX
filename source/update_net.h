#pragma once
#include <string>
#include <functional>
#include <mutex>

// Callback di avanzamento download: riceve una riga pronta da mostrare, es.
// "Downloading v0.1.21…  47%  ·  2.1 MB/s". Throttlata a ~4 volte/s.
using UpdateProgressFn = std::function<void(const std::string&)>;

// Sorgente update remota (Layer 1 dell'updater). `baseUrl` è la radice servita
// da un webserver locale ("http://192.168.1.x:8000") oppure da GitHub releases
// ("https://github.com/utente/repo/releases/latest/download"). Il file
// "<baseUrl>/latest.json" descrive l'ultima build:
//   { "version": "0.1.21", "nro": "OpenHomeNX.nro", "sha256": "<hex opzionale>" }
struct RemoteUpdateInfo {
    std::string version;  // es. "0.1.21"
    std::string nroUrl;   // assoluto, o relativo a baseUrl
    std::string sha256;   // hex minuscolo, "" = nessun controllo
};

// true quando i socket sono pronti (impostato da main dopo socketInitializeDefault).
bool updateNetAvailable();
void updateNetSetReady(bool ready);
// curl_global_init() UNA volta al boot sul main thread, prima che parta un
// qualunque worker (job.h e gli altri). libcurl lo richiede esplicito prima
// che esistano altri thread; il vecchio lazy-init in updateNetEnsureReady()
// (static bool senza mutex) era una race se worker+main lo chiamavano vicini.
void updateNetInitCurl();
// Ritenta l'init socket (+curl) se giu: il boot puo fallire la race col WiFi.
// true se rete usabile. Chiamato dai gate update prima di dichiararla off.
bool updateNetEnsureReady();

// Inizializza nifm:u una sola volta (idempotente, mai bloccante): usata sia
// da updateNetLinkStr() qui sotto sia da chi altro (es. remote_sync.cpp, per
// l'IP locale durante la scansione LAN) ha bisogno di nifm senza duplicarne
// l'init.
bool updateNetEnsureNifm();

// Lock condiviso per QUALSIASI chiamata nifm*, init compreso: nifm:u e' una
// singola sessione IPC ora condivisa fra piu' thread (autoupdate.cpp, il
// worker di scoperta in remote_sync.cpp, e il main thread) -- le chiamate
// IPC di libnx non sono garantite sicure se lanciate in concorrenza sulla
// stessa sessione da thread diversi: il rischio concreto e' un hang, non
// solo un dato letto storto (causa esatta del blocco segnalato dopo
// l'introduzione del worker in background). updateNetEnsureNifm() e
// updateNetLinkStr() prendono gia' questo lock da sole; chi chiama una
// nifm* direttamente altrove (remoteSyncScanLan in remote_sync.cpp, per
// nifmGetCurrentIpAddress) deve prenderlo per tutta la chiamata.
std::mutex& updateNetNifmMutex();

// Stato reale del link (WiFi/LAN/OFF) via nifm, throttled (~1 query ogni 2s,
// risultato cachato). updateNetAvailable() dice solo "socket pronti" — vero
// anche senza connessione — quindi la label usava quello e restava fissa.
const char* updateNetLinkStr();

// Helper: URL base per GitHub Releases (ultimo .nro).
// Uso: updateNetFetchInfo(githubReleasesUrl("utente", "repo"), token, info, err);
inline std::string githubReleasesUrl(const std::string& owner, const std::string& repo) {
    return "https://github.com/" + owner + "/" + repo + "/releases/latest/download";
}

// GET "<baseUrl>/latest.json". `token` non vuoto → header "Authorization: Bearer <token>".
// false + `err` su qualunque problema (rete, HTTP != 200, JSON senza i campi).
bool updateNetFetchInfo(const std::string& baseUrl, const std::string& token,
                        RemoteUpdateInfo& out, std::string& err);

// Canale beta: interroga la GitHub API releases, prende la prima
// pre-release (la più recente) e ne restituisce la base download
// (".../releases/download/<tag>") + tag. Da lì updateNetFetchInfo()
// legge il latest.json allegato alla pre-release. Nessuna pre-release
// → false con err "none" (non un errore di rete: il chiamante lo dice
// esplicito invece di ricadere silenzioso sullo stabile).
bool updateNetFetchBetaBase(const std::string& owner, const std::string& repo,
                            const std::string& token, std::string& outBase,
                            std::string& outTag, std::string& err);

// SHA256 hex di un file (streaming, per NRO grandi). "" se illeggibile.
std::string sha256HexFile(const std::string& path);

// Scarica `url` (assoluto) in `destPath` in due fasi: rete -> RAM (tetto
// 256MB, mai OOM silenzioso) poi un'unica scrittura sequenziale su SD.
// Se `expectSha256` non è vuoto, verifica in RAM e fallisce esplicito.
bool updateNetDownload(const std::string& url, const std::string& token,
                       const std::string& destPath, const std::string& expectSha256,
                       std::string& err, UpdateProgressFn progress = nullptr,
                       const bool* cancel = nullptr);

// POST di `basePath/debug.log` a `baseUrl/upload` (o /upload.log). Richiede debug on.
bool updateNetUploadLog(const std::string& baseUrl, const std::string& token,
                        const std::string& basePath, std::string& err,
                        bool* outSentLibLog = nullptr);

// POST di un file save a `baseUrl/upload-save?f=<gameTag>`. Il server lo
// archivia in saves/ con estensione .sav. Nessun limite di formato dal lato
// client oltre maxBytes (i save Switch superano i 4MB dei log).
bool updateNetUploadSave(const std::string& baseUrl, const std::string& token,
                         const std::string& filePath, const std::string& gameTag,
                         std::string& err);
