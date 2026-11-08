/* Public domain. No rights reserved. Wholly AI-authored. */
/* Session-private bounded registry. Lifetime locks reject stale/PID-reused members. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "hal/common/desktop_cycle.h"
#include "hal/common/desktop_lease.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MEMBER_LIMIT = 64,
    WINDOW_LIMIT = 256,
    MAGIC = 0x32435943
};

typedef struct Member
{
    uint64_t token;
    uint64_t request_serial;
    uint32_t request, blocked, pid;
} Member;

typedef struct Registry
{
    uint32_t magic, version;
    uint64_t next, active_member, serial;
    uint32_t active_window, intercepted;
    Member members[MEMBER_LIMIT];
    LxDesktopCycleWindow windows[WINDOW_LIMIT];
} Registry;

struct LxDesktopCycle
{
    int directory, guard;
    char *path;
    uint64_t token;
    LxDesktopLease *lease;
};

static void lease_name(char name[32], uint64_t token)
{
    snprintf(name, 32, "member-%016llx", (unsigned long long)token);
}

static int live(LxDesktopCycle *cycle, uint64_t token)
{
    if (token == cycle->token)
        return 1;
    char name[32];
    lease_name(name, token);
    LxDesktopLease *probe = lx_desktop_lease_open(cycle->path, name);
    if (!probe)
        return -1;
    int claimed = lx_desktop_lease_claim(probe);
    if (claimed == 1)
    {
        char file[64];
        snprintf(file, sizeof file, "desktop-%s.leader", name);
        unlinkat(cycle->directory, file, 0);
    }
    lx_desktop_lease_close(probe);
    return claimed < 0 ? -1 : claimed != 1;
}

static Registry *snapshot(LxDesktopCycle *cycle)
{
    if (!cycle || flock(cycle->guard, LOCK_EX | LOCK_NB))
        return NULL;
    Registry *value = calloc(1, sizeof *value);
    int fd = openat(cycle->directory, "registry", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (!value)
        goto failed;
    if (fd < 0 && errno == ENOENT)
    {
        value->magic = MAGIC;
        value->version = 1;
        value->next = 1;
    }
    else
    {
        struct stat info;
        if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size != sizeof *value ||
            read(fd, value, sizeof *value) != sizeof *value || value->magic != MAGIC ||
            value->version != 1)
            goto failed;
    }
    if (fd >= 0)
        close(fd);
    fd = -1;
    for (unsigned i = 0; i < MEMBER_LIMIT; i++)
        if (value->members[i].token)
        {
            int alive = live(cycle, value->members[i].token);
            if (alive < 0)
                goto failed;
            if (alive)
                continue;
            uint64_t dead = value->members[i].token;
            memset(&value->members[i], 0, sizeof value->members[i]);
            for (unsigned j = 0; j < WINDOW_LIMIT; j++)
                if (value->windows[j].member == dead)
                    memset(&value->windows[j], 0, sizeof value->windows[j]);
        }
    return value;
failed:
    if (fd >= 0)
        close(fd);
    free(value);
    flock(cycle->guard, LOCK_UN);
    return NULL;
}

static int finish(LxDesktopCycle *cycle, Registry *value, int save)
{
    int ok = 1;
    if (save)
    {
        char name[48];
        snprintf(name, sizeof name, "registry-%016llx.tmp", (unsigned long long)cycle->token);
        unlinkat(cycle->directory, name, 0);
        int fd = openat(cycle->directory, name,
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        ok = fd >= 0 && write(fd, value, sizeof *value) == sizeof *value;
        if (fd >= 0)
            close(fd);
        if (ok)
            ok = !renameat(cycle->directory, name, cycle->directory, "registry");
        if (!ok)
            unlinkat(cycle->directory, name, 0);
    }
    free(value);
    flock(cycle->guard, LOCK_UN);
    return ok;
}

static Member *member(Registry *value, uint64_t token, int create)
{
    Member *empty = NULL;
    for (unsigned i = 0; i < MEMBER_LIMIT; i++)
    {
        if (value->members[i].token == token)
            return &value->members[i];
        if (!value->members[i].token)
            empty = &value->members[i];
    }
    if (empty && create)
    {
        empty->token = token;
        empty->pid = (uint32_t)getpid();
    }
    return create ? empty : NULL;
}

LxDesktopCycle *lx_desktop_cycle_open(const char *directory, const char *session)
{
    if (!directory || !*directory || !session || !*session || strlen(session) > 64)
        return NULL;
    for (const char *at = session; *at; at++)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '-' || *at == '_'))
            return NULL;
    int parent = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0)
        return NULL;
    char name[96];
    snprintf(name, sizeof name, "desktop-%s.windows", session);
    int made = mkdirat(parent, name, 0700);
    int fd = made && errno != EEXIST
                 ? -1
                 : openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    if (fd < 0)
        return NULL;
    LxDesktopCycle *cycle = calloc(1, sizeof *cycle);
    if (!cycle)
    {
        close(fd);
        return NULL;
    }
    cycle->directory = fd;
    cycle->guard = -1;
    cycle->path = malloc(strlen(directory) + strlen(name) + 2);
    if (!cycle->path)
        goto failed;
    sprintf(cycle->path, "%s/%s", directory, name);
    int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    int good =
        random >= 0 && read(random, &cycle->token, sizeof cycle->token) == sizeof cycle->token;
    if (random >= 0)
        close(random);
    if (!good || !cycle->token)
        goto failed;
    char lease[32];
    lease_name(lease, cycle->token);
    cycle->lease = lx_desktop_lease_open(cycle->path, lease);
    if (!cycle->lease || lx_desktop_lease_claim(cycle->lease) != 1)
        goto failed;
    cycle->guard = openat(fd, "guard", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat info;
    if (cycle->guard < 0 || fstat(cycle->guard, &info) || !S_ISREG(info.st_mode))
        goto failed;
    return cycle;
failed:
    lx_desktop_cycle_close(cycle);
    return NULL;
}

int lx_desktop_cycle_publish(LxDesktopCycle *cycle, const uint32_t *windows, size_t count,
                             int blocked)
{
    if (count > WINDOW_LIMIT)
        return 0;
    Registry *value = snapshot(cycle);
    if (!value)
        return 0;
    Member *self = member(value, cycle->token, 1);
    int ok = self != NULL;
    if (self)
        self->blocked = !!blocked;
    for (unsigned i = 0; i < WINDOW_LIMIT; i++)
        if (value->windows[i].member == cycle->token)
        {
            size_t j = 0;
            while (j < count && windows[j] != value->windows[i].window)
                j++;
            if (j == count)
                memset(&value->windows[i], 0, sizeof value->windows[i]);
        }
    for (size_t j = 0; ok && j < count; j++)
    {
        LxDesktopCycleWindow *empty = NULL;
        int found = 0;
        for (unsigned i = 0; i < WINDOW_LIMIT; i++)
        {
            LxDesktopCycleWindow *entry = &value->windows[i];
            if (entry->member == cycle->token && entry->window == windows[j])
                found = 1;
            if (!entry->member)
                empty = entry;
        }
        if (!found && windows[j] && empty)
            *empty = (LxDesktopCycleWindow){cycle->token, value->next++, windows[j]};
        else if (!found)
            ok = 0;
    }
    return finish(cycle, value, ok) && ok;
}

int lx_desktop_cycle_focus(LxDesktopCycle *cycle, uint32_t window)
{
    Registry *value = snapshot(cycle);
    if (!value)
        return 0;
    value->active_member = cycle->token;
    value->active_window = window;
    return finish(cycle, value, 1);
}

int lx_desktop_cycle_request(LxDesktopCycle *cycle, uint32_t current, int reverse,
                             uint32_t *target_pid)
{
    Registry *value = snapshot(cycle);
    if (!value)
        return -1;
    int blocked = 0;
    for (unsigned i = 0; i < MEMBER_LIMIT; i++)
        blocked |= value->members[i].blocked;
    LxDesktopCycleWindow target;
    uint64_t origin = value->active_member ? value->active_member : cycle->token;
    uint32_t window = value->active_window ? value->active_window : current;
    int found = !blocked && lx_desktop_cycle_next(value->windows, WINDOW_LIMIT, origin, window,
                                                  reverse, &target);
    Member *owner = found ? member(value, target.member, 0) : NULL;
    value->intercepted = owner != NULL || blocked;
    if (blocked)
        value->serial++; /* Cancel any request made before the modal loop. */
    if (owner)
    {
        if (target_pid)
            *target_pid = owner->pid;
        owner->request = target.window;
        owner->request_serial = ++value->serial;
        value->active_member = target.member;
        value->active_window = target.window;
    }
    int handled = owner != NULL;
    int ok = finish(cycle, value, 1);
    return ok ? (handled ? 1 : blocked ? 2 : 0) : -1;
}

int lx_desktop_cycle_keyup(LxDesktopCycle *cycle)
{
    Registry *value = snapshot(cycle);
    if (!value)
        return 0;
    int intercepted = value->intercepted != 0;
    value->intercepted = 0;
    return finish(cycle, value, intercepted) && intercepted;
}

uint32_t lx_desktop_cycle_receive(LxDesktopCycle *cycle)
{
    Registry *value = snapshot(cycle);
    if (!value)
        return 0;
    Member *self = member(value, cycle->token, 0);
    uint32_t pending = self ? self->request : 0;
    uint32_t request = self && self->request_serial == value->serial &&
                               value->active_member == cycle->token &&
                               value->active_window == pending
                           ? pending
                           : 0;
    if (self)
        self->request = 0;
    return finish(cycle, value, pending != 0) ? request : 0;
}

void lx_desktop_cycle_close(LxDesktopCycle *cycle)
{
    if (!cycle)
        return;
    if (cycle->guard >= 0)
    {
        Registry *value = snapshot(cycle);
        if (value)
        {
            Member *self = member(value, cycle->token, 0);
            if (self)
                memset(self, 0, sizeof *self);
            for (unsigned i = 0; i < WINDOW_LIMIT; i++)
                if (value->windows[i].member == cycle->token)
                    memset(&value->windows[i], 0, sizeof value->windows[i]);
            finish(cycle, value, 1);
        }
        close(cycle->guard);
    }
    if (cycle->lease)
    {
        char name[32], file[64];
        lease_name(name, cycle->token);
        snprintf(file, sizeof file, "desktop-%s.leader", name);
        unlinkat(cycle->directory, file, 0);
    }
    lx_desktop_lease_close(cycle->lease);
    close(cycle->directory);
    free(cycle->path);
    free(cycle);
}
