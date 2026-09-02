#pragma once
#include "openhome_ffi.h"
#include "crypto_engine.h"
#include "sc_block.h"
#include <string>
#include <vector>
#include <fstream>

// Wrapper FFI per SaveFile — selettore PK/OH (M3d).
// M3a: espone crypto SCBlock via FFI per disaccoppiare save_file.cpp da SwishCrypto.
// M3d: dispatch su g_cryptoEngine — PK usa SwishCrypto, OH userà Rust (M5).
namespace SaveFileFFI {

// Carica un save file via Rust. Ritorna handle opaco o nullptr.
inline SaveHandle* load(const std::string& path) {
    if (useOpenHome()) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return nullptr;
        size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<uint8_t> data(size);
        if (!f.read(reinterpret_cast<char*>(data.data()), size)) return nullptr;
        return openhome_load_save(data.data(), data.size());
    }
    return OpenHomeNX::loadSaveFromPath(path);
}
inline SaveHandle* load(const std::vector<uint8_t>& data) {
    return openhome_load_save(data.data(), data.size());
}
inline void free(SaveHandle* h) { OpenHomeNX::freeSave(h); }
inline uint32_t pokemonCount(SaveHandle* h) { return OpenHomeNX::getPokemonCount(h); }
inline uint32_t boxCount(SaveHandle* h) { return OpenHomeNX::getBoxCount(h); }
inline PkmHandle* getSlot(SaveHandle* save, uint32_t box, uint32_t slot) {
    return OpenHomeNX::getPokemonFromSlot(save, box, slot);
}

// Crypto SCBlock — dispatcher PK/OH
std::vector<SCBlock> decrypt(uint8_t* fileData, size_t fileSize);
std::vector<uint8_t> encrypt(const std::vector<SCBlock>& blocks);
SCBlock* findBlock(std::vector<SCBlock>& blocks, uint32_t key);
const SCBlock* findBlock(const std::vector<SCBlock>& blocks, uint32_t key);

} // namespace SaveFileFFI
