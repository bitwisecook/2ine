/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE with other processes of the ball: a shared service directory, a named mailbox per process
   read on a host thread, and data carried by value. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/clock.h"
#include "host/mailbox.h"
#include "host/process.h"
#include <stdio.h>
#include "os2/ps/awe/dde/dde.h"

typedef struct RemoteRecord
{
    uint32 kind, sequence, process, conversation, peer, type, format, first, second, one, two;
    uint32 result, size;
} RemoteRecord;

typedef struct ServiceEntry
{
    uint32 process, instance, atom;
} ServiceEntry;

typedef struct ServiceDirectory
{
    uint32 lock;
    ServiceEntry entries[SERVICE_LIMIT];
} ServiceDirectory;

typedef struct RemoteReply
{
    struct RemoteReply *next;
    RemoteRecord record;
    uint8 *data;
} RemoteReply;

static LxHostOnce directory_once = LX_HOST_ONCE_INIT;

static ServiceDirectory *directory;

static uint32 own_process;

static LxHostMailbox *remote_inbox;

static LxHostThread *remote_thread;

static RemoteReply *remote_replies;

static uint32 remote_sequence, remote_stop;

static void mailbox_name(char name[32], uint32 process)
{
    snprintf(name, 32, "dde-%u", (unsigned)process);
}

static void initialize_directory(void)
{
    uint32 parent;
    lx_host_process_ids(&own_process, &parent);
    uint64 hash = 1469598103934665603ULL;
    const char *scope = getenv("TWOINE_BALL");
    if (!scope)
        scope = getenv("TWOINE_IPC_DIR");
    for (const char *p = scope; p && *p; p++)
        hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
    char name[40];
    snprintf(name, sizeof name, "/2ine-dde1-%016llx", (unsigned long long)hash);
    LxHostShared *shared = NULL;
    LxHostSharedResult opened = lx_host_shared_open(name, sizeof *directory, &shared);
    if (opened == LX_HOST_SHARED_EXISTS)
        opened = lx_host_shared_open(name, 0, &shared);
    if (opened == LX_HOST_SHARED_OK && lx_host_shared_size(shared) == sizeof *directory)
        directory = lx_host_shared_map(shared, NULL, 0);
}

static BOOL lock_directory(void)
{
    lx_host_once(&directory_once, initialize_directory);
    if (!directory)
        return FALSE;
    for (unsigned attempt = 0;; attempt++)
    {
        uint32 expected = 0;
        if (__atomic_compare_exchange_n(&directory->lock, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
            return TRUE;
        if (attempt == 2000) /* The holder died; the table itself stays consistent. */
            return TRUE;
        lx_host_sleep_ms(1);
    }
}

static void unlock_directory(void)
{
    __atomic_store_n(&directory->lock, 0, __ATOMIC_RELEASE);
}

static BOOL process_alive(uint32 process)
{
    char name[32];
    mailbox_name(name, process);
    LxHostMailbox *mailbox;
    uint32 owner;
    if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &mailbox, &owner))
        return FALSE;
    lx_host_mailbox_close(mailbox);
    return TRUE;
}

void dde_publish_service(ULONG instance, ULONG atom)
{
    if (!lock_directory())
        return;
    ServiceEntry *free_entry = NULL;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        ServiceEntry *entry = directory->entries + i;
        if (!entry->process)
            free_entry = free_entry ? free_entry : entry;
        else if (entry->process == own_process && entry->instance == instance &&
                 entry->atom == atom)
            free_entry = NULL, i = SERVICE_LIMIT;
    }
    if (free_entry)
        *free_entry = (ServiceEntry){own_process, instance, atom};
    unlock_directory();
}

void dde_withdraw_service(ULONG instance, ULONG atom, BOOL all)
{
    if (!lock_directory())
        return;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        ServiceEntry *entry = directory->entries + i;
        if (entry->process == own_process && entry->instance == instance &&
            (all || entry->atom == atom))
            *entry = (ServiceEntry){0};
    }
    unlock_directory();
}

static BOOL remote_send(uint32 process, const RemoteRecord *record, const void *data)
{
    char name[32];
    mailbox_name(name, process);
    LxHostMailbox *mailbox;
    uint32 owner;
    size_t size = sizeof *record + record->size;
    uint8 *wire = size <= REMOTE_LIMIT ? malloc(size) : NULL;
    if (!wire)
        return FALSE;
    memcpy(wire, record, sizeof *record);
    if (record->size)
        memcpy(wire + sizeof *record, data, record->size);
    BOOL sent = !lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &mailbox, &owner);
    if (sent)
    {
        sent = !lx_host_mailbox_send(mailbox, wire, size, 0);
        lx_host_mailbox_close(mailbox);
    }
    free(wire);
    return sent;
}

void dde_notify_processes(ULONG type, ULONG skip, ULONG atom)
{
    uint32 processes[SERVICE_LIMIT];
    unsigned count = 0;
    if (!lock_directory())
        return;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        uint32 process = directory->entries[i].process;
        unsigned known = 0;
        while (known < count && processes[known] != process)
            known++;
        if (process && process != own_process && known == count)
            processes[count++] = process;
    }
    unlock_directory();
    RemoteRecord record = {.kind = REMOTE_NOTIFY, .type = type, .first = atom, .one = skip};
    for (unsigned i = 0; i < count; i++)
        remote_send(processes[i], &record, NULL);
}

/* Waits for the reply while serving this thread's own queued work. */
static RemoteReply *remote_wait(uint32 sequence, ULONG timeout)
{
    uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * 1000;
    for (;;)
    {
        open32DdeDeliver();
        lx_host_monitor_lock(dde_lock);
        for (RemoteReply **link = &remote_replies; *link; link = &(*link)->next)
            if ((*link)->record.sequence == sequence)
            {
                RemoteReply *reply = *link;
                *link = reply->next;
                lx_host_monitor_unlock(dde_lock);
                return reply;
            }
        if (lx_host_monotonic_us() >= deadline)
        {
            lx_host_monitor_unlock(dde_lock);
            return NULL;
        }
        lx_host_monitor_wait(dde_lock, 20);
        lx_host_monitor_unlock(dde_lock);
    }
}

static void free_reply(RemoteReply *reply)
{
    free(reply->data);
    free(reply);
}

static uint8 *copy_data(ULONG handle, uint32 *size)
{
    LxHandleBorrow borrow = {0};
    *size = 0;
    if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return NULL;
    DdeData *data = borrow.resource;
    uint8 *bytes = data->size ? malloc(data->size) : NULL;
    if (bytes)
    {
        memcpy(bytes, lx_execution_mapping_host(data->mapping), data->size);
        *size = data->size;
    }
    lx_handle_release(&borrow);
    return bytes;
}

BOOL dde_remote_call(DdeConversation *conversation, ULONG timeout, ULONG type, ULONG format,
                     ULONG first, ULONG second, ULONG data, ULONG one, ULONG two, ULONG *result)
{
    uint32 size;
    uint8 *bytes = copy_data(data, &size);
    uint32 sequence = __atomic_add_fetch(&remote_sequence, 1, __ATOMIC_RELAXED);
    RemoteRecord record = {.kind = REMOTE_CALL,
                           .sequence = sequence,
                           .process = own_process,
                           .conversation = conversation->remote_handle,
                           .type = type,
                           .format = format,
                           .first = first,
                           .second = second,
                           .one = one,
                           .two = two,
                           .size = size};
    BOOL sent = remote_send(conversation->remote_process, &record, bytes);
    free(bytes);
    RemoteReply *reply = sent ? remote_wait(sequence, timeout) : NULL;
    if (!reply)
        return FALSE;
    *result = reply->record.result;
    if (reply->record.size)
        *result =
            DdeCreateDataHandle(conversation->client ? conversation->client : conversation->server,
                                reply->data, reply->record.size, 0, second, format, 0);
    free_reply(reply);
    return TRUE;
}

ULONG dde_forward_transaction(const DdeRequest *request)
{
    LxHandleBorrow borrow = {0};
    ULONG result = 0;
    if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        dde_remote_call(borrow.resource, REMOTE_TIMEOUT, request->type, request->format,
                        request->first, request->second, request->data, 0, 0, &result);
        lx_handle_release(&borrow);
    }
    return result;
}

/* Runs on the owning thread of the local instance named by the request. */
void dde_serve_remote(DdeRequest *request)
{
    RemoteRecord reply = {
        .kind = REMOTE_REPLY, .sequence = request->remote_sequence, .process = own_process};
    uint8 *bytes = NULL;
    if (request->connect)
    {
        ULONG accepted = dde_run_callback(request);
        if (accepted)
        {
            ULONG handle =
                dde_make_conversation(0, request->instance, request->second, request->first,
                                      request->remote_process, request->remote_peer);
            reply.peer = handle;
            reply.result = handle != 0;
            request->type = 0x8072; /* XTYP_CONNECT_CONFIRM */
            request->conversation = handle;
            if (handle)
                dde_run_callback(request);
        }
    }
    else
    {
        if (request->payload_size)
            request->data =
                DdeCreateDataHandle(request->instance, request->payload, request->payload_size, 0,
                                    request->second, request->format, 0);
        ULONG result = dde_run_callback(request);
        LxHandleBorrow borrow = {0};
        if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        {
            DdeConversation *conversation = borrow.resource;
            if ((request->type & ~0xCu) == 0x1030)
            {
                conversation->item = request->second;
                conversation->format = request->format;
            }
            if ((request->type & ~0xCu) == 0x1030 && result)
                conversation->advise = TRUE;
            if (request->type == 0x8040)
                conversation->advise = FALSE;
            lx_handle_release(&borrow);
        }
        reply.result = result;
        if (request->type == 0x20B0 && result)
        {
            bytes = copy_data(result, &reply.size);
            DdeFreeDataHandle(result);
        }
        if (request->data)
            DdeFreeDataHandle(request->data);
        if (request->type == 0x80C2)
            lx_handle_close(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    }
    remote_send(request->remote_process, &reply, bytes);
    free(bytes);
}

static void queue_remote(DdeRequest *request)
{
    lx_host_monitor_lock(dde_lock);
    dde_queue_request(request);
    lx_host_monitor_unlock(dde_lock);
    open32PostThreadPacket(request->thread, OPEN32_DDE_PACKET);
}

static void receive_call(const RemoteRecord *record, const uint8 *data)
{
    LxHandleBorrow borrow = {0}, owner = {0};
    if (!lx_handle_borrow(record->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return;
    DdeConversation *conversation = borrow.resource;
    ULONG instance = conversation->client ? conversation->client : conversation->server;
    DdeRequest *request = calloc(1, sizeof *request);
    if (request && dde_borrow_any(instance, &owner))
    {
        *request = (DdeRequest){.thread = ((DdeInstance *)owner.resource)->thread,
                                .instance = instance,
                                .type = record->type,
                                .format = record->format,
                                .conversation = record->conversation,
                                .first = record->first,
                                .second = record->second,
                                .one = record->one,
                                .two = record->two,
                                .remote_process = record->process,
                                .remote_sequence = record->sequence,
                                .payload_size = record->size};
        lx_handle_release(&owner);
        request->payload = record->size ? malloc(record->size) : NULL;
        if (request->payload)
            memcpy(request->payload, data, record->size);
        if (record->size && !request->payload)
        {
            free(request);
            request = NULL;
        }
        if (request)
            queue_remote(request);
    }
    else
        free(request);
    lx_handle_release(&borrow);
}

static void receive_connect(const RemoteRecord *record)
{
    LxHandleBorrow owner = {0};
    DdeRequest *request = calloc(1, sizeof *request);
    if (!request || !dde_borrow_any(record->peer, &owner))
    {
        free(request);
        RemoteRecord refusal = {
            .kind = REMOTE_REPLY, .sequence = record->sequence, .process = own_process};
        remote_send(record->process, &refusal, NULL);
        return;
    }
    *request = (DdeRequest){.thread = ((DdeInstance *)owner.resource)->thread,
                            .instance = record->peer,
                            .type = 0x1062, /* XTYP_CONNECT */
                            .first = record->first,
                            .second = record->second,
                            .remote_process = record->process,
                            .remote_sequence = record->sequence,
                            .remote_peer = record->conversation,
                            .connect = TRUE};
    lx_handle_release(&owner);
    queue_remote(request);
}

static void receive_notify(const RemoteRecord *record)
{
    lx_host_monitor_lock(dde_lock);
    for (DdeInstance *peer = dde_instances; peer; peer = peer->next)
    {
        DdeRequest *request = peer->flags & record->one ? NULL : calloc(1, sizeof *request);
        if (!request)
            continue;
        request->thread = peer->thread;
        request->instance = peer->handle;
        request->type = record->type;
        request->first = record->first;
        dde_queue_request(request);
    }
    ULONG threads[64];
    unsigned count = 0;
    for (DdeInstance *peer = dde_instances; peer && count < 64; peer = peer->next)
        threads[count++] = peer->thread;
    lx_host_monitor_unlock(dde_lock);
    for (unsigned i = 0; i < count; i++)
        open32PostThreadPacket(threads[i], OPEN32_DDE_PACKET);
}

static void *remote_listener(void *argument)
{
    (void)argument;
    uint8 *wire = malloc(REMOTE_LIMIT);
    while (wire && !__atomic_load_n(&remote_stop, __ATOMIC_ACQUIRE))
    {
        size_t size = 0;
        unsigned priority;
        uint32 token = 0;
        if (lx_host_mailbox_receive(remote_inbox, wire, REMOTE_LIMIT, &size, &priority, &token, 0))
        {
            lx_host_sleep_ms(2);
            continue;
        }
        RemoteRecord record;
        if (size < sizeof record)
            continue;
        memcpy(&record, wire, sizeof record);
        const uint8 *data = wire + sizeof record;
        if (size != sizeof record + record.size)
            continue;
        if (record.kind == REMOTE_CALL)
            receive_call(&record, data);
        else if (record.kind == REMOTE_CONNECT)
            receive_connect(&record);
        else if (record.kind == REMOTE_NOTIFY)
            receive_notify(&record);
        else if (record.kind == REMOTE_REPLY)
        {
            RemoteReply *reply = calloc(1, sizeof *reply);
            if (!reply)
                continue;
            reply->record = record;
            reply->data = record.size ? malloc(record.size) : NULL;
            if (reply->data)
                memcpy(reply->data, data, record.size);
            lx_host_monitor_lock(dde_lock);
            reply->next = remote_replies;
            remote_replies = reply;
            lx_host_monitor_wake_all(dde_lock);
            lx_host_monitor_unlock(dde_lock);
        }
    }
    free(wire);
    return NULL;
}

void dde_start_listener(void)
{
    lx_host_once(&directory_once, initialize_directory);
    lx_host_monitor_lock(dde_lock);
    BOOL started = remote_inbox != NULL;
    lx_host_monitor_unlock(dde_lock);
    if (started)
        return;
    char name[32];
    uint32 owner;
    mailbox_name(name, own_process);
    if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 1, 0, &remote_inbox, &owner))
    {
        remote_inbox = NULL;
        return;
    }
    __atomic_store_n(&remote_stop, 0, __ATOMIC_RELEASE);
    remote_thread = lx_host_thread_create(remote_listener, NULL, NULL);
}

void dde_stop_listener(void)
{
    if (remote_thread)
    {
        __atomic_store_n(&remote_stop, 1, __ATOMIC_RELEASE);
        lx_host_thread_join(remote_thread, NULL);
        remote_thread = NULL;
    }
    if (remote_inbox)
    {
        lx_host_mailbox_close(remote_inbox);
        remote_inbox = NULL;
    }
}

/* The first live server in the directory that serves the name takes the conversation. */
ULONG dde_remote_connect(ULONG id, ULONG service, ULONG topic)
{
    ServiceEntry found = {0};
    if (!lock_directory())
        return 0;
    ServiceEntry snapshot[SERVICE_LIMIT];
    memcpy(snapshot, directory->entries, sizeof snapshot);
    unlock_directory();
    for (unsigned i = 0; i < SERVICE_LIMIT && !found.process; i++)
    {
        const ServiceEntry *entry = snapshot + i;
        if (entry->process && entry->process != own_process &&
            (!service || !entry->atom || entry->atom == service) && process_alive(entry->process))
            found = *entry;
    }
    if (!found.process)
        return 0;
    ULONG handle = dde_make_conversation(id, 0, service, topic, found.process, 0);
    if (!handle)
        return 0;
    uint32 sequence = __atomic_add_fetch(&remote_sequence, 1, __ATOMIC_RELAXED);
    RemoteRecord record = {.kind = REMOTE_CONNECT,
                           .sequence = sequence,
                           .process = own_process,
                           .conversation = handle,
                           .peer = found.instance,
                           .first = topic,
                           .second = service};
    RemoteReply *reply =
        remote_send(found.process, &record, NULL) ? remote_wait(sequence, REMOTE_TIMEOUT) : NULL;
    BOOL accepted = reply && reply->record.result;
    LxHandleBorrow borrow = {0};
    if (accepted && lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        ((DdeConversation *)borrow.resource)->remote_handle = reply->record.peer;
        lx_handle_release(&borrow);
    }
    if (reply)
        free_reply(reply);
    if (accepted)
        return handle;
    lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    return 0;
}
