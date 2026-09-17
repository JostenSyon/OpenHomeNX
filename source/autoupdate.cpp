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

// Stack allocato da libnx (stack_mem=NULL): lo statico in .bss falliva con
// LibnxError_OutOfMemory (0x559) su HW. 64KB: larghi per fetch+parse.
// (threadClose in Join libera la memoria; un solo thread per boot.)
static constexpr size_t kStackSize = 64 * 1024;
static Thread s_thread;
static std::atomic<bool> s_started{false};
static std::atomic<int> s_state{0}; // 0 idle, 1 working, 2 done
static std::atomic<int> s_stage{0}; // 0 creato, 1 attesa link, 2 in fetch, 3 fetch ok, 4 confrontato
static std::atomic<bool> s_logged{false};
static std::atomic<bool> s_stop{false};
static std::atomic<bool> s_running{false}; // thread vivo: solo allora join ha senso
static char s_version[32] = {0};
static char s_err[160] = {0};
static std::string s_url, s_token, s_cur;
static bool s_beta = false;

void workerMain(void*) {
    RemoteUpdateInfo info;
    std::string err;
    // Niente DebugLog qui (non thread-safe): esito+errore in statici, li
    // logga autoUpdateLogOnceDone() che gira sul main thread. Il main rifà
    // comunque la fetch veloce quando mostra il prompt.
    // Prima aspetta il LINK vero: socketInitializeDefault riesce molto prima
    // che il WiFi si associ, e la fetch a boot+3s moriva sempre in silenzio
    // (mai vista dal server). Timeout ~46s, poi si prova comunque.
    s_stage.store(1, std::memory_order_release);
    for (int i = 0; i < 23 && !s_stop.load(std::memory_order_acquire); i++) {
        if (std::strcmp(updateNetLinkStr(), "OFF") != 0)
            break;
        svcSleepThread(2000000000ULL);
    }
    if (s_stop.load(std::memory_order_acquire)) {
        s_state.store(2, std::memory_order_release);
        return; // uscita in corso: niente fetch a metà teardown
    }
    s_stage.store(2, std::memory_order_release);
    std::string fetchUrl = s_url;
    if (s_beta && fetchUrl.empty()) {
        // Canale beta senza custom url: risolvi la pre-release corrente.
        // Fallimento = niente prompt (mai fallback silenzioso sullo stabile).
        std::string base, tag, berr;
        if (updateNetFetchBetaBase("JostenSyon", "OpenHomeNX", s_token, base, tag, berr))
            fetchUrl = base;
        else {
            if (!berr.empty() && berr != "none")
                std::snprintf(s_err, sizeof(s_err), "beta: %s", berr.substr(0, sizeof(s_err) - 8).c_str());
            s_stage.store(4, std::memory_order_release);
            s_state.store(2, std::memory_order_release);
            return;
        }
    }
    bool ok = updateNetFetchInfo(fetchUrl, s_token, info, err);
    s_stage.store(2, std::memory_order_release);
    if (ok) {
        if (compareVersionStrings(info.version, s_cur) > 0)
            std::snprintf(s_version, sizeof(s_version), "%s", info.version.c_str());
    } else if (!err.empty()) {
        std::snprintf(s_err, sizeof(s_err), "%s", err.substr(0, sizeof(s_err) - 1).c_str());
    }
    s_stage.store(4, std::memory_order_release);
    s_state.store(2, std::memory_order_release);
}

} // namespace

void autoUpdateStart(const std::string& url, const std::string& token,
                     const std::string& curVer, bool beta) {
    bool expected = false;
    if (!s_started.compare_exchange_strong(expected, true))
        return; // una sola partenza per boot
    s_url = url;
    s_token = token;
    s_cur = curVer;
    s_beta = beta && url.empty(); // custom url vince sempre sul canale
    s_state.store(1, std::memory_order_release);
    Result rcCreate = threadCreate(&s_thread, workerMain, nullptr, nullptr,
                                   kStackSize, 0x2C, -2);
    Result rcStart = R_SUCCEEDED(rcCreate)
        ? threadStart(&s_thread) : rcCreate;
    if (R_SUCCEEDED(rcCreate) && R_SUCCEEDED(rcStart))
        s_running.store(true, std::memory_order_release);
    DebugLog::line("autoupdate: thread create=0x%08X start=0x%08X",
                   (unsigned)rcCreate, (unsigned)rcStart);
    if (R_FAILED(rcCreate) || R_FAILED(rcStart)) {
        std::snprintf(s_err, sizeof(s_err), "thread create/start 0x%08X/0x%08X",
                      (unsigned)rcCreate, (unsigned)rcStart);
        s_state.store(2, std::memory_order_release); // mai appeso su start fallita
    }
}

bool autoUpdateTakeResult(std::string& outVersion) {
    if (s_state.load(std::memory_order_acquire) != 2)
        return false;
    s_state.store(0, std::memory_order_release); // consuma una sola volta
    if (s_version[0] == '\0')
        return false; // fetch fallita o niente di nuovo (loggato da LogOnceDone)
    outVersion = s_version;
    return true;
}

bool autoUpdateFinishedWithoutUpdate() {
    return s_state.load(std::memory_order_acquire) == 2 && s_version[0] == '\0';
}

bool autoUpdateSettled() {
    return s_state.load(std::memory_order_acquire) != 1; // 1 = worker al lavoro
}

// Atteso in main() prima di smontare rete/USB: evita che il worker usi
// socket/stringhe statiche a metà teardown (crash in uscita). Ritorna in
// ~ms se idle/finito o in attesa link (stop a granularità 2s); nel peggiore
// dei casi aspetta la fetch in corso (timeout curl). Mai due volte.
void autoUpdateJoin() {
    if (!s_started.load(std::memory_order_acquire))
        return;
    s_started.store(false, std::memory_order_release);
    if (!s_running.load(std::memory_order_acquire))
        return; // start fallita: nessun thread da aspettare
    s_stop.store(true, std::memory_order_release);
    threadWaitForExit(&s_thread);
    threadClose(&s_thread);
    DebugLog::line("autoupdate: worker joined");
}

void autoUpdateLogOnceDone() {
    if (s_state.load(std::memory_order_acquire) != 2)
        return;
    bool expected = false;
    if (!s_logged.compare_exchange_strong(expected, true))
        return; // una sola riga per boot, su qualsiasi screen
    if (s_version[0] != '\0')
        DebugLog::line("autoupdate: pronta v%s (prompt in home giochi)", s_version);
    else
        DebugLog::line("autoupdate: niente prompt (%s), stage=%d",
            s_err[0] ? s_err : "già aggiornato",
            s_stage.load(std::memory_order_acquire));
}

bool readUpdateAutoCfg(const std::string& basePath, std::string& urlOut,
                       std::string& tokenOut, std::string& channelOut) {
    channelOut.clear();
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
            else if (k == "channel") channelOut = v;
            else if (k == "auto" && (v == "1" || v == "on" || v == "yes")) autoOn = true;
        }
        if (autoOn) return true;
    }
    return false;
}
