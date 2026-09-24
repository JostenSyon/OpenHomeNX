// OpenHomeNX - stub forwarder (installazione launcher NCA) per build Linux/macOS.
#include <string>
#include <functional>
#include "forwarder.h"
#include "debug_log.h"

void forwarderNotifyProgress(const std::string&) {}

bool forwarderInstall(const std::string&, std::string& err, ForwarderProgressFn) {
    err = "forwarder non disponibile su questa piattaforma";
    DebugLog::line("forwarder: non disponibile (build Linux)");
    return false;
}