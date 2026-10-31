/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bootstrap queries, argv serialization, memory and calendar host contracts. */
#include "host.h"
#include "../../hal/common/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void arguments_test(int argc, char **argv)
{
    CHECK(argc == 4 && !strcmp(argv[1], "space argument") && !strcmp(argv[2], "") &&
          !strcmp(argv[3], "a\\b"));
    size_t size = 0;
    char *command = lx_host_command_line(&size);
    CHECK(command);
    size_t offset = 0;
    for (int i = 0; i < argc; i++)
    {
        size_t length = strlen(argv[i]) + 1;
        CHECK(offset <= size && length <= size - offset);
        CHECK(!memcmp(command + offset, argv[i], length));
        offset += length;
    }
    CHECK(offset == size);
    /* macOS/Win32 argv packing uses the same byte contract as Linux procfs. */
    size_t packed_size = 0;
    char *packed = lx_host_pack_arguments(argc, argv, &packed_size);
    CHECK(packed && packed_size == size && !memcmp(packed, command, size));
    free(packed);
    free(command);
    CHECK(lx_host_command_line(NULL) == NULL);
}

int main(int argc, char **argv)
{
    arguments_test(argc, argv);
    CHECK(!strcmp(lx_host_os(), TWOINE_TEST_HOST_OS));
    char *executable = lx_host_executable_path();
    CHECK(executable && strstr(executable, "twoine_host_platform_tests"));
    free(executable);
    uint32_t process = 0, parent = 0;
    lx_host_process_ids(&process, &parent);
    CHECK(process && process != parent && lx_host_stack_limit());
    uint64_t total = 0, available = 0;
    CHECK(lx_host_system_memory(&total, &available) && total && available <= total);
    CHECK(!lx_host_system_memory(NULL, &available));
    size_t page = lx_host_page_size();
    CHECK(page && !(page & (page - 1)));
    unsigned char *memory = lx_host_memory_alloc(page);
    CHECK(memory);
    memory[0] = 42;
    CHECK(lx_host_vm_protect(memory, page, LX_HOST_READ) && memory[0] == 42);
    lx_host_memory_free(memory, page);
    CHECK(!lx_host_vm_map(NULL, 0, LX_HOST_READ, 0));
    LxHostCalendar calendar;
    CHECK(lx_host_calendar(INT64_C(1577934245), 1, &calendar));
    CHECK(calendar.year == 2020 && calendar.month == 1 && calendar.day == 2 && calendar.hour == 3 &&
          calendar.minute == 4 && calendar.second == 5 && calendar.weekday == 4 &&
          calendar.utc_offset_minutes == 0);
    uint64_t before = lx_host_monotonic_us();
    lx_host_sleep_ms(2);
    CHECK(lx_host_monotonic_us() >= before + 1000 && lx_host_realtime_us());
    puts("host argv, process, memory, clock and calendar contracts passed");
    return 0;
}
