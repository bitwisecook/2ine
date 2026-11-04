/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original OS/2 REXX system initialization runs in a separate service worker. */
#include "loader/internal.h"
#include "guest/abi.h"
#include "hal/host_process.h"
#include "hal/host_shared.h"

static LxHostShared *RexxClientLease;
static int RexxServiceWorker;

static void rexxLeaseName(char name[32])
{
    uint64 hash = UINT64_C(1469598103934665603);
    const char *scope[] = {getenv("TWOINE_BALL"), getenv("TWOINE_IPC_DIR"), GLoaderState.disks[2]};
    for (size_t i = 0; i < sizeof scope / sizeof scope[0]; i++)
    {
        for (const char *p = scope[i]; p && *p; p++)
            hash = (hash ^ (unsigned char)*p) * UINT64_C(1099511628211);
        hash = (hash ^ 0xff) * UINT64_C(1099511628211);
    }
    snprintf(name, 32, "/2ine-rx-%016llx", (unsigned long long)hash);
}

static int rexxQueueOwner(uint32 *owner)
{
    LxModule *queues = NULL;
    for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "QUECALLS"))
        {
            queues = module;
            break;
        }
    typedef uint32 (*OpenQueue)(uint32 *, uint32 *, char *);
    typedef uint32 (*CloseQueue)(uint32);
    OpenQueue open_queue =
        queues ? (OpenQueue)lx_host_library_symbol(queues->nativelib, "DosOpenQueue") : NULL;
    CloseQueue close_queue =
        queues ? (CloseQueue)lx_host_library_symbol(queues->nativelib, "DosCloseQueue") : NULL;
    uint32 handle = 0;
    if (!open_queue || !close_queue || open_queue(owner, &handle, "\\QUEUES\\RXQUEUE"))
        return 0;
    close_queue(handle);
    return 1;
}

static LxHostChild *spawnRexxService(void)
{
    char *executable = lx_host_executable_path();
    if (!executable)
        return NULL;
    char *const *host = lx_host_environment();
    size_t count = 0;
    while (host[count])
        count++;
    char **environment = calloc(count + 1, sizeof *environment);
    if (!environment)
    {
        free(executable);
        return NULL;
    }
    size_t output = 0;
    for (size_t i = 0; i < count; i++)
        if (strncmp(host[i], "IS_2INE=", 8) && strncmp(host[i], "TWOINE_CHILD_ARGUMENTS=", 23))
            environment[output++] = host[i];
    char *arguments[] = {executable, "--rexx-service", NULL};
    LxHostChild *child = lx_host_child_spawn_service(executable, arguments, environment);
    free(environment);
    free(executable);
    return child;
}

int ensureRexxService(void)
{
    if (RexxServiceWorker)
        return 1;
    if (!RexxClientLease)
    {
        char name[32];
        rexxLeaseName(name);
        LxHostSharedResult result = lx_host_shared_open(name, 1, &RexxClientLease);
        if (result == LX_HOST_SHARED_EXISTS)
            result = lx_host_shared_open(name, 0, &RexxClientLease);
        if (result != LX_HOST_SHARED_OK)
        {
            fprintf(stderr, "Cannot retain the OS/2 REXX service client lease\n");
            return 0;
        }
    }
    uint32 owner;
    if (rexxQueueOwner(&owner))
        return 1;
    LxHostChild *child = spawnRexxService();
    if (!child)
    {
        fprintf(stderr, "Cannot start the OS/2 REXX initialization worker\n");
        return 0;
    }
    const uint64 deadline = lx_host_monotonic_us() + 5000000;
    do
    {
        if (rexxQueueOwner(&owner))
        {
            /* Concurrent clients can race to start the system service. Keep
               the worker that owns the live queue and stop only our loser. */
            if (owner != lx_host_child_id(child))
                lx_host_child_terminate(child);
            lx_host_child_release(child);
            return 1;
        }
        LxHostChildStatus status;
        if (lx_host_child_wait(child, 1, &status))
            break;
        lx_host_sleep_ms(1);
    } while (lx_host_monotonic_us() < deadline);
    lx_host_child_terminate(child);
    lx_host_child_release(child);
    fprintf(stderr,
            "Original REXXINIT.DLL did not publish the REXX service queue within five seconds\n");
    return 0;
}

static LxModule *serviceStack(void)
{
    /* The loader's normal executable path creates a valid main stack and TIB
       before any original DLL initializer or guest service thread executes. */
    uint8 image[515] = {0};
    image[0] = 'M';
    image[1] = 'Z';
    lx_guest_store_u32(image + 60, 64);
    LxHeader *header = (LxHeader *)(image + 64);
    header->magic_l = 'L';
    header->magic_x = 'X';
    header->cpu_type = 2;
    header->os_type = 1;
    header->module_num_pages = 1;
    header->eip_object = 1;
    header->esp_object = 2;
    header->esp = 0xfff0;
    header->page_size = 4096;
    header->object_table_offset = sizeof *header;
    header->module_num_objects = 2;
    header->object_page_table_offset = sizeof *header + 2 * sizeof(LxObjectTableEntry);
    header->resident_name_table_offset =
        header->object_page_table_offset + sizeof(LxObjectPageTableEntry);
    uint8 *name = image + 64 + header->resident_name_table_offset;
    *name = 7;
    memcpy(name + 1, "SERVICE", 7);
    header->entry_table_offset = header->resident_name_table_offset + 11;
    header->fixup_page_table_offset = header->entry_table_offset + 1;
    header->fixup_record_table_offset = header->fixup_page_table_offset + 8;
    header->import_module_table_offset = header->fixup_record_table_offset;
    header->import_proc_table_offset = header->fixup_record_table_offset;
    header->data_pages_offset = 512;
    header->auto_ds_object_num = 2;
    LxObjectTableEntry *objects = (LxObjectTableEntry *)(image + 64 + header->object_table_offset);
    objects[0] = (LxObjectTableEntry){4096, 0x10000, 0x2005, 1, 1, 0};
    objects[1] = (LxObjectTableEntry){65536, 0x20000, 0x2003, 2, 0, 0};
    LxObjectPageTableEntry *page =
        (LxObjectPageTableEntry *)(image + 64 + header->object_page_table_offset);
    page->data_size = 3;
    memcpy(image + 512, "\x31\xc0\xc3", 3);
    const char *mount = GLoaderState.disks[2];
    char path[2048];
    if (!mount || snprintf(path, sizeof path, "%s/REXXSERVICE.EXE", mount) >= (int)sizeof path)
        return NULL;
    return loadModule(path, image, sizeof image, 0);
}

int runRexxService(void)
{
    RexxServiceWorker = 1;
    if (!serviceStack())
    {
        fprintf(stderr, "Cannot allocate the OS/2 REXX service invocation stack\n");
        return 1;
    }
    LxModule *service = loadModuleByModuleName("REXXINIT");
    if (!service || service->nativelib)
    {
        fprintf(stderr,
                "The REXX service requires original REXXINIT.DLL from OS/2 installation media\n");
        return 1;
    }
    char lease[32];
    rexxLeaseName(lease);
    uint64 idle_since = 0;
    for (;;)
    {
        int live = lx_host_shared_live(lease);
        if (live < 0)
            lx_host_process_exit(1);
        if (live)
            idle_since = 0;
        else if (!idle_since)
            idle_since = lx_host_monotonic_us();
        else if (lx_host_monotonic_us() - idle_since >= 1000000)
            lx_host_process_exit(0);
        lx_host_sleep_ms(50);
    }
}
