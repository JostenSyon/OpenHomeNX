//! `OhpkmConvert for Pa9` (Legends: Z-A).
//!
//! v1: PA9 wraps PK9 for the shared record, so the OHPKM conversion delegates
//! to PK9's. Consequences accepted for now (see GenPorting.md):
//!   - move PP uses the Scarlet/Violet metadata source
//!   - met-data legalization uses PkmFormat::PK9
//!   - `is_alpha` and LZA plus-move flags are NOT carried through OHPKM; an
//!     LZA -> LZA move stays lossless via the OriginalBackup, and neither
//!     concept exists in any other target format.

use super::OhpkmConvert;
use crate::convert_strategy::ConvertStrategy;
use crate::gen9_lza;
use crate::gen9_lza::Pa9;
use crate::gen9_sv::Pk9;
use crate::ohpkm;
use crate::ohpkm::OhpkmV2;
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::result::{Error, Result};

impl OhpkmConvert for Pa9 {
    fn to_main_data(&self) -> ohpkm::v2_sections::MainDataV2 {
        self.inner.to_main_data()
    }

    // LZA has no Terastal mechanic — do not emit a Scarlet/Violet section.
    fn to_sv_data(&self) -> Option<ohpkm::v2_sections::ScarletVioletData> {
        None
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, strategy: ConvertStrategy) -> Result<Self> {
        let inner = Pk9::from_ohpkm(ohpkm, strategy)?;
        let mut mon = Pa9 {
            inner,
            is_alpha: ohpkm.is_alpha().unwrap_or(false),
        };
        mon.refresh_checksum();
        Ok(mon)
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        bytes
            .try_into()
            .map_err(|_| {
                Error::buffer_size_with_source(
                    "Pa9::OhpkmConvert::bytes_to_stored",
                    gen9_lza::PKM_DATA_SIZE,
                    bytes.len(),
                )
            })
            .map(StoredPkmBytes::Pa9)
    }
}
