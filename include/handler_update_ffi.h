#pragma once
#include "openhome_ffi.h"
#include <cstdint>

// Wrapper FFI per Handler Update — trainer logic delegata a Rust.
namespace HandlerFFI {

// Aggiorna handling trainer di un Pokémon via Rust.
// Per ora no-op; Milestone 3 collegherà SaveFile::TrainerInfo.
inline void updateHandler(PkmHandle* pkm, SaveHandle* save) {
    (void)pkm;
    (void)save;
}

inline void freePtr(void* p) { OpenHomeNX::freePtr(p); }

} // namespace HandlerFFI
