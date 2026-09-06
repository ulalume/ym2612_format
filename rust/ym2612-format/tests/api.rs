use std::fs;
use std::path::{Path, PathBuf};

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn read(relative: &str) -> Vec<u8> {
    let path = repo_root().join(relative);
    fs::read(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

#[test]
fn version_is_a_release_number() {
    let version = ym2612_format::version();
    let parts: Vec<&str> = version.split('.').collect();
    assert_eq!(parts.len(), 3, "unexpected version {version}");
    assert!(parts.iter().all(|p| p.parse::<u32>().is_ok()), "{version}");
}

#[test]
fn formats_describe_every_capability() {
    let formats = ym2612_format::formats();
    assert!(formats.len() >= 13);

    let dmp = formats.iter().find(|f| f.format == "dmp").expect("dmp");
    assert_eq!(dmp.extension, "dmp");
    assert!(dmp.can_read && dmp.can_write);
    assert!(!dmp.is_text);
    assert!(!dmp.name.is_empty());

    let mml = formats.iter().find(|f| f.format == "mml").expect("mml");
    assert!(mml.is_text && mml.can_write);

    let vgm = formats.iter().find(|f| f.format == "vgm").expect("vgm");
    assert!(vgm.can_read && !vgm.can_write);
}

#[test]
fn parses_dmp_files() {
    for name in ["acoustic bass.dmp", "bright piano.dmp", "organ.dmp"] {
        let data = read(&format!("test/{name}"));
        let parsed = ym2612_format::parse(&data, name, None).unwrap();
        assert_eq!(parsed.patches.len(), 1, "{name}");

        let patch = &parsed.patches[0];
        assert_eq!(patch.name, name);
        assert!(patch.algorithm <= 7);
        assert!(patch.feedback <= 7);
        assert!(!patch.has_macros);
        assert!(patch.mml.as_ref().unwrap().starts_with("@1 fm "));
    }
}

#[test]
fn parses_with_an_explicit_format() {
    let data = read("test/bright piano.dmp");
    let parsed = ym2612_format::parse(&data, "patch", Some("dmp")).unwrap();
    assert_eq!(parsed.patches[0].feedback, 5);
    assert_eq!(parsed.patches[0].algorithm, 0);
}

#[test]
fn parses_opm() {
    let data = read("test/sample.opm");
    let parsed = ym2612_format::parse(&data, "sample.opm", None).unwrap();
    assert!(parsed.patches.len() >= 2);
    assert!(parsed.patches.iter().all(|p| p.mml.is_some()));
}

#[test]
fn parses_tfi() {
    let data = read("test/sample_strings.tfi");
    let parsed = ym2612_format::parse(&data, "sample_strings.tfi", None).unwrap();
    assert_eq!(parsed.patches.len(), 1);
    assert_eq!(parsed.patches[0].name, "sample_strings.tfi");
}

#[test]
fn converts_to_fixed_size_formats() {
    let data = read("test/bright piano.dmp");
    let tfi = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "tfi").unwrap();
    assert_eq!(tfi.len(), 42);

    let vgi = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "vgi").unwrap();
    assert_eq!(vgi.len(), 43);

    let eif = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "eif").unwrap();
    assert_eq!(eif.len(), 29);
}

#[test]
fn converts_a_later_patch() {
    let data = read("test/sample.opm");
    let parsed = ym2612_format::parse(&data, "sample.opm", None).unwrap();
    let last = parsed.patches.len() - 1;
    let tfi = ym2612_format::convert(&data, "sample.opm", None, last, "tfi").unwrap();
    assert_eq!(tfi.len(), 42);
}

#[test]
fn mml_dmp_roundtrip() {
    let data = read("test/bright piano.dmp");
    let original = ym2612_format::parse(&data, "bright piano.dmp", None).unwrap();
    let mml = original.patches[0].mml.clone().unwrap();

    let dmp =
        ym2612_format::convert(mml.as_bytes(), "input.mml", Some("mml"), 0, "dmp").unwrap();
    let roundtripped = ym2612_format::parse(&dmp, "roundtrip.dmp", None).unwrap();

    assert_eq!(roundtripped.patches.len(), 1);
    assert_eq!(
        roundtripped.patches[0].algorithm,
        original.patches[0].algorithm
    );
    assert_eq!(
        roundtripped.patches[0].feedback,
        original.patches[0].feedback
    );
    assert_eq!(dmp, data);
}

#[test]
fn converts_to_mml_text() {
    let data = read("test/organ.dmp");
    let mml = ym2612_format::convert(&data, "organ.dmp", None, 0, "mml").unwrap();
    let text = String::from_utf8(mml).unwrap();
    assert!(text.starts_with("@1 fm "));
    assert!(text.contains("OP4"));
}

#[test]
fn rejects_an_unknown_format() {
    let data = read("test/bright piano.dmp");
    let error = ym2612_format::parse(&data, "patch", Some("nope")).unwrap_err();
    assert!(error.to_string().contains("nope"), "{error}");

    let error = ym2612_format::convert(&data, "patch.dmp", None, 0, "nope").unwrap_err();
    assert!(error.to_string().contains("nope"), "{error}");
}

#[test]
fn rejects_empty_data() {
    let error = ym2612_format::parse(&[], "patch.dmp", None).unwrap_err();
    assert!(!error.to_string().is_empty());
}

#[test]
fn rejects_an_out_of_range_index() {
    let data = read("test/bright piano.dmp");
    let error = ym2612_format::convert(&data, "bright piano.dmp", None, 7, "tfi").unwrap_err();
    assert!(error.to_string().contains("out of range"), "{error}");
}

#[test]
fn rejects_a_read_only_target() {
    let data = read("test/bright piano.dmp");
    let error = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "vgm").unwrap_err();
    assert!(error.to_string().contains("writing"), "{error}");
}

#[test]
fn rejects_arguments_with_nul_bytes() {
    let data = read("test/bright piano.dmp");
    let error = ym2612_format::parse(&data, "pat\0ch.dmp", None).unwrap_err();
    assert!(error.to_string().contains("NUL"), "{error}");
}

/// The sample/ tree is not part of the repository; skipped when absent.
#[test]
fn parses_the_sample_tree() {
    let root = repo_root().join("sample");
    let mut files = Vec::new();
    collect(&root, &mut files);
    if files.is_empty() {
        eprintln!("skip: no files under {}", root.display());
        return;
    }

    let readable: Vec<String> = ym2612_format::formats()
        .into_iter()
        .filter(|f| f.can_read)
        .map(|f| f.extension)
        .collect();

    let mut parsed_count = 0;
    for file in files {
        let extension = match file.extension().and_then(|e| e.to_str()) {
            Some(extension) => extension.to_lowercase(),
            None => continue,
        };
        if !readable.contains(&extension) {
            continue;
        }
        let data = fs::read(&file).unwrap();
        let name = file.file_name().unwrap().to_string_lossy().into_owned();
        let parsed = ym2612_format::parse(&data, &name, None)
            .unwrap_or_else(|e| panic!("{}: {e}", file.display()));
        assert!(!parsed.patches.is_empty(), "{}", file.display());
        parsed_count += 1;
    }
    assert!(parsed_count > 0);
}

fn collect(dir: &Path, out: &mut Vec<PathBuf>) {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect(&path, out);
        } else {
            out.push(path);
        }
    }
}
