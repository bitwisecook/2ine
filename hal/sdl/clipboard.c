/* Public domain. No rights reserved. Wholly AI-authored. */
/* SDL clipboard operations are marshalled with bounded worker waits. */
#include "hal/clipboard.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#define CLIP_LIMIT (64u * 1024u * 1024u)

typedef struct ClipboardOffer
{
    LxHostClipboardItem *items;
    size_t count;
    LxHostClipboardProvider provider;
    void *context;
} ClipboardOffer;

static ClipboardOffer *current;
static SDL_AtomicInt revision, owned;
static int watching;

static const char *native_mime(const char *mime)
{
    /* Cocoa's single-UTF8-type fast path assumes SDL_SetClipboardText's
       userdata is a char string. A generic offer must use the general path. */
    if (!strcmp(mime, "text/plain;charset=utf-8"))
        return "text/plain";
    if (!strcmp(mime, "text/rtf"))
    {
        const char *driver = SDL_GetCurrentVideoDriver();
        if (driver && !strcmp(driver, "cocoa"))
            return "application/rtf";
    }
    return mime;
}

static int same_mime(const char *first, const char *second)
{
    if ((!strcmp(first, "text/rtf") || !strcmp(first, "application/rtf")) &&
        (!strcmp(second, "text/rtf") || !strcmp(second, "application/rtf")))
        return 1;
    return !strcmp(native_mime(first), native_mime(second));
}

static bool watch_clipboard(void *context, SDL_Event *event)
{
    (void)context;
    if (event->type == SDL_EVENT_CLIPBOARD_UPDATE)
    {
        SDL_AddAtomicInt(&revision, 1);
        if (!event->clipboard.owner)
            SDL_SetAtomicInt(&owned, 0);
    }
    return true;
}

static void free_offer(void *context)
{
    ClipboardOffer *offer = context;
    if (current == offer)
    {
        current = NULL;
        SDL_SetAtomicInt(&owned, 0);
        SDL_AddAtomicInt(&revision, 1);
    }
    for (size_t i = 0; i < offer->count; i++)
    {
        free((void *)offer->items[i].mime);
        free((void *)offer->items[i].data);
    }
    free(offer->items);
    free(offer);
}

static const void *read_offer(void *context, const char *mime, size_t *size)
{
    ClipboardOffer *offer = context;
    *size = 0;
    if (!mime)
        return NULL;
    for (size_t i = 0; i < offer->count; i++)
    {
        LxHostClipboardItem *item = &offer->items[i];
        if (!same_mime(mime, item->mime))
            continue;
        if (!item->data && offer->provider)
        {
            size_t length = 0;
            void *bytes = offer->provider(offer->context, item->mime, &length);
            size_t total = 0;
            for (size_t n = 0; n < offer->count; n++)
                total += offer->items[n].size;
            if (bytes && length <= CLIP_LIMIT - total)
            {
                item->data = bytes;
                item->size = length;
            }
            else
                free(bytes);
        }
        *size = item->size;
        return item->data;
    }
    return NULL;
}

#include "hal/sdl/clipboard_native.h"

typedef struct ClipboardCall
{
    SDL_AtomicInt references, status;
    SDL_Semaphore *complete;
    unsigned action;
    char *mime;
    ClipboardOffer *offer;
    void *bytes;
    size_t size;
    LxHostClipboardState state;
    int result;
} ClipboardCall;

static void release_call(ClipboardCall *call)
{
    if (SDL_AddAtomicInt(&call->references, -1) != 1)
        return;
    if (call->offer)
        free_offer(call->offer);
    free(call->mime);
    free(call->bytes);
    SDL_DestroySemaphore(call->complete);
    free(call);
}

static void run_call(void *context)
{
    ClipboardCall *call = context;
    if (!SDL_CompareAndSwapAtomicInt(&call->status, 0, 1))
    {
        release_call(call);
        return;
    }
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
        goto done;
    if (!watching)
    {
        watching = SDL_AddEventWatch(watch_clipboard, NULL);
        if (!watching)
            goto done;
    }
    SDL_PumpEvents();
    if (call->action == 0)
        call->result = 1;
    else if (call->action == 1)
    {
        if (lx_clipboard_native_format(call->mime))
            call->result = lx_clipboard_native_has(call->mime);
        else
            call->result =
                SDL_HasClipboardData(native_mime(call->mime)) || SDL_HasClipboardData(call->mime);
    }
    else if (call->action == 2)
    {
        size_t size = 0;
        void *bytes;
        int native = lx_clipboard_native_format(call->mime);
        if (native)
            bytes = lx_clipboard_native_read(call->mime, &size);
        else
            bytes = SDL_GetClipboardData(native_mime(call->mime), &size);
        if (!bytes && !native && strcmp(native_mime(call->mime), call->mime))
            bytes = SDL_GetClipboardData(call->mime, &size);
        if (bytes && size <= CLIP_LIMIT)
        {
            call->bytes = malloc(size + 1);
            if (call->bytes)
            {
                memcpy(call->bytes, bytes, size);
                ((char *)call->bytes)[size] = 0;
                call->size = size;
                call->result = 1;
            }
        }
        if (native)
            free(bytes);
        else
            SDL_free(bytes);
    }
    else if (call->action == 3)
    {
        const char *mimes[128];
        size_t native_count = 0;
        for (size_t i = 0; i < call->offer->count; i++)
        {
            const char *mime = call->offer->items[i].mime;
            const char *aliases[2] = {mime, same_mime(mime, "text/rtf") ? "application/rtf" : NULL};
            for (unsigned n = 0; n < 2 && aliases[n]; n++)
            {
                const char *name = lx_clipboard_native_offer_mime(native_mime(aliases[n]));
                size_t at = 0;
                while (at < native_count && strcmp(mimes[at], name))
                    at++;
                if (at == native_count)
                    mimes[native_count++] = name;
            }
        }
        if (call->offer->count)
        {
            ClipboardOffer *offer = call->offer;
            /* SDL adopts callbacks before invoking its native backend, even
               when that backend subsequently reports a publication failure. */
            call->offer = NULL;
            current = offer;
            SDL_SetAtomicInt(&owned, 1);
            call->result = SDL_SetClipboardData(read_offer, free_offer, offer, mimes, native_count);
            if (call->result)
                call->result =
                    lx_clipboard_native_publish(offer, read_offer, offer->items, offer->count);
        }
        else
        {
            call->result = SDL_ClearClipboardData();
            current = NULL;
            SDL_SetAtomicInt(&owned, call->result ? 1 : 0);
        }
        if (call->result)
            SDL_AddAtomicInt(&revision, 1);
    }
    else if (call->action == 4)
    {
        ClipboardOffer *offer = SDL_GetAtomicInt(&owned) ? current : NULL;
        if (offer)
        {
            for (size_t i = 0; i < offer->count; i++)
            {
                size_t ignored;
                void *bytes = SDL_GetClipboardData(native_mime(offer->items[i].mime), &ignored);
                SDL_free(bytes);
            }
            offer->provider = NULL;
            offer->context = NULL;
        }
        call->result = 1;
    }
    call->state.revision = (unsigned)SDL_GetAtomicInt(&revision);
    call->state.owned = SDL_GetAtomicInt(&owned);
    if (lx_clipboard_native_owned())
        call->state.owned = 1;
done:
    SDL_SetAtomicInt(&call->status, 2);
    SDL_SignalSemaphore(call->complete);
    release_call(call);
}

static ClipboardCall *new_call(unsigned action, const char *mime)
{
    ClipboardCall *call = calloc(1, sizeof *call);
    if (!call)
        return NULL;
    SDL_SetAtomicInt(&call->references, 1);
    call->action = action;
    call->complete = SDL_CreateSemaphore(0);
    call->mime = mime ? strdup(mime) : NULL;
    if (!call->complete || (mime && !call->mime))
    {
        release_call(call);
        return NULL;
    }
    return call;
}

static int execute_call(ClipboardCall *call)
{
    if (!call)
        return 0;
    SDL_AddAtomicInt(&call->references, 1);
    if (!SDL_RunOnMainThread(run_call, call, false))
    {
        release_call(call);
        return 0;
    }
    if (!SDL_WaitSemaphoreTimeout(call->complete, 2000))
    {
        /* A pending late callback observes cancellation and cannot publish an
           unexpected clipboard replacement after the caller has timed out. */
        SDL_CompareAndSwapAtomicInt(&call->status, 0, 3);
        return 0;
    }
    return call->result;
}

int lx_host_clipboard_state(LxHostClipboardState *state)
{
    ClipboardCall *call = new_call(0, NULL);
    int result = execute_call(call);
    if (result)
        *state = call->state;
    if (call)
        release_call(call);
    return result;
}

int lx_host_clipboard_has(const char *mime)
{
    ClipboardCall *call = mime ? new_call(1, mime) : NULL;
    int result = execute_call(call);
    if (call)
        release_call(call);
    return result;
}

void *lx_host_clipboard_read(const char *mime, size_t *size)
{
    if (!size)
        return NULL;
    *size = 0;
    ClipboardCall *call = mime ? new_call(2, mime) : NULL;
    void *bytes = NULL;
    if (execute_call(call))
    {
        bytes = call->bytes;
        *size = call->size;
        call->bytes = NULL;
    }
    if (call)
        release_call(call);
    return bytes;
}

int lx_host_clipboard_offer(const LxHostClipboardItem *items, size_t count,
                            LxHostClipboardProvider provider, void *context,
                            LxHostClipboardState *state)
{
    if (count > 64 || (count && !items))
        return 0;
    ClipboardCall *call = new_call(3, NULL);
    if (!call)
        return 0;
    ClipboardOffer *offer = call->offer = calloc(1, sizeof *offer);
    if (!offer)
        goto failed;
    offer->items = calloc(count ? count : 1, sizeof *items);
    if (!offer->items)
        goto failed;
    offer->provider = provider;
    offer->context = context;
    size_t total = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (!items[i].mime || strlen(items[i].mime) > 255 || items[i].size > CLIP_LIMIT - total ||
            (!items[i].data && items[i].size))
            goto failed;
        total += items[i].size;
        offer->count++;
        offer->items[i].mime = strdup(items[i].mime);
        if (items[i].data)
        {
            void *bytes = malloc(items[i].size ? items[i].size : 1);
            if (!bytes)
                goto failed;
            memcpy(bytes, items[i].data, items[i].size);
            offer->items[i].data = bytes;
            offer->items[i].size = items[i].size;
        }
        if (!offer->items[i].mime)
            goto failed;
    }
    int result = execute_call(call);
    if (result && state)
        *state = call->state;
    release_call(call);
    return result;
failed:
    release_call(call);
    return 0;
}

int lx_host_clipboard_materialize(void)
{
    ClipboardCall *call = new_call(4, NULL);
    int result = execute_call(call);
    if (call)
        release_call(call);
    return result;
}
