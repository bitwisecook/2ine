/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native guest API entries must align host stack locals for every OS/2 alignment. */
#include "os2libs/os2types.h"
#include <stdint.h>
#include <stdio.h>
static __attribute__((noinline)) uint32_t OS2API entry(void)
{
    uint8_t buffer[16] __attribute__((aligned(16)));
    uintptr_t address;
    __asm__ __volatile__("leal %1,%0" : "=r"(address) : "m"(buffer));
    return address & 15;
}
int main(void)
{
    for (unsigned offset = 0; offset < 16; offset += 4)
    {
        uint32_t result;
        unsigned adjustment = offset;
        __asm__ __volatile__(
            "movl %%esp,%%esi\n\tandl $-16,%%esp\n\tsubl %%ecx,%%esp\n\tcall *%%edi\n\tmovl %%esi,%%esp"
            : "=a"(result), "+c"(adjustment)
            : "D"(entry)
            : "edx", "esi", "memory", "cc");
        if (result)
        {
            fprintf(stderr, "guest alignment %u left host local at +%u\n", offset, result);
            return 1;
        }
    }
    puts("Native API entries realign all four OS/2 stack offsets");
    return 0;
}
