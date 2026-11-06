# PMWP SOM class exports (17 frozen rows)

Status: unbuilt, untested. Nothing here has been compiled or run.

## Evidence

- Original PMWP: `extracted/module-packages/96f33f91.../output.000.OS2_DLL_PMWP.DLL`,
  SHA-256 `0886240f1c22d2c7a22e13cdb9f8948ceeeb7bb257dd534fcd7dd0c7017b2fcf`. (The
  directory name is the package hash, not the DLL hash.)
- Symbols: `extracted/install-media/os2image/debug/symbols/os2/dll/pmwp.sym`, SHA-256
  `54b2de4a...`. Symbol offsets agree with the DLL (WPFileSystemNewClass = object 1
  +0x540b4, the export offset of ordinal 12).
- Guest SOM: ball `drive_c/OS2/DLL/SOM.DLL`, read-only. Used ordinals: 1 SOMCalloc,
  3 SOMClassClassData, 6 SOMClassMgrClassData, 7 SOMClassMgrObject, 10 SOMFree,
  12 SOMMalloc, 14 SOMObjectClassData, 20/21/22 SOMClassMgr/SOMClass/SOMObjectNewClass,
  49 somBuildClass, 50 somParentNumResolve.
- Class tables were extracted from original bytes plus LX fixups by following each
  NewClass to the immediate pushed before som#49. All 19 classes: WPObject, WPAbstract,
  WPFileSystem, WPFolder, WPDataFile, WPTransient, WPDesktop, WPDisk, WPProgram, their
  M_ metaclasses, and WPClassManager (no metaclass of its own).
- All 71 consumed tokens match the SDK class-data layout when mapped through the
  original smt `classData` offsets (0 differences).

## Findings that decide the design

- SCI `layoutVersion` is 4; `numDirectInitClasses` is -1; smt rows use
  `redispatchStub = -1`, `applyStub = 0`. Ids are three-level: smt.methodId -> somId
  cell -> char* cell -> string.
- SOM method tokens are directly callable thunks. Original PMWP does
  `mov eax,[token]; call eax`, and `somResolve` reads `[token-4]`. Instance data tokens
  are callable the same way. So calling a token from native code IS SOM dispatch, and
  subclass overrides run. No separate resolve step is needed.
- somBuildClass returns the existing class when `cds->classObject` is set, after the
  version check: idempotence comes from SOM. Original NewClass always calls it.
- `objM_somNew`/`objM_somRenew` return NULL in the original. Objects are created only by
  wpclsNew -> wpclsINew, which allocates, calls the parent somRenew, then wpclsIncUsage,
  wpSetFolder, style, wpSetTitle, wpSetupOnce, and wpObjectReady.
- objM_somClassReady calls the parent then wpclsInitData; obj_somInit calls wpInitData;
  obj_somUninit calls wpUnInitData then wpclsDecUsage.
- SOM heap uses DosAllocMem flags 0x13 without PAG_EXECUTE. 2ine adds guest execute for
  any readable DosAllocMem page, so runtime-generated tokens can execute.
- The loader adds LX additive values after resolving an import, so `PMWP#23 + 112` works.
  `lx_execution_entry_address` requires LX_GUEST_EXECUTE on the target, so data exports
  use a read/write/execute mapping (same as DosAllocMem) and need no loader change.
- Native SOM is only a fallback: guest paths are searched first. With no original SOM
  the NewClass exports return NULL.
- LTSOMO10 is the LotusScript SOM/WPS extension (LSX). Its LSWPSClassMgr derives from
  WPClassManager; WPClassManagerNewClass(1,2) precedes its somBuildClass, and
  ltsRegisterWPSClasses calls the WPObject..WPDesktop NewClass entries with (0,0).
  L14WPD, WPCFLDR, WPCDOC and OVR*DOC are WPS class DLLs that normally run inside PMSHELL.

## Source state

Landed so far (unbuilt): `tools/lxapigen.py` data exports, `os2libs/os2native32.h` data
macros, `os2libs/pmwp.h`, `guest/bindings/pmwp.h`, the store API and constructors in
`os2libs/pmwp.c`, `os2libs/pmwp/{internal.h,som_tables.c,som_tokens.h}`. Class tables
come from `pmwp-classes.json` beside this note. Render output for all 25 modules
without data exports was compared in memory with HEAD's generator: 0 differences.
