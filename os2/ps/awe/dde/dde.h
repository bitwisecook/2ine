/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDEML state shared by the instance, string, data, callback, conversation and remote units. */
#ifndef TWOINE_PMWINX_DDE_H
#define TWOINE_PMWINX_DDE_H
#include "os2/ps/awe/internal.h"
#include "emulator/abi.h"
#include "emulator/handles.h"
#include "host/sync.h"

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
    ULONG handle, callback, flags, error, thread;
} DdeInstance;

extern LxHostMonitor *dde_lock;

extern DdeInstance *dde_instances;

LX_PRIVATE BOOL dde_borrow_instance(ULONG handle, LxHandleBorrow *borrow);

/* Data and conversations are typed resources. Guest callbacks use an x86
   argument thunk, keeping all eight callback words out of the host C ABI. */
typedef struct DdeData
{
    LxGuestMapping *mapping;
    ULONG owner, size, item, format, flags, accesses, handle;
    struct DdeData *next;
} DdeData;

extern DdeData *dde_data;

/* A conversation with another process names only its local side, in client or server;
   remote_handle is the peer's conversation in remote_process. */
typedef struct DdeConversation
{
    ULONG handle, client, server, service, topic, user, item, format, type;
    ULONG remote_process, remote_handle;
    BOOL advise;
    struct DdeConversation *next;
} DdeConversation;

extern DdeConversation *dde_conversations;

LX_PRIVATE ULONG dde_callback(DdeInstance *instance, ULONG type, ULONG format, ULONG conversation,
                              ULONG first, ULONG second, ULONG data, ULONG one, ULONG two);

/* One unit of work for an instance's owning thread. A synchronous sender waits for done;
   an asynchronous one is answered with XTYP_XACT_COMPLETE queued to the client thread. */
typedef struct DdeRequest
{
    struct DdeRequest *next;
    ULONG thread, instance, type, format, conversation, first, second, data, one, two;
    ULONG result, transaction, client, release_data, user;
    ULONG remote_process, remote_sequence, remote_peer, payload_size;
    uint8 *payload;
    BOOL asynchronous, waiting, done, abandoned, connect, forward;
} DdeRequest;

extern DdeRequest *dde_requests;

extern _Thread_local DdeRequest *dde_active;

extern ULONG dde_next_transaction;

LX_PRIVATE BOOL dde_borrow_any(ULONG handle, LxHandleBorrow *borrow);

LX_PRIVATE void dde_queue_request(DdeRequest *request);

LX_PRIVATE void dde_forget_request(DdeRequest *request);

LX_PRIVATE ULONG dde_run_callback(const DdeRequest *request);

LX_PRIVATE BOOL dde_invoke(ULONG instance, ULONG timeout, ULONG type, ULONG format,
                           ULONG conversation, ULONG first, ULONG second, ULONG data, ULONG one,
                           ULONG two, ULONG *result);

LX_PRIVATE void dde_notify_peers(DdeInstance *instance, ULONG type, ULONG skip, ULONG atom);

LX_PRIVATE ULONG dde_make_conversation(ULONG client, ULONG server, ULONG service, ULONG topic,
                                       ULONG process, ULONG peer);

LX_PRIVATE void dde_complete_transaction(DdeRequest *request);

/* Other processes of the ball. The directory lists who serves what, each process reads a
   named mailbox on a host thread, and callbacks still run on the instance's owning thread.
   Data travels by value in the record, so data handles stay local to each process. */
enum
{
    REMOTE_CALL = 1,
    REMOTE_REPLY,
    REMOTE_CONNECT,
    REMOTE_NOTIFY,
    REMOTE_LIMIT = 4 * 1024 * 1024 - 256,
    SERVICE_LIMIT = 256,
    REMOTE_TIMEOUT = 10000,
    /* Milliseconds a local server has to answer XTYP_CONNECT and XTYP_CONNECT_CONFIRM. */
    CONNECT_TIMEOUT = 10000,
    /* A waiting sender re-checks for its answer, and serves its own queue, this often. */
    DDE_POLL_MS = 20,
    US_PER_MS = 1000,
    /* XTYPF_NODATA and XTYPF_ACKREQ may accompany XTYP_ADVSTART. */
    ADVISE_START_MODIFIERS = WIN32_XTYPF_NODATA | WIN32_XTYPF_ACKREQ
};

LX_PRIVATE void dde_publish_service(ULONG instance, ULONG atom);

LX_PRIVATE void dde_withdraw_service(ULONG instance, ULONG atom, BOOL all);

LX_PRIVATE void dde_notify_processes(ULONG type, ULONG skip, ULONG atom);

LX_PRIVATE BOOL dde_remote_call(DdeConversation *conversation, ULONG timeout, ULONG type,
                                ULONG format, ULONG first, ULONG second, ULONG data, ULONG one,
                                ULONG two, ULONG *result);

LX_PRIVATE ULONG dde_forward_transaction(const DdeRequest *request);

LX_PRIVATE void dde_serve_remote(DdeRequest *request);

LX_PRIVATE void dde_start_listener(void);

LX_PRIVATE void dde_stop_listener(void);

LX_PRIVATE ULONG dde_remote_connect(ULONG id, ULONG service, ULONG topic);
#endif
