/* Public domain. No rights reserved. Wholly AI-authored. */
/* OpenSSL 1.0 client ABI subset; upstream util/ssleay.num ordinals. */
#ifndef TWOINE_SSLEAY32_H
#define TWOINE_SSLEAY32_H
#include "os2libs/os2types.h"
OS2EXPORT int OS2API Os2SslInit(void);
OS2EXPORT void OS2API Os2SslErrors(void);
OS2EXPORT uint32_t OS2API Os2SslClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslMethod(void);
OS2EXPORT uint32_t OS2API Os2SslV2ClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslV2Method(void);
OS2EXPORT uint32_t OS2API Os2SslV3ClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslV3Method(void);
OS2EXPORT uint32_t OS2API Os2SslTls1Method(void);
OS2EXPORT uint32_t OS2API Os2SslTls1ClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslTls11Method(void);
OS2EXPORT uint32_t OS2API Os2SslTls11ClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslTls12ClientMethod(void);
OS2EXPORT uint32_t OS2API Os2SslTls12Method(void);
OS2EXPORT uint32_t OS2API Os2SslContextNew(uint32_t method);
OS2EXPORT void OS2API Os2SslContextFree(uint32_t context);
OS2EXPORT int OS2API Os2SslContextTrust(uint32_t context, const char *file, const char *path);
OS2EXPORT int OS2API Os2SslContextDefaultTrust(uint32_t context);
OS2EXPORT int OS2API Os2SslContextCiphers(uint32_t context, const char *ciphers);
OS2EXPORT void OS2API Os2SslContextVerify(uint32_t context, int mode, uint32_t callback);
OS2EXPORT uint32_t OS2API Os2SslNew(uint32_t context);
OS2EXPORT void OS2API Os2SslFree(uint32_t session);
OS2EXPORT int OS2API Os2SslSetFd(uint32_t session, int socket);
OS2EXPORT int OS2API Os2SslGetFd(uint32_t session);
OS2EXPORT int32_t OS2API Os2SslCtrl(uint32_t session, int command, int32_t value,
                                    const char *pointer);
OS2EXPORT int OS2API Os2SslConnect(uint32_t session);
OS2EXPORT int OS2API Os2SslHandshake(uint32_t session);
OS2EXPORT int OS2API Os2SslRead(uint32_t session, void *buffer, int length);
OS2EXPORT int OS2API Os2SslWrite(uint32_t session, const void *buffer, int length);
OS2EXPORT int OS2API Os2SslPending(uint32_t session);
OS2EXPORT int OS2API Os2SslShutdown(uint32_t session);
OS2EXPORT int OS2API Os2SslGetError(uint32_t session, int result);
OS2EXPORT int32_t OS2API Os2SslVerifyResult(uint32_t session);
OS2EXPORT const char *OS2API Os2SslVersion(uint32_t session);
#endif
