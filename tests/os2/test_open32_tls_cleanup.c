/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native-module shutdown resets TLS allocation ownership and current-thread data. */
#include "os2/include/pmwinx.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define LOAD(function)                                                                             \
    __typeof__(function) *call_##function = dlsym(module, #function);                              \
    CHECK(call_##function)

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(module);
    LOAD(TlsAlloc);
    LOAD(TlsGetValue);
    LOAD(TlsSetValue);
    LOAD(TlsFree);
    LOAD(GetLastError);
    void (*shutdown)(void) = dlsym(module, "lxNativeModuleDeinit");
    CHECK(shutdown);
    for (unsigned pass = 0; pass < 2; pass++)
    {
        unsigned char seen[64] = {0};
        for (unsigned n = 0; n < 64; n++)
        {
            ULONG index = call_TlsAlloc();
            CHECK(index < 64 && !seen[index]);
            seen[index] = 1;
            CHECK(call_TlsGetValue(index) == 0 && call_GetLastError() == 0);
            CHECK(call_TlsSetValue(index, UINT32_C(0xfedcba98)));
        }
        CHECK(call_TlsAlloc() == UINT32_MAX);
        shutdown();
    }
    CHECK(dlclose(module) == 0);
    puts("Open32 TLS module cleanup releases all indices and clears reused thread values");
    return 0;
}
