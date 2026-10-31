/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#ifndef TWOINE_RUNTIME_CORE_INTERNAL_H
#define TWOINE_RUNTIME_CORE_INTERNAL_H
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "runtime/runtime.h"
#include "hal/host.h"
#include "hal/host_thread.h"
#include "hal/host_fs.h"
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

extern LX_PRIVATE LX_HOST_THREAD_LOCAL LxTIB *DefaultTib;
extern LX_PRIVATE LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;
LxHostThreadState *lxHostThreadState(void);
LX_PRIVATE void initPib(void);
LX_PRIVATE void dosExit_lib2ine(uint32 action, uint32 result);
LX_PRIVATE void initOs2Tib_lib2ine(uint8 *tibspace, void *_topOfStack, const size_t stacklen,
                                   const uint32 tid);
LX_PRIVATE uint16 setOs2Tib_lib2ine(uint8 *tibspace);
LX_PRIVATE LxTIB *getOs2Tib_lib2ine(void);
LX_PRIVATE void deinitOs2Tib_lib2ine(const uint16 selector);
LX_PRIVATE int findSelector_lib2ine(const uint32 addr, uint16 *outselector, uint16 *outoffset,
                                    int iscode);
LX_PRIVATE void freeSelector_lib2ine(const uint16 selector);
LX_PRIVATE void *convert1616to32_lib2ine(const uint32 addr1616);
LX_PRIVATE uint32 convert32to1616_lib2ine(void *addr32);
LX_PRIVATE LxModule *loadModule_lib2ine(const char *modname);
LX_PRIVATE char *makeUnixPath_lib2ine(const char *os2path, uint32 *err);
LX_PRIVATE void *allocSegment_lib2ine(uint16 *selector, const int iscode);
LX_PRIVATE void freeSegment_lib2ine(const uint16 selector);
LX_PRIVATE void __attribute__((noreturn)) terminate_lib2ine(const uint32 exitcode);
LX_PRIVATE void cfgWarn(const char *fname, const int lineno, const char *fmt, ...);
LX_PRIVATE int cfgIsSpace(const int ch);
LX_PRIVATE int cfgIsSymChar(const int ch);
LX_PRIVATE void cfgProcessBoolString(const char *fname, const int lineno, int *output,
                                     const char *val);
LX_PRIVATE void cfgProcessFloatString(const char *fname, const int lineno, float *output,
                                      const char *val);
LX_PRIVATE void cfgProcessMountPoint(const char *fname, const int lineno, const char *var,
                                     const char *val);
LX_PRIVATE void cfgProcessSystem(const char *fname, const int lineno, const char *var,
                                 const char *val);
LX_PRIVATE void cfgProcessLine(const char *fname, const int lineno, const char *category,
                               const char *var, const char *val);
LX_PRIVATE void cfgLoad(const char *fname);
LX_PRIVATE void cfgLoadFiles(void);
LX_PRIVATE void prepOs2Drives(void);
LX_PRIVATE void lib2ine_shutdown(void);
#endif
