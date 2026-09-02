#[cfg(not(feature = "std"))]
extern crate alloc;
#[cfg(not(feature = "std"))]
use alloc::string::ToString;
#[cfg(any(feature = "wasm", feature = "alloc"))]
use pkm_rs_resources::metadata_source::MetadataSource;

mod convert;
#[allow(deprecated)]
mod deprecated;
mod issues;
mod v2;
mod v2_sections;

pub mod extra_form;
pub mod v1;

pub use convert::OhpkmConvert;
#[cfg(any(feature = "wasm", feature = "alloc"))]
pub use v2::OhpkmV2;

#[cfg(feature = "wasm")]
use wasm_bindgen::JsValue;

#[cfg(feature = "wasm")]
type JsResult<T> = core::result::Result<T, JsValue>;

#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

#[cfg(feature = "wasm")]
#[wasm_bindgen]
extern "C" {
    #[wasm_bindgen(js_namespace = console, js_name = log)]
    fn console_log(s: &str);
}

#[cfg(all(target_arch = "wasm32", feature = "wasm"))]
pub fn log<T: ToString>(s: T) {
    console_log(&s.to_string());
}

#[cfg(all(not(target_arch = "wasm32"), feature = "std"))]
pub fn log<T: ToString>(s: T) {
    println!("{}", s.to_string());
}
#[cfg(all(not(target_arch = "wasm32"), not(feature = "std")))]
pub fn log<T: ToString>(_s: T) {}

#[macro_export]
macro_rules! log {
    ($($arg:tt)*) => {{
        #[cfg(feature = "std")]
        $crate::ohpkm::log(format!($($arg)*));
        #[cfg(not(feature = "std"))]
        $crate::ohpkm::log(alloc::format!($($arg)*));
    }};
}

#[cfg(feature = "wasm")]
#[cfg_attr(feature = "wasm", wasm_bindgen(js_name = updatePidIfWouldBecomeShinyGen345))]
#[allow(clippy::missing_const_for_fn)]
pub fn update_pid_if_would_become_shiny_gen_345(pid: u32, tid: u16, sid: u16) -> u32 {
    if !is_shiny_gen_3_to_5(pid, tid, sid) && is_shiny_gen_6_plus(pid, tid, sid) {
        pid ^ 0x10000000
    } else {
        pid
    }
}

const fn shiny_xor_value(pid: u32, tid: u16, sid: u16) -> u16 {
    ((pid & 0xffff) as u16) ^ (((pid >> 16) & 0xffff) as u16) ^ tid ^ sid
}

const fn is_shiny_gen_3_to_5(pid: u32, tid: u16, sid: u16) -> bool {
    shiny_xor_value(pid, tid, sid) < 8
}

const fn is_shiny_gen_6_plus(pid: u32, tid: u16, sid: u16) -> bool {
    shiny_xor_value(pid, tid, sid) < 16
}

#[cfg(any(feature = "wasm", feature = "alloc"))]
const MOVE_METADATA_SOURCE: MetadataSource = MetadataSource::ScarletViolet;