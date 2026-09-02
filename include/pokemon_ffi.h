#pragma once
#include "openhome_ffi.h"
#include "crypto_engine.h"
#include "poke_crypto.h"
#include <vector>
#include <cstdint>

// Wrapper FFI per Pokemon — selettore PK/OH (M3d).
namespace PokemonFFI {

inline PkmHandle* load(const std::vector<uint8_t>& data) {
    return OpenHomeNX::loadPkm(data);
}
inline PkmHandle* load(const uint8_t* data, size_t len) {
    return openhome_load_pkm(data, len);
}
inline void free(PkmHandle* h) { OpenHomeNX::freePkm(h); }
inline PkmHandle* transfer(PkmHandle* h, uint32_t targetGen) {
    return OpenHomeNX::transferPkm(h, targetGen);
}
inline bool saveToSlot(PkmHandle* h, uint32_t slot) {
    return OpenHomeNX::savePkmToFile(h, slot);
}

// PokeCrypto wrappers — M3b: pokemon.cpp usa PokemonFFI:: invece di PokeCrypto::
// M3d: dispatch PK/OH — PK chiama PokeCrypto, OH chiamerà Rust (M5)
inline void decryptArray3(const uint8_t* ekm, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::decryptArray3(ekm, len, outBuf); return; }
    PokeCrypto::decryptArray3(ekm, len, outBuf);
}
inline void encryptArray3(const uint8_t* pk, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::encryptArray3(pk, len, outBuf); return; }
    PokeCrypto::encryptArray3(pk, len, outBuf);
}
inline void decryptArray6(const uint8_t* ekm, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::decryptArray6(ekm, len, outBuf); return; }
    PokeCrypto::decryptArray6(ekm, len, outBuf);
}
inline void encryptArray6(const uint8_t* pk, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::encryptArray6(pk, len, outBuf); return; }
    PokeCrypto::encryptArray6(pk, len, outBuf);
}
inline void decryptArray8A(const uint8_t* ekm, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::decryptArray8A(ekm, len, outBuf); return; }
    PokeCrypto::decryptArray8A(ekm, len, outBuf);
}
inline void encryptArray8A(const uint8_t* pk, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::encryptArray8A(pk, len, outBuf); return; }
    PokeCrypto::encryptArray8A(pk, len, outBuf);
}
inline void decryptArray9(const uint8_t* ekm, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::decryptArray9(ekm, len, outBuf); return; }
    PokeCrypto::decryptArray9(ekm, len, outBuf);
}
inline void encryptArray9(const uint8_t* pk, size_t len, uint8_t* outBuf) {
    if (useOpenHome()) { PokeCrypto::encryptArray9(pk, len, outBuf); return; }
    PokeCrypto::encryptArray9(pk, len, outBuf);
}

// Size constants — re-export per evitare PokeCrypto:: in pokemon.cpp
constexpr int SIZE_3STORED = PokeCrypto::SIZE_3STORED;
constexpr int SIZE_3PARTY  = PokeCrypto::SIZE_3PARTY;
constexpr int SIZE_6STORED = PokeCrypto::SIZE_6STORED;
constexpr int SIZE_6PARTY  = PokeCrypto::SIZE_6PARTY;
constexpr int SIZE_8ASTORED = PokeCrypto::SIZE_8ASTORED;
constexpr int SIZE_8APARTY  = PokeCrypto::SIZE_8APARTY;
constexpr int SIZE_9STORED = PokeCrypto::SIZE_9STORED;
constexpr int SIZE_9PARTY  = PokeCrypto::SIZE_9PARTY;
constexpr int SIZE_8ABLOCK  = PokeCrypto::SIZE_8ABLOCK;
constexpr int MAX_PARTY_SIZE = PokeCrypto::MAX_PARTY_SIZE;

} // namespace PokemonFFI
