/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2native.h"
#include "ssleay32.h"
#include "guest/handles.h"
#include "hal/tls.h"
#include "hal/host_sync.h"
#include "runtime/runtime_features.h"
typedef struct SslContext
{
    LxHostTlsContext *host;
    LxHostMutex *mutex;
    int unsupported_callback;
} SslContext;
typedef struct SslSession
{
    LxHostTls *host;
    LxHandleBorrow context;
    LxHostMutex *mutex;
    int socket, last_error, verified;
    char *peer;
} SslSession;
#include "os2libs/generated/ssleay32-lx.h"
static void lock(LxHostMutex *m)
{
    lx_host_mutex_lock(m, UINT32_MAX);
}
static void context_destroy(void *value)
{
    SslContext *c = value;
    lx_host_tls_context_free(c->host);
    if (c->mutex)
        lx_host_mutex_destroy(c->mutex);
    free(c);
}
static void session_destroy(void *value)
{
    SslSession *s = value;
    lx_host_tls_free(s->host);
    lx_handle_release(&s->context);
    if (s->mutex)
        lx_host_mutex_destroy(s->mutex);
    free(s->peer);
    free(s);
}
int Os2SslInit(void)
{
    return 1;
}
void Os2SslErrors(void) {}
uint32_t Os2SslClientMethod(void)
{
    return 1;
}
uint32_t Os2SslMethod(void)
{
    return 1;
}
uint32_t Os2SslV2ClientMethod(void)
{
    return 2;
}
uint32_t Os2SslV2Method(void)
{
    return 2;
}
uint32_t Os2SslV3ClientMethod(void)
{
    return 0x0300;
}
uint32_t Os2SslV3Method(void)
{
    return 0x0300;
}
uint32_t Os2SslTls1ClientMethod(void)
{
    return 0x0301;
}
uint32_t Os2SslTls1Method(void)
{
    return 0x0301;
}
uint32_t Os2SslTls11ClientMethod(void)
{
    return 0x0302;
}
uint32_t Os2SslTls11Method(void)
{
    return 0x0302;
}
uint32_t Os2SslTls12ClientMethod(void)
{
    return 0x0303;
}
uint32_t Os2SslTls12Method(void)
{
    return 0x0303;
}
uint32_t Os2SslContextNew(uint32_t method)
{
    if (method != 1 && method != 2 && (method < 0x0300 || method > 0x0303))
        return 0;
    int upgrade = lxRuntimeTlsUpgrade();
    if (!upgrade && method != 1 && method != 0x0303)
        return 0;
    SslContext *c = calloc(1, sizeof *c);
    if (!c)
        return 0;
    c->mutex = lx_host_mutex_create(0, 0);
    c->host = lx_host_tls_context_create(0, NULL, NULL, NULL);
    if (!c->host || !c->mutex)
    {
        context_destroy(c);
        return 0;
    }
    if (!lx_host_tls_context_protocol(c->host, 0x0303, upgrade ? 0 : 0x0303))
    {
        context_destroy(c);
        return 0;
    }
    uint32_t handle = lx_handle_create(LX_HANDLE_TLS_CONTEXT, c, context_destroy);
    if (!handle)
        context_destroy(c);
    return handle;
}
void Os2SslContextFree(uint32_t h)
{
    lx_handle_close(h, LX_HANDLE_TLS_CONTEXT);
}
int Os2SslContextTrust(uint32_t h, const char *file, const char *path)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_TLS_CONTEXT, &borrow))
        return 0;
    SslContext *c = borrow.resource;
    lock(c->mutex);
    /* Guest paths are resolved by the same drive mapping as OS/2 file calls. */
    uint32 error = 0;
    char *f = file && GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(file, &error) : NULL;
    char *p = path && GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(path, &error) : NULL;
    int result = (!file || f) && (!path || p) ? lx_host_tls_context_trust(c->host, f, p) : 0;
    free(f);
    free(p);
    lx_host_mutex_unlock(c->mutex);
    lx_handle_release(&borrow);
    return result;
}
int Os2SslContextDefaultTrust(uint32_t h)
{
    return Os2SslContextTrust(h, NULL, NULL);
}
int Os2SslContextCiphers(uint32_t h, const char *ciphers)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_TLS_CONTEXT, &borrow))
        return 0;
    SslContext *c = borrow.resource;
    lock(c->mutex);
    int result = lx_host_tls_context_ciphers(c->host, ciphers);
    lx_host_mutex_unlock(c->mutex);
    lx_handle_release(&borrow);
    return result;
}
void Os2SslContextVerify(uint32_t h, int mode, uint32_t callback)
{
    (void)mode; /* Chain and name validation remain mandatory. */
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_TLS_CONTEXT, &borrow))
        return;
    SslContext *c = borrow.resource;
    lock(c->mutex);
    c->unsupported_callback = callback != 0;
    lx_host_mutex_unlock(c->mutex);
    lx_handle_release(&borrow);
}
uint32_t Os2SslNew(uint32_t h)
{
    SslSession *s = calloc(1, sizeof *s);
    if (!s)
        return 0;
    s->socket = -1;
    s->last_error = 1;
    s->mutex = lx_host_mutex_create(0, 0);
    if (!s->mutex || !lx_handle_borrow(h, LX_HANDLE_TLS_CONTEXT, &s->context))
    {
        session_destroy(s);
        return 0;
    }
    uint32_t handle = lx_handle_create(LX_HANDLE_TLS_SESSION, s, session_destroy);
    if (!handle)
        session_destroy(s);
    return handle;
}
void Os2SslFree(uint32_t h)
{
    lx_handle_close(h, LX_HANDLE_TLS_SESSION);
}
static SslSession *session(uint32_t h, LxHandleBorrow *borrow)
{
    if (!lx_handle_borrow(h, LX_HANDLE_TLS_SESSION, borrow))
        return NULL;
    SslSession *s = borrow->resource;
    lock(s->mutex);
    return s;
}
static void release(SslSession *s, LxHandleBorrow *borrow)
{
    lx_host_mutex_unlock(s->mutex);
    lx_handle_release(borrow);
}
int Os2SslSetFd(uint32_t h, int fd)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return 0;
    int result = !s->host && fd >= 0;
    if (result)
        s->socket = fd;
    release(s, &b);
    return result;
}
int Os2SslGetFd(uint32_t h)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return -1;
    int result = s->socket;
    release(s, &b);
    return result;
}
int32_t Os2SslCtrl(uint32_t h, int command, int32_t value, const char *pointer)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return 0;
    int result = 0;
    /* SSL_CTRL_SET_TLSEXT_HOSTNAME: also supplies the expected certificate name. */
    if (command == 55 && value == 0 && pointer && *pointer && !s->host)
    {
        char *copy = strdup(pointer);
        if (copy)
        {
            free(s->peer);
            s->peer = copy;
            result = 1;
        }
    }
    release(s, &b);
    return result;
}
static int start(SslSession *s)
{
    if (s->host)
        return 1;
    SslContext *c = s->context.resource;
    lock(c->mutex);
    if (!c->unsupported_callback)
        s->host = lx_host_tls_session_create(c->host, s->socket, s->peer);
    lx_host_mutex_unlock(c->mutex);
    return s->host != NULL;
}
static int converted(SslSession *s, int result, int eof)
{
    s->last_error = result == LX_TLS_WANT_READ    ? 2
                    : result == LX_TLS_WANT_WRITE ? 3
                    : result < 0                  ? 1
                    : !result && eof              ? 6
                                                  : 0;
    return result < 0 ? -1 : result;
}
int Os2SslConnect(uint32_t h)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return -1;
    int result = converted(s, start(s) ? lx_host_tls_handshake(s->host) : LX_TLS_ERROR, 0);
    if (result == 1)
        s->verified = 1;
    release(s, &b);
    return result;
}
int Os2SslHandshake(uint32_t h)
{
    return Os2SslConnect(h);
}
static int transfer(uint32_t h, void *buffer, int length, int writing)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return -1;
    int result = LX_TLS_ERROR;
    if (!s->verified && start(s))
    {
        result = lx_host_tls_handshake(s->host);
        if (result == 1)
            s->verified = 1;
    }
    if (s->verified)
        result = writing ? lx_host_tls_write(s->host, buffer, length)
                         : lx_host_tls_read(s->host, buffer, length);
    result = converted(s, result, length != 0);
    release(s, &b);
    return result;
}
int Os2SslRead(uint32_t h, void *buffer, int length)
{
    return transfer(h, buffer, length, 0);
}
int Os2SslWrite(uint32_t h, const void *buffer, int length)
{
    return transfer(h, (void *)buffer, length, 1);
}
int Os2SslShutdown(uint32_t h)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return -1;
    int result = converted(s, s->verified ? lx_host_tls_shutdown(s->host) : LX_TLS_ERROR, 0);
    release(s, &b);
    return result;
}
int Os2SslGetError(uint32_t h, int result)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return 1;
    int error = result > 0 ? 0 : s->last_error;
    release(s, &b);
    return error;
}
int Os2SslPending(uint32_t h)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return 0;
    int result = lx_host_tls_pending(s->host);
    release(s, &b);
    return result;
}
int32_t Os2SslVerifyResult(uint32_t h)
{
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return 50; /* X509_V_ERR_APPLICATION_VERIFICATION */
    int result = s->verified ? 0 : 50;
    release(s, &b);
    return result;
}
const char *Os2SslVersion(uint32_t h)
{
    static LX_HOST_THREAD_LOCAL char version[32];
    LxHandleBorrow b = {0};
    SslSession *s = session(h, &b);
    if (!s)
        return NULL;
    snprintf(version, sizeof version, "%s", s->verified ? lx_host_tls_version(s->host) : "unknown");
    release(s, &b);
    return version;
}
