/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* Declarations shared by the native i386 backend's sources. Everything here
   has hidden visibility: it is private to the module that links the backend. */
#ifndef TWOINE_EMULATOR_EXECUTION_NATIVE_I386_INTERNAL_H
#define TWOINE_EMULATOR_EXECUTION_NATIVE_I386_INTERNAL_H
#include "emulator/execution/core/backend.h"
#include "host/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <sys/syscall.h>
#include <asm/ldt.h>
#include <signal.h>
#include <ucontext.h>

#pragma GCC visibility push(hidden)

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef unsigned int uint;
#define LX_MAX_LDT_SLOTS 8192
#define FIXME(x) ((void)0)

typedef struct NativeState
{
    uint32 *ldt;
    LxExecution *execution;
    LxGuestMapping *segments[LX_MAX_LDT_SLOTS];
    uint32 near16_base;
    uint16 original_cs, original_ds, original_es, original_ss;
    uint16 call16_return;
    unsigned int tib_gdt_entry;
    LxExecutionServices services;
    struct sigaction previous_segv;
} NativeState;

extern NativeState *Native;
extern pthread_mutex_t NativeDescriptorMutex;
extern pthread_mutex_t NativeSegmentMutex;
extern __thread uint8 *NativeTib;
/* Initial-exec TLS gives each host bridge stack a fixed GS-relative slot. */
extern __thread void *NativeHostStack __attribute__((tls_model("initial-exec")));
extern __thread uint8_t *NativeHostAllocation;
extern __thread void *NativeSignalAllocation;
extern __thread stack_t NativePreviousSignalStack;

static inline uint16 lxSelectorToSegment(const uint16 selector)
{
    return (selector << 3) | 7;
}

/* selectors.c: LDT descriptors, selectors, segments and the TIB. */
void *nativeAllocSegment(uint16 *selector, const int iscode);
void *nativeConvert1616to32(const uint32 addr1616);
uint32 nativeConvert32to1616(void *addr32);
int nativeCreateCodeAlias(const uint16 selector, uint16 *output);
void nativeDeinitTib(const uint16 selector);
int nativeFindSelector(const uint32 address, uint16 *selector, uint16 *offset, int code);
void nativeFreeSegment(const uint16 selector);
void nativeFreeSelector(const uint16 selector);
void *nativeGetTib(void);
void nativeInitStackSegments(uint32 address, uint32 size, const int release);
int nativeLdt(uint16_t index, uint32_t base, uint32_t limit_pages, int code, int bits, int present);
int nativeProtect(void *address, size_t size, unsigned protection);
uint16 nativeSetTib(uint8 *tibspace);

/* entry.c: Program, NE and DLL entry, missing-import trampolines. */
void nativeFixupLinearCodeSegments(uint8 *addr, size_t size);
void *nativeMissingTrampoline(const char *_module, const char *_entry);
void *nativeMissingTrampoline16(const char *_module, const char *_entry,
                                LxExecutionBridge *binding);
void nativeRunLibraryInitOrTerm(const LxExecutionEntry *entry, const int isTermination);
__attribute__((noreturn)) void nativeRunLxModule(const LxExecutionEntry *entry);
__attribute__((noreturn)) void nativeRunNeModule(const LxExecutionEntry *entry);

/* faults.c: Guest fault emulation and the SIGSEGV handler. */
int nativeInstallSignalHandlers(void);

/* bridges.c: 16/32-bit bridges, threads, callbacks and call16. */
uint32_t nativeApiArguments(void);
int nativeApiRegisters32(LxExecutionRegisters32 *out);
int nativeApiResume32(const LxExecutionRegisters32 *registers);
int nativeCall16(uint32_t target, uint16_t ss, uint16_t top, const void *arguments, uint32_t bytes,
                 uint32_t *result);
void nativeCallback32(uint32_t entry, uint32_t argument);
uint32_t nativeCallback4(uint32_t entry, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void nativeDestroyThread16(void *invocation);
void nativeDestroyThread32(void *invocation);
size_t nativeEmitBridge16(uint8_t *output, size_t capacity, uint8_t *segment_base,
                          uint16_t selector_index, LxExecutionApi16 callback,
                          uint16_t argument_bytes);
size_t nativeEmitBridge32(uint8_t *output, size_t capacity, const LxExecutionApi32 *binding);
int nativeEndThread(void);
uint16_t nativeFlatCodeSelector(void);
uint16_t nativeFlatDataSelector(void);
uint32_t nativeGuestStack(void);
void *nativeMissingEntry(const char *module, const char *entry, unsigned bits,
                         LxExecutionBridge *binding);
void nativeNearBase(uint32_t address);
void *nativePrepareThread16(uint32_t entry, uint32_t stack);
void *nativePrepareThread32(uint32_t entry, uint32_t stack, uint32_t argument);
void nativeProgram(const LxExecutionEntry *entry, unsigned bits);
void nativeRunThread16(void *invocation);
void nativeRunThread32(void *invocation);

#pragma GCC visibility pop
#endif
