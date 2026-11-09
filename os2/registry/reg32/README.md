<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# REGISTRY.DLL

The OS/2 registry: the Win32-style Reg* API that Open32 (PMWINX) forwards to.

- IBM source: `w45registry\src\reg32\regmain\` (`rbmap.c`).
- Evidence: `@BSS` label in `os2/dll/registry.sym`; BLDLEVEL
  `@#IBM:14.081#@ OS/2 Registry`; LX entry table of REGISTRY.DLL (inventory
  `modules.json`, 524bafe7e730…: RegCloseKey @1 and the A/W entries
  @3-@45, KRegInit/KRegTerm @50/@51; imports DOSCALLS, PMWIN, PMSHAPI,
  PMSPL, LIBUNI, UCONV, FFST, NLS, MSG, not PMWINX) and of PMWINX.DLL
  (07a7368235f8…: REGCLOSEKEY @541 … REGSETVALUEEX @555 and REGFLUSHKEY
  @1030 forward to REGISTRY ordinals).
- In Warp 4.52: Real code; PMWINX's 16 Reg* exports forward to it.
- In 2ine: Implements the 15 ANSI entries PMWINX forwards to, at Warp's
  ordinals and names (`RegOpenKeyA` @22 …), over a host profile
  (`TWOINE_REGISTRY`). RegFlushKey (@17, PMWINX @1030) and the W entries are
  not implemented. Open32's last error is PMWINX state, so REGISTRY reports
  every failure through its result only.
- Exports: `registry.def`.

## Units

| 2ine unit | IBM unit | APIs and services |
|---|---|---|
| `regmain/registry.c` | unknown (`regmain` holds `rbmap.c`) | Root key aliases, key handles, Reg* over the profile |
