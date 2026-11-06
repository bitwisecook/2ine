/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE instances retain callback registration and per-instance errors on their owning thread. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
typedef struct DdeString
{
    struct DdeString *next;
    ULONG atom, references;
    BOOL service;
} DdeString;
typedef struct DdeInstance
{
    struct DdeInstance *next;
    void *owner;
    DdeString *strings;
    BOOL filter;
    ULONG handle, callback, flags, error;
} DdeInstance;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *lock;
static DdeInstance *instances;
static void cleanup_dde_owned(ULONG id);
static void initialize(void)
{
    lock = lx_host_monitor_create();
    if (!lock)
        abort();
}
static void destroy_instance(void *resource)
{
    DdeInstance *instance = resource;
    cleanup_dde_owned(instance->handle);
    lx_host_monitor_lock(lock);
    for (DdeInstance **link = &instances; *link; link = &(*link)->next)
        if (*link == instance)
        {
            *link = instance->next;
            break;
        }
    lx_host_monitor_unlock(lock);
    while (instance->strings)
    {
        DdeString *string = instance->strings;
        instance->strings = string->next;
        for (ULONG i = 0; i < string->references; i++)
            GlobalDeleteAtom(string->atom);
        if (string->service)
            GlobalDeleteAtom(string->atom);
        free(string);
    }
    free(instance);
}
static BOOL borrow_instance(ULONG handle, LxHandleBorrow *borrow)
{
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE, borrow))
        return FALSE;
    if (((DdeInstance *)borrow->resource)->owner == lxHostThreadState())
        return TRUE;
    lx_handle_release(borrow);
    return FALSE;
}
ULONG DdeInitialize(PULONG id, ULONG callback, ULONG flags, ULONG reserved)
{
    TRACE_NATIVE("DdeInitialize(%p, %x, %x, %u)", id, (uint)callback, (uint)flags, (uint)reserved);
    if (!id || reserved || (flags & ~0xff3ff031u) || (flags & 15) > 1 ||
        !open32ValidProcedure(callback))
        return 0x4006; /* DMLERR_INVALIDPARAMETER */
    if ((flags & 15) == 1)
        return 0x4004; /* DMLERR_DLL_USAGE: monitor callbacks are not provided. */
    if (*id)
    {
        LxHandleBorrow borrow;
        if (!borrow_instance(*id, &borrow))
            return 0x4006;
        DdeInstance *instance = borrow.resource;
        instance->callback = callback;
        instance->flags = flags;
        instance->error = 0;
        lx_handle_release(&borrow);
        return 0;
    }
    if (!open32Presentation())
        return 0x400f; /* DMLERR_SYS_ERROR */
    DdeInstance *instance = calloc(1, sizeof *instance);
    if (!instance)
        return 0x4007;
    lx_host_once(&once, initialize);
    instance->owner = lxHostThreadState();
    instance->callback = callback;
    instance->flags = flags;
    instance->filter = TRUE;
    instance->handle = lx_handle_create(LX_HANDLE_OPEN32_DDE, instance, destroy_instance);
    if (!instance->handle)
    {
        free(instance);
        return 0x4007;
    }
    lx_host_monitor_lock(lock);
    instance->next = instances;
    instances = instance;
    lx_host_monitor_unlock(lock);
    *id = instance->handle;
    return 0;
}
ULONG DdeGetLastError(ULONG id)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0x4006;
    DdeInstance *instance = borrow.resource;
    ULONG error = instance->error;
    instance->error = 0;
    lx_handle_release(&borrow);
    return error;
}
BOOL DdeUninitialize(ULONG id)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return FALSE;
    BOOL result = lx_handle_close(id, LX_HANDLE_OPEN32_DDE);
    lx_handle_release(&borrow);
    return result;
}
void open32DdeCleanup(void)
{
    lx_host_once(&once, initialize);
    for (;;)
    {
        lx_host_monitor_lock(lock);
        ULONG handle = instances ? instances->handle : 0;
        lx_host_monitor_unlock(lock);
        if (!handle)
            return;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE);
    }
}

static DdeString **find_string(DdeInstance *instance, ULONG atom)
{
    DdeString **link = &instance->strings;
    while (*link && (*link)->atom != atom)
        link = &(*link)->next;
    return link;
}
ULONG DdeCreateStringHandle(ULONG id, PSZ text, LONG codepage)
{
    TRACE_NATIVE("DdeCreateStringHandle(%u, '%s', %d)", (uint)id, text ? text : "", (int)codepage);
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG atom = 0;
    if (!text || strlen(text) > 255 || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (*text)
    {
        atom = GlobalAddAtom(text);
        DdeString **link = find_string(instance, atom);
        DdeString *entry = *link;
        if (atom && !entry)
        {
            entry = calloc(1, sizeof *entry);
            if (entry)
            {
                entry->atom = atom;
                *link = entry;
            }
        }
        if (!atom || !entry || entry->references == UINT32_MAX)
        {
            if (atom)
                GlobalDeleteAtom(atom);
            atom = 0;
            instance->error = 0x4007;
        }
        else
            entry->references++;
    }
    lx_handle_release(&borrow);
    return atom;
}
BOOL DdeFreeStringHandle(ULONG id, ULONG atom)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return FALSE;
    DdeInstance *instance = borrow.resource;
    DdeString **link = find_string(instance, atom);
    BOOL result = *link && (*link)->references;
    if (!result)
        instance->error = 0x4006;
    else
    {
        DdeString *entry = *link;
        GlobalDeleteAtom(entry->atom);
        if (!--entry->references && !entry->service)
        {
            *link = entry->next;
            free(entry);
        }
    }
    lx_handle_release(&borrow);
    return result;
}
BOOL DdeKeepStringHandle(ULONG id, ULONG atom)
{
    char name[256];
    if (!GlobalGetAtomName(atom, name, sizeof name))
    {
        LxHandleBorrow borrow;
        if (borrow_instance(id, &borrow))
        {
            ((DdeInstance *)borrow.resource)->error = 0x4006;
            lx_handle_release(&borrow);
        }
        return FALSE;
    }
    return DdeCreateStringHandle(id, name, 1004) == atom;
}
ULONG DdeQueryString(ULONG id, ULONG atom, PSZ output, ULONG capacity, LONG codepage)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    char name[256];
    ULONG length = GlobalGetAtomName(atom, name, sizeof name), result = 0;
    if (!length || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (!output)
        result = length;
    else if (capacity)
    {
        result = length < capacity ? length : capacity - 1;
        memcpy(output, name, result);
        output[result] = 0;
    }
    lx_handle_release(&borrow);
    return result;
}
LONG DdeCmpStringHandles(ULONG first, ULONG second)
{
    if (first == second)
        return 0;
    char left[256], right[256];
    if (!GlobalGetAtomName(first, left, sizeof left))
        return -1;
    if (!GlobalGetAtomName(second, right, sizeof right))
        return 1;
    int result = strcasecmp(left, right);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

void open32DdeSetError(ULONG id, ULONG error)
{
    LxHandleBorrow borrow;
    if (borrow_instance(id, &borrow))
    {
        ((DdeInstance *)borrow.resource)->error = error;
        lx_handle_release(&borrow);
    }
}
ULONG DdeNameService(ULONG id, ULONG atom, ULONG reserved, ULONG command)
{
    TRACE_NATIVE("DdeNameService(%u, %u, %u, %u)", (uint)id, (uint)atom, (uint)reserved,
                 (uint)command);
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG result = 0;
    DdeString **link = find_string(instance, atom);
    if (reserved || (command != 1 && command != 2 && command != 4 && command != 8) ||
        (command == 1 && (!atom || !*link)) || (atom && !*link))
        instance->error = 0x4006;
    else if (command == 4 || command == 8)
    {
        instance->filter = command == 4;
        result = 1;
    }
    else if (command == 1 && (instance->flags & 0x10))
        instance->error = 0x4004; /* Client-only instances cannot register a service. */
    else
    {
        BOOL unsupported = FALSE;
        ULONG skip = command == 1 ? 0x80000 : 0x100000;
        lx_host_monitor_lock(lock);
        for (DdeInstance *peer = instances; peer; peer = peer->next)
            if (peer != instance && peer->owner != instance->owner && !(peer->flags & skip))
                unsupported = TRUE;
        lx_host_monitor_unlock(lock);
        if (unsupported)
            instance->error = 0x4004; /* Peer registration notifications require a DDE transport. */
        else if (command == 1)
        {
            DdeString *entry = *link;
            char name[256];
            if (entry->service)
                result = 1;
            else if (GlobalGetAtomName(atom, name, sizeof name) && GlobalAddAtom(name) == atom)
            {
                entry->service = TRUE;
                result = 1;
            }
            else
                instance->error = 0x4007;
        }
        else
        {
            for (DdeString **item = &instance->strings; *item;)
            {
                DdeString *entry = *item;
                if (entry->service && (!atom || atom == entry->atom))
                {
                    entry->service = FALSE;
                    GlobalDeleteAtom(entry->atom);
                    if (!entry->references)
                    {
                        *item = entry->next;
                        free(entry);
                        continue;
                    }
                }
                item = &entry->next;
            }
            result = 1;
        }
    }
    lx_handle_release(&borrow);
    return result;
}

/* Data and conversations are typed resources. Guest callbacks use an x86
   argument thunk, keeping all eight callback words out of the host C ABI. */
typedef struct DdeData
{
    LxGuestMapping *mapping;
    ULONG owner, size, item, format, flags, accesses, handle;
    struct DdeData *next;
} DdeData;
typedef struct DdeConversation
{
    ULONG handle, client, server, service, topic, user, item, format, type;
    BOOL advise;
    struct DdeConversation *next;
} DdeConversation;
static DdeData *dde_data;
static DdeConversation *dde_conversations;
static LxGuestMapping *dde_callback_code;
static ULONG dde_callback(DdeInstance *instance, ULONG type, ULONG format, ULONG conversation,
                          ULONG first, ULONG second, ULONG data, ULONG one, ULONG two)
{
    if (!instance || instance->owner != lxHostThreadState() || !instance->callback)
        return 0;
    if (!dde_callback_code)
    {
        static const uint8 code[] = {0x8B, 0x44, 0x24, 0x04, 0xFF, 0x70, 0x1C, 0xFF, 0x70,
                                     0x18, 0xFF, 0x70, 0x14, 0xFF, 0x70, 0x10, 0xFF, 0x70,
                                     0x0C, 0xFF, 0x70, 0x08, 0xFF, 0x70, 0x04, 0xFF, 0x30,
                                     0xFF, 0x50, 0x20, 0x83, 0xC4, 0x20, 0xC3};
        dde_callback_code = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
        if (!dde_callback_code)
            return 0;
        memcpy(lx_execution_mapping_host(dde_callback_code), code, sizeof code);
        if (!lx_execution_protect(GLoaderState.execution, dde_callback_code,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GLoaderState.execution, dde_callback_code);
            dde_callback_code = NULL;
            return 0;
        }
    }
    LxGuestMapping *arguments = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!arguments)
        return 0;
    ULONG values[] = {type, format, conversation,      first, second, data,
                      one,  two,    instance->callback};
    for (unsigned i = 0; i < 9; i++)
        lx_guest_store_u32((uint8 *)lx_execution_mapping_host(arguments) + i * 4, values[i]);
    ULONG result = lx_execution_callback32_4(GLoaderState.execution,
                                             lx_execution_mapping_address(dde_callback_code),
                                             lx_execution_mapping_address(arguments), 0, 0, 0);
    lx_execution_unmap(GLoaderState.execution, arguments);
    return result;
}
static void destroy_dde_data(void *resource)
{
    DdeData *data = resource;
    lx_host_monitor_lock(lock);
    DdeData **link = &dde_data;
    while (*link && *link != data)
        link = &(*link)->next;
    if (*link)
        *link = data->next;
    lx_host_monitor_unlock(lock);
    if (data->mapping)
        lx_execution_unmap(GLoaderState.execution, data->mapping);
    free(data);
}
ULONG DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset, ULONG item,
                          ULONG format, ULONG flags)
{
    LxHandleBorrow instance = {0};
    if (!borrow_instance(id, &instance))
        return 0;
    DdeData *data = NULL;
    ULONG handle = 0;
    if ((flags & ~1u) || offset > 16u * 1024u * 1024u || count > 16u * 1024u * 1024u - offset)
    {
        ((DdeInstance *)instance.resource)->error = 0x4006;
        goto data_done;
    }
    data = calloc(1, sizeof *data);
    if (data)
        data->mapping = lx_execution_map(GLoaderState.execution, 0,
                                         (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             ? (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             : 4096,
                                         3, 0);
    if (!data || !data->mapping)
    {
        if (data)
            free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->owner = id;
    data->size = offset + count;
    data->item = item;
    data->format = format;
    data->flags = flags;
    if (input && count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    handle = lx_handle_create(LX_HANDLE_OPEN32_DDE_DATA, data, destroy_dde_data);
    if (!handle)
    {
        lx_execution_unmap(GLoaderState.execution, data->mapping);
        free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->handle = handle;
    lx_host_monitor_lock(lock);
    data->next = dde_data;
    dde_data = data;
    lx_host_monitor_unlock(lock);
data_done:
    lx_handle_release(&instance);
    return handle;
}
ULONG DdeAccessData(ULONG handle, PVOID size)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    data->accesses++;
    if (size)
        lx_guest_store_u32(size, data->size);
    ULONG address = lx_execution_mapping_address(data->mapping);
    lx_handle_release(&borrow);
    return address;
}
BOOL DdeUnaccessData(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = data->accesses != 0;
    if (valid)
        data->accesses--;
    lx_handle_release(&borrow);
    return valid;
}
BOOL DdeFreeDataHandle(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = !data->accesses;
    if (valid)
        valid = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    else
        open32DdeSetError(data->owner, 0x4004);
    lx_handle_release(&borrow);
    return valid;
}
ULONG DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG result = 0;
    if (data->accesses || (count && !input) || offset > 16u * 1024u * 1024u ||
        count > 16u * 1024u * 1024u - offset)
    {
        open32DdeSetError(data->owner, 0x4006);
        goto add_done;
    }
    ULONG required = count + offset;
    if (required > lx_execution_mapping_size(data->mapping))
    {
        LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0,
                                                   ((size_t)required + 4095) & ~(size_t)4095, 3, 0);
        if (!mapping)
        {
            open32DdeSetError(data->owner, 0x4007);
            goto add_done;
        }
        memcpy(lx_execution_mapping_host(mapping), lx_execution_mapping_host(data->mapping),
               data->size);
        lx_execution_unmap(GLoaderState.execution, data->mapping);
        data->mapping = mapping;
    }
    if (count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    if (required > data->size)
        data->size = required;
    result = handle;
add_done:
    lx_handle_release(&borrow);
    return result;
}
ULONG DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG count = offset > data->size ? 0 : data->size - offset;
    if (output)
    {
        if (count > capacity)
            count = capacity;
        if (count)
            memcpy(output, (uint8 *)lx_execution_mapping_host(data->mapping) + offset, count);
    }
    lx_handle_release(&borrow);
    return count;
}
static void destroy_conversation(void *resource)
{
    DdeConversation *conversation = resource;
    lx_host_monitor_lock(lock);
    DdeConversation **link = &dde_conversations;
    while (*link && *link != conversation)
        link = &(*link)->next;
    if (*link)
        *link = conversation->next;
    lx_host_monitor_unlock(lock);
    free(conversation);
}
ULONG DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context)
{
    LxHandleBorrow client = {0};
    if (!borrow_instance(id, &client))
        return 0;
    DdeInstance *instance = client.resource;
    ULONG server_handle = 0;
    if (context && lx_guest_load_u32(context) != 40)
    {
        instance->error = 0x4006;
        lx_handle_release(&client);
        return 0;
    }
    lx_host_monitor_lock(lock);
    for (DdeInstance *peer = instances; peer; peer = peer->next)
    {
        if (peer == instance || peer->owner != instance->owner || (peer->flags & 0x10))
            continue;
        BOOL match = !peer->filter;
        for (DdeString *string = peer->strings; string; string = string->next)
            if (string->service && (!service || service == string->atom))
            {
                match = TRUE;
                break;
            }
        if (match)
        {
            server_handle = peer->handle;
            break;
        }
    }
    lx_host_monitor_unlock(lock);
    LxHandleBorrow server = {0};
    ULONG handle = 0;
    if (!server_handle || !borrow_instance(server_handle, &server))
    {
        instance->error = 0x400A;
        goto connect_done;
    }
    if (!dde_callback(server.resource, 0x1062, 0, 0, topic, service, 0, 0, id == server_handle))
    {
        instance->error = 0x400A;
        goto connect_done;
    }
    DdeConversation *conversation = calloc(1, sizeof *conversation);
    if (!conversation)
    {
        instance->error = 0x4007;
        goto connect_done;
    }
    conversation->client = id;
    conversation->server = server_handle;
    conversation->service = service;
    conversation->topic = topic;
    handle =
        lx_handle_create(LX_HANDLE_OPEN32_DDE_CONVERSATION, conversation, destroy_conversation);
    if (!handle)
    {
        free(conversation);
        instance->error = 0x4007;
        goto connect_done;
    }
    conversation->handle = handle;
    lx_host_monitor_lock(lock);
    conversation->next = dde_conversations;
    dde_conversations = conversation;
    lx_host_monitor_unlock(lock);
    dde_callback(server.resource, 0x8072, 0, handle, topic, service, 0, 0, 0);
connect_done:
    lx_handle_release(&server);
    lx_handle_release(&client);
    return handle;
}
BOOL DdeDisconnect(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return FALSE;
    DdeConversation *conversation = borrow.resource;
    LxHandleBorrow server = {0};
    if (borrow_instance(conversation->server, &server))
    {
        dde_callback(server.resource, 0x80C2, 0, handle, 0, 0, 0, 0, 0);
        lx_handle_release(&server);
    }
    BOOL result = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    lx_handle_release(&borrow);
    return result;
}
ULONG DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item, ULONG format,
                           ULONG type, ULONG timeout, PVOID status)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    LxHandleBorrow client = {0}, server = {0};
    ULONG result = 0, data = 0;
    BOOL temporary = FALSE;
    if (!borrow_instance(conversation->client, &client) ||
        !borrow_instance(conversation->server, &server))
        goto transaction_done;
    if (timeout == UINT32_MAX)
    {
        ((DdeInstance *)client.resource)->error = 0x4004;
        goto transaction_done;
    }
    if (type != 0x20B0 && type != 0x4090 && type != 0x4050 && (type & ~0xCu) != 0x1030 &&
        type != 0x8040)
    {
        ((DdeInstance *)client.resource)->error = 0x4006;
        goto transaction_done;
    }
    if (size == UINT32_MAX)
        data = address;
    else if (size)
    {
        LxGuestBorrow input = {0};
        if (!lx_execution_borrow(GLoaderState.execution, address, size, LX_GUEST_READ, &input))
        {
            ((DdeInstance *)client.resource)->error = 0x4006;
            goto transaction_done;
        }
        data = DdeCreateDataHandle(conversation->client, input.host, size, 0, item, format, 0);
        lx_execution_release(&input);
        temporary = TRUE;
        if (!data)
            goto transaction_done;
    }
    conversation->item = item;
    conversation->format = format;
    conversation->type = type;
    result =
        dde_callback(server.resource, type, format, handle, conversation->topic, item, data, 0, 0);
    if ((type & ~0xCu) == 0x1030 && result)
        conversation->advise = TRUE;
    if (type == 0x8040)
    {
        conversation->advise = FALSE;
        result = 1;
    }
    if (status)
        lx_guest_store_u32(status, result ? 0x8000 : 0);
    if (!result)
        ((DdeInstance *)client.resource)->error = 0x4009;
    if (type != 0x20B0)
        result = result ? 1 : 0;
transaction_done:
    if (temporary && data)
        DdeFreeDataHandle(data);
    lx_handle_release(&server);
    lx_handle_release(&client);
    lx_handle_release(&borrow);
    return result;
}
BOOL DdeSetUserHandle(ULONG handle, ULONG transaction, ULONG user)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return FALSE;
    DdeConversation *conversation = borrow.resource;
    BOOL valid = transaction == UINT32_MAX;
    if (valid)
        conversation->user = user;
    else
        open32DdeSetError(conversation->client, 0x4006);
    lx_handle_release(&borrow);
    return valid;
}
ULONG DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!output || lx_guest_load_u32(output) != 100 ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    if (transaction != UINT32_MAX && transaction)
    {
        open32DdeSetError(conversation->client, 0x4006);
        lx_handle_release(&borrow);
        return 0;
    }
    memset(output, 0, 100);
    ULONG fields[] = {100,
                      conversation->user,
                      handle,
                      conversation->service,
                      conversation->service,
                      conversation->topic,
                      conversation->item,
                      conversation->format,
                      conversation->type,
                      0x10 | 0x1,
                      2,
                      0,
                      0,
                      40,
                      0,
                      0,
                      1004,
                      0x409,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0};
    for (unsigned i = 0; i < 25; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    lx_handle_release(&borrow);
    return 100;
}
BOOL DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction)
{
    LxHandleBorrow instance = {0};
    if (!borrow_instance(id, &instance))
        return FALSE;
    BOOL valid = !transaction;
    if (handle)
    {
        LxHandleBorrow conversation = {0};
        valid = valid &&
                lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &conversation) &&
                ((DdeConversation *)conversation.resource)->client == id;
        lx_handle_release(&conversation);
    }
    if (!valid)
        ((DdeInstance *)instance.resource)->error = 0x4006;
    lx_handle_release(&instance);
    return valid;
}
BOOL DdePostAdvise(ULONG id, ULONG topic, ULONG item)
{
    LxHandleBorrow server = {0};
    if (!borrow_instance(id, &server))
        return FALSE;
    ULONG *handles = NULL;
    size_t count = 0;
    lx_host_monitor_lock(lock);
    for (DdeConversation *conversation = dde_conversations; conversation;
         conversation = conversation->next)
        if (conversation->server == id && conversation->advise &&
            (!topic || topic == conversation->topic) && (!item || item == conversation->item))
        {
            ULONG *next = realloc(handles, (count + 1) * sizeof *next);
            if (!next)
            {
                free(handles);
                lx_host_monitor_unlock(lock);
                lx_handle_release(&server);
                return FALSE;
            }
            handles = next;
            handles[count++] = conversation->handle;
        }
    lx_host_monitor_unlock(lock);
    for (size_t i = 0; i < count; i++)
    {
        LxHandleBorrow borrow = {0};
        if (!lx_handle_borrow(handles[i], LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
            continue;
        DdeConversation *conversation = borrow.resource;
        LxHandleBorrow client = {0};
        ULONG data =
            dde_callback(server.resource, 0x2022, conversation->format, conversation->handle,
                         conversation->topic, conversation->item, 0, 0, 0);
        if (borrow_instance(conversation->client, &client))
        {
            dde_callback(client.resource, 0x4010, conversation->format, conversation->handle,
                         conversation->topic, conversation->item, data, 0, 0);
            lx_handle_release(&client);
        }
        if (data)
            DdeFreeDataHandle(data);
        lx_handle_release(&borrow);
    }
    free(handles);
    lx_handle_release(&server);
    return TRUE;
}

static void cleanup_dde_owned(ULONG id)
{
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(lock);
        for (DdeConversation **link = &dde_conversations; *link; link = &(*link)->next)
            if ((*link)->client == id || (*link)->server == id)
            {
                DdeConversation *conversation = *link;
                handle = conversation->handle;
                *link = conversation->next;
                break;
            }
        lx_host_monitor_unlock(lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    }
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(lock);
        for (DdeData **link = &dde_data; *link; link = &(*link)->next)
            if ((*link)->owner == id)
            {
                DdeData *data = *link;
                handle = data->handle;
                *link = data->next;
                break;
            }
        lx_host_monitor_unlock(lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    }
}
