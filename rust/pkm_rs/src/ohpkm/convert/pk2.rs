extern crate alloc;
#[cfg(not(feature = "std"))]
use alloc::string::{String, ToString};

use crate::gen2::{Pk2, BOX_SIZE, PARTY_SIZE};
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::ohpkm::OhpkmV2;
use crate::result::{Error, Result};
use crate::{convert_strategy::ConvertStrategy, format::PkmFormat};
use crate::ohpkm;
use crate::traits::PkmBytes;
use pkm_rs_resources::lookup;
use pkm_rs_resources::moves::{MoveIndex, MoveSlots};
use pkm_rs_types::{Language, NationalDex, Stats8, StatsPreSplit};

use super::OhpkmConvert;

impl OhpkmConvert for Pk2 {
    fn to_main_data(&self) -> ohpkm::v2_sections::MainDataV2 {
        let ndex = self.national_dex as u16;
        let species_and_form =
            pkm_rs_resources::species::SpeciesForm::base_form(NationalDex::assert_valid(ndex));

        let nickname = if !self.nickname.is_empty() {
            self.nickname.as_str().into()
        } else {
            lookup::species_name(NationalDex::assert_valid(ndex), Language::English).into()
        };

        let evs = Stats8 {
            hp: self.evs_g12.hp as u8,
            atk: self.evs_g12.atk as u8,
            def: self.evs_g12.def as u8,
            spe: self.evs_g12.spe as u8,
            // Gen 2 keeps a single Special stat exp (like Gen 1): it feeds
            // both spa and spd.
            spa: self.evs_g12.spc as u8,
            spd: self.evs_g12.spc as u8,
            ..Default::default()
        };

        ohpkm::v2_sections::MainDataV2 {
            personality_value: 0,
            encryption_constant: 0,
            species_and_form,
            held_item_index: self.held_item_index as u16,
            trainer_id: self.trainer_id,
            secret_id: 0,
            exp: self.exp,
            ability_index: Default::default(),
            ability_num: Default::default(),
            markings: Default::default(),
            nature: Default::default(),
            is_fateful_encounter: false,
            gender: Default::default(),
            evs,
            contest: Default::default(),
            pokerus: Default::default(),
            moves: MoveSlots::from_arrays(
                [
                    MoveIndex::from_u16(self.moves[0] as u16),
                    MoveIndex::from_u16(self.moves[1] as u16),
                    MoveIndex::from_u16(self.moves[2] as u16),
                    MoveIndex::from_u16(self.moves[3] as u16),
                ],
                self.move_pp,
                self.move_pp_ups,
            ),
            nickname,
            ivs: Default::default(),
            is_egg: false,
            is_nicknamed: false,
            game_of_origin: Default::default(),
            language: Language::English,
            trainer_name: self.trainer_name.as_str().into(),
            trainer_friendship: self.trainer_friendship,
            ball: Default::default(),
            met_location_index: self.met_location_index as u16,
            met_level: self.met_level,
            met_date: Default::default(),
            trainer_gender: Default::default(),
            ..Default::default()
        }
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, _strategy: ConvertStrategy) -> Result<Self> {
        let dvs = ohpkm.dvs();
        let evs_g12 = ohpkm.evs_g12().unwrap_or(StatsPreSplit {
            hp: 0,
            atk: 0,
            def: 0,
            spe: 0,
            spc: 0,
        });

        let moves = ohpkm.moves();
        let indices = moves.indices();
        let pp = moves.pp();
        let pp_ups = moves.pp_ups();

        let national_dex = ohpkm.species_and_form().get_ndex() as u8;

        Ok(Pk2 {
            national_dex,
            held_item_index: ohpkm.held_item_index() as u8,
            moves: [
                indices[0] as u8,
                indices[1] as u8,
                indices[2] as u8,
                indices[3] as u8,
            ],
            trainer_id: ohpkm.trainer_id(),
            exp: ohpkm.exp(),
            evs_g12,
            dvs,
            move_pp: [pp[0], pp[1], pp[2], pp[3]],
            move_pp_ups: [pp_ups[0], pp_ups[1], pp_ups[2], pp_ups[3]],
            trainer_friendship: ohpkm.trainer_friendship(),
            pokerus_byte: 0,
            met_time_of_day: 0,
            met_level: ohpkm.met_level(),
            met_location_index: ohpkm.met_location_index() as u8,
            level: 0,
            status_condition: 0,
            current_hp: 0,
            trainer_name: ohpkm.trainer_name().to_string(),
            nickname: ohpkm.nickname().to_string(),
            trainer_gender: 0,
        })
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        if bytes.len() == BOX_SIZE {
            let mut extended = bytes.to_vec();
            extended.resize(PARTY_SIZE, 0);
            let extended_len = extended.len();
            return extended
                .try_into()
                .map_err(|_| {
                    Error::buffer_size_with_source(
                        "Pk2::OhpkmConvert::bytes_to_stored",
                        PARTY_SIZE,
                        extended_len,
                    )
                })
                .map(StoredPkmBytes::Pk2);
        }
        bytes
            .try_into()
            .map_err(|_| {
                Error::buffer_size_with_source(
                    "Pk2::OhpkmConvert::bytes_to_stored",
                    PARTY_SIZE,
                    bytes.len(),
                )
            })
            .map(StoredPkmBytes::Pk2)
    }
}
