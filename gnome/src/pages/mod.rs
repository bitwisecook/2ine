// Public domain. No rights reserved. Wholly AI-authored.
//! Ball sections shown in the window's view stack.
//!
//! Each section module exposes `page(window) -> gtk::Widget`, holds a weak
//! reference to the window, and refreshes itself on `snapshot-changed`.

pub mod applications;
pub mod components;
pub mod drives;
pub mod installation;
pub mod logs;
pub mod settings;
