#pragma once
#include <cstdint>
#include <cstddef>

// PokeCrypto - Pokemon entity encryption/decryption for Gen8/Gen9.
// Ported from PKHeX.Core/PKM/Util/PokeCrypto.cs
namespace PokeCrypto {

    // Gen8/Gen9 shared (PK8, PB8, PA9)
    constexpr int SIZE_9STORED = 0x148; // 328 bytes (box format)
    constexpr int SIZE_9PARTY  = 0x158; // 344 bytes (party format)
    constexpr int BLOCK_SIZE   = 0x50;  // 80 bytes per block
    constexpr int BLOCK_COUNT  = 4;

    // Gen8a (PA8 — Legends: Arceus)
    constexpr int SIZE_8ASTORED = 0x168; // 360 bytes (box format)
    constexpr int SIZE_8APARTY  = 0x178; // 376 bytes (party format)
    constexpr int SIZE_8ABLOCK  = 0x58;  // 88 bytes per block

    // Gen6/Gen7 (PB7 — Let's Go Pikachu/Eevee)
    constexpr int SIZE_6STORED = 0xE8;   // 232 bytes
    constexpr int SIZE_6PARTY  = 0x104;  // 260 bytes (LGPE boxes store party format)
    constexpr int SIZE_6BLOCK  = 56;     // 56 bytes per block

    // Gen3 (PK3 — FireRed/LeafGreen)
    constexpr int SIZE_3STORED = 80;     // 80 bytes (box format)
    constexpr int SIZE_3PARTY  = 100;    // 100 bytes (party format)
    constexpr int SIZE_3HEADER = 32;     // unencrypted header
    constexpr int SIZE_3BLOCK  = 12;     // 12 bytes per block

    // Gen4/Gen5 (PK4/PK5 — DPPt/HGSS, BW/B2W2)
    constexpr int SIZE_4STORED = 136;    // 136 bytes (box format)
    constexpr int SIZE_4PARTY  = 236;    // 236 bytes (party format)
    constexpr int SIZE_5STORED = 136;    // 136 bytes (box format)
    constexpr int SIZE_5PARTY  = 220;    // 220 bytes (party format)
    constexpr int SIZE_45BLOCK = 32;     // 32 bytes per block

    // Largest party size across all formats (for Pokemon data array sizing)
    constexpr int MAX_PARTY_SIZE = SIZE_8APARTY; // 0x178

    // LCG-based XOR cipher on uint16 pairs.
    void cryptArray(uint8_t* data, size_t len, uint32_t seed);

    // Decrypt/encrypt Gen8/Gen9 Pokemon data (PK8, PB8, PA9).
    void decryptArray9(const uint8_t* ekm, size_t len, uint8_t* outBuf);
    void encryptArray9(const uint8_t* pk, size_t len, uint8_t* outBuf);

    // Decrypt/encrypt Gen8a Pokemon data (PA8 — Legends: Arceus).
    void decryptArray8A(const uint8_t* ekm, size_t len, uint8_t* outBuf);
    void encryptArray8A(const uint8_t* pk, size_t len, uint8_t* outBuf);

    // Decrypt/encrypt Gen6/Gen7 Pokemon data (PB7 — Let's Go Pikachu/Eevee).
    void decryptArray6(const uint8_t* ekm, size_t len, uint8_t* outBuf);
    void encryptArray6(const uint8_t* pk, size_t len, uint8_t* outBuf);

    // Decrypt/encrypt Gen3 Pokemon data (PK3 — FireRed/LeafGreen).
    // Gen3 uses PID^OID seed with constant XOR (not LCG) + 4x12-byte block shuffle.
    void decryptArray3(const uint8_t* ekm, size_t len, uint8_t* outBuf);
    void encryptArray3(const uint8_t* pk, size_t len, uint8_t* outBuf);

    // Decrypt/encrypt Gen4/Gen5 Pokemon data (PK4/PK5 — DPPt/HGSS, BW/B2W2).
    // Same LCG stream as Gen6+ but seeded by CHECKSUM (party tail by PID),
    // 4x32-byte blocks, sv = (pid >> 13) & 31 (PKHeX PokeCrypto Decrypt45).
    void decryptArray45(const uint8_t* ekm, size_t len, uint8_t* outBuf);
    void encryptArray45(const uint8_t* pk, size_t len, uint8_t* outBuf);
    // Encrypted at rest when the unused ribbon block is nonzero (PKHeX IsEncrypted45).
    bool isEncrypted45(const uint8_t* data, size_t len);

} // namespace PokeCrypto
