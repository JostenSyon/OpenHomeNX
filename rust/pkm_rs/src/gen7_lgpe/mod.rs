mod pb7;
// save disabled for now (22 errors with alloc gate); Pb7 only needed for cross-gen FFI
// mod save;

pub use pb7::Pb7;

/// Stored PB7 record size (box == party for Let's Go), used by the OHPKM
/// cross-gen conversion when reporting a buffer-size error.
pub(crate) const PKM_DATA_SIZE: usize = 260;
