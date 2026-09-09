extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{string::{String, ToString}, vec::Vec, boxed::Box, collections::{BTreeMap, BTreeSet}, borrow::ToOwned};
#[cfg(not(feature = "std"))] use alloc::format;
use super::Pb8Buffer;
use super::pb8_buffer::Pb8BufferMut;
use super::{Pb8AbilityIndex, Pb8SpeciesAndForm};
use crate::checksum::{Checksum, RefreshChecksum};
use crate::result::{Error, Result};
use crate::traits::ModernEvs;
use crate::traits::{HasSpeciesAndForm, PkmBytes};

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
    AbilityNumber, BinaryGender, ContestStats, FlagSet, HyperTraining, Ivs, Language,
    MarkingsSixShapesColors, OriginGame, Pokerus, Stats8, Stats16Le,
};
use pkm_rs_types::{Gender, PokeDate, TrainerMemory};
use serde::Serialize;

#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

#[cfg(any(feature = "wasm", feature = "alloc"))]
use crate::convert_strategy::ConvertStrategy;
#[cfg(any(feature = "wasm", feature = "alloc"))]
use crate::ohpkm::{OhpkmConvert, OhpkmV2};
#[cfg(any(feature = "wasm", feature = "alloc"))]
use pkm_rs_resources::abilities::AbilityIndexWasm;

#[cfg(feature = "randomize")]
use pkm_rs_types::randomize::Randomize;
#[cfg(feature = "randomize")]
use pkm_rs_types::randomize::RandomizeAndFix;

#[cfg_attr(feature = "wasm", wasm_bindgen(js_name = "Pb8Wasm"))]
#[cfg_attr(feature = "randomize", derive(Randomize))]
#[derive(Debug, Default, Serialize, Clone, Copy, IsShiny4096)]
pub struct Pb8 {
    pub encryption_constant: u32,
    pub sanity: u16,
    pub checksum: u16,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub species_and_form: Pb8SpeciesAndForm,
    pub held_item_index: u16,
    pub trainer_id: u16,
    pub secret_id: u16,
    pub exp: u32,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub ability_index: Pb8AbilityIndex,
    pub ability_num: AbilityNumber,
    pub is_favorite: bool,
    pub can_gigantamax: bool,
    pub markings: MarkingsSixShapesColors,
    pub personality_value: u32,
    pub nature: NatureIndex,
    pub mint_nature: NatureIndex,
    pub is_fateful_encounter: bool,
    pub gender: Gender,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub evs: Stats8,
    pub contest: ContestStats,
    pub pokerus: Pokerus,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub ribbons: ModernRibbonSet<14, MAX_RIBBON_SWSH>,
    pub contest_memory_count: u8,
    pub battle_memory_count: u8,
    pub sociability: u32,
    pub height_scalar: u8,
    pub weight_scalar: u8,
    pub nickname: SizedUtf16String<26>,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub moves: MoveSlots,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub relearn_moves: [MoveIndex; 4],
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub ivs: Ivs,
    pub is_egg: bool,
    pub is_nicknamed: bool,
    pub dynamax_level: u8,
    pub status_condition: u32,
    pub palma: u32,
    pub handler_name: SizedUtf16String<26>,
    pub handler_gender: BinaryGender,
    pub handler_language: Option<Language>,
    pub is_current_handler: bool,
    pub handler_id: u16,
    pub handler_friendship: u8,
    pub handler_memory: TrainerMemory,
    pub fullness: u8,
    pub enjoyment: u8,
    pub game_of_origin: OriginGame,
    pub game_of_origin_battle: Option<OriginGame>,
    pub language: Language,
    pub form_argument: u32,
    pub affixed_ribbon: Option<ModernRibbon>,
    pub trainer_name: SizedUtf16String<26>,
    pub trainer_friendship: u8,
    pub trainer_memory: TrainerMemory,
    pub egg_date: Option<PokeDate>,
    pub met_date: PokeDate,
    pub egg_location_index: u16,
    pub met_location_index: u16,
    pub ball: Ball,
    pub met_level: u8,
    pub trainer_gender: BinaryGender,
    #[cfg_attr(feature = "wasm", wasm_bindgen(skip))]
    pub tr_flags_swsh: FlagSet<14>,
    pub home_tracker: Option<u64>,
    pub hyper_training: HyperTraining,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stat_level: u8,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub current_hp: u16,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stats: Stats16Le,
}

const MAX_RIBBON_SWSH: usize = ModernRibbon::TowerMaster as usize;

impl Pb8 {
    // ------------------------------------------------------------------
    // Deserialise from a Pb8Buffer (the single source of all byte offsets)
    // ------------------------------------------------------------------

    pub fn from_buffer<S: AsRef<[u8]>>(buf: &Pb8Buffer<S>) -> Result<Self> {
        let home_tracker_raw = buf.home_tracker_raw();
        let mut mon = Pb8 {
            encryption_constant: buf.encryption_constant(),
            sanity: buf.sanity(),
            checksum: buf.checksum(),
            species_and_form: buf.species_and_form()?,
            held_item_index: buf.held_item_index(),
            trainer_id: buf.trainer_id(),
            secret_id: buf.secret_id(),
            exp: buf.exp(),
            ability_index: AbilityIndexBounded::try_from(buf.ability_index_raw())?,
            ability_num: buf.ability_num()?,
            is_favorite: buf.is_favorite(),
            can_gigantamax: buf.can_gigantamax(),
            markings: buf.markings(),
            personality_value: buf.personality_value(),
            nature: buf.nature()?,
            mint_nature: buf.mint_nature()?,
            is_fateful_encounter: buf.is_fateful_encounter(),
            gender: buf.gender(),
            evs: buf.evs(),
            contest: buf.contest(),
            pokerus: buf.pokerus(),
            ribbons: buf.ribbons(),
            contest_memory_count: buf.contest_memory_count(),
            battle_memory_count: buf.battle_memory_count(),
            sociability: buf.sociability(),
            height_scalar: buf.height_scalar(),
            weight_scalar: buf.weight_scalar(),
            nickname: buf.nickname(),
            moves: buf.move_slots(),
            relearn_moves: [
                buf.relearn_move(0),
                buf.relearn_move(1),
                buf.relearn_move(2),
                buf.relearn_move(3),
            ],
            ivs: buf.ivs(),
            is_egg: buf.is_egg(),
            is_nicknamed: buf.is_nicknamed(),
            dynamax_level: buf.dynamax_level(),
            status_condition: buf.status_condition(),
            palma: buf.palma(),
            handler_name: buf.handler_name(),
            handler_gender: buf.handler_gender(),
            handler_language: buf.handler_language().ok(),
            is_current_handler: buf.is_current_handler(),
            handler_id: buf.handler_id(),
            handler_friendship: buf.handler_friendship(),
            handler_memory: buf.handler_memory(),
            fullness: buf.fullness(),
            enjoyment: buf.enjoyment(),
            game_of_origin: buf.game_of_origin(),
            game_of_origin_battle: buf.game_of_origin_battle(),
            language: buf.language()?,
            form_argument: buf.form_argument(),
            affixed_ribbon: buf.affixed_ribbon(),
            trainer_name: buf.trainer_name(),
            trainer_friendship: buf.trainer_friendship(),
            trainer_memory: buf.trainer_memory(),
            egg_date: buf.egg_date(),
            met_date: buf.met_date(),
            egg_location_index: buf.egg_location_index(),
            met_location_index: buf.met_location_index(),
            ball: buf.ball(),
            met_level: buf.met_level(),
            trainer_gender: buf.trainer_gender(),
            tr_flags_swsh: FlagSet::from_bytes(buf.tr_flags_swsh_raw()),
            home_tracker: if home_tracker_raw != 0 {
                Some(home_tracker_raw)
            } else {
                None
            },
            hyper_training: buf.hyper_training(),
            ..Default::default()
        };

        mon.stat_level = mon.calculate_level();
        mon.stats = mon.calculate_stats();
        mon.current_hp = mon.stats.hp;

        Ok(mon)
    }

    pub fn write_to_box_buffer(&self, buf: &mut Pb8BufferMut) {
        buf.set_encryption_constant(self.encryption_constant);
        buf.reset_sanity();
        buf.set_species_and_form(self.species_and_form.0);
        buf.set_held_item_index(self.held_item_index);
        buf.set_trainer_id(self.trainer_id);
        buf.set_secret_id(self.secret_id);
        buf.set_exp(self.exp);
        buf.set_ability_index(self.ability_index);
        buf.set_ability_num(self.ability_num);
        buf.set_is_favorite(self.is_favorite);
        buf.set_can_gigantamax(self.can_gigantamax);
        buf.set_markings(self.markings);
        buf.set_personality_value(self.personality_value);
        buf.set_nature(self.nature);
        buf.set_mint_nature(self.mint_nature);
        buf.set_gender(self.gender);
        buf.set_is_fateful_encounter(self.is_fateful_encounter);
        buf.set_evs(self.evs);
        buf.set_contest(self.contest);
        buf.set_pokerus(self.pokerus);
        buf.set_ribbons(self.ribbons);
        buf.set_contest_memory_count(self.contest_memory_count);
        buf.set_battle_memory_count(self.battle_memory_count);
        buf.set_sociability(self.sociability);
        buf.set_height_scalar(self.height_scalar);
        buf.set_weight_scalar(self.weight_scalar);
        buf.set_nickname(&self.nickname);
        buf.set_move_slots(&self.moves);
        buf.set_relearn_move(0, self.relearn_moves[0]);
        buf.set_relearn_move(1, self.relearn_moves[1]);
        buf.set_relearn_move(2, self.relearn_moves[2]);
        buf.set_relearn_move(3, self.relearn_moves[3]);
        buf.set_ivs(&self.ivs);
        buf.set_is_egg(self.is_egg);
        buf.set_is_nicknamed(self.is_nicknamed);
        buf.set_dynamax_level(self.dynamax_level);
        buf.set_status_condition(self.status_condition);
        buf.set_palma(self.palma);
        buf.set_handler_name(&self.handler_name);
        buf.set_handler_gender(self.handler_gender);
        if let Some(lang) = self.handler_language {
            buf.set_handler_language(lang);
        }
        buf.set_is_current_handler(self.is_current_handler);
        buf.set_handler_id(self.handler_id);
        buf.set_handler_friendship(self.handler_friendship);
        buf.set_handler_memory(self.handler_memory);
        buf.set_fullness(self.fullness);
        buf.set_enjoyment(self.enjoyment);
        buf.set_game_of_origin(self.game_of_origin);
        buf.set_game_of_origin_battle(self.game_of_origin_battle);
        buf.set_language(self.language);
        buf.set_form_argument(self.form_argument);
        buf.set_affixed_ribbon(self.affixed_ribbon);
        buf.set_trainer_name(&self.trainer_name);
        buf.set_trainer_friendship(self.trainer_friendship);
        buf.set_trainer_memory(self.trainer_memory);
        buf.set_egg_date(self.egg_date);
        buf.set_met_date(self.met_date);
        buf.set_egg_location_index(self.egg_location_index);
        buf.set_met_location_index(self.met_location_index);
        buf.set_ball(self.ball);
        buf.set_met_level(self.met_level);
        buf.set_trainer_gender(self.trainer_gender);
        buf.set_tr_flags_swsh(&self.tr_flags_swsh.to_bytes());
        buf.set_home_tracker_raw(self.home_tracker.unwrap_or(0));
        buf.set_hyper_training(self.hyper_training);
        buf.set_stat_level(self.stat_level);
        buf.set_current_hp(self.current_hp);
        buf.set_stats(self.stats);

        buf.refresh_checksum();
    }

    pub fn write_to_party_buffer(&self, buf: &mut Pb8BufferMut) {
        self.write_to_box_buffer(buf);
    }

    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        let size = bytes.len();
        match size {
            Self::BOX_SIZE => Self::from_buffer(&Pb8Buffer::new(bytes)),
            _ => Err(Error::buffer_size(Self::BOX_SIZE, size)),
        }
    }

    pub fn from_encrypted_bytes(mut bytes: Box<[u8]>) -> Result<Self> {
        Self::from_buffer(Pb8Buffer::new_mut(&mut bytes).decrypted())
    }

    pub fn to_box_bytes_encrypted(self) -> Box<[u8]> {
        let mut bytes = self.to_box_bytes();
        Pb8Buffer::new_mut(&mut bytes).encrypt();

        bytes
    }

    pub fn calculate_checksum(&self) -> u16 {
        let mut bytes = [0u8; Self::BOX_SIZE];
        self.write_box_bytes(&mut bytes);
        Pb8Buffer::new(&bytes).calculate_checksum()
    }

    pub fn refresh_checksum(&mut self) {
        self.checksum = self.calculate_checksum();
    }

    pub fn calculate_stats(&self) -> Stats16Le {
        helpers::calculate_stats_modern(
            // OUR FIX (same upstream divergence as try_new above): BDSP
            // base stats come from the Sinnoh tables, not Galar's.
            MetadataSource::DiamondPearl,
            self.species_and_form.0,
            &self.ivs,
            &self.evs,
            self.calculate_level(),
            self.mint_nature.get_metadata(),
            Some(self.hyper_training),
        )
        .unwrap_or_else(|| {
            panic!(
                "pb8 has species/form present in sword + shield: {:#?}",
                self.species_and_form.0
            )
        })
    }

    pub fn recalculate_stats(&mut self) {
        self.stats = self.calculate_stats();
    }

    pub fn is_empty_slot(bytes: &[u8]) -> bool {
        let mut owned = bytes.to_owned();
        let mut buffer = Pb8Buffer::new_mut(&mut owned);
        buffer.decrypt();

        buffer.species_ndex() == 0
    }
}

impl PkmBytes for Pb8 {
    const BOX_SIZE: usize = 344;
    const PARTY_SIZE: usize = 344; // Party data is also stored in the PC now

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        self.write_to_box_buffer(&mut Pb8BufferMut::new_mut(bytes))
    }

    fn to_box_bytes(&self) -> Box<[u8]> {
        let mut bytes = Box::new([0u8; Self::BOX_SIZE]);
        self.write_box_bytes(bytes.as_mut_slice());

        bytes
    }
}

impl HasSpeciesAndForm for Pb8 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        self.species_and_form.0.get_species_metadata()
    }

    fn get_forme_metadata(&self) -> &'static FormMetadata {
        self.species_and_form.0.get_forme_metadata()
    }

    fn calculate_level(&self) -> u8 {
        self.get_species_metadata()
            .level_up_type
            .calculate_level(self.exp)
    }
}

#[cfg_attr(feature = "wasm", wasm_bindgen(js_class = "Pb8Wasm"))]
#[allow(clippy::missing_const_for_fn)]
impl Pb8 {
#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = fromOhpkmBytes))]
    pub fn from_ohpkm_bytes(
        bytes: Vec<u8>,
        strategy: ConvertStrategy,
    ) -> core::result::Result<Pb8, JsValue> {
        let ohpkm = OhpkmV2::from_bytes(&bytes).map_err(JsValue::from)?;
        Pb8::from_ohpkm(&ohpkm, strategy).map_err(JsValue::from)
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = fromBytes))]
#[cfg(feature = "wasm")]
    pub fn from_byte_vector(bytes: Vec<u8>) -> core::result::Result<Pb8, JsValue> {
        Pb8::from_bytes(&bytes).map_err(JsValue::from)
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = fromEncryptedBytes))]
#[cfg(feature = "wasm")]
    pub fn take_from_encrypted_bytes(bytes: Box<[u8]>) -> core::result::Result<Pb8, JsValue> {
        Pb8::from_encrypted_bytes(bytes).map_err(JsValue::from)
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = toBytes))]
    pub fn to_bytes_js(&self) -> Box<[u8]> {
        self.to_box_bytes()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = abilityIndex))]
    pub fn ability_index(&self) -> AbilityIndexWasm {
        AbilityIndexWasm::try_from(self.ability_index.to_u16())
            .expect("AbilityIndexWasm should accept any valid ability index")
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter = abilityIndex))]
    pub fn set_ability_index(&mut self, value: AbilityIndexWasm) -> Result<()> {
        self.ability_index = AbilityIndexBounded::try_from(value.to_u16())?;
        Ok(())
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn move_indices(&self) -> Vec<u16> {
        self.moves.indices()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter))]
    pub fn set_move_indices(&mut self, value: &[u16]) {
        self.moves.set_indices(value);
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn move_pp(&self) -> Vec<u8> {
        self.moves.pp()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter))]
    pub fn set_move_pp(&mut self, value: &[u8]) {
        self.moves.set_pp(value);
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn move_pp_ups(&self) -> Vec<u8> {
        self.moves.pp_ups()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter))]
    pub fn set_move_pp_ups(&mut self, value: &[u8]) {
        self.moves.set_pp_ups(value);
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn relearn_move_indices(&self) -> Vec<u16> {
        self.relearn_moves.into_iter().map(u16::from).collect()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter))]
    pub fn set_relearn_move_indices(&mut self, value: Vec<u16>) {
        self.relearn_moves = [
            MoveIndex::from(value[0]),
            MoveIndex::from(value[1]),
            MoveIndex::from(value[2]),
            MoveIndex::from(value[3]),
        ]
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn ribbons(&self) -> Vec<String> {
        self.ribbons
            .get_ribbons()
            .iter()
            .map(|ribbon| ribbon.to_string())
            .collect()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter))]
    pub fn ribbon_bytes(&self) -> Vec<u8> {
        self.ribbons.to_bytes().into_iter().collect()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter))]
    pub fn set_ribbon_indices(&mut self, indices: Vec<usize>) {
        self.ribbons
            .set_ribbons(indices.into_iter().filter_map(ModernRibbon::from_index));
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen)]
    pub fn set_species_and_form(
        &mut self,
        national_dex: u16,
        form_index: u16,
    ) -> core::result::Result<(), JsValue> {
        match SpeciesForm::new(national_dex, form_index) {
            Ok(species_and_form) => {
                self.species_and_form = species_and_form.try_into()?;
                Ok(())
            }
            Err(e) => Err(JsValue::from_str(&e.to_string())),
        }
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = nationalDex))]
    pub fn national_dex_js(&self) -> u16 {
        self.species_and_form.0.get_ndex_wasm()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = formIndex))]
    pub fn form_index_js(&self) -> u16 {
        self.species_and_form.0.get_forme_index()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = evs))]
    pub fn evs_js(&self) -> Stats16Le {
        self.evs.into()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter = evs))]
    pub fn set_evs_js(&mut self, v: Stats16Le) {
        self.evs = v.to_stats8_truncated()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = ivs))]
    pub fn ivs_js(&self) -> Stats16Le {
        self.ivs.into()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter = ivs))]
    pub fn set_ivs_js(&mut self, v: Stats16Le) {
        self.ivs = v.to_ivs_capped()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(getter = trFlagsSwSh))]
    pub fn tr_flags_swsh_js(&self) -> Vec<u8> {
        self.tr_flags_swsh.to_bytes().to_vec()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(setter = trFlagsSwSh))]
    pub fn set_tr_flags_swsh_js(&mut self, flags: &[u8]) -> Result<()> {
        self.tr_flags_swsh = FlagSet::from_bytes(flags.try_into().map_err(|_| {
            Error::other("incorrect byte array size passed to set_tr_flags_swsh_js")
        })?);

        Ok(())
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = toOhpkm))]
    pub fn to_ohpkm(&self) -> Result<OhpkmV2> {
        OhpkmV2::convert_with_backup(self, &self.to_party_bytes())
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = isEmptySlot))]
    pub fn is_empty_slot_wasm(bytes: &[u8]) -> bool {
        Self::is_empty_slot(bytes)
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = calculateChecksum))]
    pub fn calculate_checksum_js(&self) -> u16 {
        self.calculate_checksum()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = calculateLevel))]
    pub fn calculate_level_js(&self) -> u8 {
        self.calculate_level()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = calculateStats))]
    pub fn calculate_stats_js(&self) -> Stats16Le {
        self.calculate_stats()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = recalculateStats))]
    pub fn recalculate_stats_js(&mut self) {
        self.recalculate_stats()
    }

#[cfg(feature = "wasm")]
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_name = toBoxBytesEncrypted))]
    pub fn to_box_bytes_encrypted_js(&self) -> Box<[u8]> {
        self.to_box_bytes_encrypted()
    }
}

impl ModernEvs for Pb8 {
    fn get_evs(&self) -> Stats8 {
        self.evs
    }
}

#[cfg(feature = "randomize")]
impl RandomizeAndFix for Pb8 {
    fn fix<R: rand::prelude::Rng>(&mut self, _: &mut R) {
        self.stat_level = self.calculate_level();
        self.stats = self.calculate_stats();
        self.current_hp = self.stats.hp;
        self.refresh_checksum();
    }
}

#[cfg(test)]
impl crate::tests::PkhexJson for Pb8 {
    fn to_pkhex_json_value(&self) -> core::result::Result<serde_json::Value, serde_json::Error> {
        let mut value = serde_json::to_value(self)?;
        value["nickname_trash"] = serde_json::json!(
            self.nickname
                .bytes()
                .iter()
                .map(|b| alloc::format!("{:02X}", b))
                .collect::<String>()
        );
        value["level"] = serde_json::json!(self.calculate_level());

        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use core::path::PathBuf;

    use crate::convert_strategy::ConvertStrategy;
    use crate::gen8_bdsp::Pb8;
    use crate::ohpkm::{OhpkmConvert, OhpkmV2};
    use crate::tests::{self, TestResult};
    use crate::traits::PkmBytes;

    #[cfg(feature = "randomize")]
    use crate::tests::TestErrorWithSeed;
    #[cfg(feature = "randomize")]
    use pkm_rs_types::randomize::RandomizeAndFix;
    #[cfg(feature = "randomize")]
    use rand::{SeedableRng, rngs::StdRng};

    // No upstream `.pb8` fixtures exist. BDSP's PB8 is the PK8 344-byte record
    // 1:1 (same offsets, same Gen89 crypto, same 0x08..0x148 checksum span), so
    // the real `.pk8` fixtures are valid PB8 records for a byte-stability check
    // of the Pb8Buffer offset layer.
    #[test]
    fn to_from_bytes_pk8_layout() -> TestResult<()> {
        tests::to_from_bytes_all_in_dir::<Pb8>(
            &PathBuf::from("test-files").join("pkm-files").join("pk8"),
        )
    }

    #[cfg(feature = "randomize")]
    #[test]
    fn to_from_bytes_random() -> core::result::Result<(), TestErrorWithSeed> {
        for seed in 0..=1000 {
            let mon = Pb8::randomize_and_fix(&mut StdRng::seed_from_u64(seed));
            tests::find_inconsistencies_to_from_bytes(mon)
                .map_err(|error| TestErrorWithSeed { seed, error })?;
        }
        Ok(())
    }

    #[test]
    fn from_ohpkm_smoke() -> TestResult<()> {
        let mon = tests::pkm_from_file::<OhpkmV2>(&PathBuf::from("ohpkm").join("Machamp.ohpkm"))?.0;
        let _ = Pb8::from_ohpkm(&mon, ConvertStrategy::default())?;
        Ok(())
    }

    // OHPKM -> Pb8 -> box bytes -> Pb8: the written checksum is self-consistent
    // and the core identity survives the trip.
    #[test]
    fn ohpkm_roundtrip_stable() -> TestResult<()> {
        let ohpkm =
            tests::pkm_from_file::<OhpkmV2>(&PathBuf::from("ohpkm").join("Machamp.ohpkm"))?.0;
        let pb8 = Pb8::from_ohpkm(&ohpkm, ConvertStrategy::default())?;
        let bytes = pb8.to_box_bytes();
        let re = Pb8::from_bytes(&bytes)?;
        assert_eq!(re.checksum, re.calculate_checksum(), "pb8 checksum self-consistent");

        let back = Pb8::from_ohpkm(
            &OhpkmV2::convert_without_backup(&pb8),
            ConvertStrategy::default(),
        )?;
        assert_eq!(
            back.species_and_form.into_inner().get_ndex(),
            pb8.species_and_form.into_inner().get_ndex()
        );
        assert_eq!(back.personality_value, pb8.personality_value);
        assert_eq!(back.encryption_constant, pb8.encryption_constant);
        assert_eq!(back.trainer_id, pb8.trainer_id);
        assert_eq!(back.secret_id, pb8.secret_id);
        assert_eq!(back.exp, pb8.exp);
        assert_eq!(back.ivs, pb8.ivs);
        assert_eq!(back.evs, pb8.evs);
        assert_eq!(back.nature, pb8.nature);
        assert_eq!(back.nickname.bytes(), pb8.nickname.bytes());
        Ok(())
    }
}
