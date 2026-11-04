/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_TLS_H
#define TWOINE_HOST_TLS_H
#include <stddef.h>
typedef struct LxHostTlsContext LxHostTlsContext;
typedef struct LxHostTls LxHostTls;
/* Client contexts always verify chains and peer names, with TLS >= 1.2.
   A non-NULL CA file replaces default trust for this context (e.g. private PKI).
   Server contexts require a PEM certificate chain and private key. */
LxHostTlsContext *lx_host_tls_context_create(int server, const char *ca_file,
                                             const char *certificate, const char *key);
void lx_host_tls_context_free(LxHostTlsContext *context);
int lx_host_tls_context_trust(LxHostTlsContext *context, const char *ca_file, const char *ca_path);
int lx_host_tls_context_ciphers(LxHostTlsContext *context, const char *ciphers);
/* Wire protocol versions (0x0303 = TLS 1.2, 0x0304 = TLS 1.3); max 0 = latest.
   The security floor cannot be lowered below TLS 1.2. */
int lx_host_tls_context_protocol(LxHostTlsContext *context, int minimum, int maximum);
/* Session retains its context. The caller owns the socket, including on failure. */
LxHostTls *lx_host_tls_session_create(LxHostTlsContext *context, int socket, const char *peer_name);
void lx_host_tls_free(LxHostTls *tls);
enum
{
    LX_TLS_ERROR = -1,
    LX_TLS_WANT_READ = -2,
    LX_TLS_WANT_WRITE = -3
};
/* Handshake: 1 success; read/write: byte count, 0 clean EOF; negative as above.
   Shutdown: 1 complete, 0 close_notify sent, negative as above. Nonblocking callers
   poll the socket in the indicated direction, then retry the same operation. */
int lx_host_tls_handshake(LxHostTls *tls);
int lx_host_tls_read(LxHostTls *tls, void *buffer, int length);
int lx_host_tls_write(LxHostTls *tls, const void *buffer, int length);
int lx_host_tls_shutdown(LxHostTls *tls);
int lx_host_tls_pending(LxHostTls *tls);
const char *lx_host_tls_version(LxHostTls *tls);
/* Per-session diagnostic, copied before releasing a guest handle borrow. */
const char *lx_host_tls_diagnostic(LxHostTls *tls);
#endif
