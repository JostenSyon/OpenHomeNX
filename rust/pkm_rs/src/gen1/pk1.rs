extern crate alloc;
#[cfg(not(feature = "std"))]
use alloc::{
    boxed::Box,
    string::{String, ToString},
    vec::Vec,
};
use crate::conversion::gen1_pokemon_index;
use crate::result::{Error, Result};
use crate::traits::{HasSpeciesAndForm, IsShiny, PkmBytes};
use pkm_rs_resources::lookup;
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::{Language, NationalDex, StatsPreSplit};
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
pub struct Pk1 {
    pub national_dex: u16,
    pub current_hp: u16,
    pub level: u8,
    pub status_condition: u8,
    pub type1: u8,
    pub type2: u8,
    pub held_item_index: u8,
    pub moves: [u8; 4],
    pub trainer_id: u16,
    pub exp: u32,
    pub evs_g12: StatsPreSplit,
    pub dvs: StatsPreSplit,
    pub move_pp: [u8; 4],
    pub move_pp_ups: [u8; 4],
    pub trainer_name: String,
    pub nickname: String,
}

impl Pk1 {
    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        let len = bytes.len();
        if len < 33 {
            return Err(Error::buffer_size(33, len));
        }
        let buf = if len >= 3 && bytes[2] == 0xff {
            &bytes[3..]
        } else {
            bytes
        };
        let has_extra = buf.len() >= 66;

        let national_dex =
            gen1_pokemon_index::decode(buf[0]).map(|v| v as u16).unwrap_or(0);

        let exp = ((buf[0xe] as u32) << 16) | ((buf[0xf] as u32) << 8) | (buf[0x10] as u32);

        let dv_raw = ((buf[0x1b] as u16) << 8) | (buf[0x1c] as u16);
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
            buf[0x1d] & 0x3f,
            buf[0x1e] & 0x3f,
            buf[0x1f] & 0x3f,
            buf[0x20] & 0x3f,
        ];
        let move_pp_ups = [
            (buf[0x1d] >> 6) & 0x03,
            (buf[0x1e] >> 6) & 0x03,
            (buf[0x1f] >> 6) & 0x03,
            (buf[0x20] >> 6) & 0x03,
        ];

        let trainer_name = if has_extra {
            read_gb_string(buf, 0x2c, 8)
        } else {
            String::from("TRAINER")
        };

        let nickname = if has_extra {
            read_gb_string(buf, 0x37, 11)
        } else {
            lookup::species_name(NationalDex::assert_valid(national_dex), Language::English)
        };

        Ok(Pk1 {
            national_dex,
            current_hp: u16::from_le_bytes([buf[1], buf[2]]),
            level: buf[3],
            status_condition: buf[4],
            type1: buf[5],
            type2: buf[6],
            held_item_index: buf[7],
            moves: [buf[8], buf[9], buf[0xa], buf[0xb]],
            trainer_id: u16::from_le_bytes([buf[0xc], buf[0xd]]),
            exp,
            evs_g12: StatsPreSplit {
                hp: u16::from_le_bytes([buf[0x11], buf[0x12]]),
                atk: u16::from_le_bytes([buf[0x13], buf[0x14]]),
                def: u16::from_le_bytes([buf[0x15], buf[0x16]]),
                spe: u16::from_le_bytes([buf[0x17], buf[0x18]]),
                spc: u16::from_le_bytes([buf[0x19], buf[0x1a]]),
            },
            dvs,
            move_pp,
            move_pp_ups,
            trainer_name,
            nickname,
        })
    }

    pub fn to_bytes(&self) -> Box<[u8]> {
        let mut buf = [0u8; 33];

        buf[0] = gen1_pokemon_index::encode(self.national_dex as u8).unwrap_or(0);
        buf[1..3].copy_from_slice(&self.current_hp.to_le_bytes());
        buf[3] = self.level;
        buf[4] = self.status_condition;
        buf[5] = self.type1;
        buf[6] = self.type2;
        buf[7] = self.held_item_index;
        buf[8..12].copy_from_slice(&self.moves);
        buf[0xc..0xe].copy_from_slice(&self.trainer_id.to_le_bytes());
        buf[0xe] = ((self.exp >> 16) & 0xff) as u8;
        buf[0xf] = ((self.exp >> 8) & 0xff) as u8;
        buf[0x10] = (self.exp & 0xff) as u8;
        buf[0x11..0x13].copy_from_slice(&self.evs_g12.hp.to_le_bytes());
        buf[0x13..0x15].copy_from_slice(&self.evs_g12.atk.to_le_bytes());
        buf[0x15..0x17].copy_from_slice(&self.evs_g12.def.to_le_bytes());
        buf[0x17..0x19].copy_from_slice(&self.evs_g12.spe.to_le_bytes());
        buf[0x19..0x1b].copy_from_slice(&self.evs_g12.spc.to_le_bytes());

        let dv_raw: u16 = (self.dvs.atk << 12)
            | (self.dvs.def << 8)
            | (self.dvs.spe << 4)
            | self.dvs.spc;
        buf[0x1b..0x1d].copy_from_slice(&dv_raw.to_be_bytes());

        for i in 0..4 {
            buf[0x1d + i] = (self.move_pp_ups[i] << 6) | (self.move_pp[i] & 0x3f);
        }

        buf
    }

    pub fn to_bytes_full(&self) -> Box<[u8]> {
        let mut buf = [0u8; 66];
        let box_bytes = self.to_bytes();
        buf[..33].copy_from_slice(&box_bytes);
        write_gb_string(&mut buf, &self.trainer_name, 0x2c, 8);
        write_gb_string(&mut buf, &self.nickname, 0x37, 11);
        Box::new(buf)
    }
}

impl PkmBytes for Pk1 {
    const BOX_SIZE: usize = 33;
    const PARTY_SIZE: usize = 66;

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

impl HasSpeciesAndForm for Pk1 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        SpeciesForm::base_form(NationalDex::assert_valid(self.national_dex)).get_species_metadata()
    }

    fn get_forme_metadata(&self) -> &'static FormMetadata {
        SpeciesForm::base_form(NationalDex::assert_valid(self.national_dex)).get_forme_metadata()
    }

    fn calculate_level(&self) -> u8 {
        self.level
    }
}

impl IsShiny for Pk1 {
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
#[cfg_attr(feature = "wasm", wasm_bindgen(js_name = Pk1Wasm))]
impl Pk1 {
    #[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = fromBytes))]
    pub fn from_bytes_js(bytes: Vec<u8>) -> core::result::Result<Pk1, JsValue> {
        Pk1::from_bytes(&bytes).map_err(JsValue::from)
    }

    #[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = toBytes))]
    pub fn get_bytes_wasm(&self) -> Box<[u8]> {
        self.to_box_bytes()
    }
}
