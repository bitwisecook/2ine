/* Public domain. No rights reserved. Wholly AI-authored. */
/* A process holds one non-inherited lease; the kernel releases it after exit. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "hal/common/desktop_lease.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct LxDesktopLease
{
    int file, owned;
};
LxDesktopLease *lx_desktop_lease_open(const char *directory, const char *session)
{
    if (!directory || !*directory || !session || !*session || strlen(session) > 64)
    {
        errno = EINVAL;
        return NULL;
    }
    for (const char *at = session; *at; at++)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '-' || *at == '_'))
        {
            errno = EINVAL;
            return NULL;
        }
    int parent = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0)
        return NULL;
    char name[96];
    snprintf(name, sizeof name, "desktop-%s.leader", session);
    int file = openat(parent, name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    int error = errno;
    close(parent);
    errno = error;
    if (file < 0)
        return NULL;
    struct stat info;
    if (fstat(file, &info) || !S_ISREG(info.st_mode))
    {
        close(file);
        errno = EINVAL;
        return NULL;
    }
    LxDesktopLease *lease = calloc(1, sizeof *lease);
    if (!lease)
    {
        close(file);
        return NULL;
    }
    lease->file = file;
    return lease;
}
int lx_desktop_lease_claim(LxDesktopLease *lease)
{
    if (!lease)
    {
        errno = EINVAL;
        return -1;
    }
    if (lease->owned)
        return 1;
    if (!flock(lease->file, LOCK_EX | LOCK_NB))
    {
        lease->owned = 1;
        return 1;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
}
void lx_desktop_lease_release(LxDesktopLease *lease)
{
    if (lease && lease->owned)
    {
        flock(lease->file, LOCK_UN);
        lease->owned = 0;
    }
}
void lx_desktop_lease_close(LxDesktopLease *lease)
{
    if (lease)
    {
        close(lease->file);
        free(lease);
    }
}
