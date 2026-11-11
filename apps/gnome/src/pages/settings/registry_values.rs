// Public domain. No rights reserved. Wholly AI-authored.
//! Registry value representations used by the registry editor.
//!
//! A stored Open32 registry record is a four-byte little-endian type header
//! followed by the value payload. Editors convert between that wire form and
//! a text representation appropriate to the type.

use std::fmt::Write as _;

/// The largest payload Open32 stores for one value.
pub const MAXIMUM_PAYLOAD: usize = 16 * 1024 * 1024;

/// Registry types offered by the type selector, with their selector titles.
pub const REGISTRY_TYPES: [(u32, &str); 12] = [
    (0, "REG_NONE"),
    (1, "REG_SZ"),
    (2, "REG_EXPAND_SZ"),
    (3, "REG_BINARY"),
    (4, "REG_DWORD (Little Endian)"),
    (5, "REG_DWORD_BIG_ENDIAN"),
    (6, "REG_LINK"),
    (7, "REG_MULTI_SZ"),
    (8, "REG_RESOURCE_LIST"),
    (9, "REG_FULL_RESOURCE_DESCRIPTOR"),
    (10, "REG_RESOURCE_REQUIREMENTS_LIST"),
    (11, "REG_QWORD (Little Endian)"),
];

const TYPE_NAMES: [&str; 12] = [
    "REG_NONE",
    "REG_SZ",
    "REG_EXPAND_SZ",
    "REG_BINARY",
    "REG_DWORD",
    "REG_DWORD_BIG_ENDIAN",
    "REG_LINK",
    "REG_MULTI_SZ",
    "REG_RESOURCE_LIST",
    "REG_FULL_RESOURCE_DESCRIPTOR",
    "REG_RESOURCE_REQUIREMENTS_LIST",
    "REG_QWORD",
];

/// A value that cannot be represented or stored as requested.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
#[error("{0}")]
pub struct Invalid(pub String);

impl Invalid {
    pub fn new(message: impl Into<String>) -> Self {
        Self(message.into())
    }
}

pub type Result<T> = std::result::Result<T, Invalid>;

/// An editor representation of a registry record.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Text,
    List,
    Integer,
    Hex,
    Raw,
}

impl Mode {
    pub fn title(self) -> &'static str {
        match self {
            Self::Text => "Latin-1 Text",
            Self::List => "String List",
            Self::Integer => "Unsigned Integer",
            Self::Hex => "Hexadecimal Data",
            Self::Raw => "Complete Raw Record",
        }
    }
}

/// Fold ASCII capitals only, as Open32 does for stored names.
pub fn fold(value: &str) -> String {
    value.to_ascii_lowercase()
}

/// The selector title for a registry type, if it is one of the standard types.
pub fn selector_title(kind: u32) -> Option<&'static str> {
    REGISTRY_TYPES
        .iter()
        .find(|(code, _)| *code == kind)
        .map(|(_, title)| *title)
}

/// The position of a standard type in [`REGISTRY_TYPES`].
pub fn selector_index(kind: u32) -> Option<usize> {
    REGISTRY_TYPES.iter().position(|(code, _)| *code == kind)
}

pub fn type_name(kind: Option<u32>) -> String {
    match kind {
        None => "Malformed record".into(),
        Some(kind) => usize::try_from(kind)
            .ok()
            .and_then(|index| TYPE_NAMES.get(index))
            .map_or_else(|| format!("Unknown type {kind}"), |name| (*name).to_owned()),
    }
}

fn parse_unsigned(text: &str) -> Option<u64> {
    let text = text.trim();
    match text.get(..2) {
        Some(prefix) if prefix.eq_ignore_ascii_case("0x") => {
            u64::from_str_radix(&text[2..], 16).ok()
        }
        _ => text.parse().ok(),
    }
}

/// An unsigned 32-bit type from decimal or 0x hexadecimal text.
pub fn parse_type(text: &str) -> Option<u32> {
    parse_unsigned(text).and_then(|value| u32::try_from(value).ok())
}

/// The little-endian type header of a wire record.
pub fn header(wire: &[u8]) -> Option<u32> {
    wire.get(..4)
        .map(|bytes| u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

pub fn typed_mode(kind: u32) -> Option<Mode> {
    match kind {
        1 | 2 => Some(Mode::Text),
        7 => Some(Mode::List),
        4 | 5 | 11 => Some(Mode::Integer),
        _ => None,
    }
}

/// Representations offered for a type: its own form, then hexadecimal and raw.
pub fn modes(kind: u32) -> Vec<Mode> {
    typed_mode(kind)
        .into_iter()
        .chain([Mode::Hex, Mode::Raw])
        .collect()
}

fn integer_bytes(kind: u32) -> Option<usize> {
    match kind {
        4 | 5 => Some(4),
        11 => Some(8),
        _ => None,
    }
}

/// Decode Latin-1 bytes; every byte maps to one character.
pub fn latin1(bytes: &[u8]) -> String {
    bytes.iter().map(|&byte| char::from(byte)).collect()
}

/// Encode text as Latin-1 bytes.
pub fn to_latin1(text: &str) -> Result<Vec<u8>> {
    text.chars()
        .map(|character| u8::try_from(u32::from(character)).ok())
        .collect::<Option<_>>()
        .ok_or_else(|| Invalid::new("OS/2 values require Latin-1 characters."))
}

pub fn to_hex(bytes: &[u8]) -> String {
    bytes
        .iter()
        .fold(String::with_capacity(bytes.len() * 2), |mut text, byte| {
            let _ = write!(text, "{byte:02x}");
            text
        })
}

/// Bytes from hexadecimal text; whitespace between digits is ignored.
pub fn hex_bytes(text: &str) -> Result<Vec<u8>> {
    let digits: Vec<u8> = text
        .bytes()
        .filter(|byte| !byte.is_ascii_whitespace())
        .collect();
    if !digits.len().is_multiple_of(2) {
        return Err(Invalid::new(
            "Hexadecimal data needs two digits for every byte.",
        ));
    }
    digits
        .chunks(2)
        .map(|pair| {
            std::str::from_utf8(pair)
                .ok()
                .and_then(|pair| u8::from_str_radix(pair, 16).ok())
        })
        .collect::<Option<_>>()
        .ok_or_else(|| Invalid::new("Hexadecimal data can contain only 0–9 and A–F."))
}

pub fn decode_string(payload: &[u8]) -> Result<String> {
    match payload.split_last() {
        Some((0, body)) if !body.contains(&0) => Ok(latin1(body)),
        _ => Err(Invalid::new(
            "A string requires one terminating NUL and no embedded NUL bytes. \
             Use hexadecimal to preserve malformed data.",
        )),
    }
}

pub fn decode_list(payload: &[u8]) -> Result<Vec<String>> {
    let Some(body) = payload.strip_suffix(b"\0\0") else {
        return Err(Invalid::new(
            "REG_MULTI_SZ requires a double NUL terminator.",
        ));
    };
    if body.is_empty() {
        return Ok(Vec::new());
    }
    let rows: Vec<&[u8]> = body.split(|&byte| byte == 0).collect();
    if rows.iter().any(|row| row.is_empty()) {
        return Err(Invalid::new(
            "REG_MULTI_SZ cannot contain an empty entry before its terminator.",
        ));
    }
    Ok(rows.into_iter().map(latin1).collect())
}

pub fn encode_list(rows: &[&str]) -> Result<Vec<u8>> {
    if rows.iter().any(|row| row.is_empty()) {
        return Err(Invalid::new("Multi-string entries cannot be empty."));
    }
    if rows.is_empty() {
        return Ok(vec![0, 0]);
    }
    let mut wire = Vec::new();
    for row in rows {
        wire.extend(to_latin1(row)?);
        wire.push(0);
    }
    wire.push(0);
    Ok(wire)
}

pub fn decode_integer(payload: &[u8], kind: u32) -> Result<u64> {
    let count = integer_bytes(kind);
    if count != Some(payload.len()) {
        return Err(Invalid(format!(
            "{} requires exactly {} bytes.",
            type_name(Some(kind)),
            count.unwrap_or(0)
        )));
    }
    let mut bytes = [0_u8; 8];
    if kind == 5 {
        bytes[8 - payload.len()..].copy_from_slice(payload);
        Ok(u64::from_be_bytes(bytes))
    } else {
        bytes[..payload.len()].copy_from_slice(payload);
        Ok(u64::from_le_bytes(bytes))
    }
}

pub fn encode_integer(text: &str, kind: u32) -> Result<Vec<u8>> {
    let count = integer_bytes(kind);
    let value = parse_unsigned(text);
    match (count, value) {
        (Some(4), Some(value)) if let Ok(value) = u32::try_from(value) => Ok(if kind == 5 {
            value.to_be_bytes()
        } else {
            value.to_le_bytes()
        }
        .to_vec()),
        (Some(8), Some(value)) => Ok(value.to_le_bytes().to_vec()),
        _ => Err(Invalid(format!(
            "Enter an unsigned {}-bit decimal number or hexadecimal number starting with 0x.",
            if kind == 11 { "64" } else { "32" }
        ))),
    }
}

/// Check a complete wire record before it is stored.
pub fn validate_wire(wire: &[u8]) -> Result<()> {
    if wire.len() < 4 {
        return Err(Invalid::new(
            "The raw record needs a four-byte little-endian type header. Repair it explicitly before saving.",
        ));
    }
    if wire.len() - 4 > MAXIMUM_PAYLOAD {
        return Err(Invalid::new(
            "Open32 values support at most 16 MiB of data.",
        ));
    }
    if header(wire) == Some(4) && wire.len() != 8 {
        return Err(Invalid::new("REG_DWORD requires exactly four bytes."));
    }
    Ok(())
}

/// Check a typed payload against the layout its registry type requires.
pub fn validate_value(kind: Option<u32>, data: &[u8]) -> Result<()> {
    if data.len() > MAXIMUM_PAYLOAD {
        return Err(Invalid::new("Registry payloads are limited to 16 MiB."));
    }
    match kind {
        Some(4 | 5) if data.len() != 4 => {
            Err(Invalid::new("DWORD values require exactly four bytes."))
        }
        Some(11) if data.len() != 8 => {
            Err(Invalid::new("QWORD values require exactly eight bytes."))
        }
        Some(1 | 2 | 6) if decode_string(data).is_err() => Err(Invalid::new(
            "This string type requires one terminating NUL and no embedded NUL bytes.",
        )),
        Some(7)
            if !data.ends_with(b"\0\0") || {
                let body = &data[..data.len() - 2];
                !body.is_empty() && body.split(|&byte| byte == 0).any(<[u8]>::is_empty)
            } =>
        {
            Err(Invalid::new(
                "REG_MULTI_SZ requires NUL-separated strings, a final double NUL and no empty interior strings.",
            ))
        }
        _ => Ok(()),
    }
}

/// The complete wire record for an editor representation.
pub fn encode(mode: Mode, kind: Option<u32>, text: &str) -> Result<Vec<u8>> {
    if mode == Mode::Raw {
        return hex_bytes(text);
    }
    let Some(kind) = kind else {
        return Err(Invalid::new(
            "Enter a registry type between 0 and 4,294,967,295.",
        ));
    };
    let payload = match mode {
        Mode::Text => {
            if !matches!(kind, 1 | 2) {
                return Err(Invalid::new(
                    "Choose a string type before using the text editor.",
                ));
            }
            if text.contains('\0') {
                return Err(Invalid::new(
                    "Text cannot contain NUL. Use hexadecimal for binary values.",
                ));
            }
            let mut payload = to_latin1(text)?;
            payload.push(0);
            payload
        }
        Mode::List => {
            if kind != 7 {
                return Err(Invalid::new(
                    "Choose REG_MULTI_SZ before using the string list editor.",
                ));
            }
            let rows: Vec<&str> = if text.is_empty() {
                Vec::new()
            } else {
                text.split('\n').collect()
            };
            encode_list(&rows)?
        }
        Mode::Integer => {
            if integer_bytes(kind).is_none() {
                return Err(Invalid::new(
                    "Choose DWORD or QWORD before using the integer editor.",
                ));
            }
            encode_integer(text, kind)?
        }
        Mode::Hex | Mode::Raw => hex_bytes(text)?,
    };
    let mut wire = kind.to_le_bytes().to_vec();
    wire.extend(payload);
    Ok(wire)
}

/// Editor text for a wire record in the requested representation.
pub fn render(mode: Mode, wire: &[u8]) -> Result<String> {
    if mode == Mode::Raw {
        return Ok(to_hex(wire));
    }
    let Some(kind) = header(wire) else {
        return Err(Invalid::new(
            "Repair the type header in the complete raw record first.",
        ));
    };
    let payload = &wire[4..];
    match mode {
        Mode::Hex | Mode::Raw => Ok(to_hex(payload)),
        Mode::Text => decode_string(payload),
        Mode::List => {
            let rows = decode_list(payload)?;
            if rows.iter().any(|row| row.contains(['\n', '\r'])) {
                return Err(Invalid::new(
                    "An entry contains a newline; use hexadecimal to preserve its exact bytes.",
                ));
            }
            Ok(rows.join("\n"))
        }
        Mode::Integer => {
            if integer_bytes(kind).is_none() {
                return Err(Invalid::new(
                    "The record does not contain a supported integer type.",
                ));
            }
            Ok(decode_integer(payload, kind)?.to_string())
        }
    }
}

/// The starting representation and text for an existing record.
pub fn initial(wire: &[u8]) -> (Mode, String) {
    let Some(kind) = header(wire) else {
        return (Mode::Raw, to_hex(wire));
    };
    if let Some(mode) = typed_mode(kind)
        && let Ok(text) = render(mode, wire)
    {
        return (mode, text);
    }
    (Mode::Hex, to_hex(&wire[4..]))
}

/// Whether a stored record name is already in the folded Open32 form.
pub fn normalized(application: &str, key: &str) -> bool {
    key.starts_with("v:") && fold(application) == application && fold(key) == key
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wire(kind: u32, payload: &[u8]) -> Vec<u8> {
        let mut wire = kind.to_le_bytes().to_vec();
        wire.extend_from_slice(payload);
        wire
    }

    #[test]
    fn existing_values_open_in_their_typed_representation() {
        assert_eq!(initial(&wire(1, b"abc\0")), (Mode::Text, "abc".into()));
        assert_eq!(
            initial(&wire(7, b"a\0bc\0\0")),
            (Mode::List, "a\nbc".into())
        );
        assert_eq!(
            initial(&wire(4, b"\x2a\0\0\0")),
            (Mode::Integer, "42".into())
        );
        assert_eq!(initial(&wire(3, b"\x01\x02")), (Mode::Hex, "0102".into()));
        assert_eq!(initial(b"\x01\x02"), (Mode::Raw, "0102".into()));
    }

    #[test]
    fn malformed_typed_payload_falls_back_to_hexadecimal() {
        assert_eq!(
            initial(&wire(1, b"no-terminator")),
            (Mode::Hex, to_hex(b"no-terminator"))
        );
    }

    #[test]
    fn integer_widths_and_byte_order_follow_the_type() {
        assert_eq!(
            encode(Mode::Integer, Some(4), "0x10").unwrap(),
            wire(4, b"\x10\0\0\0")
        );
        assert_eq!(
            encode(Mode::Integer, Some(5), "16").unwrap(),
            wire(5, b"\0\0\0\x10")
        );
        assert_eq!(
            encode(Mode::Integer, Some(11), "1").unwrap(),
            wire(11, b"\x01\0\0\0\0\0\0\0")
        );
        assert_eq!(
            render(Mode::Integer, &wire(5, b"\0\0\x01\0")).unwrap(),
            "256"
        );
        assert!(encode(Mode::Integer, Some(4), &(1_u64 << 32).to_string()).is_err());
    }

    #[test]
    fn string_list_encoding_matches_reg_multi_sz() {
        assert_eq!(encode(Mode::List, Some(7), "").unwrap(), wire(7, b"\0\0"));
        assert_eq!(
            encode(Mode::List, Some(7), "a\nb").unwrap(),
            wire(7, b"a\0b\0\0")
        );
        assert!(encode(Mode::List, Some(7), "a\n\nb").is_err());
    }

    #[test]
    fn raw_record_carries_the_type_header() {
        let record = encode(Mode::Raw, None, "05000000 00000001").unwrap();
        assert_eq!(header(&record), Some(5));
        assert_eq!(render(Mode::Integer, &record).unwrap(), "1");
        assert!(validate_wire(&hex_bytes("0400").unwrap()).is_err());
    }

    #[test]
    fn type_text_accepts_hexadecimal() {
        assert_eq!(parse_type("0x10"), Some(16));
        assert_eq!(parse_type("0x100000000"), None);
        assert_eq!(parse_type("-1"), None);
        assert_eq!(modes(4), [Mode::Integer, Mode::Hex, Mode::Raw]);
        assert_eq!(modes(3), [Mode::Hex, Mode::Raw]);
    }

    #[test]
    fn only_folded_names_are_editable() {
        assert!(normalized("hkcu\\software", "v:name"));
        assert!(!normalized("HKCU\\Software", "v:name"));
        assert!(!normalized("hkcu", "v:Name"));
    }

    #[test]
    fn typed_payloads_are_validated_before_saving() {
        assert!(validate_value(Some(1), b"ok\0").is_ok());
        assert!(validate_value(Some(1), b"a\0b\0").is_err());
        assert!(validate_value(Some(4), b"\0\0\0").is_err());
        assert!(validate_value(Some(11), &[0; 8]).is_ok());
        assert!(validate_value(Some(7), b"\0\0").is_ok());
        assert!(validate_value(Some(7), b"a\0\0b\0\0").is_err());
        assert!(validate_value(Some(3), b"").is_ok());
    }

    #[test]
    fn text_is_latin1() {
        assert_eq!(to_latin1("café").unwrap(), b"caf\xe9");
        assert!(to_latin1("€").is_err());
        assert_eq!(latin1(b"caf\xe9"), "café");
        assert_eq!(type_name(Some(4)), "REG_DWORD");
        assert_eq!(type_name(Some(99)), "Unknown type 99");
        assert_eq!(type_name(None), "Malformed record");
    }
}
