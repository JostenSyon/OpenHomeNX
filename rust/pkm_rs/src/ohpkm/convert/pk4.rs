use super::OhpkmConvert;
use crate::convert_strategy::{ConvertStrategy, PkmConverter};
use crate::format::PkmFormat;
use crate::gen4::{self, Pk4};
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::ohpkm::OhpkmV2;
use crate::result::{Error, Result};
use crate::traits::HasSpeciesAndForm;
use crate::ohpkm;

use pkm_rs_resources::ribbons::OpenHomeRibbonSet;

impl OhpkmConvert for Pk4 {
    fn to_main_data(&self) -> ohpkm::v2_sections::MainDataV2 {
        ohpkm::v2_sections::MainDataV2 {
            personality_value: self.personality_value,
            encryption_constant: self.personality_value,
            species_and_form: self.species_and_form(),
            held_item_index: self.held_item_index,
            trainer_id: self.trainer_id,
            secret_id: self.secret_id,
            exp: self.exp,
            ability_index: pkm_rs_resources::abilities::AbilityIndexBounded::try_from(self.ability as u16)
                .unwrap_or_else(|_| {
                    pkm_rs_resources::abilities::AbilityIndexBounded::new(1)
                        .expect("1 is a valid ability index")
                }),
            ability_num: if self.personality_value % 2 == 1 {
                pkm_rs_types::AbilityNumber::Second
            } else {
                pkm_rs_types::AbilityNumber::First
            },
            markings: self.markings.into(),
            nature: self.nature(),
            is_fateful_encounter: self.is_fateful_encounter,
            gender: self.gender,
            evs: self.evs,
            contest: self.contest,
            pokerus: pkm_rs_types::Pokerus::from_byte(self.pokerus_byte),
            ribbons: OpenHomeRibbonSet::default(),
            moves: pkm_rs_resources::moves::MoveSlots::from_arrays(
                self.moves,
                self.move_pp,
                self.move_pp_ups,
            ),
            nickname: pkm_rs_types::strings::SizedUtf16String::from_bytes({
                let mut buf = [0u8; 52];
                buf[..24].copy_from_slice(self.nickname.bytes());
                buf
            }),
            ivs: self.ivs,
            is_egg: self.is_egg,
            is_nicknamed: self.is_nicknamed,
            game_of_origin: pkm_rs_types::OriginGame::from(self.game_of_origin),
            language: self.language,
            trainer_name: pkm_rs_types::strings::SizedUtf16String::from_bytes({
                let mut buf = [0u8; 52];
                buf[..16].copy_from_slice(self.trainer_name.bytes());
                buf
            }),
            trainer_friendship: self.trainer_friendship,
            ball: pkm_rs_resources::ball::Ball::from(self.ball()),
            egg_location_index: Some(self.egg_location_index()),
            met_location_index: self.met_location_index(),
            met_level: self.met_level,
            trainer_gender: self.trainer_gender,
            egg_date: self.egg_date,
            met_date: self.met_date.unwrap_or_else(pkm_rs_types::PokeDate::today),
            ..Default::default()
        }
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, strategy: ConvertStrategy) -> Result<Self> {
        let converter = PkmConverter::new(PkmFormat::PK4, strategy);
        let met_data = converter.met_data(ohpkm);

        let mut moves = [0u16; 4];
        let indices = ohpkm.moves().indices();
        for (i, &m) in indices.iter().enumerate().take(4) {
            moves[i] = m;
        }
        let mut move_pp = [0u8; 4];
        let pp = ohpkm.moves().pp();
        for (i, &p) in pp.iter().enumerate().take(4) {
            move_pp[i] = p;
        }
        let mut move_pp_ups = [0u8; 4];
        let pp_ups = ohpkm.moves().pp_ups();
        for (i, &p) in pp_ups.iter().enumerate().take(4) {
            move_pp_ups[i] = p;
        }

        let mut mon = Self {
            personality_value: ohpkm.personality_value(),
            national_dex: ohpkm.species_and_form().get_ndex() as u16,
            held_item_index: ohpkm.held_item_index(),
            trainer_id: ohpkm.trainer_id(),
            secret_id: ohpkm.secret_id(),
            exp: ohpkm.exp(),
            trainer_friendship: ohpkm.trainer_friendship(),
            ability: ohpkm.ability_index().to_u16() as u8,
            markings: ohpkm.markings().into(),
            language: ohpkm.language(),
            evs: ohpkm.evs(),
            contest: ohpkm.contest(),
            moves,
            move_pp,
            move_pp_ups,
            ivs: converter.ivs(ohpkm),
            is_egg: ohpkm.is_egg(),
            is_nicknamed: ohpkm.is_nicknamed(),
            gender: ohpkm.gender(),
            form_index: ohpkm.species_and_form().get_forme_index() as u8,
            shiny_leaves: 0,
            game_of_origin: met_data.origin as u8,
            egg_date: ohpkm.egg_date(),
            met_date: Some(ohpkm.met_date()),
            pokerus_byte: ohpkm.pokerus().to_byte(),
            ball_dppt: {
                let ball = ohpkm.ball() as u8;
                if ball <= 24 { ball } else { 4 }
            },
            ball_hgss: {
                let ball = ohpkm.ball() as u8;
                if ball > 24 { ball } else { 0 }
            },
            met_level: ohpkm.met_level(),
            encounter_type: 0,
            performance: 0,
            status_condition: 0,
            current_hp: 0,
            egg_location_index_dp: ohpkm.egg_location_index().unwrap_or(0),
            egg_location_index_pthgss: ohpkm.egg_location_index().unwrap_or(0),
            met_location_index_dp: met_data.location_index,
            met_location_index_pthgss: met_data.location_index,
            ribbons: gen4::Gen4Ribbons::empty(),
            nickname: gen4::Gen4String::from_bytes(&[0u8; 24]),
            trainer_name: gen4::Gen4String::from_bytes(&[0u8; 16]),
            trainer_gender: ohpkm.trainer_gender(),
            is_fateful_encounter: ohpkm.is_fateful_encounter(),
            checksum: 0,
        };

        mon.refresh_checksum();

        Ok(mon)
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        if bytes.len() == gen4::BOX_SIZE {
            let mut extended = bytes.to_vec();
            extended.resize(gen4::PARTY_SIZE, 0);
            return extended
                .try_into()
                .map_err(|_| {
                    Error::buffer_size_with_source(
                        "Pk4::OhpkmConvert::bytes_to_stored",
                        gen4::PARTY_SIZE,
                        extended.len(),
                    )
                })
                .map(StoredPkmBytes::Pk4);
        }
        bytes
            .try_into()
            .map_err(|_| {
                Error::buffer_size_with_source(
                    "Pk4::OhpkmConvert::bytes_to_stored",
                    gen4::PARTY_SIZE,
                    bytes.len(),
                )
            })
            .map(StoredPkmBytes::Pk4)
    }
}
