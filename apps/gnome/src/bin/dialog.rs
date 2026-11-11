// Public domain. No rights reserved. Wholly AI-authored.
//! `2ine-dialog`: native GNOME dialogs requested by the OS/2 runtime.

fn main() {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    std::process::exit(twoine_gnome::host_dialog::run(&arguments));
}
