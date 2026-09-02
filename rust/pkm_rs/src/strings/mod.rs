// mod gameboy;

#[cfg(any(feature = "wasm", feature = "alloc"))]
mod gen3;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub use gen3::*;

// mod gen4;

// mod gen5;
// pub use gen5::Gen5String;