/* Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt. */
/* Host, protocol and service databases behind platform-neutral host contracts. */
#define _DEFAULT_SOURCE 1
#include "host/socket.h"
#include "host/thread.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

/* libc database functions may share process storage. Copy it while holding the lock. */
static pthread_mutex_t DatabaseMutex = PTHREAD_MUTEX_INITIALIZER;
static LX_HOST_THREAD_LOCAL int ResolverError;

/* Each query kind keeps one per-thread copy of its last result. */
#define DATABASE_BYTES 65536
#define DATABASE_LIST_MAX 256
/* BSD h_errno NO_RECOVERY, which OS/2 TCP/IP reports unchanged. */
#define RESOLVER_NO_RECOVERY 3
/* Host entries hold BSD AF_INET (2) addresses of four bytes. */
#define BSD_AF_INET 2
#define IPV4_ADDRESS_BYTES 4

typedef struct DatabaseStorage
{
    char data[DATABASE_BYTES];
    size_t used;
    char *aliases[DATABASE_LIST_MAX + 1], *addresses[DATABASE_LIST_MAX + 1];
} DatabaseStorage;

static char *database_copy(DatabaseStorage *storage, const void *data, size_t size)
{
    if (size > sizeof storage->data - storage->used)
        return NULL;
    char *result = storage->data + storage->used;
    memcpy(result, data, size);
    storage->used += size;
    return result;
}

static int database_aliases(DatabaseStorage *storage, char **aliases)
{
    unsigned count = 0;
    for (; aliases && aliases[count]; count++)
    {
        if (count == DATABASE_LIST_MAX ||
            !(storage->aliases[count] =
                  database_copy(storage, aliases[count], strlen(aliases[count]) + 1)))
            return 0;
    }
    storage->aliases[count] = NULL;
    return 1;
}

static int copy_host(struct hostent *host, LxHostSocketHost *result)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    if (!host)
    {
        ResolverError = h_errno > 0 ? h_errno : RESOLVER_NO_RECOVERY;
        return 0;
    }
    storage.used = 0;
    ResolverError = RESOLVER_NO_RECOVERY;
    if (host->h_addrtype != AF_INET || host->h_length != IPV4_ADDRESS_BYTES)
        return 0;
    char *name = database_copy(&storage, host->h_name, strlen(host->h_name) + 1);
    if (!name || !database_aliases(&storage, host->h_aliases))
        return 0;
    unsigned count = 0;
    for (; host->h_addr_list[count]; count++)
    {
        if (count == DATABASE_LIST_MAX ||
            !(storage.addresses[count] =
                  database_copy(&storage, host->h_addr_list[count], IPV4_ADDRESS_BYTES)))
            return 0;
    }
    storage.addresses[count] = NULL;
    *result = (LxHostSocketHost){name, storage.aliases, BSD_AF_INET, IPV4_ADDRESS_BYTES,
                                 storage.addresses};
    ResolverError = 0;
    return 1;
}

int lx_host_socket_host(const char *name, LxHostSocketHost *result)
{
    if (!name || !result)
    {
        ResolverError = RESOLVER_NO_RECOVERY;
        errno = EFAULT;
        return 0;
    }
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_host(gethostbyname(name), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_reverse(uint32_t address, LxHostSocketHost *result)
{
    if (!result)
    {
        ResolverError = RESOLVER_NO_RECOVERY;
        errno = EFAULT;
        return 0;
    }
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_host(gethostbyaddr(&address, IPV4_ADDRESS_BYTES, AF_INET), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_resolver_error(void)
{
    return ResolverError;
}

int lx_host_socket_protocol(const char *name, int number, char **official, char ***aliases,
                            int *protocol)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    pthread_mutex_lock(&DatabaseMutex);
    struct protoent *p = name ? getprotobyname(name) : getprotobynumber(number);
    storage.used = 0;
    int ok = p && (*official = database_copy(&storage, p->p_name, strlen(p->p_name) + 1)) &&
             database_aliases(&storage, p->p_aliases);
    if (ok)
    {
        *aliases = storage.aliases;
        *protocol = p->p_proto;
    }
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

static int copy_service(struct servent *service, LxHostSocketService *result)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    storage.used = 0;
    if (!service)
        return 0;
    char *name = database_copy(&storage, service->s_name, strlen(service->s_name) + 1);
    char *protocol = database_copy(&storage, service->s_proto, strlen(service->s_proto) + 1);
    if (!name || !protocol || !database_aliases(&storage, service->s_aliases))
        return 0;
    *result = (LxHostSocketService){name, storage.aliases, service->s_port, protocol};
    return 1;
}

int lx_host_socket_service_port(int port, const char *protocol, LxHostSocketService *result)
{
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_service(getservbyport(port, protocol), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_service(const char *name, const char *protocol, LxHostSocketService *result)
{
    pthread_mutex_lock(&DatabaseMutex);
    int ok = name && copy_service(getservbyname(name, protocol), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}
