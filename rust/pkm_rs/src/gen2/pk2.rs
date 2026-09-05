extern crate alloc;
#[cfg(not(feature = "std"))]
use alloc::{
    boxed::Box,
    string::{String, ToString},
    vec::Vec,
};
use crate::result::{Error, Result};
use crate::traits::{HasSpeciesAndForm, IsShiny, PkmBytes};
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::{NationalDex, StatsPreSplit};
use serde::Serialize;

fn read_gb_string(bytes: &[u8], offset: usize, length: usize) -> String {
    let mut s = String::new();
    for i in offset..offset + length {
        if i >= bytes.len() {
            break;
        }
        let b = bytes[i];
        if b == 0x50 {
            break;
        }
        if let Some(c) = crate::conversion::gameboy_string_encoding::decode(b) {
            s.push(c);
        }
    }
    s
}

fn write_gb_string(bytes: &mut [u8], s: &str, offset: usize, length: usize) {
    let mut written = 0;
    for ch in s.chars() {
        if written >= length {
            break;
        }
        if let Some(encoded) = crate::conversion::gameboy_string_encoding::encode(ch) {
            bytes[offset + written] = encoded;
            written += 1;
        }
    }
    if written < length {
        bytes[offset + written] = 0x50;
    }
}

#[derive(Debug, Default, Serialize, Clone)]
pub struct Pk2 {
    pub national_dex: u8,
    pub held_item_index: u8,
    pub moves: [u8; 4],
    pub trainer_id: u16,
    pub exp: u32,
    pub evs_g12: StatsPreSplit,
    pub dvs: StatsPreSplit,
    pub move_pp: [u8; 4],
    pub move_pp_ups: [u8; 4],
    pub trainer_friendship: u8,
    pub pokerus_byte: u8,
    pub met_time_of_day: u8,
    pub met_level: u8,
    pub met_location_index: u8,
    pub level: u8,
    pub status_condition: u8,
    pub current_hp: u8,
    pub trainer_name: String,
    pub nickname: String,
    pub trainer_gender: u8,
}

impl Pk2 {
    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        let len = bytes.len();
        if len < 32 {
            return Err(Error::buffer_size(32, len));
        }
        let buf = if len >= 3 && bytes[2] == 0xff {
            &bytes[3..]
        } else {
            bytes
        };
        let has_extra = buf.len() >= 70;

        let exp = ((buf[0x8] as u32) << 16) | ((buf[0x9] as u32) << 8) | (buf[0xa] as u32);

        let dv_raw = ((buf[0x15] as u16) << 8) | (buf[0x16] as u16);
        let dvs = StatsPreSplit {
            atk: ((dv_raw >> 12) & 0x0f),
            def: ((dv_raw >> 8) & 0x0f),
            spe: ((dv_raw >> 4) & 0x0f),
            spc: (dv_raw & 0x0f),
            hp: (((dv_raw >> 12) & 1) << 3
                | (((dv_raw >> 8) & 1) << 2)
                | (((dv_raw >> 4) & 1) << 1)
                | (dv_raw & 1)),
        };

        let move_pp = [
            buf[0x17] & 0x3f,
            buf[0x18] & 0x3f,
            buf[0x19] & 0x3f,
            buf[0x1a] & 0x3f,
        ];
        let move_pp_ups = [
            (buf[0x17] >> 6) & 0x03,
            (buf[0x18] >> 6) & 0x03,
            (buf[0x19] >> 6) & 0x03,
            (buf[0x1a] >> 6) & 0x03,
        ];

        let met_level = buf[0x1d] & 0x3f;
        let met_time_of_day = (buf[0x1d] >> 6) & 0x03;
        let met_location_index = buf[0x1e] & 0x7f;
        let trainer_gender = (buf[0x1e] >> 7) & 1;

        let level = buf[0x1f];

        let status_condition = if has_extra { buf[0x20] } else { 0 };
        let current_hp = if has_extra { buf[0x22] } else { 0 };

        let trainer_name = if has_extra {
            read_gb_string(buf, 0x30, 8)
        } else {
            String::from("TRAINER")
        };

        let nickname = if has_extra {
            read_gb_string(buf, 0x3b, 11)
        } else {
            String::new()
        };

        Ok(Pk2 {
            national_dex: buf[0],
            held_item_index: buf[1],
            moves: [buf[2], buf[3], buf[4], buf[5]],
            trainer_id: u16::from_be_bytes([buf[6], buf[7]]),
            exp,
            evs_g12: StatsPreSplit {
                hp: u16::from_be_bytes([buf[0xb], buf[0xc]]),
                atk: u16::from_be_bytes([buf[0xd], buf[0xe]]),
                def: u16::from_be_bytes([buf[0xf], buf[0x10]]),
                spe: u16::from_be_bytes([buf[0x11], buf[0x12]]),
                spc: u16::from_be_bytes([buf[0x13], buf[0x14]]),
            },
            dvs,
            move_pp,
            move_pp_ups,
            trainer_friendship: buf[0x1b],
            pokerus_byte: buf[0x1c],
            met_time_of_day,
            met_level,
            met_location_index,
            level,
            status_condition,
            current_hp,
            trainer_name,
            nickname,
            trainer_gender,
        })
    }

    pub fn to_bytes(&self) -> Box<[u8]> {
        let mut buf = [0u8; 32];

        buf[0] = self.national_dex;
        buf[1] = self.held_item_index;
        buf[2..6].copy_from_slice(&self.moves);
        buf[6..8].copy_from_slice(&self.trainer_id.to_be_bytes());
        buf[0x8] = ((self.exp >> 16) & 0xff) as u8;
        buf[0x9] = ((self.exp >> 8) & 0xff) as u8;
        buf[0xa] = (self.exp & 0xff) as u8;
        buf[0xb..0xd].copy_from_slice(&self.evs_g12.hp.to_be_bytes());
        buf[0xd..0xf].copy_from_slice(&self.evs_g12.atk.to_be_bytes());
        buf[0xf..0x11].copy_from_slice(&self.evs_g12.def.to_be_bytes());
        buf[0x11..0x13].copy_from_slice(&self.evs_g12.spe.to_be_bytes());
        buf[0x13..0x15].copy_from_slice(&self.evs_g12.spc.to_be_bytes());

        let dv_raw: u16 = (self.dvs.atk << 12)
            | (self.dvs.def << 8)
            | (self.dvs.spe << 4)
            | self.dvs.spc;
        buf[0x15..0x17].copy_from_slice(&dv_raw.to_be_bytes());

        for i in 0..4 {
            buf[0x17 + i] = (self.move_pp_ups[i] << 6) | (self.move_pp[i] & 0x3f);
        }

        buf[0x1b] = self.trainer_friendship;
        buf[0x1c] = self.pokerus_byte;
        buf[0x1d] = (self.met_time_of_day << 6) | (self.met_level & 0x3f);
        buf[0x1e] = (self.trainer_gender << 7) | (self.met_location_index & 0x7f);
        buf[0x1f] = self.level;

        Box::new(buf)
    }

    pub fn to_bytes_full(&self) -> Box<[u8]> {
        let mut buf = [0u8; 73];
        let box_bytes = self.to_bytes();
        buf[..32].copy_from_slice(&box_bytes);
        buf[0x20] = self.status_condition;
        buf[0x22] = self.current_hp;
        write_gb_string(&mut buf, &self.trainer_name, 0x30, 8);
        write_gb_string(&mut buf, &self.nickname, 0x3b, 11);
        Box::new(buf)
    }
}

impl PkmBytes for Pk2 {
    const BOX_SIZE: usize = 32;
    const PARTY_SIZE: usize = 73;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        let box_data = self.to_bytes();
        bytes[..Self::BOX_SIZE].copy_from_slice(&box_data);
    }

    fn write_party_bytes(&self, bytes: &mut [u8]) {
        let full = self.to_bytes_full();
        bytes[..Self::PARTY_SIZE].copy_from_slice(&full);
    }

    fn to_box_bytes(&self) -> Box<[u8]> {
        self.to_bytes()
    }

    fn to_party_bytes(&self) -> Box<[u8]> {
        self.to_bytes_full()
    }
}

impl HasSpeciesAndForm for Pk2 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        SpeciesForm::base_form(NationalDex::assert_valid(self.national_dex as u16))
            .get_species_metadata()
    }

    fn get_forme_metadata(&self) -> &'static FormMetadata {
        SpeciesForm::base_form(NationalDex::assert_valid(self.national_dex as u16))
            .get_forme_metadata()
    }

    fn calculate_level(&self) -> u8 {
        self.level
    }
}

impl IsShiny for Pk2 {
    fn is_shiny(&self) -> bool {
        self.dvs.spe == 10
            && self.dvs.def == 10
            && self.dvs.spc == 10
            && matches!(self.dvs.atk, 2 | 3 | 6 | 7 | 10 | 11 | 14 | 15)
    }

    fn is_square_shiny(&self) -> bool {
        false
    }
}

#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

#[cfg(feature = "wasm")]
#[cfg_attr(feature = "wasm", wasm_bindgen(js_name = Pk2Wasm))]
impl Pk2 {
    #[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = fromBytes))]
    pub fn from_bytes_js(bytes: Vec<u8>) -> core::result::Result<Pk2, JsValue> {
        Pk2::from_bytes(&bytes).map_err(JsValue::from)
    }

    #[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = toBytes))]
    pub fn get_bytes_wasm(&self) -> Box<[u8]> {
        self.to_box_bytes()
    }
}
