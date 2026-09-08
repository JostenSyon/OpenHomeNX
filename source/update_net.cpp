#include "update_net.h"
#include "debug_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <vector>

#include <switch.h>
#include <curl/curl.h>
#include <mbedtls/sha256.h>

namespace {

bool g_netReady = false;

// romfs:/cacert.pem è opzionale. Se manca (target è un webserver HTTP locale,
// oppure non lo abbiamo ancora impacchettato) si disattiva la verifica del
// certificato: accettabile per uso personale, l'utente sceglie la sorgente.
const char* kCaBundle = "romfs:/cacert.pem";

bool caBundlePresent() {
    FILE* f = std::fopen(kCaBundle, "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// Download in RAM: la rete non aspetta mai la SD. Tetto 256MB (un NRO
// è ~18MB): oltre si abortisce esplicito, mai OOM silenzioso.
constexpr size_t kRamCap = 256u * 1024u * 1024u;
struct MemSink {
    std::vector<uint8_t> data;
    size_t contentLen = 0;  // da Content-Length, 0 se chunked/sconosciuto
    bool overCap = false;
};
size_t writeToMemory(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* m = static_cast<MemSink*>(userdata);
    size_t n = size * nmemb;
    if (m->overCap || m->contentLen > kRamCap || m->data.size() + n > kRamCap) {
        m->overCap = true;
        return 0;  // -> CURLE_WRITE_ERROR, mappato in errore leggibile
    }
    m->data.insert(m->data.end(), ptr, ptr + n);
    return n;
}
size_t headerToMem(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* m = static_cast<MemSink*>(userdata);
    size_t n = size * nmemb;
    // "Content-Length: 12345" case-insensitive
    const char* kKey = "content-length:";
    if (n > strlen(kKey)) {
        bool match = true;
        for (size_t i = 0; i < strlen(kKey); i++)
            if (std::tolower((unsigned char)ptr[i]) != kKey[i]) { match = false; break; }
        if (match) {
            unsigned long v = strtoul(ptr + strlen(kKey), nullptr, 10);
            m->contentLen = (size_t)v;
            if (m->contentLen > kRamCap) m->overCap = true;
            else if (m->contentLen > 0) m->data.reserve(m->contentLen);
        }
    }
    return n;
}

void applyCommonOpts(CURL* c, const std::string& token, struct curl_slist** hdrs) {
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);   // GitHub 302 -> objects.githubusercontent.com
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);  // < 1 KB/s per 30s -> abort
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "OpenHomeNX-updater/1");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    // Buffer più grandi per throughput (default 16KB)
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256 * 1024L);
    // HTTP/2 su GitHub (richiede libcurl compilato con nghttp2)
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    // TCP keepalive per connessioni lunghe
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPINTVL, 10L);
    if (caBundlePresent()) {
        curl_easy_setopt(c, CURLOPT_CAINFO, kCaBundle);
    } else {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    *hdrs = curl_slist_append(*hdrs, "Accept: application/octet-stream");
    // GitHub: preferisce compression per JSON, ma NRO è già compresso
    if (!token.empty()) {
        std::string h = "Authorization: Bearer " + token;
        *hdrs = curl_slist_append(*hdrs, h.c_str());
    }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, *hdrs);
}

// Estrae il valore stringa di "key" da un JSON piatto (il latest.json lo
// generiamo noi, niente annidamento). "" se assente.
std::string jsonStr(const std::string& body, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return "";
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string::npos) return "";
    size_t q1 = body.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return body.substr(q1 + 1, q2 - q1 - 1);
}

std::string joinUrl(const std::string& base, const std::string& rel) {
    if (rel.rfind("http://", 0) == 0 || rel.rfind("https://", 0) == 0) return rel;
    if (!base.empty() && base.back() == '/') return base + rel;
    return base + "/" + rel;
}

std::string sha256HexBuf(const uint8_t* data, size_t len) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, data, len);
    unsigned char out[32];
    mbedtls_sha256_finish_ret(&ctx, out);
    mbedtls_sha256_free(&ctx);
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (unsigned char b : out) { s += hex[b >> 4]; s += hex[b & 0xF]; }
    return s;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool updateNetAvailable() { return g_netReady; }
void updateNetSetReady(bool ready) { g_netReady = ready; }

// Poll throttled dello stato link via nifm (nifm:u). Lazy-init: se nifm non si
// apre, resta OFF e riprova al poll successivo. Mai fatale, mai bloccante.
const char* updateNetLinkStr() {
    static double lastPoll = -1e9;
    static char cached[8] = "OFF";
    static bool nifmReady = false;
    double now = (double)armTicksToNs(armGetSystemTick()) / 1.0e9;
    if (now - lastPoll < 2.0)
        return cached;
    lastPoll = now;
    if (!nifmReady) {
        if (R_FAILED(nifmInitialize(NifmServiceType_User)))
            return cached; // resta OFF, riprova tra 2s
        nifmReady = true;
    }
    NifmInternetConnectionType type = (NifmInternetConnectionType)0;
    u32 strength = 0;
    NifmInternetConnectionStatus st = (NifmInternetConnectionStatus)0;
    const char* s = "OFF";
    if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &st)) &&
        st == NifmInternetConnectionStatus_Connected) {
        s = (type == NifmInternetConnectionType_Ethernet) ? "LAN" : "WiFi";
    }
    std::snprintf(cached, sizeof(cached), "%s", s);
    return cached;
}

bool updateNetFetchInfo(const std::string& baseUrl, const std::string& token,
                        RemoteUpdateInfo& out, std::string& err) {
    if (!g_netReady) { err = "rete non inizializzata"; return false; }
    const std::string url = joinUrl(baseUrl, "latest.json");

    CURL* c = curl_easy_init();
    if (!c) { err = "curl_easy_init fallito"; return false; }
    std::string body;
    struct curl_slist* hdrs = nullptr;
    applyCommonOpts(c, token, &hdrs);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { err = std::string("GET latest.json: ") + curl_easy_strerror(rc); return false; }
    if (http != 200)    { err = "latest.json HTTP " + std::to_string(http); return false; }

    out.version = jsonStr(body, "version");
    out.nroUrl  = jsonStr(body, "nro");
    out.sha256  = toLower(jsonStr(body, "sha256"));
    if (out.version.empty() || out.nroUrl.empty()) {
        err = "latest.json senza 'version'/'nro'";
        return false;
    }
    out.nroUrl = joinUrl(baseUrl, out.nroUrl);
    DebugLog::line("update-net: latest v%s nro=%s", out.version.c_str(), out.nroUrl.c_str());
    return true;
}

namespace {
// Tempo di parete in secondi. `clock()` su newlib/Switch è tempo CPU: durante un
// download il processo è bloccato su I/O e `clock()` non avanza → il throttle
// non scadeva mai e il callback non emetteva. Uso il tick di sistema di libnx.
double wallSeconds() {
    return (double)armTicksToNs(armGetSystemTick()) / 1.0e9;
}
struct DlProgress {
    UpdateProgressFn cb;
    std::string label;
    double lastEmit = 0.0;
    double startTime = 0.0;  // media stabile su tutto il trasferimento
};
int xferInfo(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* dp = static_cast<DlProgress*>(p);
    if (!dp || !dp->cb) return 0;
    // No all-zero frames: the "Downloading vX..." card stays up until the
    // first bytes arrive (avoids the ugly 0% / 0.0 MB/s flash on connect).
    if (dlnow <= 0 && dltotal <= 0) return 0;
    double now = wallSeconds();
    if (dp->startTime == 0.0) dp->startTime = now;
    if (dp->lastEmit != 0.0 && now - dp->lastEmit < 0.25) return 0;
    // Media dall'inizio: stabile e veritiera. L'istantanea su finestre da
    // 0.25s oscilla (lettura rete a burst + scrittura SD bloccante) e
    // mostra 0.5 MB/s anche quando la media reale è 2 MB/s.
    double elapsed = now - dp->startTime;
    double mbps = (elapsed > 0.05) ? ((double)dlnow / elapsed / (1024.0 * 1024.0)) : 0.0;
    dp->lastEmit = now;
    char line[160];
    // NOTE: ASCII '-' separator, not '·' (U+00B7): the Switch system font
    // lacks it and renders tofu (box with X) instead.
    if (dltotal > 0) {
        int pct = (int)((dlnow * 100) / dltotal);
        std::snprintf(line, sizeof(line), "%s\n  %d%%  (%.1f / %.1f MB)  -  %.1f MB/s",
                      dp->label.c_str(), pct,
                      dlnow / (1024.0 * 1024.0), dltotal / (1024.0 * 1024.0), mbps);
    } else {
        std::snprintf(line, sizeof(line), "%s\n  %.1f MB  -  %.1f MB/s",
                      dp->label.c_str(), dlnow / (1024.0 * 1024.0), mbps);
    }
    dp->cb(line);
    return 0;
}
} // namespace

bool updateNetDownload(const std::string& url, const std::string& token,
                       const std::string& destPath, const std::string& expectSha256,
                       std::string& err, UpdateProgressFn progress) {
    if (!g_netReady) { err = "rete non inizializzata"; return false; }

    // Fase 1: rete -> RAM (la SD non rallenta mai il socket).
    MemSink mem;
    CURL* c = curl_easy_init();
    if (!c) { err = "curl_easy_init fallito"; return false; }
    struct curl_slist* hdrs = nullptr;
    applyCommonOpts(c, token, &hdrs);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToMemory);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, headerToMem);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &mem);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);

    DlProgress dp;
    dp.label = "Downloading";
    if (progress) {
        dp.cb = progress;
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferInfo);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &dp);
    }

    // Niente eccezioni nel build Switch (-fno-exceptions): la protezione
    // OOM è il tetto kRamCap controllato in writeToMemory/headerToMem.
    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_off_t dlBytes = 0, dlNanos = 0;
    double dlAvg = 0.0;
    curl_easy_getinfo(c, CURLINFO_SIZE_DOWNLOAD_T, &dlBytes);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME_T, &dlNanos);
    curl_easy_getinfo(c, CURLINFO_SPEED_DOWNLOAD, &dlAvg);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc == CURLE_WRITE_ERROR && mem.overCap) {
        err = "file oltre il tetto RAM 256MB, download abortito";
        return false;
    }
    if (rc != CURLE_OK) {
        err = std::string("download: ") + curl_easy_strerror(rc);
        return false;
    }
    if (http != 200) {
        err = "download HTTP " + std::to_string(http);
        return false;
    }
    if (mem.data.empty()) { err = "download vuoto"; return false; }
    if (dlNanos > 0)
        DebugLog::line("update-net: %.1f MB in RAM in %.1fs (rete %.1f MB/s)",
                       mem.data.size() / (1024.0 * 1024.0),
                       dlNanos / 1.0e6, dlAvg / (1024.0 * 1024.0));

    // Fase 2: sha in RAM (niente rilettura da SD).
    if (!expectSha256.empty()) {
        std::string got = sha256HexBuf(mem.data.data(), mem.data.size());
        if (got != toLower(expectSha256)) {
            err = "sha256 non combacia (atteso " + expectSha256.substr(0, 12) +
                  "…, ottenuto " + got.substr(0, 12) + "…)";
            return false;
        }
        DebugLog::line("update-net: sha256 ok");
    }

    // Fase 3: un'unica scrittura sequenziale su SD (veloce su flash).
    if (progress) progress("Scrittura su SD…");
    double w0 = wallSeconds();
    const std::string tmp = destPath + ".part";
    std::remove(tmp.c_str());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { err = "impossibile scrivere " + tmp; return false; }
    size_t w = std::fwrite(mem.data.data(), 1, mem.data.size(), f);
    bool wok = (w == mem.data.size()) && (std::fflush(f) == 0);
    std::fclose(f);
    if (!wok) {
        std::remove(tmp.c_str());
        err = "scrittura SD incompleta (" + std::to_string(w) + "/" +
              std::to_string(mem.data.size()) + " byte)";
        return false;
    }
    double wsec = wallSeconds() - w0;
    if (wsec > 0.01)
        DebugLog::line("update-net: %.1f MB su SD in %.1fs (%.1f MB/s)",
                       mem.data.size() / (1024.0 * 1024.0), wsec,
                       mem.data.size() / (1024.0 * 1024.0) / wsec);

    std::remove(destPath.c_str());
    if (std::rename(tmp.c_str(), destPath.c_str()) != 0) {
        std::remove(tmp.c_str());
        err = "rename " + tmp + " -> " + destPath + " fallito";
        return false;
    }
    DebugLog::line("update-net: scaricato -> %s", destPath.c_str());
    return true;
}

// POST binario di un file su <baseUrl><endpoint>[?f=nome]. Ritorna false + err
// su qualunque problema. Usato sia per debug.log che per libusbhsfs.log
// (endpoint /upload) che per i save (endpoint /upload-save).
static bool uploadOneFile(const std::string& baseUrl, const std::string& token,
                          const std::string& path, const std::string& remoteName,
                          std::string& err, const std::string& endpoint = "/upload",
                          long maxBytes = 4 * 1024 * 1024) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = path + " non trovato (errno " + std::strerror(errno) + ")"; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); err = path + " vuoto"; return false; }
    if (sz > maxBytes) { std::fclose(f); err = path + " troppo grande (>" + std::to_string(maxBytes / (1024 * 1024)) + "MB)"; return false; }

    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += endpoint;
    if (!remoteName.empty()) url += "?f=" + remoteName;

    CURL* c = curl_easy_init();
    if (!c) { std::fclose(f); err = "curl_easy_init fallito"; return false; }
    struct curl_slist* hdrs = nullptr;
    applyCommonOpts(c, token, &hdrs);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_READDATA, f);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)sz);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    // invia come binary
    hdrs = curl_slist_append(hdrs, "Content-Type: application/octet-stream");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    std::fclose(f);
    if (rc != CURLE_OK) { err = std::string("upload: ") + curl_easy_strerror(rc); return false; }
    if (http < 200 || http >= 300) { err = "upload HTTP " + std::to_string(http); return false; }
    DebugLog::line("update-net: %s inviato -> %s (%ld byte)", path.c_str(), url.c_str(), sz);
    return true;
}

bool updateNetUploadLog(const std::string& baseUrl, const std::string& token,
                        const std::string& basePath, std::string& err,
                        bool* outSentLibLog) {
    if (outSentLibLog) *outSentLibLog = false;
    if (!g_netReady) { err = "rete non inizializzata"; return false; }
    std::string logPath;
    if (!DebugLog::flushAndReopenForUpload(logPath)) {
        // fallback: prova basePath diretto se flush fallisce (debug appena attivato senza file)
        logPath = basePath + "debug.log";
        DebugLog::line("upload: flush fallito, provo %s", logPath.c_str());
        FILE* tf = std::fopen(logPath.c_str(), "rb");
        if (!tf) {
            std::string alt = "sdmc:/switch/OpenHomeNX/debug.log";
            tf = std::fopen(alt.c_str(), "rb");
            if (tf) { std::fclose(tf); logPath = alt; }
            else { err = "debug.log non trovato in " + logPath + " (attiva Debug log dal menu +)"; return false; }
        } else std::fclose(tf);
        // riapri comunque il log per continuare
        std::string dummy;
        DebugLog::flushAndReopenForUpload(dummy);
    }
    if (!uploadOneFile(baseUrl, token, logPath, "", err)) {
        DebugLog::reopenAfterUpload();
        return false;
    }
    DebugLog::reopenAfterUpload();
    // Best-effort: se esiste anche il log della libreria USB, invialo. Un suo
    // fallimento non invalida l'upload principale (già riuscito).
    {
        const char* libLog = "sdmc:/libusbhsfs.log";
        FILE* probe = std::fopen(libLog, "rb");
        if (probe) {
            std::fclose(probe);
            std::string libErr;
            if (uploadOneFile(baseUrl, token, libLog, "libusbhsfs", libErr)) {
                if (outSentLibLog) *outSentLibLog = true;
            } else {
                DebugLog::line("upload: libusbhsfs.log saltato: %s", libErr.c_str());
            }
        } else {
            DebugLog::line("upload: sdmc:/libusbhsfs.log assente, invio solo debug.log");
        }
    }
    return true;
}

bool updateNetUploadSave(const std::string& baseUrl, const std::string& token,
                         const std::string& filePath, const std::string& gameTag,
                         std::string& err) {
    if (!g_netReady) { err = "rete non inizializzata"; return false; }
    // 128MB: i save Switch superano di molto il tetto 4MB dei log.
    if (!uploadOneFile(baseUrl, token, filePath, gameTag, err, "/upload-save",
                       128L * 1024 * 1024))
        return false;
    return true;
}
