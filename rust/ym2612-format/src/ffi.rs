//! Declarations for include/ym2612_format/c_api.h.

use std::ffi::{c_char, c_void};

extern "C" {
    pub fn ym2612_version() -> *const c_char;

    pub fn ym2612_formats_json() -> *mut c_char;

    pub fn ym2612_parse_json(
        data: *const u8,
        size: usize,
        name: *const c_char,
        format: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut c_char;

    pub fn ym2612_convert(
        data: *const u8,
        size: usize,
        name: *const c_char,
        format: *const c_char,
        index: usize,
        target_format: *const c_char,
        out_size: *mut usize,
        error: *mut *mut c_char,
    ) -> *mut u8;

    pub fn ym2612_free(ptr: *mut c_void);
}
