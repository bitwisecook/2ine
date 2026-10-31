/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_CPU_INTERNAL_H
#define TWOINE_CPU_INTERNAL_H
#include "cpu.h"

typedef struct LxCpuOperations
{
    const char *name;
    int (*open)(LxCpu *cpu);
    void (*close)(LxCpu *cpu);
    int (*map)(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions, void *host);
    int (*unmap)(LxCpu *cpu, uint32_t address, size_t size);
    int (*protect)(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions);
    int (*invalidate)(LxCpu *cpu, uint32_t address, size_t size);
    int (*segment)(LxCpu *cpu, const LxSegment *segment);
    int (*get_registers)(LxCpu *cpu, LxCpuRegisters *registers);
    int (*set_registers)(LxCpu *cpu, const LxCpuRegisters *registers);
    LxCpuEvent (*run)(LxCpu *cpu, uint64_t instructions, uint64_t timeout_us);
    void *(*save)(LxCpu *cpu);
    int (*restore)(LxCpu *cpu, const void *context);
    void (*free_context)(void *context);
} LxCpuOperations;

typedef struct LxMemoryRegion
{
    uint32_t address;
    size_t size;
    unsigned permissions;
    void *host;
    int owns_host;
    struct LxMemoryRegion *next;
} LxMemoryRegion;

typedef struct LxCpuSegment
{
    LxSegment definition;
    struct LxCpuSegment *next;
} LxCpuSegment;

struct LxCpu
{
    const LxCpuOperations *ops;
    void *backend;
    LxMemoryRegion *memory;
    LxCpuContext *contexts;
    LxCpuSegment *segments;
    char error[256];
};

/* Guest linear space:
   00000000..FFFDFFFF  application mappings
   FFFE0000..FFFEFFFF  GDT and ring-transition scratch
   FFFF0000..FFFFFFFF  LDT */
#define LX_DESCRIPTOR_BASE UINT32_C(0xfffe0000)
#define LX_DESCRIPTOR_SIZE UINT32_C(0x00020000)
int lx_cpu_fail(LxCpu *cpu, const char *message);
extern const LxCpuOperations lx_cpu_unicorn_operations;
#endif
