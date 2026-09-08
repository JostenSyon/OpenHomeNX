#include "save_file_ffi.h"
#include "swish_crypto.h"

namespace SaveFileFFI {

void _ensure_linked() {
    (void)openhome_init;
    (void)openhome_load_save;
}

// Nota: la cifratura SCBlock resta SwishCrypto (C++) per entrambi i motori —
// il core Rust gestisce parsing/transfer, non il container cifrato.
// I rami OH/PK identici sono stati collassati (M5 chiusa).
std::vector<SCBlock> decrypt(uint8_t* fileData, size_t fileSize) {
    return SwishCrypto::decrypt(fileData, fileSize);
}
std::vector<uint8_t> encrypt(const std::vector<SCBlock>& blocks) {
    return SwishCrypto::encrypt(blocks);
}
SCBlock* findBlock(std::vector<SCBlock>& blocks, uint32_t key) {
    return SwishCrypto::findBlock(blocks, key);
}
const SCBlock* findBlock(const std::vector<SCBlock>& blocks, uint32_t key) {
    return SwishCrypto::findBlock(blocks, key);
}

} // namespace SaveFileFFI
