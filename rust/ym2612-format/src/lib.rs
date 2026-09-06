//! Safe bindings for the ym2612_format C API.
//!
//! ```no_run
//! let data = std::fs::read("bright piano.dmp").unwrap();
//! let parsed = ym2612_format::parse(&data, "bright piano.dmp", None).unwrap();
//! let tfi = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "tfi").unwrap();
//! # let _ = (parsed, tfi);
//! ```

mod ffi;

use serde::{Deserialize, Serialize};
use std::ffi::{c_char, CStr, CString};
use std::fmt;
use std::ptr;

/// Metadata about a supported format.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FormatInfo {
    /// Canonical format name, usable as the `format` argument.
    pub format: String,
    /// Display name.
    pub name: String,
    /// File extension without dot.
    pub extension: String,
    pub can_read: bool,
    pub can_write: bool,
    pub is_text: bool,
}

/// One instrument from a parsed file.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PatchInfo {
    pub name: String,
    pub algorithm: u8,
    pub feedback: u8,
    pub has_macros: bool,
    /// ctrmml text including the `@1 fm` header; `None` when the patch
    /// cannot be expressed as MML.
    pub mml: Option<String>,
}

/// Result of [`parse`].
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Parsed {
    pub patches: Vec<PatchInfo>,
    pub warnings: Vec<String>,
}

#[derive(Debug)]
pub enum Error {
    /// Message from the C API.
    Format(String),
    /// Argument contained an interior NUL byte.
    InvalidArgument(&'static str),
    /// The C API returned undecodable JSON.
    Json(serde_json::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Format(message) => f.write_str(message),
            Error::InvalidArgument(field) => write!(f, "`{field}` contains a NUL byte"),
            Error::Json(error) => write!(f, "malformed JSON from ym2612_format: {error}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Json(error) => Some(error),
            _ => None,
        }
    }
}

impl From<serde_json::Error> for Error {
    fn from(error: serde_json::Error) -> Self {
        Error::Json(error)
    }
}

/// Library version, e.g. `"0.3.0"`.
pub fn version() -> &'static str {
    unsafe { CStr::from_ptr(ffi::ym2612_version()) }
        .to_str()
        .unwrap_or_default()
}

/// Every supported format; empty if the descriptor list cannot be read.
pub fn formats() -> Vec<FormatInfo> {
    let json = Owned(unsafe { ffi::ym2612_formats_json() });
    match json.as_string() {
        Some(text) => serde_json::from_str(&text).unwrap_or_default(),
        None => Vec::new(),
    }
}

/// Parse instrument data.
///
/// `name` is the fallback patch name; its extension is the format hint when
/// `format` is `None`.  `format` forces an extension or format name.
pub fn parse(data: &[u8], name: &str, format: Option<&str>) -> Result<Parsed, Error> {
    let name = c_string(name, "name")?;
    let format = c_format(format)?;
    let mut error = ptr::null_mut();

    let json = Owned(unsafe {
        ffi::ym2612_parse_json(
            data.as_ptr(),
            data.len(),
            name.as_ptr(),
            as_ptr(&format),
            &mut error,
        )
    });
    let error = Owned(error);

    match json.as_string() {
        Some(text) => Ok(serde_json::from_str(&text)?),
        None => Err(Error::Format(error.message())),
    }
}

/// Parse `data`, then serialize patch `index` to `target`.
pub fn convert(
    data: &[u8],
    name: &str,
    format: Option<&str>,
    index: usize,
    target: &str,
) -> Result<Vec<u8>, Error> {
    let name = c_string(name, "name")?;
    let format = c_format(format)?;
    let target = c_string(target, "target")?;
    let mut size = 0usize;
    let mut error = ptr::null_mut();

    let out = Owned(unsafe {
        ffi::ym2612_convert(
            data.as_ptr(),
            data.len(),
            name.as_ptr(),
            as_ptr(&format),
            index,
            target.as_ptr(),
            &mut size,
            &mut error,
        )
    });
    let error = Owned(error);

    if out.0.is_null() {
        return Err(Error::Format(error.message()));
    }
    Ok(unsafe { std::slice::from_raw_parts(out.0, size) }.to_vec())
}

/// Owns a pointer returned by the C API and frees it on drop.
struct Owned<T>(*mut T);

impl<T> Drop for Owned<T> {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { ffi::ym2612_free(self.0.cast()) };
        }
    }
}

impl Owned<c_char> {
    /// Lossy so that names carrying non-UTF-8 bytes still decode.
    fn as_string(&self) -> Option<String> {
        if self.0.is_null() {
            return None;
        }
        Some(unsafe { CStr::from_ptr(self.0) }.to_string_lossy().into_owned())
    }

    fn message(&self) -> String {
        self.as_string()
            .unwrap_or_else(|| "unknown error".to_string())
    }
}

fn c_string(value: &str, field: &'static str) -> Result<CString, Error> {
    CString::new(value).map_err(|_| Error::InvalidArgument(field))
}

fn c_format(format: Option<&str>) -> Result<Option<CString>, Error> {
    format.map(|value| c_string(value, "format")).transpose()
}

fn as_ptr(value: &Option<CString>) -> *const c_char {
    value.as_ref().map_or(ptr::null(), |value| value.as_ptr())
}
