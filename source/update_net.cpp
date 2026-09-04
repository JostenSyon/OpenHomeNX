#include "update_net.h"
#include "debug_log.h"

#include <cstdio>
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

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* f = static_cast<FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, f) * size;
}

void applyCommonOpts(CURL* c, const std::string& token, struct curl_slist** hdrs) {
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);   // GitHub 302 -> objects.githubusercontent.com
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 64L);  // < 64 B/s per 30s -> abort
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "OpenHomeNX-updater/1");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (caBundlePresent()) {
        curl_easy_setopt(c, CURLOPT_CAINFO, kCaBundle);
    } else {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    *hdrs = curl_slist_append(*hdrs, "Accept: application/octet-stream");
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

std::string sha256Hex(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    std::vector<unsigned char> buf(64 * 1024);
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
        mbedtls_sha256_update_ret(&ctx, buf.data(), n);
    std::fclose(f);
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
    curl_off_t lastBytes = 0;
    double lastBytesTime = 0.0;
};
int xferInfo(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* dp = static_cast<DlProgress*>(p);
    if (!dp || !dp->cb) return 0;
    double now = wallSeconds();
    if (dp->lastEmit != 0.0 && now - dp->lastEmit < 0.25) return 0;
    double dt = now - dp->lastBytesTime;
    double mbps = (dp->lastBytesTime > 0.0 && dt > 0.05)
                  ? ((double)(dlnow - dp->lastBytes) / dt / (1024.0 * 1024.0)) : 0.0;
    dp->lastEmit = now; dp->lastBytes = dlnow; dp->lastBytesTime = now;
    char line[160];
    if (dltotal > 0) {
        int pct = (int)((dlnow * 100) / dltotal);
        std::snprintf(line, sizeof(line), "%s\n  %d%%  (%.1f / %.1f MB)  ·  %.1f MB/s",
                      dp->label.c_str(), pct,
                      dlnow / (1024.0 * 1024.0), dltotal / (1024.0 * 1024.0), mbps);
    } else {
        std::snprintf(line, sizeof(line), "%s\n  %.1f MB  ·  %.1f MB/s",
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

    const std::string tmp = destPath + ".part";
    std::remove(tmp.c_str());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { err = "impossibile scrivere " + tmp; return false; }

    CURL* c = curl_easy_init();
    if (!c) { std::fclose(f); std::remove(tmp.c_str()); err = "curl_easy_init fallito"; return false; }
    struct curl_slist* hdrs = nullptr;
    applyCommonOpts(c, token, &hdrs);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);

    DlProgress dp;
    dp.label = "Downloading";
    if (progress) {
        dp.cb = progress;
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferInfo);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &dp);
    }

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    std::fclose(f);

    if (rc != CURLE_OK) {
        std::remove(tmp.c_str());
        err = std::string("download: ") + curl_easy_strerror(rc);
        return false;
    }
    if (http != 200) {
        std::remove(tmp.c_str());
        err = "download HTTP " + std::to_string(http);
        return false;
    }

    if (!expectSha256.empty()) {
        std::string got = sha256Hex(tmp);
        if (got != toLower(expectSha256)) {
            std::remove(tmp.c_str());
            err = "sha256 non combacia (atteso " + expectSha256.substr(0, 12) +
                  "…, ottenuto " + got.substr(0, 12) + "…)";
            return false;
        }
        DebugLog::line("update-net: sha256 ok");
    }

    std::remove(destPath.c_str());
    if (std::rename(tmp.c_str(), destPath.c_str()) != 0) {
        std::remove(tmp.c_str());
        err = "rename " + tmp + " -> " + destPath + " fallito";
        return false;
    }
    DebugLog::line("update-net: scaricato -> %s", destPath.c_str());
    return true;
}

bool updateNetUploadLog(const std::string& baseUrl, const std::string& token,
                        const std::string& basePath, std::string& err) {
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
    FILE* f = std::fopen(logPath.c_str(), "rb");
    if (!f) { err = "debug.log non trovato in " + logPath + " (errno " + std::strerror(errno) + ")"; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); err = "debug.log vuoto"; return false; }
    if (sz > 4 * 1024 * 1024) { std::fclose(f); err = "debug.log troppo grande (>4MB)"; return false; }

    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/upload";

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
    DebugLog::reopenAfterUpload();
    if (rc != CURLE_OK) { err = std::string("upload: ") + curl_easy_strerror(rc); return false; }
    if (http < 200 || http >= 300) { err = "upload HTTP " + std::to_string(http); return false; }
    DebugLog::line("update-net: log inviato -> %s (%ld byte)", url.c_str(), sz);
    return true;
}
