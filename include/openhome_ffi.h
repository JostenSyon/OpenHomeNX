#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SaveHandle SaveHandle;
typedef struct PkmHandle PkmHandle;
typedef struct PokemonInfo PokemonInfo;
typedef struct Format Format;
typedef struct FormatList FormatList;

bool openhome_init(void);
void openhome_deinit(void);
SaveHandle *openhome_load_save(const uint8_t *data, size_t len);
SaveHandle *openhome_load_save_from_path(const char *path); // legacy, calls load_save with file bytes
void openhome_free_save(SaveHandle *handle);
PkmHandle *openhome_load_pkm(const uint8_t *data, size_t len);
// Stored (box) bytes of a concrete format -> OHPKM handle. Needed because
// openhome_load_pkm only accepts real OHPKM files (magic + version 2), so it
// cannot ingest a Pk7/Pk8/Pk9 record read out of a save. NULL on failure.
PkmHandle *openhome_load_pkm_from_gen(const uint8_t *data, size_t len, uint32_t gen);
void openhome_free_pkm(PkmHandle *handle);
uint32_t openhome_get_pokemon_count(SaveHandle *handle);
uint32_t openhome_get_box_count(SaveHandle *handle);
PkmHandle *openhome_get_pokemon_from_slot(SaveHandle *handle, uint32_t box_idx, uint32_t slot_idx);
PkmHandle *openhome_transfer_pkm(PkmHandle *pkm_handle, uint32_t target_gen);
bool openhome_save_pkm_to_file(PkmHandle *pkm_handle, uint32_t slot);
uint32_t openhome_get_pkm_box_bytes(PkmHandle *pkm_handle, uint8_t *out_buf, size_t out_len);
uint32_t openhome_get_pkm_box_bytes_for_gen(PkmHandle *pkm_handle, uint32_t gen, uint8_t *out_buf, size_t out_len);
uint32_t openhome_get_pkm_original_backup(PkmHandle *pkm_handle, uint8_t *out_buf, size_t out_len);
// OHPKM universal storage (bank cross-gen)
uint32_t openhome_get_ohpkm_bytes(PkmHandle *handle, uint8_t *out_buf, size_t out_len);
PkmHandle *openhome_load_ohpkm(const uint8_t *data, size_t len);
uint16_t openhome_ohpkm_species(PkmHandle *handle);
uint16_t openhome_ohpkm_form(PkmHandle *handle);
uint8_t openhome_ohpkm_level(PkmHandle *handle);
bool openhome_ohpkm_is_shiny(PkmHandle *handle);
uint8_t openhome_ohpkm_gender(PkmHandle *handle);
uint16_t openhome_ohpkm_held_item(PkmHandle *handle);
uint8_t openhome_ohpkm_origin_gen(PkmHandle *handle);
uint32_t openhome_ohpkm_nickname(PkmHandle *handle, uint8_t *out_buf, size_t out_len);
void openhome_free_ptr(void *ptr);
const FormatList *openhome_get_supported_formats(void);

#ifdef __cplusplus
}

namespace OpenHomeNX {

inline bool init() { return openhome_init(); }
inline void deinit() { openhome_deinit(); }
inline SaveHandle* loadSave(const std::vector<uint8_t>& data) { return openhome_load_save(data.data(), data.size()); }
inline SaveHandle* loadSaveFromPath(const std::string& path) { return openhome_load_save_from_path(path.c_str()); }
inline SaveHandle* loadSave(const std::string& path) { // legacy: reads file via C++ and calls load_save
    // For Switch, C++ will read file bytes and call load_save with data
    // This wrapper keeps compatibility but does file read in C++ via SaveFileFFI
    return loadSaveFromPath(path);
}
inline void freeSave(SaveHandle* handle) { openhome_free_save(handle); }
inline PkmHandle* loadPkm(const std::vector<uint8_t>& data) { return openhome_load_pkm(data.data(), data.size()); }
inline void freePkm(PkmHandle* handle) { openhome_free_pkm(handle); }
inline uint32_t getPokemonCount(SaveHandle* handle) { return openhome_get_pokemon_count(handle); }
inline uint32_t getBoxCount(SaveHandle* handle) { return openhome_get_box_count(handle); }
inline PkmHandle* getPokemonFromSlot(SaveHandle* handle, uint32_t box_idx, uint32_t slot_idx) { return openhome_get_pokemon_from_slot(handle, box_idx, slot_idx); }
inline PkmHandle* transferPkm(PkmHandle* pkm_handle, uint32_t target_gen) { return openhome_transfer_pkm(pkm_handle, target_gen); }
inline bool savePkmToFile(PkmHandle* pkm_handle, uint32_t slot) { return openhome_save_pkm_to_file(pkm_handle, slot); }
inline std::vector<uint8_t> getPkmBoxBytes(PkmHandle* pkm_handle) {
    // First call with null buffer to get the required size (344 for Pk8).
    // Then allocate and fill.
    uint8_t buf[344];
    uint32_t written = openhome_get_pkm_box_bytes(pkm_handle, buf, sizeof(buf));
    if (written == 0) return {};
    return std::vector<uint8_t>(buf, buf + written);
}
inline PkmHandle* loadPkmFromGen(const std::vector<uint8_t>& data, uint32_t gen) {
    return openhome_load_pkm_from_gen(data.data(), data.size(), gen);
}
inline std::vector<uint8_t> getPkmBoxBytesForGen(PkmHandle* pkm_handle, uint32_t gen) {
    size_t max_len = 344;
    if (gen == 3) max_len = 80;
    else if (gen == 7) max_len = 232;
    else if (gen == 9) max_len = 344;
    else if (gen == 10) max_len = 360; // Pa8 (Legends: Arceus)
    else if (gen == 11) max_len = 344; // Pa9 (Legends: Z-A, shares Pk9 record)
    std::vector<uint8_t> buf(max_len);
    uint32_t written = openhome_get_pkm_box_bytes_for_gen(pkm_handle, gen, buf.data(), buf.size());
    if (written == 0) return {};
    buf.resize(written);
    return buf;
}
// OHPKM OriginalBackup of the pre-conversion original: [tag u16 LE][record].
// Empty if the handle carries no backup.
inline std::vector<uint8_t> getPkmOriginalBackup(PkmHandle* pkm_handle) {
    uint8_t buf[384]; // tag(2) + largest supported record (PA8 = 376)
    uint32_t written = openhome_get_pkm_original_backup(pkm_handle, buf, sizeof(buf));
    if (written == 0) return {};
    return std::vector<uint8_t>(buf, buf + written);
}
// OHPKM universal storage (bank cross-gen) — same style as getPkmBoxBytesForGen / loadPkmFromGen
inline std::vector<uint8_t> getOhpkmBytes(PkmHandle* handle) {
    // A serialized OhpkmV2 (all sections + up to two ~378-byte backup blobs)
    // comfortably exceeds 512 bytes; 8 KiB matches the Rust-side test buffer.
    static uint8_t buf[8192];
    uint32_t written = openhome_get_ohpkm_bytes(handle, buf, sizeof(buf));
    if (written == 0) return {};
    return std::vector<uint8_t>(buf, buf + written);
}
inline PkmHandle* loadOhpkm(const std::vector<uint8_t>& data) {
    return openhome_load_ohpkm(data.data(), data.size());
}
inline uint16_t ohpkmSpecies(PkmHandle* handle) { return openhome_ohpkm_species(handle); }
inline uint16_t ohpkmForm(PkmHandle* handle) { return openhome_ohpkm_form(handle); }
inline uint8_t ohpkmLevel(PkmHandle* handle) { return openhome_ohpkm_level(handle); }
inline bool ohpkmIsShiny(PkmHandle* handle) { return openhome_ohpkm_is_shiny(handle); }
inline uint8_t ohpkmGender(PkmHandle* handle) { return openhome_ohpkm_gender(handle); }
inline uint16_t ohpkmHeldItem(PkmHandle* handle) { return openhome_ohpkm_held_item(handle); }
inline uint8_t ohpkmOriginGen(PkmHandle* handle) { return openhome_ohpkm_origin_gen(handle); }
inline std::string ohpkmNickname(PkmHandle* handle) {
    uint8_t buf[64];
    uint32_t written = openhome_ohpkm_nickname(handle, buf, sizeof(buf));
    if (written == 0) return {};
    return std::string(reinterpret_cast<char*>(buf), written);
}
inline void freePtr(void* ptr) { openhome_free_ptr(ptr); }
inline const FormatList* getSupportedFormats() { return openhome_get_supported_formats(); }

} // namespace OpenHomeNX

#endif