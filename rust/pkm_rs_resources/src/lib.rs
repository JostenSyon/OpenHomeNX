#![cfg_attr(not(feature = "std"), no_std)]
extern crate alloc;
mod pkhex_bin;
mod pkhex_text;
mod result;

pub mod abilities;
pub mod ball;
pub mod helpers;
pub mod items;
pub mod levelup;
pub mod lookup;
pub mod metadata_source;
pub mod moves;
pub mod natures;
pub mod restrictions;
pub mod ribbons;
pub mod species;
pub mod stats;
pub mod text_resource;

pub use result::*;

#[cfg(not(feature = "std"))] use alloc::string::ToString;

#[cfg(target_arch = "wasm32")]
use wasm_bindgen::prelude::*;

#[cfg(target_arch = "wasm32")]
#[cfg_attr(feature = "wasm", wasm_bindgen)]
extern "C" {
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_namespace = console, js_name = log))]
    fn console_log(s: &str);
}

#[cfg(target_arch = "wasm32")]
pub fn log<T: ToString>(s: T) {
    console_log(&s.to_string());
}

#[cfg(all(not(target_arch = "wasm32"), feature = "std"))]
pub fn log<T: ToString>(s: T) {
    #[cfg(feature="std")] println!("{}", s.to_string());
}
#[cfg(all(not(target_arch = "wasm32"), not(feature = "std")))]
pub fn log<T: ToString>(_s: T) {}

pub fn fatal_log<T: ToString>(s: T) -> ! {
    let message = s.to_string();
    log(&message);
    panic!("{message}");
}

#[macro_export]
macro_rules! log {
    ($($arg:tt)*) => {{
        #[cfg(feature = "std")]
        $crate::log(alloc::format!($($arg)*));
        #[cfg(not(feature = "std"))]
        $crate::log(alloc::format!($($arg)*));
    }};
}

#[macro_export]
macro_rules! fatal_log {
    ($($arg:tt)*) => {{
        #[cfg(feature = "std")]
        $crate::fatal_log(alloc::format!($($arg)*));
        #[cfg(not(feature = "std"))]
        $crate::fatal_log(alloc::format!($($arg)*));
    }};
}

pub trait ExpectLog<T> {
    fn expect_log<S: ToString>(self, msg: S) -> T;
}

impl<T> ExpectLog<T> for Option<T> {
    fn expect_log<S: ToString>(self, msg: S) -> T {
        match self {
            Some(value) => value,
            None => fatal_log(msg),
        }
    }
}

impl<T, E: core::error::Error> ExpectLog<T> for core::result::Result<T, E> {
    fn expect_log<S: ToString>(self, msg: S) -> T {
        match self {
            Ok(value) => value,
            #[cfg(feature = "std")]
            Err(e) => fatal_log(alloc::format!("{}: {}", msg.to_string(), e)),
            #[cfg(not(feature = "std"))]
            Err(e) => fatal_log(alloc::format!("{}: {}", msg.to_string(), e)),
        }
    }
}
