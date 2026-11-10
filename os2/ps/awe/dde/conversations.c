/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE conversations and transactions: connect, disconnect, client transactions, advise loops and
   conversation queries. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "os2/ps/awe/dde/dde.h"

enum
{
    /* MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) */
    ENGLISH_US_LANGID = 0x0409
};

static void destroy_conversation(void *resource)
{
    DdeConversation *conversation = resource;
    lx_host_monitor_lock(dde_lock);
    DdeConversation **link = &dde_conversations;
    while (*link && *link != conversation)
        link = &(*link)->next;
    if (*link)
        *link = conversation->next;
    lx_host_monitor_unlock(dde_lock);
    free(conversation);
}

/* Calls the far side of a conversation, in this process or another. */
static BOOL conversation_invoke(DdeConversation *conversation, ULONG peer, ULONG timeout,
                                ULONG type, ULONG format, ULONG first, ULONG second, ULONG data,
                                ULONG one, ULONG two, ULONG *result)
{
    if (conversation->remote_process)
        return dde_remote_call(conversation, timeout, type, format, first, second, data, one, two,
                               result);
    return dde_invoke(peer, timeout, type, format, conversation->handle, first, second, data, one,
                      two, result);
}

ULONG dde_make_conversation(ULONG client, ULONG server, ULONG service, ULONG topic, ULONG process,
                            ULONG peer)
{
    DdeConversation *conversation = calloc(1, sizeof *conversation);
    if (!conversation)
        return 0;
    conversation->client = client;
    conversation->server = server;
    conversation->service = service;
    conversation->topic = topic;
    conversation->remote_process = process;
    conversation->remote_handle = peer;
    ULONG handle =
        lx_handle_create(LX_HANDLE_OPEN32_DDE_CONVERSATION, conversation, destroy_conversation);
    if (!handle)
    {
        free(conversation);
        return 0;
    }
    conversation->handle = handle;
    lx_host_monitor_lock(dde_lock);
    conversation->next = dde_conversations;
    dde_conversations = conversation;
    lx_host_monitor_unlock(dde_lock);
    return handle;
}

ULONG DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context)
{
    LxHandleBorrow client = {0};
    if (!dde_borrow_instance(id, &client))
        return 0;
    DdeInstance *instance = client.resource;
    ULONG server_handle = 0;
    if (context && lx_guest_load_u32(context) != WIN32_CONVCONTEXT_SIZE)
    {
        instance->error = WIN32_DMLERR_INVALIDPARAMETER;
        lx_handle_release(&client);
        return 0;
    }
    lx_host_monitor_lock(dde_lock);
    for (DdeInstance *peer = dde_instances; peer; peer = peer->next)
    {
        if (peer == instance || (peer->flags & WIN32_APPCMD_CLIENTONLY))
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
    lx_host_monitor_unlock(dde_lock);
    ULONG handle = 0, accepted = 0;
    if (!server_handle)
    {
        handle = dde_remote_connect(id, service, topic);
        if (!handle)
            instance->error = WIN32_DMLERR_NO_CONV_ESTABLISHED;
        goto connect_done;
    }
    if (!dde_invoke(server_handle, CONNECT_TIMEOUT, WIN32_XTYP_CONNECT, 0, 0, topic, service, 0, 0,
                    id == server_handle, &accepted) ||
        !accepted)
    {
        instance->error = WIN32_DMLERR_NO_CONV_ESTABLISHED;
        goto connect_done;
    }
    DdeConversation *conversation = calloc(1, sizeof *conversation);
    if (!conversation)
    {
        instance->error = WIN32_DMLERR_LOW_MEMORY;
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
        instance->error = WIN32_DMLERR_LOW_MEMORY;
        goto connect_done;
    }
    conversation->handle = handle;
    lx_host_monitor_lock(dde_lock);
    conversation->next = dde_conversations;
    dde_conversations = conversation;
    lx_host_monitor_unlock(dde_lock);
    ULONG ignored;
    dde_invoke(server_handle, CONNECT_TIMEOUT, WIN32_XTYP_CONNECT_CONFIRM, 0, handle, topic,
               service, 0, 0, 0, &ignored);
connect_done:
    lx_handle_release(&client);
    return handle;
}

BOOL DdeDisconnect(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return FALSE;
    DdeConversation *conversation = borrow.resource;
    ULONG ignored;
    conversation_invoke(conversation,
                        conversation->server ? conversation->server : conversation->client,
                        REMOTE_TIMEOUT, WIN32_XTYP_DISCONNECT, 0, 0, 0, 0, 0, 0, &ignored);
    BOOL result = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    lx_handle_release(&borrow);
    return result;
}

static void note_transaction(DdeConversation *conversation, ULONG type, ULONG result)
{
    if ((type & ~ADVISE_START_MODIFIERS) == WIN32_XTYP_ADVSTART && result)
        conversation->advise = TRUE;
    if (type == WIN32_XTYP_ADVSTOP)
        conversation->advise = FALSE;
}

/* Poke and execute answer with DDE_FACK or DDE_FBUSY; the others answer with a value. */
static ULONG transaction_status(ULONG type, ULONG result)
{
    if (type == WIN32_XTYP_POKE || type == WIN32_XTYP_EXECUTE)
        return result & WIN32_DDE_FBUSY  ? WIN32_DDE_FBUSY
               : result & WIN32_DDE_FACK ? WIN32_DDE_FACK
                                         : 0;
    return result ? WIN32_DDE_FACK : 0;
}

void dde_complete_transaction(DdeRequest *request)
{
    LxHandleBorrow borrow = {0};
    if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        note_transaction(borrow.resource, request->type, request->result);
        lx_handle_release(&borrow);
    }
    if (request->release_data)
        DdeFreeDataHandle(request->release_data);
    DdeRequest *reply = calloc(1, sizeof *reply);
    LxHandleBorrow client = {0};
    if (!reply || !dde_borrow_any(request->client, &client))
    {
        free(reply);
        return;
    }
    ULONG thread = ((DdeInstance *)client.resource)->thread;
    lx_handle_release(&client);
    ULONG status = transaction_status(request->type, request->result);
    *reply =
        (DdeRequest){.thread = thread,
                     .instance = request->client,
                     .type = WIN32_XTYP_XACT_COMPLETE,
                     .format = request->format,
                     .conversation = request->conversation,
                     .first = request->first,
                     .second = request->second,
                     .data = request->type == WIN32_XTYP_REQUEST ? request->result : status != 0,
                     .one = request->transaction,
                     .two = status,
                     .transaction = request->transaction,
                     .user = request->user};
    lx_host_monitor_lock(dde_lock);
    dde_queue_request(reply);
    lx_host_monitor_unlock(dde_lock);
    open32PostThreadPacket(thread, OPEN32_DDE_PACKET);
}

static BOOL queue_transaction(DdeConversation *conversation, ULONG type, ULONG format, ULONG item,
                              ULONG data, ULONG temporary, PVOID status)
{
    /* A remote partner is reached from the client's own thread, on its next delivery. */
    BOOL forward = conversation->remote_process != 0;
    LxHandleBorrow server = {0};
    DdeRequest *request = calloc(1, sizeof *request);
    if (!request || !dde_borrow_any(forward ? conversation->client : conversation->server, &server))
    {
        free(request);
        return FALSE;
    }
    ULONG thread = ((DdeInstance *)server.resource)->thread;
    lx_handle_release(&server);
    lx_host_monitor_lock(dde_lock);
    ULONG transaction = ++dde_next_transaction;
    *request = (DdeRequest){.thread = thread,
                            .instance = conversation->server,
                            .type = type,
                            .format = format,
                            .conversation = conversation->handle,
                            .first = conversation->topic,
                            .second = item,
                            .data = data,
                            .transaction = transaction,
                            .client = conversation->client,
                            .release_data = temporary,
                            .asynchronous = TRUE,
                            .forward = forward};
    dde_queue_request(request);
    lx_host_monitor_unlock(dde_lock);
    if (!open32PostThreadPacket(thread, OPEN32_DDE_PACKET))
    {
        lx_host_monitor_lock(dde_lock);
        dde_forget_request(request);
        lx_host_monitor_unlock(dde_lock);
        free(request);
        return FALSE;
    }
    if (status)
        lx_guest_store_u32(status, transaction);
    return TRUE;
}

ULONG DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item, ULONG format,
                           ULONG type, ULONG timeout, PVOID status)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    LxHandleBorrow client = {0};
    ULONG result = 0, data = 0;
    BOOL temporary = FALSE;
    if (!dde_borrow_instance(conversation->client, &client))
        goto transaction_done;
    if (type != WIN32_XTYP_REQUEST && type != WIN32_XTYP_POKE && type != WIN32_XTYP_EXECUTE &&
        (type & ~ADVISE_START_MODIFIERS) != WIN32_XTYP_ADVSTART && type != WIN32_XTYP_ADVSTOP)
    {
        ((DdeInstance *)client.resource)->error = WIN32_DMLERR_INVALIDPARAMETER;
        goto transaction_done;
    }
    if (size == UINT32_MAX)
        data = address;
    else if (size)
    {
        LxGuestBorrow input = {0};
        if (!lx_execution_borrow(GProcessState.execution, address, size, LX_GUEST_READ, &input))
        {
            ((DdeInstance *)client.resource)->error = WIN32_DMLERR_INVALIDPARAMETER;
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
    if (timeout == WIN32_TIMEOUT_ASYNC)
    {
        result =
            queue_transaction(conversation, type, format, item, data, temporary ? data : 0, status);
        if (result)
            temporary = FALSE;
        else
            ((DdeInstance *)client.resource)->error = WIN32_DMLERR_POSTMSG_FAILED;
        goto transaction_done;
    }
    ULONG answer = 0;
    if (!conversation_invoke(conversation, conversation->server, timeout, type, format,
                             conversation->topic, item, data, 0, 0, &answer))
    {
        ((DdeInstance *)client.resource)->error =
            type == WIN32_XTYP_REQUEST   ? WIN32_DMLERR_DATAACKTIMEOUT
            : type == WIN32_XTYP_POKE    ? WIN32_DMLERR_POKEACKTIMEOUT
            : type == WIN32_XTYP_EXECUTE ? WIN32_DMLERR_EXECACKTIMEOUT
                                         : WIN32_DMLERR_ADVACKTIMEOUT;
        goto transaction_done;
    }
    note_transaction(conversation, type, answer);
    if (type == WIN32_XTYP_ADVSTOP)
        answer = 1;
    ULONG acknowledged = transaction_status(type, answer);
    if (status)
        lx_guest_store_u32(status, acknowledged);
    if (!acknowledged)
        ((DdeInstance *)client.resource)->error = WIN32_DMLERR_NOTPROCESSED;
    result = type == WIN32_XTYP_REQUEST ? answer : (acknowledged & WIN32_DDE_FACK) != 0;
transaction_done:
    if (temporary && data)
        DdeFreeDataHandle(data);
    lx_handle_release(&client);
    lx_handle_release(&borrow);
    return result;
}

/* The asynchronous transaction with this id, pending or now reporting its completion. */
static DdeRequest *find_transaction(ULONG handle, ULONG transaction)
{
    if (dde_active && dde_active->transaction == transaction && dde_active->conversation == handle)
        return dde_active;
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->transaction == transaction && request->conversation == handle)
            return request;
    return NULL;
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
    {
        lx_host_monitor_lock(dde_lock);
        DdeRequest *request = find_transaction(handle, transaction);
        if (request)
            request->user = user;
        valid = request != NULL;
        lx_host_monitor_unlock(dde_lock);
        if (!valid)
            open32DdeSetError(conversation->client, WIN32_DMLERR_INVALIDPARAMETER);
    }
    lx_handle_release(&borrow);
    return valid;
}

ULONG DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!output || lx_guest_load_u32(output) != WIN32_CONVINFO_SIZE ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    ULONG user = conversation->user;
    if (transaction != UINT32_MAX && transaction)
    {
        lx_host_monitor_lock(dde_lock);
        DdeRequest *request = find_transaction(handle, transaction);
        BOOL pending = request != NULL;
        if (request)
            user = request->user;
        lx_host_monitor_unlock(dde_lock);
        if (!pending)
        {
            open32DdeSetError(conversation->client, WIN32_DMLERR_INVALIDPARAMETER);
            lx_handle_release(&borrow);
            return 0;
        }
    }
    memset(output, 0, WIN32_CONVINFO_SIZE);
    /* CONVINFO, field by field, with its embedded CONVCONTEXT from the tenth word on. */
    ULONG fields[] = {WIN32_CONVINFO_SIZE,
                      user,
                      handle,
                      conversation->service,
                      conversation->service,
                      conversation->topic,
                      conversation->item,
                      conversation->format,
                      conversation->type,
                      WIN32_ST_CLIENT | WIN32_ST_CONNECTED,
                      WIN32_XST_CONNECTED,
                      0,
                      0,
                      WIN32_CONVCONTEXT_SIZE,
                      0,
                      0,
                      WIN32_CP_WINANSI,
                      ENGLISH_US_LANGID,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    lx_handle_release(&borrow);
    return WIN32_CONVINFO_SIZE;
}

BOOL DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction)
{
    LxHandleBorrow instance = {0};
    if (!dde_borrow_instance(id, &instance))
        return FALSE;
    BOOL valid = !transaction;
    if (handle)
    {
        LxHandleBorrow conversation = {0};
        BOOL owned = lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &conversation) &&
                     ((DdeConversation *)conversation.resource)->client == id;
        lx_handle_release(&conversation);
        if (!owned)
            transaction = 0, valid = FALSE;
    }
    /* A zero transaction abandons everything on the conversation, or on the instance. */
    lx_host_monitor_lock(dde_lock);
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->asynchronous && request->client == id &&
            (!handle || request->conversation == handle) &&
            (!transaction || request->transaction == transaction))
        {
            request->abandoned = TRUE;
            valid = TRUE;
        }
    lx_host_monitor_unlock(dde_lock);
    if (!valid)
        ((DdeInstance *)instance.resource)->error = WIN32_DMLERR_INVALIDPARAMETER;
    lx_handle_release(&instance);
    return valid;
}

BOOL DdePostAdvise(ULONG id, ULONG topic, ULONG item)
{
    LxHandleBorrow server = {0};
    if (!dde_borrow_instance(id, &server))
        return FALSE;
    ULONG *handles = NULL;
    size_t count = 0;
    lx_host_monitor_lock(dde_lock);
    for (DdeConversation *conversation = dde_conversations; conversation;
         conversation = conversation->next)
        if (conversation->server == id && conversation->advise &&
            (!topic || topic == conversation->topic) && (!item || item == conversation->item))
        {
            ULONG *next = realloc(handles, (count + 1) * sizeof *next);
            if (!next)
            {
                free(handles);
                lx_host_monitor_unlock(dde_lock);
                lx_handle_release(&server);
                return FALSE;
            }
            handles = next;
            handles[count++] = conversation->handle;
        }
    lx_host_monitor_unlock(dde_lock);
    for (size_t i = 0; i < count; i++)
    {
        LxHandleBorrow borrow = {0};
        if (!lx_handle_borrow(handles[i], LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
            continue;
        DdeConversation *conversation = borrow.resource;
        ULONG ignored;
        ULONG data =
            dde_callback(server.resource, WIN32_XTYP_ADVREQ, conversation->format,
                         conversation->handle, conversation->topic, conversation->item, 0, 0, 0);
        conversation_invoke(conversation, conversation->client, REMOTE_TIMEOUT, WIN32_XTYP_ADVDATA,
                            conversation->format, conversation->topic, conversation->item, data, 0,
                            0, &ignored);
        if (data)
            DdeFreeDataHandle(data);
        lx_handle_release(&borrow);
    }
    free(handles);
    lx_handle_release(&server);
    return TRUE;
}
