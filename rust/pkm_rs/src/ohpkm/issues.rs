extern crate alloc;
#[cfg(not(feature = "std"))] use alloc::{string::{String, ToString}, vec::Vec, boxed::Box, collections::{BTreeMap, BTreeSet}, borrow::ToOwned};
#[cfg(not(feature = "std"))] use alloc::format;
use pkm_rs_resources::{abilities::AbilityIndexBounded, species::GenderRatio};
use pkm_rs_types::{AbilityNumber, Gender};
use serde::Serialize;
use core::fmt::Display;

#[derive(Debug, Clone, Serialize)]
pub enum OhpkmIssue {
    SpeciesNameCorrupted {
        corrupted: String,
        expected: String,
    },
    SpeciesNameAllCaps,
    HadPrevoSpeciesName,
    NicknameFlagIncorrect {
        expected: bool,
    },
    UnexpectedEggData,
    AffixedRibbonNotPresent,
    AbilityNumIndexMismatch {
        index: AbilityIndexBounded,
        number: AbilityNumber,
    },
    InvalidGender {
        gender: Gender,
        ratio: GenderRatio,
    },
    StellarTeraCorrupted,
}

impl Display for OhpkmIssue {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::SpeciesNameCorrupted {
                corrupted,
                expected,
            } => f.write_str(&alloc::format!(
                "Species name: expected {expected}; found {corrupted}"
            )),
            Self::SpeciesNameAllCaps => f.write_str("Nickname is species name in all caps"),
            Self::HadPrevoSpeciesName => f.write_str("Nickname is preevolution species name"),
            Self::NicknameFlagIncorrect { expected } => {
                f.write_str(&alloc::format!("Nickname flag should be {expected}"))
            }
            Self::UnexpectedEggData => f.write_str("Unexpected egg data present"),
            Self::AffixedRibbonNotPresent => f.write_str("Affixed ribbon not in possession"),
            Self::AbilityNumIndexMismatch { index, number } => f.write_str(&alloc::format!(
                "Ability index {index} does not match species + ability number {number}"
            )),
            Self::InvalidGender { gender, ratio } => f.write_str(&alloc::format!(
                "Gender {gender} invalid for species gender ratio {ratio}"
            )),
            Self::StellarTeraCorrupted => f.write_str("Stellar tera stored as invalid byte"),
        }
    }
}