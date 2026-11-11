// Public domain. No rights reserved. Wholly AI-authored.
//! Native dialogs requested by the OS/2 runtime (`2ine-dialog`).
//!
//! The runtime runs this helper with literal arguments and reads its standard
//! output. A chosen value is printed on one line with exit status 0; status 1
//! means the user cancelled and 2 reports invalid arguments or an error.
//!
//! ```text
//! 2ine-dialog print-device
//! 2ine-dialog print-pcl [PRINTER…]            prints a printer name or "@save"
//! 2ine-dialog save TITLE FILENAME EXTENSION   prints an absolute path
//! 2ine-dialog fatal TITLE MESSAGE [REPORT]
//! ```

use std::cell::{Cell, RefCell};
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use gtk::{gio, glib};

pub const APP_ID: &str = "io.github.bitwisecook.Twoine.Ball.Dialog";

/// Exit status for a cancelled dialog.
pub const CANCELLED: i32 = 1;
/// Exit status for invalid arguments or a failure to show the dialog.
pub const FAILED: i32 = 2;

/// The value printed when the user chooses to save a PCL file instead of printing.
pub const SAVE_PCL: &str = "@save";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Request {
    /// Choose the OS/2 print device: `pdf` or `pcl`.
    PrintDevice,
    /// Choose a PCL 5 system printer or saving a PCL file.
    PrintPcl { printers: Vec<String> },
    /// Choose an output file.
    Save {
        title: String,
        filename: String,
        extension: String,
    },
    /// Report an application failure, optionally offering its report.
    Fatal {
        title: String,
        message: String,
        report: Option<PathBuf>,
    },
}

/// Parse the helper's arguments (excluding the program name).
pub fn parse(arguments: &[String]) -> Result<Request, String> {
    let (command, rest) = arguments.split_first().ok_or("missing dialog kind")?;
    match (command.as_str(), rest) {
        ("print-device", []) => Ok(Request::PrintDevice),
        ("print-pcl", printers) => Ok(Request::PrintPcl {
            printers: printers.to_vec(),
        }),
        ("save", [title, filename, extension]) => Ok(Request::Save {
            title: title.clone(),
            filename: safe_filename(filename),
            extension: extension.clone(),
        }),
        ("fatal", [title, message]) => Ok(Request::Fatal {
            title: title.clone(),
            message: message.clone(),
            report: None,
        }),
        ("fatal", [title, message, report]) => Ok(Request::Fatal {
            title: title.clone(),
            message: message.clone(),
            report: (!report.is_empty()).then(|| PathBuf::from(report)),
        }),
        (kind, _) => Err(format!("invalid arguments for dialog kind {kind:?}")),
    }
}

/// A suggested file name without path separators or control characters.
pub fn safe_filename(name: &str) -> String {
    let name: String = name
        .chars()
        .map(|c| {
            if matches!(c, '/' | '\\') || c.is_control() {
                '-'
            } else {
                c
            }
        })
        .collect();
    if name.trim().is_empty() {
        "Print Job".into()
    } else {
        name
    }
}

/// Run the requested dialog and return the process exit status.
pub fn run(arguments: &[String]) -> i32 {
    let request = match parse(arguments) {
        Ok(request) => request,
        Err(error) => {
            eprintln!("2ine-dialog: {error}");
            return FAILED;
        }
    };
    if let Err(error) = gtk::init() {
        eprintln!("2ine-dialog: cannot open the display: {error}");
        return FAILED;
    }
    let application = adw::Application::builder()
        .application_id(APP_ID)
        .flags(gio::ApplicationFlags::NON_UNIQUE)
        .build();
    let status = Rc::new(Cell::new(CANCELLED));
    let output = Rc::new(RefCell::new(None::<String>));
    application.connect_activate(glib::clone!(
        #[strong]
        status,
        #[strong]
        output,
        move |application| {
            let guard = application.hold();
            let finish = Finish {
                guard: RefCell::new(Some(guard)),
                status: status.clone(),
                output: output.clone(),
            };
            show(&request, Rc::new(finish));
        }
    ));
    let program = std::env::args()
        .next()
        .unwrap_or_else(|| "2ine-dialog".into());
    application.run_with_args(&[program]);
    if let Some(value) = output.take() {
        println!("{value}");
    }
    status.get()
}

/// Records the outcome and releases the application.
struct Finish {
    guard: RefCell<Option<gio::ApplicationHoldGuard>>,
    status: Rc<Cell<i32>>,
    output: Rc<RefCell<Option<String>>>,
}

impl Finish {
    fn chose(&self, value: impl Into<String>) {
        self.output.replace(Some(value.into()));
        self.status.set(0);
        self.guard.take();
    }

    fn cancelled(&self) {
        self.status.set(CANCELLED);
        self.guard.take();
    }

    fn done(&self) {
        self.status.set(0);
        self.guard.take();
    }
}

fn show(request: &Request, finish: Rc<Finish>) {
    match request {
        Request::PrintDevice => print_device(finish),
        Request::PrintPcl { printers } => print_pcl(printers, finish),
        Request::Save {
            title,
            filename,
            extension,
        } => {
            save(title, &format!("{filename}.{extension}"), extension, finish);
        }
        Request::Fatal {
            title,
            message,
            report,
        } => fatal(title, message, report.clone(), finish),
    }
}

fn alert(heading: &str, body: &str) -> adw::AlertDialog {
    let dialog = adw::AlertDialog::new(Some(heading), Some(body));
    dialog.set_close_response("cancel");
    dialog
}

fn print_device(finish: Rc<Finish>) {
    let dialog = alert(
        "Print",
        "PDF saves a document. PCL prints to a compatible printer or saves a PCL file.",
    );
    dialog.add_responses(&[
        ("cancel", "Cancel"),
        ("pcl", "PCL Printer"),
        ("pdf", "Save as PDF"),
    ]);
    dialog.set_response_appearance("pdf", adw::ResponseAppearance::Suggested);
    dialog.set_default_response(Some("pdf"));
    dialog.connect_response(None, move |_, response| match response {
        "pdf" | "pcl" => finish.chose(response),
        _ => finish.cancelled(),
    });
    dialog.present(None::<&gtk::Widget>);
}

/// Destinations offered for PCL output: saving a file, then each system printer.
pub fn pcl_destinations(printers: &[String]) -> Vec<(String, String)> {
    std::iter::once((SAVE_PCL.to_owned(), "Save PCL File…".to_owned()))
        .chain(
            printers
                .iter()
                .map(|printer| (printer.clone(), printer.clone())),
        )
        .collect()
}

fn print_pcl(printers: &[String], finish: Rc<Finish>) {
    let dialog = alert(
        "PCL Printer",
        "Choose a PCL 5-compatible system printer, or save a PCL file.",
    );
    dialog.add_responses(&[("cancel", "Cancel"), ("choose", "Continue")]);
    dialog.set_response_appearance("choose", adw::ResponseAppearance::Suggested);
    dialog.set_default_response(Some("choose"));
    let list = gtk::ListBox::builder()
        .selection_mode(gtk::SelectionMode::None)
        .css_classes(["boxed-list"])
        .build();
    let choice = Rc::new(RefCell::new(SAVE_PCL.to_owned()));
    let mut group: Option<gtk::CheckButton> = None;
    for (value, title) in pcl_destinations(printers) {
        let check = gtk::CheckButton::builder()
            .active(group.is_none())
            .valign(gtk::Align::Center)
            .build();
        check.set_group(group.as_ref());
        let row = adw::ActionRow::builder()
            .title(&title)
            .use_markup(false)
            .activatable_widget(&check)
            .build();
        row.add_prefix(&check);
        check.connect_toggled(glib::clone!(
            #[strong]
            choice,
            move |check| {
                if check.is_active() {
                    choice.replace(value.clone());
                }
            }
        ));
        group.get_or_insert(check);
        list.append(&row);
    }
    dialog.set_extra_child(Some(&list));
    dialog.connect_response(None, move |_, response| {
        if response == "choose" {
            finish.chose(choice.borrow().clone());
        } else {
            finish.cancelled();
        }
    });
    dialog.present(None::<&gtk::Widget>);
}

fn save(title: &str, name: &str, extension: &str, finish: Rc<Finish>) {
    let filter = gtk::FileFilter::new();
    filter.set_name(Some(&format!("{} Files", extension.to_uppercase())));
    filter.add_suffix(extension);
    let filters = gio::ListStore::new::<gtk::FileFilter>();
    filters.append(&filter);
    let dialog = gtk::FileDialog::builder()
        .title(if title.is_empty() {
            "Save Print Output"
        } else {
            title
        })
        .initial_name(name)
        .filters(&filters)
        .default_filter(&filter)
        .modal(true)
        .build();
    glib::spawn_future_local(async move {
        match dialog.save_future(None::<&gtk::Window>).await {
            Ok(file) => match file.path() {
                Some(path) if path.is_absolute() => finish.chose(path.to_string_lossy()),
                _ => {
                    eprintln!("2ine-dialog: choose a file on this computer");
                    finish.status.set(FAILED);
                    finish.guard.take();
                }
            },
            Err(_) => finish.cancelled(),
        }
    });
}

fn fatal(title: &str, message: &str, report: Option<PathBuf>, finish: Rc<Finish>) {
    let dialog = adw::AlertDialog::new(Some(title), Some(message));
    dialog.add_response("close", "Close");
    if report.is_some() {
        dialog.add_response("report", "Show Report");
    }
    dialog.set_default_response(Some("close"));
    dialog.set_close_response("close");
    dialog.connect_response(None, move |_, response| {
        let Some(report) = report.clone().filter(|_| response == "report") else {
            finish.done();
            return;
        };
        let finish = finish.clone();
        gtk::FileLauncher::new(Some(&gio::File::for_path(report))).launch(
            None::<&gtk::Window>,
            gio::Cancellable::NONE,
            move |_| finish.done(),
        );
    });
    dialog.present(None::<&gtk::Widget>);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn arguments(values: &[&str]) -> Vec<String> {
        values.iter().map(ToString::to_string).collect()
    }

    #[test]
    fn requests_parse_literal_arguments() {
        assert_eq!(
            parse(&arguments(&["print-device"])),
            Ok(Request::PrintDevice)
        );
        assert_eq!(
            parse(&arguments(&["print-pcl", "Laser", "Office"])),
            Ok(Request::PrintPcl {
                printers: arguments(&["Laser", "Office"])
            })
        );
        assert_eq!(
            parse(&arguments(&["save", "Save Print Output", "a/b", "pdf"])),
            Ok(Request::Save {
                title: "Save Print Output".into(),
                filename: "a-b".into(),
                extension: "pdf".into()
            })
        );
        assert_eq!(
            parse(&arguments(&["fatal", "Word Pro", "Stopped", ""])),
            Ok(Request::Fatal {
                title: "Word Pro".into(),
                message: "Stopped".into(),
                report: None
            })
        );
        assert!(parse(&arguments(&["print-device", "extra"])).is_err());
        assert!(parse(&[]).is_err());
    }

    #[test]
    fn suggested_names_are_single_file_names() {
        assert_eq!(safe_filename("Q1\\report\n"), "Q1-report-");
        assert_eq!(safe_filename("  "), "Print Job");
    }

    #[test]
    fn pcl_destinations_offer_saving_first() {
        let destinations = pcl_destinations(&arguments(&["Laser"]));
        assert_eq!(destinations[0].0, SAVE_PCL);
        assert_eq!(destinations[1], ("Laser".into(), "Laser".into()));
    }
}
