use pkm_rs_resources::metadata_source::MetadataSource;
use pkm_rs_types::strings::SizedUtf16String;

use crate::result::Error;
pub use pb8::*;
use pb8_buffer::Pb8Buffer;
use pkm_rs_resources;
use pkm_rs_resources::abilities::AbilityIndexBounded;
use pkm_rs_resources::ribbons::ModernRibbon;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_resources::species::form_metadata::source_has_form_metadata;

#[cfg(feature = "randomize")]
use pkm_rs_types::randomize::Randomize;

mod pb8;
mod pb8_buffer;

pub(crate) const PKM_DATA_SIZE: usize = 344;

const MAX_BOX_COUNT: u8 = 32;
const BOX_ROWS: u8 = 5;
const BOX_COLS: u8 = 6;
const BOX_SLOTS: u8 = BOX_ROWS * BOX_COLS;
const BOX_NAME_LENGTH: usize = 34;
const MAX_ABILITY_INDEX: u16 = 267; // As One (Calyrex Shadow Rider)
const MAX_RIBBON_SWSH: usize = ModernRibbon::TowerMaster as usize;

pub type Pb8AbilityIndex = AbilityIndexBounded<MAX_ABILITY_INDEX>;

type BoxName = SizedUtf16String<BOX_NAME_LENGTH>;

#[derive(Debug, Clone, Copy, Default)]
pub struct Pb8SpeciesAndForm(SpeciesForm);

impl Pb8SpeciesAndForm {
    fn try_new(species_and_form: SpeciesForm) -> Option<Self> {
        if source_has_form_metadata(
            MetadataSource::SwordShield,
            species_and_form.get_ndex() as u16,
            species_and_form.get_forme_index(),
        ) {
            Some(Self(species_and_form))
        } else {
            None
        }
    }

    pub const fn into_inner(self) -> SpeciesForm {
        self.0
    }
}

impl serde::Serialize for Pb8SpeciesAndForm {
    fn serialize<S>(&self, serializer: S) -> core::result::Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.0.serialize(serializer)
    }
}

#[cfg(feature = "randomize")]
impl Randomize for Pb8SpeciesAndForm {
    fn randomized<R: rand::prelude::Rng>(rng: &mut R) -> Self {
        loop {
            if let Some(randomized) = Self::try_new(SpeciesForm::randomized(rng)) {
                return randomized;
            }
        }
    }
}

impl TryFrom<SpeciesForm> for Pb8SpeciesAndForm {
    type Error = Error;

    fn try_from(value: SpeciesForm) -> core::result::Result<Self, Self::Error> {
        Self::try_new(value).ok_or(Error::form_index(value))
    }
}

pub type BoxIndex = pkm_rs_types::BoundedU8<{ MAX_BOX_COUNT - 1 }>;

pub type BoxSlot = pkm_rs_types::BoundedU8<{ BOX_SLOTS - 1 }>;

#[cfg(test)]
mod tests {
    use super::{BOX_SLOTS, BoxIndex, BoxSlot, MAX_BOX_COUNT};
    use crate::result::{Error, Result};

    #[test]
    fn all_boxes_valid() -> Result<()> {
        for index in 0..MAX_BOX_COUNT {
            BoxIndex::check_bound(index).or(Err(Error::BoxIndex(index)))?;
        }

        for slot in 0..BOX_SLOTS {
            BoxSlot::check_bound(slot).or(Err(Error::BoxSlot(slot)))?;
        }

        Ok(())
    }
}