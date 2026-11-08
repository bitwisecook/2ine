/* Public domain. No rights reserved. Wholly AI-authored. */
/* Selection and dispatch for the process execution boundary. */
#include "emulator/execution/core/backend.h"
#include "host/thread.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int lx_execution_fail(LxExecution *e, const char *message)
{
    snprintf(e->error, sizeof e->error, "%s", message);
    return 0;
}

LxExecution *lx_execution_create(const char *backend, const LxExecutionServices *services,
                                 char *error, size_t size)
{
    LxExecution *e = calloc(1, sizeof *e);
    if (!e)
    {
        if (error && size)
            snprintf(error, size, "Out of memory");
        return NULL;
    }
    e->memory_mutex = lx_host_mutex_create(0, 0);
    if (!e->memory_mutex)
    {
        if (error && size)
            snprintf(error, size, "Host memory lock allocation failed");
        free(e);
        return NULL;
    }
    const int automatic = !backend || !strcmp(backend, "auto");
    const LxExecutionOperations *candidates[2];
    unsigned count = 0;
#if defined(TWOINE_NATIVE_LINUX_I386)
    if (automatic || !strcmp(backend, "native"))
        candidates[count++] = &lx_execution_native_linux;
#endif
#if defined(TWOINE_WITH_UNICORN)
    if (automatic || !strcmp(backend, "unicorn"))
        candidates[count++] = &lx_execution_unicorn;
#endif
    (void)automatic;
    if (services)
        e->services = *services;
    if (!count)
        snprintf(e->error, sizeof e->error, "Execution backend '%s' is not linked",
                 backend ? backend : "auto");
    int ready = 0;
    for (unsigned i = 0; i < count; i++)
    {
        e->ops = candidates[i];
        if (e->ops->initialize(e))
        {
            ready = 1;
            break;
        }
    }
    if (!ready)
    {
        if (error && size)
            snprintf(error, size, "%s", e->error);
        lx_host_mutex_destroy(e->memory_mutex);
        free(e);
        return NULL;
    }
    if (error && size)
        error[0] = '\0';
    return e;
}

void lx_execution_destroy(LxExecution *e)
{
    if (!e)
        return;
    while (e->memory)
    {
        if (!lx_execution_unmap(e, e->memory))
            abort();
    }
    e->ops->destroy(e);
    lx_host_mutex_destroy(e->memory_mutex);
    free(e);
}

const char *lx_execution_backend(const LxExecution *e)
{
    return e->ops->name;
}

const char *lx_execution_error(const LxExecution *e)
{
    return e->error;
}

void lx_execution_program(LxExecution *e, const LxExecutionEntry *p, unsigned b)
{
    e->ops->program(p, b);
}

void lx_execution_library(LxExecution *e, const LxExecutionEntry *p, int term)
{
    e->ops->library(p, term);
}

void *lx_execution_prepare_thread16(LxExecution *e, uint32_t p, uint32_t s)
{
    return e->ops->prepare_thread16(p, s);
}

void lx_execution_thread16(LxExecution *e, void *p)
{
    e->ops->thread16(p);
}

void lx_execution_destroy_thread16(LxExecution *e, void *p)
{
    if (p)
        e->ops->destroy_thread16(p);
}

void lx_execution_callback32(LxExecution *e, uint32_t p, uint32_t a)
{
    e->ops->callback32(p, a);
}

uint16_t lx_execution_attach_tib(LxExecution *e, uint8_t *p)
{
    return e->ops->attach_tib(p);
}

void *lx_execution_current_tib(LxExecution *e)
{
    return e->ops->current_tib();
}

void lx_execution_detach_tib(LxExecution *e, uint16_t t)
{
    e->ops->detach_tib(t);
}

void *lx_execution_alloc_segment(LxExecution *e, uint16_t *s, int x)
{
    return e->ops->alloc_segment(s, x);
}

void lx_execution_free_segment(LxExecution *e, uint16_t s)
{
    e->ops->free_segment(s);
}

int lx_execution_code_alias(LxExecution *e, uint16_t s, uint16_t *out)
{
    return e->ops->code_alias(s, out);
}

int lx_execution_find_selector(LxExecution *e, uint32_t a, uint16_t *s, uint16_t *o, int x)
{
    return e->ops->find_selector(a, s, o, x);
}

void lx_execution_free_selector(LxExecution *e, uint16_t s)
{
    e->ops->free_selector(s);
}

void *lx_execution_far_to_host(LxExecution *e, uint32_t p)
{
    return e->ops->far_to_host(p);
}

uint32_t lx_execution_host_to_far(LxExecution *e, void *p)
{
    return e->ops->host_to_far(p);
}

void lx_execution_stack_segments(LxExecution *e, uint32_t a, uint32_t n, int r)
{
    e->ops->stack_segments(a, n, r);
}

void lx_execution_near_base(LxExecution *e, uint32_t a)
{
    e->ops->near_base(a);
}

uint16_t lx_execution_flat_code_selector(LxExecution *e)
{
    return e->ops->flat_code_selector();
}

void lx_execution_prepare_code(LxExecution *e, uint8_t *p, size_t n)
{
    e->ops->prepare_code(p, n);
}

void *lx_execution_missing_entry(LxExecution *e, const char *m, const char *n, unsigned b,
                                 LxExecutionBridge *x)
{
    return e->ops->missing_entry(m, n, b, x);
}

size_t lx_execution_emit_bridge16(LxExecution *e, uint8_t *p, size_t n, uint8_t *base, uint16_t s,
                                  LxExecutionApi16 fn, uint16_t args)
{
    return e->ops->emit_bridge16(p, n, base, s, fn, args);
}

int lx_execution_entry_address(LxExecution *e, void *host, uint32_t *address)
{
    if (lx_execution_address(e, host, 1, LX_GUEST_EXECUTE, address))
        return 1;
    if (e->ops->native_entry)
        return e->ops->native_entry(e, host, address);
    return lx_execution_fail(e, "Host API requires a generated guest binding");
}

uint32_t lx_execution_api_arguments(LxExecution *e)
{
    return e && e->ops->api_arguments ? e->ops->api_arguments() : 0;
}

size_t lx_execution_emit_bridge32(LxExecution *e, uint8_t *output, size_t capacity,
                                  const LxExecutionApi32 *binding)
{
    if (!binding || !binding->callback ||
        (binding->convention != LX_ABI_STACK32 && binding->convention != LX_ABI_EAX32) ||
        binding->argument_bytes % 4 || binding->callee_pop % 4 ||
        binding->callee_pop > binding->argument_bytes ||
        (binding->convention == LX_ABI_EAX32 &&
         (binding->argument_bytes != 4 || binding->callee_pop)))
        return 0;
    return e->ops->emit_bridge32(output, capacity, binding);
}

void *lx_execution_prepare_thread32(LxExecution *e, uint32_t entry, uint32_t stack,
                                    uint32_t argument)
{
    if (!lx_execution_memory(e, entry, 1, LX_GUEST_EXECUTE) || stack < 8 ||
        !lx_execution_memory(e, stack - 8, 8, LX_GUEST_WRITE))
        return NULL;
    return e->ops->prepare_thread32(entry, stack, argument);
}

void lx_execution_thread32(LxExecution *e, void *invocation)
{
    e->ops->thread32(invocation);
}

void lx_execution_destroy_thread32(LxExecution *e, void *invocation)
{
    if (invocation)
        e->ops->destroy_thread32(invocation);
}

void lx_execution_cleanup_thread32(LxExecution *e)
{
    if (e->ops->cleanup_thread32)
        e->ops->cleanup_thread32();
}

int lx_execution_end_thread(LxExecution *e)
{
    return e->ops->end_thread();
}

uint32_t lx_execution_callback32_4(LxExecution *e, uint32_t entry, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d)
{
    return e->ops->callback32_4(entry, a, b, c, d);
}

int lx_execution_api_registers32(LxExecution *e, LxExecutionRegisters32 *registers)
{
    return e && registers && e->ops->api_registers32 ? e->ops->api_registers32(registers) : 0;
}

int lx_execution_api_resume32(LxExecution *e, const LxExecutionRegisters32 *registers)
{
    return e && registers && e->ops->api_resume32 ? e->ops->api_resume32(registers) : 0;
}

uint16_t lx_execution_flat_data_selector(LxExecution *e)
{
    return e && e->ops->flat_data_selector ? e->ops->flat_data_selector() : 0;
}

void lx_execution_terminate(LxExecution *e, void (*cleanup)(uint32_t), uint32_t code)
{
    if (e && e->ops->terminate)
        e->ops->terminate(e, cleanup, code);
    else
        cleanup(code);
}

void lx_execution_poll_exit(LxExecution *e)
{
    if (e && e->ops->poll_termination)
        e->ops->poll_termination(e);
}

void lx_execution_poll_termination(LxExecution *e)
{
    lx_execution_poll_exit(e);
    lx_execution_checkpoint(e);
}

void lx_execution_set_checkpoint(LxExecution *e, void (*checkpoint)(void *), void *argument)
{
    if (e)
    {
        e->checkpoint_argument = argument;
        e->checkpoint = checkpoint;
    }
}

void lx_execution_checkpoint(LxExecution *e)
{
    if (e && e->checkpoint)
        e->checkpoint(e->checkpoint_argument);
}

void lx_execution_set_park_flag(LxExecution *e, const int *flag)
{
    if (e && e->ops->set_park_flag)
        e->ops->set_park_flag(flag);
}

int lx_execution_quiesce(LxExecution *e)
{
    if (!e || !e->ops->quiesce)
        return 0;
    e->ops->quiesce();
    return 1;
}

/* A 16-bit target owns the rest of its tile; the original leaves the caller's
   stack only when fewer than this many bytes remain below SP. */
#define CALL16_MINIMUM_STACK 0x1000u
#define CALL16_TILE 0x10000u
/* Tiled selectors reach only the first 512 MiB of the guest address space. */
#define CALL16_TILED_LIMIT 0x20000000u
#define CALL16_MAXIMUM_ARGUMENTS 0x400u

typedef struct Call16
{
    LxExecution *execution;
    uint32_t target, bytes;
    const void *arguments;
    uint32_t *result;
    LxGuestMapping *stack;
    uint16_t selector;
} Call16;

static uint32_t call16Operation(void *argument)
{
    Call16 *call = argument;
    LxExecution *e = call->execution;
    uint32_t stack = e->ops->guest_stack();
    uint16_t selector = 0, offset = 0;
    if (!stack || stack >= CALL16_TILED_LIMIT || (stack & 0xffff) <= CALL16_MINIMUM_STACK ||
        !lx_execution_find_selector(e, stack, &selector, &offset, 0))
    {
        call->stack = lx_execution_map_aligned(e, 0, CALL16_TILE, LX_GUEST_READ | LX_GUEST_WRITE,
                                               CALL16_TILE);
        if (!call->stack)
            return 0;
        stack = lx_execution_mapping_address(call->stack) + CALL16_TILE - 16;
        if (!lx_execution_find_selector(e, stack, &selector, &offset, 0))
            return 0;
        call->selector = selector;
    }
    return (uint32_t)e->ops->call16(call->target, (uint16_t)((selector << 3) | 7), offset,
                                    call->arguments, call->bytes, call->result);
}

static void call16Cleanup(void *argument)
{
    Call16 *call = argument;
    if (call->selector)
        lx_execution_free_selector(call->execution, call->selector);
    if (call->stack)
        lx_execution_unmap(call->execution, call->stack);
}

int lx_execution_call16(LxExecution *e, uint32_t target, const void *arguments, uint32_t bytes,
                        uint32_t *result)
{
    if (!e || !e->ops->call16 || !e->ops->guest_stack || !result || !(target >> 16) ||
        bytes > CALL16_MAXIMUM_ARGUMENTS || (bytes && !arguments))
        return 0;
    Call16 call = {e, target, bytes, arguments, result, NULL, 0};
    return (int)lx_host_guarded_call(call16Operation, &call, call16Cleanup, &call);
}
