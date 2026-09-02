extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{string::{String, ToString}, vec::Vec, boxed::Box, collections::{BTreeMap, BTreeSet}, borrow::ToOwned};
#[cfg(not(feature = "std"))] use alloc::format;
use crate::pkhex_text;

use pkm_rs_types::{Language, NationalDex, OriginGame};
#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

pub fn species_name(national_dex: NationalDex, language: Language) -> &'static str {
    pkhex_text::species_name(language, national_dex)
}

#[cfg_attr(feature = "wasm", wasm_bindgen)]
pub struct Lookup;

#[cfg_attr(feature = "wasm", wasm_bindgen)]
impl Lookup {
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = speciesName))]
    pub fn species_name(national_dex: u16, language: Language) -> String {
        let Ok(national_dex) = NationalDex::new(national_dex) else {
            return alloc::format!("UNKNOWN SPECIES ({national_dex})");
        };
        pkhex_text::species_name(language, national_dex).to_owned()
    }

    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = locationName))]
    pub fn location_name(game: OriginGame, language: Language, index: usize) -> Option<String> {
        pkhex_text::location_name(game, language, index).map(&str::to_owned)
    }
}