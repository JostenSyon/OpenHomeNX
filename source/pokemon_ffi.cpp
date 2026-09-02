#include "pokemon_ffi.h"
namespace PokemonFFI {
void _ensure_linked() {
    (void)openhome_load_pkm;
    (void)openhome_transfer_pkm;
}
} // namespace PokemonFFI
