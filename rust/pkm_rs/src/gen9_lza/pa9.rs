//! Legends: Z-A (PA9).
//!
//! LZA reuses the Scarlet/Violet PK9 record layout almost 1:1 (same field
//! offsets, same `0x08..0x148` checksum span, same 344-byte size). The only
//! LZA-specific additions are:
//!   - `is_alpha` flag at byte 0x23 bit 0 (PK9 leaves that byte unused)
//!   - LZA "plus move" mastery flags (block C at 0xD6, block B at 0x60 — the
//!     latter overlaps the tail of the nickname field)
//!
//! For now `Pa9` wraps `Pk9` for the shared record and only tracks `is_alpha`.
//! Plus-move flags are NOT round-tripped through OHPKM yet (they survive an
//! LZA -> LZA move via the OriginalBackup, and do not exist in any other
//! format). See GenPorting.md.

extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::boxed::Box;
use crate::gen9_sv::Pk9;
use crate::result::Result;
use crate::traits::{HasSpeciesAndForm, IsShiny, PkmBytes};
use crate::util;
use pkm_rs_resources::species::{FormMetadata, SpeciesMetadata};
use serde::Serialize;

const IS_ALPHA_OFFSET: usize = 0x23;

#[derive(Debug, Default, Serialize, Clone, Copy)]
pub struct Pa9 {
    pub inner: Pk9,
    pub is_alpha: bool,
}

impl Pa9 {
    pub fn from_box_bytes(bytes: &[u8]) -> Result<Self> {
        let inner = Pk9::from_bytes(bytes)?;
        let is_alpha = bytes.len() > IS_ALPHA_OFFSET
            && util::get_flag(bytes, IS_ALPHA_OFFSET, 0);
        Ok(Self { inner, is_alpha })
    }

    pub fn refresh_checksum(&mut self) {
        self.inner.refresh_checksum();
    }

    pub fn is_empty_slot(bytes: &[u8]) -> bool {
        Pk9::is_empty_slot(bytes)
    }
}

impl PkmBytes for Pa9 {
    const BOX_SIZE: usize = Pk9::BOX_SIZE;
    const PARTY_SIZE: usize = Pk9::PARTY_SIZE;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::from_box_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        self.inner.write_box_bytes(bytes);
        // Overlay the LZA-only alpha flag (PK9 never touches byte 0x23).
        if bytes.len() > IS_ALPHA_OFFSET {
            util::set_flag(bytes, IS_ALPHA_OFFSET, 0, self.is_alpha);
        }
    }

    fn to_box_bytes(&self) -> Box<[u8]> {
        let mut bytes = Box::new([0u8; Self::BOX_SIZE]);
        self.write_box_bytes(bytes.as_mut_slice());
        bytes
    }
}

impl IsShiny for Pa9 {
    fn is_shiny(&self) -> bool {
        self.inner.is_shiny()
    }
    fn is_square_shiny(&self) -> bool {
        self.inner.is_square_shiny()
    }
}

impl HasSpeciesAndForm for Pa9 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        self.inner.get_species_metadata()
    }
    fn get_forme_metadata(&self) -> &'static FormMetadata {
        self.inner.get_forme_metadata()
    }
    fn calculate_level(&self) -> u8 {
        self.inner.calculate_level()
    }
}

impl crate::traits::ModernEvs for Pa9 {
    fn get_evs(&self) -> pkm_rs_types::Stats8 {
        self.inner.get_evs()
    }
}

#[cfg(test)]
impl crate::tests::PkhexJson for Pa9 {
    fn to_pkhex_json_value(&self) -> core::result::Result<serde_json::Value, serde_json::Error> {
        self.inner.to_pkhex_json_value()
    }
}
