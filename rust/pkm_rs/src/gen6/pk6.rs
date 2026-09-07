extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{vec::Vec, boxed::Box, borrow::ToOwned};
use crate::result::{Error, Result};
use crate::traits::ModernEvs;
use crate::traits::{HasSpeciesAndForm, PkmBytes};

use super::Pk6AbilityIndex;
use pkm_rs_derive::IsShiny4096;
use pkm_rs_resources::abilities::AbilityIndexBounded;
use pkm_rs_resources::ball::Ball;
use pkm_rs_resources::helpers;
use pkm_rs_resources::metadata_source::MetadataSource;
use pkm_rs_resources::moves::{MoveIndex, MoveSlots};
use pkm_rs_resources::natures::NatureIndex;
use pkm_rs_resources::ribbons::{ModernRibbon, ModernRibbonSet};
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::strings::SizedUtf16String;
use pkm_rs_types::{
    AbilityNumber, BinaryGender, ContestStats, Gender, Geolocations, Ivs, Language,
    MarkingsSixShapes, OriginGame, Pokerus, PokeDate, Stats8, Stats16Le, TrainerMemory,
};

use serde::Serialize;

#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

#[cfg(any(feature = "wasm", feature = "alloc"))]
use crate::ohpkm::{OhpkmConvert, OhpkmV2};
#[cfg(any(feature = "wasm", feature = "alloc"))]
use pkm_rs_resources::abilities::AbilityIndexWasm;

#[cfg(feature = "randomize")]
use pkm_rs_types::randomize::Randomize;

#[cfg_attr(feature = "wasm", wasm_bindgen(js_name = Pk6Wasm))]
#[cfg_attr(feature = "randomize", derive(Randomize))]
#[derive(Debug, Default, Serialize, Clone, Copy, IsShiny4096)]
pub struct Pk6 {
    pub encryption_constant: u32,
    pub checksum: u16,
    pub national_dex: u16,
    pub held_item_index: u16,
    pub trainer_id: u16,
    pub secret_id: u16,
    pub exp: u32,
    pub ability_index: Pk6AbilityIndex,
    pub ability_num: AbilityNumber,
    pub training_bag_hits: u8,
    pub training_bag: u8,
    pub personality_value: u32,
    pub nature: NatureIndex,
    pub form_index: u8,
    pub gender: Gender,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub evs: Stats8,
    pub contest: ContestStats,
    pub markings: MarkingsSixShapes,
    pub is_fateful_encounter: bool,
    pub pokerus: Pokerus,
    pub super_training_flags: u32,
    pub contest_memory_count: u8,
    pub battle_memory_count: u8,
    pub super_training_dist_flags: u8,
    pub form_argument: u32,
    pub nickname: SizedUtf16String<24>,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub moves: MoveSlots,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub relearn_moves: [MoveIndex; 4],
    pub secret_super_training_unlocked: bool,
    pub secret_super_training_complete: bool,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub ivs: Ivs,
    pub is_egg: bool,
    pub is_nicknamed: bool,
    pub handler_name: SizedUtf16String<24>,
    pub handler_gender: BinaryGender,
    pub is_current_handler: bool,
    pub geolocations: Geolocations,
    pub handler_friendship: u8,
    pub handler_affection: u8,
    pub handler_memory: TrainerMemory,
    pub fullness: u8,
    pub enjoyment: u8,
    pub trainer_name: SizedUtf16String<24>,
    pub trainer_friendship: u8,
    pub trainer_affection: u8,
    pub trainer_memory: TrainerMemory,
    pub trainer_gender: BinaryGender,
    pub egg_date: Option<PokeDate>,
    pub met_date: PokeDate,
    pub egg_location_index: u16,
    pub met_location_index: u16,
    pub ball: Ball,
    pub met_level: u8,
    pub encounter_type: u8,
    pub game_of_origin: OriginGame,
    pub country: u8,
    pub region: u8,
    pub console_region: u8,
    pub language: Language,
    pub status_condition: u32,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stat_level: u8,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub current_hp: u16,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stats: Stats16Le,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub ribbons: ModernRibbonSet<6, MAX_RIBBON_GEN6>,
}

const MAX_RIBBON_GEN6: usize = ModernRibbon::ToughnessMaster as usize;

impl Pk6 {
    /// Parse EC-encrypted save slots (mirrors Pk7::from_encrypted_bytes).
    pub fn from_encrypted_bytes(mut bytes: Box<[u8]>) -> Result<Self> {
        let ec = u32::from_le_bytes(bytes[0x00..0x04].try_into().unwrap());
        crate::gen6::crypto::decrypt_in_place(&mut bytes, ec);
        Self::try_from_bytes(&bytes)
    }

    /// 16-bit little-endian sum over `0x08..0xE8`, same scheme as Pk7/Pb7
    /// (PKHeX `get16BitChecksumLittleEndian`). Tail past 0xE8 not covered.
    pub fn calculate_checksum(&self) -> u16 {
        let mut bytes = [0u8; Self::BOX_SIZE];
        self.write_box_bytes(&mut bytes);
        crate::checksum::checksum_u16_le(&bytes[0x08..0xE8])
    }

    pub fn refresh_checksum(&mut self) {
        self.checksum = self.calculate_checksum();
    }

    pub fn calculate_stats(&self) -> Stats16Le {
        helpers::calculate_stats_modern(
            MetadataSource::XY,
            SpeciesForm::new(self.national_dex, self.form_index as u16).unwrap(),
            &self.ivs,
            &self.evs,
            self.calculate_level(),
            self.nature.get_metadata(),
            None,
        )
        .unwrap_or_else(|| {
            panic!(
                "pk6 has species/form present in x + y: {:#?}",
                SpeciesForm::new(self.national_dex, self.form_index as u16).unwrap()
            )
        })
    }

    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        let size = bytes.len();
        if size < Self::BOX_SIZE {
            return Err(Error::buffer_size(Self::BOX_SIZE, size));
        }

        let data_view = bytes;

        // TS line 124: encryptionConstant at 0x0
        let encryption_constant = u32::from_le_bytes(data_view[0x00..0x04].try_into().unwrap());
        // TS line 125: checksum at 0x6
        let checksum = u16::from_le_bytes(data_view[0x06..0x08].try_into().unwrap());
        // TS line 126: nationalDex at 0x8
        let national_dex = u16::from_le_bytes(data_view[0x08..0x0a].try_into().unwrap());
        // TS line 127: heldItemIndex at 0xa
        let held_item_index = u16::from_le_bytes(data_view[0x0a..0x0c].try_into().unwrap());
        // TS line 128: trainerID at 0xc
        let trainer_id = u16::from_le_bytes(data_view[0x0c..0x0e].try_into().unwrap());
        // TS line 129: secretID at 0xe
        let secret_id = u16::from_le_bytes(data_view[0x0e..0x10].try_into().unwrap());
        // TS line 130: exp at 0x10
        let exp = u32::from_le_bytes(data_view[0x10..0x14].try_into().unwrap());
        // TS line 131: ability at 0x14
        let ability_index = Pk6AbilityIndex::try_from(data_view[0x14] as u16)?;
        // TS line 132: abilityNum at 0x15
        let ability_num = AbilityNumber::from_u8_first_three_bits(data_view[0x15])?;
        // TS line 133: trainingBagHits at 0x16
        let training_bag_hits = data_view[0x16];
        // TS line 134: trainingBag at 0x17
        let training_bag = data_view[0x17];
        // TS line 135: personalityValue at 0x18
        let personality_value = u32::from_le_bytes(data_view[0x18..0x1c].try_into().unwrap());
        // TS line 136: nature at 0x1c
        let nature = NatureIndex::try_from(data_view[0x1c])?;
        // TS line 137: formIndex from bits 3-5 of byte 0x1d
        let form_index = ((data_view[0x1d] >> 3) & 0x07) as u8;
        // TS line 148: gender from bits 1-2 of byte 0x1d
        let gender = Gender::from_bits_1_2(data_view[0x1d]);
        // TS line 149: evs at 0x1e (6 bytes)
        let evs = Stats8::from_bytes(data_view[0x1e..0x24].try_into().unwrap());
        // TS line 150: contest at 0x24 (6 bytes)
        let contest = ContestStats::from_bytes(data_view[0x24..0x2a].try_into().unwrap());
        // TS line 151: markings at 0x2a
        let markings = MarkingsSixShapes::from_byte(data_view[0x2a]);
        // TS line 152: pokerusByte at 0x2b
        let pokerus = Pokerus::from_byte(data_view[0x2b]);
        // TS line 153: superTrainingFlags at 0x2c
        let super_training_flags = u32::from_le_bytes(data_view[0x2c..0x30].try_into().unwrap());
        // TS line 154: contestMemoryCount at 0x38
        let contest_memory_count = data_view[0x38];
        // TS line 155: battleMemoryCount at 0x39
        let battle_memory_count = data_view[0x39];
        // TS line 156: superTrainingDistFlags at 0x3a
        let super_training_dist_flags = data_view[0x3a];
        // TS line 157: formArgument at 0x3c
        let form_argument = u32::from_le_bytes(data_view[0x3c..0x40].try_into().unwrap());
        // TS line 158: nickname at 0x40 (12 UTF-16 chars = 24 bytes)
        let nickname = SizedUtf16String::<24>::from_bytes(data_view[0x40..0x58].try_into().unwrap());
        // TS lines 159-164: moves at 0x5a (4 × u16 LE)
        let move_indices = [
            MoveIndex::from(u16::from_le_bytes(data_view[0x5a..0x5c].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x5c..0x5e].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x5e..0x60].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x60..0x62].try_into().unwrap())),
        ];
        // TS lines 165-170: movePP at 0x62 (4 × u8)
        let move_pp = [
            data_view[0x62],
            data_view[0x63],
            data_view[0x64],
            data_view[0x65],
        ];
        // TS lines 171-176: movePPUps at 0x66 (4 × u8)
        let move_pp_ups = [
            data_view[0x66],
            data_view[0x67],
            data_view[0x68],
            data_view[0x69],
        ];
        let moves = MoveSlots::from_arrays(move_indices, move_pp, move_pp_ups);
        // TS lines 177-182: relearnMoves at 0x6a (4 × u16 LE)
        let relearn_moves = [
            MoveIndex::from(u16::from_le_bytes(data_view[0x6a..0x6c].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x6c..0x6e].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x6e..0x70].try_into().unwrap())),
            MoveIndex::from(u16::from_le_bytes(data_view[0x70..0x72].try_into().unwrap())),
        ];
        // TS line 183: secretSuperTrainingUnlocked bit 1 of 0x72
        let secret_super_training_unlocked = (data_view[0x72] & 0x02) != 0;
        // TS line 184: secretSuperTrainingComplete bit 2 of 0x72
        let secret_super_training_complete = (data_view[0x72] & 0x04) != 0;
        // TS line 185: ivs at 0x74 (30-bit packed)
        let ivs = Ivs::from_30_bits(data_view[0x74..0x78].try_into().unwrap());
        // TS line 186: isEgg bit 30 of 0x74
        let is_egg = (u32::from_le_bytes(data_view[0x74..0x78].try_into().unwrap()) & (1 << 30)) != 0;
        // TS line 187: isNicknamed bit 31 of 0x74
        let is_nicknamed = (u32::from_le_bytes(data_view[0x74..0x78].try_into().unwrap()) & (1 << 31)) != 0;
        // TS line 188: handlerName at 0x78 (12 UTF-16 chars = 24 bytes)
        let handler_name = SizedUtf16String::<24>::from_bytes(data_view[0x78..0x90].try_into().unwrap());
        // TS line 189: handlerGender from bit 0 of 0x92
        let handler_gender = BinaryGender::from((data_view[0x92] & 0x01) != 0);
        // TS line 190: isCurrentHandler bit 0 of 0x93
        let is_current_handler = (data_view[0x93] & 0x01) != 0;
        // TS lines 191-197: geolocations at 0x94 (5 × 2 bytes = 10 bytes)
        let geolocations = Geolocations::from_bytes(data_view[0x94..0x9e].try_into().unwrap());
        // TS line 198: handlerFriendship at 0xa2
        let handler_friendship = data_view[0xa2];
        // TS line 199: handlerAffection at 0xa3
        let handler_affection = data_view[0xa3];
        // TS line 200: handlerMemory at 0xa4 (3DS handler format: intensity, memory, feeling, textVar LE16)
        let handler_memory = TrainerMemory {
            intensity: data_view[0xa4],
            memory: data_view[0xa5],
            feeling: data_view[0xa6],
            text_variable: u16::from_le_bytes(data_view[0xa7..0xa9].try_into().unwrap()),
        };
        // TS line 202: fullness at 0xae
        let fullness = data_view[0xae];
        // TS line 203: enjoyment at 0xaf
        let enjoyment = data_view[0xaf];
        // TS line 204: trainerName at 0xb0 (12 UTF-16 chars = 24 bytes)
        let trainer_name = SizedUtf16String::<24>::from_bytes(data_view[0xb0..0xc8].try_into().unwrap());
        // TS line 205: trainerFriendship at 0xca
        let trainer_friendship = data_view[0xca];
        // TS line 206: trainerAffection at 0xcb
        let trainer_affection = data_view[0xcb];
        // TS line 201: trainerMemory at 0xcc (3DS trainer format: intensity, memory, textVar LE16, feeling)
        let trainer_memory = TrainerMemory {
            intensity: data_view[0xcc],
            memory: data_view[0xcd],
            text_variable: u16::from_le_bytes(data_view[0xce..0xd0].try_into().unwrap()),
            feeling: data_view[0xd0],
        };
        // TS line 207: eggDate at 0xd1 (3 bytes)
        let egg_date = PokeDate::from_bytes_optional(data_view[0xd1..0xd4].try_into().unwrap());
        // TS line 208: metDate at 0xd4 (3 bytes)
        let met_date = PokeDate::from_bytes(data_view[0xd4..0xd7].try_into().unwrap());
        // TS line 209: eggLocationIndex at 0xd8
        let egg_location_index = u16::from_le_bytes(data_view[0xd8..0xda].try_into().unwrap());
        // TS line 210: metLocationIndex at 0xda
        let met_location_index = u16::from_le_bytes(data_view[0xda..0xdc].try_into().unwrap());
        // TS line 211: ball at 0xdc
        let ball = Ball::from(data_view[0xdc]);
        // TS line 212: metLevel at 0xdd (lower 7 bits)
        let met_level = data_view[0xdd] & 0x7f;
        // TS line 213: encounterType at 0xde
        let encounter_type = data_view[0xde];
        // TS line 214: gameOfOrigin at 0xdf
        let game_of_origin = OriginGame::from(data_view[0xdf]);
        // TS line 215: country at 0xe0
        let country = data_view[0xe0];
        // TS line 216: region at 0xe1
        let region = data_view[0xe1];
        // TS line 217: consoleRegion at 0xe2
        let console_region = data_view[0xe2];
        // TS line 218: language at 0xe3
        let language = Language::try_from(data_view[0xe3])?;
        // TS lines 219-223: statusCondition at 0xe8 (party only)
        let status_condition = if size >= super::PARTY_SIZE {
            u32::from_le_bytes(data_view[0xe8..0xec].try_into().unwrap())
        } else {
            0
        };
        // TS lines 225-229: currentHP at 0xf0 (party only)
        let current_hp = if size >= super::PARTY_SIZE {
            u16::from_le_bytes(data_view[0xf0..0xf2].try_into().unwrap())
        } else {
            0
        };
        // TS line 230: isFatefulEncounter bit 0 of 0x1d
        let is_fateful_encounter = (data_view[0x1d] & 0x01) != 0;
        // TS lines 231-233: ribbons at 0x30 (46 bits in 6 bytes)
        let mut ribbon_bytes = [0u8; 6];
        ribbon_bytes.copy_from_slice(&data_view[0x30..0x36]);
        let ribbons = ModernRibbonSet::<6, MAX_RIBBON_GEN6>::from_bytes(ribbon_bytes);
        // TS line 234: trainerGender from bit 7 of 0xdd
        let trainer_gender = BinaryGender::from((data_view[0xdd] & 0x80) != 0);

        let mut mon = Pk6 {
            encryption_constant,
            checksum,
            national_dex,
            held_item_index,
            trainer_id,
            secret_id,
            exp,
            ability_index,
            ability_num,
            training_bag_hits,
            training_bag,
            personality_value,
            nature,
            form_index,
            gender,
            evs,
            contest,
            markings,
            is_fateful_encounter,
            pokerus,
            super_training_flags,
            contest_memory_count,
            battle_memory_count,
            super_training_dist_flags,
            form_argument,
            nickname,
            moves,
            relearn_moves,
            secret_super_training_unlocked,
            secret_super_training_complete,
            ivs,
            is_egg,
            is_nicknamed,
            handler_name,
            handler_gender,
            is_current_handler,
            geolocations,
            handler_friendship,
            handler_affection,
            handler_memory,
            fullness,
            enjoyment,
            trainer_name,
            trainer_friendship,
            trainer_affection,
            trainer_memory,
            trainer_gender,
            egg_date,
            met_date,
            egg_location_index,
            met_location_index,
            ball,
            met_level,
            encounter_type,
            game_of_origin,
            country,
            region,
            console_region,
            language,
            status_condition,
            stat_level: 0,
            current_hp,
            stats: Stats16Le::default(),
            ribbons,
        };

        mon.stat_level = mon.calculate_level();
        mon.stats = mon.calculate_stats();
        mon.current_hp = if mon.current_hp == 0 {
            mon.stats.hp
        } else {
            mon.current_hp
        };

        Ok(mon)
    }
}

impl PkmBytes for Pk6 {
    const BOX_SIZE: usize = super::BOX_SIZE;
    const PARTY_SIZE: usize = super::PARTY_SIZE;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        bytes[0x00..0x04].copy_from_slice(&self.encryption_constant.to_le_bytes());
        bytes[0x06..0x08].copy_from_slice(&self.checksum.to_le_bytes());
        bytes[0x08..0x0a].copy_from_slice(&self.national_dex.to_le_bytes());
        bytes[0x0a..0x0c].copy_from_slice(&self.held_item_index.to_le_bytes());
        bytes[0x0c..0x0e].copy_from_slice(&self.trainer_id.to_le_bytes());
        bytes[0x0e..0x10].copy_from_slice(&self.secret_id.to_le_bytes());
        bytes[0x10..0x14].copy_from_slice(&self.exp.to_le_bytes());
        bytes[0x14] = self.ability_index.to_u16() as u8;
        bytes[0x15] = self.ability_num.to_byte();
        bytes[0x16] = self.training_bag_hits;
        bytes[0x17] = self.training_bag;
        bytes[0x18..0x1c].copy_from_slice(&self.personality_value.to_le_bytes());
        bytes[0x1c] = self.nature.to_byte();

        // byte 0x1d: fateful_encounter(bit 0) | gender(bits 1-2) | form_index(bits 3-5)
        let mut byte_1d: u8 = 0;
        if self.is_fateful_encounter {
            byte_1d |= 0x01;
        }
        self.gender.set_bits_1_2(&mut byte_1d);
        byte_1d |= (self.form_index & 0x07) << 3;
        bytes[0x1d] = byte_1d;

        bytes[0x1e..0x24].copy_from_slice(&self.evs.to_bytes());
        bytes[0x24..0x2a].copy_from_slice(&self.contest.to_bytes());
        bytes[0x2a] = self.markings.to_byte();
        bytes[0x2b] = self.pokerus.to_byte();
        bytes[0x2c..0x30].copy_from_slice(&self.super_training_flags.to_le_bytes());

        // ribbons at 0x30 (46 bits stored in 6 bytes)
        bytes[0x30..0x36].copy_from_slice(&self.ribbons.to_bytes());

        bytes[0x38] = self.contest_memory_count;
        bytes[0x39] = self.battle_memory_count;
        bytes[0x3a] = self.super_training_dist_flags;
        bytes[0x3c..0x40].copy_from_slice(&self.form_argument.to_le_bytes());
        bytes[0x40..0x58].copy_from_slice(&*self.nickname);

        // moves at 0x5a
        for (i, slot) in self.moves.into_iter().enumerate() {
            let offset = 0x5a + i * 2;
            bytes[offset..offset + 2].copy_from_slice(&slot.move_index.to_le_bytes());
        }
        // move PP at 0x62
        for (i, slot) in self.moves.into_iter().enumerate() {
            bytes[0x62 + i] = slot.pp;
        }
        // move PP ups at 0x66
        for (i, slot) in self.moves.into_iter().enumerate() {
            bytes[0x66 + i] = slot.pp_ups;
        }
        // relearn moves at 0x6a
        for (i, m) in self.relearn_moves.iter().enumerate() {
            let offset = 0x6a + i * 2;
            bytes[offset..offset + 2].copy_from_slice(&m.to_le_bytes());
        }

        // secret super training flags at 0x72
        let mut st_flags: u8 = 0;
        if self.secret_super_training_unlocked {
            st_flags |= 0x02;
        }
        if self.secret_super_training_complete {
            st_flags |= 0x04;
        }
        bytes[0x72] = st_flags;

        // IVs + is_egg + is_nicknamed at 0x74
        bytes[0x74..0x78].copy_from_slice(&[0u8; 4]);
        self.ivs.write_30_bits(bytes, 0x74);
        if self.is_egg {
            bytes[0x77] |= 0x40;
        }
        if self.is_nicknamed {
            bytes[0x77] |= 0x80;
        }

        bytes[0x78..0x90].copy_from_slice(&*self.handler_name);
        bytes[0x92] = if bool::from(self.handler_gender) { 0x01 } else { 0x00 };
        bytes[0x93] = if self.is_current_handler { 0x01 } else { 0x00 };
        bytes[0x94..0x9e].copy_from_slice(&self.geolocations.to_bytes());
        bytes[0xa2] = self.handler_friendship;
        bytes[0xa3] = self.handler_affection;
        // handler memory at 0xa4 (3DS handler format: intensity, memory, feeling, textVar LE16)
        bytes[0xa4] = self.handler_memory.intensity;
        bytes[0xa5] = self.handler_memory.memory;
        bytes[0xa6] = self.handler_memory.feeling;
        bytes[0xa7..0xa9].copy_from_slice(&self.handler_memory.text_variable.to_le_bytes());
        bytes[0xae] = self.fullness;
        bytes[0xaf] = self.enjoyment;
        bytes[0xb0..0xc8].copy_from_slice(&*self.trainer_name);
        bytes[0xca] = self.trainer_friendship;
        bytes[0xcb] = self.trainer_affection;
        // trainer memory at 0xcc (3DS trainer format: intensity, memory, textVar LE16, feeling)
        bytes[0xcc] = self.trainer_memory.intensity;
        bytes[0xcd] = self.trainer_memory.memory;
        bytes[0xce..0xd0].copy_from_slice(&self.trainer_memory.text_variable.to_le_bytes());
        bytes[0xd0] = self.trainer_memory.feeling;
        // egg date at 0xd1
        if let Some(egg_date) = self.egg_date {
            bytes[0xd1..0xd4].copy_from_slice(&egg_date.to_bytes());
        }
        // met date at 0xd4
        bytes[0xd4..0xd7].copy_from_slice(&self.met_date.to_bytes());
        bytes[0xd8..0xda].copy_from_slice(&self.egg_location_index.to_le_bytes());
        bytes[0xda..0xdc].copy_from_slice(&self.met_location_index.to_le_bytes());
        bytes[0xdc] = self.ball as u8;
        bytes[0xdd] = (self.met_level & 0x7f) | if bool::from(self.trainer_gender) { 0x80 } else { 0x00 };
        bytes[0xde] = self.encounter_type;
        bytes[0xdf] = self.game_of_origin as u8;
        bytes[0xe0] = self.country;
        bytes[0xe1] = self.region;
        bytes[0xe2] = self.console_region;
        bytes[0xe3] = self.language as u8;
    }

    fn write_party_bytes(&self, bytes: &mut [u8]) {
        self.write_box_bytes(bytes);
        bytes[0xe8..0xec].copy_from_slice(&self.status_condition.to_le_bytes());
        bytes[0xf0..0xf2].copy_from_slice(&self.current_hp.to_le_bytes());
    }
}

impl HasSpeciesAndForm for Pk6 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        pkm_rs_resources::species::get_species_metadata(self.national_dex)
    }

    fn get_forme_metadata(&self) -> &'static FormMetadata {
        SpeciesForm::new(self.national_dex, self.form_index as u16)
            .unwrap()
            .get_forme_metadata()
    }

    fn calculate_level(&self) -> u8 {
        self.get_species_metadata()
            .level_up_type
            .calculate_level(self.exp)
    }
}

impl ModernEvs for Pk6 {
    fn get_evs(&self) -> Stats8 {
        self.evs
    }
}

#[cfg(test)]
mod tests {
    use crate::gen6::Pk6;
    use crate::traits::IsShiny;

    #[test]
    fn default_not_shiny() {
        let mon = Pk6::default();
        assert!(!mon.is_shiny());
    }
}
