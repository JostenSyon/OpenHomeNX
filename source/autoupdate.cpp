#include "autoupdate.h"
#include "update_net.h"
#include "nro_version.h"
#include "debug_log.h"
#include <switch.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {

// Stack dedicato al thread (libnx vuole allineamento 16). 64KB: larghi per
// una fetch curl + parse JSON piatto.
alignas(16) static uint8_t s_stack[64 * 1024];
static Thread s_thread;
static std::atomic<bool> s_started{false};
static std::atomic<int> s_state{0}; // 0 idle, 1 working, 2 done
static char s_version[32] = {0};
static char s_err[160] = {0};
static std::string s_url, s_token, s_cur;

void workerMain(void*) {
    RemoteUpdateInfo info;
    std::string err;
    // Niente DebugLog qui (non thread-safe): esito+errore in statici, li
    // logga takeResult() che gira sul main thread. Il main rifà comunque la
    // fetch veloce quando mostra il prompt.
    if (updateNetFetchInfo(s_url, s_token, info, err)) {
        if (compareVersionStrings(info.version, s_cur) > 0)
            std::snprintf(s_version, sizeof(s_version), "%s", info.version.c_str());
    } else if (!err.empty()) {
        std::snprintf(s_err, sizeof(s_err), "%s", err.substr(0, sizeof(s_err) - 1).c_str());
    }
    s_state.store(2, std::memory_order_release);
}

} // namespace

void autoUpdateStart(const std::string& url, const std::string& token,
                     const std::string& curVer) {
    bool expected = false;
    if (!s_started.compare_exchange_strong(expected, true))
        return; // una sola partenza per boot
    s_url = url;
    s_token = token;
    s_cur = curVer;
    s_state.store(1, std::memory_order_release);
    threadCreate(&s_thread, workerMain, nullptr, s_stack, sizeof(s_stack),
                 0x2C, -2);
    threadStart(&s_thread);
    // Mai joinato: curl ha i suoi timeout, all'exit il processo lo raccoglie.
}

bool autoUpdateTakeResult(std::string& outVersion) {
    if (s_state.load(std::memory_order_acquire) != 2)
        return false;
    s_state.store(0, std::memory_order_release); // consuma una sola volta
    if (s_version[0] == '\0') {
        // Una sola riga per boot: distingue "pari, tutto ok" da "fetch fallita",
        // altrimenti il silenzio sembra "non attivo".
        DebugLog::line("autoupdate: niente prompt (%s)",
            s_err[0] ? s_err : "già aggiornato");
        return false;
    }
    outVersion = s_version;
    return true;
}

bool readUpdateAutoCfg(const std::string& basePath, std::string& urlOut,
                       std::string& tokenOut) {
    // Stessi path di readUpdateCfg in ui_selectors.cpp (duplicato a posta:
    // quello è statico in un anonymous namespace).
    const std::string paths[] = { basePath + "update.cfg",
                                  "sdmc:/switch/OpenHomeNX/update.cfg" };
    bool autoOn = false;
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (!f.good()) continue;
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
            if (k == "url") urlOut = v;
            else if (k == "token") tokenOut = v;
            else if (k == "auto" && (v == "1" || v == "on" || v == "yes")) autoOn = true;
        }
        if (autoOn) return true;
    }
    return false;
}
