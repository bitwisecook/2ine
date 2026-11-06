/* Public domain. No rights reserved. Wholly AI-authored. */
/* Mach-O image identities are what tie a report to the exact runtime build. */
#define _DARWIN_C_SOURCE 1
#include "hal/host_fatal.h"
#include "hal/posix/host_fatal_platform.h"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/ucontext.h>
static void write_register(const char *name, uint64_t value)
{
    lx_host_fatal_write(name);
    lx_host_fatal_write("=");
    lx_host_fatal_write_hex(value, 16);
    lx_host_fatal_write(" ");
}
void lx_host_fatal_write_registers(const void *context)
{
    const ucontext_t *state = context;
#if defined(__aarch64__)
    const struct __darwin_arm_thread_state64 *r = &state->uc_mcontext->__ss;
    write_register("pc", r->__pc);
    write_register("lr", r->__lr);
    write_register("fp", r->__fp);
    write_register("sp", r->__sp);
    for (unsigned i = 0; i < 29; i++)
    {
        lx_host_fatal_write("x");
        lx_host_fatal_write_decimal(i);
        write_register("", r->__x[i]);
    }
#else
    const struct __darwin_x86_thread_state64 *r = &state->uc_mcontext->__ss;
    write_register("rip", r->__rip);
    write_register("rsp", r->__rsp);
    write_register("rbp", r->__rbp);
    write_register("rax", r->__rax);
    write_register("rbx", r->__rbx);
    write_register("rcx", r->__rcx);
    write_register("rdx", r->__rdx);
    write_register("rsi", r->__rsi);
    write_register("rdi", r->__rdi);
#endif
}
void lx_host_fatal_write_images(void)
{
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++)
    {
        const char *name = _dyld_get_image_name(i);
        const struct mach_header_64 *header =
            (const struct mach_header_64 *)_dyld_get_image_header(i);
        /* System images are identified by the host version; 2ine's are not. */
        if (!name || !header || !strncmp(name, "/usr/lib/", 9) || !strncmp(name, "/System/", 8))
            continue;
        lx_host_fatal_write_hex((uint64_t)(uintptr_t)header, 16);
        const uint8_t *command = (const uint8_t *)(header + 1);
        for (uint32_t c = 0; c < header->ncmds; c++)
        {
            const struct load_command *load = (const struct load_command *)command;
            if (load->cmd == LC_UUID)
            {
                const struct uuid_command *identity = (const struct uuid_command *)load;
                lx_host_fatal_write(" ");
                for (unsigned b = 0; b < sizeof identity->uuid; b++)
                    lx_host_fatal_write_hex(identity->uuid[b], 2);
                break;
            }
            command += load->cmdsize;
        }
        lx_host_fatal_write(" ");
        lx_host_fatal_write(name);
        lx_host_fatal_write("\n");
    }
}
int lx_host_fatal_display_available(void)
{
    return 1;
}
short lx_host_fatal_prepare_spawn(posix_spawn_file_actions_t *actions)
{
    for (int descriptor = 0; descriptor < 3; descriptor++)
        posix_spawn_file_actions_addinherit_np(actions, descriptor);
    return POSIX_SPAWN_CLOEXEC_DEFAULT;
}
