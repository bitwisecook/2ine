/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host.h"
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
int lx_host_system_memory(uint64_t *total, uint64_t *available)
{
    if (!total || !available)
        return 0;
    uint64_t bytes = 0;
    size_t size = sizeof bytes;
    if (sysctlbyname("hw.memsize", &bytes, &size, NULL, 0) != 0)
        return 0;
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    kern_return_t rc = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count);
    mach_port_deallocate(mach_task_self(), host);
    if (rc != KERN_SUCCESS)
        return 0;
    *total = bytes;
    *available = ((uint64_t)vm.free_count + vm.inactive_count) * lx_host_page_size();
    return 1;
}

const char *lx_host_os(void)
{
    return "macos";
}
