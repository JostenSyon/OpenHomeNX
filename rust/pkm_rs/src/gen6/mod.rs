mod pk6;
pub mod crypto;

pub use pk6::*;

use pkm_rs_resources::abilities::AbilityIndexBounded;

pub(crate) const BOX_SIZE: usize = 232;
pub(crate) const PARTY_SIZE: usize = 260;

use pkm_rs_resources::levelup::LearnsetFileReader;
use pkm_rs_resources::moves::MoveIndex;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_types::{Generation, NationalDex};

// PKHeX X/Y level-up learnset table, indexed by national dex.
static XY_LEARNSETS: LearnsetFileReader = LearnsetFileReader::from_pkl_bytes(
    include_bytes!("../../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_xy.pkl"),
);

/// True if this species exists in Gen 6 (X/Y metadata): the dex-cut gate
/// for downgrades. Same data PKHeX/HOME enforce.
pub fn species_legal_in_gen6(ndex: u16) -> bool {
    pkm_rs_resources::species::form_metadata::source_has_form_metadata(
        pkm_rs_resources::metadata_source::MetadataSource::XY,
        ndex,
        0,
    )
}

/// Level for this species at the given EXP (upstream pattern, same as
/// Pk3/Pk8 `calculate_level`).
pub fn level_for_exp(ndex: u16, exp: u32) -> u8 {
    SpeciesForm::base_form(NationalDex::assert_valid(ndex))
        .get_species_metadata()
        .level_up_type
        .calculate_level(exp)
}

/// True if this move id may appear on a Gen 6 record: empty slot or a move
/// introduced in Gen 6 or earlier. Newer moves are dropped explicitly by
/// our FFI layer instead of leaking through.
pub fn move_legal_in_gen6(move_id: u16) -> bool {
    if move_id == 0 {
        return true;
    }
    MoveIndex::from_u16(move_id)
        .get_metadata()
        .is_some_and(|md| md.introduced <= Generation::G6)
}

/// Base moves for a Gen 6 record of this species: the head of its X/Y
/// learnset (up to 4), each with its base PP. Pound (id 1) when the table
/// has nothing usable. Used to refill a record whose moves were all
/// dropped, so it never ends up moveless.
pub fn base_moves_for_species(ndex: u16) -> ([u16; 4], [u8; 4]) {
    let mut moves = [0u16; 4];
    let mut pp = [0u8; 4];
    let mut filled = 0;
    if ndex != 0 {
        if let Some(reader) = XY_LEARNSETS.learnset_at_index(ndex) {
            for m in reader.all_moves() {
                if filled >= 4 {
                    break;
                }
                let id = m.move_id_raw();
                if id == 0 || !move_legal_in_gen6(id) {
                    continue;
                }
                moves[filled] = id;
                pp[filled] = MoveIndex::from_u16(id)
                    .get_metadata()
                    .map(|md| md.pp)
                    .unwrap_or(0);
                filled += 1;
            }
        }
    }
    if filled == 0 {
        moves[0] = 1;
        pp[0] = MoveIndex::from_u16(1)
            .get_metadata()
            .map(|md| md.pp)
            .unwrap_or(35);
    }
    (moves, pp)
}

const NEUROFORCE: u16 = 233;
pub type Pk6AbilityIndex = AbilityIndexBounded<NEUROFORCE>;
