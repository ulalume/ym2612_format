use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = manifest
        .join("../..")
        .canonicalize()
        .expect("repository root not found");

    let src = root.join("src");
    let include = root.join("include");
    let miniz = root.join("third_party").join("miniz");
    let cmakelists = root.join("CMakeLists.txt");

    let version = project_version(&cmakelists);

    let mut library = cc::Build::new();
    library
        .cpp(true)
        .std("c++20")
        .warnings(false)
        .include(&include)
        .include(&src)
        .include(&miniz)
        .define("YM2612_FORMAT_VERSION", format!("\"{version}\"").as_str());
    if env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc") {
        library.flag("/EHsc");
    }
    for file in cpp_sources(&src) {
        library.file(file);
    }
    library.compile("ym2612_format");

    let mut miniz_build = cc::Build::new();
    miniz_build
        .warnings(false)
        .include(&miniz)
        .file(miniz.join("miniz.c"))
        .compile("miniz");

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed={}", src.display());
    println!("cargo:rerun-if-changed={}", include.display());
    println!("cargo:rerun-if-changed={}", miniz.display());
    println!("cargo:rerun-if-changed={}", cmakelists.display());
}

/// Library sources, non-recursive so that src/cli is left out.
fn cpp_sources(dir: &Path) -> Vec<PathBuf> {
    let mut files: Vec<PathBuf> = fs::read_dir(dir)
        .expect("src directory not readable")
        .filter_map(|entry| {
            let path = entry.ok()?.path();
            (path.extension()? == "cpp").then_some(path)
        })
        .collect();
    files.sort();
    assert!(!files.is_empty(), "no sources in {}", dir.display());
    files
}

/// Version from `project(ym2612_format VERSION x.y.z ...)`.
fn project_version(cmakelists: &Path) -> String {
    let text = fs::read_to_string(cmakelists).expect("CMakeLists.txt not readable");
    for line in text.lines() {
        let line = line.trim();
        if !line.starts_with("project(") {
            continue;
        }
        let mut words = line.split_whitespace();
        while let Some(word) = words.next() {
            if word == "VERSION" {
                if let Some(version) = words.next() {
                    return version.trim_end_matches(')').to_string();
                }
            }
        }
    }
    panic!("no VERSION in {}", cmakelists.display());
}
