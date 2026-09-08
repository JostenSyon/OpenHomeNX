mod pk3;
mod pk3_buffer;
mod pokemon_index;
// mod save;

pub use pk3::*;
pub use pokemon_index::Gen3PokemonIndex;

use pk3_buffer::Offset;
use pk3_buffer::Pk3Buffer;
use pkm_rs_resources::levelup::LearnsetFileReader;
use pkm_rs_resources::{abilities::AbilityIndexBounded, moves::MoveDataOffsets};
use pkm_rs_resources::moves::MoveIndex;
use pkm_rs_types::Generation;

pub(crate) const BOX_SIZE: usize = 80;
pub(crate) const PARTY_SIZE: usize = 100;

const MOVE_DATA_OFFSETS: MoveDataOffsets<Offset> = MoveDataOffsets {
    moves: Offset::Moves,
    pp: Offset::MovePp,
    pp_ups: Offset::MovePpUps,
};

const AIR_LOCK: u16 = 76;
pub const PK3_MAX_ABILITY: u16 = AIR_LOCK;
pub type Pk3AbilityIndex = AbilityIndexBounded<AIR_LOCK>;

// PKHeX Emerald level-up learnset table, indexed by national dex (canonical
// Gen 3 table — matches MetadataSource::Emerald used by from_ohpkm).
static E_LEARNSETS: LearnsetFileReader = LearnsetFileReader::from_pkl_bytes(
    include_bytes!("../../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_e.pkl"),
);

/// True if this move id may appear on a Gen 3 record: empty slot or a move
/// introduced in Gen 3 or earlier. Same introduced-generation pattern as
/// gen1/2/4/5/6 (upstream core itself does not check on from_ohpkm).
pub fn move_legal_in_gen3(move_id: u16) -> bool {
    if move_id == 0 {
        return true;
    }
    MoveIndex::from_u16(move_id)
        .get_metadata()
        .is_some_and(|md| md.introduced <= Generation::G3)
}

/// Base moves for a Gen 3 record of this species: the head of its Emerald
/// learnset (up to 4), each with its base PP. Pound (id 1) when the table
/// has nothing usable. Refill for a record whose moves were all dropped.
pub fn base_moves_for_species(ndex: u16) -> ([u16; 4], [u8; 4]) {
    let mut moves = [0u16; 4];
    let mut pp = [0u8; 4];
    let mut filled = 0;
    if ndex != 0 {
        if let Some(reader) = E_LEARNSETS.learnset_at_index(ndex) {
            for m in reader.all_moves() {
                if filled >= 4 {
                    break;
                }
                let id = m.move_id_raw();
                if id == 0 || !move_legal_in_gen3(id) {
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