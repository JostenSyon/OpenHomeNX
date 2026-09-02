#pragma once

#include <string>

// Selettore motore crypto — M3d rivisitato.
// Permette di tenere i file pkHouse per sicurezza e switchare gradualmente a OpenHome Rust.
// Default: PK (pkHouse). M5 attiverà OH quando i stub Rust saranno reali.

enum class CryptoEngine {
    PK, // pkHouse: SwishCrypto / PokeCrypto (source/poke_crypto.cpp ecc.)
    OH, // OpenHome: Rust openhome_switch (pkm_rs / openhome_core)
};

// Variabile globale inline (C++17) — modificabile a runtime o via settings.
// Default OH (OpenHome): le letture non-SwSh ricadono comunque su PK
// (openhome_load_save torna NULL fuori da SwSh), quindi è sicuro come default.
// Sovrascritto da crypto.cfg all'avvio se presente.
inline CryptoEngine g_cryptoEngine = CryptoEngine::OH;

inline bool useOpenHome() { return g_cryptoEngine == CryptoEngine::OH; }
inline bool usePkHouse()  { return g_cryptoEngine == CryptoEngine::PK; }

inline void setCryptoEngine(CryptoEngine e) { g_cryptoEngine = e; }

// Persistenza su file (crypto.cfg): 0=PK, 1=OH
int loadCryptoEngine(const std::string& basePath);
void saveCryptoEngine(const std::string& basePath, CryptoEngine engine);
