/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "hal/tls.h"
#include "hal/socket.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TWOINE_TLS_TEST_SHIM
#include "os2libs/ssleay32.h"
#include "runtime/runtime_features.h"
#endif
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct Server
{
    int socket, success;
    LxHostTlsContext *context;
} Server;
static const char Message[] = "encrypted OS/2 round trip";
static void *serve(void *value)
{
    Server *server = value;
    int fd = lx_host_socket_accept(server->socket, NULL);
    CHECK(fd >= 0);
    LxHostTls *tls = lx_host_tls_session_create(server->context, fd, NULL);
    CHECK(tls);
    int result = lx_host_tls_handshake(tls);
    if (server->success)
    {
        CHECK(result == 1);
        char buffer[sizeof Message];
        int total = 0;
        while (total < (int)sizeof Message)
        {
            int n = lx_host_tls_read(tls, buffer + total, sizeof Message - total);
            CHECK(n > 0);
            total += n;
        }
        CHECK(!memcmp(buffer, Message, sizeof Message));
        CHECK(lx_host_tls_write(tls, buffer, sizeof buffer) == sizeof buffer);
        CHECK(lx_host_tls_shutdown(tls) >= 0);
    }
    lx_host_tls_free(tls);
    CHECK(!lx_host_socket_close(fd));
    return NULL;
}
static void wait_for(int fd, int result)
{
    CHECK(result == LX_TLS_WANT_READ || result == LX_TLS_WANT_WRITE);
    CHECK(lx_host_socket_poll(&fd, result == LX_TLS_WANT_READ, result == LX_TLS_WANT_WRITE, 0,
                              1000) == 1);
}
static void run(const char *cert, const char *key, const char *peer, int trusted, int upgrade)
{
    Server server = {lx_host_socket_open(LX_SOCKET_STREAM), trusted && !strcmp(peer, "localhost"),
                     NULL};
    CHECK(server.socket >= 0);
    server.context = lx_host_tls_context_create(1, NULL, cert, key);
    CHECK(server.context);
    LxHostSocketAddress address = {lx_host_socket_parse_address("127.0.0.1"), 0};
    CHECK(!lx_host_socket_bind(server.socket, &address));
    CHECK(!lx_host_socket_name(server.socket, &address));
    CHECK(!lx_host_socket_listen(server.socket, 1));
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, serve, &server));
    int fd = lx_host_socket_open(LX_SOCKET_STREAM);
    CHECK(fd >= 0 && !lx_host_socket_connect(fd, &address));
    CHECK(!lx_host_socket_nonblocking(fd, 1));
    int result, wants = 0;
#ifdef TWOINE_TLS_TEST_SHIM
    CHECK(lxRuntimeConfigureFeature("tls_upgrade", upgrade ? "true" : "false") == 1);
    CHECK(!setenv("SSL_CERT_FILE", trusted ? cert : "/nonexistent/twoine-ca.pem", 1));
    CHECK(!setenv("SSL_CERT_DIR", "/nonexistent/twoine-ca-directory", 1));
    uint32_t context =
        Os2SslContextNew(upgrade ? Os2SslTls1ClientMethod() : Os2SslTls12ClientMethod());
    CHECK(context);
    uint32_t tls = Os2SslNew(context);
    CHECK(tls && Os2SslSetFd(tls, fd));
    CHECK(Os2SslCtrl(tls, 55, 0, peer) == 1);
    Os2SslContextFree(context); /* Active sessions retain their context. */
    do
    {
        result = Os2SslConnect(tls);
        int error = Os2SslGetError(tls, result);
        if (error != 2 && error != 3)
            break;
        wait_for(fd, error == 2 ? LX_TLS_WANT_READ : LX_TLS_WANT_WRITE);
        wants++;
    } while (wants < 20);
#else
    (void)upgrade;
    LxHostTlsContext *context = lx_host_tls_context_create(0, trusted ? cert : NULL, NULL, NULL);
    CHECK(context);
    CHECK(!lx_host_tls_session_create(context, fd, NULL));
    LxHostTls *tls = lx_host_tls_session_create(context, fd, peer);
    CHECK(tls);
    lx_host_tls_context_free(context);
    do
    {
        result = lx_host_tls_handshake(tls);
        if (result != LX_TLS_WANT_READ && result != LX_TLS_WANT_WRITE)
            break;
        wait_for(fd, result);
        wants++;
    } while (wants < 20);
#endif
    if (server.success)
    {
        CHECK(result == 1 && wants > 0);
        char output[sizeof Message];
#ifdef TWOINE_TLS_TEST_SHIM
        CHECK(Os2SslVerifyResult(tls) == 0);
        CHECK(!strcmp(Os2SslVersion(tls), upgrade ? "TLSv1.3" : "TLSv1.2"));
        do
        {
            result = Os2SslWrite(tls, Message, sizeof Message);
            if (result < 0)
            {
                int error = Os2SslGetError(tls, result);
                wait_for(fd, error == 2 ? -2 : error == 3 ? -3 : -1);
            }
        } while (result < 0);
        CHECK(result == sizeof Message);
        do
        {
            result = Os2SslRead(tls, output, sizeof output);
            if (result < 0)
            {
                int error = Os2SslGetError(tls, result);
                wait_for(fd, error == 2 ? -2 : error == 3 ? -3 : -1);
            }
        } while (result < 0);
        CHECK(result == sizeof output && !memcmp(output, Message, sizeof output));
#else
        CHECK(!strcmp(lx_host_tls_version(tls), "TLSv1.3"));
        do
        {
            result = lx_host_tls_write(tls, Message, sizeof Message);
            if (result < 0)
                wait_for(fd, result);
        } while (result < 0);
        CHECK(result == sizeof Message);
        do
        {
            result = lx_host_tls_read(tls, output, sizeof output);
            if (result < 0)
                wait_for(fd, result);
        } while (result < 0);
        CHECK(result == sizeof output && !memcmp(output, Message, sizeof output));
#endif
    }
    else
        CHECK(result < 0);
#ifdef TWOINE_TLS_TEST_SHIM
    Os2SslFree(tls);
    CHECK(!Os2SslSetFd(tls, fd)); /* Stale handles cannot be reused. */
#else
    if (!server.success)
        CHECK(lx_host_tls_diagnostic(tls)[0]);
    lx_host_tls_free(tls);
#endif
    CHECK(!lx_host_socket_close(fd));
    CHECK(!pthread_join(thread, NULL));
    lx_host_tls_context_free(server.context);
    CHECK(!lx_host_socket_close(server.socket));
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
#ifdef TWOINE_TLS_TEST_SHIM
    CHECK(lxRuntimeTlsUpgrade());
    CHECK(lxRuntimeConfigureFeature("tls_upgrade", "invalid") == -1);
    CHECK(lxRuntimeTlsUpgrade());
    CHECK(lxRuntimeConfigureFeature("tls_upgrade", "false") == 1);
    CHECK(!Os2SslContextNew(Os2SslTls1ClientMethod()));
    CHECK(!Os2SslContextNew(Os2SslV3ClientMethod()));
    run(argv[1], argv[2], "localhost", 1, 0);
#endif
    run(argv[1], argv[2], "localhost", 1, 1);
    run(argv[1], argv[2], "wrong.example", 1, 1);
    run(argv[1], argv[2], "localhost", 0, 1);
    puts("TLS encrypted transfer, nonblocking retries, trust and peer-name rejection passed");
    return 0;
}
