/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
/* Process-level invocation contracts for native and emulated execution. */
#include "execution.h"
#include "guest/abi.h"
#include "guest/arguments.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static jmp_buf ProgramReturn;
static uint32_t ProgramResult;
static LxExecution *Execution;
static uint32_t CallbackEntry, TlsWindow, ReturnCallback, Leaf16Entry;
static int Concurrent, Arrivals;
static pthread_mutex_t BarrierMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t BarrierCondition = PTHREAD_COND_INITIALIZER;
static void returned(void *unused, uint32_t result)
{
    (void)unused;
    ProgramResult = result;
    if (ReturnCallback)
        lx_execution_callback32(Execution, ReturnCallback, result);
    longjmp(ProgramReturn, 1);
}
static uint32_t bridge(uint8_t *args)
{
    uint16_t a;
    uint32_t b;
    memcpy(&a, args, 2);
    memcpy(&b, args + 2, 4);
    CHECK(a == 7 && b == 0x12345678);
    if (Concurrent)
    {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 5;
        pthread_mutex_lock(&BarrierMutex);
        Arrivals++;
        pthread_cond_broadcast(&BarrierCondition);
        while (Arrivals < 2)
            CHECK(pthread_cond_timedwait(&BarrierCondition, &BarrierMutex, &deadline) != ETIMEDOUT);
        pthread_mutex_unlock(&BarrierMutex);
    }
    uint8_t *tls = (uint8_t *)lx_execution_current_tib(Execution) + 40;
    CHECK(lx_execution_memory(Execution, TlsWindow, 4, 3) == tls);
    LxGuestBorrow pinned;
    CHECK(lx_execution_borrow(Execution, TlsWindow, 4, 3, &pinned));
    CHECK(pinned.host == tls);
    uint32_t prior = lx_guest_load_u32(tls);
    lx_execution_callback32(Execution, CallbackEntry, 0xabcdef01);
    CHECK(lx_guest_load_u32(tls) == prior + 1);
    lx_execution_release(&pinned);
    return 0x4321;
}
static uint32_t bridge32(uint8_t *arguments)
{
#if defined(__i386__)
    __asm__ __volatile__("movl $0x11112222, %%ecx" : : : "ecx");
#endif
    return lx_guest_load_u32(arguments) + 1;
}
static void put32(uint8_t *p, uint32_t value)
{
    memcpy(p, &value, 4);
}
static void mapping_contract(void)
{
    LxGuestMapping *m = lx_execution_map(Execution, 0x05000000, 0x10000, 3, 1);
    CHECK(m);
    uint8_t *host = lx_execution_mapping_host(m);
    host[0] = 0x5a;
    CHECK(!lx_execution_map(Execution, 0x05000000, 0x10000, 3, 1));
    CHECK(host[0] == 0x5a);
    lx_guest_store_u32(host + 3, 0x12345678);
    uint32_t previous;
    CHECK(lx_execution_atomic_u32(Execution, 0x05000003, 1, 0xabcdef01, &previous));
    CHECK(previous == 0x12345678 && lx_guest_load_u32(host + 3) == 0xabcdef01);
    CHECK(lx_execution_atomic_u32(Execution, 0x05000003, 0, 0, &previous));
    CHECK(previous == 0xabcdef01);
    CHECK(!lx_execution_atomic_u32(Execution, 0x0500ffff, 1, 0,
                                   &previous)); /* A collision must never overwrite live memory. */
    uint32_t address;
    CHECK(lx_execution_address(Execution, host + 12, 4, LX_GUEST_WRITE, &address));
    CHECK(address == 0x0500000c);
    CHECK(lx_execution_memory(Execution, address, 4, LX_GUEST_READ) == host + 12);
    CHECK(!lx_execution_memory(Execution, 0x0500ffff, 2, LX_GUEST_READ));
    CHECK(!lx_execution_memory(Execution, UINT32_MAX, 2, 0));
    LxGuestBorrow borrow;
    CHECK(lx_execution_borrow(Execution, 0x05000003, 4, 3, &borrow));
    CHECK(borrow.host == host + 3);
    CHECK(!lx_execution_unmap(Execution, m));
    CHECK(!lx_execution_protect(Execution, m, LX_GUEST_READ));
    lx_execution_release(&borrow);
    lx_execution_release(&borrow);
    CHECK(!lx_execution_address(Execution, &address, sizeof address, 0, &address));
    CHECK(lx_execution_protect(Execution, m, LX_GUEST_READ));
    CHECK(!lx_execution_memory(Execution, 0x05000000, 1, LX_GUEST_WRITE));
    CHECK(lx_execution_unmap(Execution, m));
    CHECK(!lx_execution_memory(Execution, 0x05000000, 1, 0));
    puts("guest mapping ownership, address translation, collision and permission checks passed");
}
static void argument_contract(void)
{
    uint16_t selector;
    uint8_t *host = lx_execution_alloc_segment(Execution, &selector, 0);
    CHECK(host);
    uint32_t far_base = (uint32_t)((selector << 3) | 7) << 16, address;
    CHECK(lx_execution_address(Execution, host, 0x10000, 3, &address));
    LxGuestArgument argument;
    CHECK(!lx_guest_argument_far(Execution, far_base | 0xffff, 2, 3, 0, 0, &argument));
    CHECK(!lx_guest_argument_far(Execution, 0, 2, 3, 0, 0, &argument));
    CHECK(lx_guest_argument_far(Execution, 0, 2, 3, 0, 1, &argument));
    CHECK(!argument.host);
    lx_guest_argument_finish(&argument, 1);
    lx_guest_store_u16(host + 3, 0x1234);
    CHECK(lx_guest_argument_far(Execution, far_base | 3, 2, 3, 0, 0, &argument));
    CHECK((uintptr_t)argument.host % 8 == 0 && argument.host != host + 3);
    CHECK(lx_guest_load_u16(argument.host) == 0x1234);
    *(uint16_t *)argument.host = 0x5678;
    CHECK(lx_guest_load_u16(host + 3) == 0x1234);
    lx_guest_argument_finish(&argument, 1);
    CHECK(lx_guest_load_u16(host + 3) == 0x5678);
    memcpy(host + 16, "snapshot", 9);
    CHECK(lx_guest_argument_strings(Execution, far_base | 16, 1, 65536, 0, &argument));
    const char changed[] = "modified";
    CHECK(lx_execution_write(Execution, address + 16, changed, sizeof changed));
    CHECK(!strcmp(argument.host, "snapshot"));
    lx_guest_argument_finish(&argument, 0);
    CHECK(!strcmp((char *)host + 16, "modified"));
    host[0xffff] = 'x';
    CHECK(!lx_guest_argument_strings(Execution, far_base | 0xffff, 1, 65536, 0, &argument));
    CHECK(lx_execution_read(Execution, address, NULL, 0));
    CHECK(lx_execution_write(Execution, address, NULL, 0));
    lx_execution_free_segment(Execution, selector);
    puts("USE16 span bounds, aligned argument copies, output commit and string snapshots passed");
}
static void invoke32(void)
{
    LxGuestMapping *code = lx_execution_map(Execution, 0x06000000, 0x10000, 7, 1);
    LxGuestMapping *stack = lx_execution_map(Execution, 0x06100000, 0x10000, 3, 1);
    LxGuestMapping *data = lx_execution_map(Execution, 0x06200000, 0x10000, 3, 1);
    CHECK(code && stack && data);
    uint8_t *p = lx_execution_mapping_host(code);
    /* Program entry reads the four OS/2 startup words and returns a result. */
    const uint8_t program[] = {0x8b, 0x44, 0x24, 4,    0x03, 0x44, 0x24, 8,   0x03,
                               0x44, 0x24, 12,   0x03, 0x44, 0x24, 16,   0xc3};
    memcpy(p, program, sizeof program);
    /* DLL entry records module + termination, then returns to the host. */
    uint8_t library[] = {0x8b, 0x44, 0x24, 4, 0x03, 0x44, 0x24, 8, 0xa3, 0, 0, 0, 0, 0xc3};
    put32(library + 9, lx_execution_mapping_address(data));
    memcpy(p + 0x100, library, sizeof library);
    LxExecutionApi32 api = {bridge32, 4, 0, LX_ABI_STACK32};
    CHECK(lx_execution_emit_bridge32(Execution, p + 0x200, 64, &api));
    api.callee_pop = 4;
    CHECK(lx_execution_emit_bridge32(Execution, p + 0x240, 64, &api));
    api.callee_pop = 0;
    api.convention = LX_ABI_EAX32;
    CHECK(lx_execution_emit_bridge32(Execution, p + 0x280, 64, &api));
    uint8_t caller_pop[] = {0x68, 0x34, 0x12, 0,    0,    0xba, 0, 0,
                            0,    0,    0xff, 0xd2, 0x83, 0xc4, 4, 0xc3};
    put32(caller_pop + 6, lx_execution_mapping_address(code) + 0x200);
    memcpy(p + 0x300, caller_pop, sizeof caller_pop);
    uint8_t callee_pop[] = {0x68, 0x34, 0x12, 0, 0, 0xba, 0, 0, 0, 0, 0xff, 0xd2, 0xc3};
    put32(callee_pop + 6, lx_execution_mapping_address(code) + 0x240);
    memcpy(p + 0x340, callee_pop, sizeof callee_pop);
    uint8_t register_call[] = {0xb9, 0x78, 0x56, 0x34, 0x12, 0xb8, 0x34, 0x12, 0,    0,
                               0xba, 0,    0,    0,    0,    0xff, 0xd2, 0x81, 0xf9, 0x78,
                               0x56, 0x34, 0x12, 0x74, 2,    0x31, 0xc0, 0xc3};
    put32(register_call + 11, lx_execution_mapping_address(code) + 0x280);
    memcpy(p + 0x380, register_call, sizeof register_call);
    uint8_t post_return[] = {0x8b, 0x44, 0x24, 4, 0xa3, 0, 0, 0, 0, 0xc3};
    put32(post_return + 5, lx_execution_mapping_address(data) + 4);
    memcpy(p + 0x400, post_return, sizeof post_return);
    ReturnCallback = lx_execution_mapping_address(code) + 0x400;
    CHECK(lx_execution_protect(Execution, code, 5));
    LxExecutionEntry entry = {0};
    entry.entry = lx_execution_mapping_address(code);
    entry.stack = lx_execution_mapping_address(stack) + 0xff00;
    entry.module = 10;
    entry.command = 20;
    entry.environment = 30;
    if (!setjmp(ProgramReturn))
    {
        lx_execution_program(Execution, &entry, 32);
        CHECK(0);
    }
    CHECK(ProgramResult == 60);
    CHECK(lx_guest_load_u32((uint8_t *)lx_execution_mapping_host(data) + 4) == 60);
    entry.entry += 0x100;
    lx_execution_library(Execution, &entry, 1);
    uint32_t value;
    memcpy(&value, lx_execution_mapping_host(data), 4);
    CHECK(value == 11);
    for (unsigned i = 0; i < 3; i++)
    {
        entry.entry = lx_execution_mapping_address(code) + 0x300 + i * 0x40;
        if (!setjmp(ProgramReturn))
        {
            lx_execution_program(Execution, &entry, 32);
            CHECK(0);
        }
        CHECK(ProgramResult == 0x1235);
    }
    ReturnCallback = 0;
    CHECK(lx_execution_unmap(Execution, code));
    CHECK(lx_execution_unmap(Execution, stack));
    CHECK(lx_execution_unmap(Execution, data));
    puts("USE32 program/DLL entry, caller/callee cleanup and EAX import ABI passed");
}
typedef struct ThreadTest
{
    void *invocation;
    uint8_t *tib;
} ThreadTest;
static void *run_thread(void *userdata)
{
    ThreadTest *test = userdata;
    uint16_t token = lx_execution_attach_tib(Execution, test->tib);
    lx_execution_thread16(Execution, test->invocation);
    lx_execution_detach_tib(Execution, token);
    return NULL;
}
static uint32_t leaf16(uint8_t *arguments)
{
    (void)arguments;
    return 0x5555;
}
static uint32_t nested16(uint8_t *arguments)
{
    (void)arguments;
    uint16_t selector;
    uint8_t *stack = lx_execution_alloc_segment(Execution, &selector, 0);
    CHECK(stack);
    void *invocation = lx_execution_prepare_thread16(
        Execution, Leaf16Entry, ((uint32_t)((selector << 3) | 7) << 16) | 0xff00);
    CHECK(invocation);
    lx_execution_thread16(Execution, invocation);
    CHECK(lx_guest_load_u16(stack + 0x20) == 0x5555);
    lx_execution_destroy_thread16(Execution, invocation);
    lx_execution_free_segment(Execution, selector);
    return 0;
}
static void invoke16(void)
{
    LxGuestMapping *nested = lx_execution_map(Execution, 0x06400000, 0x10000, 7, 1);
    CHECK(nested);
    uint8_t callback_code[] = {0x8b, 0x44, 0x24, 4, 0xa3, 0,    0,    0,    0,
                               0xba, 0,    0,    0, 0,    0x8b, 0x02, 0x40, 0x89,
                               0x02, 0xba, 0,    0, 0,    0,    0xff, 0xd2, 0xc3};
    put32(callback_code + 10, TlsWindow);
    CallbackEntry = lx_execution_mapping_address(nested);
    LxExecutionApi32 nested_api = {nested16, 0, 0, LX_ABI_STACK32};
    CHECK(lx_execution_emit_bridge32(
        Execution, (uint8_t *)lx_execution_mapping_host(nested) + 0x200, 64, &nested_api));
    put32(callback_code + 20, CallbackEntry + 0x200);
    put32(callback_code + 5, CallbackEntry + 0x100);
    memcpy(lx_execution_mapping_host(nested), callback_code, sizeof callback_code);
    uint16_t code_selector, stack_selector, bridge_selector;
    uint8_t *code = lx_execution_alloc_segment(Execution, &code_selector, 1);
    uint8_t *stack = lx_execution_alloc_segment(Execution, &stack_selector, 0);
    uint8_t *api = lx_execution_alloc_segment(Execution, &bridge_selector, 1);
    CHECK(code && stack && api);
    CHECK(lx_execution_emit_bridge16(Execution, api, 0x10000, api, bridge_selector, bridge, 6));
    CHECK(lx_execution_emit_bridge16(Execution, api + 0x200, 0x10000 - 0x200, api, bridge_selector,
                                     leaf16, 0));
    uint8_t leaf_body[] = {0x9a, 0, 2, 0, 0, 0xa3, 0x20, 0, 0xcb};
    lx_guest_store_u16(leaf_body + 3, (bridge_selector << 3) | 7);
    memcpy(code + 0x100, leaf_body, sizeof leaf_body);
    Leaf16Entry = ((uint32_t)((code_selector << 3) | 7) << 16) | 0x100;
    /* Push Pascal arguments, CALL FAR to the API, store AX through DS and RETF.
       The invocation supplies a segmented SS/DS and a far return continuation. */
    uint8_t body[] = {0x66, 0x68, 0x78, 0x56, 0x34, 0x12, 0x6a, 7,   0x9a,
                      0,    0,    0,    0,    0xa3, 0x20, 0,    0xcb};
    uint16_t segment = (bridge_selector << 3) | 7;
    memcpy(body + 11, &segment, 2);
    memcpy(code, body, sizeof body);
    uint32_t entry = ((uint32_t)((code_selector << 3) | 7) << 16);
    uint32_t sp = ((uint32_t)((stack_selector << 3) | 7) << 16) | 0xff00;
    void *invocation = lx_execution_prepare_thread16(Execution, entry, sp);
    CHECK(invocation);
    lx_execution_thread16(Execution, invocation);
    uint16_t value;
    memcpy(&value, stack + 0x20, 2);
    CHECK(value == 0x4321);
    uint32_t callback_value;
    memcpy(&callback_value, (uint8_t *)lx_execution_mapping_host(nested) + 0x100, 4);
    CHECK(callback_value == 0xabcdef01);
    lx_execution_destroy_thread16(Execution, invocation);
    /* Two guests rendezvous inside a blocking API. Holding an engine lock
       across that API would deadlock before the second guest could arrive. */
    LxGuestMapping *tibs[2];
    uint16_t stacks[2];
    uint8_t *stack_hosts[2];
    ThreadTest tests[2];
    pthread_t threads[2];
    Concurrent = 1;
    Arrivals = 0;
    for (unsigned i = 0; i < 2; i++)
    {
        tibs[i] = lx_execution_map(Execution, 0x06500000 + i * 0x10000, 0x10000, 3, 1);
        CHECK(tibs[i]);
        tests[i].tib = lx_execution_mapping_host(tibs[i]);
        lx_guest_store_u32(tests[i].tib + 40, 100 + i);
        lx_guest_store_u32(tests[i].tib + offsetof(LxGuestTib, tib2),
                           lx_execution_mapping_address(tibs[i]) + sizeof(LxGuestTib));
        stack_hosts[i] = lx_execution_alloc_segment(Execution, &stacks[i], 0);
        CHECK(stack_hosts[i]);
        uint32_t thread_sp = ((uint32_t)((stacks[i] << 3) | 7) << 16) | 0xff00;
        tests[i].invocation = lx_execution_prepare_thread16(Execution, entry, thread_sp);
        CHECK(tests[i].invocation);
    }
    /* Keep the callback's mapped code/data alive through both invocations. */
    for (unsigned i = 0; i < 2; i++)
        CHECK(pthread_create(&threads[i], NULL, run_thread, &tests[i]) == 0);
    for (unsigned i = 0; i < 2; i++)
    {
        CHECK(pthread_join(threads[i], NULL) == 0);
        memcpy(&value, stack_hosts[i] + 0x20, 2);
        CHECK(value == 0x4321);
        lx_execution_destroy_thread16(Execution, tests[i].invocation);
        lx_execution_free_segment(Execution, stacks[i]);
        CHECK(lx_execution_unmap(Execution, tibs[i]));
    }
    Concurrent = 0;
    CHECK(lx_execution_unmap(Execution, nested));
    lx_execution_free_segment(Execution, code_selector);
    lx_execution_free_segment(Execution, stack_selector);
    lx_execution_free_segment(Execution, bridge_selector);
    puts(
        "USE16 thread entry, Pascal API bridges, nested callbacks and blocking two-thread rendezvous passed");
}
static uint32_t end_thread_bridge(uint8_t *arguments)
{
    CHECK(lx_guest_load_u32(arguments) == 42);
    lx_execution_end_thread(Execution);
    CHECK(0);
    return 0;
}
static void *run_thread32(void *userdata)
{
    ThreadTest *test = userdata;
    uint16_t token = lx_execution_attach_tib(Execution, test->tib);
    lx_execution_thread32(Execution, test->invocation);
    lx_execution_detach_tib(Execution, token);
    return NULL;
}
static void invoke_thread32(void)
{
    LxGuestMapping *code = lx_execution_map(Execution, 0x06800000, 0x10000, 7, 1);
    CHECK(code);
    uint8_t *p = lx_execution_mapping_host(code);
    LxExecutionApi32 api = {end_thread_bridge, 4, 0, LX_ABI_STACK32};
    CHECK(lx_execution_emit_bridge32(Execution, p + 0x200, 64, &api));
    /* Store the entry argument in TLS, exit through the API, and trap if the
       exit API returns into guest code. Both contexts use the same TLS address. */
    uint8_t body[] = {0x8b, 0x44, 0x24, 4, 0xba, 0, 0, 0,    0,    0x89, 0x02,
                      0x6a, 42,   0xba, 0, 0,    0, 0, 0xff, 0xd2, 0x0f, 0x0b};
    put32(body + 5, TlsWindow);
    put32(body + 14, lx_execution_mapping_address(code) + 0x200);
    memcpy(p, body, sizeof body);
    LxGuestMapping *tibs[2], *stacks[2];
    ThreadTest tests[2];
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; i++)
    {
        tibs[i] = lx_execution_map(Execution, 0x06900000 + i * 0x10000, 0x10000, 3, 1);
        stacks[i] = lx_execution_map(Execution, 0x06b00000 + i * 0x10000, 0x10000, 3, 1);
        CHECK(tibs[i] && stacks[i]);
        tests[i].tib = lx_execution_mapping_host(tibs[i]);
        lx_guest_store_u32(tests[i].tib + 12, lx_execution_mapping_address(tibs[i]) + 24);
        tests[i].invocation = lx_execution_prepare_thread32(
            Execution, lx_execution_mapping_address(code),
            lx_execution_mapping_address(stacks[i]) + 0xff00, 100 + i);
        CHECK(tests[i].invocation);
        CHECK(!pthread_create(&threads[i], NULL, run_thread32, &tests[i]));
    }
    for (unsigned i = 0; i < 2; i++)
    {
        CHECK(!pthread_join(threads[i], NULL));
        CHECK(lx_guest_load_u32(tests[i].tib + 40) == 100 + i);
        lx_execution_destroy_thread32(Execution, tests[i].invocation);
        CHECK(lx_execution_unmap(Execution, tibs[i]));
        CHECK(lx_execution_unmap(Execution, stacks[i]));
    }
    CHECK(lx_execution_unmap(Execution, code));
    puts("USE32 thread argument, per-thread TLS alias and non-returning exit passed");
}
int main(int argc, char **argv)
{
    const char *backend = argc > 1 ? argv[1] : "unicorn";
    LxExecutionServices services = {0};
    services.program_returned = returned;
    services.tib_size = sizeof(LxGuestTib) + sizeof(LxGuestTib2) + 128;
    services.tib2_offset = sizeof(LxGuestTib);
    services.tls_offset = sizeof(LxGuestTib) + sizeof(LxGuestTib2);
    char error[256];
    Execution = lx_execution_create(backend, &services, error, sizeof error);
    if (!Execution)
    {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    printf("process execution backend=%s\n", lx_execution_backend(Execution));
    LxGuestMapping *tib = lx_execution_map(Execution, 0x06300000, 0x10000, 3, 1);
    CHECK(tib);
    uint8_t *tib_host = lx_execution_mapping_host(tib);
    lx_guest_store_u32(tib_host + offsetof(LxGuestTib, tib2),
                       lx_execution_mapping_address(tib) + sizeof(LxGuestTib));
    uint16_t tib_token = lx_execution_attach_tib(Execution, tib_host);
    CHECK(lx_execution_current_tib(Execution) == tib_host);
    TlsWindow = lx_execution_tls_window(Execution);
    CHECK(TlsWindow);
    CHECK(lx_execution_write(Execution, TlsWindow + 124, "x\0\0", 3));
    char *tls_string = lx_execution_copy_string(Execution, TlsWindow + 124, 4096);
    CHECK(tls_string && !strcmp(tls_string, "x"));
    free(tls_string);
    tls_string = lx_execution_copy_strings(Execution, TlsWindow + 124, 4096, 0);
    CHECK(tls_string && !strcmp(tls_string, "x"));
    free(tls_string);
    CHECK(!lx_execution_copy_string(Execution, TlsWindow + 128, 4096));
    mapping_contract();
    argument_contract();
    invoke32();
    invoke16();
    invoke_thread32();
    lx_execution_detach_tib(Execution, tib_token);
    CHECK(lx_execution_unmap(Execution, tib));
    lx_execution_destroy(Execution);
    puts("Process execution contracts passed");
    return 0;
}
