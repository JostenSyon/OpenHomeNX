// OpenHomeNX - stub LED + misc system per build Linux/macOS.
#include <string>
#include "../../OpenHomeNX/include/led.h"
#include "../../OpenHomeNX/include/nro_version.h"
#include "../../OpenHomeNX/include/autocheck_usb.h"
#include "../../OpenHomeNX/include/debug_log.h"

void ledInit() {}
void ledInitWithPath(const char*) {}
void ledExit() {}
void ledBlink() {}
void ledOff() {}

bool readNroDisplayVersion(const std::string&, std::string& out) { out = ""; return false; }
int compareVersionStrings(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    return a < b ? -1 : 1;
}
bool copyFileTo(const std::string& src, const std::string& dst) {
    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[65536]; size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
        std::fwrite(buf, 1, n, out);
    std::fclose(out); std::fclose(in);
    return true;
}

bool loadAutoCheckUsb(const std::string&) { return false; }
void saveAutoCheckUsb(const std::string&, bool) {}