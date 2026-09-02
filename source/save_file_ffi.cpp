#include "save_file_ffi.h"
#include "swish_crypto.h"

namespace SaveFileFFI {

void _ensure_linked() {
    (void)openhome_init;
    (void)openhome_load_save;
}

std::vector<SCBlock> decrypt(uint8_t* fileData, size_t fileSize) {
    if (useOpenHome()) {
        // TODO M5: delegare a Rust openhome_switch (stub ritorna vuoto per ora)
        // Fallback PK per non rompere save esistenti
        return SwishCrypto::decrypt(fileData, fileSize);
    }
    return SwishCrypto::decrypt(fileData, fileSize);
}
std::vector<uint8_t> encrypt(const std::vector<SCBlock>& blocks) {
    if (useOpenHome()) {
        return SwishCrypto::encrypt(blocks);
    }
    return SwishCrypto::encrypt(blocks);
}
SCBlock* findBlock(std::vector<SCBlock>& blocks, uint32_t key) {
    if (useOpenHome()) {
        return SwishCrypto::findBlock(blocks, key);
    }
    return SwishCrypto::findBlock(blocks, key);
}
const SCBlock* findBlock(const std::vector<SCBlock>& blocks, uint32_t key) {
    if (useOpenHome()) {
        return SwishCrypto::findBlock(blocks, key);
    }
    return SwishCrypto::findBlock(blocks, key);
}

} // namespace SaveFileFFI
