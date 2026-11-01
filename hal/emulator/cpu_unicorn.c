/* Public domain. No rights reserved. Wholly AI-authored. */
/* Unicorn adapter. Public runtime interfaces contain no Unicorn types. */
#include "cpu_internal.h"
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#if UC_API_MAJOR < 2 ||                                                                            \
    (UC_API_MAJOR == 2 && (UC_API_MINOR < 1 || (UC_API_MINOR == 1 && UC_API_PATCH < 4)))
#error "Unicorn 2.1.4 or newer is required"
#endif

#define LX_TABLE_SIZE UINT32_C(0x10000)
#define LX_LDT_SELECTOR UINT16_C(0xfff8)

typedef struct UnicornCpu
{
    uc_engine *engine;
    uc_hook interrupt_hook, memory_hook, block_hook;
    uint64_t block_budget, blocks_remaining;
    LxCpuEvent event;
    int stopped;
} UnicornCpu;

static int checked(LxCpu *cpu, uc_err error)
{
    return error == UC_ERR_OK ? 1 : lx_cpu_fail(cpu, uc_strerror(error));
}

static void block_boundary(uc_engine *engine, uint64_t address, uint32_t size, void *opaque)
{
    UnicornCpu *u = opaque;
    (void)address;
    (void)size;
    if (u->block_budget && !u->blocks_remaining--)
        uc_emu_stop(engine);
}

static void interrupt(uc_engine *engine, uint32_t vector, void *opaque)
{
    UnicornCpu *u = opaque;
    u->event.reason = LX_CPU_TRAP;
    u->event.vector = vector;
    u->stopped = 1;
    uc_emu_stop(engine);
}

static bool memory_fault(uc_engine *engine, uc_mem_type type, uint64_t address, int size,
                         int64_t value, void *opaque)
{
    UnicornCpu *u = opaque;
    (void)engine;
    (void)size;
    (void)value;
    u->event.reason = LX_CPU_FAULT;
    u->event.address = (uint32_t)address;
    u->event.memory_access =
        (type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT)   ? LX_MEM_WRITE
        : (type == UC_MEM_FETCH_UNMAPPED || type == UC_MEM_FETCH_PROT) ? LX_MEM_EXECUTE
                                                                       : LX_MEM_READ;
    u->stopped = 1;
    return false;
}

static void close_cpu(LxCpu *cpu)
{
    UnicornCpu *u = cpu->backend;
    if (u)
    {
        if (u->engine)
            uc_close(u->engine);
        free(u);
    }
    cpu->backend = NULL;
}

static int set_segment(LxCpu *cpu, const LxSegment *s);

/* Enter ring 3 through IRETD. Writing CS through Unicorn's register API loads
   a segment cache but does not change CPL; SS validation uses the actual CPL. */
static int enter_user_mode(LxCpu *cpu)
{
    UnicornCpu *u = cpu->backend;
    const LxSegment segments[] = {{8, 0, UINT32_MAX, 32, 1},
                                  {16, 0, UINT32_MAX, 32, 0},
                                  {0x1b, 0, UINT32_MAX, 32, 1},
                                  {0x23, 0, UINT32_MAX, 32, 0}};
    for (unsigned i = 0; i < sizeof segments / sizeof segments[0]; i++)
        if (!set_segment(cpu, &segments[i]))
            return 0;
    const uint32_t code = LX_DESCRIPTOR_BASE + 0x100;
    const uint32_t stack = LX_DESCRIPTOR_BASE + 0x200;
    const uint8_t instructions[] = {0xcf, 0xcc}; /* IRETD; INT3. */
    const uint32_t words[] = {code + 1, 0x1b, 2, stack + 0x100, 0x23};
    uint8_t frame[20];
    for (unsigned i = 0; i < 5; i++)
        for (unsigned byte = 0; byte < 4; byte++)
            frame[i * 4 + byte] = (uint8_t)(words[i] >> (byte * 8));
    uint16_t cs = 8, ss = 16;
    if (!checked(cpu,
                 uc_mem_protect(u->engine, LX_DESCRIPTOR_BASE, LX_DESCRIPTOR_SIZE, UC_PROT_ALL)) ||
        !checked(cpu, uc_mem_write(u->engine, code, instructions, sizeof instructions)) ||
        !checked(cpu, uc_mem_write(u->engine, stack, frame, sizeof frame)) ||
        !checked(cpu, uc_reg_write(u->engine, UC_X86_REG_CS, &cs)) ||
        !checked(cpu, uc_reg_write(u->engine, UC_X86_REG_SS, &ss)) ||
        !checked(cpu, uc_reg_write(u->engine, UC_X86_REG_ESP, &stack)) ||
        !checked(cpu, uc_emu_start(u->engine, code, UINT64_MAX, 100000, 8)))
        return 0;
    if (!u->stopped || u->event.reason != LX_CPU_TRAP || u->event.vector != 3)
        return lx_cpu_fail(cpu, "Protected-mode user entry did not complete");
    return checked(cpu, uc_mem_protect(u->engine, LX_DESCRIPTOR_BASE, LX_DESCRIPTOR_SIZE,
                                       UC_PROT_READ | UC_PROT_WRITE));
}

static int open_cpu(LxCpu *cpu)
{
    UnicornCpu *u = calloc(1, sizeof *u);
    if (!u)
        return lx_cpu_fail(cpu, "Out of memory");
    cpu->backend = u;
    if (!checked(cpu, uc_open(UC_ARCH_X86, UC_MODE_32, &u->engine)))
        goto fail;
    if (!checked(cpu, uc_ctl_set_cpu_model(u->engine, UC_CPU_X86_PENTIUM3)))
        goto fail;
    if (!checked(cpu, uc_mem_map(u->engine, LX_DESCRIPTOR_BASE, LX_DESCRIPTOR_SIZE,
                                 UC_PROT_READ | UC_PROT_WRITE)))
        goto fail;
    uc_x86_mmr gdt = {0, LX_DESCRIPTOR_BASE, LX_TABLE_SIZE - 1, 0};
    uc_x86_mmr ldt = {LX_LDT_SELECTOR, LX_DESCRIPTOR_BASE + LX_TABLE_SIZE, LX_TABLE_SIZE - 1,
                      0x8200};
    uint32_t cr0 = 1; /* Protected mode; paging disabled. */
    if (!checked(cpu, uc_reg_write(u->engine, UC_X86_REG_GDTR, &gdt)) ||
        !checked(cpu, uc_reg_write(u->engine, UC_X86_REG_LDTR, &ldt)) ||
        !checked(cpu, uc_reg_write(u->engine, UC_X86_REG_CR0, &cr0)) ||
        !checked(cpu, uc_hook_add(u->engine, &u->interrupt_hook, UC_HOOK_INTR, (void *)interrupt, u,
                                  1, 0)) ||
        !checked(cpu, uc_hook_add(u->engine, &u->block_hook, UC_HOOK_BLOCK, (void *)block_boundary,
                                  u, 1, 0)) ||
        !checked(cpu, uc_hook_add(u->engine, &u->memory_hook, UC_HOOK_MEM_INVALID,
                                  (void *)memory_fault, u, 1, 0)))
        goto fail;
    if (!enter_user_mode(cpu))
        goto fail;
    return 1;
fail:
    close_cpu(cpu);
    return 0;
}

static int map_memory(LxCpu *cpu, uint32_t at, size_t size, unsigned permissions, void *host)
{
    UnicornCpu *u = cpu->backend;
    return checked(cpu, uc_mem_map_ptr(u->engine, at, size, permissions, host));
}
static int unmap_memory(LxCpu *cpu, uint32_t at, size_t size)
{
    UnicornCpu *u = cpu->backend;
    return checked(cpu, uc_mem_unmap(u->engine, at, size));
}
static int protect_memory(LxCpu *cpu, uint32_t at, size_t size, unsigned permissions)
{
    UnicornCpu *u = cpu->backend;
    return checked(cpu, uc_mem_protect(u->engine, at, size, permissions));
}
static int invalidate(LxCpu *cpu, uint32_t at, size_t size)
{
    UnicornCpu *u = cpu->backend;
    return !size || checked(cpu, uc_ctl_remove_cache(u->engine, at, (uint64_t)at + size));
}

static int set_segment(LxCpu *cpu, const LxSegment *s)
{
    UnicornCpu *u = cpu->backend;
    uint32_t limit = s->limit;
    unsigned flags = s->bits == 32 ? 4u : 0u;
    if (limit > 0xfffff)
    {
        if ((limit & 0xfff) != 0xfff)
            return lx_cpu_fail(cpu, "Large x86 segment limit is not page granular");
        limit >>= 12;
        flags |= 8;
    }
    if (!(s->selector & 4) && (s->selector & ~7u) == LX_LDT_SELECTOR)
        return lx_cpu_fail(cpu, "Reserved LDT descriptor selector");
    unsigned char d[8] = {
        (unsigned char)limit,
        (unsigned char)(limit >> 8),
        (unsigned char)s->base,
        (unsigned char)(s->base >> 8),
        (unsigned char)(s->base >> 16),
        (unsigned char)(0x92u | ((s->selector & 3u) << 5) | (s->executable ? 8u : 0u)),
        (unsigned char)(((limit >> 16) & 15u) | (flags << 4)),
        (unsigned char)(s->base >> 24)};
    unsigned table = !!(s->selector & 4);
    uint32_t at = LX_DESCRIPTOR_BASE + table * LX_TABLE_SIZE + (s->selector & ~7u);
    if (!checked(cpu, uc_mem_write(u->engine, at, d, sizeof d)))
        return 0;
    return 1;
}

#define GENERAL_REGISTERS(X)                                                                       \
    X(eax, EAX)                                                                                    \
    X(ebx, EBX)                                                                                    \
    X(ecx, ECX)                                                                                    \
    X(edx, EDX)                                                                                    \
    X(esi, ESI)                                                                                    \
    X(edi, EDI)                                                                                    \
    X(ebp, EBP)                                                                                    \
    X(esp, ESP)                                                                                    \
    X(eip, EIP)                                                                                    \
    X(eflags, EFLAGS)
#define SEGMENT_REGISTERS(X)                                                                       \
    X(cs, CS)                                                                                      \
    X(ds, DS)                                                                                      \
    X(es, ES)                                                                                      \
    X(fs, FS)                                                                                      \
    X(gs, GS)                                                                                      \
    X(ss, SS)

static int get_registers(LxCpu *cpu, LxCpuRegisters *r)
{
    UnicornCpu *u = cpu->backend;
    memset(r, 0, sizeof *r);
#define REG_ID(field, name) UC_X86_REG_##name,
    static const int ids[] = {GENERAL_REGISTERS(REG_ID) SEGMENT_REGISTERS(REG_ID)};
#undef REG_ID
#define REG_VALUE(field, name) &r->field,
    void *values[] = {GENERAL_REGISTERS(REG_VALUE) SEGMENT_REGISTERS(REG_VALUE)};
#undef REG_VALUE
    return checked(cpu, uc_reg_read_batch(u->engine, ids, values, sizeof ids / sizeof ids[0]));
}
static int set_registers(LxCpu *cpu, const LxCpuRegisters *r)
{
    UnicornCpu *u = cpu->backend;
#define REG_ID(field, name) UC_X86_REG_##name,
    static const int ids[] = {SEGMENT_REGISTERS(REG_ID) GENERAL_REGISTERS(REG_ID)};
#undef REG_ID
#define REG_VALUE(field, name) (void *)&r->field,
    void *values[] = {SEGMENT_REGISTERS(REG_VALUE) GENERAL_REGISTERS(REG_VALUE)};
#undef REG_VALUE
    return checked(cpu, uc_reg_write_batch(u->engine, ids, values, sizeof ids / sizeof ids[0]));
}

static LxCpuEvent run_bounded(LxCpu *cpu, uint64_t instructions, uint64_t timeout_us,
                              uint64_t blocks)
{
    UnicornCpu *u = cpu->backend;
    uint32_t eip = 0;
    uint16_t cs = 0;
    const int ids[] = {UC_X86_REG_EIP, UC_X86_REG_CS};
    void *values[] = {&eip, &cs};
    memset(&u->event, 0, sizeof u->event);
    u->event.reason = LX_CPU_BUDGET;
    u->stopped = 0;
    if (!instructions && !timeout_us && !blocks)
    {
        lx_cpu_fail(cpu, "Guest execution requires an instruction, block or time bound");
        u->event.reason = LX_CPU_FAULT;
        return u->event;
    }
    u->block_budget = u->blocks_remaining = blocks;
    if (instructions > SIZE_MAX || !checked(cpu, uc_reg_read_batch(u->engine, ids, values, 2)))
    {
        lx_cpu_fail(cpu, "Invalid guest execution bound or CPU state");
        u->event.reason = LX_CPU_FAULT;
        return u->event;
    }
    /* UC_MODE_32 starts from EIP; its x86 core applies the cached CS base. */
    uint64_t start = eip;
    uc_err error = uc_emu_start(u->engine, start, UINT64_MAX, timeout_us, (size_t)instructions);
    u->block_budget = 0;
    if (error != UC_ERR_OK)
    {
        checked(cpu, error);
        if (!u->stopped)
        {
            u->event.reason = LX_CPU_FAULT;
            u->event.address = eip;
            for (LxCpuSegment *s = cpu->segments; s; s = s->next)
            {
                if ((s->definition.selector & ~3u) == (cs & ~3u))
                {
                    u->event.address += s->definition.base;
                    break;
                }
            }
        }
    }
    return u->event;
}

static LxCpuEvent run_cpu(LxCpu *cpu, uint64_t instructions, uint64_t timeout_us)
{
    return run_bounded(cpu, instructions, timeout_us, 0);
}
static LxCpuEvent run_blocks(LxCpu *cpu, uint64_t blocks)
{
    return run_bounded(cpu, 0, 0, blocks);
}

static void *save_context(LxCpu *cpu)
{
    UnicornCpu *u = cpu->backend;
    uc_context *context = NULL;
    if (!checked(cpu, uc_context_alloc(u->engine, &context)))
        return NULL;
    if (!checked(cpu, uc_context_save(u->engine, context)))
    {
        uc_context_free(context);
        return NULL;
    }
    return context;
}
static int restore_context(LxCpu *cpu, const void *context)
{
    UnicornCpu *u = cpu->backend;
    return checked(cpu, uc_context_restore(u->engine, (uc_context *)context));
}
static int update_context(LxCpu *cpu, void *context)
{
    UnicornCpu *u = cpu->backend;
    return checked(cpu, uc_context_save(u->engine, context));
}
static void free_context(void *context)
{
    uc_context_free(context);
}

const LxCpuOperations lx_cpu_unicorn_operations = {
    "unicorn",       open_cpu,     close_cpu,     map_memory,    unmap_memory, protect_memory,
    invalidate,      set_segment,  get_registers, set_registers, run_cpu,      save_context,
    restore_context, free_context, run_blocks,    update_context};
