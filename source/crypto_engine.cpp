#include "crypto_engine.h"
#include "settings_cfg.h"

int loadCryptoEngine(const std::string& basePath) {
    (void)basePath;
    int val = Settings::cryptoEngine();
    if (val < 0 || val > 1) val = 1; // valore invalido → default OH
    return val;
}

void saveCryptoEngine(const std::string& basePath, CryptoEngine engine) {
    (void)basePath;
    Settings::setCryptoEngine((engine == CryptoEngine::OH) ? 1 : 0);
}
