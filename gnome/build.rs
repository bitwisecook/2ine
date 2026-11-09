// Public domain. No rights reserved. Wholly AI-authored.
//! Compiles Blueprint UI files and bundled icons into a `GResource` and builds the
//! shared C ball engine statically, so the application has no runtime engine
//! library dependency.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::{env, fs};

const PREFIX: &str = "/io/github/bitwisecook/Twoine/Ball";

fn main() {
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("manifest dir"));
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("out dir"));
    let repository = manifest.parent().expect("repository root").to_path_buf();

    compile_resources(&manifest, &out);
    build_engine(&repository);

    let version = fs::read_to_string(repository.join("VERSION")).expect("VERSION");
    println!("cargo::rustc-env=TWOINE_VERSION={}", version.trim());
    println!(
        "cargo::rerun-if-changed={}",
        repository.join("VERSION").display()
    );
}

fn sorted_files(directory: &Path, extension: &str) -> Vec<PathBuf> {
    let mut files: Vec<_> = fs::read_dir(directory)
        .into_iter()
        .flatten()
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|value| value == extension))
        .collect();
    files.sort();
    files
}

fn compile_resources(manifest: &Path, out: &Path) {
    let data = manifest.join("data");
    let blueprints = sorted_files(&data.join("ui"), "blp");
    let compiled = out.join("ui");
    fs::create_dir_all(&compiled).expect("ui output");
    println!("cargo::rerun-if-changed={}", data.display());
    if !blueprints.is_empty() {
        let status = Command::new("blueprint-compiler")
            .arg("batch-compile")
            .arg(&compiled)
            .arg(data.join("ui"))
            .args(&blueprints)
            .status()
            .expect("blueprint-compiler is required to build the UI");
        assert!(status.success(), "blueprint-compiler failed");
    }

    let mut xml = format!(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gresources>\n  <gresource prefix=\"{PREFIX}\">\n"
    );
    for blueprint in &blueprints {
        let name = blueprint.file_stem().unwrap().to_string_lossy();
        writeln!(
            xml,
            "    <file compressed=\"true\" preprocess=\"xml-stripblanks\" alias=\"ui/{name}.ui\">{}</file>",
            compiled.join(format!("{name}.ui")).display()
        )
        .unwrap();
    }
    for icon in sorted_files(&data.join("icons"), "svg") {
        let name = icon.file_name().unwrap().to_string_lossy();
        let kind = if name.ends_with("-symbolic.svg") {
            "symbolic"
        } else {
            "scalable"
        };
        writeln!(
            xml,
            "    <file compressed=\"true\" preprocess=\"xml-stripblanks\" alias=\"icons/{kind}/apps/{name}\">{}</file>",
            icon.display()
        )
        .unwrap();
    }
    xml.push_str("  </gresource>\n</gresources>\n");
    let descriptor = out.join("resources.gresource.xml");
    fs::write(&descriptor, xml).expect("resource descriptor");
    glib_build_tools::compile_resources(&[out], descriptor.to_str().unwrap(), "twoine.gresource");
}

fn build_engine(repository: &Path) {
    let engine = repository.join("ball");
    println!("cargo::rerun-if-changed={}", engine.display());
    let destination = cmake::Config::new(&engine)
        .generator("Ninja")
        .build_target("twoine_ball_static")
        .profile("Release")
        .build();
    println!(
        "cargo::rustc-link-search=native={}",
        destination.join("build").display()
    );
    println!("cargo::rustc-link-lib=static=twoine_ball_core");
    for library in ["archive", "z", "m"] {
        println!("cargo::rustc-link-lib=dylib={library}");
    }
}
