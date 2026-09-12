#include "autocheck_usb.h"
#include "settings_cfg.h"

bool loadAutoCheckUsb(const std::string& basePath) {
    (void)basePath;
    return Settings::autoCheckUsb(); // nessun file -> default off
}

void saveAutoCheckUsb(const std::string& basePath, bool enabled) {
    (void)basePath;
    Settings::setAutoCheckUsb(enabled);
}
