extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{string::String, vec::Vec, boxed::Box, borrow::ToOwned};
use super::Pa8Buffer;
use super::pa8_buffer::Pa8BufferMut;
use super::{Pa8AbilityIndex, Pa8SpeciesAndForm};
use crate::checksum::{Checksum, RefreshChecksum};
use crate::result::{Error, Result};
use crate::traits::ModernEvs;
use crate::traits::{HasSpeciesAndForm, PkmBytes};

use pkm_rs_derive::IsShiny4096;
use pkm_rs_resources::abilities::AbilityIndexBounded;
use pkm_rs_resources::ball::Ball;
use pkm_rs_resources::helpers;
use pkm_rs_resources::metadata_source::MetadataSource;
use pkm_rs_resources::moves::MoveSlots;
use pkm_rs_resources::natures::NatureIndex;
use pkm_rs_resources::ribbons::{ModernRibbon, ModernRibbonSet};
use pkm_rs_resources::species::{FormMetadata, SpeciesForm, SpeciesMetadata};
use pkm_rs_types::strings::SizedUtf16String;
use pkm_rs_types::{
    AbilityNumber, BinaryGender, ContestStats, FlagSet, Gender, Ivs, Language,
    MarkingsSixShapesColors, OriginGame, PokeDate, Pokerus, Stats8, Stats16Le,
};
use serde::Serialize;

pub(crate) const MAX_RIBBON_LA: usize = ModernRibbon::TwinklingStar as usize;

#[cfg_attr(feature = "randomize", derive(pkm_rs_types::randomize::Randomize))]
#[derive(Debug, Default, Serialize, Clone, Copy, IsShiny4096)]
pub struct Pa8 {
    pub encryption_constant: u32,
    pub sanity: u16,
    pub checksum: u16,
    pub species_and_form: Pa8SpeciesAndForm,
    pub held_item_index: u16,
    pub trainer_id: u16,
    pub secret_id: u16,
    pub exp: u32,
    pub ability_index: Pa8AbilityIndex,
    pub ability_num: AbilityNumber,
    pub is_favorite: bool,
    pub can_gigantamax: bool,
    pub is_alpha: bool,
    pub is_noble: bool,
    pub markings: MarkingsSixShapesColors,
    pub personality_value: u32,
    pub nature: NatureIndex,
    pub mint_nature: NatureIndex,
    pub is_fateful_encounter: bool,
    pub flag2_la: bool,
    pub gender: Gender,
    pub evs: Stats8,
    pub contest: ContestStats,
    pub pokerus: Pokerus,
    pub ribbons: ModernRibbonSet<14, MAX_RIBBON_LA>,
    pub contest_memory_count: u8,
    pub battle_memory_count: u8,
    pub alpha_move: u16,
    pub sociability: u32,
    pub height_scalar: u8,
    pub weight_scalar: u8,
    pub scale: u8,
    pub nickname: SizedUtf16String<26>,
    pub moves: MoveSlots,
    pub ivs: Ivs,
    pub is_egg: bool,
    pub is_nicknamed: bool,
    pub dynamax_level: u8,
    pub status_condition: u32,
    pub gvs: Stats8,
    pub handler_name: SizedUtf16String<26>,
    pub handler_gender: BinaryGender,
    pub handler_language: Option<Language>,
    pub is_current_handler: bool,
    pub handler_id: u16,
    pub handler_friendship: u8,
    pub fullness: u8,
    pub enjoyment: u8,
    pub game_of_origin: OriginGame,
    pub game_of_origin_battle: Option<OriginGame>,
    pub language: Language,
    pub form_argument: u32,
    pub affixed_ribbon: Option<ModernRibbon>,
    pub trainer_name: SizedUtf16String<26>,
    pub trainer_friendship: u8,
    pub egg_date: Option<PokeDate>,
    pub met_date: PokeDate,
    pub egg_location_index: u16,
    pub met_location_index: u16,
    pub ball: Ball,
    pub met_level: u8,
    pub trainer_gender: BinaryGender,
    pub move_flags_la: FlagSet<14>,
    pub tutor_flags_la: FlagSet<8>,
    pub master_flags_la: FlagSet<8>,
    pub home_tracker: Option<u64>,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stat_level: u8,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub current_hp: u16,
    #[cfg_attr(feature = "randomize", randomize(skip))]
    pub stats: Stats16Le,
}

impl Pa8 {
    pub fn from_buffer<S: AsRef<[u8]>>(buf: &Pa8Buffer<S>) -> Result<Self> {
        let home_tracker_raw = buf.home_tracker_raw();
        let mut mon = Pa8 {
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
            is_alpha: buf.is_alpha(),
            is_noble: buf.is_noble(),
            markings: buf.markings(),
            personality_value: buf.personality_value(),
            nature: buf.nature()?,
            mint_nature: buf.mint_nature()?,
            is_fateful_encounter: buf.is_fateful_encounter(),
            flag2_la: buf.flag2_la(),
            gender: buf.gender(),
            evs: buf.evs(),
            contest: buf.contest(),
            pokerus: buf.pokerus(),
            ribbons: buf.ribbons(),
            contest_memory_count: buf.contest_memory_count(),
            battle_memory_count: buf.battle_memory_count(),
            alpha_move: buf.alpha_move(),
            sociability: buf.sociability(),
            height_scalar: buf.height_scalar(),
            weight_scalar: buf.weight_scalar(),
            scale: buf.scale(),
            nickname: buf.nickname(),
            moves: buf.move_slots(),
            ivs: buf.ivs(),
            is_egg: buf.is_egg(),
            is_nicknamed: buf.is_nicknamed(),
            dynamax_level: buf.dynamax_level(),
            status_condition: buf.status_condition(),
            gvs: buf.gvs(),
            handler_name: buf.handler_name(),
            handler_gender: buf.handler_gender(),
            handler_language: buf.handler_language().ok(),
            is_current_handler: buf.is_current_handler(),
            handler_id: buf.handler_id(),
            handler_friendship: buf.handler_friendship(),
            fullness: buf.fullness(),
            enjoyment: buf.enjoyment(),
            game_of_origin: buf.game_of_origin(),
            game_of_origin_battle: buf.game_of_origin_battle(),
            language: buf.language()?,
            form_argument: buf.form_argument(),
            affixed_ribbon: ModernRibbon::from_affixed_byte(buf.affixed_ribbon_raw()),
            trainer_name: buf.trainer_name(),
            trainer_friendship: buf.trainer_friendship(),
            egg_date: buf.egg_date(),
            met_date: buf.met_date(),
            egg_location_index: buf.egg_location_index(),
            met_location_index: buf.met_location_index(),
            ball: buf.ball(),
            met_level: buf.met_level(),
            trainer_gender: buf.trainer_gender(),
            move_flags_la: FlagSet::from_bytes(buf.move_flags_la_raw()),
            tutor_flags_la: FlagSet::from_bytes(buf.tutor_flags_la_raw()),
            master_flags_la: FlagSet::from_bytes(buf.master_flags_la_raw()),
            home_tracker: if home_tracker_raw != 0 {
                Some(home_tracker_raw)
            } else {
                None
            },
            current_hp: buf.current_hp(),
            ..Default::default()
        };

        mon.stat_level = mon.calculate_level();
        mon.stats = mon.calculate_stats();
        Ok(mon)
    }

    pub fn write_to_box_buffer(&self, buf: &mut Pa8BufferMut) {
        buf.set_encryption_constant(self.encryption_constant);
        buf.reset_sanity();
        buf.set_species_and_form(self.species_and_form.into_inner());
        buf.set_held_item_index(self.held_item_index);
        buf.set_trainer_id(self.trainer_id);
        buf.set_secret_id(self.secret_id);
        buf.set_exp(self.exp);
        buf.set_ability_index(self.ability_index);
        buf.set_ability_num(self.ability_num);
        buf.set_is_favorite(self.is_favorite);
        buf.set_can_gigantamax(self.can_gigantamax);
        buf.set_is_alpha(self.is_alpha);
        buf.set_is_noble(self.is_noble);
        buf.set_markings(self.markings);
        buf.set_personality_value(self.personality_value);
        buf.set_nature(self.nature);
        buf.set_mint_nature(self.mint_nature);
        buf.set_is_fateful_encounter(self.is_fateful_encounter);
        buf.set_flag2_la(self.flag2_la);
        buf.set_gender(self.gender);
        buf.set_evs(self.evs);
        buf.set_contest(self.contest);
        buf.set_pokerus(self.pokerus);
        buf.set_ribbons(self.ribbons);
        buf.set_contest_memory_count(self.contest_memory_count);
        buf.set_battle_memory_count(self.battle_memory_count);
        buf.set_alpha_move(self.alpha_move);
        buf.set_sociability(self.sociability);
        buf.set_height_scalar(self.height_scalar);
        buf.set_weight_scalar(self.weight_scalar);
        buf.set_scale(self.scale);
        buf.set_nickname(&self.nickname);
        buf.set_move_slots(&self.moves);
        buf.set_ivs(&self.ivs);
        buf.set_is_egg(self.is_egg);
        buf.set_is_nicknamed(self.is_nicknamed);
        buf.set_dynamax_level(self.dynamax_level);
        buf.set_status_condition(self.status_condition);
        buf.set_gvs(self.gvs);
        buf.set_handler_name(&self.handler_name);
        buf.set_handler_gender(self.handler_gender);
        if let Some(lang) = self.handler_language {
            buf.set_handler_language(lang);
        }
        buf.set_is_current_handler(self.is_current_handler);
        buf.set_handler_id(self.handler_id);
        buf.set_handler_friendship(self.handler_friendship);
        buf.set_fullness(self.fullness);
        buf.set_enjoyment(self.enjoyment);
        buf.set_game_of_origin(self.game_of_origin);
        buf.set_game_of_origin_battle(self.game_of_origin_battle);
        buf.set_language(self.language);
        buf.set_form_argument(self.form_argument);
        buf.set_affixed_ribbon_raw(ModernRibbon::to_affixed_byte(self.affixed_ribbon));
        buf.set_trainer_name(&self.trainer_name);
        buf.set_trainer_friendship(self.trainer_friendship);
        buf.set_egg_date(self.egg_date);
        buf.set_met_date(self.met_date);
        buf.set_egg_location_index(self.egg_location_index);
        buf.set_met_location_index(self.met_location_index);
        buf.set_ball(self.ball);
        buf.set_met_level(self.met_level);
        buf.set_trainer_gender(self.trainer_gender);
        buf.set_move_flags_la(&self.move_flags_la.to_bytes());
        buf.set_tutor_flags_la(&self.tutor_flags_la.to_bytes());
        buf.set_master_flags_la(&self.master_flags_la.to_bytes());
        buf.set_home_tracker_raw(self.home_tracker.unwrap_or(0));
        buf.set_current_hp(self.current_hp);

        buf.refresh_checksum();
    }

    pub fn try_from_bytes(bytes: &[u8]) -> Result<Self> {
        match bytes.len() {
            Self::BOX_SIZE => Self::from_buffer(&Pa8Buffer::new(bytes)),
            // PKHeX .pa8 exports are party size (0x178); the box record is the
            // leading 0x168 bytes, the rest is recomputed party stats.
            Self::PARTY_SIZE => Self::from_buffer(&Pa8Buffer::new(&bytes[..Self::BOX_SIZE])),
            n => Err(Error::buffer_size(Self::BOX_SIZE, n)),
        }
    }

    pub fn from_encrypted_bytes(mut bytes: Box<[u8]>) -> Result<Self> {
        Self::from_buffer(Pa8Buffer::new_mut(&mut bytes).decrypted())
    }

    pub fn calculate_checksum(&self) -> u16 {
        let mut bytes = [0u8; Self::BOX_SIZE];
        self.write_box_bytes(&mut bytes);
        Pa8Buffer::new(&bytes).calculate_checksum()
    }

    pub fn refresh_checksum(&mut self) {
        self.checksum = self.calculate_checksum();
    }

    pub fn calculate_stats(&self) -> Stats16Le {
        helpers::calculate_stats_modern(
            MetadataSource::LegendsArceus,
            self.species_and_form.into_inner(),
            &self.ivs,
            &self.evs,
            self.calculate_level(),
            self.mint_nature.get_metadata(),
            None,
        )
        .unwrap_or_default()
    }

    pub fn is_empty_slot(bytes: &[u8]) -> bool {
        let mut owned = bytes.to_owned();
        let mut buffer = Pa8Buffer::new_mut(&mut owned);
        buffer.decrypt();
        buffer.species_ndex() == 0
    }
}

impl PkmBytes for Pa8 {
    const BOX_SIZE: usize = 360;
    const PARTY_SIZE: usize = 376;

    fn from_bytes(bytes: &[u8]) -> Result<Self> {
        Self::try_from_bytes(bytes)
    }

    fn write_box_bytes(&self, bytes: &mut [u8]) {
        self.write_to_box_buffer(&mut Pa8BufferMut::new_mut(bytes))
    }

    fn to_box_bytes(&self) -> Box<[u8]> {
        let mut bytes = Box::new([0u8; Self::BOX_SIZE]);
        self.write_box_bytes(bytes.as_mut_slice());
        bytes
    }
}

impl HasSpeciesAndForm for Pa8 {
    fn get_species_metadata(&self) -> &'static SpeciesMetadata {
        self.species_and_form.into_inner().get_species_metadata()
    }
    fn get_forme_metadata(&self) -> &'static FormMetadata {
        self.species_and_form.into_inner().get_forme_metadata()
    }
    fn calculate_level(&self) -> u8 {
        self.get_species_metadata()
            .level_up_type
            .calculate_level(self.exp)
    }
}

impl ModernEvs for Pa8 {
    fn get_evs(&self) -> Stats8 {
        self.evs
    }
}

#[cfg(test)]
impl crate::tests::PkhexJson for Pa8 {
    fn to_pkhex_json_value(&self) -> core::result::Result<serde_json::Value, serde_json::Error> {
        let mut value = serde_json::to_value(self)?;
        value["level"] = serde_json::json!(self.calculate_level());
        Ok(value)
    }
}
