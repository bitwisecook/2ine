/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opaque TLS objects are typed guest handles; no host pointers cross the ABI. */
static uint32 ssl_word(const uint8 *a, unsigned i)
{
    return lx_guest_load_u32(a + 4 * i);
}

static uint32 bridge32_Os2SslInit(uint8 *a)
{
    (void)a;
    return Os2SslInit();
}

static uint32 bridge32_Os2SslErrors(uint8 *a)
{
    (void)a;
    Os2SslErrors();
    return 0;
}

static uint32 bridge32_Os2SslClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslClientMethod();
}

static uint32 bridge32_Os2SslMethod(uint8 *a)
{
    (void)a;
    return Os2SslMethod();
}

static uint32 bridge32_Os2SslContextNew(uint8 *a)
{
    return Os2SslContextNew(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslContextFree(uint8 *a)
{
    Os2SslContextFree(ssl_word(a, 0));
    return 0;
}

static uint32 bridge32_Os2SslContextDefaultTrust(uint8 *a)
{
    return Os2SslContextDefaultTrust(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslContextVerify(uint8 *a)
{
    Os2SslContextVerify(ssl_word(a, 0), ssl_word(a, 1), ssl_word(a, 2));
    return 0;
}

static uint32 bridge32_Os2SslNew(uint8 *a)
{
    return Os2SslNew(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslFree(uint8 *a)
{
    Os2SslFree(ssl_word(a, 0));
    return 0;
}

static uint32 bridge32_Os2SslSetFd(uint8 *a)
{
    return Os2SslSetFd(ssl_word(a, 0), ssl_word(a, 1));
}

static uint32 bridge32_Os2SslGetFd(uint8 *a)
{
    return Os2SslGetFd(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslConnect(uint8 *a)
{
    return Os2SslConnect(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslHandshake(uint8 *a)
{
    return Os2SslHandshake(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslPending(uint8 *a)
{
    return Os2SslPending(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslShutdown(uint8 *a)
{
    return Os2SslShutdown(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslGetError(uint8 *a)
{
    return Os2SslGetError(ssl_word(a, 0), ssl_word(a, 1));
}

static uint32 bridge32_Os2SslVerifyResult(uint8 *a)
{
    return Os2SslVerifyResult(ssl_word(a, 0));
}

static uint32 bridge32_Os2SslContextTrust(uint8 *a)
{
    char *file = ssl_word(a, 1)
                     ? lx_execution_copy_string(GLoaderState.execution, ssl_word(a, 1), 4096)
                     : NULL;
    char *path = ssl_word(a, 2)
                     ? lx_execution_copy_string(GLoaderState.execution, ssl_word(a, 2), 4096)
                     : NULL;
    uint32 result = ((!ssl_word(a, 1) || file) && (!ssl_word(a, 2) || path))
                        ? Os2SslContextTrust(ssl_word(a, 0), file, path)
                        : 0;
    free(file);
    free(path);
    return result;
}

static uint32 bridge32_Os2SslContextCiphers(uint8 *a)
{
    char *text = lx_execution_copy_string(GLoaderState.execution, ssl_word(a, 1), 4096);
    uint32 result = text ? Os2SslContextCiphers(ssl_word(a, 0), text) : 0;
    free(text);
    return result;
}

static uint32 bridge32_Os2SslCtrl(uint8 *a)
{
    /* Only the hostname control accepts guest memory; other controls fail closed. */
    if (ssl_word(a, 1) != 55 || ssl_word(a, 2) != 0)
        return 0;
    char *text = lx_execution_copy_string(GLoaderState.execution, ssl_word(a, 3), 256);
    uint32 result = text ? Os2SslCtrl(ssl_word(a, 0), 55, 0, text) : 0;
    free(text);
    return result;
}

static uint32 ssl_transfer(uint8 *a, int writing)
{
    uint32 length = ssl_word(a, 2), result = (uint32)-1;
    LxGuestBorrow buffer = {0};
    if (length <= INT32_MAX &&
        lx_execution_borrow(GLoaderState.execution, ssl_word(a, 1), length,
                            writing ? LX_GUEST_READ : LX_GUEST_WRITE, &buffer))
        result = writing ? Os2SslWrite(ssl_word(a, 0), buffer.host, length)
                         : Os2SslRead(ssl_word(a, 0), buffer.host, length);
    lx_execution_release(&buffer);
    return result;
}

static uint32 bridge32_Os2SslRead(uint8 *a)
{
    return ssl_transfer(a, 0);
}

static uint32 bridge32_Os2SslWrite(uint8 *a)
{
    return ssl_transfer(a, 1);
}

static uint32 bridge32_Os2SslVersion(uint8 *a)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    const char *text = Os2SslVersion(ssl_word(a, 0));
    if (!text)
        return 0;
    if (!storage)
        storage =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    memcpy(lx_execution_mapping_host(storage), text, strlen(text) + 1);
    return lx_execution_mapping_address(storage);
}

static uint32 bridge32_Os2SslV2ClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslV2ClientMethod();
}

static uint32 bridge32_Os2SslV2Method(uint8 *a)
{
    (void)a;
    return Os2SslV2Method();
}

static uint32 bridge32_Os2SslV3ClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslV3ClientMethod();
}

static uint32 bridge32_Os2SslV3Method(uint8 *a)
{
    (void)a;
    return Os2SslV3Method();
}

static uint32 bridge32_Os2SslTls1Method(uint8 *a)
{
    (void)a;
    return Os2SslTls1Method();
}

static uint32 bridge32_Os2SslTls1ClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslTls1ClientMethod();
}

static uint32 bridge32_Os2SslTls11Method(uint8 *a)
{
    (void)a;
    return Os2SslTls11Method();
}

static uint32 bridge32_Os2SslTls11ClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslTls11ClientMethod();
}

static uint32 bridge32_Os2SslTls12Method(uint8 *a)
{
    (void)a;
    return Os2SslTls12Method();
}

static uint32 bridge32_Os2SslTls12ClientMethod(uint8 *a)
{
    (void)a;
    return Os2SslTls12ClientMethod();
}
