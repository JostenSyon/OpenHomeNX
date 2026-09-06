mod pk1;

pub use pk1::Pk1;

pub(crate) const BOX_SIZE: usize = 33;
pub(crate) const PARTY_SIZE: usize = 66;

use pkm_rs_resources::levelup::LearnsetFileReader;
use pkm_rs_resources::moves::MoveIndex;
use pkm_rs_resources::species::SpeciesForm;
use pkm_rs_types::{Generation, NationalDex};

// PKHeX Red/Blue level-up learnset table, indexed by national dex
// (verified: entry 0 is empty, entry 1 is Bulbasaur, entry 25 is Pikachu).
// NOTE: the table omits level-1 starting moves (Bulbasaur's entry starts at
// Leech Seed L7), so "base moves" below is an approximation, documented.
static RB_LEARNSETS: LearnsetFileReader = LearnsetFileReader::from_pkl_bytes(
    include_bytes!("../../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_rb.pkl"),
);

/// True if this species exists in Gen 1 (Red/Blue metadata): the dex-cut
/// gate for downgrades. Same data PKHeX/HOME enforce (not a hand range).
pub fn species_legal_in_gen1(ndex: u16) -> bool {
    pkm_rs_resources::species::form_metadata::source_has_form_metadata(
        pkm_rs_resources::metadata_source::MetadataSource::RedBlue,
        ndex,
        0,
    )
}

/// Level for this species at the given EXP (upstream pattern, same as
/// Pk3/Pk8 `calculate_level`). from_ohpkm writes level 0; our FFI layer
/// restores the real level with this instead of inventing a growth table.
pub fn level_for_exp(ndex: u16, exp: u32) -> u8 {
    SpeciesForm::base_form(NationalDex::assert_valid(ndex))
        .get_species_metadata()
        .level_up_type
        .calculate_level(exp)
}

/// True if this move id may appear on a Gen 1 record: empty slot or a move
/// introduced in Gen 1. Anything else would silently become another move
/// through the `as u8` cast in `Pk1::from_ohpkm`, so our FFI layer drops it
/// explicitly instead (upstream core itself does not check).
pub fn move_legal_in_gen1(move_id: u16) -> bool {
    if move_id == 0 {
        return true;
    }
    MoveIndex::from_u16(move_id)
        .get_metadata()
        .is_some_and(|md| md.introduced <= Generation::G1)
}

/// Base moves for a Gen 1 record of this species: the head of its Red/Blue
/// learnset (up to 4), each with its base PP. Pound (id 1) when the table
/// has nothing usable (unknown species or empty entry). Used to refill a
/// record whose moves were all dropped as non-Gen-1, so it never ends up
/// moveless (a moveless mon cannot act in Gen 1).
pub fn base_moves_for_species(ndex: u16) -> ([u8; 4], [u8; 4]) {
    let mut moves = [0u8; 4];
    let mut pp = [0u8; 4];
    let mut filled = 0;
    if ndex != 0 {
        if let Some(reader) = RB_LEARNSETS.learnset_at_index(ndex) {
            for m in reader.all_moves() {
                if filled >= 4 {
                    break;
                }
                let id = m.move_id_raw();
                if id == 0 || id > 165 {
                    continue;
                }
                moves[filled] = id as u8;
                pp[filled] = MoveIndex::from_u16(id)
                    .get_metadata()
                    .map(|md| md.pp)
                    .unwrap_or(0);
                filled += 1;
            }
        }
    }
    if filled == 0 {
        // Universal Gen 1 fallback (Pound's own base PP from metadata).
        moves[0] = 1;
        pp[0] = MoveIndex::from_u16(1)
            .get_metadata()
            .map(|md| md.pp)
            .unwrap_or(35);
    }
    (moves, pp)
}
