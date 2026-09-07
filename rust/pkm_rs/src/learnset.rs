//! Level-up learnset tables (PKHeX `lvlmove_*.pkl`) for the move viewer.
//!
//! Each table is indexed by national dex — entry == dex was verified per
//! file (openhome_switch `learnset_tables_hold_kanto_starters`). Table ids
//! mirror the C++ `learnsetTableFor()`: 1=RB 2=Y 3=GS 4=C 5=RS 6=E 7=FR
//! 8=GG (LGPE) 9=SWSH 10=BDSP 11=LA 12=SV 13=ZA 14=DP 15=Pt 16=HGSS 17=BW
//! 18=B2W2 19=XY 20=ORAS 21=SM 22=USUM.
extern crate alloc;

use alloc::vec::Vec;
use pkm_rs_resources::levelup::{LearnsetCondition, LearnsetFileReader};

macro_rules! learnset_table {
    ($name:ident, $file:literal) => {
        static $name: LearnsetFileReader =
            LearnsetFileReader::from_pkl_bytes(include_bytes!($file));
    };
}

learnset_table!(RB, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_rb.pkl");
learnset_table!(Y, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_y.pkl");
learnset_table!(GS, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_gs.pkl");
learnset_table!(C, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_c.pkl");
learnset_table!(RS, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_rs.pkl");
learnset_table!(E, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_e.pkl");
learnset_table!(FR, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_fr.pkl");
learnset_table!(GG, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_gg.pkl");
learnset_table!(SWSH, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_swsh.pkl");
learnset_table!(BDSP, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_bdsp.pkl");
learnset_table!(LA, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_la.pkl");
learnset_table!(SV, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_sv.pkl");
learnset_table!(ZA, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_za.pkl");
learnset_table!(DP, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_dp.pkl");
learnset_table!(PT, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_pt.pkl");
learnset_table!(HGSS, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_hgss.pkl");
learnset_table!(BW, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_bw.pkl");
learnset_table!(B2W2, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_b2w2.pkl");
learnset_table!(XY, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_xy.pkl");
learnset_table!(AO, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_ao.pkl");
learnset_table!(SM, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_sm.pkl");
learnset_table!(UU, "../../pkm_rs_resources/src/pkhex_bin/levelup/lvlmove_uu.pkl");

fn table_for_id(table: u32) -> Option<&'static LearnsetFileReader> {
    match table {
        1 => Some(&RB),
        2 => Some(&Y),
        3 => Some(&GS),
        4 => Some(&C),
        5 => Some(&RS),
        6 => Some(&E),
        7 => Some(&FR),
        8 => Some(&GG),
        9 => Some(&SWSH),
        10 => Some(&BDSP),
        11 => Some(&LA),
        12 => Some(&SV),
        13 => Some(&ZA),
        14 => Some(&DP),
        15 => Some(&PT),
        16 => Some(&HGSS),
        17 => Some(&BW),
        18 => Some(&B2W2),
        19 => Some(&XY),
        20 => Some(&AO),
        21 => Some(&SM),
        22 => Some(&UU),
        _ => None,
    }
}

/// Level-up learnset as (move id, level) pairs, level 0 = evolution move.
/// Empty for unknown table/species.
pub fn learnset_moves(table: u32, ndex: u16) -> Vec<(u16, u8)> {
    let Some(reader) = table_for_id(table).and_then(|t| t.learnset_at_index(ndex)) else {
        return Vec::new();
    };
    reader
        .all_moves()
        .into_iter()
        .map(|m| {
            let lvl = match m.get_condition() {
                LearnsetCondition::LevelUp(l) => l,
                LearnsetCondition::Evolution => 0,
            };
            (m.move_id_raw(), lvl)
        })
        .collect()
}
