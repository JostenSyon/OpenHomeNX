use pkm_rs_resources::ball::Ball;
use pkm_rs_resources::metadata_source::MetadataSource;
use pkm_rs_resources::ribbons::OpenHomeRibbonSet;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_types::{AbilityNumber, Stats16Le};

use super::OhpkmConvert;
use crate::convert_strategy::ConvertStrategy;
use crate::convert_strategy::PkmConverter;
use crate::format::PkmFormat;
use crate::gen6;
use crate::gen6::Pk6;
use crate::ohpkm;
use crate::ohpkm::OhpkmV2;
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::result::{Error, Result};
use crate::traits::HasSpeciesAndForm;

impl OhpkmConvert for Pk6 {
    fn to_main_data(&self) -> ohpkm::v2_sections::MainDataV2 {
        let species_and_form =
            SpeciesForm::new(self.national_dex, self.form_index as u16).unwrap();

        ohpkm::v2_sections::MainDataV2 {
            personality_value: self.personality_value,
            encryption_constant: self.encryption_constant,
            species_and_form,
            held_item_index: self.held_item_index,
            trainer_id: self.trainer_id,
            secret_id: self.secret_id,
            exp: self.exp,
            ability_index: self
                .ability_index
                .change_bound()
                .expect("Pk6 max ability <= overall max ability"),
            ability_num: self.ability_num,
            markings: self.markings.into(),
            nature: self.nature,
            is_fateful_encounter: self.is_fateful_encounter,
            gender: self.gender,
            evs: self.evs,
            contest: self.contest,
            pokerus: self.pokerus,
            contest_memory_count: self.contest_memory_count,
            battle_memory_count: self.battle_memory_count,
            ribbons: OpenHomeRibbonSet::from_modern(self.ribbons),
            moves: self.moves.to_pp_adjusted(
                MetadataSource::XY,
                ohpkm::MOVE_METADATA_SOURCE,
            ),
            nickname: {
                let mut nickname = self.nickname;
                nickname.resize()
            },
            relearn_moves: self.relearn_moves,
            ivs: self.ivs,
            is_egg: self.is_egg,
            is_nicknamed: self.is_nicknamed,
            handler_name: {
                let mut handler_name = self.handler_name;
                handler_name.resize()
            },
            is_current_handler: self.is_current_handler,
            handler_friendship: self.handler_friendship,
            handler_memory: self.handler_memory,
            handler_affection: self.handler_affection,
            handler_gender: self.handler_gender,
            fullness: self.fullness,
            enjoyment: self.enjoyment,
            game_of_origin: self.game_of_origin,
            console_region: self.console_region,
            language: self.language,
            form_argument: self.form_argument,
            trainer_name: {
                let mut trainer_name = self.trainer_name;
                trainer_name.resize()
            },
            trainer_friendship: self.trainer_friendship,
            trainer_memory: self.trainer_memory,
            trainer_affection: self.trainer_affection,
            egg_date: self.egg_date,
            met_date: self.met_date,
            ball: self.ball,
            egg_location_index: if self.egg_location_index == 0 {
                None
            } else {
                Some(self.egg_location_index)
            },
            met_location_index: self.met_location_index,
            met_level: self.met_level,
            trainer_gender: self.trainer_gender,
            ..Default::default()
        }
    }

    fn to_gen_67_data(&self) -> Option<ohpkm::v2_sections::Gen67Data> {
        Some(ohpkm::v2_sections::Gen67Data {
            country: self.country,
            region: self.region,
            geolocations: self.geolocations,
            super_training_flags: self.super_training_flags,
            super_training_dist_flags: self.super_training_dist_flags,
            secret_super_training_unlocked: self.secret_super_training_unlocked,
            secret_super_training_complete: self.secret_super_training_complete,
            ..Default::default()
        })
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, strategy: ConvertStrategy) -> Result<Self> {
        let converter = PkmConverter::new(PkmFormat::PK6, strategy);
        let met_data = converter.met_data(ohpkm);
        let species_and_form = ohpkm.species_and_form();

        let form_index = species_and_form.get_forme_index() as u8;
        let national_dex = species_and_form.get_ndex() as u16;

        let mut mon = Self {
            encryption_constant: ohpkm.encryption_constant(),
            checksum: 0,
            national_dex,
            held_item_index: ohpkm.held_item_index(),
            trainer_id: ohpkm.trainer_id(),
            secret_id: ohpkm.secret_id(),
            exp: ohpkm.exp(),
            ability_index: ohpkm.ability_index().change_bound().unwrap_or(
                ohpkm.get_forme_metadata()
                    .get_ability(ohpkm.ability_num())
                    .change_bound()
                    .unwrap_or(
                        ohpkm.get_forme_metadata()
                            .get_ability(AbilityNumber::First)
                            .change_bound()
                            .ok_or(Error::AbilityIndex {
                                ability_index: ohpkm
                                    .get_forme_metadata()
                                    .get_ability(AbilityNumber::First)
                                    .to_u16(),
                            })?,
                    ),
            ),
            ability_num: ohpkm.ability_num(),
            training_bag_hits: 0,
            training_bag: 0,
            personality_value: ohpkm.personality_value(),
            nature: ohpkm.nature(),
            form_index,
            gender: ohpkm.gender(),
            evs: ohpkm.evs(),
            contest: ohpkm.contest(),
            markings: ohpkm.markings().into(),
            is_fateful_encounter: ohpkm.is_fateful_encounter(),
            pokerus: ohpkm.pokerus(),
            super_training_flags: ohpkm.super_training_flags().unwrap_or_default(),
            contest_memory_count: ohpkm.contest_memory_count(),
            battle_memory_count: ohpkm.battle_memory_count(),
            super_training_dist_flags: ohpkm.super_training_dist_flags().unwrap_or_default(),
            form_argument: ohpkm.form_argument(),
            nickname: {
                let mut nickname = ohpkm.nickname();
                nickname.resize()
            },
            moves: ohpkm.moves().to_pp_adjusted(
                ohpkm::MOVE_METADATA_SOURCE,
                MetadataSource::XY,
            ),
            relearn_moves: ohpkm.relearn_moves(),
            secret_super_training_unlocked: ohpkm
                .secret_super_training_unlocked()
                .unwrap_or_default(),
            secret_super_training_complete: ohpkm
                .secret_super_training_complete()
                .unwrap_or_default(),
            ivs: converter.ivs(ohpkm),
            is_egg: ohpkm.is_egg(),
            is_nicknamed: ohpkm.is_nicknamed(),
            handler_name: {
                let mut handler_name = ohpkm.handler_name();
                handler_name.resize()
            },
            handler_gender: ohpkm.handler_gender(),
            is_current_handler: ohpkm.is_current_handler(),
            geolocations: ohpkm.geolocations().unwrap_or_default(),
            handler_friendship: ohpkm.handler_friendship(),
            handler_affection: ohpkm.handler_affection(),
            handler_memory: ohpkm.handler_memory(),
            fullness: ohpkm.fullness(),
            enjoyment: ohpkm.enjoyment(),
            trainer_name: {
                let mut trainer_name = ohpkm.trainer_name();
                trainer_name.resize()
            },
            trainer_friendship: ohpkm.trainer_friendship(),
            trainer_affection: ohpkm.trainer_affection(),
            trainer_memory: ohpkm.trainer_memory(),
            trainer_gender: ohpkm.trainer_gender(),
            egg_date: ohpkm.egg_date(),
            met_date: ohpkm.met_date(),
            egg_location_index: ohpkm.egg_location_index().unwrap_or(0),
            met_location_index: met_data.location_index,
            ball: if ohpkm.ball() <= Ball::Beast {
                ohpkm.ball()
            } else {
                Ball::Poke
            },
            met_level: ohpkm.met_level(),
            encounter_type: 0,
            game_of_origin: met_data.origin,
            country: ohpkm.country().unwrap_or_default(),
            region: ohpkm.region().unwrap_or_default(),
            console_region: ohpkm.console_region(),
            language: ohpkm.language(),
            status_condition: 0,
            stat_level: 0,
            current_hp: 0,
            stats: Stats16Le::default(),
            ribbons: ohpkm.ribbons().get_modern().into_iter().collect(),
        };

        mon.stat_level = mon.calculate_level();
        mon.stats = mon.calculate_stats();
        mon.current_hp = mon.stats.hp;

        mon.refresh_checksum();

        Ok(mon)
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        if bytes.len() == gen6::BOX_SIZE {
            let mut extended = bytes.to_vec();
            extended.resize(gen6::PARTY_SIZE, 0);
            let extended_len = extended.len();

            return extended
                .try_into()
                .map_err(|_| {
                    Error::buffer_size_with_source(
                        "Pk6::OhpkmConvert::bytes_to_stored",
                        gen6::PARTY_SIZE,
                        extended_len,
                    )
                })
                .map(StoredPkmBytes::Pk6);
        }
        bytes
            .try_into()
            .map_err(|_| {
                Error::buffer_size_with_source(
                    "Pk6::OhpkmConvert::bytes_to_stored",
                    gen6::PARTY_SIZE,
                    bytes.len(),
                )
            })
            .map(StoredPkmBytes::Pk6)
    }
}
