use super::OhpkmConvert;
use crate::conversion::gen4_string_encoding;
use crate::convert_strategy::{ConvertStrategy, PkmConverter};
use crate::format::PkmFormat;
use crate::gen4::{self, Pk4};
use crate::ohpkm::v2_sections::pkm_bytes::StoredPkmBytes;
use crate::ohpkm::OhpkmV2;
use crate::result::{Error, Result};
use crate::traits::HasSpeciesAndForm;
use crate::ohpkm;

use pkm_rs_resources::moves::MoveIndex;
use pkm_rs_resources::ribbons::DsRibbonSet;
use pkm_rs_types::strings::SizedUtf16String;
use pkm_rs_types::{MarkingsFourShapes, OriginGame};

/// Gen4 charset codes -> OHPKM UTF-16 string (0x0000-terminated, zero-padded).
/// Unknown codes pass through raw (documented, lossless).
fn gen4_name_to_ohpkm(codes: &[u16]) -> SizedUtf16String<26> {
    let mut raw = [0u8; 26];
    let mut o = 0;
    for &code in codes {
        if code == 0xFFFF || o + 2 > raw.len() {
            break;
        }
        let uni = gen4_string_encoding::decode(code).unwrap_or(code);
        raw[o..o + 2].copy_from_slice(&uni.to_le_bytes());
        o += 2;
    }
    SizedUtf16String::from_bytes(raw)
}

/// OHPKM UTF-16 string -> Gen4 charset codes (0xFFFF-padded).
/// Unencodable chars pass through raw (documented, lossless).
fn ohpkm_name_to_gen4<const N: usize>(name: SizedUtf16String<26>) -> [u16; N] {
    let bytes = name.bytes();
    let mut out = [0xFFFFu16; N];
    let mut i = 0;
    while i < N && i * 2 + 1 < bytes.len() {
        let uni = u16::from_le_bytes([bytes[i * 2], bytes[i * 2 + 1]]);
        if uni == 0x0000 {
            break;
        }
        out[i] = gen4_string_encoding::encode(uni).unwrap_or(uni);
        i += 1;
    }
    out
}

impl OhpkmConvert for Pk4 {
    fn to_main_data(&self) -> ohpkm::v2_sections::MainDataV2 {
        let moves = [
            MoveIndex::from(self.moves[0]),
            MoveIndex::from(self.moves[1]),
            MoveIndex::from(self.moves[2]),
            MoveIndex::from(self.moves[3]),
        ];
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
            markings: MarkingsFourShapes::from_byte(self.markings).into(),
            nature: self.nature(),
            is_fateful_encounter: self.is_fateful_encounter,
            gender: self.gender,
            evs: self.evs,
            contest: self.contest,
            pokerus: pkm_rs_types::Pokerus::from_byte(self.pokerus_byte),
            ribbons: DsRibbonSet::from_bytes(
                self.ribbons[4..8].try_into().unwrap(),
                self.ribbons[0..4].try_into().unwrap(),
                self.ribbons[8..12].try_into().unwrap(),
            )
            .to_openhome(),
            moves: pkm_rs_resources::moves::MoveSlots::from_arrays(
                moves,
                self.move_pp,
                self.move_pp_ups,
            ),
            nickname: gen4_name_to_ohpkm(&self.nickname),
            ivs: self.ivs,
            is_egg: self.is_egg,
            is_nicknamed: self.is_nicknamed,
            game_of_origin: OriginGame::from(self.game_of_origin),
            language: self.language,
            trainer_name: gen4_name_to_ohpkm(&self.trainer_name),
            trainer_friendship: self.trainer_friendship,
            ball: pkm_rs_resources::ball::Ball::from(self.get_ball()),
            egg_location_index: Some(self.egg_location_index()),
            met_location_index: self.met_location_index(),
            met_level: self.met_level,
            trainer_gender: self.trainer_gender,
            egg_date: self.egg_date,
            met_date: self.met_date,
            ..Default::default()
        }
    }

    fn from_ohpkm(ohpkm: &OhpkmV2, strategy: ConvertStrategy) -> Result<Self> {
        let converter = PkmConverter::new(PkmFormat::PK4, strategy);
        let met_data = converter.met_data(ohpkm);

        let indices = ohpkm.moves().indices();
        let mut moves = [0u16; 4];
        for (i, m) in indices.iter().take(4).enumerate() {
            moves[i] = *m;
        }
        let pp = ohpkm.moves().pp();
        let mut move_pp = [0u8; 4];
        for (i, p) in pp.iter().take(4).enumerate() {
            move_pp[i] = *p;
        }
        let pp_ups = ohpkm.moves().pp_ups();
        let mut move_pp_ups = [0u8; 4];
        for (i, p) in pp_ups.iter().take(4).enumerate() {
            move_pp_ups[i] = *p;
        }

        let ribbons = DsRibbonSet::from_openhome(ohpkm.ribbons()).to_bytes_12();
        let version = met_data.origin as u8;

        let mut mon = Self {
            personality_value: ohpkm.personality_value(),
            sanity: 0,
            checksum: 0,
            national_dex: ohpkm.species_and_form().get_ndex() as u16,
            held_item_index: ohpkm.held_item_index(),
            trainer_id: ohpkm.trainer_id(),
            secret_id: ohpkm.secret_id(),
            exp: ohpkm.exp(),
            trainer_friendship: ohpkm.trainer_friendship(),
            ability: ohpkm.ability_index().to_u16() as u8,
            markings: MarkingsFourShapes::from(ohpkm.markings()).to_byte(),
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
            game_of_origin: version,
            version,
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
            trainer_gender: ohpkm.trainer_gender(),
            encounter_type: 0,
            ground_tile: 0,
            walking_mood: 0,
            egg_location_index_dp: ohpkm.egg_location_index().unwrap_or(0),
            egg_location_index_pthgss: ohpkm.egg_location_index().unwrap_or(0),
            met_location_index_dp: met_data.location_index,
            met_location_index_pthgss: met_data.location_index,
            ribbons,
            nickname: ohpkm_name_to_gen4(ohpkm.nickname()),
            trainer_name: ohpkm_name_to_gen4(ohpkm.trainer_name()),
            is_fateful_encounter: ohpkm.is_fateful_encounter(),
            egg_date: ohpkm.egg_date(),
            met_date: ohpkm.met_date(),
            status_condition: 0,
            stat_level: 0,
            current_hp: 0,
            stats: pkm_rs_types::Stats16Le::default(),
        };

        mon.refresh_checksum();

        Ok(mon)
    }

    fn bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes> {
        if bytes.len() == gen4::BOX_SIZE {
            let mut extended = bytes.to_vec();
            extended.resize(gen4::PARTY_SIZE, 0);
            let extended_len = extended.len();

            return extended
                .try_into()
                .map_err(|_| {
                    Error::buffer_size_with_source(
                        "Pk4::OhpkmConvert::bytes_to_stored",
                        gen4::PARTY_SIZE,
                        extended_len,
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
