/* Public domain. No rights reserved. Wholly AI-authored. */
#include "hal/tls.h"
#include "hal/socket.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>

struct LxHostTlsContext
{
    SSL_CTX *ssl;
    int server;
};

struct LxHostTls
{
    SSL *ssl;
    int server;
    char error[256];
};

/* Keep SIGPIPE suppression and socket ownership inside the host socket contract.
   The stock socket BIO can raise SIGPIPE on Linux when a peer disconnects. */
static BIO_METHOD *SocketBio;
static pthread_once_t SocketBioOnce = PTHREAD_ONCE_INIT;

static int bio_create(BIO *bio)
{
    BIO_set_init(bio, 1);
    BIO_set_data(bio, NULL);
    return 1;
}

static int bio_destroy(BIO *bio)
{
    return bio != NULL;
}

static int bio_read(BIO *bio, char *buffer, int length)
{
    BIO_clear_retry_flags(bio);
    if (length <= 0)
        return 0;
    int n =
        (int)lx_host_socket_recv((int)(intptr_t)BIO_get_data(bio) - 1, buffer, (size_t)length, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_read(bio);
    return n;
}

static int bio_write(BIO *bio, const char *buffer, int length)
{
    BIO_clear_retry_flags(bio);
    if (length <= 0)
        return 0;
    int n =
        (int)lx_host_socket_send((int)(intptr_t)BIO_get_data(bio) - 1, buffer, (size_t)length, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_write(bio);
    return n;
}

static long bio_ctrl(BIO *bio, int command, long value, void *pointer)
{
    (void)value;
    if (command == BIO_C_GET_FD)
    {
        int fd = (int)(intptr_t)BIO_get_data(bio) - 1;
        if (pointer)
            *(int *)pointer = fd;
        return fd;
    }
    return command == BIO_CTRL_FLUSH || command == BIO_CTRL_DUP || command == BIO_CTRL_SET_CLOSE;
}

static void bio_initialize(void)
{
    SocketBio = BIO_meth_new(BIO_TYPE_SOURCE_SINK | BIO_get_new_index(), "2ine host socket");
    if (SocketBio &&
        (!BIO_meth_set_create(SocketBio, bio_create) ||
         !BIO_meth_set_destroy(SocketBio, bio_destroy) || !BIO_meth_set_read(SocketBio, bio_read) ||
         !BIO_meth_set_write(SocketBio, bio_write) || !BIO_meth_set_ctrl(SocketBio, bio_ctrl)))
    {
        BIO_meth_free(SocketBio);
        SocketBio = NULL;
    }
}

LxHostTlsContext *lx_host_tls_context_create(int server, const char *ca, const char *certificate,
                                             const char *key)
{
    LxHostTlsContext *ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;
    ctx->server = !!server;
    ctx->ssl = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
    if (!ctx->ssl || !SSL_CTX_set_min_proto_version(ctx->ssl, TLS1_2_VERSION))
        goto fail;
    SSL_CTX_set_options(ctx->ssl, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    if (server)
    {
        if (!certificate || !key ||
            SSL_CTX_use_certificate_chain_file(ctx->ssl, certificate) != 1 ||
            SSL_CTX_use_PrivateKey_file(ctx->ssl, key, SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(ctx->ssl) != 1)
            goto fail;
    }
    else
    {
        SSL_CTX_set_verify(ctx->ssl, SSL_VERIFY_PEER, NULL);
        if (ca ? SSL_CTX_load_verify_locations(ctx->ssl, ca, NULL) != 1
               : SSL_CTX_set_default_verify_paths(ctx->ssl) != 1)
            goto fail;
    }
    return ctx;
fail:
    lx_host_tls_context_free(ctx);
    return NULL;
}

void lx_host_tls_context_free(LxHostTlsContext *ctx)
{
    if (ctx)
    {
        SSL_CTX_free(ctx->ssl);
        free(ctx);
    }
}

int lx_host_tls_context_trust(LxHostTlsContext *ctx, const char *file, const char *path)
{
    if (!ctx)
        return 0;
    return file || path ? SSL_CTX_load_verify_locations(ctx->ssl, file, path)
                        : SSL_CTX_set_default_verify_paths(ctx->ssl);
}

int lx_host_tls_context_ciphers(LxHostTlsContext *ctx, const char *ciphers)
{
    return ctx && ciphers ? SSL_CTX_set_cipher_list(ctx->ssl, ciphers) : 0;
}

int lx_host_tls_context_protocol(LxHostTlsContext *ctx, int minimum, int maximum)
{
    if (!ctx || minimum < TLS1_2_VERSION || (maximum && maximum < minimum))
        return 0;
    return SSL_CTX_set_min_proto_version(ctx->ssl, minimum) &&
           SSL_CTX_set_max_proto_version(ctx->ssl, maximum);
}

LxHostTls *lx_host_tls_session_create(LxHostTlsContext *ctx, int socket, const char *peer)
{
    if (!ctx || socket < 0 || (!ctx->server && (!peer || !*peer)))
    {
        errno = EINVAL;
        return NULL;
    }
    LxHostTls *tls = calloc(1, sizeof *tls);
    if (!tls)
        return NULL;
    tls->server = ctx->server;
    tls->ssl = SSL_new(ctx->ssl);
    if (!tls->ssl)
        goto fail;
    pthread_once(&SocketBioOnce, bio_initialize);
    BIO *bio = SocketBio ? BIO_new(SocketBio) : NULL;
    if (!bio)
        goto fail;
    BIO_set_data(bio, (void *)((intptr_t)socket + 1));
    SSL_set_bio(tls->ssl, bio, bio);
    if (!ctx->server)
    {
        unsigned char ip[16];
        if (inet_pton(AF_INET, peer, ip) == 1 || inet_pton(AF_INET6, peer, ip) == 1)
        {
            if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(tls->ssl), peer) != 1)
                goto fail;
        }
        else if (SSL_set1_dnsname(tls->ssl, peer) != 1 ||
                 SSL_set_tlsext_host_name(tls->ssl, peer) != 1)
            goto fail;
    }
    if (ctx->server)
        SSL_set_accept_state(tls->ssl);
    else
        SSL_set_connect_state(tls->ssl);
    return tls;
fail:
    lx_host_tls_free(tls);
    return NULL;
}

void lx_host_tls_free(LxHostTls *tls)
{
    if (tls)
    {
        SSL_free(tls->ssl);
        free(tls);
    }
}

static int result(LxHostTls *tls, int value)
{
    if (value > 0)
        return value;
    int error = SSL_get_error(tls->ssl, value);
    if (error == SSL_ERROR_ZERO_RETURN)
        return 0;
    if (error == SSL_ERROR_WANT_READ)
        return LX_TLS_WANT_READ;
    if (error == SSL_ERROR_WANT_WRITE)
        return LX_TLS_WANT_WRITE;
    unsigned long code = ERR_peek_last_error();
    if (code)
        ERR_error_string_n(code, tls->error, sizeof tls->error);
    else
        snprintf(tls->error, sizeof tls->error, "TLS I/O error %d (errno %d)", error, errno);
    return LX_TLS_ERROR;
}

int lx_host_tls_handshake(LxHostTls *tls)
{
    if (!tls)
        return LX_TLS_ERROR;
    ERR_clear_error();
    return result(tls, SSL_do_handshake(tls->ssl));
}

int lx_host_tls_read(LxHostTls *tls, void *buffer, int length)
{
    if (!tls || length < 0 || (!buffer && length))
        return LX_TLS_ERROR;
    if (!length)
        return 0;
    ERR_clear_error();
    return result(tls, SSL_read(tls->ssl, buffer, length));
}

int lx_host_tls_write(LxHostTls *tls, const void *buffer, int length)
{
    if (!tls || length < 0 || (!buffer && length))
        return LX_TLS_ERROR;
    if (!length)
        return 0;
    ERR_clear_error();
    return result(tls, SSL_write(tls->ssl, buffer, length));
}

int lx_host_tls_shutdown(LxHostTls *tls)
{
    if (!tls)
        return LX_TLS_ERROR;
    ERR_clear_error();
    int value = SSL_shutdown(tls->ssl);
    return value >= 0 ? value : result(tls, value);
}

int lx_host_tls_pending(LxHostTls *tls)
{
    return tls ? SSL_pending(tls->ssl) : 0;
}

const char *lx_host_tls_version(LxHostTls *tls)
{
    return tls ? SSL_get_version(tls->ssl) : "";
}

const char *lx_host_tls_diagnostic(LxHostTls *tls)
{
    return tls ? tls->error : "Invalid TLS session";
}
