mod pk2;

pub use pk2::Pk2;

pub(crate) const BOX_SIZE: usize = 32;
pub(crate) const PARTY_SIZE: usize = 73;

use pkm_rs_resources::levelup::LearnsetFileReader;
use pkm_rs_resources::moves::MoveIndex;
use pkm_rs_types::Generation;

// PKHeX Gold/Silver level-up learnset table, indexed by national dex
// (same convention as the RB table: entry 0 is empty). NOTE: like RB, the
// table may omit level-1 starting moves, so "base moves" below is an
// approximation, documented.
static GS_LEARNSETS: LearnsetFileReader = LearnsetFileReader::from_pkl_bytes(
    include_bytes!("../../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_gs.pkl"),
);

/// True if this move id may appear on a Gen 2 record: empty slot or a move
/// introduced in Gen 1-2. Anything else would silently wrap through the
/// `as u8` cast in `Pk2::from_ohpkm`, so our FFI layer drops it explicitly
/// instead (upstream core itself does not check).
pub fn move_legal_in_gen2(move_id: u16) -> bool {
    if move_id == 0 {
        return true;
    }
    MoveIndex::from_u16(move_id)
        .get_metadata()
        .is_some_and(|md| md.introduced <= Generation::G2)
}

/// Base moves for a Gen 2 record of this species: the head of its G/S
/// learnset (up to 4), each with its base PP. Pound (id 1) when the table
/// has nothing usable. Used to refill a record whose moves were all dropped
/// as non-Gen-2, so it never ends up moveless.
pub fn base_moves_for_species(ndex: u16) -> ([u8; 4], [u8; 4]) {
    let mut moves = [0u8; 4];
    let mut pp = [0u8; 4];
    let mut filled = 0;
    if ndex != 0 {
        if let Some(reader) = GS_LEARNSETS.learnset_at_index(ndex) {
            for m in reader.all_moves() {
                if filled >= 4 {
                    break;
                }
                let id = m.move_id_raw();
                if id == 0 || id > 251 {
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
        // Universal fallback (Pound's own base PP from metadata).
        moves[0] = 1;
        pp[0] = MoveIndex::from_u16(1)
            .get_metadata()
            .map(|md| md.pp)
            .unwrap_or(35);
    }
    (moves, pp)
}
