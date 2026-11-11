// Public domain. No rights reserved. Wholly AI-authored.
//! Bindings to the shared C ball engine.
//!
//! The engine accepts one JSON request and returns one JSON result through
//! `tb_request`. It owns ball persistence, drive translation, runtime selection,
//! launch plans and installer receipts; this client never edits `ball.json`.

use std::ffi::{CStr, CString, NulError, c_char, c_int, c_void};
use std::path::PathBuf;
use std::ptr;
use std::sync::mpsc;
use std::thread;

use serde::de::DeserializeOwned;
use serde_json::{Map, Value, json};

unsafe extern "C" {
    fn tb_request(
        request_json: *const c_char,
        result_json: *mut *mut c_char,
        error_utf8: *mut *mut c_char,
    ) -> c_int;
    fn tb_free(allocation: *mut c_void);
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{0}")]
    Engine(String),
    #[error("The ball engine returned malformed JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Requests cannot contain NUL characters")]
    Nul(#[from] NulError),
    #[error(transparent)]
    Io(#[from] std::io::Error),
    #[error("{0}")]
    Message(String),
}

impl Error {
    pub fn message(text: impl Into<String>) -> Self {
        Self::Message(text.into())
    }
}

pub type Result<T, E = Error> = std::result::Result<T, E>;

/// Owns one allocation returned by the engine.
struct Allocation(*mut c_char);

impl Allocation {
    fn text(&self) -> Option<String> {
        // SAFETY: the engine returns NUL-terminated UTF-8 or a null pointer.
        (!self.0.is_null()).then(|| {
            unsafe { CStr::from_ptr(self.0) }
                .to_string_lossy()
                .into_owned()
        })
    }
}

impl Drop for Allocation {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: the pointer came from tb_request and is freed exactly once.
            unsafe { tb_free(self.0.cast()) };
        }
    }
}

/// Send one request to the engine and return its JSON result.
pub fn request(message: &Value) -> Result<Value> {
    let encoded = CString::new(serde_json::to_vec(message)?)?;
    let (mut result, mut error) = (ptr::null_mut(), ptr::null_mut());
    // SAFETY: arguments are a valid C string and two out-pointers owned here.
    let status = unsafe { tb_request(encoded.as_ptr(), &raw mut result, &raw mut error) };
    let (result, error) = (Allocation(result), Allocation(error));
    if status != 0 {
        return Err(Error::Engine(
            error
                .text()
                .unwrap_or_else(|| "Ball operation failed".into()),
        ));
    }
    Ok(serde_json::from_str(&result.text().unwrap_or_default())?)
}

/// Send a request and decode its result into a typed value.
pub fn request_as<T: DeserializeOwned>(message: &Value) -> Result<T> {
    Ok(serde_json::from_value(request(message)?)?)
}

/// Build a request object from an action and additional fields.
pub fn message(action: &str, fields: Value) -> Value {
    let mut object = match fields {
        Value::Object(map) => map,
        _ => Map::new(),
    };
    object.insert("action".into(), json!(action));
    Value::Object(object)
}

/// Default directory for balls created without an explicit location.
pub fn ball_home() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME")
        .map_or_else(|| home().join(".local/share"), PathBuf::from)
        .join("2ine/ball")
}

pub fn home() -> PathBuf {
    std::env::var_os("HOME").map_or_else(|| PathBuf::from("/"), PathBuf::from)
}

/// Whether a dependency report justifies asking for original OS/2 media.
///
/// Only verified OS/2 package classifications can require the chooser; a
/// third-party static import may be missing, but OS/2 media cannot supply it.
pub fn needs_dependency_media(report: &Value) -> bool {
    if report["cached"].as_bool() == Some(true) {
        return false;
    }
    let groups = report["componentGroups"]
        .as_array()
        .map(Vec::as_slice)
        .unwrap_or_default();
    let missing = groups
        .iter()
        .any(|group| non_empty(&group["modules"]) || non_empty(&group["files"]))
        || report["componentFiles"]
            .as_array()
            .is_some_and(|files| files.iter().any(|file| file["status"] == "missing"));
    let verified = report["media"].as_array().is_some_and(|media| {
        media.iter().any(|source| {
            matches!(
                source["status"].as_str(),
                Some("verified-os2-media" | "verified-os2-set")
            )
        })
    });
    missing && report["stale"].as_bool() != Some(true) && !verified
}

fn non_empty(value: &Value) -> bool {
    value.as_array().is_some_and(|items| !items.is_empty())
}

/// Describe unresolved libraries without treating third-party DLLs as OS/2 packages.
pub fn dependency_advice(report: &Value) -> Vec<String> {
    if report["unknown"].as_bool() == Some(true) {
        return vec![report["summary"].as_str().map_or_else(
            || {
                "Dependency status is unknown. Use Rescan Dependencies to update the stored index."
                    .into()
            },
            str::to_owned,
        )];
    }
    if report["stale"].as_bool() == Some(true) {
        return vec![
            "The library check is out of date. Check again to see which OS/2 components are missing."
                .into(),
        ];
    }
    let groups = report["componentGroups"]
        .as_array()
        .map(Vec::as_slice)
        .unwrap_or_default();
    let grouped: Vec<String> = groups
        .iter()
        .flat_map(|group| group["modules"].as_array().into_iter().flatten())
        .filter_map(Value::as_str)
        .map(str::to_uppercase)
        .collect();
    let strings = |value: &Value| -> Vec<String> {
        value
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .map(str::to_owned)
            .collect()
    };
    let mut missing: Vec<String> = strings(&report["missing"])
        .into_iter()
        .filter(|name| !grouped.contains(&name.to_uppercase()))
        .collect();
    for item in report["unreachable"].as_array().into_iter().flatten() {
        if let Some(name) = item["module"].as_str()
            && !name.is_empty()
            && !grouped.contains(&name.to_uppercase())
            && !missing.iter().any(|known| known == name)
        {
            missing.push(name.to_owned());
        }
    }
    let mut lines = Vec::new();
    if !groups.is_empty() {
        lines.push(
            "Missing OS/2 components — click a component to find it, then tick it to install:"
                .into(),
        );
    }
    if !missing.is_empty() {
        let list = missing.join(", ");
        if report["scope"] == "launch" {
            lines.push(format!("Required libraries for this launch: {list}."));
            lines.push(
                "The selected executable or its static dependencies import these libraries. \
                 They may require separate software; OS/2 media may not provide them."
                    .into(),
            );
        } else {
            lines.push(format!("Other missing libraries: {list}."));
            lines.push(
                "Some may belong to optional features or require separate software. \
                 See the audit for details."
                    .into(),
            );
        }
    }
    let disabled = strings(&report["disabled"]);
    if !disabled.is_empty() {
        lines.push(format!(
            "Libraries disabled in settings: {}.",
            disabled.join(", ")
        ));
    }
    let missing_files = report["componentFiles"]
        .as_array()
        .is_some_and(|files| files.iter().any(|file| file["status"] == "missing"));
    if groups.is_empty() && missing.is_empty() && missing_files {
        lines.push("OS/2 support files are missing. See the audit for details.".into());
    }
    lines
}

type Job = Box<dyn FnOnce() + Send>;

/// A single background thread that serializes ball-changing engine requests.
#[derive(Debug, Clone)]
pub struct Worker {
    jobs: mpsc::Sender<Job>,
}

impl Worker {
    pub fn new(name: &str) -> Self {
        let (jobs, receiver) = mpsc::channel::<Job>();
        thread::Builder::new()
            .name(name.into())
            .spawn(move || receiver.into_iter().for_each(|job| job()))
            .expect("worker thread");
        Self { jobs }
    }

    /// Run `job` after every previously queued job and await its result on the main loop.
    pub async fn run<T: Send + 'static>(&self, job: impl FnOnce() -> T + Send + 'static) -> T {
        let (sender, receiver) = async_channel::bounded(1);
        self.jobs
            .send(Box::new(move || {
                let _ = sender.send_blocking(job());
            }))
            .expect("worker thread is running");
        receiver.recv().await.expect("worker returns a result")
    }
}

/// Run an independent blocking job on the GIO thread pool.
pub async fn spawn<T: Send + 'static>(job: impl FnOnce() -> T + Send + 'static) -> T {
    gtk::gio::spawn_blocking(job)
        .await
        .expect("background job completes")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn verified_media_or_stale_reports_do_not_request_media() {
        let missing = json!({"componentGroups": [{"id": "cmd", "modules": ["REXX"]}]});
        assert!(needs_dependency_media(&missing));
        let mut stale = missing.clone();
        stale["stale"] = json!(true);
        assert!(!needs_dependency_media(&stale));
        let mut verified = missing.clone();
        verified["media"] = json!([{"status": "verified-os2-media"}]);
        assert!(!needs_dependency_media(&verified));
        assert!(!needs_dependency_media(&json!({"cached": true})));
    }

    #[test]
    fn message_adds_the_action() {
        assert_eq!(
            message("snapshot", json!({"ball": "/b"})),
            json!({"ball": "/b", "action": "snapshot"})
        );
    }
}
