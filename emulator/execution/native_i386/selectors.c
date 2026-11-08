/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* LDT descriptors, selectors, 16:16 conversion, stack segments and the TIB. */
#define _GNU_SOURCE 1
#include "emulator/execution/native_i386/internal.h"

int nativeLdt(uint16_t index, uint32_t base, uint32_t limit_pages, int code, int bits, int present)
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

int nativeProtect(void *address, size_t size, unsigned protection)
{
    return lx_host_vm_protect(address, size, protection) ? 0 : -1;
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
        else if (iscode == 0)
            return 0;
    }

    if (iscode != 0 || preferred == -1)
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

int nativeFindSelector(const uint32 address, uint16 *selector, uint16 *offset, int code)
{
    pthread_mutex_lock(&NativeDescriptorMutex);
    int result = lxFindSelectorUnlocked(address, selector, offset, code);
    pthread_mutex_unlock(&NativeDescriptorMutex);
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

void nativeFreeSelector(const uint16 selector)
{
    pthread_mutex_lock(&NativeDescriptorMutex);
    lxFreeSelectorUnlocked(selector);
    pthread_mutex_unlock(&NativeDescriptorMutex);
}

void *nativeConvert1616to32(const uint32 addr1616)
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

uint32 nativeConvert32to1616(void *addr32)
{
    if (addr32 == NULL)
        return 0;

    uint16 selector = 0;
    uint16 offset = 0;
    if (!nativeFindSelector((uint32)addr32, &selector, &offset, -1))
    {
        fprintf(stderr, "Uhoh, ran out of LDT entries?!\n");
        return 0;
    }

    return (((uint32)lxSelectorToSegment(selector)) << 16) | ((uint32)offset);
}

static inline void *lxConvertSegmentOffsetto32(const uint16 seg, const uint16 off)
{
    return nativeConvert1616to32((((uint32)seg) << 16) | ((uint32)off));
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
            nativeFreeSelector((uint16)(addr >> 16));
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

void nativeInitStackSegments(uint32 address, uint32 size, const int release)
{
    pthread_mutex_lock(&NativeDescriptorMutex);
    initOs2StackSegmentsUnlocked(address, size, release);
    pthread_mutex_unlock(&NativeDescriptorMutex);
}

/* FS:0 addresses this thread's guest TIB; attach from the entering host thread. */
uint16 nativeSetTib(uint8 *tibspace)
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
    pthread_mutex_lock(&NativeDescriptorMutex);
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
    pthread_mutex_unlock(&NativeDescriptorMutex);

    /* TI=0 and RPL=3 select a ring-3 GDT entry; LDT entries use TI=1. */
    const unsigned int segment = (entry.entry_number << 3) | 3;
    __asm__ __volatile__("movw %%ax, %%fs  \n\t" : : "a"(segment));
    return (uint16)entry.entry_number;
}

static void *lxGetOs2Tib2(void)
{
    return NativeTib + Native->services.tib2_offset;
}

void *nativeGetTib(void)
{
    return NativeTib;
}

void nativeDeinitTib(const uint16 selector)
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
        nativeFreeSelector(slot);
        return 0;
    }

    *outselector = (uint16)((slot << 3) | 7);
    return 1;
}

int nativeCreateCodeAlias(const uint16 selector, uint16 *output)
{
    pthread_mutex_lock(&NativeDescriptorMutex);
    int result = lxCreateCodeAliasUnlocked(selector, output);
    pthread_mutex_unlock(&NativeDescriptorMutex);
    return result;
}

void *nativeAllocSegment(uint16 *selector, const int iscode)
{
    /* Keep USE16 allocations below 512 MiB so tiled selectors convert directly to linear addresses. */
    static size_t baseaddr = 136 * 1024 * 1024;
    const uint32 segmentsize = 0x10000;
    *selector = 0xFFFF;
    pthread_mutex_lock(&NativeSegmentMutex);
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
        if (nativeFindSelector((uint32)segment, selector, &offset, iscode) && offset == 0)
        {
            Native->segments[*selector] = mapping;
            pthread_mutex_unlock(&NativeSegmentMutex);
            return segment;
        }
        lx_execution_unmap(Native->execution, mapping);
    }
    pthread_mutex_unlock(&NativeSegmentMutex);
    fprintf(stderr, "Failed to allocate a low 16-bit segment.\n");
    return NULL;
}

void nativeFreeSegment(const uint16 selector)
{
    if (selector >= LX_MAX_LDT_SLOTS)
        return;
    pthread_mutex_lock(&NativeSegmentMutex);
    LxGuestMapping *mapping = Native->segments[selector];
    if (mapping && lx_execution_unmap(Native->execution, mapping))
    {
        Native->segments[selector] = NULL;
        nativeFreeSelector(selector);
    }
    pthread_mutex_unlock(&NativeSegmentMutex);
}
