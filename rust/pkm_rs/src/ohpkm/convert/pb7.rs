//! `OhpkmConvert for Pb7` (Pokémon: Let's Go, Pikachu! / Eevee!).
//!
//! Faithful port of the gen-7 mapping (`convert/pk7.rs`) trimmed to the PB7
//! field set: LGPE dropped contests / super-training / memories / geolocation
//! and *added* Awakening Values (AVs), CP and a `favorite` flag. Behavioural
//! reference: `/tmp/oh-upstream/src/core/pkm/PB7.ts`.
//!
//! v1 caveat (documented, not silent): a foreign mon converted *into* PB7 via
//! `from_ohpkm` gets a valid, checksum-correct record, but CP and party stats
//! are left for the game to recompute and AVs are clamped to 0-255. An
//! LGPE→LGPE move is byte-exact through the OriginalBackup, so the common path
//! is lossless.

use pkm_rs_resources::ball::Ball;
use pkm_rs_resources::abilities::{AbilityIndexBounded, AbilityIndexWasm};
use pkm_rs_resources::metadata_source::MetadataSource;
use pkm_rs_resources::moves::{MoveSlot, MoveSlots};
use pkm_rs_resources::natures::NatureIndex;
use pkm_rs_types::{AbilityNumber, BinaryGender, Gender, Language, Stats16Le};

use super::OhpkmConvert;
use crate::convert_strategy::{ConvertStrategy, PkmConverter};
use crate::format::PkmFormat;
use crate::gen7_lgpe::{self, Pb7};
use crate::ohpkm::OhpkmV2;
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::result::{Error, Result};
use crate::traits::HasSpeciesAndForm;

const MOVE_SOURCE: MetadataSource = MetadataSource::LetsGoPikachuEevee;

impl OhpkmConvert for Pb7 {
    fn to_main_data(&self) -> crate::ohpkm::v2_sections::MainDataV2 {
        let move_slots: MoveSlots = (0..4)
            .map(|i| MoveSlot::new(self.moves[i], self.move_pp[i], self.move_pp_ups[i]))
            .collect();

        let mut handler_name = self.handler_name;
        let mut trainer_name = self.trainer_name;

        crate::ohpkm::v2_sections::MainDataV2 {
            personality_value: self.personality_value,
            encryption_constant: self.encryption_constant,
            species_and_form: self.species_and_form,
            held_item_index: self.held_item_index,
            trainer_id: self.trainer_id,
            secret_id: self.secret_id,
            exp: self.exp,
            ability_index: AbilityIndexBounded::try_from(self.ability_index.to_u16())
                .unwrap_or_default(),
            ability_num: AbilityNumber::from_u8_first_three_bits(self.ability_num)
                .unwrap_or_default(),
            favorite: self.favorite,
            markings: self.markings,
            nature: NatureIndex::try_from(self.nature).unwrap_or_default(),
            is_fateful_encounter: self.is_fateful_encounter,
            gender: self.gender,
            evs: self.evs,
            pokerus: self.pokerus,
            moves: move_slots.to_pp_adjusted(MOVE_SOURCE, crate::ohpkm::MOVE_METADATA_SOURCE),
            nickname: self.nickname,
            relearn_moves: self.relearn_moves,
            ivs: self.ivs,
            is_egg: self.is_egg,
            is_nicknamed: self.is_nicknamed,
            handler_name: handler_name.resize::<26>(),
            is_current_handler: self.is_current_handler,
            handler_friendship: self.handler_friendship,
            handler_gender: BinaryGender::from(self.handler_gender),
            fullness: self.fullness,
            enjoyment: self.enjoyment,
            game_of_origin: self.game_of_origin.into(),
            language: Language::try_from(self.language_index).unwrap_or_default(),
            form_argument: self.form_argument,
            trainer_name: trainer_name.resize::<26>(),
            trainer_friendship: self.trainer_friendship,
            egg_date: if self.egg_date.month == 0 {
                None
            } else {
                Some(self.egg_date)
            },
            met_date: self.met_date,
            ball: Ball::from(self.ball),
            egg_location_index: if self.egg_location_index == 0 {
                None
            } else {
                Some(self.egg_location_index)
            },
            met_location_index: self.met_location_index,
            met_level: self.met_level,
            hyper_training: self.hyper_training,
            trainer_gender: BinaryGender::from(self.trainer_gender == Gender::Female),
            height_scalar: self.height,
            weight_scalar: self.weight,
            ..Default::default()
        }
    }

    fn to_gen_67_data(&self) -> Option<crate::ohpkm::v2_sections::Gen67Data> {
        Some(crate::ohpkm::v2_sections::Gen67Data {
            resort_event_status: self.resort_event_status,
            avs: Stats16Le::from(self.avs),
            ..Default::default()
        })
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, strategy: ConvertStrategy) -> Result<Self> {
        let form_metadata = ohpkm.get_forme_metadata();
        let converter = PkmConverter::new(PkmFormat::PB7, strategy);
        let met_data = converter.met_data(ohpkm);

        let adjusted = ohpkm
            .moves()
            .to_pp_adjusted(crate::ohpkm::MOVE_METADATA_SOURCE, MOVE_SOURCE);
        let mut moves = [Default::default(); 4];
        let mut move_pp = [0u8; 4];
        let mut move_pp_ups = [0u8; 4];
        for (i, slot) in adjusted.into_iter().enumerate() {
            moves[i] = slot.move_index;
            move_pp[i] = slot.pp;
            move_pp_ups[i] = slot.pp_ups;
        }

        let avs = ohpkm
            .avs()
            .map(|a| a.to_stats8_truncated())
            .unwrap_or_default();

        let ability_index = AbilityIndexWasm::try_from(u16::from(ohpkm.ability_index()))
            .or_else(|_| {
                AbilityIndexWasm::try_from(form_metadata.get_ability(ohpkm.ability_num()).to_u16())
            })
            .unwrap_or_default();

        let mut handler_name = ohpkm.handler_name();
        let mut trainer_name = ohpkm.trainer_name();

        let ball_u8 = {
            let b = ohpkm.ball();
            if b <= Ball::Beast { b } else { Ball::Poke }
        } as u8;

        let mut mon = Self {
            encryption_constant: ohpkm.encryption_constant(),
            checksum: 0,
            species_and_form: ohpkm.species_and_form(),
            held_item_index: ohpkm.held_item_index(),
            trainer_id: ohpkm.trainer_id(),
            secret_id: ohpkm.secret_id(),
            exp: ohpkm.exp(),
            ability_index,
            ability_num: ohpkm.ability_num().to_byte(),
            favorite: ohpkm.favorite(),
            markings: ohpkm.markings(),
            personality_value: ohpkm.personality_value(),
            nature: u8::from(ohpkm.nature()),
            is_fateful_encounter: ohpkm.is_fateful_encounter(),
            gender: ohpkm.gender(),
            evs: ohpkm.evs(),
            avs,
            resort_event_status: ohpkm.resort_event_status().unwrap_or_default(),
            pokerus: ohpkm.pokerus(),
            height_absolute_bytes: [0; 4],
            height: ohpkm.height_scalar(),
            weight: ohpkm.weight_scalar(),
            form_argument: ohpkm.form_argument(),
            nickname: ohpkm.nickname(),
            moves,
            move_pp,
            move_pp_ups,
            relearn_moves: ohpkm.relearn_moves(),
            ivs: converter.ivs(ohpkm),
            is_egg: ohpkm.is_egg(),
            is_nicknamed: ohpkm.is_nicknamed(),
            handler_name: handler_name.resize::<24>(),
            handler_gender: bool::from(ohpkm.handler_gender()),
            is_current_handler: ohpkm.is_current_handler(),
            handler_friendship: ohpkm.handler_friendship(),
            field_event_fatigue1: 0,
            field_event_fatigue2: 0,
            fullness: ohpkm.fullness(),
            enjoyment: ohpkm.enjoyment(),
            trainer_name: trainer_name.resize::<24>(),
            trainer_friendship: ohpkm.trainer_friendship(),
            received_year: 0,
            received_month: 0,
            received_day: 0,
            received_hour: 0,
            received_minute: 0,
            received_second: 0,
            egg_date: ohpkm.egg_date().unwrap_or_default(),
            met_date: ohpkm.met_date(),
            egg_location_index: ohpkm.egg_location_index().unwrap_or(0),
            met_location_index: met_data.location_index,
            ball: ball_u8,
            met_level: ohpkm.met_level(),
            hyper_training: ohpkm.hyper_training(),
            game_of_origin: met_data.origin as u8,
            language_index: ohpkm.language() as u8,
            weight_absolute_bytes: [0; 4],
            status_condition: 0,
            level: 0,
            dirt_type: 0,
            dirt_location: 0,
            current_hp: 0,
            stats: Stats16Le::default(),
            cp: 0,
            is_mega: 0,
            mega_form: 0,
            trainer_gender: Gender::from(ohpkm.trainer_gender()),
        };

        mon.level = mon.calculate_level();
        mon.refresh_checksum();

        Ok(mon)
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        bytes
            .try_into()
            .map_err(|_| {
                Error::buffer_size_with_source(
                    "Pb7::OhpkmConvert::bytes_to_stored",
                    gen7_lgpe::PKM_DATA_SIZE,
                    bytes.len(),
                )
            })
            .map(StoredPkmBytes::Pb7)
    }
}
