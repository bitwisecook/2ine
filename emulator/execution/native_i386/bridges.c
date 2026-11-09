/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* 16/32-bit API bridges, guest threads, callbacks and 16-bit calls. */
#define _GNU_SOURCE 1
#include "emulator/execution/native_i386/internal.h"
#include "emulator/execution/core/x86/x86.h"

enum
{
    /* Upper bound on one emitted 16-bit API bridge. */
    BRIDGE16_CAPACITY = 256,
    /* Thread trampolines keep the saved host ESP after their code. */
    SAVED_HOST_SP_OFFSET = 0x100
};

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

size_t nativeEmitBridge16(uint8_t *output, size_t capacity, uint8_t *segment_base,
                          uint16_t selector_index, LxExecutionApi16 callback,
                          uint16_t argument_bytes)
{
    if (capacity < BRIDGE16_CAPACITY)
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
    *ptr++ = 0x83; /* sub esp,8 */
    *ptr++ = 0xec;
    *ptr++ = 8;
    *ptr++ = 0x8d;
    *ptr++ = 0x50;
    *ptr++ = 0xf0; /* lea edx,[eax-16] */
    *ptr++ = 0x52; /* guest stack below the saved USE16 registers */
    *ptr++ = 0x68; /* push callback */
    const uint32 callback_address = (uint32)(uintptr_t)callback;
    memcpy(ptr, &callback_address, 4);
    ptr += 4;
    *ptr++ = 0x50; /* argument-frame pointer */
    *ptr++ = 0xb8; /* mov eax,dispatchApi16 */
    const uint32 dispatcher = (uint32)(uintptr_t)dispatchApi16;
    memcpy(ptr, &dispatcher, 4);
    ptr += 4;
    *ptr++ = 0xff; /* call eax */
    *ptr++ = 0xd0;
    *ptr++ = 0x83; /* add esp,20: three arguments and the padding */
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
    const uint16 jmp16segment =
        (selector_index << X86_SELECTOR_INDEX_SHIFT) | X86_SELECTOR_LDT_RING3;
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

void *nativePrepareThread16(uint32_t entry, uint32_t stack)
{
    NativeThread16 *thread = calloc(1, sizeof *thread);
    if (!thread)
        return NULL;
    thread->code = nativeAllocSegment(&thread->selector, 1);
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
    const uint16 code_selector = (selector << X86_SELECTOR_INDEX_SHIFT) | X86_SELECTOR_LDT_RING3;
    const uint32 saved_sp = (uint32)(code + SAVED_HOST_SP_OFFSET);
    /* Save the host frame, switch to the guest stack and call its far entry. */
    *p++ = 0x60; /* pushad */
    *p++ = 0x89; /* mov [saved_sp],esp */
    *p++ = 0x25;
    memcpy(p, &saved_sp, 4);
    p += 4;
    *p++ = 0x66; /* jmp far code_selector:next, entering USE16 */
    *p++ = 0xea;
    uint16 off = (uint16)((p - code) + 4);
    memcpy(p, &off, 2);
    p += 2;
    memcpy(p, &code_selector, 2);
    p += 2;
    *p++ = 0xb8; /* mov ax,guest_ss */
    memcpy(p, &guest_ss, 2);
    p += 2;
    *p++ = 0x8e; /* mov ss,ax */
    *p++ = 0xd0;
    *p++ = 0xbc; /* mov sp,guest_sp */
    memcpy(p, &guest_sp, 2);
    p += 2;
    *p++ = 0x8e; /* mov ds,ax */
    *p++ = 0xd8;
    *p++ = 0x8e; /* mov es,ax */
    *p++ = 0xc0;
    *p++ = 0x9a; /* call far guest_cs:guest_ip */
    memcpy(p, &guest_ip, 2);
    p += 2;
    memcpy(p, &guest_cs, 2);
    p += 2;
    *p++ = 0x66; /* jmp far original_cs:flat, back to USE32 */
    *p++ = 0xea;
    const uint32 flat = (uint32)(p + 6);
    memcpy(p, &flat, 4);
    p += 4;
    memcpy(p, &Native->original_cs, 2);
    p += 2;
    *p++ = 0x66; /* mov ax,original_ss; mov ss,ax */
    *p++ = 0xb8;
    memcpy(p, &Native->original_ss, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd0;
    *p++ = 0x66; /* mov ax,original_ds; mov ds,ax */
    *p++ = 0xb8;
    memcpy(p, &Native->original_ds, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xd8;
    *p++ = 0x66; /* mov ax,original_es; mov es,ax */
    *p++ = 0xb8;
    memcpy(p, &Native->original_es, 2);
    p += 2;
    *p++ = 0x8e;
    *p++ = 0xc0;
    *p++ = 0x8b; /* mov esp,[saved_sp] */
    *p++ = 0x25;
    memcpy(p, &saved_sp, 4);
    p += 4;
    *p++ = 0x61;
    *p++ = 0xc3; /* popad; ret */
    return thread;
}

void nativeRunThread16(void *invocation)
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

void nativeDestroyThread16(void *invocation)
{
    NativeThread16 *thread = invocation;
    nativeFreeSegment(thread->selector);
    free(thread);
}

void nativeProgram(const LxExecutionEntry *entry, unsigned bits)
{
    if (bits == 16)
        nativeRunNeModule(entry);
    nativeRunLxModule(entry);
}

uint32_t nativeCallback4(uint32_t entry, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    uintptr_t base = (uintptr_t)NativeHostAllocation;
    int suspended = NativeBridge && sp >= base && sp < base + NATIVE_HOST_STACK_SIZE;
    uint32_t words[4] = {a, b, c, d}, value;
    void *previous = NativeHostStack;
    if (suspended)
        NativeHostStack = (void *)((sp - NATIVE_STACK_RED_ZONE) & ~NATIVE_STACK_ALIGN_MASK);
    uint32_t stack = suspended ? NativeBridge->guest_stack : (uint32_t)(sp - NATIVE_STACK_RED_ZONE);
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

void nativeCallback32(uint32_t entry, uint32_t argument)
{
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    const uintptr_t base = (uintptr_t)NativeHostAllocation;
    if (!NativeBridge || sp < base || sp >= base + NATIVE_HOST_STACK_SIZE)
    {
        ((void (*)(uint32_t))(uintptr_t)entry)(argument);
        return;
    }
    void *previous = NativeHostStack;
    /* Nested imports use host stack space below this frame; callbacks use the suspended guest stack. */
    NativeHostStack = (void *)((sp - NATIVE_STACK_RED_ZONE) & ~NATIVE_STACK_ALIGN_MASK);
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

void *nativePrepareThread32(uint32_t entry, uint32_t stack, uint32_t argument)
{
    NativeThread32 *thread = calloc(1, sizeof *thread);
    if (!thread)
        return NULL;
    thread->mapping = lx_execution_map(Native->execution, 0, X86_PAGE_SIZE,
                                       LX_GUEST_READ | LX_GUEST_WRITE | LX_GUEST_EXECUTE, 0);
    if (!thread->mapping)
    {
        free(thread);
        return NULL;
    }
    uint8_t *code = lx_execution_mapping_host(thread->mapping), *p = code;
    uint32_t saved = lx_execution_mapping_address(thread->mapping) + SAVED_HOST_SP_OFFSET;
    *p++ = 0x60; /* pushad: host callee-saved registers */
    *p++ = 0x89; /* mov [saved],esp */
    *p++ = 0x25;
    memcpy(p, &saved, 4);
    p += 4;
    *p++ = 0xbc; /* mov esp,stack */
    memcpy(p, &stack, 4);
    p += 4;
    *p++ = 0x68; /* push argument */
    memcpy(p, &argument, 4);
    p += 4;
    *p++ = 0xb8; /* mov eax,entry */
    memcpy(p, &entry, 4);
    p += 4;
    *p++ = 0xff;
    *p++ = 0xd0; /* call eax: the guest entry */
    *p++ = 0x8b; /* mov esp,[saved] */
    *p++ = 0x25;
    memcpy(p, &saved, 4);
    p += 4;
    *p++ = 0x61;
    *p++ = 0xc3; /* popad; ret to host */
    return thread;
}

void nativeRunThread32(void *invocation)
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

int nativeEndThread(void)
{
    if (!ThreadReturn)
        return 0;
    longjmp(*ThreadReturn, 1);
}

void nativeDestroyThread32(void *invocation)
{
    NativeThread32 *thread = invocation;
    lx_execution_unmap(Native->execution, thread->mapping);
    free(thread);
}

void nativeNearBase(uint32_t address)
{
    Native->near16_base = address;
}

uint16_t nativeFlatCodeSelector(void)
{
    return Native->original_cs;
}

void *nativeMissingEntry(const char *module, const char *entry, unsigned bits,
                         LxExecutionBridge *binding)
{
    return bits == 16 ? nativeMissingTrampoline16(module, entry, binding)
                      : nativeMissingTrampoline(module, entry);
}

typedef struct NativeApiCall
{
    uint32_t *registers;
    uint32_t arguments;
    struct NativeApiCall *previous;
    LxExecutionRegisters32 resume_registers;
    int resume;
} NativeApiCall;

static _Thread_local NativeApiCall *NativeApi;
static _Thread_local uint32_t NativeResumeWords[14];

static __attribute__((noreturn)) void resumeApi32(const LxExecutionRegisters32 *r)
{
    /* POPAD ignores its ESP slot; switch to the target stack before restoring
       integer registers, selectors, flags and the continuation address. */
    const uint32_t words[] = {r->edi, r->esi, r->ebp, 0,     r->ebx, r->edx,    r->ecx,
                              r->eax, r->gs,  r->fs,  r->es, r->ds,  r->eflags, r->eip};
    /* The destination may overlap this C invocation's stack. Retain the frame
       in thread-local host storage and make no C calls after changing ESP. */
    memcpy(NativeResumeWords, words, sizeof words);
    __asm__ __volatile__("movl %1,%%esp; "
                         "pushl 52(%0); pushl 48(%0); pushl 44(%0); pushl 40(%0); "
                         "pushl 36(%0); pushl 32(%0); pushl 28(%0); pushl 24(%0); "
                         "pushl 20(%0); pushl 16(%0); pushl 12(%0); pushl 8(%0); "
                         "pushl 4(%0); pushl 0(%0); "
                         "popal; pop %%gs; pop %%fs; pop %%es; pop %%ds; popfl; ret"
                         :
                         : "a"(NativeResumeWords), "d"(r->esp)
                         : "memory");
    __builtin_unreachable();
}

static uint32_t dispatchApi32(LxExecutionApi16 callback, uint8_t *arguments, uint32_t *registers)
{
    NativeApiCall call = {0};
    call.registers = registers;
    call.arguments = arguments == (uint8_t *)(registers + 7) ? 0 : (uint32_t)(uintptr_t)arguments;
    call.previous = NativeApi;
    NativeApi = &call;
    uint32_t result = callback(arguments);
    NativeApi = call.previous;
    if (call.resume)
        resumeApi32(&call.resume_registers);
    return result;
}

uint32_t nativeApiArguments(void)
{
    return NativeApi ? NativeApi->arguments : 0;
}

int nativeApiRegisters32(LxExecutionRegisters32 *out)
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

int nativeApiResume32(const LxExecutionRegisters32 *registers)
{
    uint16_t cs, ss;
    __asm__ __volatile__("mov %%cs,%0; mov %%ss,%1" : "=r"(cs), "=r"(ss));
    if (!NativeApi || registers->cs != cs || registers->ss != ss || registers->esp < 56 ||
        !lx_execution_memory(Native->execution, registers->eip, 1, LX_GUEST_EXECUTE) ||
        !lx_execution_memory(Native->execution, registers->esp - 56, 56, LX_GUEST_WRITE))
        return 0;
    NativeApi->resume_registers = *registers;
    NativeApi->resume = 1;
    return 1;
}

uint16_t nativeFlatDataSelector(void)
{
    return Native->original_ds;
}

/* Bytes kept clear of live host frames when the C caller runs on the guest stack. */
#define CALL16_STACK_MARGIN 1024

uint32_t nativeGuestStack(void)
{
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    const uintptr_t base = (uintptr_t)NativeHostAllocation;
    if (NativeBridge && sp >= base && sp < base + NATIVE_HOST_STACK_SIZE)
        return NativeBridge->guest_stack;
    return (uint32_t)(sp - CALL16_STACK_MARGIN);
}

static pthread_mutex_t Call16Mutex = PTHREAD_MUTEX_INITIALIZER;

/* The one USE16 instruction every call returns through: a 32-bit far return
   popping the flat continuation stored directly above the target's arguments. */
static uint16_t call16Return(void)
{
    pthread_mutex_lock(&Call16Mutex);
    if (!Native->call16_return)
    {
        uint16_t selector;
        uint8_t *code = nativeAllocSegment(&selector, 1);
        if (code)
        {
            code[0] = 0x66;
            code[1] = 0xcb;
            Native->call16_return =
                (uint16_t)((selector << X86_SELECTOR_INDEX_SHIFT) | X86_SELECTOR_LDT_RING3);
        }
    }
    const uint16_t selector = Native->call16_return;
    pthread_mutex_unlock(&Call16Mutex);
    return selector;
}

int nativeCall16(uint32_t target, uint16_t ss, uint16_t top, const void *arguments, uint32_t bytes,
                 uint32_t *result)
{
    const uint16_t return_selector = call16Return();
    const uint32_t stack = top & ~3u, frame = 24 + bytes;
    if (!return_selector || stack < frame)
        return 0;
    const uint32_t sp0 = stack - frame;
    /* Frame, low to high: target far pointer, its return far pointer, arguments,
       flat continuation EIP and CS, saved flat ESP and SS. */
    uint8_t *p = (uint8_t *)(uintptr_t)(Native->ldt[ss >> X86_SELECTOR_INDEX_SHIFT] + sp0);
    memset(p, 0, frame);
    const uint16_t pointers[4] = {(uint16_t)target, (uint16_t)(target >> 16), 0, return_selector};
    memcpy(p, pointers, sizeof pointers);
    if (bytes)
        memcpy(p + 8, arguments, bytes);
    uint8_t *saved = p + 16 + bytes;
    uintptr_t sp;
    __asm__ __volatile__("movl %%esp,%0" : "=r"(sp));
    const uintptr_t base = (uintptr_t)NativeHostAllocation;
    void *previous = NativeHostStack;
    if (NativeBridge && sp >= base && sp < base + NATIVE_HOST_STACK_SIZE)
        NativeHostStack = (void *)((sp - NATIVE_STACK_RED_ZONE) & ~NATIVE_STACK_ALIGN_MASK);
    uint32_t ax = ss, dx = sp0;
    /* The frame, not a host register, carries the way back: the target owns every
       register, and the return path reloads SS:ESP as the original thunk does. */
    __asm__ __volatile__("pushfl\n\tpushal\n\tpushl %%ds\n\tpushl %%es\n\tpushl %%fs\n\t"
                         "call 2f\n"
                         "2:\n\tpopl %%ecx\n\t"
                         "addl $(1f-2b),%%ecx\n\t"
                         "movl %%ecx,-8(%%ebx)\n\t"
                         "movw %%cs,-4(%%ebx)\n\t"
                         "movl %%esp,(%%ebx)\n\t"
                         "movw %%ss,4(%%ebx)\n\t"
                         "movw %%ax,%%ss\n\t"
                         "movl %%edx,%%esp\n\t"
                         "lretw\n"
                         "1:\n\tmovzwl %%sp,%%esp\n\t"
                         "lss (%%esp),%%esp\n\t"
                         "movl %%eax,40(%%esp)\n\t"
                         "movl %%edx,32(%%esp)\n\t"
                         "popl %%fs\n\tpopl %%es\n\tpopl %%ds\n\tpopal\n\tpopfl"
                         : "+a"(ax), "+d"(dx)
                         : "b"(saved)
                         : "ecx", "memory", "cc");
    NativeHostStack = previous;
    *result = (dx << 16) | (ax & 0xffff);
    return 1;
}

size_t nativeEmitBridge32(uint8_t *output, size_t capacity, const LxExecutionApi32 *binding)
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
