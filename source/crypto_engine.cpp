#include "crypto_engine.h"
#include <cstdio>
#include <cstdint>

int loadCryptoEngine(const std::string& basePath) {
    std::string path = basePath + "crypto.cfg";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 1; // nessun file → default OH
    uint8_t val = 1;
    if (std::fread(&val, 1, 1, f) != 1) val = 1; // lettura fallita → default OH
    std::fclose(f);
    if (val > 1) val = 1; // valore invalido → default OH
    return val;
}

void saveCryptoEngine(const std::string& basePath, CryptoEngine engine) {
    std::string path = basePath + "crypto.cfg";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t val = (engine == CryptoEngine::OH) ? 1 : 0;
    std::fwrite(&val, 1, 1, f);
    std::fclose(f);
}
