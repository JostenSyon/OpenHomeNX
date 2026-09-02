use alloc::boxed::Box;
use alloc::collections::{BTreeMap as HashMap, BTreeSet as HashSet};

use pkm_rs_types::NationalDex;

pub enum PkmRestrictions {
    CappedNationalDex {
        max_national_dex: NationalDex,
        excluded_formes: HashMap<NationalDex, Box<[u16]>>,
    },
    Dexit {
        included_national_dex: HashSet<NationalDex>,
        excluded_formes: HashMap<NationalDex, Box<[u16]>>,
    },
}
