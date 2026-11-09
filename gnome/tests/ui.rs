// Public domain. No rights reserved. Wholly AI-authored.
//! End-to-end checks of the real manager window, driven through its actions and
//! public widget tree. Requires a display:
//!
//! ```sh
//! xvfb-run -a dbus-run-session -- cargo test --test ui
//! ```
//!
//! `TWOINE_TEST_BUILD` may name a runtime build for guest launches.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use adw::prelude::*;
use gtk::{gio, glib};
use serde_json::json;
use twoine_gnome::application::Application;
use twoine_gnome::engine;
use twoine_gnome::model::DriveKind;
use twoine_gnome::sessions::State;
use twoine_gnome::window::BallWindow;

type Check = fn(&Harness);

struct Harness {
    application: Application,
    ball: PathBuf,
    _home: tempfile::TempDir,
}

impl Harness {
    fn window(&self) -> BallWindow {
        self.application
            .active_window()
            .and_downcast::<BallWindow>()
            .expect("manager window")
    }

    fn settled(&self) {
        let window = self.window();
        pump("the window to become idle", || !window.operation_busy());
    }

    fn activate(&self, action: &str, target: Option<&str>) {
        let window = self.window();
        let target = target.map(ToVariant::to_variant);
        WidgetExt::activate_action(&window, &format!("win.{action}"), target.as_ref())
            .unwrap_or_else(|error| panic!("activate win.{action}: {error}"));
    }
}

/// Iterate the main loop until `condition` holds.
fn pump(what: &str, condition: impl Fn() -> bool) {
    let context = glib::MainContext::default();
    let deadline = Instant::now() + Duration::from_secs(60);
    while !condition() {
        assert!(Instant::now() < deadline, "timed out waiting for {what}");
        context.iteration(false);
        std::thread::sleep(Duration::from_millis(5));
    }
    // Let idle work queued by the condition's change settle.
    for _ in 0..50 {
        context.iteration(false);
    }
}

fn descendants<T: IsA<gtk::Widget>>(root: &gtk::Widget) -> Vec<T> {
    let mut found = Vec::new();
    let mut child = root.first_child();
    while let Some(widget) = child {
        if let Ok(item) = widget.clone().downcast::<T>() {
            found.push(item);
        }
        found.extend(descendants::<T>(&widget));
        child = widget.next_sibling();
    }
    found
}

fn opens_the_ball_with_every_section(harness: &Harness) {
    let window = harness.window();
    pump("the ball snapshot", || window.snapshot().is_some());
    assert_eq!(window.ball().as_deref(), Some(harness.ball.as_path()));
    let stacks = descendants::<adw::ViewStack>(window.upcast_ref());
    let stack = stacks.first().expect("section stack");
    let names: Vec<String> = (0..stack.pages().n_items())
        .filter_map(|index| {
            stack
                .pages()
                .item(index)
                .and_downcast::<adw::ViewStackPage>()
        })
        .filter_map(|page| page.name().map(Into::into))
        .collect();
    assert_eq!(
        names,
        [
            "drives",
            "components",
            "application-installation",
            "apps",
            "settings",
            "activity"
        ]
    );
    let rows = descendants::<adw::ActionRow>(window.upcast_ref());
    assert!(
        rows.iter().any(|row| row.title() == "Fixture"),
        "sidebar lists the ball"
    );
}

fn removable_media_supports_undo_and_redo(harness: &Harness) {
    let window = harness.window();
    harness.settled();
    harness.activate("add-device", Some("cdrom"));
    pump("a CD device", || {
        window.snapshot().is_some_and(|snapshot| {
            snapshot
                .drive("D")
                .is_some_and(|drive| drive.kind() == DriveKind::Cdrom)
        })
    });
    harness.settled();
    harness.activate("undo", None);
    pump("Undo to remove the device", || {
        window
            .snapshot()
            .is_some_and(|snapshot| snapshot.drive("D").is_none())
    });
    harness.settled();
    harness.activate("redo", None);
    pump("Redo to restore the device", || {
        window
            .snapshot()
            .is_some_and(|snapshot| snapshot.drive("D").is_some())
    });
    harness.settled();
    harness.activate("readonly-at", Some("D:on"));
    harness.settled();
    let manifest: serde_json::Value =
        serde_json::from_slice(&std::fs::read(harness.ball.join("ball.json")).unwrap()).unwrap();
    assert_eq!(manifest["drives"]["D"]["device"], "cdrom");
}

fn sections_follow_keyboard_navigation(harness: &Harness) {
    let window = harness.window();
    for (index, name) in [
        "drives",
        "components",
        "application-installation",
        "apps",
        "settings",
        "activity",
    ]
    .into_iter()
    .enumerate()
    {
        harness.activate(&format!("page{}", index + 1), None);
        pump("the section to show", || {
            window.visible_page().as_deref() == Some(name)
        });
    }
}

fn narrow_windows_collapse_navigation(harness: &Harness) {
    let window = harness.window();
    window.set_default_size(360, 640);
    let split = descendants::<adw::NavigationSplitView>(window.upcast_ref())
        .into_iter()
        .next()
        .expect("split view");
    pump("the sidebar to collapse", || split.is_collapsed());
    let bar = descendants::<adw::ViewSwitcherBar>(window.upcast_ref())
        .into_iter()
        .next()
        .expect("switcher bar");
    assert!(
        bar.reveals(),
        "narrow windows switch sections from the bottom bar"
    );
    window.set_default_size(1280, 800);
    pump("the sidebar to return", || !split.is_collapsed());
}

fn launches_a_saved_application(harness: &Harness) {
    if std::env::var_os("TWOINE_TEST_BUILD").is_none() {
        println!("ui: guest launch skipped (set TWOINE_TEST_BUILD to a runtime build)");
        return;
    }
    let window = harness.window();
    harness.settled();
    let program = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../tests/hello.exe");
    std::fs::copy(&program, harness.ball.join("drive_c/HELLO.EXE")).expect("copy the guest");
    window.execute(vec![
        "add-app".into(),
        "Hello".into(),
        "C:\\HELLO.EXE".into(),
    ]);
    pump("the saved application", || {
        window
            .snapshot()
            .is_some_and(|snapshot| snapshot.manifest.applications.contains_key("Hello"))
    });
    harness.settled();
    harness.activate("launch-application", Some("Hello"));
    pump("the application to finish", || {
        window
            .sessions()
            .borrow()
            .for_ball(&harness.ball)
            .iter()
            .any(|session| session.state.get() == State::Finished)
    });
    let session = window.sessions().borrow().for_ball(&harness.ball)[0].clone();
    assert_eq!(session.code.get(), Some(0), "the guest exits normally");
    let log = session.log.borrow().clone().expect("session log");
    let text = std::fs::read_to_string(log).expect("read the session log");
    assert!(text.contains("Hello world"), "{text}");
}

/// Render the window into a PNG with GTK's own renderer.
fn capture(harness: &Harness, path: &std::path::Path) {
    let window = harness.window();
    pump("the frame to settle", || true);
    let (width, height) = (window.width(), window.height());
    let paintable = gtk::WidgetPaintable::new(Some(&window));
    let snapshot = gtk::Snapshot::new();
    paintable.snapshot(&snapshot, f64::from(width), f64::from(height));
    let node = snapshot.to_node().expect("rendered window");
    let renderer = window.renderer().expect("window renderer");
    renderer
        .render_texture(&node, None)
        .save_to_png(path)
        .expect("write screenshot");
}

fn captures_screenshots(harness: &Harness) {
    let Some(directory) = std::env::var_os("TWOINE_SCREENSHOTS").map(PathBuf::from) else {
        return;
    };
    std::fs::create_dir_all(&directory).expect("screenshot directory");
    let window = harness.window();
    let style = adw::StyleManager::default();
    for (appearance, scheme) in [
        ("light", adw::ColorScheme::ForceLight),
        ("dark", adw::ColorScheme::ForceDark),
    ] {
        style.set_color_scheme(scheme);
        window.set_default_size(1280, 820);
        for (index, section) in twoine_gnome::navigation::SECTIONS.iter().enumerate() {
            harness.activate(&format!("page{}", index + 1), None);
            let deadline = Instant::now() + Duration::from_millis(800);
            pump("the section to render", || Instant::now() > deadline);
            capture(
                harness,
                &directory.join(format!("{appearance}-{}.png", section.name)),
            );
        }
        window.set_default_size(360, 720);
        harness.activate("page1", None);
        let deadline = Instant::now() + Duration::from_millis(800);
        pump("the narrow layout", || Instant::now() > deadline);
        capture(harness, &directory.join(format!("{appearance}-narrow.png")));
    }
    style.set_color_scheme(adw::ColorScheme::Default);
    window.set_default_size(1280, 800);
}

fn main() {
    if std::env::var_os("DISPLAY").is_none() && std::env::var_os("WAYLAND_DISPLAY").is_none() {
        eprintln!("ui: skipped (no display)");
        return;
    }
    let home = tempfile::tempdir().expect("temporary home");
    // SAFETY: set before GTK or any other thread starts.
    unsafe {
        std::env::set_var("XDG_CONFIG_HOME", home.path().join("config"));
        std::env::set_var("XDG_DATA_HOME", home.path().join("data"));
        std::env::set_var("GSETTINGS_BACKEND", "memory");
        if let Some(build) = std::env::var_os("TWOINE_TEST_BUILD") {
            std::env::set_var("TWOINE_BUILD", build);
        }
    }
    let ball = home.path().join("Fixture.2ineball");
    engine::request(&json!({"action": "command", "ball": ball, "arguments": ["init"]}))
        .expect("create a fixture ball");
    let ball = ball.canonicalize().unwrap();

    gtk::init().expect("GTK initialises");
    adw::init().expect("libadwaita initialises");
    twoine_gnome::register_resources();
    let application = Application::new();
    application
        .register(gio::Cancellable::NONE)
        .expect("application registers");
    application.open(&[gio::File::for_path(&ball)], "");

    let harness = Harness {
        application,
        ball,
        _home: home,
    };
    let checks: &[(&str, Check)] = &[
        (
            "opens the ball with every section",
            opens_the_ball_with_every_section,
        ),
        (
            "removable media supports undo and redo",
            removable_media_supports_undo_and_redo,
        ),
        ("launches a saved application", launches_a_saved_application),
        ("captures screenshots", captures_screenshots),
        (
            "sections follow keyboard navigation",
            sections_follow_keyboard_navigation,
        ),
        (
            "narrow windows collapse navigation",
            narrow_windows_collapse_navigation,
        ),
    ];
    for (name, check) in checks {
        check(&harness);
        println!("ui: {name} ... ok");
    }
    harness.window().destroy();
}
