#include "debug_log.h"
#include "app_version.h"
#include <fstream>
#include <cstdarg>
#include <cstring>

#ifdef OH_DEBUG_LOG

namespace DebugLog {

namespace {
bool s_enabled = false;
FILE* s_file = nullptr;
std::string s_basePath;

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
void openLogFile() {
    if (s_file || s_basePath.empty()) return;
    std::string logPath = s_basePath + "debug.log";
    s_file = std::fopen(logPath.c_str(), "a");
    if (!s_file) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "===== RUN %s v%s =====",
        currentDateTime().c_str(),
#ifdef APP_VERSION
        APP_VERSION
#else
        "?"
#endif
    );
    std::fputs(buf, s_file);
    std::fputs("\n", s_file);
    std::fflush(s_file);
}
} // anonymous namespace

void init(const std::string& basePath) {
    s_basePath = basePath;
    std::ifstream f(basePath + "debug.enable");
    if (f.good()) {
        s_enabled = true;
        openLogFile();
    }
}

bool enabled() {
    return s_enabled;
}

void setEnabled(bool on) {
    if (on == s_enabled) return;
    if (on) {
        s_enabled = true;
        openLogFile();
        line("log ON (dal menu)");
    } else {
        line("log OFF (dal menu)");
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
    if (!s_enabled || !s_file) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "[%s] ", currentTime().c_str());
    size_t prefixLen = std::strlen(buf);
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf + prefixLen, sizeof(buf) - prefixLen, fmt, ap);
    va_end(ap);
    std::snprintf(buf + std::strlen(buf), sizeof(buf) - std::strlen(buf), "\n");
    std::fputs(buf, s_file);
    std::fflush(s_file);
}

std::string logPath() {
    if (s_basePath.empty()) return "";
    return s_basePath + "debug.log";
}

bool flushAndReopenForUpload(std::string& outPath) {
    if (!s_enabled) return false;
    if (s_file) {
        std::fflush(s_file);
        std::fclose(s_file);
        s_file = nullptr;
    }
    outPath = logPath();
    // verifica che esista (senza riaprire subito: il file resta chiuso così
    // update_net.cpp può fare fopen("rb") senza I/O error su Horizon)
    FILE* f = std::fopen(outPath.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    // lascia s_file chiuso fino a reopenAfterUpload()
    return true;
}

void reopenAfterUpload() {
    if (!s_enabled || s_file) return;
    std::string p = logPath();
    if (!p.empty()) s_file = std::fopen(p.c_str(), "a");
}

} // namespace DebugLog

#else // OH_DEBUG_LOG

// No-op implementations already in header; nothing to compile here.

#endif // OH_DEBUG_LOG