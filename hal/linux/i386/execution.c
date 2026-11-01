/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
#define _GNU_SOURCE 1
#include "runtime/execution_internal.h"
#include "hal/host.h"
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

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef unsigned int uint;
#define LX_MAX_LDT_SLOTS 8192
#define FIXME(x) ((void)0)
static int nativeLdt(uint16_t index, uint32_t base, uint32_t limit_pages, int code, int bits,
                     int present)
{
    struct user_desc entry = {0};
    entry.entry_number = index;
    if (!present)
    {
        entry.read_exec_only = 1;
        entry.seg_not_present = 1;
        return syscall(SYS_modify_ldt, 1, &entry, sizeof entry) == 0 ? 0 : -1;
    }
    entry.base_addr = base;
    entry.limit = limit_pages;
    entry.seg_32bit = bits == 32;
    entry.contents = code ? MODIFY_LDT_CONTENTS_CODE : MODIFY_LDT_CONTENTS_DATA;
    entry.limit_in_pages = 1;
    entry.seg_not_present = !present;
    entry.useable = 1;
    return syscall(SYS_modify_ldt, 1, &entry, sizeof entry) == 0 ? 0 : -1;
}
static int nativeProtect(void *address, size_t size, unsigned protection)
{
    return lx_host_vm_protect(address, size, protection) ? 0 : -1;
}
typedef struct NativeState
{
    uint32 *ldt;
    LxExecution *execution;
    LxGuestMapping *segments[LX_MAX_LDT_SLOTS];
    uint32 near16_base;
    uint16 original_cs, original_ds, original_es, original_ss;
    unsigned int tib_gdt_entry;
    LxExecutionServices services;
    struct sigaction previous_segv;
} NativeState;
static NativeState *Native;
static pthread_mutex_t DescriptorMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutex_t SegmentMutex = PTHREAD_MUTEX_INITIALIZER;
/* Initial-exec TLS gives each host bridge stack a fixed GS-relative slot; guest FS remains private. */
static __thread uint8 *NativeTib;
static __thread void *NativeHostStack __attribute__((tls_model("initial-exec")));
static __thread uint8_t *NativeHostAllocation;
static __thread void *NativeSignalAllocation;
static __thread stack_t NativePreviousSignalStack;
typedef struct NativeBridgeFrame
{
    uint32_t guest_stack;
    struct NativeBridgeFrame *outer;
} NativeBridgeFrame;
static __thread NativeBridgeFrame *NativeBridge;
static uint32_t dispatchApi16(uint8_t *arguments, LxExecutionApi16 callback, uint32_t guest_stack)
{
    NativeBridgeFrame frame = {guest_stack, NativeBridge};
    NativeBridge = &frame;
    uint32_t result = callback(arguments);
    NativeBridge = frame.outer;
    return result;
}
static uint32 nativeHostStackOffset(void)
{
    return (uint32)((uintptr_t)&NativeHostStack - (uintptr_t)__builtin_thread_pointer());
}
static __attribute__((noreturn, used)) void endLxProcess(uint32 result)
{
    Native->services.program_returned(Native->services.userdata, result);
    abort();
}
static inline uint16 lxSelectorToSegment(const uint16 selector)
{
    return (selector << 3) | 7;
}

static int allocateSelector(const uint16 selector, const int pages, const uint32 addr,
                            const unsigned int contents, const int is32bit)
{
    assert(selector < LX_MAX_LDT_SLOTS);

    if (Native->ldt[selector])
        return 0;

    if (nativeLdt(selector, addr, (uint32)pages - 1, contents == MODIFY_LDT_CONTENTS_CODE,
                  is32bit ? 32 : 16, 1) != 0)
        return 0;

    Native->ldt[selector] = addr;
    return 1;
}

#define LX_NEAR16_SENTINEL_SELECTOR 0x1FFF

static int lxFindSelectorUnlocked(const uint32 _addr, uint16 *outselector, uint16 *outoffset,
                                  int iscode)
{
    uint32 addr = _addr;
    if (addr < 4096)
        return 0;

    const uint32 *ldt = Native->ldt;

    int available = -1;
    int preferred = -1;

    /* Tiled selectors encode a 64-KiB-aligned base directly. */
    if (addr < (1024 * 1024 * 512))
    {
        const uint16 idx = (uint16)(addr >> 16);
        const uint32 tile = ldt[idx];
        if ((tile == 0) && (idx != LX_NEAR16_SENTINEL_SELECTOR))
        {
            preferred = available = idx;
        }
        else if ((tile != 0) && (idx != LX_NEAR16_SENTINEL_SELECTOR) && (tile <= addr) &&
                 ((tile + 0x10000) > addr))
        {
            *outselector = idx;
            *outoffset = (uint16)(addr - tile);

            return 1;
        }
    }

    for (int i = 0; i < LX_MAX_LDT_SLOTS; i++)
    {
        if (i == LX_NEAR16_SENTINEL_SELECTOR)
            continue; // reserved for the Microsoft 16-bit DS sentinel.
        const uint32 tile = ldt[i];
        if (tile == 0)
            available = i;
        else if ((tile <= addr) && ((tile + 0x10000) > addr))
        {
            *outselector = (uint16)i;
            *outoffset = (uint16)(addr - tile);

            return 1;
        }
    }

    if (available == -1)
    {
        fprintf(stderr, "Uhoh, we've run out of LDT selectors! Probably about to crash...\n");
        fflush(stderr);
        return 0;
    }

    /* Descriptor type follows the mapped object permissions. */
    if (iscode == -1 && Native->services.query_executable)
        iscode = Native->services.query_executable(Native->services.userdata, addr);

    if (iscode == -1)
    {
        iscode = 0;
    }

    const uint32 diff = addr % 0x10000;
    addr -= diff;

    const uint16 selector = (uint16)(preferred != -1) ? preferred : available;

    if (!allocateSelector(selector, 16, addr,
                          iscode ? MODIFY_LDT_CONTENTS_CODE : MODIFY_LDT_CONTENTS_DATA, 0))
    {
        fprintf(stderr,
                "Uhoh, we've failed to allocate LDT selector %u! Probably about to crash...\n",
                (uint)selector);
        fflush(stderr);
        return 0;
    }

    *outselector = selector;
    *outoffset = (uint16)diff;
    return 1;
}
static int lxFindSelector(const uint32 address, uint16 *selector, uint16 *offset, int code)
{
    pthread_mutex_lock(&DescriptorMutex);
    int result = lxFindSelectorUnlocked(address, selector, offset, code);
    pthread_mutex_unlock(&DescriptorMutex);
    return result;
}

static void lxFreeSelectorUnlocked(const uint16 selector)
{
    assert(selector < LX_MAX_LDT_SLOTS);

    if (!Native->ldt[selector])
        return;

    if (nativeLdt(selector, 0, 0, 0, 16, 0) != 0)
        return;

    Native->ldt[selector] = 0;
}
static void lxFreeSelector(const uint16 selector)
{
    pthread_mutex_lock(&DescriptorMutex);
    lxFreeSelectorUnlocked(selector);
    pthread_mutex_unlock(&DescriptorMutex);
}

static void *lxConvert1616to32(const uint32 addr1616)
{
    if (addr1616 == 0)
        return NULL;

    const uint16 selector =
        (uint16)(addr1616 >> 19); // slide segment down, and shift out control bits.
    const uint16 offset =
        (uint16)(addr1616 %
                 0x10000); /* LDT bases are 64-KiB aligned; selector 0xFFFF means the Microsoft runtime DS base. */
    if ((selector == 0x1FFF) && Native->near16_base)
        return (void *)(size_t)(Native->near16_base + offset);

    if (!Native->ldt[selector])
        return NULL;
    return lx_execution_memory(Native->execution, Native->ldt[selector] + offset, 1, 0);
}

static uint32 lxConvert32to1616(void *addr32)
{
    if (addr32 == NULL)
        return 0;

    uint16 selector = 0;
    uint16 offset = 0;
    if (!lxFindSelector((uint32)addr32, &selector, &offset, -1))
    {
        fprintf(stderr, "Uhoh, ran out of LDT entries?!\n");
        return 0;
    }

    return (((uint32)lxSelectorToSegment(selector)) << 16) | ((uint32)offset);
}

static inline void *lxConvertSegmentOffsetto32(const uint16 seg, const uint16 off)
{
    return lxConvert1616to32((((uint32)seg) << 16) | ((uint32)off));
}

static void initOs2StackSegmentsUnlocked(uint32 addr, uint32 stacklen, const int deinit)
{

    const uint32 diff = addr % 0x10000;
    addr -= diff;
    stacklen += diff;

    /* EMX expects every stack tile to have an LDT descriptor. */
    if (stacklen % 0x10000)
        stacklen += 0x10000 - (stacklen % 0x10000);

    while (stacklen)
    {

        if (deinit)
        {
            lxFreeSelector((uint16)(addr >> 16));
        }
        else
        {
            if (!allocateSelector((uint16)(addr >> 16), 16, addr, MODIFY_LDT_CONTENTS_DATA, 0))
            {
                FIXME("uhoh, couldn't set up an LDT entry for a stack segment! Might crash later!");
            }
        }
        stacklen -= 0x10000;
        addr += 0x10000;
    }
}
static void initOs2StackSegments(uint32 address, uint32 size, const int release)
{
    pthread_mutex_lock(&DescriptorMutex);
    initOs2StackSegmentsUnlocked(address, size, release);
    pthread_mutex_unlock(&DescriptorMutex);
}

/* FS:0 addresses this thread's guest TIB; attach from the entering host thread. */
static uint16 lxSetOs2Tib(uint8 *tibspace)
{
    if (sigaltstack(NULL, &NativePreviousSignalStack))
        abort();
    if (NativePreviousSignalStack.ss_flags & SS_DISABLE)
    {
        size_t size = SIGSTKSZ > 65536 ? SIGSTKSZ : 65536;
        NativeSignalAllocation = malloc(size);
        stack_t signals = {.ss_sp = NativeSignalAllocation, .ss_size = size};
        if (!NativeSignalAllocation || sigaltstack(&signals, NULL))
            abort();
    }
    void *stack = malloc(65536);
    if (!stack)
    {
        fprintf(stderr, "Could not allocate native bridge stack\n");
        abort();
    }
    NativeHostAllocation = stack;
    NativeHostStack = (uint8 *)stack + 65536;
    NativeTib = tibspace;
    struct user_desc entry = {0};
    /* Linux copies the parent's three GDT TLS descriptors into each child.
       Reuse our reserved index in every task instead of allocating another
       inherited slot; glibc's GS descriptor remains separate. */
    pthread_mutex_lock(&DescriptorMutex);
    entry.entry_number = Native->tib_gdt_entry;
    entry.base_addr = (unsigned int)((size_t)tibspace);
    entry.limit = Native->services.tib_size;
    entry.seg_32bit = 1;
    entry.contents = MODIFY_LDT_CONTENTS_DATA;
    entry.read_exec_only = 0;
    entry.limit_in_pages = 0;
    entry.seg_not_present = 0;
    entry.useable = 1;
    const long rc = syscall(SYS_set_thread_area, &entry);
    if (rc != 0)
    {
        perror("set_thread_area");
        abort();
    }

    Native->tib_gdt_entry = entry.entry_number;
    pthread_mutex_unlock(&DescriptorMutex);

    /* TI=0 and RPL=3 select a ring-3 GDT entry; LDT entries use TI=1. */
    const unsigned int segment = (entry.entry_number << 3) | 3;
    __asm__ __volatile__("movw %%ax, %%fs  \n\t" : : "a"(segment));
    return (uint16)entry.entry_number;
}

static void *lxGetOs2Tib2(void)
{
    return NativeTib + Native->services.tib2_offset;
}
static void *lxGetOs2Tib(void)
{
    return NativeTib;
}

static void lxDeinitOs2Tib(const uint16 selector)
{
    struct user_desc entry;
    memset(&entry, '\0', sizeof(entry));
    entry.entry_number = selector;
    entry.read_exec_only = 1;
    entry.seg_not_present = 1;

    const long rc = syscall(SYS_set_thread_area, &entry);
    if (rc != 0)
    {
        perror("set_thread_area");
        abort();
    }
    free(NativeHostAllocation);
    NativeHostAllocation = NULL;
    NativeHostStack = NULL;
    NativeTib = NULL;
    if (NativeSignalAllocation)
    {
        if (sigaltstack(&NativePreviousSignalStack, NULL))
            abort();
        free(NativeSignalAllocation);
        NativeSignalAllocation = NULL;
    }
}

static int lxCreateCodeAliasUnlocked(const uint16 selDS, uint16 *outselector)
{
    const uint32 base = Native->ldt[selDS >> 3];
    if (!base)
        return 0;

    uint16 slot = 0;
    for (uint16 i = 1; i < LX_MAX_LDT_SLOTS; i++)
    {
        if (!Native->ldt[i])
        {
            slot = i;
            break;
        }
    }
    if (!slot)
        return 0;

    if (!allocateSelector(slot, 16, base, MODIFY_LDT_CONTENTS_CODE, 0))
        return 0;

    /* A CS alias executes bytes written through the shared data mapping. */
    LxGuestMapping *mapping = lx_execution_mapping_at(Native->execution, base);
    if (!mapping || !lx_execution_protect(Native->execution, mapping, 7))
    {
        lxFreeSelector(slot);
        return 0;
    }

    *outselector = (uint16)((slot << 3) | 7);
    return 1;
}
static int lxCreateCodeAlias(const uint16 selector, uint16 *output)
{
    pthread_mutex_lock(&DescriptorMutex);
    int result = lxCreateCodeAliasUnlocked(selector, output);
    pthread_mutex_unlock(&DescriptorMutex);
    return result;
}

static void *lxAllocSegment(uint16 *selector, const int iscode)
{
    /* Keep USE16 allocations below 512 MiB so tiled selectors convert directly to linear addresses. */
    static size_t baseaddr = 136 * 1024 * 1024;
    const uint32 segmentsize = 0x10000;
    *selector = 0xFFFF;
    pthread_mutex_lock(&SegmentMutex);
    while (baseaddr < 512 * 1024 * 1024)
    {
        const size_t candidate = baseaddr;
        baseaddr += segmentsize;
        LxGuestMapping *mapping =
            lx_execution_map(Native->execution, (uint32)candidate, segmentsize, 7, 1);
        if (!mapping)
            continue;
        void *segment = lx_execution_mapping_host(mapping);
        uint16 offset = 0xFFFF;
        if (lxFindSelector((uint32)segment, selector, &offset, iscode) && offset == 0)
        {
            Native->segments[*selector] = mapping;
            pthread_mutex_unlock(&SegmentMutex);
            return segment;
        }
        lx_execution_unmap(Native->execution, mapping);
    }
    pthread_mutex_unlock(&SegmentMutex);
    fprintf(stderr, "Failed to allocate a low 16-bit segment.\n");
    return NULL;
}

static void lxFreeSegment(const uint16 selector)
{
    if (selector >= LX_MAX_LDT_SLOTS)
        return;
    pthread_mutex_lock(&SegmentMutex);
    LxGuestMapping *mapping = Native->segments[selector];
    if (mapping && lx_execution_unmap(Native->execution, mapping))
    {
        Native->segments[selector] = NULL;
        lxFreeSelector(selector);
    }
    pthread_mutex_unlock(&SegmentMutex);
}

static void missingEntryPointCalled(const char *module, const char *entry)
{
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\n\nMissing entry point '%s' in module '%s'! Aborting.\n\n\n", entry, module);

    fflush(stderr);
    endLxProcess(1);
}

static void *generateMissingTrampoline(const char *_module, const char *_entry)
{
    static void *page = NULL;
    static uint32 pageused = 0;
    static uint32 pagesize = 0;

    if (pagesize == 0)
        pagesize = lx_host_page_size();

    if ((!page) || ((pagesize - pageused) < 32))
    {
        if (page)
            nativeProtect(page, pagesize, LX_HOST_READ | LX_HOST_EXECUTE);
        page = lx_host_vm_map(NULL, pagesize, LX_HOST_READ | LX_HOST_WRITE | LX_HOST_EXECUTE, 0);
        pageused = 0;
    }

    void *trampoline = page + pageused;
    char *ptr = (char *)trampoline;
    char *module = strdup(_module);
    char *entry = strdup(_entry);

    *(ptr++) = 0x55; // pushl %ebp
    *(ptr++) = 0x89; // movl %esp,%ebp
    *(ptr++) = 0xE5;
    *(ptr++) = 0x68; // pushl immediate
    memcpy(ptr, &entry, sizeof(char *));
    ptr += sizeof(uint32);
    *(ptr++) = 0x68; // pushl immediate
    memcpy(ptr, &module, sizeof(char *));
    ptr += sizeof(uint32);
    *(ptr++) = 0xB8; // movl immediate to %eax
    const void *fn = missingEntryPointCalled;
    memcpy(ptr, &fn, sizeof(void *));
    ptr += sizeof(void *);
    *(ptr++) = 0xFF; // call absolute in %eax.
    *(ptr++) = 0xD0;

    const uint32 trampoline_len = (uint32)(ptr - ((char *)trampoline));
    assert(trampoline_len <= 32);
    pageused += trampoline_len;

    if (pageused % 4) // keep these aligned to 32 bits.
        pageused += (4 - (pageused % 4));

    return trampoline;
}

static void *generateMissingTrampoline16(const char *_module, const char *_entry,
                                         LxExecutionBridge *binding)
{
    static void *segment = NULL;
    static uint32 segmentused = 0;
    const uint32 segmentsize = 0x10000;
    static uint16 selector = 0xFFFF;

    if ((!segment) || ((segmentsize - segmentused) < 64))
    {
        if (segment)
            nativeProtect(segment, segmentsize, LX_HOST_READ | LX_HOST_EXECUTE);
        segment = lxAllocSegment(&selector, 1);
        assert(segment != NULL);
        assert(selector != 0xFFFF);
        segmentused = 0;
    }

    void *trampoline = segment + segmentused;
    char *ptr = (char *)trampoline;
    char *module = strdup(_module);
    char *entry = strdup(_entry);
    if (getenv("TWOINE_TRACE_FIXUPS"))
        fprintf(stderr, "2INE MISSING16: %s@%s -> %p (sel %04X)\n", _module, _entry, trampoline,
                (uint)selector);

    *(ptr++) = 0x89; /* mov cx,sp... */
    *(ptr++) = 0xE1;
    *(ptr++) = 0x66; /* jmp dword 0x7788:0x33332222... */
    *(ptr++) = 0xEA;
    const uint32 jmp32addr = (uint32)(ptr + 6);
    memcpy(ptr, &jmp32addr, 4);
    ptr += 4;
    memcpy(ptr, &Native->original_cs, 2);
    ptr += 2;

    *(ptr++) = 0x66; /* mov ax,ss... */
    *(ptr++) = 0x8C;
    *(ptr++) = 0xD0;
    *(ptr++) = 0x66; /* shr ax,byte 0x3... */
    *(ptr++) = 0xC1;
    *(ptr++) = 0xE8;
    *(ptr++) = 0x03;
    *(ptr++) = 0xC1; /* shl eax,byte 0x10... */
    *(ptr++) = 0xE0;
    *(ptr++) = 0x10;
    *(ptr++) = 0x66; /* mov ax,cx... */
    *(ptr++) = 0x89;
    *(ptr++) = 0xC8;
    *(ptr++) = 0x66; /* mov cx,0xabcd... */
    *(ptr++) = 0xB9;
    memcpy(ptr, &Native->original_ss, 2);
    ptr += 2;
    *(ptr++) = 0x8E; /* mov ss,ecx... */
    *(ptr++) = 0xD1;
    *(ptr++) = 0x89; /* mov esp,eax... */
    *(ptr++) = 0xC4;

    if (Native->original_ss != Native->original_ds)
    {
        *(ptr++) = 0x66; /* mov cx,0x8888... */
        *(ptr++) = 0xB9;
        memcpy(ptr, &Native->original_ds, 2);
        ptr += 2;
    }
    *(ptr++) = 0x8E; /* mov ds,ecx... */
    *(ptr++) = 0xD9;

    if (Native->original_es != Native->original_ds)
    {
        *(ptr++) = 0x66; /* mov cx,0x8888... */
        *(ptr++) = 0xB9;
        memcpy(ptr, &Native->original_es, 2);
        ptr += 2;
    }
    *(ptr++) = 0x8E; /* mov es,ecx... */
    *(ptr++) = 0xC1;

    // okay, CPU is in a sane state again, call the trampoline. Don't bother cleaning up.
    *(ptr++) = 0x68; // pushl immediate
    memcpy(ptr, &entry, sizeof(char *));
    ptr += sizeof(uint32);
    *(ptr++) = 0x68; // pushl immediate
    memcpy(ptr, &module, sizeof(char *));
    ptr += sizeof(uint32);
    *(ptr++) = 0xB8; // movl immediate to %eax

    const void *fn = missingEntryPointCalled;
    memcpy(ptr, &fn, sizeof(void *));
    ptr += sizeof(void *);
    *(ptr++) = 0xFF; // call absolute in %eax.
    *(ptr++) = 0xD0;

    const uint32 trampoline_len = (uint32)(ptr - ((char *)trampoline));
    assert(trampoline_len <= 64);
    segmentused += trampoline_len;

    if (segmentused % 4) // keep these aligned to 32 bits.
        segmentused += (4 - (segmentused % 4));

    binding->host = trampoline;
    binding->address = (uint32)(uintptr_t)trampoline;
    binding->segment_base = (uint32)(uintptr_t)segment;
    binding->size = 0x10000;
    binding->selector_index = selector;

    return trampoline;
}

static __attribute__((noreturn)) void runLxModule(const LxExecutionEntry *entry)
{
    uint8 *stack = (uint8 *)((size_t)entry->stack);

    __asm__ __volatile__(
        "movl %%esi,%%esp  \n\t"
        "pushl %%eax       \n\t"
        "pushl %%ecx       \n\t"
        "pushl $0          \n\t"
        "pushl %%edx       \n\t" // module handle
        "leal 1f,%%eax     \n\t"
        "pushl %%eax       \n\t"
        "pushl %%edi       \n\t" // the OS/2 process entry point (we'll "ret" to it instead of jmp, so stack and registers are all correct).
        "xorl %%eax,%%eax  \n\t"
        "xorl %%ebx,%%ebx  \n\t"
        "xorl %%ecx,%%ecx  \n\t"
        "xorl %%edx,%%edx  \n\t"
        "xorl %%esi,%%esi  \n\t"
        "xorl %%edi,%%edi  \n\t"
        "xorl %%ebp,%%ebp  \n\t"
        "ret               \n\t"
        "1:                \n\t"
        "pushl %%eax       \n\t" // push exit code from OS/2 app.
        "call endLxProcess \n\t"
        :
        : "a"((void *)(uintptr_t)entry->command), "c"((void *)(uintptr_t)entry->environment),
          "d"(entry->module), "S"(stack), "D"(entry->entry)
        : "memory");

    (void)endLxProcess;

    __builtin_unreachable();
}

static __attribute__((noreturn)) void runNeModule(const LxExecutionEntry *entry)
{

    /* NE startup supplies both the stack ABI and AX/BX/CX runtime arguments. */
    const uint16 stacksize = (uint16)entry->stack_bytes;
    const uint16 ss = (entry->stack >> 16) & 0xFFFF; // stack segment
    const uint32 cmd1616 = lxConvert32to1616((void *)(uintptr_t)entry->command);
    const uint16 cmdlineoffset = cmd1616 & 0xFFFF;
    const uint16 envseg = (cmd1616 >> 16) & 0xFFFF;

    uint8 *stack = (uint8 *)lxConvert1616to32(entry->stack);

    stack -= 2;
    *((uint16 *)stack) = cmdlineoffset; // cmdline offset
    stack -= 2;
    *((uint16 *)stack) = envseg; // env segment
    stack -= 2;
    *((uint16 *)stack) = entry->stack & 0xFFFF; // top of stack
    stack -= 2;
    *((uint16 *)stack) = ss;
    stack -= 2;
    *((uint16 *)stack) = (entry->stack & 0xFFFF) - stacksize; // bottom of stack
    stack -= 2;
    *((uint16 *)stack) = ss; // bottom of stack

    uint16 selector = 0xFFFF;
    void *segment = lxAllocSegment(&selector, 1);
    assert(segment != NULL);
    assert(selector != 0xFFFF);
    char *ptr = (char *)segment;

    /* NE startup stack (low addresses first):
     SS:SP -> stack-bottom segment, offset
              stack-top segment, offset
              environment segment, command offset
     AX=environment, BX=command offset, CX=autodata size. */

    *(ptr++) = 0x66; /* jmp word 0xaaaa:0xbbbb... */
    *(ptr++) = 0xEA;
    const uint16 jmp16offset = 6;
    memcpy(ptr, &jmp16offset, 2);
    ptr += 2;
    const uint16 jmp16segment = lxSelectorToSegment(selector);
    memcpy(ptr, &jmp16segment, 2);
    ptr += 2;

    *(ptr++) = 0xB8; /* mov ax,0x1111... */
    memcpy(ptr, &ss, 2);
    ptr += 2;
    *(ptr++) = 0x8E; /* mov ss,ax... */
    *(ptr++) = 0xD0;
    *(ptr++) = 0xBC;                                      /* mov sp,0x2222... */
    const uint16 sp = (uint16)(((size_t)stack) & 0xFFFF); // top of stack offset
    memcpy(ptr, &sp, 2);
    ptr += 2;
    *(ptr++) = 0xB8;                                                    /* mov ax,0x3333... */
    const uint16 ds = entry->data_selector ? entry->data_selector : ss; // data segment
    memcpy(ptr, &ds, 2);
    ptr += 2;
    *(ptr++) = 0x8E; /* mov ds,ax... */
    *(ptr++) = 0xD8;
    *(ptr++) = 0x31; /* xor ax,ax... */
    *(ptr++) = 0xC0;
    *(ptr++) = 0x8E; /* mov es,ax... */
    *(ptr++) = 0xC0;
    *(ptr++) = 0xB8; /* mov ax,0x4444... */
    memcpy(ptr, &envseg, 2);
    ptr += 2;
    *(ptr++) = 0xBB; /* mov bx,0x5555... */
    memcpy(ptr, &cmdlineoffset, 2);
    ptr += 2;
    *(ptr++) = 0xB9; /* mov cx,0x6666... */
    const uint16 autodatasize = entry->data_bytes;
    memcpy(ptr, &autodatasize, 2);
    ptr += 2;
    *(ptr++) = 0x31; /* xor dx,dx... */
    *(ptr++) = 0xD2;
    *(ptr++) = 0x31; /* xor si,si... */
    *(ptr++) = 0xF6;
    *(ptr++) = 0x31; /* xor di,di... */
    *(ptr++) = 0xFF;
    *(ptr++) = 0x31; /* xor bp,bp... */
    *(ptr++) = 0xED;
    *(ptr++) = 0xEA; /* jmp 0x6666:0x7777... */
    const uint16 ip = (uint16)(entry->entry & 0xFFFF);
    memcpy(ptr, &ip, 2);
    ptr += 2;
    const uint16 cs = (uint16)((entry->entry >> 16) & 0xFFFF);
    memcpy(ptr, &cs, 2);
    ptr += 2;
    *(ptr++) = 0xCD; /* int 0x3... */
    *(ptr++) = 0x03;

    __asm__ __volatile__("jmp *%%eax\n\t" : : "a"(segment) : "memory");

    __builtin_unreachable();
}

static void runLxLibraryInitOrTerm(const LxExecutionEntry *entry, const int isTermination)
{
    uint8 *stack = NULL;

    stack = (uint8 *)(uintptr_t)entry->stack;

    __asm__ __volatile__(
        "pushal            \n\t"
        "pushfl            \n\t"
        "movl %%esp,%%ecx  \n\t"
        "testl %%esi,%%esi \n\t"
        "je 1f             \n\t"
        "movl %%esi,%%esp  \n\t"
        "1:                \n\t" //
        "pushl %%ecx       \n\t"
        "pushl %%eax       \n\t"
        "pushl %%edx       \n\t" // library module handle
        "leal 2f,%%eax     \n\t"
        "pushl %%eax       \n\t"
        "pushl %%edi       \n\t" // the OS/2 library entry point (we'll "ret" to it instead of jmp, so stack and registers are all correct).
        "xorl %%eax,%%eax  \n\t"
        "xorl %%ebx,%%ebx  \n\t"
        "xorl %%ecx,%%ecx  \n\t"
        "xorl %%edx,%%edx  \n\t"
        "xorl %%esi,%%esi  \n\t"
        "xorl %%edi,%%edi  \n\t"
        "xorl %%ebp,%%ebp  \n\t"
        "ret               \n\t"
        "2:                \n\t"
        "addl $8,%%esp     \n\t"
        "popl %%esp        \n\t"
        "popfl             \n\t"
        "popal             \n\t"
        :
        : "a"(isTermination), "d"(entry->module), "S"(stack), "D"(entry->entry)
        : "memory");
}

static void fixupLinearCodeSegmentReferences(uint8 *addr, size_t size)
{

    while (size >= 7)
    {
        size--;
        if (*(addr++) == 0xEA)
        {
            if ((addr[4] == 0x5B) && (addr[5] == 0x00))
            {

                memcpy(&addr[4], &Native->original_cs, 2);
                size -= 6;
                addr += 6;
            }
        }
    }
}

static void handleThreadLocalStorageAccess(const int slot, ucontext_t *uctx)
{
    greg_t *gregs = uctx->uc_mcontext.gregs;

    // Read FS from the faulting thread to locate its guest TIB2.
    void *ptib2 = NULL;
    __asm__ __volatile__("pushw %%fs            \n\t"
                         "movw %%ax, %%fs       \n\t"
                         "movl %%fs:0xC, %%eax  \n\t"
                         "popw %%fs             \n\t"
                         : "=a"(ptib2)
                         : "eax"(gregs[REG_FS]));

    /* The 32 TLS words follow TIB/TIB2 in guest memory. */
    uint32 *tls =
        (uint32 *)((uint8 *)ptib2 + Native->services.tls_offset - Native->services.tib2_offset);
    tls += slot;

    static const int x86RegisterToUContextEnum[] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                                    REG_ESP, REG_EBP, REG_ESI, REG_EDI};

    int handled = 1;
    uint8 *eip = (void *)(size_t)gregs[REG_EIP];
    const uint8 operand = eip[1];
    if ((operand & 0xC0) || ((operand & 7) == 4) || ((operand & 7) == 5))
    {
        fprintf(stderr, "Unsupported TLS memory addressing at %p\n", eip);
        abort();
    }
    switch (eip[0])
    {
        case 0xC7: /* MOV immediate to TLS memory. */
            if (operand & 0x38)
            {
                handled = 0;
                break;
            }
            *tls = *((uint32 *)(eip + 2));
            gregs[REG_EIP] += 6;
            break;

        case 0x89: /* MOV register to TLS memory. */
            *tls = (uint32)gregs[x86RegisterToUContextEnum[(operand >> 3) & 7]];
            gregs[REG_EIP] += 2;
            break;

        case 0x8B: /* MOV TLS memory to register. */
            gregs[x86RegisterToUContextEnum[(operand >> 3) & 7]] = (greg_t)*tls;
            gregs[REG_EIP] += 2;
            break;

        case 0x81:
        case 0x83: /* CMP TLS memory with a full or sign-extended immediate. */
        {
            if ((operand & 0x38) != 0x38)
            {
                handled = 0;
                break;
            }
            uint32 immediate, flags;
            if (*eip == 0x83)
                immediate = (uint32)(int32_t)(int8_t)eip[2];
            else
                memcpy(&immediate, eip + 2, 4);
            __asm__ __volatile__("cmpl %2, %1; pushfl; popl %0"
                                 : "=r"(flags)
                                 : "r"(*tls), "r"(immediate)
                                 : "cc");
            /* CMP changes OF, SF, ZF, AF, PF and CF; preserve all control flags. */
            gregs[REG_EFL] = (gregs[REG_EFL] & ~0x8D5) | (flags & 0x8D5);
            gregs[REG_EIP] += *eip == 0x83 ? 3 : 6;
            break;
        }

        default:
            handled = 0;
            break;
    }

    if (!handled)
    {
        fprintf(stderr, "Oh no, unhandled opcode 0x%X at %p accessing TLS register! File a bug!\n",
                (uint)eip[0], eip);
    }
    else
    {
        /* The signal return restores both guest registers and arithmetic flags. */
        return;
    }

    fflush(stderr);
    abort();
}

/* Resolve a fault address to its guest module/object for diagnostics. */
static int describeModuleAddress(const void *ptr, char *buffer, size_t size)
{
    if (!Native->services.describe_address)
    {
        *buffer = '\0';
        return 0;
    }
    return Native->services.describe_address(Native->services.userdata, (uint32)(uintptr_t)ptr,
                                             buffer, size);
}

/* Resolve host-only fault addresses through /proc/self/maps. */
static void describeHostMapping(const void *ptr, char *buf, const size_t buflen)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)getpid());
    FILE *io = fopen(path, "r");
    *buf = '\0';
    if (!io)
        return;

    const unsigned long want = (unsigned long)(size_t)ptr;
    char line[512];
    while (fgets(line, sizeof(line), io))
    {
        unsigned long start = 0, end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2)
        {
            if ((want >= start) && (want < end))
            {
                strncpy(buf, line, buflen - 1);
                buf[buflen - 1] = '\0';
                char *nl = strchr(buf, '\n');
                if (nl)
                    *nl = '\0';
                break;
            }
        }
    }
    fclose(io);
}

/* OS/2's flat GDT selectors cannot be installed by an unprivileged Linux task.
   Translate a faulting data-selector load to the equivalent host flat segment. */
static int handleFlatDataSelector(ucontext_t *context)
{
    greg_t *registers = context->uc_mcontext.gregs;
    if (registers[REG_TRAPNO] != 13)
        return 0;
    uint16_t cs = registers[REG_CS], ss = registers[REG_SS];
    int code16 = (cs & 4) != 0, stack16 = (ss & 4) != 0;
    uint32_t code_base = code16 ? Native->ldt[cs >> 3] : 0;
    uint32_t stack_base = stack16 ? Native->ldt[ss >> 3] : 0;
    if ((code16 && !code_base) || (stack16 && !stack_base) ||
        (!code16 && cs != Native->original_cs))
        return 0;
    uint32_t ip = code16 ? (uint16_t)registers[REG_EIP] : registers[REG_EIP];
    const uint8_t *instruction = (const uint8_t *)(uintptr_t)(code_base + ip);
    uint32_t bytes = 1, selector;
    int destination;
    if (*instruction == 0x1f || *instruction == 0x07) /* POP DS / ES */
    {
        uint32_t sp = stack16 ? (uint16_t)registers[REG_ESP] : registers[REG_ESP];
        if (stack16 && sp > 0xfffe)
            return 0;
        uint16_t word;
        memcpy(&word, (const void *)(uintptr_t)(stack_base + sp), sizeof word);
        selector = word;
        destination = *instruction == 0x1f ? REG_DS : REG_ES;
        if (selector != 0x53)
            return 0;
        uint32_t step = code16 ? 2 : 4;
        registers[REG_ESP] =
            stack16 ? (registers[REG_ESP] & ~0xffffu) | (uint16_t)(sp + step) : sp + step;
    }
    else if (*instruction == 0x8e && (instruction[1] & 0xc0) == 0xc0)
    {
        unsigned segment = (instruction[1] >> 3) & 7;
        if (segment != 0 && segment != 3)
            return 0;
        static const int source[] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                     REG_ESP, REG_EBP, REG_ESI, REG_EDI};
        selector = (uint16_t)registers[source[instruction[1] & 7]];
        if (selector != 0x53)
            return 0;
        destination = segment == 3 ? REG_DS : REG_ES;
        bytes = 2;
    }
    else
        return 0;
    registers[destination] = Native->original_ds;
    registers[REG_EIP] = code16 ? (uint16_t)(ip + bytes) : ip + bytes;
    return 1;
}
static void segfault_catcher(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uctx = (ucontext_t *)ctx;
    if (handleFlatDataSelector(uctx))
        return;
    const uint32 *addr = (const uint32 *)info->si_addr;
    const uint32 *tlspage = (const uint32 *)(uintptr_t)Native->execution->tls_address;
    greg_t *gregs = uctx->uc_mcontext.gregs;

    if (tlspage && (addr >= tlspage))
    {
        // was the app accessing one of the OS/2 TLS slots?
        const int slot = (int)(addr - tlspage);
        if (slot < 32)
        {
            handleThreadLocalStorageAccess(slot, uctx);
            return;
        }
    }

    static int faults = 0;
    faults++;
    switch (faults)
    {
        case 1:
        {
            char eipmod[512], addrmod[512];
            const void *eip = (const void *)(size_t)gregs[REG_EIP];
            describeModuleAddress(eip, eipmod, sizeof(eipmod));
            describeModuleAddress(addr, addrmod, sizeof(addrmod));
            if (!*eipmod)
            {
                char line[512];
                describeHostMapping(eip, line, sizeof(line));
                snprintf(eipmod, sizeof(eipmod), "%s", *line ? line : "unmapped");
            }
            if (!*addrmod)
            {
                char line[512];
                describeHostMapping(addr, line, sizeof(line));
                snprintf(addrmod, sizeof(addrmod), "%s", *line ? line : "unmapped");
            }
            fprintf(stderr,
                    "SIGSEGV at %p (%s) eip=%p (%s) eax=%08X ebx=%08X "
                    "ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X\n",
                    addr, (*addrmod) ? addrmod : "unmapped", eip, (*eipmod) ? eipmod : "unmapped",
                    (uint)gregs[REG_EAX], (uint)gregs[REG_EBX], (uint)gregs[REG_ECX],
                    (uint)gregs[REG_EDX], (uint)gregs[REG_ESI], (uint)gregs[REG_EDI],
                    (uint)gregs[REG_EBP], (uint)gregs[REG_ESP]);
            {
                char line[512];
                describeHostMapping(eip, line, sizeof(line));
                if (*line)
                {
                    const uint8 *bytes = (const uint8 *)(size_t)eip;
                    fprintf(stderr, "  eip bytes:");
                    for (int i = 0; i < 16; i++)
                        fprintf(stderr, " %02X", bytes[i]);
                    fprintf(stderr, "\n");
                }
            }
            {
                const uint32 *stack = (const uint32 *)(size_t)gregs[REG_ESP];
                fprintf(stderr, "  near-stack:");
                for (int i = -8; i < 8; i++)
                    fprintf(stderr, " [%08X]=%08X", (uint)(stack + i), (uint)stack[i]);
                fprintf(stderr, "\n");
            }
            {
                const uint32 *stack = (const uint32 *)(size_t)gregs[REG_ESP];
                fprintf(stderr, "  stack:");
                for (int i = 0; i < 12; i++)
                {
                    char mod[320];
                    describeModuleAddress((const void *)(size_t)stack[i], mod, sizeof(mod));
                    fprintf(stderr, " [%08X%s%s]", (uint)stack[i], (*mod) ? "=" : "", mod);
                }
                fprintf(stderr, "\n");
            }
            break;
        }
        case 2:
            write(2, "SIGSEGV, aborting.\n", 19);
            break;
        default:
            break;
    }

    abort();
}

static int installSignalHandlers(void)
{
    struct sigaction action;

    memset(&action, '\0', sizeof(action));
    action.sa_sigaction = segfault_catcher;
    action.sa_flags = SA_NODEFER | SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGSEGV, &action, &Native->previous_segv) == -1)
    {
        fprintf(stderr, "Couldn't install SIGSEGV handler! (%s)\n", strerror(errno));
        return 0;
    }

    return 1;
}

static size_t emitBridge16(uint8_t *output, size_t capacity, uint8_t *segment_base,
                           uint16_t selector_index, LxExecutionApi16 callback,
                           uint16_t argument_bytes)
{
    if (capacity < 256)
        return 0;
    uint8 *ptr = output;

    /* instructions are in Intel syntax here, not AT&T. */

    *(ptr++) = 0x53; /* push bx */
    *(ptr++) = 0x51; /* push cx */
    *(ptr++) = 0x52; /* push dx */
    *(ptr++) = 0x56; /* push si */
    *(ptr++) = 0x57; /* push di */
    *(ptr++) = 0x55; /* push bp */
    *(ptr++) = 0x89; /* mov cx,sp... */
    *(ptr++) = 0xE1;
    *(ptr++) = 0x66; /* jmp dword 0x7788:0x33332222... */
    *(ptr++) = 0xEA;
    const uint32 jmp32addr = (uint32)(ptr + 6);
    memcpy(ptr, &jmp32addr, 4);
    ptr += 4;
    memcpy(ptr, &Native->original_cs, 2);
    ptr += 2;

    *(ptr++) = 0x66; /* mov bx,ss... */
    *(ptr++) = 0x8C;
    *(ptr++) = 0xD3;
    *(ptr++) = 0xC1; /* shl ebx,byte 0x10... */
    *(ptr++) = 0xE3;
    *(ptr++) = 0x10;
    *(ptr++) = 0x66; /* mov bx,cx... */
    *(ptr++) = 0x89;
    *(ptr++) = 0xCB;
    *(ptr++) = 0x66; /* mov ax,ss... */
    *(ptr++) = 0x8C;
    *(ptr++) = 0xD0;
    *(ptr++) = 0x66; /* shr ax,byte 0x3... */
    *(ptr++) = 0xC1;
    *(ptr++) = 0xE8;
    *(ptr++) = 0x03;
    *(ptr++) = 0xC1; /* shl eax,byte 0x10... */
    *(ptr++) = 0xE0;
    *(ptr++) = 0x10;
    *(ptr++) = 0x66; /* mov ax,cx... */
    *(ptr++) = 0x89;
    *(ptr++) = 0xC8;
    *(ptr++) = 0x66; /* mov cx,0xabcd... */
    *(ptr++) = 0xB9;
    memcpy(ptr, &Native->original_ss, 2);
    ptr += 2;
    *(ptr++) = 0x8E; /* mov ss,ecx... */
    *(ptr++) = 0xD1;
    /* Use the per-thread host stack, preserving EAX as the guest argument pointer. */
    *(ptr++) = 0x65; /* mov edx,gs:[private bridge-stack TLS displacement] */
    *(ptr++) = 0x8B;
    *(ptr++) = 0x15;
    const uint32 hoststack_offset = nativeHostStackOffset();
    memcpy(ptr, &hoststack_offset, 4);
    ptr += 4;
    *(ptr++) = 0x89;
    *(ptr++) = 0xD4; /* mov esp,edx */
    *(ptr++) = 0x83; /* add eax,byte +0x10... */
    *(ptr++) = 0xC0;
    *(ptr++) = 0x10;
    *(ptr++) = 0x53; /* push ebx */
    *(ptr++) = 0x1E; /* push ds */
    *(ptr++) = 0x06; /* push es */
    if (Native->original_ss != Native->original_ds)
    {
        *(ptr++) = 0x66; /* mov cx,0x8888... */
        *(ptr++) = 0xB9;
        memcpy(ptr, &Native->original_ds, 2);
        ptr += 2;
    }
    *(ptr++) = 0x8E; /* mov ds,ecx... */
    *(ptr++) = 0xD9;
    if (Native->original_ds != Native->original_es)
    {
        *(ptr++) = 0x66; /* mov cx,0x9999... */
        *(ptr++) = 0xB9;
        memcpy(ptr, &Native->original_es, 2);
        ptr += 2;
    }
    *(ptr++) = 0x8E; /* mov es,ecx... */
    *(ptr++) = 0xC1;
    /* Keep guest continuation outside the host stack; padding aligns three C arguments. */
    *ptr++ = 0x83;
    *ptr++ = 0xec;
    *ptr++ = 8;
    *ptr++ = 0x8d;
    *ptr++ = 0x50;
    *ptr++ = 0xf0; /* lea edx,[eax-16] */
    *ptr++ = 0x52; /* guest stack below the saved USE16 registers */
    *ptr++ = 0x68;
    const uint32 callback_address = (uint32)(uintptr_t)callback;
    memcpy(ptr, &callback_address, 4);
    ptr += 4;
    *ptr++ = 0x50; /* argument-frame pointer */
    *ptr++ = 0xb8;
    const uint32 dispatcher = (uint32)(uintptr_t)dispatchApi16;
    memcpy(ptr, &dispatcher, 4);
    ptr += 4;
    *ptr++ = 0xff;
    *ptr++ = 0xd0;
    *ptr++ = 0x83;
    *ptr++ = 0xc4;
    *ptr++ = 20;
    *(ptr++) = 0x07; /* pop es */
    *(ptr++) = 0x1F; /* pop ds */
    *(ptr++) = 0x66; /* pop bx... */
    *(ptr++) = 0x5B;
    *(ptr++) = 0x66; /* pop cx... */
    *(ptr++) = 0x59;
    *(ptr++) = 0x66; /* jmp word 0xaaaa:0xbbbb... */
    *(ptr++) = 0xEA;
    const uint16 jmp16offset = (uint16)((((uint32)ptr) - ((uint32)segment_base)) + 4);
    memcpy(ptr, &jmp16offset, 2);
    ptr += 2;
    const uint16 jmp16segment = (selector_index << 3) | 7;
    memcpy(ptr, &jmp16segment, 2);
    ptr += 2;

    *(ptr++) = 0x8E; /* mov ss,cx... */
    *(ptr++) = 0xD1;
    *(ptr++) = 0x89; /* mov sp,bx... */
    *(ptr++) = 0xDC;
    *(ptr++) = 0x5D; /* pop bp */
    *(ptr++) = 0x5F; /* pop di */
    *(ptr++) = 0x5E; /* pop si */
    *(ptr++) = 0x5A; /* pop dx */
    *(ptr++) = 0x59; /* pop cx */
    *(ptr++) = 0x5B; /* pop bx */
    *(ptr++) = 0xCA; /* retf 0x22... */
    const uint16 argbytecount = argument_bytes;
    memcpy(ptr, &argbytecount, 2);
    ptr += 2;
    return (size_t)(ptr - output);
}
static __thread jmp_buf *ThreadReturn;
typedef struct NativeThread16
{
    uint8 *code;
    uint16 selector;
} NativeThread16;
static void *prepareThread16(uint32_t entry, uint32_t stack)
{
    NativeThread16 *thread = calloc(1, sizeof *thread);
    if (!thread)
        return NULL;
    thread->code = lxAllocSegment(&thread->selector, 1);
    if (!thread->code)
    {
        free(thread);
        return NULL;
    }
    uint8 *code = thread->code;
    const uint16 selector = thread->selector;
    uint8 *p = code;
    const uint16 guest_ss = stack >> 16, guest_sp = stack & 0xffff;
    const uint16 guest_cs = entry >> 16, guest_ip = entry & 0xffff;
    const uint16 code_selector = (selector << 3) | 7;
    const uint32 saved_sp = (uint32)(code + 0x100);
    /* Save the host frame, switch to the guest stack and call its far entry. */
    *p++ = 0x60;
    *p++ = 0x89;
    *p++ = 0x25;
    memcpy(p, &saved_sp, 4);
    p += 4;
    *p++ = 0x66;
    *p++ = 0xea;
    uint16 off = (uint16)((p - code) + 4);
    memcpy(p, &off, 2);
    p += 2;
    memcpy(p, &code_selector, 2);
    p += 2;
    *p++ = 0xb8;
    memcpy(p, &guest_ss, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd0;
    *p++ = 0xbc;
    memcpy(p, &guest_sp, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd8;
    *p++ = 0x8e;
    *p++ = 0xc0;
    *p++ = 0x9a;
    memcpy(p, &guest_ip, 2);
    p += 2;
    memcpy(p, &guest_cs, 2);
    p += 2;
    *p++ = 0x66;
    *p++ = 0xea;
    const uint32 flat = (uint32)(p + 6);
    memcpy(p, &flat, 4);
    p += 4;
    memcpy(p, &Native->original_cs, 2);
    p += 2;
    *p++ = 0x66;
    *p++ = 0xb8;
    memcpy(p, &Native->original_ss, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd0;
    *p++ = 0x66;
    *p++ = 0xb8;
    memcpy(p, &Native->original_ds, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd8;
    *p++ = 0x66;
    *p++ = 0xb8;
    memcpy(p, &Native->original_es, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xc0;
    *p++ = 0x8b;
    *p++ = 0x25;
    memcpy(p, &saved_sp, 4);
    p += 4;
    *p++ = 0x61;
    *p++ = 0xc3; /* popad; ret */
    return thread;
}
static void thread16(void *invocation)
{
    NativeThread16 *thread = invocation;
    jmp_buf target;
    jmp_buf *outer_return = ThreadReturn;
    NativeBridgeFrame *outer_bridge = NativeBridge;
    void *outer_stack = NativeHostStack;
    ThreadReturn = &target;
    if (!setjmp(target))
        ((void (*)(void))thread->code)();
    ThreadReturn = outer_return;
    NativeBridge = outer_bridge;
    NativeHostStack = outer_stack;
}
static void destroyThread16(void *invocation)
{
    NativeThread16 *thread = invocation;
    lxFreeSegment(thread->selector);
    free(thread);
}

static void program(const LxExecutionEntry *entry, unsigned bits)
{
    if (bits == 16)
        runNeModule(entry);
    runLxModule(entry);
}
static uint32_t callback4(uint32_t entry, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    uintptr_t base = (uintptr_t)NativeHostAllocation;
    int suspended = NativeBridge && sp >= base && sp < base + 65536;
    uint32_t words[4] = {a, b, c, d}, value;
    void *previous = NativeHostStack;
    if (suspended)
        NativeHostStack = (void *)((sp - 128) & ~(uintptr_t)15);
    uint32_t stack = suspended ? NativeBridge->guest_stack : (uint32_t)(sp - 128);
    /* ESI retains the saved host frame across the guest procedure and nested imports. */
    __asm__ __volatile__("pushfl\n\tpushal\n\tmovl %%esp,%%esi\n\t"
                         "movl %%eax,%%esp\n\tandl $-16,%%esp\n\t"
                         "pushl 12(%%ebx)\n\tpushl 8(%%ebx)\n\tpushl 4(%%ebx)\n\tpushl (%%ebx)\n\t"
                         "call *%%edi\n\tmovl %%eax,28(%%esi)\n\tmovl %%esi,%%esp\n\tpopal\n\tpopfl"
                         : "=a"(value)
                         : "a"(stack), "b"(words), "D"(entry)
                         : "memory");
    NativeHostStack = previous;
    return value;
}
static void callback32(uint32_t entry, uint32_t argument)
{
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    const uintptr_t base = (uintptr_t)NativeHostAllocation;
    if (!NativeBridge || sp < base || sp >= base + 65536)
    {
        ((void (*)(uint32_t))(uintptr_t)entry)(argument);
        return;
    }
    void *previous = NativeHostStack;
    /* Nested imports use host stack space below this frame; callbacks use the suspended guest stack. */
    NativeHostStack = (void *)((sp - 128) & ~(uintptr_t)15);
    const uint32_t stack = NativeBridge->guest_stack;
    __asm__ __volatile__("pushfl\n\t"
                         "pushal\n\t"
                         "movl %%esp,%%ecx\n\t"
                         "movl %%esi,%%esp\n\t"
                         "andl $-16,%%esp\n\t"
                         "subl $8,%%esp\n\t"
                         "pushl %%ecx\n\t"
                         "pushl %%eax\n\t"
                         "call *%%edi\n\t"
                         "addl $4,%%esp\n\t"
                         "popl %%esp\n\t"
                         "popal\n\t"
                         "popfl\n\t"
                         :
                         : "S"(stack), "D"(entry), "a"(argument)
                         : "memory");
    NativeHostStack = previous;
}
typedef struct NativeThread32
{
    LxGuestMapping *mapping;
} NativeThread32;
static void *prepareThread32(uint32_t entry, uint32_t stack, uint32_t argument)
{
    NativeThread32 *thread = calloc(1, sizeof *thread);
    if (!thread)
        return NULL;
    thread->mapping = lx_execution_map(Native->execution, 0, 4096, 7, 0);
    if (!thread->mapping)
    {
        free(thread);
        return NULL;
    }
    uint8_t *code = lx_execution_mapping_host(thread->mapping), *p = code;
    uint32_t saved = lx_execution_mapping_address(thread->mapping) + 0x100;
    *p++ = 0x60; /* pushad: host callee-saved registers */
    *p++ = 0x89;
    *p++ = 0x25;
    memcpy(p, &saved, 4);
    p += 4;
    *p++ = 0xbc;
    memcpy(p, &stack, 4);
    p += 4;
    *p++ = 0x68;
    memcpy(p, &argument, 4);
    p += 4;
    *p++ = 0xb8;
    memcpy(p, &entry, 4);
    p += 4;
    *p++ = 0xff;
    *p++ = 0xd0; /* call guest entry */
    *p++ = 0x8b;
    *p++ = 0x25;
    memcpy(p, &saved, 4);
    p += 4;
    *p++ = 0x61;
    *p++ = 0xc3; /* popad; ret to host */
    return thread;
}
static void thread32(void *invocation)
{
    NativeThread32 *thread = invocation;
    jmp_buf target;
    jmp_buf *outer_return = ThreadReturn;
    NativeBridgeFrame *outer_bridge = NativeBridge;
    void *outer_stack = NativeHostStack;
    ThreadReturn = &target;
    if (!setjmp(target))
        ((void (*)(void))lx_execution_mapping_host(thread->mapping))();
    ThreadReturn = outer_return;
    NativeBridge = outer_bridge;
    NativeHostStack = outer_stack;
}
static int endThread(void)
{
    if (!ThreadReturn)
        return 0;
    longjmp(*ThreadReturn, 1);
}
static void destroyThread32(void *invocation)
{
    NativeThread32 *thread = invocation;
    lx_execution_unmap(Native->execution, thread->mapping);
    free(thread);
}
static void nearBase(uint32_t address)
{
    Native->near16_base = address;
}
static uint16_t flatCodeSelector(void)
{
    return Native->original_cs;
}
static void *missingEntry(const char *module, const char *entry, unsigned bits,
                          LxExecutionBridge *binding)
{
    return bits == 16 ? generateMissingTrampoline16(module, entry, binding)
                      : generateMissingTrampoline(module, entry);
}
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
    if (!installSignalHandlers())
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
    if (exchange)
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
typedef struct NativeApiCall
{
    uint32_t *registers;
    uint32_t arguments;
    struct NativeApiCall *previous;
} NativeApiCall;
static _Thread_local NativeApiCall *NativeApi;
static uint32_t dispatchApi32(LxExecutionApi16 callback, uint8_t *arguments, uint32_t *registers)
{
    NativeApiCall call = {
        registers, arguments == (uint8_t *)(registers + 7) ? 0 : (uint32_t)(uintptr_t)arguments,
        NativeApi};
    NativeApi = &call;
    uint32_t result = callback(arguments);
    NativeApi = call.previous;
    return result;
}
static uint32_t apiArguments(void)
{
    return NativeApi ? NativeApi->arguments : 0;
}
static int apiRegisters32(LxExecutionRegisters32 *out)
{
    if (!NativeApi)
        return 0;
    const uint32_t *r = NativeApi->registers;
    memset(out, 0, sizeof *out);
    out->edi = r[0];
    out->esi = r[1];
    out->ebp = r[2];
    out->ebx = r[4];
    out->edx = r[5];
    out->ecx = r[6];
    out->eax = r[7];
    out->eflags = r[8];
    out->eip = r[9];
    out->esp = (uint32_t)(uintptr_t)(r + 10);
    __asm__ __volatile__(
        "mov %%cs,%0; mov %%ds,%1; mov %%es,%2; mov %%fs,%3; mov %%gs,%4; mov %%ss,%5"
        : "=m"(out->cs), "=m"(out->ds), "=m"(out->es), "=m"(out->fs), "=m"(out->gs), "=m"(out->ss));
    return 1;
}
static size_t emitBridge32(uint8_t *output, size_t capacity, const LxExecutionApi32 *binding)
{
    if (capacity < 64)
        return 0;
    uint8_t *p = output;
    const int eax_argument = binding->convention == LX_ABI_EAX32;
    *p++ = 0x9c;
    *p++ = 0x60; /* pushfd; pushad: retain the suspended guest registers */
    *p++ = 0x55;
    *p++ = 0x89;
    *p++ = 0xe5; /* push ebp; mov ebp,esp */
    *p++ = 0x83;
    *p++ = 0xe4;
    *p++ = 0xf0; /* align host call stack */
    *p++ = 0x83;
    *p++ = 0xec;
    *p++ = 4;
    *p++ = 0x8d;
    *p++ = 0x45;
    *p++ = 4;
    *p++ = 0x50; /* saved registers */
    *p++ = 0x8d;
    *p++ = 0x45;
    *p++ = eax_argument ? 32 : 44;
    *p++ = 0x50;
    *p++ = 0x68;
    uint32_t callback = (uint32_t)(uintptr_t)binding->callback;
    memcpy(p, &callback, 4);
    p += 4;
    *p++ = 0xb8;
    uint32_t dispatch = (uint32_t)(uintptr_t)dispatchApi32;
    memcpy(p, &dispatch, 4);
    p += 4;
    *p++ = 0xff;
    *p++ = 0xd0;
    *p++ = 0x89;
    *p++ = 0x45;
    *p++ = 32; /* callback result replaces saved EAX */
    *p++ = 0xc9;
    *p++ = 0x61;
    *p++ = 0x9d; /* leave; popad; popfd */
    if (binding->callee_pop)
    {
        *p++ = 0xc2;
        memcpy(p, &binding->callee_pop, 2);
        p += 2;
    }
    else
        *p++ = 0xc3;
    return (size_t)(p - output);
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
                                                         program,
                                                         runLxLibraryInitOrTerm,
                                                         prepareThread16,
                                                         thread16,
                                                         destroyThread16,
                                                         callback32,
                                                         lxSetOs2Tib,
                                                         lxGetOs2Tib,
                                                         lxDeinitOs2Tib,
                                                         lxAllocSegment,
                                                         lxFreeSegment,
                                                         lxCreateCodeAlias,
                                                         lxFindSelector,
                                                         lxFreeSelector,
                                                         lxConvert1616to32,
                                                         lxConvert32to1616,
                                                         initOs2StackSegments,
                                                         nearBase,
                                                         flatCodeSelector,
                                                         fixupLinearCodeSegmentReferences,
                                                         missingEntry,
                                                         emitBridge16,
                                                         atomicU32,
                                                         nativeEntry,
                                                         1,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         emitBridge32,
                                                         copyOut,
                                                         prepareThread32,
                                                         thread32,
                                                         destroyThread32,
                                                         endThread,
                                                         copyIn,
                                                         callback4,
                                                         apiArguments,
                                                         apiRegisters32};
