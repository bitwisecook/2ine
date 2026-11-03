# OS/2 configuration file discovery and editing

The shared ball engine exposes `configuration-files`, `configuration-file-read`
and `configuration-file-write`. Discovery includes CONFIG.SYS and configured
USER_INI/SYSTEM_INI/registry aliases, deduplicating their roles, followed by other
existing private C: INI files. Missing configured profiles remain visible as
`kind:profile`, `exists:false`, with “Created on first save”. Frontends use the
existing named profile actions for those roles; generic file actions remain
existing-file only.

Generic reads identify Latin-1 text, supported OS/2/2INE binary profiles, or
unsupported binary data. Text writes preserve Latin-1 bytes and line endings;
profile edits retain unrelated records. Unsupported binary is read-only hex.
Writes require the SHA-256 revision returned by the read and reject stale drafts.
Profile writes use `application`, `key`, `text` (hex; `data` alias accepted),
and optional `remove`; text writes use `text`.

Generic paths must be absolute guest C: paths inside the ball's private drive.
Links, traversal, external drive mappings, non-INI files (except root CONFIG.SYS),
temporary directories and the native runtime cache are rejected. Discovery is
bounded to 2048 files, 100000 entries, 4096 directories, depth 64 and 32 MiB per
editable file. Responses expose `truncated`, `limit`, `skipped` and a reason.
The existing stopped-guest frontend guard remains required. Revision checks do
not constitute a lock against an independent host writer.

Verification: `tests.tools.test_configuration` and
`tests.tools.test_configuration_files`, 12 tests, cover Latin-1/CRLF, stale
revisions, unsupported binary preservation, unrelated profile bytes, missing
standard profiles and first named-area save, alias roles, discovery truncation,
links, traversal and external mappings. Final build/test logs are
`/tmp/2ine-generic-ini-final-build2.log` and
`/tmp/2ine-generic-ini-final-tests2.log`.
