/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host services used by the guest execution backends. */
#ifndef TWOINE_HOST_H
#define TWOINE_HOST_H
#include <stddef.h>
#include <stdint.h>

/* Guest executable permission does not require executable host backing memory. */
void *lx_host_memory_alloc(size_t size);
void lx_host_memory_free(void *memory, size_t size);
uint64_t lx_host_monotonic_us(void);
void lx_host_sleep_ms(uint32_t milliseconds);
const char *lx_host_os(void);
const char *lx_host_arch(void);
/* Bootstrap argv is returned as consecutive NUL-terminated strings. */
char *lx_host_command_line(size_t *size);
char *lx_host_executable_path(void);
typedef struct LxHostCalendar
{
    int year, month, day, weekday, hour, minute, second, utc_offset_minutes;
} LxHostCalendar;
uint64_t lx_host_realtime_us(void);
int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result);
void lx_host_process_ids(uint32_t *process, uint32_t *parent);
size_t lx_host_stack_limit(void);
#if defined(_MSC_VER)
__declspec(noreturn)
#else
__attribute__((noreturn))
#endif
void lx_host_process_exit(uint32_t status);
int lx_host_system_memory(uint64_t *total, uint64_t *available);

enum
{
    LX_HOST_READ = 1,
    LX_HOST_WRITE = 2,
    LX_HOST_EXECUTE = 4
};
/* Exact mappings fail on occupied addresses; existing mappings are never replaced.
   Host addresses and host protection are independent of guest virtual addresses. */
void *lx_host_vm_map(void *hint, size_t size, unsigned permissions, int exact);
int lx_host_vm_protect(void *memory, size_t size, unsigned permissions);
size_t lx_host_page_size(void);
#endif
