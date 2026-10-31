/* Backend-independent x86 guest execution and guest-address access. */
#ifndef TWOINE_CPU_H
#define TWOINE_CPU_H
#include <stddef.h>
#include <stdint.h>

typedef uint32_t LxGuestAddress;
typedef struct LxCpu LxCpu;
typedef struct LxCpuContext LxCpuContext;

enum { LX_MEM_READ = 1, LX_MEM_WRITE = 2, LX_MEM_EXECUTE = 4 };
enum { LX_GUEST_PAGE_SIZE = 4096 };

typedef struct LxCpuRegisters {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
    uint16_t cs, ds, es, fs, gs, ss;
} LxCpuRegisters;

typedef struct LxSegment {
    uint16_t selector;
    uint32_t base;
    uint32_t limit;       /* Inclusive byte limit. */
    unsigned bits;       /* 16 or 32; descriptor D/B bit. */
    int executable;
} LxSegment;

typedef enum LxCpuStop {
    LX_CPU_TRAP,          /* Software interrupt, or a CPU exception vector. */
    LX_CPU_BUDGET,        /* Instruction or wall-clock execution bound. */
    LX_CPU_FAULT
} LxCpuStop;

typedef struct LxCpuEvent {
    LxCpuStop reason;
    uint32_t vector;
    uint32_t address;
    int memory_access;   /* LX_MEM_* for a memory fault, zero otherwise. */
} LxCpuEvent;

/* Backend names are explicit: an unavailable requested backend never falls back. */
LxCpu *lx_cpu_create(const char *backend, char *error, size_t error_size);
void lx_cpu_destroy(LxCpu *cpu);
const char *lx_cpu_backend(const LxCpu *cpu);
const char *lx_cpu_error(const LxCpu *cpu);

/* Mappings are guest-page aligned and must not overlap. */
int lx_cpu_map(LxCpu *cpu, LxGuestAddress address, size_t size, unsigned permissions);
/* Borrowed page-aligned backing remains owned by the caller through unmap.
   Guest permissions do not require executable host backing. */
int lx_cpu_map_borrowed(LxCpu *cpu, LxGuestAddress address, size_t size,
                         unsigned permissions, void *backing);
int lx_cpu_unmap(LxCpu *cpu, LxGuestAddress address, size_t size);
int lx_cpu_protect(LxCpu *cpu, LxGuestAddress address, size_t size, unsigned permissions);
int lx_cpu_read(LxCpu *cpu, LxGuestAddress address, void *out, size_t size);
int lx_cpu_write(LxCpu *cpu, LxGuestAddress address, const void *in, size_t size);
/* The returned host pointer is valid until unmap/destroy. No pointer truncation. */
void *lx_cpu_translate(LxCpu *cpu, LxGuestAddress address, size_t size, unsigned permissions);

int lx_cpu_set_segment(LxCpu *cpu, const LxSegment *segment);
/* Resolve a protected-mode selector:offset, checking its inclusive byte limit. */
void *lx_cpu_translate_far(LxCpu *cpu, uint16_t selector, uint32_t offset,
                           size_t size, unsigned permissions);
int lx_cpu_get_registers(LxCpu *cpu, LxCpuRegisters *registers);
int lx_cpu_set_registers(LxCpu *cpu, const LxCpuRegisters *registers);
/* Execution returns to the caller for API dispatch; host calls run outside the JIT. */
LxCpuEvent lx_cpu_run(LxCpu *cpu, uint64_t instruction_limit, uint64_t timeout_us);

/* Contexts include backend CPU state, including x87; they do not snapshot RAM.
   A context can only be restored to the CPU which created it. */
LxCpuContext *lx_cpu_save(LxCpu *cpu);
int lx_cpu_restore(LxCpu *cpu, const LxCpuContext *context);
void lx_cpu_context_free(LxCpuContext *context);
#endif
