/* Public domain. No rights reserved. Wholly AI-authored. */
/* File locks serialize mailbox records across independent worker processes. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "hal/host_mailbox.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
enum
{
    MAILBOX_LIMIT = 4 * 1024 * 1024,
    MAILBOX_MAGIC = 0x324d424c
};
typedef struct
{
    uint32_t magic, owner, order, count, next;
} Header;
typedef struct
{
    uint32_t size, priority, token;
} Record;
struct LxHostMailbox
{
    int fd, owner, lifetime;
    char *path;
    pthread_mutex_t mutex;
};
static void unlock(LxHostMailbox *mailbox)
{
    flock(mailbox->fd, LOCK_UN);
    pthread_mutex_unlock(&mailbox->mutex);
}
static unsigned char *snapshot(LxHostMailbox *mailbox, size_t *size)
{
    pthread_mutex_lock(&mailbox->mutex);
    if (flock(mailbox->fd, LOCK_EX) < 0)
    {
        pthread_mutex_unlock(&mailbox->mutex);
        return NULL;
    }
    struct stat st;
    if (fstat(mailbox->fd, &st) || st.st_size < (off_t)sizeof(Header) || st.st_size > MAILBOX_LIMIT)
        goto failed;
    *size = st.st_size;
    unsigned char *bytes = malloc(MAILBOX_LIMIT);
    if (!bytes)
        goto failed;
    if (pread(mailbox->fd, bytes, *size, 0) != (ssize_t)*size ||
        ((Header *)bytes)->magic != MAILBOX_MAGIC)
    {
        free(bytes);
        goto failed;
    }
    return bytes;
failed:
    unlock(mailbox);
    return NULL;
}
static int commit(LxHostMailbox *mailbox, void *data, size_t size)
{
    return pwrite(mailbox->fd, data, size, 0) == (ssize_t)size && !ftruncate(mailbox->fd, size)
               ? 0
               : -1;
}
int lx_host_mailbox_open(const char *directory, const char *name, int create, unsigned order,
                         LxHostMailbox **output, uint32_t *owner)
{
    if (!name || strchr(name, '/') || order > 2)
        return -1;
    char fallback[80];
    if (!directory || !*directory)
    {
        snprintf(fallback, sizeof fallback, "/tmp/2ine-%lu", (unsigned long)getuid());
        directory = fallback;
    }
    if (mkdir(directory, 0700) && errno != EEXIST)
        return -1;
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = malloc(length);
    if (!path)
        return -1;
    snprintf(path, length, "%s/%s", directory, name);
    int lifetime = -1;
    if (create)
    {
        char *lockpath = malloc(length + 6);
        if (!lockpath)
        {
            free(path);
            return -1;
        }
        snprintf(lockpath, length + 6, "%s.lock", path);
        lifetime = open(lockpath, O_RDWR | O_CLOEXEC | O_CREAT, 0600);
        free(lockpath);
        if (lifetime < 0 || flock(lifetime, LOCK_EX | LOCK_NB))
        {
            if (lifetime >= 0)
                close(lifetime);
            free(path);
            return 2;
        }
        /* The lifetime lock is released by the kernel even after a crash. */
        unlink(path);
    }
    int fd = open(path, O_RDWR | O_CLOEXEC | (create ? O_CREAT | O_EXCL : 0), 0600);
    if (fd < 0)
    {
        int result = errno == ENOENT ? 1 : errno == EEXIST ? 2 : -1;
        if (lifetime >= 0)
            close(lifetime);
        free(path);
        return result;
    }
    Header header = {MAILBOX_MAGIC, (uint32_t)getpid(), order, 0, 1};
    int valid = create ? pwrite(fd, &header, sizeof header, 0) == sizeof header
                       : pread(fd, &header, sizeof header, 0) == sizeof header &&
                             header.magic == MAILBOX_MAGIC;
    LxHostMailbox *mailbox = valid ? calloc(1, sizeof *mailbox) : NULL;
    if (!mailbox || pthread_mutex_init(&mailbox->mutex, NULL))
    {
        free(mailbox);
        close(fd);
        if (lifetime >= 0)
            close(lifetime);
        if (create)
            unlink(path);
        free(path);
        return -1;
    }
    mailbox->fd = fd;
    mailbox->lifetime = lifetime;
    mailbox->path = path;
    mailbox->owner = create;
    *owner = header.owner;
    *output = mailbox;
    return 0;
}
void lx_host_mailbox_close(LxHostMailbox *mailbox)
{
    if (mailbox->owner)
        unlink(mailbox->path);
    close(mailbox->fd);
    if (mailbox->lifetime >= 0)
        close(mailbox->lifetime);
    pthread_mutex_destroy(&mailbox->mutex);
    free(mailbox->path);
    free(mailbox);
}
int lx_host_mailbox_send(LxHostMailbox *mailbox, const void *data, size_t size, unsigned priority)
{
    size_t used;
    unsigned char *bytes = snapshot(mailbox, &used);
    if (!bytes)
        return -1;
    int result = -1;
    if (used <= MAILBOX_LIMIT - sizeof(Record) && size <= MAILBOX_LIMIT - used - sizeof(Record))
    {
        Header *header = (void *)bytes;
        Record record = {(uint32_t)size, priority, header->next++};
        memcpy(bytes + used, &record, sizeof record);
        memcpy(bytes + used + sizeof record, data, size);
        header->count++;
        result = commit(mailbox, bytes, used + sizeof record + size);
    }
    free(bytes);
    unlock(mailbox);
    return result;
}
int lx_host_mailbox_receive(LxHostMailbox *mailbox, void *data, size_t capacity, size_t *size,
                            unsigned *priority, uint32_t *token, int peek)
{
    size_t used;
    unsigned char *bytes = snapshot(mailbox, &used);
    if (!bytes)
        return -1;
    Header *header = (void *)bytes;
    int result = header->count ? (*token ? (peek ? 3 : 2) : -1) : 1;
    size_t selected = 0, at = sizeof *header;
    Record chosen = {0}, cursor = {0};
    if (peek && *token)
    {
        size_t scan = sizeof *header;
        while (scan < used)
        {
            Record record;
            if (used - scan < sizeof record)
                break;
            memcpy(&record, bytes + scan, sizeof record);
            if (record.size > used - scan - sizeof record)
                break;
            if (record.token == *token)
            {
                cursor = record;
                break;
            }
            scan += sizeof record + record.size;
        }
        if (!cursor.token)
        {
            free(bytes);
            unlock(mailbox);
            return 2;
        }
    }
    while (at < used)
    {
        Record record;
        if (used - at < sizeof record)
            break;
        memcpy(&record, bytes + at, sizeof record);
        if (record.size > used - at - sizeof record)
            break;
        int eligible = !*token || (!peek && *token == record.token);
        if (peek && *token)
            eligible = header->order == 1 ? record.token < cursor.token
                       : header->order == 2
                           ? record.priority < cursor.priority ||
                                 (record.priority == cursor.priority && record.token > cursor.token)
                           : record.token > cursor.token;
        if (eligible && (!selected || header->order == 1 ||
                         (header->order == 2 && record.priority > chosen.priority)))
        {
            selected = at;
            chosen = record;
        }
        at += sizeof record + record.size;
    }
    if (at == used && selected && chosen.size <= capacity)
    {
        memcpy(data, bytes + selected + sizeof chosen, chosen.size);
        *size = chosen.size;
        *priority = chosen.priority;
        *token = chosen.token;
        result = 0;
        if (!peek)
        {
            size_t end = selected + sizeof chosen + chosen.size;
            memmove(bytes + selected, bytes + end, used - end);
            header->count--;
            result = commit(mailbox, bytes, used - sizeof chosen - chosen.size);
        }
    }
    free(bytes);
    unlock(mailbox);
    return result;
}
int lx_host_mailbox_count(LxHostMailbox *mailbox, uint32_t *count, int purge)
{
    size_t size;
    unsigned char *bytes = snapshot(mailbox, &size);
    if (!bytes)
        return -1;
    Header *header = (void *)bytes;
    *count = header->count;
    int result = 0;
    if (purge)
    {
        header->count = 0;
        result = commit(mailbox, bytes, sizeof *header);
    }
    free(bytes);
    unlock(mailbox);
    return result;
}

int lx_host_mailbox_revision(LxHostMailbox *mailbox, uint32_t *revision)
{
    pthread_mutex_lock(&mailbox->mutex);
    if (flock(mailbox->fd, LOCK_SH))
    {
        pthread_mutex_unlock(&mailbox->mutex);
        return -1;
    }
    Header header;
    int result = pread(mailbox->fd, &header, sizeof header, 0) == sizeof header &&
                         header.magic == MAILBOX_MAGIC
                     ? 0
                     : -1;
    if (!result)
        *revision = header.next;
    unlock(mailbox);
    return result;
}
