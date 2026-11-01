/* Public domain. No rights reserved. Wholly AI-authored. */
/* Execution contracts shared by portable CPU backend implementations. */
#include "cpu.h"
#include "hal/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static LxCpu *make_cpu(void)
{
    char error[256];
    LxCpu *cpu = lx_cpu_create("auto", error, sizeof error);
    if (!cpu)
    {
        fprintf(stderr, "%s\n", error);
        exit(1);
    }
    CHECK(lx_cpu_map(cpu, 0x10000, 0x10000, LX_MEM_READ | LX_MEM_WRITE | LX_MEM_EXECUTE));
    CHECK(lx_cpu_map(cpu, 0x20000, 0x10000, LX_MEM_READ | LX_MEM_WRITE | LX_MEM_EXECUTE));
    CHECK(lx_cpu_map(cpu, 0x40000, 0x10000, LX_MEM_READ | LX_MEM_WRITE));
    CHECK(lx_cpu_map(cpu, 0x50000, 0x10000, LX_MEM_READ | LX_MEM_WRITE));
    LxSegment code32 = {0x1b, 0, UINT32_MAX, 32, 1};
    LxSegment data32 = {0x23, 0, UINT32_MAX, 32, 0};
    LxSegment code16 = {0x2b, 0x20000, 0xffff, 16, 1};
    LxSegment data16 = {0x2f, 0x50000, 0xffff, 16, 0};
    CHECK(lx_cpu_set_segment(cpu, &code32));
    CHECK(lx_cpu_set_segment(cpu, &data32));
    CHECK(lx_cpu_set_segment(cpu, &code16));
    CHECK(lx_cpu_set_segment(cpu, &data16));
    LxCpuRegisters r = {0};
    r.cs = 0x1b;
    r.ds = r.es = r.ss = 0x23;
    r.fs = 0x2f;
    r.eip = 0x10000;
    r.esp = 0x4fff0;
    r.eflags = 2;
    CHECK(lx_cpu_set_registers(cpu, &r));
    return cpu;
}

static void start_at(LxCpu *cpu, uint16_t cs, uint32_t eip)
{
    LxCpuRegisters r;
    CHECK(lx_cpu_get_registers(cpu, &r));
    r.cs = cs;
    r.eip = eip;
    CHECK(lx_cpu_set_registers(cpu, &r));
}

static LxCpuRegisters run_trap(LxCpu *cpu, unsigned vector)
{
    LxCpuEvent e = lx_cpu_run(cpu, 1000, 100000);
    if (e.reason != LX_CPU_TRAP)
        fprintf(stderr, "CPU stopped: %d at %x: %s\n", e.reason, e.address, lx_cpu_error(cpu));
    CHECK(e.reason == LX_CPU_TRAP && e.vector == vector);
    LxCpuRegisters r;
    CHECK(lx_cpu_get_registers(cpu, &r));
    return r;
}

static void mixed_modes(void)
{
    LxCpu *cpu = make_cpu();
    /* 32-bit far jump -> nonzero-base USE16 segment -> 32-bit far jump back. */
    const unsigned char code32[] = {0xb8, 0x78, 0x56, 0x34, 0x12, 0xea, 0, 0, 0, 0, 0x2b, 0};
    const unsigned char code16[] = {0xbb, 0x34, 0x12, 0x66, 0x40, 0x66, 0xea,
                                    0x20, 0,    1,    0,    0x1b, 0};
    const unsigned char resume[] = {0x83, 0xc0, 2, 0xcc};
    CHECK(lx_cpu_write(cpu, 0x10000, code32, sizeof code32));
    CHECK(lx_cpu_write(cpu, 0x20000, code16, sizeof code16));
    CHECK(lx_cpu_write(cpu, 0x10020, resume, sizeof resume));
    LxCpuRegisters r = run_trap(cpu, 3);
    CHECK(r.eax == 0x1234567b && r.ebx == 0x1234 && r.cs == 0x1b);
    /* Resume directly inside the 16-bit nonzero-base segment. */
    start_at(cpu, 0x2b, 0);
    r = run_trap(cpu, 3);
    CHECK(r.eax == 0x1234567e && r.cs == 0x1b);
    /* Replace translated code through the memory API and execute it again. */
    const unsigned char five = 5;
    CHECK(lx_cpu_write(cpu, 0x10022, &five, 1));
    start_at(cpu, 0x1b, 0x10000);
    r = run_trap(cpu, 3);
    CHECK(r.eax == 0x1234567e);
    lx_cpu_destroy(cpu);
    puts("mixed 16/32-bit protected mode, nonzero bases and code invalidation passed");
}

static void segmented_stack_and_far_call(void)
{
    LxCpu *cpu = make_cpu();
    /* USE16 stack with CALL FAR32 to USE32 and RETF back to USE16. */
    const unsigned char code16[] = {0x68, 0x34, 0x12, 0x58, 0x66, 0x9a, 0x40, 0,
                                    1,    0,    0x1b, 0,    0xa3, 0x40, 0,    0xcc};
    const unsigned char code32[] = {0x83, 0xc0, 1, 0xcb};
    CHECK(lx_cpu_write(cpu, 0x20000, code16, sizeof code16));
    CHECK(lx_cpu_write(cpu, 0x10040, code32, sizeof code32));
    LxCpuRegisters r;
    CHECK(lx_cpu_get_registers(cpu, &r));
    r.cs = 0x2b;
    r.ss = r.ds = r.es = 0x2f;
    r.esp = 0xff00;
    r.eip = 0;
    CHECK(lx_cpu_set_registers(cpu, &r));
    r = run_trap(cpu, 3);
    uint16_t value;
    CHECK(r.cs == 0x2b && r.ss == 0x2f && r.esp == 0xff00);
    CHECK(lx_cpu_read(cpu, 0x50040, &value, sizeof value) && value == 0x1235);
    lx_cpu_destroy(cpu);
    puts("ring-3 segmented USE16 stack and far call/return across code widths passed");
}

static void ldt_and_api_trap(void)
{
    LxCpu *cpu = make_cpu();
    uint32_t value = 0x1234abcd;
    CHECK(lx_cpu_write(cpu, 0x50014, &value, sizeof value));
    const unsigned char code[] = {0x64, 0xa1, 0x14, 0, 0, 0, 0xcd, 0xf1, 0x83, 0xc0, 1, 0xcc};
    CHECK(lx_cpu_write(cpu, 0x10000, code, sizeof code));
    LxCpuRegisters r = run_trap(cpu, 0xf1);
    CHECK(r.eax == value && r.eip == 0x10008);
    r.eax = 41;
    CHECK(lx_cpu_set_registers(cpu, &r));
    r = run_trap(cpu, 3);
    CHECK(r.eax == 42);
    lx_cpu_destroy(cpu);
    puts("LDT/FS addressing and host API trap/resume passed");
}

static void floating_point_context(void)
{
    LxCpu *cpu = make_cpu();
    /* FNINIT; FLD1; INT3; FLD1; FADDP ST1; FISTP dword [40000h]; INT3. */
    const unsigned char code[] = {0xdb, 0xe3, 0xd9, 0xe8, 0xcc, 0xd9, 0xe8, 0xde,
                                  0xc1, 0xdb, 0x1d, 0,    0,    4,    0,    0xcc};
    CHECK(lx_cpu_write(cpu, 0x10000, code, sizeof code));
    run_trap(cpu, 3);
    LxCpuContext *context = lx_cpu_save(cpu);
    CHECK(context);
    run_trap(cpu, 3);
    uint32_t value;
    CHECK(lx_cpu_read(cpu, 0x40000, &value, sizeof value) && value == 2);
    CHECK(lx_cpu_restore(cpu, context));
    run_trap(cpu, 3);
    CHECK(lx_cpu_read(cpu, 0x40000, &value, sizeof value) && value == 2);
    CHECK(lx_cpu_update(cpu, context));
    start_at(cpu, 0x1b, 0x10000);
    CHECK(lx_cpu_restore(cpu, context));
    LxCpuRegisters saved;
    CHECK(lx_cpu_get_registers(cpu, &saved) && saved.eip == 0x10010);
    LxCpu *other = make_cpu();
    CHECK(!lx_cpu_restore(other, context));
    CHECK(!lx_cpu_update(other, context));
    LxCpuContext *orphan = lx_cpu_save(other);
    CHECK(orphan);
    lx_cpu_destroy(other);
    other = make_cpu();
    CHECK(!lx_cpu_restore(other, orphan));
    lx_cpu_context_free(orphan);
    lx_cpu_context_free(context);
    lx_cpu_destroy(other);
    lx_cpu_destroy(cpu);
    puts("full CPU/x87 context restore and context ownership passed");
}

static void memory_and_faults(void)
{
    LxCpu *cpu = make_cpu();
    uint32_t value = 0;
    CHECK(!lx_cpu_map(cpu, 0x10000, 4096, LX_MEM_READ));
    CHECK(lx_cpu_translate_far(cpu, 0x2f, 0x14, sizeof value, LX_MEM_READ) ==
          lx_cpu_translate(cpu, 0x50014, sizeof value, LX_MEM_READ));
    CHECK(!lx_cpu_translate_far(cpu, 0x2f, 0xffff, 2, LX_MEM_READ));
    CHECK(!lx_cpu_translate_far(cpu, 0, 0, 1, LX_MEM_READ));
    CHECK(!lx_cpu_translate_far(cpu, 0x2f, UINT32_MAX, 1, LX_MEM_READ));
    CHECK(!lx_cpu_map(cpu, 0xfffff000, 8192, LX_MEM_READ));
    CHECK(!lx_cpu_translate(cpu, 0xfffffffe, 8, LX_MEM_READ));
    CHECK(!lx_cpu_read(cpu, 0x30000, &value, sizeof value));
    CHECK(lx_cpu_translate(cpu, 0x40000, sizeof value, LX_MEM_WRITE) != NULL);
    const unsigned char write[] = {0xc7, 0x05, 0, 0, 5, 0, 1, 0, 0, 0, 0xcc};
    CHECK(lx_cpu_write(cpu, 0x10000, write, sizeof write));
    CHECK(lx_cpu_protect(cpu, 0x50000, 0x10000, LX_MEM_READ));
    CHECK(!lx_cpu_write(cpu, 0x50000, &value, sizeof value));
    LxCpuEvent e = lx_cpu_run(cpu, 1000, 100000);
    CHECK(e.reason == LX_CPU_FAULT && e.address == 0x50000 && e.memory_access == LX_MEM_WRITE);
    CHECK(lx_cpu_unmap(cpu, 0x50000, 0x10000));
    CHECK(!lx_cpu_translate(cpu, 0x50000, sizeof value, LX_MEM_READ));
    lx_cpu_destroy(cpu);
    puts("guest pointer bounds, overlap, permissions, unmap and write fault passed");
}

static void bounded_execution(void)
{
    LxCpu *cpu = make_cpu();
    const unsigned char loop[] = {0xeb, 0xfe};
    CHECK(lx_cpu_write(cpu, 0x10000, loop, sizeof loop));
    LxCpuEvent e = lx_cpu_run(cpu, 100, 0);
    CHECK(e.reason == LX_CPU_BUDGET);
    e = lx_cpu_run(cpu, 0, 1000);
    CHECK(e.reason == LX_CPU_BUDGET);
    e = lx_cpu_run(cpu, 0, 0);
    CHECK(e.reason == LX_CPU_FAULT);
    e = lx_cpu_run_blocks(cpu, 100);
    CHECK(e.reason == LX_CPU_BUDGET);
    e = lx_cpu_run_blocks(cpu, 0);
    CHECK(e.reason == LX_CPU_FAULT);
    /* A block slice must resume after a budget stop and preserve API traps. */
    const unsigned char trap[] = {0x40, 0xcd, 0xf1, 0x40, 0xcc};
    CHECK(lx_cpu_write(cpu, 0x10000, trap, sizeof trap));
    start_at(cpu, 0x1b, 0x10000);
    e = lx_cpu_run_blocks(cpu, 100);
    CHECK(e.reason == LX_CPU_TRAP && e.vector == 0xf1);
    LxCpuRegisters before, after;
    CHECK(lx_cpu_get_registers(cpu, &before));
    e = lx_cpu_run_blocks(cpu, 100);
    CHECK(e.reason == LX_CPU_TRAP && e.vector == 3);
    CHECK(lx_cpu_get_registers(cpu, &after) && after.eax == before.eax + 1);
    lx_cpu_destroy(cpu);
    char error[256];
    CHECK(!lx_cpu_create("unavailable", error, sizeof error));
    CHECK(strstr(error, "not linked"));
    puts("instruction/time budgets and explicit unavailable-backend error passed");
}

int main(void)
{
    printf("host=%s/%s backend=unicorn\n", lx_host_os(), lx_host_arch());
    mixed_modes();
    segmented_stack_and_far_call();
    ldt_and_api_trap();
    floating_point_context();
    memory_and_faults();
    bounded_execution();
    puts("CPU execution contracts passed");
    return 0;
}
