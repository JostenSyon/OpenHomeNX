#![cfg_attr(not(feature = "std"), no_std)]
extern crate alloc;
#[cfg(any(feature = "wasm", feature = "alloc"))]
mod checksum;
#[cfg(any(feature = "wasm", feature = "alloc"))]
mod encryption;
mod conversion;
mod rom_hacks;
mod strings;
mod util;

pub mod bytes;
pub mod convert_strategy;
pub mod format;
#[cfg(all(feature = "newgen", any(feature = "wasm", feature = "alloc")))]
pub mod gen1;
#[cfg(all(feature = "newgen", any(feature = "wasm", feature = "alloc")))]
pub mod gen2;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub mod gen3;
#[cfg(all(feature = "newgen", any(feature = "wasm", feature = "alloc")))]
pub mod gen4;
#[cfg(all(feature = "newgen", any(feature = "wasm", feature = "alloc")))]
pub mod gen5;
#[cfg(all(feature = "newgen", any(feature = "wasm", feature = "alloc")))]
pub mod gen6;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub mod gen7_alola;
// pub mod gen7_lgpe; // Disabled: LGPE save loader has 22 compilation errors with alloc gate
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub mod gen8_la;
pub mod gen8_swsh;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub mod gen9_lza;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub mod gen9_sv;
pub mod location;
pub mod ohpkm;
pub mod result;
pub mod sectioned_data;
#[cfg(test)]
pub mod tests;
pub mod traits;

pub use rom_hacks::PluginIdentifier;

#[cfg(any(feature = "wasm", feature = "alloc"))]
pub use strings::Gen3Strings;

extern crate static_assertions;