// Public domain. No rights reserved. Wholly AI-authored.
//! Launcher icons from the engine's `application-icon` and `image-preview` results.
//!
//! Results are cached per ball, launcher and launcher record, so an edited
//! launcher reloads its icon while unchanged rows reuse decoded textures.

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::PathBuf;

use gtk::prelude::*;
use gtk::{gdk, glib};
use serde_json::{Value, json};

use crate::window::BallWindow;

/// Cached entries before the cache is cleared.
const CACHE_LIMIT: usize = 256;

type Key = (PathBuf, String, Option<String>);

thread_local! {
    static CACHE: RefCell<HashMap<Key, Option<gdk::Texture>>> = RefCell::new(HashMap::new());
}

/// Decode an icon result: raw `rgba` pixels with `width`/`height`, or an
/// encoded `image`. None when the result carries no artwork.
pub fn texture(result: &Value) -> Result<Option<gdk::Texture>, glib::Error> {
    if let Some(rgba) = result["rgba"].as_str().filter(|text| !text.is_empty()) {
        let width = result["width"]
            .as_i64()
            .and_then(|value| i32::try_from(value).ok())
            .unwrap_or(0);
        let height = result["height"]
            .as_i64()
            .and_then(|value| i32::try_from(value).ok())
            .unwrap_or(0);
        let pixels = glib::base64_decode(rgba);
        let stride = usize::try_from(width).unwrap_or(0) * 4;
        if width <= 0 || height <= 0 || pixels.len() < stride * usize::try_from(height).unwrap_or(0)
        {
            return Err(glib::Error::new(
                glib::FileError::Inval,
                "The icon data is incomplete.",
            ));
        }
        let bytes = glib::Bytes::from_owned(pixels);
        return Ok(Some(
            gdk::MemoryTexture::new(width, height, gdk::MemoryFormat::R8g8b8a8, &bytes, stride)
                .upcast(),
        ));
    }
    if let Some(image) = result["image"].as_str().filter(|text| !text.is_empty()) {
        let bytes = glib::Bytes::from_owned(glib::base64_decode(image));
        return gdk::Texture::from_bytes(&bytes).map(Some);
    }
    Ok(None)
}

/// Show a launcher's icon in `image`, then report whether real artwork exists.
pub fn load_application_icon(
    window: &BallWindow,
    name: &str,
    image: &gtk::Image,
    completed: impl FnOnce(bool) + 'static,
) {
    let Some(ball) = window.ball() else { return };
    let record = window
        .snapshot()
        .and_then(|snapshot| snapshot.application_records.get(name).cloned());
    let key: Key = (ball, name.to_owned(), record);
    if let Some(cached) = CACHE.with_borrow(|cache| cache.get(&key).cloned()) {
        if let Some(texture) = &cached {
            image.set_paintable(Some(texture));
        }
        completed(cached.is_some());
        return;
    }
    let image = image.downgrade();
    let request = window.message("application-icon", json!({"ball": key.0, "name": name}));
    glib::spawn_future_local(async move {
        let result = crate::engine::spawn(move || crate::engine::request(&request)).await;
        let texture = result
            .ok()
            .and_then(|result| texture(&result).ok().flatten());
        CACHE.with_borrow_mut(|cache| {
            if cache.len() > CACHE_LIMIT {
                cache.clear();
            }
            cache.insert(key, texture.clone());
        });
        if let Some(image) = image.upgrade()
            && let Some(texture) = &texture
        {
            image.set_paintable(Some(texture));
        }
        completed(texture.is_some());
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_results_have_no_texture() {
        assert!(texture(&json!({"source": "none"})).unwrap().is_none());
    }

    #[test]
    fn truncated_pixels_are_rejected() {
        let result =
            json!({"rgba": glib::base64_encode(&[0; 8]).as_str(), "width": 2, "height": 2});
        assert!(texture(&result).is_err());
    }
}
