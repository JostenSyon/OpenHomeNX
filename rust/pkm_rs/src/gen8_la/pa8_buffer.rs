#[cfg(not(feature = "std"))] use alloc::boxed::Box;
use super::{Pa8AbilityIndex, Pa8SpeciesAndForm};
use crate::bytes::{AsBytes, AsBytesMut};
use crate::checksum::{Checksum, ChecksumU16Le, RefreshChecksum};
use crate::encryption::BlockCrypto;
use crate::result::{Error, Result};
use crate::util;
use arbitrary_int::{u3, u7};
use pkm_rs_resources::abilities::AbilityIndexWasm;
use pkm_rs_resources::ball::Ball;
use pkm_rs_resources::moves::{MoveDataOffsets, MoveSlots, PpUpStorage};
use pkm_rs_resources::natures::NatureIndex;
use pkm_rs_resources::ribbons::ModernRibbonSet;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_types::strings::SizedUtf16String;
use pkm_rs_types::{
    AbilityNumber, BinaryGender, ContestStats, Gender, Ivs, Language,
    MarkingsSixShapesColors, OriginGame, PokeDate, Pokerus, Stats8, Stats16Le, read_u64_le,
};
use pkm_rs_types::{read_u16_le, read_u32_le};

const CHECKSUM_OFFSET: usize = 6;

// PA8 packs its four moves at 0x54 (no relearn moves in Legends: Arceus), so
// the move-data offsets differ from PK8's.
const MOVE_DATA_OFFSETS: MoveDataOffsets = MoveDataOffsets {
    moves: 0x54,
    pp: 0x5C,
    pp_ups: 0x86, // PPUps present in the box layout even if the game never sets them
};

// Byte offsets, transcribed from OpenHome's PA8.ts and PKHeX.Core PA8.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Offset {
    EncryptionConstant = 0x00,
    Sanity = 0x04,
    Checksum = 0x06,
    NationalDex = 0x08,
    HeldItem = 0x0A,
    TrainerId = 0x0C,
    SecretId = 0x0E,
    Exp = 0x10,
    AbilityIndex = 0x14,
    // bits 0-2 ability number, bit3 favorite, bit4 canGigantamax,
    // bit5 isAlpha, bit6 isNoble
    AbilityNumFlags = 0x16,
    Markings = 0x18,
    PersonalityValue = 0x1C,
    Nature = 0x20,
    MintNature = 0x21,
    // bit0 fateful, bit1 flag2LA, bits2-3 gender
    FatefulFlag2Gender = 0x22,
    FormNum = 0x24,
    Evs = 0x26,
    Contest = 0x2C,
    Pokerus = 0x32,
    RibbonsA = 0x34,
    RibbonsB = 0x40,
    ContestMemoryCount = 0x3C,
    BattleMemoryCount = 0x3D,
    AlphaMove = 0x3E,
    Sociability = 0x48,
    HeightScalar = 0x50,
    WeightScalar = 0x51,
    Scale = 0x52,
    Nickname = 0x60,
    CurrentHp = 0x92,
    IvsEggNicknamed = 0x94,
    DynamaxLevel = 0x98,
    StatusCondition = 0x9C,
    Gvs = 0xA4,
    HandlerName = 0xB8,
    HandlerGender = 0xD2,
    HandlerLanguage = 0xD3,
    IsCurrentHandler = 0xD4,
    HandlerId = 0xD6,
    HandlerFriendship = 0xD8,
    Fullness = 0xEC,
    Enjoyment = 0xED,
    GameOfOrigin = 0xEE,
    GameOfOriginBattle = 0xEF,
    Language = 0xF2,
    FormArgument = 0xF4,
    AffixedRibbon = 0xF8,
    TrainerName = 0x110,
    TrainerFriendship = 0x12A,
    EggDate = 0x131,
    MetDate = 0x134,
    Ball = 0x137,
    EggLocation = 0x138,
    MetLocation = 0x13A,
    MetLevelTrainerGender = 0x13D,
    MoveFlagsLa = 0x13F,   // 14 bytes
    HomeTracker = 0x14D,   // u64
    TutorFlagsLa = 0x155,  // 8 bytes
    MasterFlagsLa = 0x15D, // 8 bytes
    // Party stats live only in the 0x178 party record (outside this 0x168 box
    // buffer) and are recomputed by the game, so PA8 does not serialise them
    // into the box.
}

impl From<Offset> for usize {
    fn from(offset: Offset) -> usize {
        offset as usize
    }
}

pub type Pa8BufferMut<'a> = Pa8Buffer<&'a mut [u8]>;

#[derive(Default, Clone, Copy)]
pub struct Pa8Buffer<S: AsRef<[u8]>>(S);

impl<'a> Pa8Buffer<&'a [u8]> {
    pub fn new(span: &'a [u8]) -> Self {
        assert_eq!(span.len(), super::PKM_DATA_SIZE);
        Self(span)
    }
}

impl<'a> Pa8Buffer<&'a mut [u8]> {
    pub fn new_mut(span: &'a mut [u8]) -> Self {
        assert_eq!(span.len(), super::PKM_DATA_SIZE);
        Self(span)
    }
}

// ------------------------------------------------------------------
// Primitive accessors
// ------------------------------------------------------------------

impl<S: AsRef<[u8]>> Pa8Buffer<S> {
    fn get_u8(&self, offset: Offset) -> u8 {
        self.bytes()[offset as usize]
    }
    fn get_u16_le(&self, offset: Offset) -> u16 {
        read_u16_le!(self.bytes(), offset as usize)
    }
    fn get_u32_le(&self, offset: Offset) -> u32 {
        read_u32_le!(self.bytes(), offset as usize)
    }
    fn get_u64_le(&self, offset: Offset) -> u64 {
        read_u64_le!(self.bytes(), offset as usize)
    }
    fn get_flag(&self, offset: Offset, bit_index: usize) -> bool {
        util::get_flag(self.bytes(), offset as usize, bit_index)
    }
    fn get_array<const N: usize>(&self, offset: Offset) -> [u8; N] {
        let o = offset as usize;
        self.bytes()[o..o + N].try_into().unwrap()
    }
}

impl<S: AsRef<[u8]> + AsMut<[u8]>> Pa8Buffer<S> {
    fn set_u8(&mut self, offset: Offset, v: u8) {
        self.bytes_mut()[offset as usize] = v;
    }
    fn set_u16_le(&mut self, offset: Offset, v: u16) {
        let o = offset as usize;
        self.bytes_mut()[o..o + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn set_u32_le(&mut self, offset: Offset, v: u32) {
        let o = offset as usize;
        self.bytes_mut()[o..o + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn set_u64_le(&mut self, offset: Offset, v: u64) {
        let o = offset as usize;
        self.bytes_mut()[o..o + 8].copy_from_slice(&v.to_le_bytes());
    }
    fn set_flag(&mut self, offset: Offset, bit_index: usize, v: bool) {
        util::set_flag(self.bytes_mut(), offset as usize, bit_index, v);
    }
    fn set_array<const N: usize>(&mut self, offset: Offset, v: &[u8; N]) {
        let o = offset as usize;
        self.bytes_mut()[o..o + N].copy_from_slice(v);
    }
}

// ------------------------------------------------------------------
// Getters
// ------------------------------------------------------------------

impl<S: AsRef<[u8]>> Pa8Buffer<S> {
    fn bytes(&self) -> &[u8] {
        self.0.as_ref()
    }

    pub fn encryption_constant(&self) -> u32 {
        self.get_u32_le(Offset::EncryptionConstant)
    }
    pub fn sanity(&self) -> u16 {
        self.get_u16_le(Offset::Sanity)
    }
    pub fn checksum(&self) -> u16 {
        self.get_u16_le(Offset::Checksum)
    }
    pub fn species_ndex(&self) -> u16 {
        self.get_u16_le(Offset::NationalDex)
    }
    pub fn form_num(&self) -> u16 {
        self.get_u16_le(Offset::FormNum)
    }
    pub fn species_and_form(&self) -> Result<Pa8SpeciesAndForm> {
        let sf = SpeciesForm::new(self.species_ndex(), self.form_num())?;
        Pa8SpeciesAndForm::try_new(sf).ok_or(Error::form_index(sf))
    }
    pub fn held_item_index(&self) -> u16 {
        self.get_u16_le(Offset::HeldItem)
    }
    pub fn trainer_id(&self) -> u16 {
        self.get_u16_le(Offset::TrainerId)
    }
    pub fn secret_id(&self) -> u16 {
        self.get_u16_le(Offset::SecretId)
    }
    pub fn exp(&self) -> u32 {
        self.get_u32_le(Offset::Exp)
    }
    pub fn ability_index_raw(&self) -> u16 {
        self.get_u16_le(Offset::AbilityIndex)
    }
    pub fn ability_index(&self) -> Result<AbilityIndexWasm> {
        Ok(AbilityIndexWasm::try_from(self.ability_index_raw())?)
    }
    fn byte_16(&self) -> u8 {
        self.get_u8(Offset::AbilityNumFlags)
    }
    pub fn ability_num(&self) -> Result<AbilityNumber> {
        Ok(u3::extract_u8(self.byte_16(), 0).try_into()?)
    }
    pub fn is_favorite(&self) -> bool {
        self.get_flag(Offset::AbilityNumFlags, 3)
    }
    pub fn can_gigantamax(&self) -> bool {
        self.get_flag(Offset::AbilityNumFlags, 4)
    }
    pub fn is_alpha(&self) -> bool {
        self.get_flag(Offset::AbilityNumFlags, 5)
    }
    pub fn is_noble(&self) -> bool {
        self.get_flag(Offset::AbilityNumFlags, 6)
    }
    pub fn markings_raw(&self) -> [u8; 2] {
        self.get_array(Offset::Markings)
    }
    pub fn markings(&self) -> MarkingsSixShapesColors {
        MarkingsSixShapesColors::from_bytes(self.markings_raw())
    }
    pub fn personality_value(&self) -> u32 {
        self.get_u32_le(Offset::PersonalityValue)
    }
    pub fn nature(&self) -> Result<NatureIndex> {
        Ok(NatureIndex::try_from(self.get_u8(Offset::Nature))?)
    }
    pub fn mint_nature(&self) -> Result<NatureIndex> {
        Ok(NatureIndex::try_from(self.get_u8(Offset::MintNature))?)
    }
    pub fn is_fateful_encounter(&self) -> bool {
        self.get_flag(Offset::FatefulFlag2Gender, 0)
    }
    pub fn flag2_la(&self) -> bool {
        self.get_flag(Offset::FatefulFlag2Gender, 1)
    }
    pub fn gender(&self) -> Gender {
        Gender::from_bits_2_3(self.get_u8(Offset::FatefulFlag2Gender))
    }
    pub fn evs(&self) -> Stats8 {
        Stats8::from_bytes(self.get_array(Offset::Evs))
    }
    pub fn contest(&self) -> ContestStats {
        ContestStats::from_bytes(self.get_array(Offset::Contest))
    }
    pub fn pokerus(&self) -> Pokerus {
        Pokerus::from_byte(self.get_u8(Offset::Pokerus))
    }
    pub fn ribbons_a_raw(&self) -> [u8; 8] {
        self.get_array(Offset::RibbonsA)
    }
    pub fn ribbons_b_raw(&self) -> [u8; 6] {
        self.get_array(Offset::RibbonsB)
    }
    pub fn ribbons(&self) -> ModernRibbonSet<14, { super::MAX_RIBBON_LA }> {
        let mut combined = [0u8; 14];
        combined[..8].copy_from_slice(&self.ribbons_a_raw());
        combined[8..14].copy_from_slice(&self.ribbons_b_raw());
        ModernRibbonSet::from_bytes(combined)
    }
    pub fn contest_memory_count(&self) -> u8 {
        self.get_u8(Offset::ContestMemoryCount)
    }
    pub fn battle_memory_count(&self) -> u8 {
        self.get_u8(Offset::BattleMemoryCount)
    }
    pub fn alpha_move(&self) -> u16 {
        self.get_u16_le(Offset::AlphaMove)
    }
    pub fn sociability(&self) -> u32 {
        self.get_u32_le(Offset::Sociability)
    }
    pub fn height_scalar(&self) -> u8 {
        self.get_u8(Offset::HeightScalar)
    }
    pub fn weight_scalar(&self) -> u8 {
        self.get_u8(Offset::WeightScalar)
    }
    pub fn scale(&self) -> u8 {
        self.get_u8(Offset::Scale)
    }
    pub fn nickname_raw(&self) -> [u8; 26] {
        self.get_array(Offset::Nickname)
    }
    pub fn nickname(&self) -> SizedUtf16String<26> {
        SizedUtf16String::<26>::from_bytes(self.nickname_raw())
    }
    pub fn move_slots(&self) -> MoveSlots {
        MoveSlots::from_bytes(self.bytes(), MOVE_DATA_OFFSETS, PpUpStorage::FourBytes)
    }
    fn ivs_egg_nicknamed_raw(&self) -> [u8; 4] {
        self.get_array(Offset::IvsEggNicknamed)
    }
    pub fn ivs(&self) -> Ivs {
        Ivs::from_30_bits(self.ivs_egg_nicknamed_raw())
    }
    pub fn is_egg(&self) -> bool {
        self.get_flag(Offset::IvsEggNicknamed, 30)
    }
    pub fn is_nicknamed(&self) -> bool {
        self.get_flag(Offset::IvsEggNicknamed, 31)
    }
    pub fn dynamax_level(&self) -> u8 {
        self.get_u8(Offset::DynamaxLevel)
    }
    pub fn status_condition(&self) -> u32 {
        self.get_u32_le(Offset::StatusCondition)
    }
    pub fn gvs(&self) -> Stats8 {
        Stats8::from_bytes(self.get_array(Offset::Gvs))
    }
    pub fn handler_name(&self) -> SizedUtf16String<26> {
        SizedUtf16String::<26>::from_bytes(self.get_array(Offset::HandlerName))
    }
    pub fn handler_gender(&self) -> BinaryGender {
        self.get_flag(Offset::HandlerGender, 0).into()
    }
    pub fn handler_language(&self) -> Result<Language> {
        Ok(Language::try_from(self.get_u8(Offset::HandlerLanguage))?)
    }
    pub fn is_current_handler(&self) -> bool {
        self.get_flag(Offset::IsCurrentHandler, 0)
    }
    pub fn handler_id(&self) -> u16 {
        self.get_u16_le(Offset::HandlerId)
    }
    pub fn handler_friendship(&self) -> u8 {
        self.get_u8(Offset::HandlerFriendship)
    }
    pub fn fullness(&self) -> u8 {
        self.get_u8(Offset::Fullness)
    }
    pub fn enjoyment(&self) -> u8 {
        self.get_u8(Offset::Enjoyment)
    }
    pub fn game_of_origin(&self) -> OriginGame {
        OriginGame::from(self.get_u8(Offset::GameOfOrigin))
    }
    pub fn game_of_origin_battle(&self) -> Option<OriginGame> {
        OriginGame::try_from_u8(self.get_u8(Offset::GameOfOriginBattle))
    }
    pub fn language(&self) -> Result<Language> {
        Ok(Language::try_from(self.get_u8(Offset::Language))?)
    }
    pub fn form_argument(&self) -> u32 {
        self.get_u32_le(Offset::FormArgument)
    }
    pub fn affixed_ribbon_raw(&self) -> u8 {
        self.get_u8(Offset::AffixedRibbon)
    }
    pub fn trainer_name(&self) -> SizedUtf16String<26> {
        SizedUtf16String::<26>::from_bytes(self.get_array(Offset::TrainerName))
    }
    pub fn trainer_friendship(&self) -> u8 {
        self.get_u8(Offset::TrainerFriendship)
    }
    pub fn egg_date(&self) -> Option<PokeDate> {
        PokeDate::from_bytes_optional(self.get_array(Offset::EggDate))
    }
    pub fn met_date(&self) -> PokeDate {
        PokeDate::from_bytes(self.get_array(Offset::MetDate))
    }
    pub fn egg_location_index(&self) -> u16 {
        self.get_u16_le(Offset::EggLocation)
    }
    pub fn met_location_index(&self) -> u16 {
        self.get_u16_le(Offset::MetLocation)
    }
    pub fn ball(&self) -> Ball {
        Ball::from(self.get_u8(Offset::Ball))
    }
    pub fn met_level(&self) -> u8 {
        u7::extract_u8(self.get_u8(Offset::MetLevelTrainerGender), 0).into()
    }
    pub fn trainer_gender(&self) -> BinaryGender {
        self.get_flag(Offset::MetLevelTrainerGender, 7).into()
    }
    pub fn move_flags_la_raw(&self) -> [u8; 14] {
        self.get_array(Offset::MoveFlagsLa)
    }
    pub fn tutor_flags_la_raw(&self) -> [u8; 8] {
        self.get_array(Offset::TutorFlagsLa)
    }
    pub fn master_flags_la_raw(&self) -> [u8; 8] {
        self.get_array(Offset::MasterFlagsLa)
    }
    pub fn home_tracker_raw(&self) -> u64 {
        self.get_u64_le(Offset::HomeTracker)
    }
    pub fn current_hp(&self) -> u16 {
        self.get_u16_le(Offset::CurrentHp)
    }

    fn block_crypto(&self) -> BlockCrypto {
        BlockCrypto::gen89(self.encryption_constant())
    }
    pub fn encrypted_copy(&self) -> Box<[u8]> {
        self.block_crypto().to_encrypted_bytes(self.0.as_ref())
    }
    pub fn decrypted_copy(&self) -> Box<[u8]> {
        self.block_crypto().to_decrypted_bytes(self.0.as_ref())
    }
}

// ------------------------------------------------------------------
// Setters
// ------------------------------------------------------------------

impl<S: AsRef<[u8]> + AsMut<[u8]>> Pa8Buffer<S> {
    fn bytes_mut(&mut self) -> &mut [u8] {
        self.0.as_mut()
    }

    pub fn set_encryption_constant(&mut self, v: u32) {
        self.set_u32_le(Offset::EncryptionConstant, v);
    }
    pub fn reset_sanity(&mut self) {
        self.set_u16_le(Offset::Sanity, 0);
    }
    pub fn set_checksum(&mut self, v: u16) {
        self.set_u16_le(Offset::Checksum, v);
    }
    pub fn set_species_and_form(&mut self, v: SpeciesForm) {
        self.set_u16_le(Offset::NationalDex, v.get_ndex() as u16);
        self.set_u16_le(Offset::FormNum, v.get_forme_index());
    }
    pub fn set_held_item_index(&mut self, v: u16) {
        self.set_u16_le(Offset::HeldItem, v);
    }
    pub fn set_trainer_id(&mut self, v: u16) {
        self.set_u16_le(Offset::TrainerId, v);
    }
    pub fn set_secret_id(&mut self, v: u16) {
        self.set_u16_le(Offset::SecretId, v);
    }
    pub fn set_exp(&mut self, v: u32) {
        self.set_u32_le(Offset::Exp, v);
    }
    pub fn set_ability_index(&mut self, v: Pa8AbilityIndex) {
        self.set_u16_le(Offset::AbilityIndex, u16::from(v));
    }
    pub fn set_ability_num(&mut self, v: AbilityNumber) {
        let masked = self.byte_16() & !0b111;
        self.set_u8(Offset::AbilityNumFlags, masked | v.to_byte());
    }
    pub fn set_is_favorite(&mut self, v: bool) {
        self.set_flag(Offset::AbilityNumFlags, 3, v);
    }
    pub fn set_can_gigantamax(&mut self, v: bool) {
        self.set_flag(Offset::AbilityNumFlags, 4, v);
    }
    pub fn set_is_alpha(&mut self, v: bool) {
        self.set_flag(Offset::AbilityNumFlags, 5, v);
    }
    pub fn set_is_noble(&mut self, v: bool) {
        self.set_flag(Offset::AbilityNumFlags, 6, v);
    }
    pub fn set_markings(&mut self, v: MarkingsSixShapesColors) {
        self.set_array(Offset::Markings, &v.to_bytes());
    }
    pub fn set_personality_value(&mut self, v: u32) {
        self.set_u32_le(Offset::PersonalityValue, v);
    }
    pub fn set_nature(&mut self, v: NatureIndex) {
        self.set_u8(Offset::Nature, v.to_byte());
    }
    pub fn set_mint_nature(&mut self, v: NatureIndex) {
        self.set_u8(Offset::MintNature, v.to_byte());
    }
    pub fn set_is_fateful_encounter(&mut self, v: bool) {
        self.set_flag(Offset::FatefulFlag2Gender, 0, v);
    }
    pub fn set_flag2_la(&mut self, v: bool) {
        self.set_flag(Offset::FatefulFlag2Gender, 1, v);
    }
    pub fn set_gender(&mut self, v: Gender) {
        v.set_bits_2_3(&mut self.bytes_mut()[Offset::FatefulFlag2Gender as usize]);
    }
    pub fn set_evs(&mut self, v: Stats8) {
        self.set_array(Offset::Evs, &v.to_bytes());
    }
    pub fn set_contest(&mut self, v: ContestStats) {
        self.set_array(Offset::Contest, &v.to_bytes());
    }
    pub fn set_pokerus(&mut self, v: Pokerus) {
        self.set_u8(Offset::Pokerus, v.to_byte());
    }
    pub fn set_ribbons(&mut self, v: ModernRibbonSet<14, { super::MAX_RIBBON_LA }>) {
        let bytes = v.to_bytes();
        let mut a = [0u8; 8];
        let mut b = [0u8; 6];
        a.copy_from_slice(&bytes[..8]);
        b.copy_from_slice(&bytes[8..14]);
        self.set_array(Offset::RibbonsA, &a);
        self.set_array(Offset::RibbonsB, &b);
    }
    pub fn set_contest_memory_count(&mut self, v: u8) {
        self.set_u8(Offset::ContestMemoryCount, v);
    }
    pub fn set_battle_memory_count(&mut self, v: u8) {
        self.set_u8(Offset::BattleMemoryCount, v);
    }
    pub fn set_alpha_move(&mut self, v: u16) {
        self.set_u16_le(Offset::AlphaMove, v);
    }
    pub fn set_sociability(&mut self, v: u32) {
        self.set_u32_le(Offset::Sociability, v);
    }
    pub fn set_height_scalar(&mut self, v: u8) {
        self.set_u8(Offset::HeightScalar, v);
    }
    pub fn set_weight_scalar(&mut self, v: u8) {
        self.set_u8(Offset::WeightScalar, v);
    }
    pub fn set_scale(&mut self, v: u8) {
        self.set_u8(Offset::Scale, v);
    }
    pub fn set_nickname(&mut self, v: &SizedUtf16String<26>) {
        self.set_array(Offset::Nickname, &v.bytes());
    }
    pub fn set_move_slots(&mut self, v: &MoveSlots) {
        v.write_spans(self.bytes_mut(), MOVE_DATA_OFFSETS, PpUpStorage::FourBytes);
    }
    pub fn set_ivs(&mut self, v: &Ivs) {
        v.write_30_bits(self.bytes_mut(), Offset::IvsEggNicknamed as usize);
    }
    pub fn set_is_egg(&mut self, v: bool) {
        self.set_flag(Offset::IvsEggNicknamed, 30, v);
    }
    pub fn set_is_nicknamed(&mut self, v: bool) {
        self.set_flag(Offset::IvsEggNicknamed, 31, v);
    }
    pub fn set_dynamax_level(&mut self, v: u8) {
        self.set_u8(Offset::DynamaxLevel, v);
    }
    pub fn set_status_condition(&mut self, v: u32) {
        self.set_u32_le(Offset::StatusCondition, v);
    }
    pub fn set_gvs(&mut self, v: Stats8) {
        self.set_array(Offset::Gvs, &v.to_bytes());
    }
    pub fn set_handler_name(&mut self, v: &SizedUtf16String<26>) {
        self.set_array(Offset::HandlerName, &v.bytes());
    }
    pub fn set_handler_gender(&mut self, v: BinaryGender) {
        self.set_flag(Offset::HandlerGender, 0, v.into());
    }
    pub fn set_handler_language(&mut self, v: Language) {
        self.set_u8(Offset::HandlerLanguage, v as u8);
    }
    pub fn set_is_current_handler(&mut self, v: bool) {
        self.set_flag(Offset::IsCurrentHandler, 0, v);
    }
    pub fn set_handler_id(&mut self, v: u16) {
        self.set_u16_le(Offset::HandlerId, v);
    }
    pub fn set_handler_friendship(&mut self, v: u8) {
        self.set_u8(Offset::HandlerFriendship, v);
    }
    pub fn set_fullness(&mut self, v: u8) {
        self.set_u8(Offset::Fullness, v);
    }
    pub fn set_enjoyment(&mut self, v: u8) {
        self.set_u8(Offset::Enjoyment, v);
    }
    pub fn set_game_of_origin(&mut self, v: OriginGame) {
        self.set_u8(Offset::GameOfOrigin, v as u8);
    }
    pub fn set_game_of_origin_battle(&mut self, v: Option<OriginGame>) {
        self.set_u8(Offset::GameOfOriginBattle, v.map(|g| g as u8).unwrap_or(0));
    }
    pub fn set_language(&mut self, v: Language) {
        self.set_u8(Offset::Language, v as u8);
    }
    pub fn set_form_argument(&mut self, v: u32) {
        self.set_u32_le(Offset::FormArgument, v);
    }
    pub fn set_affixed_ribbon_raw(&mut self, v: u8) {
        self.set_u8(Offset::AffixedRibbon, v);
    }
    pub fn set_trainer_name(&mut self, v: &SizedUtf16String<26>) {
        self.set_array(Offset::TrainerName, &v.bytes());
    }
    pub fn set_trainer_friendship(&mut self, v: u8) {
        self.set_u8(Offset::TrainerFriendship, v);
    }
    pub fn set_egg_date(&mut self, v: Option<PokeDate>) {
        self.set_array(Offset::EggDate, &PokeDate::to_bytes_optional(v));
    }
    pub fn set_met_date(&mut self, v: PokeDate) {
        self.set_array(Offset::MetDate, &v.to_bytes());
    }
    pub fn set_egg_location_index(&mut self, v: u16) {
        self.set_u16_le(Offset::EggLocation, v);
    }
    pub fn set_met_location_index(&mut self, v: u16) {
        self.set_u16_le(Offset::MetLocation, v);
    }
    pub fn set_ball(&mut self, v: Ball) {
        self.set_u8(Offset::Ball, v as u8);
    }
    pub fn set_met_level(&mut self, v: u8) {
        self.bytes_mut()[Offset::MetLevelTrainerGender as usize] |= v & 0x7F;
    }
    pub fn set_trainer_gender(&mut self, v: BinaryGender) {
        let b: bool = v.into();
        util::set_flag(self.bytes_mut(), Offset::MetLevelTrainerGender as usize, 7, b);
    }
    pub fn set_move_flags_la(&mut self, v: &[u8; 14]) {
        self.set_array(Offset::MoveFlagsLa, v);
    }
    pub fn set_tutor_flags_la(&mut self, v: &[u8; 8]) {
        self.set_array(Offset::TutorFlagsLa, v);
    }
    pub fn set_master_flags_la(&mut self, v: &[u8; 8]) {
        self.set_array(Offset::MasterFlagsLa, v);
    }
    pub fn set_home_tracker_raw(&mut self, v: u64) {
        self.set_u64_le(Offset::HomeTracker, v);
    }
    pub fn set_current_hp(&mut self, v: u16) {
        self.set_u16_le(Offset::CurrentHp, v);
    }

    pub fn encrypt(&mut self) {
        self.block_crypto().encrypt(self.0.as_mut());
    }
    pub fn decrypt(&mut self) {
        self.block_crypto().decrypt(self.0.as_mut());
    }
    pub fn decrypted(&mut self) -> &mut Self {
        self.block_crypto().decrypt(self.0.as_mut());
        self
    }
}

impl<S: AsRef<[u8]>> AsBytes for Pa8Buffer<S> {
    fn as_bytes(&self) -> &[u8] {
        self.0.as_ref()
    }
}
impl<S: AsRef<[u8]> + AsMut<[u8]>> AsBytesMut for Pa8Buffer<S> {
    fn as_bytes_mut(&mut self) -> &mut [u8] {
        self.0.as_mut()
    }
}

// Checksum covers bytes 8 .. 0x168 (box end) for PA8.
const CHECKSUM_START: usize = 8;
const CHECKSUM_END: usize = 0x168;

impl<S: AsRef<[u8]>> Checksum for Pa8Buffer<S> {
    type A = ChecksumU16Le;
    const SPAN_START: usize = CHECKSUM_START;
    const SPAN_END: usize = CHECKSUM_END;
}
impl<S: AsRef<[u8]> + AsMut<[u8]>> RefreshChecksum for Pa8Buffer<S> {
    const STORED_OFFSET: usize = CHECKSUM_OFFSET;
}
