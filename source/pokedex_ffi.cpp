#include "pokedex_ffi.h"
namespace PokedexFFI {
void _ensure_linked() {
    (void)openhome_get_supported_formats;
}
} // namespace PokedexFFI
