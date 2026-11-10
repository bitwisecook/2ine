/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#ifndef TWOINE_EMULATOR_PROCESS_INTERNAL_H
#define TWOINE_EMULATOR_PROCESS_INTERNAL_H
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "emulator/runtime.h"
#include "host/process.h"
#include "host/thread.h"
#include "host/fs.h"
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

extern LX_PRIVATE LX_HOST_THREAD_LOCAL LxGuestTib *DefaultTib;
extern LX_PRIVATE LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;
LxHostThreadState *lxHostThreadState(void);

/* Accepted display settings, from configuration or the environment. */
enum
{
    LX_DEFAULT_DISPLAY_DPI = 96,
    LX_MIN_DISPLAY_DPI = 72,
    LX_MAX_DISPLAY_DPI = 384,
    LX_DEFAULT_DISPLAY_DEPTH = 24
};

LX_PRIVATE void initPib(void);
LX_PRIVATE void dosExit_lib2ine(uint32 action, uint32 result);
LX_PRIVATE void lxInitOs2Tib(uint8 *tibspace, void *_topOfStack, const size_t stacklen,
                             const uint32 tid);
LX_PRIVATE uint16 lxSetOs2Tib(uint8 *tibspace);
LX_PRIVATE LxGuestTib *lxGetOs2Tib(void);
LX_PRIVATE void lxDeinitOs2Tib(const uint16 selector);
LX_PRIVATE void *lxAllocSegment(uint16 *selector, const int iscode);
LX_PRIVATE void lxFreeSegment(const uint16 selector);
LX_PRIVATE int lxCreateCodeAlias(const uint16 selDS, uint16 *outselector);
LX_PRIVATE int lxFindSelector(const uint32 addr, uint16 *outselector, uint16 *outoffset,
                              int iscode);
LX_PRIVATE void lxFreeSelector(const uint16 selector);
LX_PRIVATE void *lxConvert1616to32(const uint32 addr1616);
LX_PRIVATE uint32 lxConvert32to1616(void *addr32);
LX_PRIVATE struct LxModule *loadModule_lib2ine(const char *modname);
LX_PRIVATE char *lxProcessMakeUnixPath(const char *os2path, uint32 *err);
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
