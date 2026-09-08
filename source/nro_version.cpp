#include "nro_version.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <switch.h>    // fsdevCommitDevice

namespace {

constexpr uint32_t NRO_MAGIC  = 0x304F524E; // "NRO0" little-endian
constexpr uint32_t ASET_MAGIC = 0x54455341; // "ASET" little-endian

// Offset of DisplayVersion (char[0x10]) inside a NACP record. Stable ABI value
// (see libnx <switch/nacp.h> NacpStruct).
constexpr long NACP_DISPLAY_VERSION_OFFSET = 0x3060;
constexpr long NACP_DISPLAY_VERSION_LEN    = 0x10;

bool readAt(FILE* f, long off, void* buf, size_t n) {
    return off >= 0
        && std::fseek(f, off, SEEK_SET) == 0
        && std::fread(buf, 1, n, f) == n;
}

} // namespace

bool readNroDisplayVersion(const std::string& nroPath, std::string& outVersion) {
    FILE* f = std::fopen(nroPath.c_str(), "rb");
    if (!f)
        return false;

    bool ok = false;
    do {
        // NRO header sits at file offset 0x10: magic, version, size, flags, ...
        uint32_t hdr[4] = {0};
        if (!readAt(f, 0x10, hdr, sizeof(hdr)))
            break;
        if (hdr[0] != NRO_MAGIC)
            break;
        const long nroSize = static_cast<long>(hdr[2]); // size @ 0x18
        if (nroSize < 0x40)
            break;

        // Asset header ("ASET") is appended right after the NRO image.
        uint8_t aset[0x38] = {0};
        if (!readAt(f, nroSize, aset, sizeof(aset)))
            break;
        uint32_t asetMagic = 0;
        std::memcpy(&asetMagic, aset + 0x00, 4);
        if (asetMagic != ASET_MAGIC)
            break;

        uint64_t nacpOff = 0, nacpSize = 0;
        std::memcpy(&nacpOff,  aset + 0x18, 8); // nacp.offset (relative to ASET)
        std::memcpy(&nacpSize, aset + 0x20, 8); // nacp.size
        if (nacpSize < static_cast<uint64_t>(NACP_DISPLAY_VERSION_OFFSET + NACP_DISPLAY_VERSION_LEN))
            break;

        char ver[NACP_DISPLAY_VERSION_LEN + 1] = {0};
        const long verPos = nroSize + static_cast<long>(nacpOff) + NACP_DISPLAY_VERSION_OFFSET;
        if (!readAt(f, verPos, ver, NACP_DISPLAY_VERSION_LEN))
            break;
        ver[NACP_DISPLAY_VERSION_LEN] = '\0';

        outVersion = ver;
        ok = !outVersion.empty();
    } while (false);

    std::fclose(f);
    return ok;
}

bool copyFileTo(const std::string& src, const std::string& dst) {
    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in)
        return false;
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    bool ok = true;
    static thread_local uint8_t buf[65536];
    for (;;) {
        size_t n = std::fread(buf, 1, sizeof(buf), in);
        if (n == 0)
            break;
        if (std::fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    if (ok && std::ferror(in))
        ok = false;
    if (std::fflush(out) != 0)
        ok = false;
    std::fclose(in);
    std::fclose(out);
    // Durabilità: senza il commit, la scrittura resta nella cache FS di libnx e
    // un exit/relaunch immediato (self-update) rilancia il .nro VECCHIO o
    // troncato — era la causa di "aggiorna, riavvia, ma sono ancora alla vecchia
    // versione" (e del .nro finalizzato con size sbagliata, 17572402 vs 17576498).
    if (ok && dst.rfind("sdmc:/", 0) == 0) {
        if (R_FAILED(fsdevCommitDevice("sdmc")))
            fsdevCommitDevice("sdmc:");
    }
    return ok;
}

int compareVersionStrings(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s, int out[4]) {
        out[0] = out[1] = out[2] = out[3] = 0;
        int idx = 0;
        size_t i = 0;
        while (idx < 4 && i < s.size()) {
            int v = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i] - '0');
                any = true;
                ++i;
            }
            if (any)
                out[idx++] = v;
            while (i < s.size() && (s[i] < '0' || s[i] > '9'))
                ++i; // skip '.', '-', spaces, etc.
        }
    };
    int va[4], vb[4];
    parse(a, va);
    parse(b, vb);
    for (int i = 0; i < 4; ++i) {
        if (va[i] != vb[i])
            return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}
