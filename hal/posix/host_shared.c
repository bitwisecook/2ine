/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared backing for guest address spaces, without guest ABI or OS/2 policy. */
#define _GNU_SOURCE 1
#include "hal/host_shared.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
struct LxHostShared
{
    int fd, lifetime;
    size_t size;
    char *directory, *name;
};

typedef struct SharedView
{
    void *address;
    int lifetime;
    struct SharedView *next;
} SharedView;
static SharedView *views;
static pthread_mutex_t view_mutex = PTHREAD_MUTEX_INITIALIZER;

LxHostSharedResult lx_host_shared_open(const char *name, size_t size, LxHostShared **output)
{
    *output = NULL;
    if (!name || name[0] != '/' || strchr(name + 1, '/'))
        return LX_HOST_SHARED_DENIED;
    const char *directory = getenv("TWOINE_IPC_DIR");
    char fallback[80];
    if (!directory || !*directory)
    {
        snprintf(fallback, sizeof fallback, "/tmp/2ine-%lu", (unsigned long)getuid());
        directory = fallback;
    }
    if (mkdir(directory, 0700) && errno != EEXIST)
        return LX_HOST_SHARED_DENIED;
    size_t capacity = strlen(directory) + strlen(name) + 16;
    char *path = malloc(capacity);
    LxHostShared *s = calloc(1, sizeof *s);
    if (!s || !path)
    {
        free(s);
        free(path);
        return LX_HOST_SHARED_NOMEM;
    }
    snprintf(path, capacity, "%s/%s.open", directory, name + 1);
    int operation = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (operation < 0 || flock(operation, LOCK_EX))
    {
        if (operation >= 0)
            close(operation);
        free(path);
        free(s);
        return LX_HOST_SHARED_DENIED;
    }
    snprintf(path, capacity, "%s/%s.live", directory, name + 1);
    int lifetime = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    LxHostSharedResult result = LX_HOST_SHARED_DENIED;
    int fd = -1;
    if (lifetime < 0)
        goto done;
    /* Shared locks are live handles/views; only an unreferenced object can reset. */
    const int inactive = flock(lifetime, LOCK_EX | LOCK_NB) == 0;
    if ((!size && inactive) || (size && !inactive))
    {
        result = size ? LX_HOST_SHARED_EXISTS : LX_HOST_SHARED_MISSING;
        goto done;
    }
    /* Ordinary files preserve logical byte sizes on hosts whose POSIX shm
       objects report VM-page-rounded sizes. Live views retain retired inodes. */
    snprintf(path, capacity, "%s/%s.data", directory, name + 1);
    if (size)
        unlink(path);
    fd = open(path, O_RDWR | O_CLOEXEC | (size ? O_CREAT | O_EXCL : 0), 0600);
    if (fd < 0)
    {
        result = errno == ENOENT ? LX_HOST_SHARED_MISSING : LX_HOST_SHARED_DENIED;
        goto done;
    }
    struct stat info;
    if ((size && ftruncate(fd, (off_t)size)) || fstat(fd, &info) || info.st_size <= 0 ||
        (uint64_t)info.st_size > SIZE_MAX || flock(lifetime, LOCK_SH))
        goto done;
    s->directory = strdup(directory);
    s->name = strdup(name + 1);
    if (!s->directory || !s->name)
        goto done;
    s->fd = fd;
    s->lifetime = lifetime;
    s->size = (size_t)info.st_size;
    *output = s;
    result = LX_HOST_SHARED_OK;
done:
    free(path);
    close(operation);
    if (result != LX_HOST_SHARED_OK)
    {
        if (fd >= 0)
            close(fd);
        if (lifetime >= 0)
            close(lifetime);
        free(s->directory);
        free(s->name);
        free(s);
    }
    return result;
}

void *lx_host_shared_map(LxHostShared *s, void *hint, int exact)
{
    void *view = mmap(hint, s->size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (view == MAP_FAILED)
        return NULL;
    if (exact && view != hint)
    {
        munmap(view, s->size);
        return NULL;
    }
    SharedView *entry = malloc(sizeof *entry);
    int reference = fcntl(s->lifetime, F_DUPFD_CLOEXEC, 0);
    if (!entry || reference < 0)
    {
        if (reference >= 0)
            close(reference);
        free(entry);
        munmap(view, s->size);
        return NULL;
    }
    entry->address = view;
    entry->lifetime = reference;
    pthread_mutex_lock(&view_mutex);
    entry->next = views;
    views = entry;
    pthread_mutex_unlock(&view_mutex);
    return view;
}
void lx_host_shared_unmap(void *view, size_t size)
{
    munmap(view, size);
    pthread_mutex_lock(&view_mutex);
    SharedView **slot = &views;
    while (*slot && (*slot)->address != view)
        slot = &(*slot)->next;
    if (*slot)
    {
        SharedView *entry = *slot;
        *slot = entry->next;
        close(entry->lifetime);
        free(entry);
    }
    pthread_mutex_unlock(&view_mutex);
}
void lx_host_shared_close(LxHostShared *s)
{
    close(s->fd);
    close(s->lifetime);
    free(s->directory);
    free(s->name);
    free(s);
}
size_t lx_host_shared_size(const LxHostShared *s)
{
    return s->size;
}

/* Fixed wire rows keep i386 and 64-bit workers on the same coordination file. */
enum
{
    SLOT_NAME = 128,
    SLOT_ROW = 160,
    SLOT_LIMIT = 4096
};
static uint64_t slot_word(const unsigned char *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof value);
    return value;
}
static void store_slot(unsigned char *p, uint64_t value)
{
    memcpy(p, &value, sizeof value);
}
int lx_host_shared_slot(LxHostShared *s, uint64_t first, uint64_t limit, uint64_t alignment,
                        uint64_t *slot)
{
    if (!alignment || (alignment & (alignment - 1)) || first >= limit ||
        strlen(s->name) >= SLOT_NAME)
        return 0;
    size_t length = strlen(s->directory) + SLOT_NAME + 32;
    char *path = malloc(length);
    unsigned char *rows = calloc(SLOT_LIMIT, SLOT_ROW);
    if (!path || !rows)
    {
        free(path);
        free(rows);
        return 0;
    }
    snprintf(path, length, "%s/shared-slots", s->directory);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600), locked = 0, result = 0;
    if (fd < 0 || flock(fd, LOCK_EX))
        goto done;
    locked = 1;
    struct stat table, object;
    if (fstat(fd, &table) || table.st_size < 0 || table.st_size % SLOT_ROW ||
        table.st_size > SLOT_LIMIT * SLOT_ROW || fstat(s->fd, &object))
        goto done;
    size_t count = table.st_size / SLOT_ROW;
    if (pread(fd, rows, table.st_size, 0) != table.st_size)
        goto done;
    uint64_t identity = (uint64_t)object.st_ino ^ ((uint64_t)object.st_dev << 32);
    size_t vacant = count;
    for (size_t i = 0; i < count; i++)
    {
        unsigned char *row = rows + i * SLOT_ROW;
        if (!row[0])
        {
            vacant = i;
            continue;
        }
        if (!memchr(row, 0, SLOT_NAME))
            goto done;
        uint64_t row_address = slot_word(row + 136), row_size = slot_word(row + 144);
        if (row_address < first || row_address >= limit || row_address % alignment || !row_size ||
            row_size > limit - row_address)
            goto done;
        if (!strcmp((const char *)row, s->name) && slot_word(row + 128) == identity)
        {
            uint64_t address = slot_word(row + 136), size = slot_word(row + 144);
            if (address >= first && address % alignment == 0 && address < limit &&
                size <= limit - address && size == s->size)
            {
                *slot = address;
                result = 1;
            }
            goto done;
        }
        snprintf(path, length, "%s/%s.live", s->directory, row);
        int live = open(path, O_RDWR | O_CLOEXEC);
        if (live < 0 && errno != ENOENT)
            goto done;
        int inactive = live < 0;
        if (live >= 0)
        {
            inactive = flock(live, LOCK_EX | LOCK_NB) == 0;
            if (!inactive && errno != EWOULDBLOCK && errno != EAGAIN)
            {
                close(live);
                goto done;
            }
        }
        if (inactive)
        {
            memset(row, 0, SLOT_ROW);
            vacant = i;
        }
        if (live >= 0)
            close(live);
    }
    uint64_t candidate = (first + alignment - 1) & ~(alignment - 1);
    for (size_t i = 0; i < count; i++)
    {
        unsigned char *row = rows + i * SLOT_ROW;
        uint64_t address = slot_word(row + 136), size = slot_word(row + 144);
        if (row[0] && candidate < address + size && address < candidate + s->size)
        {
            candidate = (address + size + alignment - 1) & ~(alignment - 1);
            i = (size_t)-1;
        }
    }
    if (vacant >= SLOT_LIMIT || candidate >= limit || s->size > limit - candidate)
        goto done;
    unsigned char *row = rows + vacant * SLOT_ROW;
    memset(row, 0, SLOT_ROW);
    strcpy((char *)row, s->name);
    store_slot(row + 128, identity);
    store_slot(row + 136, candidate);
    store_slot(row + 144, s->size);
    if (vacant == count)
        count++;
    if (pwrite(fd, rows, count * SLOT_ROW, 0) != (ssize_t)(count * SLOT_ROW))
        goto done;
    *slot = candidate;
    result = 1;
done:
    if (locked)
        flock(fd, LOCK_UN);
    if (fd >= 0)
        close(fd);
    free(rows);
    free(path);
    return result;
}
