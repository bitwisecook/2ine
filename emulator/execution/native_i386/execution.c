/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* Backend state, lifetime, guest memory access and the LxExecutionOperations table. */
#define _GNU_SOURCE 1
#include "emulator/execution/native_i386/internal.h"

NativeState *Native;
pthread_mutex_t NativeDescriptorMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_mutex_t NativeSegmentMutex = PTHREAD_MUTEX_INITIALIZER;
/* Initial-exec TLS gives each host bridge stack a fixed GS-relative slot; guest FS remains private. */
__thread uint8 *NativeTib;
__thread void *NativeHostStack __attribute__((tls_model("initial-exec")));
__thread uint8_t *NativeHostAllocation;
__thread void *NativeSignalAllocation;
__thread stack_t NativePreviousSignalStack;

static int initialize(LxExecution *execution)
{
    if (Native)
        return lx_execution_fail(execution,
                                 "A native address space is already active in this worker");
    Native = calloc(1, sizeof *Native);
    if (!Native)
        return lx_execution_fail(execution, "Out of memory");
    execution->backend = Native;
    Native->services = execution->services;
    Native->execution = execution;
    Native->tib_gdt_entry = (unsigned int)-1;
    Native->ldt = calloc(LX_MAX_LDT_SLOTS, sizeof *Native->ldt);
    if (!Native->ldt)
    {
        free(Native);
        Native = NULL;
        execution->backend = NULL;
        return lx_execution_fail(execution, "Native descriptor allocation failed");
    }
    /* Probe USE16 descriptors because i386 compatibility alone does not enable them. */
    if (nativeLdt(LX_MAX_LDT_SLOTS - 2, 0x10000, 15, 1, 16, 1) != 0)
    {
        char message[256];
        snprintf(message, sizeof message, "Native 16-bit descriptor support unavailable: %s",
                 strerror(errno));
        free(Native->ldt);
        free(Native);
        Native = NULL;
        execution->backend = NULL;
        return lx_execution_fail(execution, message);
    }
    nativeLdt(LX_MAX_LDT_SLOTS - 2, 0, 0, 0, 16, 0);
    if (!nativeInstallSignalHandlers())
    {
        free(Native->ldt);
        free(Native);
        Native = NULL;
        execution->backend = NULL;
        return lx_execution_fail(execution, "Native fault initialization failed");
    }
    __asm__ __volatile__("movw %%cs, %0" : "=r"(Native->original_cs));
    __asm__ __volatile__("movw %%ds, %0" : "=r"(Native->original_ds));
    __asm__ __volatile__("movw %%es, %0" : "=r"(Native->original_es));
    __asm__ __volatile__("movw %%ss, %0" : "=r"(Native->original_ss));
    return 1;
}

static void destroy(LxExecution *execution)
{
    NativeState *state = execution->backend;
    sigaction(SIGSEGV, &state->previous_segv, NULL);
    for (uint16_t i = 1; i < LX_MAX_LDT_SLOTS; i++)
        if (state->ldt[i])
            nativeLdt(i, 0, 0, 0, 16, 0);
    free(state->ldt);
    free(state);
    Native = NULL;
}

static int atomicU32(LxExecution *e, uint32_t address, void *host, int exchange, uint32_t value,
                     uint32_t *previous)
{
    (void)e;
    (void)address;
    if (exchange == 2)
    {
        __asm__ __volatile__("lock xaddl %0,(%1)" : "+r"(value) : "r"(host) : "memory", "cc");
        *previous = value;
    }
    else if (exchange)
    {
        __asm__ __volatile__("xchgl %0,(%1)" : "+r"(value) : "r"(host) : "memory");
        *previous = value;
    }
    else
    {
        uint32_t old = 0, zero = 0;
        __asm__ __volatile__("lock cmpxchgl %2,(%1)"
                             : "+a"(old)
                             : "r"(host), "r"(zero)
                             : "memory", "cc");
        *previous = old;
    }
    return 1;
}

static int nativeEntry(LxExecution *e, void *host, uint32_t *address)
{
    (void)e;
    if (!host || !address)
        return 0;
    *address = (uint32_t)(uintptr_t)host;
    return 1;
}

static void copyOut(LxExecution *e, const void *host, void *output, size_t size)
{
    (void)e;
    memcpy(output, host, size);
}

static int copyIn(LxExecution *e, uint32_t address, void *host, const void *input, size_t size)
{
    (void)e;
    (void)address;
    memcpy(host, input, size);
    return 1;
}

const LxExecutionOperations lx_execution_native_linux = {"native",
                                                         initialize,
                                                         destroy,
                                                         nativeProgram,
                                                         nativeRunLibraryInitOrTerm,
                                                         nativePrepareThread16,
                                                         nativeRunThread16,
                                                         nativeDestroyThread16,
                                                         nativeCallback32,
                                                         nativeSetTib,
                                                         nativeGetTib,
                                                         nativeDeinitTib,
                                                         nativeAllocSegment,
                                                         nativeFreeSegment,
                                                         nativeCreateCodeAlias,
                                                         nativeFindSelector,
                                                         nativeFreeSelector,
                                                         nativeConvert1616to32,
                                                         nativeConvert32to1616,
                                                         nativeInitStackSegments,
                                                         nativeNearBase,
                                                         nativeFlatCodeSelector,
                                                         nativeFixupLinearCodeSegments,
                                                         nativeMissingEntry,
                                                         nativeEmitBridge16,
                                                         atomicU32,
                                                         nativeEntry,
                                                         1,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         nativeEmitBridge32,
                                                         copyOut,
                                                         nativePrepareThread32,
                                                         nativeRunThread32,
                                                         nativeDestroyThread32,
                                                         nativeEndThread,
                                                         copyIn,
                                                         nativeCallback4,
                                                         nativeApiArguments,
                                                         nativeApiRegisters32,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         nativeApiResume32,
                                                         nativeFlatDataSelector,
                                                         nativeGuestStack,
                                                         nativeCall16,
                                                         NULL,
                                                         NULL};
