mod pk4;

pub use pk4::*;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub use crate::gen6::crypto::{decrypt_in_place, encrypt_in_place};

pub(crate) const BOX_SIZE: usize = 136;
pub(crate) const PARTY_SIZE: usize = 236;

pub const BOX_COUNT: u8 = 18;
pub const BOX_ROWS: u8 = 5;
pub const BOX_COLS: u8 = 6;
pub const BOX_SLOTS: u8 = BOX_ROWS * BOX_COLS;
