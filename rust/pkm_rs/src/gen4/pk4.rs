extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{boxed::Box, vec::Vec};
use crate::encryption;
use crate::result::{Error, Result};
use crate::traits::{HasSpeciesAndForm, PkmBytes};
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::{
    BinaryGender, ContestStats, Gender, Ivs, Language, OriginGame, PokeDate, Pokerus, Stats16Le, Stats8,
};
use serde::Serialize;

/// Gen4 Pokémon structure (DPPt/HGSS)
///
/// Box size: 136 bytes stored, 236 bytes party
/// Source: PKHeX PK4.cs (kwsch/PKHeX)
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
    pub contest: ContestStats,
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
    pub version: u8,
    pub pokerus_byte: u8,
    pub ball_dppt: u8,
    pub ball_hgss: u8,
    pub met_level: u8,
    pub encounter_type: u8,
    pub ground_tile: u8,
    pub walking_mood: u8,
    pub egg_location_index_dp: u16,
    pub egg_location_index_pthgss: u16,
    pub met_location_index_dp: u16,
    pub met_location_index_pthgss: u16,
    /// Raw ribbon bytes: 0x24-0x27 (Sinnoh/Unova), 0x3C-0x3F (Hoenn),
    /// 0x60-0x63 (Sinnoh contest). Lossless; typed at the convert boundary.
    pub ribbons: [u8; 12],
    pub nickname: [u16; 11],
    pub trainer_name: [u16; 8],
    pub is_fateful_encounter: bool,
    pub trainer_gender: BinaryGender,
    pub egg_date: Option<PokeDate>,
    pub met_date: PokeDate,
    // Party-only fields
    pub status_condition: u32,
    pub stat_level: u8,
    pub current_hp: u16,
    pub stats: Stats16Le,
}

pub const BOX_SIZE: usize = 136;
pub const PARTY_SIZE: usize = 236;

impl PkmBytes for Pk4 {
    const BOX_SIZE: usize = BOX_SIZE;
    const PARTY_SIZE: usize = PARTY_SIZE;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        bytes[0x00..0x04].copy_from_slice(&self.personality_value.to_le_bytes());
        bytes[0x04..0x06].copy_from_slice(&self.sanity.to_le_bytes());
        bytes[0x06..0x08].copy_from_slice(&self.checksum.to_le_bytes());
        bytes[0x08..0x0a].copy_from_slice(&self.national_dex.to_le_bytes());
        bytes[0x0a..0x0c].copy_from_slice(&self.held_item_index.to_le_bytes());
        bytes[0x0c..0x0e].copy_from_slice(&self.trainer_id.to_le_bytes());
        bytes[0x0e..0x10].copy_from_slice(&self.secret_id.to_le_bytes());
        bytes[0x10..0x14].copy_from_slice(&self.exp.to_le_bytes());
        bytes[0x14] = self.trainer_friendship;
        bytes[0x15] = self.ability;
        bytes[0x16] = self.markings;
        bytes[0x17] = self.language as u8;
        bytes[0x18..0x1e].copy_from_slice(&self.evs.to_bytes());
        bytes[0x1e..0x24].copy_from_slice(&self.contest.to_bytes());
        bytes[0x24..0x28].copy_from_slice(&self.ribbons[0..4]);
        for (i, m) in self.moves.iter().enumerate() {
            bytes[0x28 + i * 2..0x2a + i * 2].copy_from_slice(&m.to_le_bytes());
        }
        bytes[0x30..0x34].copy_from_slice(&self.move_pp);
        bytes[0x34..0x38].copy_from_slice(&self.move_pp_ups);
        let mut iv32 = [0u8; 4];
        self.ivs.write_30_bits(&mut iv32, 0);
        if self.is_egg {
            iv32[3] |= 0x40;
        }
        if self.is_nicknamed {
            iv32[3] |= 0x80;
        }
        bytes[0x38..0x3c].copy_from_slice(&iv32);
        bytes[0x3c..0x40].copy_from_slice(&self.ribbons[4..8]);
        let mut byte_40: u8 = if self.is_fateful_encounter { 1 } else { 0 };
        self.gender.set_bits_1_2(&mut byte_40);
        byte_40 |= self.form_index << 3;
        bytes[0x40] = byte_40;
        bytes[0x41] = self.shiny_leaves;
        bytes[0x42..0x44].copy_from_slice(&[0u8; 2]);
        bytes[0x44..0x46].copy_from_slice(&self.egg_location_index_pthgss.to_le_bytes());
        bytes[0x46..0x48].copy_from_slice(&self.met_location_index_pthgss.to_le_bytes());
        for (i, c) in self.nickname.iter().enumerate() {
            bytes[0x48 + i * 2..0x4a + i * 2].copy_from_slice(&c.to_le_bytes());
        }
        bytes[0x5e] = 0;
        bytes[0x5f] = self.version;
        bytes[0x60..0x64].copy_from_slice(&self.ribbons[8..12]);
        bytes[0x64..0x68].copy_from_slice(&[0u8; 4]);
        for (i, c) in self.trainer_name.iter().enumerate() {
            bytes[0x68 + i * 2..0x6a + i * 2].copy_from_slice(&c.to_le_bytes());
        }
        bytes[0x78..0x7b].copy_from_slice(&match self.egg_date {
            Some(date) => date.to_bytes(),
            None => [0, 0, 0],
        });
        bytes[0x7b..0x7e].copy_from_slice(&self.met_date.to_bytes());
        bytes[0x7e..0x80].copy_from_slice(&self.egg_location_index_dp.to_le_bytes());
        bytes[0x80..0x82].copy_from_slice(&self.met_location_index_dp.to_le_bytes());
        bytes[0x82] = self.pokerus_byte;
        bytes[0x83] = self.ball_dppt;
        bytes[0x84] = (self.met_level & 0x7f)
            | (if bool::from(self.trainer_gender) { 0x80 } else { 0 });
        bytes[0x85] = self.ground_tile;
        bytes[0x86] = self.ball_hgss;
        bytes[0x87] = self.walking_mood;
    }

    fn write_party_bytes(&self, bytes: &mut [u8]) {
        self.write_box_bytes(bytes);
        bytes[0x88..0x8c].copy_from_slice(&self.status_condition.to_le_bytes());
        bytes[0x8c] = self.stat_level;
        bytes[0x8d] = 0;
        bytes[0x8e..0x90].copy_from_slice(&self.current_hp.to_le_bytes());
        bytes[0x90..0x9c].copy_from_slice(&self.stats.to_bytes());
        bytes[0x9c..0xec].copy_from_slice(&[0u8; 0xec - 0x9c]);
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
        // PKHeX PK4(byte[]): box slots are stored encrypted; decrypt first.
        let mut data = bytes.to_vec();
        encryption::decrypt_if_encrypted_45(&mut data);
        Self::parse_decrypted(&data, false)
    }

    fn from_party_bytes(bytes: &[u8]) -> Result<Self> {
        let mut data = bytes.to_vec();
        encryption::decrypt_if_encrypted_45(&mut data);
        Self::parse_decrypted(&data, true)
    }

    #[allow(clippy::too_many_lines)]
    fn parse_decrypted(data: &[u8], party: bool) -> Result<Self> {
        let u16le = |o: usize| u16::from_le_bytes([data[o], data[o + 1]]);
        let personality_value = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
        let iv32 = u32::from_le_bytes([data[0x38], data[0x39], data[0x3a], data[0x3b]]);
        let byte_40 = data[0x40];

        let mut nickname = [0u16; 11];
        for (i, c) in nickname.iter_mut().enumerate() {
            *c = u16le(0x48 + i * 2);
        }
        let mut trainer_name = [0u16; 8];
        for (i, c) in trainer_name.iter_mut().enumerate() {
            *c = u16le(0x68 + i * 2);
        }
        let mut moves = [0u16; 4];
        for (i, m) in moves.iter_mut().enumerate() {
            *m = u16le(0x28 + i * 2);
        }
        let mut ribbons = [0u8; 12];
        ribbons[0..4].copy_from_slice(&data[0x24..0x28]);
        ribbons[4..8].copy_from_slice(&data[0x3c..0x40]);
        ribbons[8..12].copy_from_slice(&data[0x60..0x64]);

        let version = data[0x5f];

        let mut mon = Self {
            personality_value,
            sanity: u16le(0x04),
            checksum: u16le(0x06),
            national_dex: u16le(0x08),
            held_item_index: u16le(0x0a),
            trainer_id: u16le(0x0c),
            secret_id: u16le(0x0e),
            exp: u32::from_le_bytes([data[0x10], data[0x11], data[0x12], data[0x13]]),
            trainer_friendship: data[0x14],
            ability: data[0x15],
            markings: data[0x16],
            language: Language::try_from(data[0x17])?,
            evs: Stats8::from_bytes(data[0x18..0x1e].try_into().unwrap()),
            contest: ContestStats::from_bytes(data[0x1e..0x24].try_into().unwrap()),
            moves,
            move_pp: data[0x30..0x34].try_into().unwrap(),
            move_pp_ups: data[0x34..0x38].try_into().unwrap(),
            ivs: Ivs::from_30_bits(data[0x38..0x3c].try_into().unwrap()),
            is_egg: (iv32 >> 30) & 1 == 1,
            is_nicknamed: (iv32 >> 31) & 1 == 1,
            gender: Gender::from_bits_1_2(byte_40),
            form_index: byte_40 >> 3,
            shiny_leaves: data[0x41],
            game_of_origin: version,
            version,
            pokerus_byte: data[0x82],
            ball_dppt: data[0x83],
            ball_hgss: data[0x86],
            met_level: data[0x84] & 0x7f,
            trainer_gender: BinaryGender::from(data[0x84] & 0x80 != 0),
            encounter_type: 0,
            ground_tile: data[0x85],
            walking_mood: data[0x87],
            egg_location_index_dp: u16le(0x7e),
            egg_location_index_pthgss: u16le(0x44),
            met_location_index_dp: u16le(0x80),
            met_location_index_pthgss: u16le(0x46),
            ribbons,
            nickname,
            trainer_name,
            is_fateful_encounter: byte_40 & 1 == 1,
            egg_date: PokeDate::from_bytes_optional(data[0x78..0x7b].try_into().unwrap()),
            met_date: PokeDate::from_bytes(data[0x7b..0x7e].try_into().unwrap()),
            status_condition: 0,
            stat_level: 0,
            current_hp: 0,
            stats: Stats16Le::default(),
        };
        if party {
            mon.status_condition =
                u32::from_le_bytes([data[0x88], data[0x89], data[0x8a], data[0x8b]]);
            mon.stat_level = data[0x8c];
            mon.current_hp = u16le(0x8e);
            mon.stats = Stats16Le::from_bytes(data[0x90..0x9c].try_into().unwrap());
        }
        Ok(mon)
    }

    pub fn from_encrypted_bytes(bytes: &mut [u8]) -> Result<Self> {
        encryption::decrypt_if_encrypted_45(bytes);
        Self::try_from_bytes(bytes)
    }

    /// Encrypted box record, ready to write as a .pk4 file: fresh checksum,
    /// then Encrypt45 (checksum must be set BEFORE encrypt, it seeds the xor).
    pub fn to_encrypted_box_bytes(&self) -> Box<[u8]> {
        let mut mon = *self;
        mon.refresh_checksum();
        let mut bytes = mon.to_box_bytes();
        encryption::encrypt_45(&mut bytes);
        bytes
    }

    pub fn calculate_checksum(&self) -> u16 {
        let mut bytes = [0u8; Self::BOX_SIZE];
        self.write_box_bytes(&mut bytes);
        encryption::checksum_45(&bytes)
    }

    pub fn refresh_checksum(&mut self) {
        self.checksum = self.calculate_checksum();
    }

    pub fn nature(&self) -> pkm_rs_resources::natures::NatureIndex {
        pkm_rs_resources::natures::NatureIndex::new_from_modulo(self.personality_value)
    }

    pub fn species_and_form(&self) -> SpeciesForm {
        SpeciesForm::new(self.national_dex, self.form_index as u16).expect("gen 4 form is valid")
    }

    pub fn is_empty_slot(bytes: &[u8]) -> bool {
        bytes.len() >= 8 && u16::from_le_bytes([bytes[8], bytes[9]]) == 0
    }

    pub fn is_hgss(&self) -> bool {
        self.version == OriginGame::HeartGold as u8
            || self.version == OriginGame::SoulSilver as u8
    }

    /// Active egg location (extended fields on HGSS, DP fields otherwise).
    pub fn egg_location_index(&self) -> u16 {
        if self.is_hgss() {
            self.egg_location_index_pthgss
        } else {
            self.egg_location_index_dp
        }
    }

    /// Active met location (extended fields on HGSS, DP fields otherwise).
    pub fn met_location_index(&self) -> u16 {
        if self.is_hgss() {
            self.met_location_index_pthgss
        } else {
            self.met_location_index_dp
        }
    }

    pub fn get_ball(&self) -> u8 {
        core::cmp::max(self.ball_dppt, self.ball_hgss)
    }
}
