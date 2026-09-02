extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{boxed::Box, vec::Vec};
use crate::encryption::BlockCrypto;

// Gen4-6 crypto shuffle: re-export BlockCrypto::gen67 for Gen6 (PK6) and
// provide helpers matching JS PK6 encrypt/decrypt flows.
//
// PK6 uses the same block layout as Gen6/7: block size 0x38, offset 0x08,
// shuffle derived from encryptionConstant.

pub fn decrypt_in_place(bytes: &mut [u8], encryption_constant: u32) {
    BlockCrypto::gen67(encryption_constant).decrypt(bytes);
}

pub fn encrypt_in_place(bytes: &mut [u8], encryption_constant: u32) {
    BlockCrypto::gen67(encryption_constant).encrypt(bytes);
}

pub fn to_decrypted_bytes(bytes: &[u8], encryption_constant: u32) -> Box<[u8]> {
    BlockCrypto::gen67(encryption_constant).to_decrypted_bytes(bytes)
}

pub fn to_encrypted_bytes(bytes: &[u8], encryption_constant: u32) -> Box<[u8]> {
    BlockCrypto::gen67(encryption_constant).to_encrypted_bytes(bytes)
}
