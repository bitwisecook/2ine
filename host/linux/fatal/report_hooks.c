/* Public domain. No rights reserved. Wholly AI-authored. */
/* Linux register and loaded-image report hooks. */
#define _GNU_SOURCE 1
#include "host/fatal.h"
#include "host/posix/fatal/report_hooks.h"
#include "host/linux/system/maps.h"
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

typedef struct MapCopy
{
    size_t copied;
} MapCopy;

static int write_map_line(const char *line, size_t length, void *context)
{
    MapCopy *copy = context;
    lx_host_fatal_write_bytes(line, length);
    if (!length || line[length - 1] != '\n')
        lx_host_fatal_write_bytes("\n", 1);
    copy->copied += length;
    return copy->copied < MAP_COPY_BYTES;
}

void lx_host_fatal_write_images(void)
{
    MapCopy copy = {0};
    lx_linux_maps_visit(write_map_line, &copy);
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
