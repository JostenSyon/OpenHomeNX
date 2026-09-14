#include "debug_log.h"
#include "app_version.h"
#include <fstream>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <mutex>

#ifdef OH_DEBUG_LOG

namespace DebugLog {

namespace {
bool s_enabled = false;
FILE* s_file = nullptr;
std::string s_basePath;
std::mutex s_mutex;

std::string currentTime() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_r(&now, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return std::string(buf);
}

std::string currentDateTime() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// Apre s_file (una volta) e scrive il banner di RUN. Idempotente.
// NON blocca s_mutex da sola: i suoi due soli chiamanti (init(),
// setEnabled()) lo detengono gia' quando la invocano -- rilocking sullo
// stesso std::mutex non ricorsivo era un autodeadlock garantito al primo
// avvio con debug.enable presente (schermo nero prima di ogni rendering).
void openLogFile() {
    if (s_file || s_basePath.empty()) return;
    std::string logPath = s_basePath + "debug.log";
    s_file = std::fopen(logPath.c_str(), "a");
    if (!s_file) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "===== RUN %s v%s (%s) =====",
        currentDateTime().c_str(),
#ifdef APP_VERSION
        APP_VERSION
#else
        "?"
#endif
#ifdef BUILD_SHA
        ,
        BUILD_SHA
#else
        ,
        "?"
#endif
    );
    std::fputs(buf, s_file);
    std::fputs("\n", s_file);
    std::fflush(s_file);
}

// Scrive fmt+varargs sul file (assume s_mutex gia' detenuto dal chiamante).
// Usata da line() dopo aver preso il lock -- NON richiamare line() da dentro
// una funzione che il lock ce l'ha gia': std::mutex non e' ricorsivo, stesso
// autodeadlock documentato sopra per openLogFile() (li' al boot, qui invece
// capitava nei due line(...) dentro setEnabled(), freeze al toggle
// "Debug log" dal menu, sia accendendolo che spegnendolo).
void lineUnlocked(const char* fmt, va_list ap) {
    if (!s_enabled || !s_file) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "[%s] ", currentTime().c_str());
    size_t prefixLen = std::strlen(buf);
    std::vsnprintf(buf + prefixLen, sizeof(buf) - prefixLen, fmt, ap);
    std::snprintf(buf + std::strlen(buf), sizeof(buf) - std::strlen(buf), "\n");
    std::fputs(buf, s_file);
    std::fflush(s_file);
}

// Variante senza formattazione, per i due messaggi fissi di setEnabled()
// (evita di dover costruire un va_list in una funzione non variadica).
void writeLineUnlocked(const char* msg) {
    if (!s_enabled || !s_file) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "[%s] %s\n", currentTime().c_str(), msg);
    std::fputs(buf, s_file);
    std::fflush(s_file);
}
} // anonymous namespace

void init(const std::string& basePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_basePath = basePath;
    std::ifstream f(basePath + "debug.enable");
    if (f.good()) {
        s_enabled = true;
        openLogFile();
    }
}

bool enabled() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_enabled;
}

void setEnabled(bool on) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (on == s_enabled) return;
    if (on) {
        s_enabled = true;
        openLogFile();
        writeLineUnlocked("log ON (dal menu)");
    } else {
        writeLineUnlocked("log OFF (dal menu)");
        s_enabled = false;
    }
    // Persist across restarts: init() enables the log on startup when this
    // file exists (same "debug.enable" flag used for the file-based opt-in).
    if (!s_basePath.empty()) {
        std::string flag = s_basePath + "debug.enable";
        if (on) {
            FILE* f = std::fopen(flag.c_str(), "wb");
            if (f) std::fclose(f);
        } else {
            std::remove(flag.c_str());
        }
    }
}

void line(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(s_mutex);
    va_list ap;
    va_start(ap, fmt);
    lineUnlocked(fmt, ap);
    va_end(ap);
}

std::string logPath() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_basePath.empty()) return "";
    return s_basePath + "debug.log";
}

// Tiene solo gli ultimi maxBytes byte di un file, riscrivendolo IN PLACE
// (letto in RAM: maxBytes e' piccolo, mai il file intero — a differenza
// della vecchia versione che copiava la coda in un file .tail temporaneo:
// su SD quasi piena quel fopen("wb") poteva fallire, e in quel caso il
// taglio veniva saltato SILENZIOSAMENTE lasciando il file originale
// intero, che poi falliva l'upload con "troppo grande". Qui se la
// riscrittura fallisce si ritorna false esplicitamente, niente upload
// del file non tagliato).
static bool trimFileTailInPlace(const std::string& path, long maxBytes) {
    FILE* rf = std::fopen(path.c_str(), "rb");
    if (!rf) return false;
    std::fseek(rf, 0, SEEK_END);
    long sz = std::ftell(rf);
    if (sz <= maxBytes) { std::fclose(rf); return true; } // niente da tagliare
    std::fseek(rf, sz - maxBytes, SEEK_SET);
    std::string buf(static_cast<size_t>(maxBytes), '\0');
    size_t n = std::fread(&buf[0], 1, buf.size(), rf);
    std::fclose(rf);
    FILE* wf = std::fopen(path.c_str(), "wb");
    if (!wf) return false;
    size_t w = std::fwrite(buf.data(), 1, n, wf);
    std::fclose(wf);
    return w == n;
}

bool flushAndReopenForUpload(std::string& outPath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file) {
        std::fflush(s_file);
        std::fclose(s_file);
        s_file = nullptr;
    }
    outPath = logPath();
    if (outPath.empty()) return false;
    // Tetto upload: il log cresce senza limiti e l'invio rallenta (480KB+).
    // Tieni solo gli ultimi 256KB — la coda è quella che serve per
    // diagnosticare. Eseguito qui così il file su SD resta piccolo sempre.
    // Niente guard su s_enabled: cosi' un log vecchio accumulato con il
    // logger ora spento si puo' comunque tagliare/inviare.
    constexpr long KEEP_TAIL = 256L * 1024L;
    if (!trimFileTailInPlace(outPath, KEEP_TAIL)) return false;
    // verifica che esista (senza riaprire subito: il file resta chiuso così
    // update_net.cpp può fare fopen("rb") senza I/O error su Horizon)
    FILE* f = std::fopen(outPath.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    // lascia s_file chiuso fino a reopenAfterUpload()
    return true;
}

bool clearLog(int keepLastLines) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file) {
        std::fflush(s_file);
        std::fclose(s_file);
        s_file = nullptr;
    }
    std::string path = logPath();
    if (path.empty()) return false;
    // Coda generosa in byte (basta per centinaia di righe), poi si tiene
    // solo le ultime keepLastLines righe vere e proprie.
    constexpr long READ_TAIL = 64L * 1024L;
    FILE* rf = std::fopen(path.c_str(), "rb");
    if (!rf) return false; // niente da pulire
    std::fseek(rf, 0, SEEK_END);
    long sz = std::ftell(rf);
    long start = sz > READ_TAIL ? sz - READ_TAIL : 0;
    std::fseek(rf, start, SEEK_SET);
    std::string tail(static_cast<size_t>(sz - start), '\0');
    size_t n = std::fread(&tail[0], 1, tail.size(), rf);
    std::fclose(rf);
    tail.resize(n);
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= tail.size()) {
        size_t nl = tail.find('\n', pos);
        if (nl == std::string::npos) {
            if (pos < tail.size()) lines.push_back(tail.substr(pos));
            break;
        }
        lines.push_back(tail.substr(pos, nl - pos));
        pos = nl + 1;
    }
    int startIdx = (int)lines.size() > keepLastLines ? (int)lines.size() - keepLastLines : 0;
    FILE* wf = std::fopen(path.c_str(), "wb");
    if (!wf) return false;
    char banner[128];
    std::snprintf(banner, sizeof(banner), "===== LOG PULITO %s (tenute ultime %d righe) =====\n",
                  currentDateTime().c_str(), keepLastLines);
    std::fputs(banner, wf);
    for (int i = startIdx; i < (int)lines.size(); i++) {
        std::fputs(lines[i].c_str(), wf);
        std::fputc('\n', wf);
    }
    std::fclose(wf);
    if (s_enabled) s_file = std::fopen(path.c_str(), "a");
    return true;
}

void reopenAfterUpload() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_enabled || s_file) return;
    std::string p = logPath();
    if (!p.empty()) s_file = std::fopen(p.c_str(), "a");
}

} // namespace DebugLog

#else // OH_DEBUG_LOG

// No-op implementations already in header; nothing to compile here.

#endif // OH_DEBUG_LOG