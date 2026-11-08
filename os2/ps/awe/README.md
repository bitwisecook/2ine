<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMWINX.DLL

Open32: the Win32 API over PM and the base OS.

- IBM source: `ps\awe\` (w*.cpp windows and controls, x*.cpp API layers, o*.cpp GDI objects; about 60 files).
- Evidence: `@BSS` labels in `os2/dll/pmwinx.sym`; SYM directory `ps/awe`.
- In Warp 4.52: Real code; its Reg* exports forward to REGISTRY.DLL.
- Exports: `pmwinx.def`.
