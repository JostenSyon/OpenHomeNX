extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{boxed::Box, vec::Vec};
use crate::result::{Error, Result};
use crate::traits::{HasSpeciesAndForm, PkmBytes};
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::{Gender, Ivs, Language, Stats8};
use serde::Serialize;

/// Gen4 Pokémon structure (DPPt/HGSS)
///
/// Box size: 136 bytes stored, 236 bytes party
/// Source: PK4.ts upstream (andrewbenington/OpenHome)
#[derive(Debug, Default, Serialize, Clone, Copy)]
pub struct Pk4 {
    pub personality_value: u32,
    pub sanity: u16,
    pub checksum: u16,
    pub national_dex: u16,
    pub held_item_index: u16,
    pub trainer_id: u16,
    pub secret_id: u16,
    pub exp: u32,
    pub trainer_friendship: u8,
    pub ability: u8,
    pub markings: u8,
    pub language: Language,
    pub evs: Stats8,
    pub contest: [u8; 6],
    pub moves: [u16; 4],
    pub move_pp: [u8; 4],
    pub move_pp_ups: [u8; 4],
    pub ivs: Ivs,
    pub is_egg: bool,
    pub is_nicknamed: bool,
    pub gender: Gender,
    pub form_index: u8,
    pub shiny_leaves: u8,
    pub game_of_origin: u8,
    pub pokerus_byte: u8,
    pub ball_dppt: u8,
    pub ball_hgss: u8,
    pub met_level: u8,
    pub encounter_type: u8,
    pub egg_location_index_dp: u16,
    pub egg_location_index_pthgss: u16,
    pub met_location_index_dp: u16,
    pub met_location_index_pthgss: u16,
    pub nickname: [u16; 12],
    pub trainer_name: [u16; 8],
    pub is_fateful_encounter: bool,
    // Party-only fields
    pub status_condition: u8,
    pub current_hp: u16,
}

pub const BOX_SIZE: usize = 136;
pub const PARTY_SIZE: usize = 236;

impl PkmBytes for Pk4 {
    const BOX_SIZE: usize = BOX_SIZE;
    const PARTY_SIZE: usize = PARTY_SIZE;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, _bytes: &mut [u8]) {
        // TODO: implement from upstream PK4.ts toBytes()
    }

    fn write_party_bytes(&self, _bytes: &mut [u8]) {
        // TODO: implement from upstream PK4.ts toBytes(includeExtraFields=true)
    }

    fn to_box_bytes(&self) -> Box<[u8]> {
        let mut bytes = Box::new([0u8; Self::BOX_SIZE]);
        self.write_box_bytes(bytes.as_mut_slice());
        bytes
    }

    fn to_party_bytes(&self) -> Box<[u8]> {
        let mut bytes = Box::new([0u8; Self::PARTY_SIZE]);
        self.write_party_bytes(bytes.as_mut_slice());
        bytes
    }
}

impl crate::traits::IsShiny for Pk4 {
    fn is_shiny(&self) -> bool {
        pkm_rs_types::shiny_xor_value(self.personality_value, self.trainer_id, self.secret_id) < 16
    }

    fn is_square_shiny(&self) -> bool {
        pkm_rs_types::shiny_xor_value(self.personality_value, self.trainer_id, self.secret_id) == 0
    }
}

impl HasSpeciesAndForm for Pk4 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        pkm_rs_resources::lookup::species_metadata(self.national_dex)
            .expect("valid species")
    }

    fn get_forme_metadata(&self) -> &'static FormMetadata {
        pkm_rs_resources::lookup::form_metadata(self.national_dex, self.form_index as u16)
    }

    fn calculate_level(&self) -> u8 {
        self.get_species_metadata()
            .level_up_type
            .calculate_level(self.exp)
    }
}

impl Pk4 {
    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        let size = bytes.len();
        match size {
            BOX_SIZE => Self::from_box_bytes(bytes),
            PARTY_SIZE => Self::from_party_bytes(bytes),
            _ => Err(Error::buffer_size(BOX_SIZE, size)),
        }
    }

    fn from_box_bytes(bytes: &[u8]) -> Result<Self> {
        // TODO: implement from upstream PK4.ts constructor
        Ok(Self::default())
    }

    fn from_party_bytes(bytes: &[u8]) -> Result<Self> {
        // TODO: implement from upstream PK4.ts constructor with party fields
        Ok(Self::default())
    }

    pub fn from_encrypted_bytes(_bytes: &mut [u8]) -> Result<Self> {
        // TODO: decrypt then parse
        Ok(Self::default())
    }

    pub fn calculate_checksum(&self) -> u16 {
        // TODO: implement from upstream PK4.ts calculateChecksum()
        0
    }

    pub fn refresh_checksum(&mut self) {
        self.checksum = self.calculate_checksum();
    }

    pub fn nature(&self) -> pkm_rs_resources::natures::NatureIndex {
        pkm_rs_resources::natures::NatureIndex::new_from_modulo(self.personality_value)
    }

    pub fn species_and_form(&self) -> SpeciesForm {
        SpeciesForm::new_valid_ndex(self.national_dex, self.form_index as u16)
            .expect("gen 4 form is valid")
    }

    pub fn is_empty_slot(bytes: &[u8]) -> bool {
        bytes.len() >= 8 && u16::from_le_bytes([bytes[8], bytes[9]]) == 0
    }

    pub fn get_ball(&self) -> u8 {
        core::cmp::max(self.ball_dppt, self.ball_hgss)
    }
}
