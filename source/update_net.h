#pragma once
#include <string>
#include <functional>

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

// GET "<baseUrl>/latest.json". `token` non vuoto → header "Authorization: Bearer <token>".
// false + `err` su qualunque problema (rete, HTTP != 200, JSON senza i campi).
bool updateNetFetchInfo(const std::string& baseUrl, const std::string& token,
                        RemoteUpdateInfo& out, std::string& err);

// Scarica `url` (assoluto) in `destPath`. Se `expectSha256` non è vuoto, verifica
// e cancella il file se non combacia. false + `err` su errore.
bool updateNetDownload(const std::string& url, const std::string& token,
                       const std::string& destPath, const std::string& expectSha256,
                       std::string& err, UpdateProgressFn progress = nullptr);

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
