// mod cfru;
// pub mod rr;
// pub mod ub;

#[cfg_attr(feature = "specta", derive(specta::Type))]
#[derive(Debug, Clone, Copy, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PluginIdentifier {
    RadicalRed,
    Unbound,
    LuminescentPlatinum,
    Compass,
}