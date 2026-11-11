// Public domain. No rights reserved. Wholly AI-authored.
//! Application identity and installation layout.

use std::path::PathBuf;
use std::sync::LazyLock;

pub const APP_ID: &str = "io.github.bitwisecook.Twoine.Ball";
pub const RESOURCE_PATH: &str = "/io/github/bitwisecook/Twoine/Ball";
pub const VERSION: &str = env!("TWOINE_VERSION");
pub const HELP_URL: &str = "https://github.com/bitwisecook/2ine/blob/main/docs/gnome-app.md";

/// The runtime build used when neither `--build` nor `TWOINE_BUILD` is given:
/// the packaged runtime beside the executable, else a development checkout's
/// `build/runtime`.
pub static DEFAULT_BUILD: LazyLock<PathBuf> = LazyLock::new(|| {
    if let Some(value) = std::env::var_os("TWOINE_BUILD") {
        return PathBuf::from(value);
    }
    let executable = std::env::current_exe()
        .ok()
        .and_then(|path| path.canonicalize().ok());
    if let Some(prefix) = executable
        .as_deref()
        .and_then(|path| path.parent()?.parent())
    {
        for candidate in [prefix.join("lib/2ine-ball/runtime"), prefix.join("runtime")] {
            if candidate.join("lx_loader").is_file() {
                return candidate;
            }
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/runtime")
});

/// Host-only application preferences, separate from portable ball settings.
pub fn preferences_path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map_or_else(|| crate::engine::home().join(".config"), PathBuf::from)
        .join("2ine/ball-gnome.json")
}

/// The native dialog helper installed beside this executable, used by launched
/// runtimes for printing and failure reports.
pub static DIALOG_HELPER: LazyLock<Option<PathBuf>> = LazyLock::new(|| {
    let executable = std::env::current_exe().ok()?.canonicalize().ok()?;
    let helper = executable.parent()?.join("2ine-dialog");
    helper.is_file().then_some(helper)
});
