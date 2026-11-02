/* Public domain. No rights reserved. Wholly AI-authored. */
/* Mux records retain their semaphore resources; event posts and mutex releases wake waits. */
#include "os2libs/doscalls/internal.h"
#define MUX_LIMIT 64u

typedef struct MuxMember
{
    SEMRECORD record;
    LxHandleBorrow borrow;
    LxHandleKind kind;
} MuxMember;
typedef struct MuxWait
{
    struct MuxWait *next;
    char *name;
    HMUX handle;
    ULONG flags, count, opens, waiters;
    BOOL closed;
    MuxMember members[MUX_LIMIT];
} MuxWait;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static MuxWait *muxes;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
static void lock_muxes(void)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
}
void notifyMuxWaiters(void)
{
    lock_muxes();
    lx_host_monitor_wake_all(monitor);
    lx_host_monitor_unlock(monitor);
}
static void destroy_mux(void *value)
{
    MuxWait *mux = value;
    for (ULONG i = 0; i < mux->count; i++)
        lx_handle_release(&mux->members[i].borrow);
    free(mux->name);
    free(mux);
}
static APIRET member_borrow(ULONG handle, MuxMember *member)
{
    memset(member, 0, sizeof *member);
    if (borrowEventResource(handle, &member->borrow))
        member->kind = LX_HANDLE_EVENT;
    else if (lx_handle_borrow(handle, LX_HANDLE_MUTEX, &member->borrow))
        member->kind = LX_HANDLE_MUTEX;
    else
        return ERROR_INVALID_HANDLE;
    return NO_ERROR;
}
static BOOL member_shared(const MuxMember *member)
{
    return member->kind == LX_HANDLE_EVENT
               ? ((EventSem *)member->borrow.resource)->shared != NULL ||
                     semaphoreResourceShared(member->borrow.resource)
               : semaphoreResourceShared(member->borrow.resource);
}
static APIRET add_member(MuxWait *mux, const SEMRECORD *record)
{
    if (mux->count == MUX_LIMIT)
        return ERROR_TOO_MANY_SEMAPHORES;
    MuxMember member;
    APIRET result = member_borrow(record->hsemCur, &member);
    if (result)
        return result;
    if ((mux->flags & 1) && !member_shared(&member))
        result = ERROR_WRONG_TYPE;
    if (mux->count && member_shared(&mux->members[0]) != member_shared(&member))
        result = ERROR_WRONG_TYPE;
    if (mux->count && mux->members[0].kind != member.kind)
        result = ERROR_WRONG_TYPE;
    for (ULONG i = 0; !result && i < mux->count; i++)
        if (mux->members[i].borrow.resource == member.borrow.resource)
            result = ERROR_DUPLICATE_HANDLE;
    if (result)
        lx_handle_release(&member.borrow);
    else
    {
        member.record = *record;
        mux->members[mux->count++] = member;
    }
    return result;
}
APIRET DosCreateMuxWaitSem(PSZ name, PHMUX output, ULONG count, PSEMRECORD records, ULONG flags)
{
    TRACE_NATIVE("DosCreateMuxWaitSem('%s', %p, %u, %p, %X)", name, output, count, records, flags);
    if (!output || count > MUX_LIMIT || (count && !records) || (flags & ~7u) ||
        ((flags & 6) != DCMW_WAIT_ANY && (flags & 6) != DCMW_WAIT_ALL))
        return ERROR_INVALID_PARAMETER;
    if (name && (strncasecmp(name, "\\SEM32\\", 7) || !name[7] || strlen(name) > 255))
        return ERROR_INVALID_NAME;
    MuxWait *mux = calloc(1, sizeof *mux);
    if (!mux)
        return ERROR_NOT_ENOUGH_MEMORY;
    mux->flags = flags | (name ? 1 : 0);
    mux->opens = 1;
    if (name && !(mux->name = strdup(name)))
    {
        free(mux);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    APIRET result = NO_ERROR;
    for (ULONG i = 0; !result && i < count; i++)
        result = add_member(mux, &records[i]);
    lock_muxes();
    for (MuxWait *m = muxes; !result && name && m; m = m->next)
        if (m->name && !strcasecmp(m->name, name))
            result = ERROR_DUPLICATE_NAME;
    if (!result)
    {
        mux->handle = lx_handle_create(LX_HANDLE_MUX, mux, destroy_mux);
        if (!mux->handle)
            result = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            mux->next = muxes;
            muxes = mux;
            *output = mux->handle;
        }
    }
    lx_host_monitor_unlock(monitor);
    if (result)
        destroy_mux(mux);
    return result;
}
APIRET DosOpenMuxWaitSem(PSZ name, PHMUX output)
{
    TRACE_NATIVE("DosOpenMuxWaitSem('%s', %p)", name, output);
    if (!output)
        return ERROR_INVALID_PARAMETER;
    lock_muxes();
    APIRET result = ERROR_SEM_NOT_FOUND;
    for (MuxWait *m = muxes; m; m = m->next)
        if (name ? m->name && !strcasecmp(m->name, name) : m->handle == *output)
        {
            if (m->opens == UINT32_MAX)
                result = ERROR_TOO_MANY_OPENS;
            else
            {
                m->opens++;
                *output = m->handle;
                result = NO_ERROR;
            }
            break;
        }
    lx_host_monitor_unlock(monitor);
    return result;
}
APIRET DosCloseMuxWaitSem(HMUX handle)
{
    TRACE_NATIVE("DosCloseMuxWaitSem(%u)", handle);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUX, &borrow))
        return ERROR_INVALID_HANDLE;
    MuxWait *mux = borrow.resource;
    lock_muxes();
    APIRET result = NO_ERROR;
    BOOL close = FALSE;
    if (mux->closed)
        result = ERROR_INVALID_HANDLE;
    else if (mux->opens > 1)
        mux->opens--;
    else if (mux->waiters)
        result = ERROR_SEM_BUSY;
    else
    {
        mux->closed = TRUE;
        MuxWait **link = &muxes;
        while (*link != mux)
            link = &(*link)->next;
        *link = mux->next;
        close = TRUE;
    }
    lx_host_monitor_unlock(monitor);
    if (close)
        lx_handle_close(handle, LX_HANDLE_MUX);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosAddMuxWaitSem(HMUX handle, PSEMRECORD record)
{
    TRACE_NATIVE("DosAddMuxWaitSem(%u, %p)", handle, record);
    if (!record)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUX, &borrow))
        return ERROR_INVALID_HANDLE;
    MuxWait *mux = borrow.resource;
    lock_muxes();
    APIRET result = mux->closed ? ERROR_INVALID_HANDLE : add_member(mux, record);
    if (!result)
        lx_host_monitor_wake_all(monitor);
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosDeleteMuxWaitSem(HMUX handle, ULONG semaphore)
{
    TRACE_NATIVE("DosDeleteMuxWaitSem(%u, %u)", handle, semaphore);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUX, &borrow))
        return ERROR_INVALID_HANDLE;
    MuxMember input;
    APIRET result = member_borrow(semaphore, &input);
    if (result)
    {
        lx_handle_release(&borrow);
        return result;
    }
    MuxWait *mux = borrow.resource;
    lock_muxes();
    result = mux->closed ? ERROR_INVALID_HANDLE : ERROR_SEM_NOT_FOUND;
    for (ULONG i = 0; !mux->closed && i < mux->count; i++)
        if (mux->members[i].borrow.resource == input.borrow.resource)
        {
            lx_handle_release(&mux->members[i].borrow);
            memmove(&mux->members[i], &mux->members[i + 1],
                    (mux->count - i - 1) * sizeof(MuxMember));
            mux->count--;
            lx_host_monitor_wake_all(monitor);
            result = NO_ERROR;
            break;
        }
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&input.borrow);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosQueryMuxWaitSem(HMUX handle, PULONG count, PSEMRECORD records, PULONG flags)
{
    TRACE_NATIVE("DosQueryMuxWaitSem(%u, %p, %p, %p)", handle, count, records, flags);
    if (!count || !flags)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUX, &borrow))
        return ERROR_INVALID_HANDLE;
    MuxWait *mux = borrow.resource;
    lock_muxes();
    APIRET result = mux->closed ? ERROR_INVALID_HANDLE : NO_ERROR;
    if (!result)
    {
        ULONG capacity = *count;
        *count = mux->count;
        if (capacity < mux->count)
            result = ERROR_PARAM_TOO_SMALL;
        else if (mux->count && !records)
            result = ERROR_INVALID_PARAMETER;
        else
        {
            *flags = mux->flags;
            for (ULONG i = 0; i < mux->count; i++)
                records[i] = mux->members[i].record;
        }
    }
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&borrow);
    return result;
}
static APIRET try_member(MuxMember *member)
{
    if (member->kind == LX_HANDLE_MUTEX)
        return DosRequestMutexSemResource(member->record.hsemCur, 0, member->borrow.resource);
    ULONG posts = 0;
    APIRET result =
        DosQueryEventSemResource(member->record.hsemCur, &posts, member->borrow.resource);
    return result ? result : posts ? NO_ERROR : ERROR_TIMEOUT;
}
static APIRET try_mux(MuxWait *mux, PULONG user)
{
    APIRET result = ERROR_TIMEOUT;
    if ((mux->flags & 6) == DCMW_WAIT_ANY)
    {
        for (ULONG i = 0; i < mux->count; i++)
        {
            result = try_member(&mux->members[i]);
            if (result != ERROR_TIMEOUT)
            {
                if (!result || result == ERROR_SEM_OWNER_DIED)
                    *user = mux->members[i].record.ulUser;
                return result;
            }
        }
        return result;
    }
    ULONG acquired = 0;
    APIRET abandoned = NO_ERROR;
    for (; acquired < mux->count; acquired++)
    {
        result = try_member(&mux->members[acquired]);
        if (result == ERROR_SEM_OWNER_DIED)
            abandoned = result;
        else if (result)
            break;
    }
    if (acquired == mux->count)
    {
        *user = mux->members[mux->count - 1].record.ulUser;
        return abandoned;
    }
    if (mux->members[0].kind == LX_HANDLE_MUTEX)
        for (ULONG i = 0; i < acquired; i++)
            lx_host_mutex_unlock(mux->members[i].borrow.resource);
    return result;
}

typedef struct MuxWaitCall
{
    LxHandleBorrow borrow;
    ULONG timeout;
    PULONG user;
    BOOL registered;
} MuxWaitCall;
static void wait_cleanup(void *argument)
{
    MuxWaitCall *call = argument;
    if (call->registered)
    {
        lock_muxes();
        ((MuxWait *)call->borrow.resource)->waiters--;
        lx_host_monitor_unlock(monitor);
    }
    lx_handle_release(&call->borrow);
}
static uint32 wait_operation(void *argument)
{
    MuxWaitCall *call = argument;
    MuxWait *mux = call->borrow.resource;
    ULONG timeout = call->timeout;
    const uint64 deadline =
        timeout == UINT32_MAX ? UINT64_MAX : lx_host_monotonic_us() + (uint64)timeout * 1000;
    lock_muxes();
    mux->waiters++;
    call->registered = TRUE;
    APIRET result;
    for (;;)
    {
        if (mux->closed)
        {
            result = ERROR_INVALID_HANDLE;
            break;
        }
        if (!mux->count)
        {
            result = ERROR_EMPTY_MUXWAIT;
            break;
        }
        result = try_mux(mux, call->user);
        if (result != ERROR_TIMEOUT)
            break;
        uint64 now = lx_host_monotonic_us();
        if (now >= deadline)
            break;
        ULONG remaining =
            deadline == UINT64_MAX ? UINT32_MAX : (ULONG)((deadline - now + 999) / 1000);
        if (mux->count && member_shared(&mux->members[0]) && remaining > 10)
            remaining = 10;
        LxHostLockResult waited = lx_host_monitor_wait(monitor, remaining);
        if (waited == LX_HOST_LOCK_TIMEOUT)
        {
            if (lx_host_monotonic_us() >= deadline)
                break;
            continue;
        }
        if (waited != LX_HOST_LOCK_OK)
        {
            result = ERROR_INTERRUPT;
            break;
        }
    }
    mux->waiters--;
    call->registered = FALSE;
    lx_host_monitor_unlock(monitor);
    return result;
}
APIRET DosWaitMuxWaitSem(HMUX handle, ULONG timeout, PULONG user)
{
    TRACE_NATIVE("DosWaitMuxWaitSem(%u, %u, %p)", handle, timeout, user);
    if (!user)
        return ERROR_INVALID_PARAMETER;
    MuxWaitCall call = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_MUX, &call.borrow))
        return ERROR_INVALID_HANDLE;
    call.timeout = timeout;
    call.user = user;
    return lx_host_guarded_call(wait_operation, &call, wait_cleanup, &call);
}
