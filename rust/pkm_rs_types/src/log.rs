#[cfg(target_arch = "wasm32")]
#[cfg(not(feature = "std"))] use alloc::format;
#[cfg(not(feature = "std"))] use alloc::{string::{String, ToString}, vec::Vec, boxed::Box, collections::{BTreeMap, BTreeSet}};
#[cfg(feature = "wasm")]
#[cfg(feature = "wasm")]
use wasm_bindgen::prelude::*;

#[cfg(target_arch = "wasm32")]
#[cfg(all(target_arch = "wasm32", feature = "wasm"))]
#[cfg_attr(feature = "wasm", wasm_bindgen)]
extern "C" {
    #[cfg_attr(feature = "wasm", wasm_bindgen(js_namespace = console, js_name = log))]
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

pub fn fatal_log<T: ToString>(s: T) -> ! {
    let message = s.to_string();
    log(&message);
    panic!("{message}");
}

#[macro_export]
macro_rules! log {
    ($($arg:tt)*) => {{
        #[cfg(feature = "std")]
        $crate::log::log(format!($($arg)*));
        #[cfg(not(feature = "std"))]
        $crate::log::log(alloc::format!($($arg)*));
    }};
}

#[macro_export]
macro_rules! fatal_log {
    ($($arg:tt)*) => {{
        #[cfg(feature = "std")]
        $crate::fatal_log(format!($($arg)*));
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
            Err(e) => fatal_log(format!("{}: {}", msg.to_string(), e)),
            #[cfg(not(feature = "std"))]
            Err(e) => fatal_log(alloc::format!("{}: {}", msg.to_string(), e)),
        }
    }
}
