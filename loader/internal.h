/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#ifndef TWOINE_LOADER_INTERNAL_H
#define TWOINE_LOADER_INTERNAL_H
#define _GNU_SOURCE 1
#include "runtime/runtime.h"
#include "formats/decompress.h"
#include "hal/host_fs.h"
#include "hal/host.h"
#include <stddef.h>
#include <errno.h>
#include <assert.h>

LX_PRIVATE int sanityCheckLxModule(const uint8 *exe, const uint32 exelen);
LX_PRIVATE int sanityCheckNeModule(const uint8 *exe, const uint32 exelen);
LX_PRIVATE int sanityCheckModule(uint8 **_exe, uint32 *_exelen, int *_is_lx);
LX_PRIVATE char *makeOS2Path(const char *fname);
LX_PRIVATE int locateOneElement(char *buf);
LX_PRIVATE int locatePathCaseInsensitive(char *buf);
LX_PRIVATE char *lxMakeUnixPath(const char *os2path, uint32 *err);

LX_PRIVATE uint16 lxSelectorToSegment(uint16 index);
LX_PRIVATE uint16 lxSetOs2Tib(uint8 *source);
LX_PRIVATE LxTIB *lxGetOs2Tib(void);
LX_PRIVATE void lxDeinitOs2Tib(uint16 token);
LX_PRIVATE void *lxAllocSegment(uint16 *s, int x);
LX_PRIVATE void lxFreeSegment(uint16 s);
LX_PRIVATE int lxCreateCodeAlias(uint16 s, uint16 *out);
LX_PRIVATE int lxFindSelector(uint32 a, uint16 *s, uint16 *o, int x);
LX_PRIVATE void lxFreeSelector(uint16 s);
LX_PRIVATE void *lxConvert1616to32(uint32 p);
LX_PRIVATE uint32 lxConvert32to1616(void *p);
LX_PRIVATE void initOs2StackSegments(uint32 a, uint32 n, int release);
LX_PRIVATE void fixupLinearCodeSegmentReferences(uint8 *p, size_t n);
LX_PRIVATE void *generateMissingTrampoline(const char *m, const char *n);
LX_PRIVATE void *generateMissingTrampoline16(const char *m, const char *n, const LxExport **out);
LX_PRIVATE uint32 guestEntryAddress(void *host);
LX_PRIVATE LxExecutionEntry executionEntry(const LxModule *module);
LX_PRIVATE void runModule(LxModule *module);
LX_PRIVATE void runLxLibraryInitOrTerm(LxModule *module, int termination);
LX_PRIVATE __attribute__((noreturn)) void lxTerminate(const uint32 exitcode);
LX_PRIVATE void runLxLibraryInit(LxModule *lxmod);
LX_PRIVATE void runLxLibraryTerm(LxModule *lxmod);
LX_PRIVATE void runNeLibraryInit(LxModule *lxmod);
LX_PRIVATE void runNeLibraryTerm(LxModule *lxmod);
LX_PRIVATE void runLibraryTerm(LxModule *lxmod);
LX_PRIVATE void freeLxModule(LxModule *lxmod);
LX_PRIVATE void destroyLxModule(void *resource);
LX_PRIVATE void *getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal,
                                            const LxExport **_lxexp, const int want16bit,
                                            const int native);
LX_PRIVATE void *getModuleProcAddrByName(const LxModule *module, const char *name,
                                         const LxExport **_lxexp, const int want16bit);
LX_PRIVATE void doFixup(uint8 *page, const sint16 offset, const uint32 finalval,
                        const uint16 finalval2, const uint32 finalsize);
LX_PRIVATE void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj,
                          const uint32 pagenum, uint8 *page);
LX_PRIVATE int loadLxNameTable(LxModule *lxmod, const uint8 *name_table);
LX_PRIVATE void dumpLoadedModule(const LxModule *lxmod);
LX_PRIVATE LxModule *loadLxModule(const char *fname, const uint8 *origexe, uint8 *exe,
                                  uint32 exelen, int dependency_tree_depth);
LX_PRIVATE int fixupNeSegment(const NeSegmentTableEntry *seg, LxModule *lxmod, uint8 *dst,
                              const uint32 sector_shift, const uint8 *origexe, uint8 *exe,
                              uint32 exelen);
LX_PRIVATE LxModule *loadNeModule(const char *fname, const uint8 *origexe, uint8 *exe,
                                  uint32 exelen, int dependency_tree_depth);
LX_PRIVATE LxModule *loadModule(const char *fname, uint8 *exe, uint32 exelen,
                                int dependency_tree_depth);
LX_PRIVATE LxModule *loadModuleByPathInternal(const char *fname, const int dependency_tree_depth);
LX_PRIVATE LxModule *loadModuleByPath(const char *fname);
LX_PRIVATE void registerLxModule(LxModule *module);
LX_PRIVATE void unloadAllModules(void);
LX_PRIVATE LxModule *loadNativeModule(const char *fname, const char *modname);
LX_PRIVATE LxModule *loadModuleByModuleNameInternal(const char *modname,
                                                    const int dependency_tree_depth);
LX_PRIVATE LxModule *loadModuleByModuleName(const char *modname);
LX_PRIVATE LxModule *loadModuleByPathOrModuleName(const char *modname);
LX_PRIVATE int describeModuleAddress(const void *ptr, char *buf, const size_t buflen);
LX_PRIVATE int executableAt(void *unused, uint32 address);
LX_PRIVATE int describeExecutionAddress(void *unused, uint32 address, char *buffer, size_t size);
LX_PRIVATE void executionReturned(void *unused, uint32 result);
#endif
