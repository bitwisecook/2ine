/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy queue results retain their 16-bit widths and far-address representation. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "os2/include/bseerr.h"
#include "os2/include/bsedos.h"
#include "os2/include/bsedos16.h"
#include "os2/include/private/bsep.h"
#include "emulator/runtime.h"
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void *segments[16];

static void *allocate_segment(uint16 *selector, int executable)
{
    (void)executable;
    for (unsigned i = 1; i < 16; i++)
        if (!segments[i])
        {
            *selector = i;
            return segments[i] = calloc(1, 65536);
        }
    *selector = 0xffff;
    return NULL;
}

static void *far_to_host(ULONG pointer)
{
    unsigned slot = pointer >> 19;
    return slot < 16 && segments[slot] ? (BYTE *)segments[slot] + (pointer & 0xffff) : NULL;
}

static ULONG host_to_far(void *pointer)
{
    for (unsigned i = 1; i < 16; i++)
        if (segments[i] && (uintptr_t)pointer >= (uintptr_t)segments[i] &&
            (uintptr_t)pointer < (uintptr_t)segments[i] + 65536)
            return ((ULONG)((i << 3) | 7) << 16) | ((uintptr_t)pointer - (uintptr_t)segments[i]);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc == 4);
    char ipc[] = "/tmp/twoine-legacy-queue-XXXXXX";
    CHECK(mkdtemp(ipc));
    CHECK(!setenv("TWOINE_IPC_DIR", ipc, 1));
    GProcessState.allocSegment = allocate_segment;
    GProcessState.convert1616to32 = far_to_host;
    GProcessState.convert32to1616 = host_to_far;
    void *queue_library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(queue_library);
    void *session_library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    CHECK(session_library);
    void *doscalls_library = dlopen(argv[3], RTLD_NOW | RTLD_LOCAL);
    CHECK(doscalls_library);
#define LOAD(library, function)                                                                    \
    __typeof__(function) *call_##function = dlsym(library, #function);                             \
    CHECK(call_##function)
    LOAD(queue_library, Dos16CreateQueue);
    LOAD(queue_library, Dos16ReadQueue);
    LOAD(queue_library, Dos16CloseQueue);
    LOAD(queue_library, lx_queue_open_private);
    LOAD(queue_library, lx_queue_write_copy);
    LOAD(queue_library, DosCloseQueue);
    LOAD(session_library, Dos16StopSession);
    LOAD(session_library, Dos16SetSession);
    LOAD(session_library, DosStartSession);
    LOAD(doscalls_library, Dos16SMSetTitle);
    LOAD(doscalls_library, lx_session_title);

    struct
    {
        USHORT before, handle, after;
    } output = {0xa55a, 0, 0x5aa5};

    CHECK(!call_Dos16CreateQueue(&output.handle, 0, "\\QUEUES\\LegacyFixture"));
    CHECK(output.handle && output.before == 0xa55a && output.after == 0x5aa5);
    CHECK(call_Dos16CreateQueue(&output.handle, 0, "\\QUEUES\\LegacyFixture") ==
          ERROR_QUE_DUPLICATE);
    CHECK(call_Dos16CreateQueue(&output.handle, 4, "\\QUEUES\\OtherFixture") ==
          ERROR_INVALID_PARAMETER);
    PID owner;
    HQUEUE sender;
    CHECK(!call_lx_queue_open_private(&owner, &sender, "\\QUEUES\\LegacyFixture"));
    const USHORT terminated[] = {123, 47};
    CHECK(!call_lx_queue_write_copy(sender, 0x1234, sizeof terminated, (PVOID)terminated, 0));

    struct
    {
        USHORT before, bytes, after;
    } length = {0xabcd, 0, 0xdcba};

    ULONG request = 0, pointer = 0;
    BYTE priority = 0xff;
    CHECK(
        !call_Dos16ReadQueue(output.handle, &request, &length.bytes, &pointer, 0, 0, &priority, 0));
    CHECK(length.bytes == sizeof terminated && length.before == 0xabcd && length.after == 0xdcba);
    CHECK((request >> 16) == 0x1234 && (USHORT)request == (USHORT)GProcessState.pib.process_id);
    CHECK(priority == 0 && pointer && !(pointer & 0xffff));
    CHECK(!memcmp(far_to_host(pointer), terminated, sizeof terminated));
    CHECK(call_Dos16ReadQueue(output.handle, &request, &length.bytes, &pointer, 0, 1, &priority,
                              0) == ERROR_QUE_EMPTY);
    CHECK(call_Dos16ReadQueue(output.handle, &request, &length.bytes, &pointer, 0, 2, &priority,
                              0) == ERROR_QUE_INVALID_WAIT);
    CHECK(!call_DosCloseQueue(sender));
    CHECK(!call_Dos16CloseQueue(output.handle));
    CHECK(call_Dos16CloseQueue(output.handle) == ERROR_QUE_INVALID_HANDLE);
    CHECK(call_Dos16StopSession(2, 0, 0) == ERROR_SMG_INVALID_STOP_OPTION);
    CHECK(call_Dos16StopSession(0, 0, 1) == ERROR_SMG_BAD_RESERVE);
    CHECK(call_Dos16StopSession(0, 0, 0) == ERROR_SMG_INVALID_SESSION_ID);
    CHECK(!call_Dos16StopSession(1, 0, 0));
    STATUSDATA status = {5, 0, 0};
    CHECK(call_Dos16SetSession(0, &status) == ERROR_SMG_INVALID_DATA_LENGTH);
    STARTDATA start = {0};
    start.Length = 60;
    start.PgmName = "C:\\Missing.exe";
    start.Related = 2;
    ULONG sid;
    PID pid;
    CHECK(call_DosStartSession(&start, &sid, &pid) == ERROR_SMG_INVALID_RELATED_OPT);
    /* DOSSMSETTITLE sets the title the switch list shows for this session,
       truncated to the switch-list title size. */
    char title[80];
    CHECK(call_Dos16SMSetTitle(NULL) == ERROR_INVALID_PARAMETER);
    CHECK(!call_Dos16SMSetTitle("Legacy fixture"));
    call_lx_session_title(title, sizeof title);
    CHECK(!strcmp(title, "Legacy fixture"));
    char long_title[100];
    memset(long_title, 'T', sizeof long_title - 1);
    long_title[sizeof long_title - 1] = 0;
    CHECK(!call_Dos16SMSetTitle(long_title));
    call_lx_session_title(title, sizeof title);
    CHECK(strlen(title) == 63 && title[0] == 'T');
    CHECK(!dlclose(doscalls_library));
    CHECK(!dlclose(session_library));
    CHECK(!dlclose(queue_library));
    for (unsigned i = 1; i < 16; i++)
        free(segments[i]);
    DIR *directory = opendir(ipc);
    CHECK(directory);
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            char path[1024];
            CHECK(snprintf(path, sizeof path, "%s/%s", ipc, entry->d_name) > 0);
            CHECK(!unlink(path));
        }
    CHECK(!closedir(directory));
    CHECK(!rmdir(ipc));
    puts("legacy queue addresses, output widths and session validation passed");
    return 0;
}
