// SHIM OpenHomeNX: implementa le 4 interfacce Sphaira usate dal percorso
// forwarder (ProgressBox, App::Notify, ini_browse). Niente logica copiata.
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "minIni.h"
#include "debug_log.h"
#include "forwarder.h"

namespace sphaira::ui {

ProgressBox& ProgressBox::SetTitle(const std::string& title) {
    DebugLog::line("fwd: %s", title.c_str());
    forwarderNotifyProgress(title);
    return *this;
}
ProgressBox& ProgressBox::SetImageDataConst(std::span<const u8>) {
    return *this;
}
ProgressBox& ProgressBox::NewTransfer(const std::string& transfer) {
    DebugLog::line("fwd: %s", transfer.c_str());
    forwarderNotifyProgress(transfer);
    return *this;
}
ProgressBox& ProgressBox::UpdateTransfer(s64, s64) {
    return *this;
}

} // namespace sphaira::ui

namespace sphaira {

void App::Notify(const std::string& msg) {
    DebugLog::line("fwd notify: %s", msg.c_str());
}

} // namespace sphaira

extern "C" int ini_browse(int (*)(const mTCHAR*, const mTCHAR*, const mTCHAR*, void*),
                          void*, const mTCHAR*) {
    return 0; // mai letto: usiamo solo chiavi on-device via spl
}
