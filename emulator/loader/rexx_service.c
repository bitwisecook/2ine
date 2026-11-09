/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original OS/2 REXX system initialization runs in a separate service worker. */
#include "emulator/loader/internal.h"
#include "emulator/abi.h"
#include "host/process.h"
#include "host/shared_memory.h"

enum
{
    LEASE_NAME_SIZE = 32,
    /* Time allowed for REXXINIT.DLL to publish its queue. */
    SERVICE_START_TIMEOUT_US = 5000000,
    /* The service exits once no client has held the lease for this long. */
    SERVICE_IDLE_EXIT_US = 1000000,
    SERVICE_POLL_MS = 50,

    /* Layout of the synthetic REXXSERVICE.EXE image that gives the service a main stack. */
    SERVICE_IMAGE_SIZE = 515,
    SERVICE_LX_HEADER_OFFSET = ENEWEXE,
    SERVICE_CODE_OBJECT_BASE = 0x10000,
    SERVICE_STACK_OBJECT_BASE = 0x20000,
    SERVICE_STACK_OBJECT_SIZE = 0x10000,
    SERVICE_INITIAL_ESP = 0xfff0,
    SERVICE_DATA_PAGES_OFFSET = 512,
    SERVICE_NAME_TABLE_SIZE = 11,     /* length byte, "SERVICE", ordinal, table terminator */
    SERVICE_FIXUP_PAGE_TABLE_SIZE = 8 /* two offsets for the single page */
};

/* 64-bit FNV-1a parameters for the lease name. */
#define FNV64_OFFSET_BASIS UINT64_C(1469598103934665603)
#define FNV64_PRIME UINT64_C(1099511628211)

static LxHostShared *RexxClientLease;
static int RexxServiceWorker;

/* Clients sharing a ball, IPC scope and C: drive share one service lease. */
static void rexxLeaseName(char name[LEASE_NAME_SIZE])
{
    uint64 hash = FNV64_OFFSET_BASIS;
    const char *scope[] = {getenv("TWOINE_BALL"), getenv("TWOINE_IPC_DIR"),
                           GProcessState.disks['C' - 'A']};
    for (size_t i = 0; i < sizeof scope / sizeof scope[0]; i++)
    {
        for (const char *p = scope[i]; p && *p; p++)
            hash = (hash ^ (unsigned char)*p) * FNV64_PRIME;
        /* Separate the scope strings so their boundaries affect the hash. */
        hash = (hash ^ 0xff) * FNV64_PRIME;
    }
    snprintf(name, LEASE_NAME_SIZE, "/2ine-rx-%016llx", (unsigned long long)hash);
}

static int rexxQueueOwner(uint32 *owner)
{
    /* QUECALLS forwards its queue entries to DOSCALLS, which implements them. */
    LxModule *queues = NULL;
    for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "DOSCALLS"))
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
    char **environment = calloc(count + 2, sizeof *environment);
    if (!environment)
    {
        free(executable);
        return NULL;
    }
    size_t output = 0;
    for (size_t i = 0; i < count; i++)
        if (strncmp(host[i], "IS_2INE=", strlen("IS_2INE=")) &&
            strncmp(host[i], "TWOINE_CHILD_ARGUMENTS=", strlen("TWOINE_CHILD_ARGUMENTS=")) &&
            strncmp(host[i], "TWOINE_DESKTOP_AUXILIARY=", strlen("TWOINE_DESKTOP_AUXILIARY=")))
            environment[output++] = host[i];
    environment[output++] = "TWOINE_DESKTOP_AUXILIARY=1";
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
        char name[LEASE_NAME_SIZE];
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
    const uint64 deadline = lx_host_monotonic_us() + SERVICE_START_TIMEOUT_US;
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
    uint8 image[SERVICE_IMAGE_SIZE] = {0};
    image[0] = 'M';
    image[1] = 'Z';
    lx_guest_store_u32(image + ENEWHDR, SERVICE_LX_HEADER_OFFSET);
    LxHeader *header = (LxHeader *)(image + SERVICE_LX_HEADER_OFFSET);
    header->magic_l = 'L';
    header->magic_x = 'X';
    header->cpu_type = E32CPU386;
    header->os_type = LX_OS_OS2;
    header->module_num_pages = 1;
    header->eip_object = 1;
    header->esp_object = 2;
    header->esp = SERVICE_INITIAL_ESP;
    header->page_size = LX_PAGE_SIZE;
    header->object_table_offset = sizeof *header;
    header->module_num_objects = 2;
    header->object_page_table_offset = sizeof *header + 2 * sizeof(LxObjectTableEntry);
    header->resident_name_table_offset =
        header->object_page_table_offset + sizeof(LxObjectPageTableEntry);
    uint8 *name = image + SERVICE_LX_HEADER_OFFSET + header->resident_name_table_offset;
    *name = (uint8)strlen("SERVICE");
    memcpy(name + 1, "SERVICE", strlen("SERVICE"));
    header->entry_table_offset = header->resident_name_table_offset + SERVICE_NAME_TABLE_SIZE;
    header->fixup_page_table_offset = header->entry_table_offset + 1;
    header->fixup_record_table_offset =
        header->fixup_page_table_offset + SERVICE_FIXUP_PAGE_TABLE_SIZE;
    header->import_module_table_offset = header->fixup_record_table_offset;
    header->import_proc_table_offset = header->fixup_record_table_offset;
    header->data_pages_offset = SERVICE_DATA_PAGES_OFFSET;
    header->auto_ds_object_num = 2;
    LxObjectTableEntry *objects =
        (LxObjectTableEntry *)(image + SERVICE_LX_HEADER_OFFSET + header->object_table_offset);
    objects[0] = (LxObjectTableEntry){
        LX_PAGE_SIZE, SERVICE_CODE_OBJECT_BASE, OBJBIGDEF | OBJEXEC | OBJREAD, 1, 1, 0};
    objects[1] = (LxObjectTableEntry){SERVICE_STACK_OBJECT_SIZE,
                                      SERVICE_STACK_OBJECT_BASE,
                                      OBJBIGDEF | OBJWRITE | OBJREAD,
                                      2,
                                      0,
                                      0};
    LxObjectPageTableEntry *page = (LxObjectPageTableEntry *)(image + SERVICE_LX_HEADER_OFFSET +
                                                              header->object_page_table_offset);
    /* xor eax, eax; ret */
    static const uint8 return_zero[] = {0x31, 0xc0, 0xc3};
    page->data_size = sizeof return_zero;
    memcpy(image + SERVICE_DATA_PAGES_OFFSET, return_zero, sizeof return_zero);
    const char *mount = GProcessState.disks['C' - 'A'];
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
    char lease[LEASE_NAME_SIZE];
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
        else if (lx_host_monotonic_us() - idle_since >= SERVICE_IDLE_EXIT_US)
            lx_host_process_exit(0);
        lx_host_sleep_ms(SERVICE_POLL_MS);
    }
}
