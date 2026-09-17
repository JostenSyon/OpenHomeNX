// Colla OpenHomeNX -> Sphaira vendored (vendor/sphaira, codice 1:1).
// Tutta la logica forwarder (PFS0/RomFS/IVFC/NCA/CNMT + install ncm/ns)
// è quella di Sphaira (GPLv3); qui solo riempimento OwoConfig e mapping errori.
#include "forwarder.h"
#include "debug_log.h"
#include "owo.hpp"
#include "ui/progress_box.hpp"
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
ForwarderProgressFn g_progressCb;
} // namespace

void forwarderNotifyProgress(const std::string& msg) {
    if (g_progressCb) g_progressCb(msg);
}

static bool readFile(const char* path, std::vector<u8>& out, size_t maxSize) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > maxSize) { fclose(f); return false; }
    out.resize(sz);
    size_t r = fread(out.data(), 1, sz, f);
    fclose(f);
    return r == (size_t)sz;
}

bool forwarderInstall(const std::string& nroPath, std::string& err,
                       ForwarderProgressFn onProgress) {
    err.clear();
    g_progressCb = onProgress;
    // Ripulisce il callback su ogni via d'uscita (successo, ogni return
    // false, o un'eventuale eccezione) cosi' una chiamata successiva senza
    // callback non eredita quello di questa.
    struct ProgressGuard { ~ProgressGuard() { g_progressCb = nullptr; } } progressGuard;

    // NACP vero dal template ufficiale del build (come Sphaira legge dal NRO).
    NacpStruct nacp{};
    std::vector<u8> tpl;
    if (!readFile("romfs:/forwarder/template.nacp", tpl, sizeof(nacp)) ||
        tpl.size() < sizeof(nacp)) {
        err = "template.nacp mancante in romfs:/forwarder/";
        DebugLog::line("forwarder: %s", err.c_str());
        return false;
    }
    memcpy(&nacp, tpl.data(), sizeof(nacp));

    // Icona 256x256 dal romfs.
    std::vector<u8> icon;
    if (!readFile("romfs:/icon.jpg", icon, 1 << 20)) {
        err = "icon.jpg mancante in romfs:/";
        DebugLog::line("forwarder: %s", err.c_str());
        return false;
    }

    sphaira::OwoConfig config;
    config.nro_path = nroPath;
    config.args = nroPath;
    config.name = "OpenHomeNX";
    config.author = "JostenSyon";
    config.nacp = nacp;
    config.icon = icon;

    sphaira::ui::ProgressBox pbox;
    Result rc = sphaira::install_forwarder(&pbox, config, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        // Il dettaglio (rc/desc/mod) va solo nel debug.log: e' l'unico
        // consumatore reale, il chiamante in ui_selectors.cpp mostra
        // sempre il testo gentile all'utente e non ripete `err` nel log.
        err = "install fallita";
        DebugLog::line("forwarder: rc=0x%X desc=%u mod=%u", rc, R_DESCRIPTION(rc), R_MODULE(rc));
        return false;
    }
    DebugLog::line("forwarder: installato ok");
    return true;
}
