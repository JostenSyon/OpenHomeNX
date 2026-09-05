#include "autocheck_usb.h"
#include <cstdio>
#include <cstdint>

bool loadAutoCheckUsb(const std::string& basePath) {
    std::string path = basePath + "autocheck_usb.cfg";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false; // nessun file -> default off
    uint8_t val = 0;
    if (std::fread(&val, 1, 1, f) != 1) val = 0;
    std::fclose(f);
    return val != 0;
}

void saveAutoCheckUsb(const std::string& basePath, bool enabled) {
    std::string path = basePath + "autocheck_usb.cfg";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t val = enabled ? 1 : 0;
    std::fwrite(&val, 1, 1, f);
    std::fclose(f);
}
