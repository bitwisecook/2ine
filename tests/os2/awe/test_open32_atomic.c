/* Public domain. No rights reserved. Wholly AI-authored. */
/* Real guest backing, original return signs, unaligned words and host contenders. */
#include "os2/include/private/export.h"
#include "os2/include/os2win.h"
#include "host/thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LONG InterlockedIncrement(PLONG);
LONG InterlockedDecrement(PLONG);
LONG InterlockedExchange(PLONG, LONG);
static _Thread_local ULONG error;

void open32SetError(ULONG value)
{
    error = value;
}

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (error %u)\n", __FILE__, __LINE__, #x, error);              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void word_cases(uint8 *host)
{
    PLONG value = (PLONG)(void *)host;
    lx_guest_store_u32(host, 123);
    error = 0x5151;
    LONG incremented = InterlockedIncrement(value);
    if (incremented != 1 || lx_guest_load_u32(host) != 124)
        fprintf(stderr, "increment sign=%d word=%08x\n", incremented, lx_guest_load_u32(host));
    CHECK(incremented == 1 && lx_guest_load_u32(host) == 124);
    CHECK(InterlockedDecrement(value) == 1 && lx_guest_load_u32(host) == 123);
    CHECK(InterlockedExchange(value, -1) == 123 && lx_guest_load_u32(host) == UINT32_MAX);
    CHECK(InterlockedIncrement(value) == 0 && !lx_guest_load_u32(host));
    CHECK(InterlockedDecrement(value) == -1 && lx_guest_load_u32(host) == UINT32_MAX);
    CHECK(InterlockedExchange(value, 0x7fffffff) == -1);
    CHECK(InterlockedIncrement(value) == -1 && lx_guest_load_u32(host) == 0x80000000);
    CHECK(InterlockedDecrement(value) == 1 && lx_guest_load_u32(host) == 0x7fffffff);
    CHECK(InterlockedExchange(value, 1) == 0x7fffffff);
    CHECK(InterlockedDecrement(value) == 0 && !lx_guest_load_u32(host));
    CHECK(error == 0x5151);
}

typedef struct Contender
{
    PLONG word;
    unsigned iterations;
    int decrement;
} Contender;

static void *contend(void *argument)
{
    Contender *work = argument;
    error = 0x6161;
    for (unsigned i = 0; i < work->iterations; i++)
    {
        LONG sign =
            work->decrement ? InterlockedDecrement(work->word) : InterlockedIncrement(work->word);
        CHECK(sign >= -1 && sign <= 1 && error == 0x6161);
    }
    return NULL;
}

typedef struct GuestContender
{
    uint32 entry, word, iterations;
} GuestContender;

static void *guest_contend(void *argument)
{
    GuestContender *work = argument;
    CHECK(!lx_execution_callback32_4(GProcessState.execution, work->entry, work->word,
                                     work->iterations, 0, 0));
    lx_execution_cleanup_thread32(GProcessState.execution);
    return NULL;
}

static void contention(uint8 *host, uint32 code)
{
    enum
    {
        THREADS = 4,
        ITERATIONS = 2000,
        GUEST_ITERATIONS = 100000
    };

    LxHostThread *threads[THREADS];
    Contender work[THREADS];
    lx_guest_store_u32(host, 0);
    for (unsigned phase = 0; phase < 2; phase++)
    {
        GuestContender cpu = {code + phase * 32, 0, GUEST_ITERATIONS};
        CHECK(lx_execution_address(GProcessState.execution, host, 4, 3, &cpu.word));
        LxHostThread *guest_thread = lx_host_thread_create(guest_contend, &cpu, NULL);
        CHECK(guest_thread);
        for (unsigned i = 0; i < THREADS; i++)
        {
            work[i] = (Contender){(PLONG)(void *)host, ITERATIONS, phase != 0};
            threads[i] = lx_host_thread_create(contend, &work[i], 0);
            CHECK(threads[i]);
        }
        for (unsigned i = 0; i < THREADS; i++)
            CHECK(!lx_host_thread_join(threads[i], NULL));
        CHECK(!lx_host_thread_join(guest_thread, NULL));
        CHECK(lx_guest_load_u32(host) == (phase ? 0 : THREADS * ITERATIONS + GUEST_ITERATIONS));
    }
}

int main(void)
{
    char message[256];
    GProcessState.execution = lx_execution_create(TWOINE_PORTABLE_LOADER ? "unicorn" : "native",
                                                  NULL, message, sizeof message);
    CHECK(GProcessState.execution);
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0x05000000, 8192, 3, 1);
    CHECK(mapping);
    uint8 *host = lx_execution_mapping_host(mapping);
    memset(host, 0xa5, 8192);
    for (unsigned i = 0; i < 3; i++)
    {
        unsigned offset = i == 2 ? 4095 : i;
        word_cases(host + offset);
        CHECK(host[offset + 4] == 0xa5);
    }
    LxGuestMapping *code = lx_execution_map(GProcessState.execution, 0x05100000, 4096, 7, 1);
    CHECK(code);
    /* MOV EDX,[ESP+4]; MOV ECX,[ESP+8]; LOCK INC/DEC[EDX]; DEC ECX; JNZ; XOR EAX,EAX; RET. */
    uint8 loop[] = {0x8b, 0x54, 0x24, 4,    0x8b, 0x4c, 0x24, 8,   0xf0,
                    0xff, 2,    0x49, 0x75, 0xfa, 0x31, 0xc0, 0xc3};
    CHECK(lx_execution_write(GProcessState.execution, 0x05100000, loop, sizeof loop));
    loop[10] = 0x0a;
    CHECK(lx_execution_write(GProcessState.execution, 0x05100020, loop, sizeof loop));
    contention(host + 7, 0x05100000);    /* Host/API and x86 CPU share one unaligned word. */
    contention(host + 4095, 0x05100000); /* DWORD spanning two mapped guest pages. */
    LONG local = 777;
    CHECK(!InterlockedIncrement(&local) && local == 777 && error == 87);
    CHECK(!InterlockedDecrement(NULL) && error == 87);
    CHECK(!InterlockedExchange((PLONG)(uintptr_t)0xdeadbeef, 1) && error == 87);
    CHECK(!InterlockedIncrement((PLONG)(void *)(host + 8191)) && host[8191] == 0xa5);
    CHECK(lx_execution_protect(GProcessState.execution, mapping, LX_GUEST_READ));
    uint32 old = lx_guest_load_u32(host + 4095);
    CHECK(!InterlockedExchange((PLONG)(void *)(host + 4095), 123) && error == 87);
    CHECK(lx_guest_load_u32(host + 4095) == old);
    CHECK(lx_execution_unmap(GProcessState.execution, mapping));
    CHECK(!InterlockedIncrement((PLONG)(void *)host) && error == 87);
    lx_execution_destroy(GProcessState.execution);
    GProcessState.execution = NULL;
    GProcessState.execution = NULL;
    puts("Open32 interlocked signs, backing, cross-page/unaligned guards and contention passed");
    return 0;
}
