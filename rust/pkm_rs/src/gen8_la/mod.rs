use pkm_rs_resources::metadata_source::MetadataSource;

use crate::result::Error;
pub use pa8::*;
use pa8_buffer::Pa8Buffer;
use pkm_rs_resources::abilities::AbilityIndexBounded;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_resources::species::form_metadata::source_has_form_metadata;

#[cfg(feature = "randomize")]
use pkm_rs_types::randomize::Randomize;

mod pa8;
mod pa8_buffer;

pub(crate) const PKM_DATA_SIZE: usize = 360;

const MAX_BOX_COUNT: u8 = 32;
const BOX_ROWS: u8 = 5;
const BOX_COLS: u8 = 6;
const BOX_SLOTS: u8 = BOX_ROWS * BOX_COLS;
// As One (Calyrex) is the highest ability the SwSh/LA family can reference.
const MAX_ABILITY_INDEX: u16 = 267;
pub(crate) use pa8::MAX_RIBBON_LA;

pub type Pa8AbilityIndex = AbilityIndexBounded<MAX_ABILITY_INDEX>;

#[derive(Debug, Clone, Copy, Default)]
pub struct Pa8SpeciesAndForm(SpeciesForm);

impl Pa8SpeciesAndForm {
    fn try_new(species_and_form: SpeciesForm) -> Option<Self> {
        if source_has_form_metadata(
            MetadataSource::LegendsArceus,
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

impl serde::Serialize for Pa8SpeciesAndForm {
    fn serialize<S>(&self, serializer: S) -> core::result::Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.0.serialize(serializer)
    }
}

#[cfg(feature = "randomize")]
impl Randomize for Pa8SpeciesAndForm {
    fn randomized<R: rand::prelude::Rng>(rng: &mut R) -> Self {
        loop {
            if let Some(randomized) = Self::try_new(SpeciesForm::randomized(rng)) {
                return randomized;
            }
        }
    }
}

impl TryFrom<SpeciesForm> for Pa8SpeciesAndForm {
    type Error = Error;

    fn try_from(value: SpeciesForm) -> core::result::Result<Self, Self::Error> {
        Self::try_new(value).ok_or(Error::form_index(value))
    }
}

pub type BoxIndex = pkm_rs_types::BoundedU8<{ MAX_BOX_COUNT - 1 }>;
pub type BoxSlot = pkm_rs_types::BoundedU8<{ BOX_SLOTS - 1 }>;
