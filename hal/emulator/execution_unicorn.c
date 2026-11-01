/* Public domain. No rights reserved. Wholly AI-authored. */
/* Portable process entry and API dispatch over the CPU execution interface. */
#include "execution_internal.h"
#include "cpu.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SELECTOR_COUNT 8192
#define FLAT_CODE 0x1b
#define FLAT_DATA 0x23
#define API_VECTOR 0xf1
#define RETURN_VECTOR 0xf0

typedef struct Binding
{
    uint32_t address;
    uint32_t (*callback)(uint8_t *);
    uint16_t argument_bytes;
    LxExecutionAbi32 convention;
    char *missing_module, *missing_entry;
    struct Binding *next;
} Binding;
typedef struct Invocation
{
    LxCpuContext *context;
    struct Invocation *outer;
    LxGuestBorrow arguments;
    uint32_t api_arguments;
    const LxCpuRegisters *api_registers;
} Invocation;
typedef struct EmulatedState
{
    LxExecution *execution;
    LxCpu *cpu;
    LxCpuContext *initial;
    Invocation completed_main;
    void *tls_backing;
    LxSegment selectors[SELECTOR_COUNT];
    LxGuestMapping *segments[SELECTOR_COUNT];
    Binding *bindings;
    LxGuestMapping *return_page;
    uint32_t return_address, near_base;
    uint16_t return_selector;
    LxHostMutex *mutex, *segment_mutex;
} EmulatedState;
/* One guest address space per worker. Each host thread has its own CPU context;
   the shared engine is held only during CPU operations, never across an API. */
static EmulatedState *Emulated;
static __thread Invocation *Active;
static __thread uint8_t *ThreadTib;
static __thread uint16_t ThreadFs;

static __attribute__((noreturn)) void failure(const char *message)
{
    fprintf(stderr, "2ine execution: %s\n", message);
    abort();
}
static int map(LxExecution *e, uint32_t a, size_t n, unsigned p, void *host)
{
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    int ok = lx_cpu_map_borrowed(s->cpu, a, n, p, host);
    if (!ok)
        lx_execution_fail(e, lx_cpu_error(s->cpu));
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static int unmap(LxExecution *e, uint32_t a, size_t n)
{
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    int ok = lx_cpu_unmap(s->cpu, a, n);
    if (!ok)
        lx_execution_fail(e, lx_cpu_error(s->cpu));
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static int protect(LxExecution *e, uint32_t a, size_t n, unsigned p)
{
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    int ok = lx_cpu_protect(s->cpu, a, n, p);
    if (!ok)
        lx_execution_fail(e, lx_cpu_error(s->cpu));
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static int findSelector(uint32_t address, uint16_t *selector, uint16_t *offset, int code)
{
    EmulatedState *s = Emulated;
    if (address < 4096)
        return 0;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    unsigned slot = 0;
    const uint32_t base = address & ~0xffffu;
    for (unsigned i = 1; i < SELECTOR_COUNT - 1; i++)
    {
        if (s->selectors[i].selector && s->selectors[i].base == base &&
            (code < 0 || s->selectors[i].executable == code))
        {
            *selector = i;
            *offset = address - base;
            lx_host_mutex_unlock(s->mutex);
            return 1;
        }
        if (!slot && !s->selectors[i].selector)
            slot = i;
    }
    if (!slot)
    {
        lx_host_mutex_unlock(s->mutex);
        return 0;
    }
    if (code < 0)
        code =
            s->execution->services.query_executable
                ? s->execution->services.query_executable(s->execution->services.userdata, address)
                : 0;
    LxSegment segment = {(uint16_t)((slot << 3) | 7), base, 0xffff, 16, code > 0};
    int ok = lx_cpu_set_segment(s->cpu, &segment);
    if (ok)
    {
        s->selectors[slot] = segment;
        *selector = slot;
        *offset = address - base;
    }
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static void freeSelector(uint16_t selector)
{
    lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
    if (selector < SELECTOR_COUNT)
        memset(&Emulated->selectors[selector], 0, sizeof(LxSegment));
    lx_host_mutex_unlock(Emulated->mutex);
}
static void *farToHost(uint32_t pointer)
{
    if (!pointer)
        return NULL;
    uint16_t selector = pointer >> 16;
    uint32_t base;
    if (selector == 0xffff)
        base = Emulated->near_base;
    else
    {
        unsigned index = selector >> 3;
        if (!(selector & 4) || !Emulated->selectors[index].selector)
            return NULL;
        base = Emulated->selectors[index].base;
    }
    return lx_execution_memory(Emulated->execution, base + (pointer & 0xffff), 1, 0);
}
static uint32_t hostToFar(void *pointer)
{
    uint32_t address;
    uint16_t selector, offset;
    if (!pointer || !lx_execution_address(Emulated->execution, pointer, 1, 0, &address) ||
        !findSelector(address, &selector, &offset, -1))
        return 0;
    return ((uint32_t)((selector << 3) | 7) << 16) | offset;
}
static void *allocSegment(uint16_t *selector, int executable)
{
    EmulatedState *s = Emulated;
    *selector = 0xffff;
    lx_host_mutex_lock(s->segment_mutex, UINT32_MAX);
    for (uint32_t address = 0x08800000; address < 0x1fff0000; address += 0x10000)
    {
        LxGuestMapping *m = lx_execution_map(s->execution, address, 0x10000, 7, 1);
        if (!m)
            continue;
        uint16_t offset;
        if (findSelector(address, selector, &offset, executable))
        {
            s->segments[*selector] = m;
            lx_host_mutex_unlock(s->segment_mutex);
            return lx_execution_mapping_host(m);
        }
        lx_execution_unmap(s->execution, m);
        break;
    }
    lx_host_mutex_unlock(s->segment_mutex);
    return NULL;
}
static void freeSegment(uint16_t selector)
{
    if (selector >= SELECTOR_COUNT)
        return;
    lx_host_mutex_lock(Emulated->segment_mutex, UINT32_MAX);
    LxGuestMapping *m = Emulated->segments[selector];
    if (m && lx_execution_unmap(Emulated->execution, m))
    {
        Emulated->segments[selector] = NULL;
        freeSelector(selector);
    }
    lx_host_mutex_unlock(Emulated->segment_mutex);
}
static int codeAlias(uint16_t data, uint16_t *code)
{
    unsigned index = data >> 3;
    uint16_t slot, offset;
    if (index >= SELECTOR_COUNT || !Emulated->selectors[index].selector ||
        !findSelector(Emulated->selectors[index].base, &slot, &offset, 1))
        return 0;
    LxGuestMapping *m =
        lx_execution_mapping_at(Emulated->execution, Emulated->selectors[index].base);
    if (!m || !lx_execution_protect(Emulated->execution, m, 7))
    {
        freeSelector(slot);
        return 0;
    }
    *code = (slot << 3) | 7;
    return 1;
}
static void stackSegments(uint32_t address, uint32_t size, int release)
{
    uint64_t end = (uint64_t)address + size;
    for (uint64_t a = address & ~0xffffu; a < end; a += 0x10000)
    {
        uint16_t slot, offset;
        if (findSelector((uint32_t)a, &slot, &offset, 0) && release)
            freeSelector(slot);
    }
}
static void nearBase(uint32_t a)
{
    Emulated->near_base = a;
}
static uint16_t flatCode(void)
{
    return FLAT_CODE;
}
static void prepareCode(uint8_t *code, size_t size)
{
    (void)code;
    (void)size;
}
static uint16_t attachTib(uint8_t *tib)
{
    uint32_t address;
    uint16_t slot, offset;
    if (!lx_execution_address(Emulated->execution, tib, Emulated->execution->services.tib_size, 3,
                              &address) ||
        !findSelector(address, &slot, &offset, 0) || offset)
        failure("TIB must be in guest memory at a segment boundary");
    ThreadTib = tib;
    ThreadFs = (slot << 3) | 7;
    return slot;
}
static void *currentTib(void)
{
    return ThreadTib;
}
static void detachTib(uint16_t token)
{
    freeSelector(token);
    ThreadFs = 0;
    ThreadTib = NULL;
}
static size_t emitBridge16(uint8_t *output, size_t capacity, uint8_t *base, uint16_t selector,
                           LxExecutionApi16 callback, uint16_t bytes)
{
    (void)base;
    (void)selector;
    if (capacity < 5)
        return 0;
    Binding *b = calloc(1, sizeof *b);
    if (!b || !lx_execution_address(Emulated->execution, output, 5, LX_GUEST_WRITE, &b->address))
    {
        free(b);
        return 0;
    }
    b->callback = callback;
    b->argument_bytes = bytes;
    b->next = Emulated->bindings;
    Emulated->bindings = b;
    output[0] = 0xcd;
    output[1] = API_VECTOR;
    output[2] = 0xca;
    output[3] = bytes;
    output[4] = bytes >> 8;
    return 5;
}
static size_t emitBridge32(uint8_t *output, size_t capacity, const LxExecutionApi32 *api)
{
    if (capacity < 5 ||
        !emitBridge16(output, capacity, output, 0, api->callback, api->argument_bytes))
        return 0;
    Binding *b = Emulated->bindings;
    b->convention = api->convention;
    if (api->callee_pop)
    {
        output[2] = 0xc2;
        lx_guest_store_u16(output + 3, api->callee_pop);
        return 5;
    }
    output[2] = 0xc3;
    return 3;
}
static void replaceContext(Invocation *frame)
{
    LxCpuContext *next = lx_cpu_save(Emulated->cpu);
    if (!next)
        failure(lx_cpu_error(Emulated->cpu));
    lx_cpu_context_free(frame->context);
    frame->context = next;
}
/* The CPU engine is serialized. Rebind the process-wide TLS alias when the
   host thread changes; no guest CPU operation observes another thread's TIB. */
static void bindThreadWindow(EmulatedState *s)
{
    if (!s->execution->tls_window || s->tls_backing == ThreadTib)
        return;
    if (!ThreadTib)
        failure("Thread window requires an attached TIB");
    const uint32_t base = s->execution->tls_address - s->execution->services.tls_offset;
    if (!lx_cpu_unmap(s->cpu, base, 4096) ||
        !lx_cpu_map_borrowed(s->cpu, base, 4096, LX_MEM_READ | LX_MEM_WRITE, ThreadTib))
        failure(lx_cpu_error(s->cpu));
    s->tls_backing = ThreadTib;
}
static uint32_t invoke(LxCpuRegisters registers, int preserve)
{
    EmulatedState *s = Emulated;
    Invocation frame = {0};
    frame.outer = Active;
    Active = &frame;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    if (!lx_cpu_restore(s->cpu, frame.outer ? frame.outer->context : s->initial) ||
        !lx_cpu_set_registers(s->cpu, &registers))
        failure(lx_cpu_error(s->cpu));
    replaceContext(&frame);
    lx_host_mutex_unlock(s->mutex);
    for (;;)
    {
        lx_host_mutex_lock(s->mutex, UINT32_MAX);
        if (!lx_cpu_restore(s->cpu, frame.context))
            failure(lx_cpu_error(s->cpu));
        bindThreadWindow(s);
        LxCpuEvent event = lx_cpu_run(s->cpu, 0, 10000);
        if (!lx_cpu_get_registers(s->cpu, &registers))
            failure(lx_cpu_error(s->cpu));
        replaceContext(&frame);
        lx_host_mutex_unlock(s->mutex);
        if (event.reason == LX_CPU_BUDGET)
            continue;
        if (event.reason == LX_CPU_TRAP && event.vector == RETURN_VECTOR)
            break;
        if (event.reason != LX_CPU_TRAP || event.vector != API_VECTOR)
        {
            char error[512];
            snprintf(error, sizeof error,
                     "CPU stop %d vector %u at %04x:%08x "
                     "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x "
                     "ss:esp=%04x:%08x ds=%04x es=%04x fs=%04x gs=%04x: %s",
                     event.reason, event.vector, registers.cs, registers.eip, registers.eax,
                     registers.ebx, registers.ecx, registers.edx, registers.esi, registers.edi,
                     registers.ss, registers.esp, registers.ds, registers.es, registers.fs,
                     registers.gs, lx_cpu_error(s->cpu));
            lx_host_mutex_lock(s->mutex, UINT32_MAX);
            uint8_t *bytes =
                lx_cpu_translate_far(s->cpu, registers.cs, registers.eip, 16, LX_MEM_READ);
            if (bytes)
            {
                fprintf(stderr, "guest instruction:");
                for (unsigned i = 0; i < 16; i++)
                    fprintf(stderr, " %02x", bytes[i]);
                fprintf(stderr, "\n");
            }
            bytes =
                lx_cpu_translate_far(s->cpu, registers.ss, registers.esp & 0xffff, 16, LX_MEM_READ);
            if (bytes)
            {
                fprintf(stderr, "guest stack:");
                for (unsigned i = 0; i < 16; i++)
                    fprintf(stderr, " %02x", bytes[i]);
                fprintf(stderr, "\n");
            }
            lx_host_mutex_unlock(s->mutex);
            failure(error);
        }
        uint32_t linear = registers.eip - 2;
        if (registers.cs & 4)
            linear += s->selectors[registers.cs >> 3].base;
        Binding *b;
        for (b = s->bindings; b && b->address != linear; b = b->next)
        {
        }
        if (!b)
            failure("Unregistered guest API trap");
        if (!b->callback)
        {
            char message[320];
            snprintf(message, sizeof message, "Missing import %s:%s", b->missing_module,
                     b->missing_entry);
            failure(message);
        }
        const unsigned ss_index = registers.ss >> 3;
        const uint32_t sp = (registers.ss & 4) ? (registers.esp & 0xffff) : registers.esp;
        uint32_t stack_base = (registers.ss & 4) ? s->selectors[ss_index].base : 0;
        const uint32_t stack_arguments = b->convention == LX_ABI_EAX32 ? 0 : b->argument_bytes;
        if ((registers.ss & 4) &&
            (!s->selectors[ss_index].selector ||
             (uint64_t)sp + 4 + stack_arguments > (uint64_t)s->selectors[ss_index].limit + 1))
            failure("API frame exceeds its stack segment");
        const uint64_t frame_address = (uint64_t)stack_base + sp + 4;
        if (frame_address + stack_arguments > UINT64_C(0x100000000))
            failure("API frame address overflows");
        uint8_t eax_frame[4];
        lx_guest_store_u32(eax_frame, registers.eax);
        LxGuestBorrow *arguments = &frame.arguments;
        uint8_t *args = eax_frame;
        if (b->convention != LX_ABI_EAX32)
        {
            if (!lx_execution_borrow(s->execution, (uint32_t)frame_address, b->argument_bytes,
                                     LX_GUEST_READ, arguments))
                failure(lx_execution_error(s->execution));
            args = arguments->host;
        }
        frame.api_arguments = b->convention == LX_ABI_EAX32 ? 0 : (uint32_t)frame_address;
        frame.api_registers = &registers;
        registers.eax = b->callback(args);
        frame.api_registers = NULL;
        frame.api_arguments = 0;
        lx_execution_release(arguments);
        lx_host_mutex_lock(s->mutex, UINT32_MAX);
        if (!lx_cpu_restore(s->cpu, frame.context) || !lx_cpu_set_registers(s->cpu, &registers))
            failure(lx_cpu_error(s->cpu));
        replaceContext(&frame);
        lx_host_mutex_unlock(s->mutex);
    }
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    if (preserve)
    {
        lx_cpu_context_free(s->completed_main.context);
        s->completed_main.context = frame.context;
        s->completed_main.outer = NULL;
        Active = &s->completed_main;
    }
    else
    {
        lx_cpu_context_free(frame.context);
        Active = frame.outer;
    }
    lx_host_mutex_unlock(s->mutex);
    return registers.eax;
}
static LxCpuRegisters initialRegisters(void)
{
    LxCpuRegisters r = {0};
    r.cs = FLAT_CODE;
    r.ds = r.es = r.ss = FLAT_DATA;
    r.fs = ThreadFs;
    r.eflags = 2;
    return r;
}
static void push32(LxCpuRegisters *r, uint32_t word)
{
    r->esp -= 4;
    uint8_t *p = lx_execution_memory(Emulated->execution, r->esp, 4, LX_GUEST_WRITE);
    if (!p)
        failure("Invalid guest invocation stack");
    lx_guest_store_u32(p, word);
}
static void program(const LxExecutionEntry *e, unsigned bits)
{
    if (Active == &Emulated->completed_main)
        Active = NULL;
    LxCpuRegisters r = initialRegisters();
    if (bits == 32)
    {
        r.eip = e->entry;
        r.esp = e->stack;
        push32(&r, e->command);
        push32(&r, e->environment);
        push32(&r, 0);
        push32(&r, e->module);
        push32(&r, Emulated->return_address);
    }
    else
    {
        uint16_t slot, offset;
        if (!findSelector(e->command, &slot, &offset, 0))
            failure("Invalid guest command segment");
        r.cs = e->entry >> 16;
        r.eip = e->entry & 0xffff;
        r.ss = e->stack >> 16;
        r.esp = e->stack & 0xffff;
        r.ds = e->data_selector ? e->data_selector : r.ss;
        r.es = 0;
        r.eax = (slot << 3) | 7;
        r.ebx = offset;
        r.ecx = e->data_bytes;
        uint16_t words[] = {
            r.ss,  (uint16_t)(r.esp - e->stack_bytes), r.ss, (uint16_t)r.esp, (uint16_t)r.eax,
            offset};
        r.esp -= sizeof words;
        uint32_t a = Emulated->selectors[r.ss >> 3].base + r.esp;
        void *p = lx_execution_memory(Emulated->execution, a, sizeof words, LX_GUEST_WRITE);
        if (!p)
            failure("Invalid guest program stack");
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
            lx_guest_store_u16((uint8_t *)p + i * 2, words[i]);
    }
    uint32_t result = invoke(r, 1);
    Emulated->execution->services.program_returned(Emulated->execution->services.userdata, result);
}
static void library(const LxExecutionEntry *e, int termination)
{
    LxCpuRegisters r = initialRegisters();
    r.eip = e->entry;
    if (e->stack)
        r.esp = e->stack;
    else if (Active)
    {
        lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
        lx_cpu_restore(Emulated->cpu, Active->context);
        lx_cpu_get_registers(Emulated->cpu, &r);
        lx_host_mutex_unlock(Emulated->mutex);
        if (r.ss & 4)
            r.esp = Emulated->selectors[r.ss >> 3].base + (r.esp & 0xffff);
        r.ss = r.ds = r.es = FLAT_DATA;
        r.eip = e->entry;
        r.cs = FLAT_CODE;
    }
    else
        failure("Library invocation needs a guest stack");
    push32(&r, termination);
    push32(&r, e->module);
    push32(&r, Emulated->return_address);
    invoke(r, 0);
}
static __thread jmp_buf *ThreadReturn;
static __thread Invocation *ThreadOuter;
typedef struct ThreadEntry
{
    uint32_t entry, stack;
} ThreadEntry;
static void *prepareThread(uint32_t entry, uint32_t stack)
{
    ThreadEntry *p = malloc(sizeof *p);
    if (p)
    {
        p->entry = entry;
        p->stack = stack;
    }
    return p;
}
static void thread16(void *p)
{
    ThreadEntry *e = p;
    LxCpuRegisters r = initialRegisters();
    r.cs = e->entry >> 16;
    r.eip = e->entry & 0xffff;
    r.ss = r.ds = r.es = e->stack >> 16;
    r.esp = (e->stack & 0xffff) - 4;
    uint16_t words[] = {0, Emulated->return_selector};
    lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
    void *stack = lx_cpu_translate_far(Emulated->cpu, r.ss, r.esp, 4, LX_MEM_WRITE);
    if (!stack)
        failure("Invalid guest thread stack");
    lx_guest_store_u16(stack, words[0]);
    lx_guest_store_u16((uint8_t *)stack + 2, words[1]);
    lx_host_mutex_unlock(Emulated->mutex);
    jmp_buf target;
    jmp_buf *outer_return = ThreadReturn;
    Invocation *outer_frame = ThreadOuter;
    ThreadReturn = &target;
    ThreadOuter = Active;
    if (!setjmp(target))
        invoke(r, 0);
    ThreadReturn = outer_return;
    ThreadOuter = outer_frame;
}
static void callback(uint32_t entry, uint32_t argument)
{
    if (!Active)
        failure("Guest callback requires an active guest invocation");
    if (!lx_execution_memory(Emulated->execution, entry, 1, LX_GUEST_EXECUTE))
        failure("Invalid guest callback entry");
    LxCpuRegisters r;
    lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
    lx_cpu_restore(Emulated->cpu, Active->context);
    lx_cpu_get_registers(Emulated->cpu, &r);
    lx_host_mutex_unlock(Emulated->mutex);
    if (r.ss & 4)
        r.esp = Emulated->selectors[r.ss >> 3].base + (r.esp & 0xffff);
    r.ss = r.ds = r.es = FLAT_DATA;
    r.cs = FLAT_CODE;
    r.eip = entry;
    push32(&r, argument);
    push32(&r, Emulated->return_address);
    invoke(r, 0);
}
static uint32_t callback4(uint32_t entry, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (!Active)
        failure("Guest callback requires an active guest invocation");
    if (!lx_execution_memory(Emulated->execution, entry, 1, LX_GUEST_EXECUTE))
        failure("Invalid guest callback entry");
    LxCpuRegisters r;
    lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
    lx_cpu_restore(Emulated->cpu, Active->context);
    lx_cpu_get_registers(Emulated->cpu, &r);
    lx_host_mutex_unlock(Emulated->mutex);
    if (r.ss & 4)
        r.esp = Emulated->selectors[r.ss >> 3].base + (r.esp & 0xffff);
    r.ss = r.ds = r.es = FLAT_DATA;
    r.cs = FLAT_CODE;
    r.eip = entry;
    push32(&r, d);
    push32(&r, c);
    push32(&r, b);
    push32(&r, a);
    push32(&r, Emulated->return_address);
    return invoke(r, 0);
}
typedef struct Thread32Entry
{
    uint32_t entry, stack, argument;
} Thread32Entry;
static void *prepareThread32(uint32_t entry, uint32_t stack, uint32_t argument)
{
    Thread32Entry *p = malloc(sizeof *p);
    if (p)
    {
        p->entry = entry;
        p->stack = stack;
        p->argument = argument;
    }
    return p;
}
static void thread32(void *invocation)
{
    Thread32Entry *entry = invocation;
    jmp_buf target;
    jmp_buf *outer_return = ThreadReturn;
    Invocation *outer_frame = ThreadOuter;
    ThreadReturn = &target;
    ThreadOuter = Active;
    if (!setjmp(target))
    {
        LxCpuRegisters r = initialRegisters();
        r.eip = entry->entry;
        r.esp = entry->stack;
        push32(&r, entry->argument);
        push32(&r, Emulated->return_address);
        invoke(r, 0);
    }
    ThreadReturn = outer_return;
    ThreadOuter = outer_frame;
}
static int endThread(void)
{
    if (!ThreadReturn)
        return 0;
    while (Active != ThreadOuter)
    {
        Invocation *frame = Active;
        lx_execution_release(&frame->arguments);
        lx_host_mutex_lock(Emulated->mutex, UINT32_MAX);
        lx_cpu_context_free(frame->context);
        Active = frame->outer;
        lx_host_mutex_unlock(Emulated->mutex);
    }
    longjmp(*ThreadReturn, 1);
}
static void *missing(const char *module, const char *entry, unsigned bits, LxExecutionBridge *b)
{
    uint16_t selector;
    uint8_t *code = allocSegment(&selector, 1);
    if (!code)
        failure("Cannot allocate missing-import entry");
    if (!emitBridge16(code, 0x10000, code, selector, NULL, 0))
        failure("Cannot bind missing-import entry");
    if (bits == 32)
        code[2] = 0xc3;
    Binding *binding = Emulated->bindings;
    binding->missing_module = strdup(module);
    binding->missing_entry = strdup(entry);
    if (!binding->missing_module || !binding->missing_entry)
        failure("Out of memory");
    if (b)
    {
        b->host = code;
        b->address = binding->address;
        b->segment_base = binding->address;
        b->size = 0x10000;
        b->selector_index = selector;
    }
    return code;
}
static int initialize(LxExecution *e)
{
    if (Emulated)
        return lx_execution_fail(e, "An emulated address space is already active in this worker");
    EmulatedState *s = calloc(1, sizeof *s);
    if (!s)
        return lx_execution_fail(e, "Out of memory");
    s->execution = e;
    e->backend = s;
    s->mutex = lx_host_mutex_create(0, 0);
    if (!s->mutex)
    {
        free(s);
        e->backend = NULL;
        return lx_execution_fail(e, "Host CPU lock allocation failed");
    }
    s->segment_mutex = lx_host_mutex_create(0, 0);
    if (!s->segment_mutex)
    {
        lx_host_mutex_destroy(s->mutex);
        free(s);
        e->backend = NULL;
        return lx_execution_fail(e, "Host segment lock allocation failed");
    }
    s->cpu = lx_cpu_create("unicorn", e->error, sizeof e->error);
    if (!s->cpu)
    {
        lx_host_mutex_destroy(s->segment_mutex);
        lx_host_mutex_destroy(s->mutex);
        free(s);
        e->backend = NULL;
        return 0;
    }
    Emulated = s;
    LxSegment code = {FLAT_CODE, 0, UINT32_MAX, 32, 1};
    LxSegment data = {FLAT_DATA, 0, UINT32_MAX, 32, 0};
    if (!lx_cpu_set_segment(s->cpu, &code) || !lx_cpu_set_segment(s->cpu, &data))
        failure(lx_cpu_error(s->cpu));
    s->return_page = lx_execution_map(e, 0, 0x10000, 7, 0);
    if (!s->return_page)
        failure(lx_execution_error(e));
    s->return_address = lx_execution_mapping_address(s->return_page);
    uint8_t *p = lx_execution_mapping_host(s->return_page);
    p[0] = 0xcd;
    p[1] = RETURN_VECTOR;
    uint16_t slot, offset;
    if (!findSelector(s->return_address, &slot, &offset, 1))
        failure("Cannot allocate guest return segment");
    s->return_selector = (slot << 3) | 7;
    s->initial = lx_cpu_save(s->cpu);
    if (!s->initial)
        failure(lx_cpu_error(s->cpu));
    return 1;
}
static void destroy(LxExecution *e)
{
    EmulatedState *s = e->backend;
    while (s->bindings)
    {
        Binding *b = s->bindings;
        s->bindings = b->next;
        free(b->missing_module);
        free(b->missing_entry);
        free(b);
    }
    if (Active == &s->completed_main)
        Active = NULL;
    lx_cpu_context_free(s->completed_main.context);
    lx_cpu_context_free(s->initial);
    lx_cpu_destroy(s->cpu);
    lx_host_mutex_destroy(s->segment_mutex);
    lx_host_mutex_destroy(s->mutex);
    free(s);
    Emulated = NULL;
}
static int atomicU32(LxExecution *e, uint32_t address, void *host, int exchange, uint32_t value,
                     uint32_t *previous)
{
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    *previous = lx_guest_load_u32(host);
    int ok = 1;
    if (exchange)
    {
        uint8_t bytes[4];
        lx_guest_store_u32(bytes, value);
        bindThreadWindow(s);
        ok = lx_cpu_write(s->cpu, address, bytes, sizeof bytes);
    }
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static void copyOut(LxExecution *e, const void *host, void *output, size_t size)
{
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    memcpy(output, host, size);
    lx_host_mutex_unlock(s->mutex);
}
static int copyIn(LxExecution *e, uint32_t address, void *host, const void *input, size_t size)
{
    (void)host;
    EmulatedState *s = e->backend;
    lx_host_mutex_lock(s->mutex, UINT32_MAX);
    bindThreadWindow(s);
    int ok = lx_cpu_write(s->cpu, address, input, size);
    lx_host_mutex_unlock(s->mutex);
    return ok;
}
static uint32_t apiArguments(void)
{
    return Active ? Active->api_arguments : 0;
}
static int apiRegisters32(LxExecutionRegisters32 *out)
{
    if (!Active || !Active->api_registers)
        return 0;
    const LxCpuRegisters *r = Active->api_registers;
    out->eax = r->eax;
    out->ebx = r->ebx;
    out->ecx = r->ecx;
    out->edx = r->edx;
    out->esi = r->esi;
    out->edi = r->edi;
    out->ebp = r->ebp;
    out->esp = r->esp + 4;
    uint8_t return_address[4];
    if (!lx_execution_read(Emulated->execution, r->esp, return_address, 4))
        return 0;
    out->eip = lx_guest_load_u32(return_address);
    out->eflags = r->eflags;
    out->cs = r->cs;
    out->ds = r->ds;
    out->es = r->es;
    out->fs = r->fs;
    out->gs = r->gs;
    out->ss = r->ss;
    return 1;
}
const LxExecutionOperations lx_execution_unicorn = {
    "unicorn",     initialize,      destroy,     program,
    library,       prepareThread,   thread16,    free,
    callback,      attachTib,       currentTib,  detachTib,
    allocSegment,  freeSegment,     codeAlias,   findSelector,
    freeSelector,  farToHost,       hostToFar,   stackSegments,
    nearBase,      flatCode,        prepareCode, missing,
    emitBridge16,  atomicU32,       NULL,        0,
    map,           unmap,           protect,     emitBridge32,
    copyOut,       prepareThread32, thread32,    free,
    endThread,     copyIn,          callback4,   apiArguments,
    apiRegisters32};
