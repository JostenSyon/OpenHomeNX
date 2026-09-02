#pragma once
#include "openhome_ffi.h"

// Wrapper FFI per Pokedex — traduce dati Rust → pkHouse.
// Stub Milestone 2; logica reale delegata a Rust in Milestone 5.
namespace PokedexFFI {

// Registra un Pokémon nel Pokédex del save via Rust (no-op per ora).
inline void registerPokemon(SaveHandle* save, PkmHandle* pkm) {
    (void)save;
    (void)pkm;
}

// Ritorna lista formati supportati da Rust.
inline const FormatList* supportedFormats() {
    return OpenHomeNX::getSupportedFormats();
}

} // namespace PokedexFFI
