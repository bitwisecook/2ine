/* Public domain. No rights reserved. Wholly AI-authored. */
/* /proc/self/maps names each loaded image and its address range. */
#define _GNU_SOURCE 1
#include "hal/host_fatal.h"
#include "hal/posix/host_fatal_platform.h"
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

enum
{
    /* A guest address space can have a very long map. */
    MAP_COPY_BYTES = 512 * 1024
};

#if defined(__x86_64__) || defined(__aarch64__)
static void write_register(const char *name, uint64_t value)
{
    lx_host_fatal_write(name);
    lx_host_fatal_write("=");
    lx_host_fatal_write_hex(value, 16);
    lx_host_fatal_write(" ");
}
#endif
void lx_host_fatal_write_registers(const void *context)
{
    const ucontext_t *state = context;
#if defined(__aarch64__)
    write_register("pc", state->uc_mcontext.pc);
    write_register("sp", state->uc_mcontext.sp);
    for (unsigned i = 0; i < 31; i++)
    {
        lx_host_fatal_write("x");
        lx_host_fatal_write_decimal(i);
        write_register("", state->uc_mcontext.regs[i]);
    }
#elif defined(__x86_64__)
    static const struct
    {
        const char *name;
        int index;
    } names[] = {{"rip", REG_RIP}, {"rsp", REG_RSP}, {"rbp", REG_RBP}, {"rax", REG_RAX},
                 {"rbx", REG_RBX}, {"rcx", REG_RCX}, {"rdx", REG_RDX}, {"rsi", REG_RSI},
                 {"rdi", REG_RDI}, {"r8", REG_R8},   {"r9", REG_R9},   {"r10", REG_R10},
                 {"r11", REG_R11}, {"r12", REG_R12}, {"r13", REG_R13}, {"r14", REG_R14},
                 {"r15", REG_R15}};

    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        write_register(names[i].name, (uint64_t)state->uc_mcontext.gregs[names[i].index]);
#else
    (void)state;
    lx_host_fatal_write("unavailable on this architecture");
#endif
}

void lx_host_fatal_write_images(void)
{
    int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (maps < 0)
        return;
    char block[4096];
    size_t copied = 0;
    ssize_t count;
    while (copied < MAP_COPY_BYTES && (count = read(maps, block, sizeof block)) > 0)
    {
        lx_host_fatal_write_bytes(block, (size_t)count);
        copied += (size_t)count;
    }
    close(maps);
}

int lx_host_fatal_display_available(void)
{
    return getenv("DISPLAY") || getenv("WAYLAND_DISPLAY");
}

short lx_host_fatal_prepare_spawn(posix_spawn_file_actions_t *actions)
{
    (void)actions;
    return 0;
}
