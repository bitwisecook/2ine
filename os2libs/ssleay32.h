/* Public domain. No rights reserved. Wholly AI-authored. */
/* OpenSSL 1.0 client ABI subset; upstream util/ssleay.num ordinals. */
#ifndef TWOINE_SSLEAY32_H
#define TWOINE_SSLEAY32_H
#include "os2libs/os2types.h"
OS2EXPORT int OS2API Os2SslInit(void) OS2APIINFO(ord = 183, name = SSL_library_init);
OS2EXPORT void OS2API Os2SslErrors(void) OS2APIINFO(ord = 74, name = SSL_load_error_strings);
OS2EXPORT uint32_t OS2API Os2SslClientMethod(void)
    OS2APIINFO(ord = 110, name = SSLv23_client_method);
OS2EXPORT uint32_t OS2API Os2SslMethod(void) OS2APIINFO(ord = 111, name = SSLv23_method);
OS2EXPORT uint32_t OS2API Os2SslV2ClientMethod(void)
    OS2APIINFO(ord = 113, name = SSLv2_client_method);
OS2EXPORT uint32_t OS2API Os2SslV2Method(void) OS2APIINFO(ord = 114, name = SSLv2_method);
OS2EXPORT uint32_t OS2API Os2SslV3ClientMethod(void)
    OS2APIINFO(ord = 116, name = SSLv3_client_method);
OS2EXPORT uint32_t OS2API Os2SslV3Method(void) OS2APIINFO(ord = 117, name = SSLv3_method);
OS2EXPORT uint32_t OS2API Os2SslTls1Method(void) OS2APIINFO(ord = 170, name = TLSv1_method);
OS2EXPORT uint32_t OS2API Os2SslTls1ClientMethod(void)
    OS2APIINFO(ord = 172, name = TLSv1_client_method);
OS2EXPORT uint32_t OS2API Os2SslTls11Method(void) OS2APIINFO(ord = 313, name = TLSv1_1_method);
OS2EXPORT uint32_t OS2API Os2SslTls11ClientMethod(void)
    OS2APIINFO(ord = 314, name = TLSv1_1_client_method);
OS2EXPORT uint32_t OS2API Os2SslTls12ClientMethod(void)
    OS2APIINFO(ord = 341, name = TLSv1_2_client_method);
OS2EXPORT uint32_t OS2API Os2SslTls12Method(void) OS2APIINFO(ord = 350, name = TLSv1_2_method);
OS2EXPORT uint32_t OS2API Os2SslContextNew(uint32_t method)
    OS2APIINFO(ord = 12, name = SSL_CTX_new);
OS2EXPORT void OS2API Os2SslContextFree(uint32_t context) OS2APIINFO(ord = 8, name = SSL_CTX_free);
OS2EXPORT int OS2API Os2SslContextTrust(uint32_t context, const char *file, const char *path)
    OS2APIINFO(ord = 141, name = SSL_CTX_load_verify_locations);
OS2EXPORT int OS2API Os2SslContextDefaultTrust(uint32_t context)
    OS2APIINFO(ord = 142, name = SSL_CTX_set_default_verify_paths);
OS2EXPORT int OS2API Os2SslContextCiphers(uint32_t context, const char *ciphers)
    OS2APIINFO(ord = 15, name = SSL_CTX_set_cipher_list);
OS2EXPORT void OS2API Os2SslContextVerify(uint32_t context, int mode, uint32_t callback)
    OS2APIINFO(ord = 21, name = SSL_CTX_set_verify);
OS2EXPORT uint32_t OS2API Os2SslNew(uint32_t context) OS2APIINFO(ord = 75, name = SSL_new);
OS2EXPORT void OS2API Os2SslFree(uint32_t session) OS2APIINFO(ord = 48, name = SSL_free);
OS2EXPORT int OS2API Os2SslSetFd(uint32_t session, int socket)
    OS2APIINFO(ord = 87, name = SSL_set_fd);
OS2EXPORT int OS2API Os2SslGetFd(uint32_t session) OS2APIINFO(ord = 59, name = SSL_get_fd);
OS2EXPORT int32_t OS2API Os2SslCtrl(uint32_t session, int command, int32_t value,
                                    const char *pointer) OS2APIINFO(ord = 45, name = SSL_ctrl);
OS2EXPORT int OS2API Os2SslConnect(uint32_t session) OS2APIINFO(ord = 43, name = SSL_connect);
OS2EXPORT int OS2API Os2SslHandshake(uint32_t session)
    OS2APIINFO(ord = 125, name = SSL_do_handshake);
OS2EXPORT int OS2API Os2SslRead(uint32_t session, void *buffer, int length)
    OS2APIINFO(ord = 78, name = SSL_read);
OS2EXPORT int OS2API Os2SslWrite(uint32_t session, const void *buffer, int length)
    OS2APIINFO(ord = 108, name = SSL_write);
OS2EXPORT int OS2API Os2SslPending(uint32_t session) OS2APIINFO(ord = 77, name = SSL_pending);
OS2EXPORT int OS2API Os2SslShutdown(uint32_t session) OS2APIINFO(ord = 96, name = SSL_shutdown);
OS2EXPORT int OS2API Os2SslGetError(uint32_t session, int result)
    OS2APIINFO(ord = 58, name = SSL_get_error);
OS2EXPORT int32_t OS2API Os2SslVerifyResult(uint32_t session)
    OS2APIINFO(ord = 157, name = SSL_get_verify_result);
OS2EXPORT const char *OS2API Os2SslVersion(uint32_t session)
    OS2APIINFO(ord = 71, name = SSL_get_version);
#endif
