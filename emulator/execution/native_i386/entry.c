/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* Program, NE and DLL entry, and trampolines for missing imports. */
#define _GNU_SOURCE 1
#include "emulator/execution/native_i386/internal.h"

static __attribute__((noreturn, used)) void endLxProcess(uint32 result)
{
    Native->services.program_returned(Native->services.userdata, result);
    abort();
}

static void missingEntryPointCalled(const char *module, const char *entry)
{
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\n\nMissing entry point '%s' in module '%s'! Aborting.\n\n\n", entry, module);

    fflush(stderr);
    endLxProcess(1);
}

void *nativeMissingTrampoline(const char *_module, const char *_entry)
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

void *nativeMissingTrampoline16(const char *_module, const char *_entry, LxExecutionBridge *binding)
{
    static void *segment = NULL;
    static uint32 segmentused = 0;
    const uint32 segmentsize = 0x10000;
    static uint16 selector = 0xFFFF;

    if ((!segment) || ((segmentsize - segmentused) < 64))
    {
        if (segment)
            nativeProtect(segment, segmentsize, LX_HOST_READ | LX_HOST_EXECUTE);
        segment = nativeAllocSegment(&selector, 1);
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

__attribute__((noreturn)) void nativeRunLxModule(const LxExecutionEntry *entry)
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

__attribute__((noreturn)) void nativeRunNeModule(const LxExecutionEntry *entry)
{

    /* NE startup supplies both the stack ABI and AX/BX/CX runtime arguments. */
    const uint16 stacksize = (uint16)entry->stack_bytes;
    const uint16 ss = (entry->stack >> 16) & 0xFFFF; // stack segment
    const uint32 cmd1616 = nativeConvert32to1616((void *)(uintptr_t)entry->command);
    const uint16 cmdlineoffset = cmd1616 & 0xFFFF;
    const uint16 envseg = (cmd1616 >> 16) & 0xFFFF;

    uint8 *stack = (uint8 *)nativeConvert1616to32(entry->stack);

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
    void *segment = nativeAllocSegment(&selector, 1);
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

void nativeRunLibraryInitOrTerm(const LxExecutionEntry *entry, const int isTermination)
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

void nativeFixupLinearCodeSegments(uint8 *addr, size_t size)
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
