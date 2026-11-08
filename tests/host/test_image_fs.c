/* Public domain. No rights reserved. Wholly AI-authored. */
/* Runtime adapter contract against an internally mapped image member. */
#include "host/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "image filesystem check failed at line %d: %s\n", __LINE__, #x);       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int set_medium(const char *control, const char *source)
{
    unsigned char header[16] = "2INEMED1";
    header[8] = 1;
    size_t n = source ? strlen(source) : 0;
    for (unsigned i = 0; i < 4; i++)
        header[12 + i] = (unsigned char)(n >> (8 * i));
    char *temporary = malloc(strlen(control) + 5);
    CHECK(temporary);
    sprintf(temporary, "%s.tmp", control);
    FILE *file = fopen(temporary, "wb");
    CHECK(file);
    CHECK(fwrite(header, 1, 16, file) == 16);
    CHECK(!n || fwrite(source, 1, n, file) == n);
    CHECK(!fclose(file));
    CHECK(!rename(temporary, control));
    free(temporary);
    return 0;
}

static int swap_test(const char *path, const char *control, const char *next)
{
    LxHostFsError error;
    size_t n;
    unsigned char before, after;
    LxHostFile *old = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &error);
    CHECK(old);
    CHECK(lx_host_file_read(old, &before, 1, &n) == LX_HOST_FS_OK && n == 1);
    CHECK(!set_medium(control, NULL));
    CHECK(!lx_host_file_open(path, LX_HOST_FILE_READ, 0, &error) && error == LX_HOST_FS_NOT_READY);
    char *root = malloc(strlen(path) + 1);
    CHECK(root);
    strcpy(root, path);
    char *slash = strchr(root + strlen("/@2ine-device/"), '/');
    CHECK(slash);
    *slash = 0;
    LxHostFileInfo info;
    CHECK(lx_host_path_info(root, &info) == LX_HOST_FS_OK && info.kind == LX_HOST_FILE_DIRECTORY);
    uint64_t total, available;
    CHECK(lx_host_path_space(root, &total, &available) == LX_HOST_FS_NOT_READY);
    CHECK(!lx_host_directory_open(root, &error) && error == LX_HOST_FS_NOT_READY);
    CHECK(!set_medium(control, next));
    LxHostFile *current = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &error);
    CHECK(current);
    CHECK(lx_host_file_read(current, &after, 1, &n) == LX_HOST_FS_OK && n == 1 && after != before);
    uint64_t position;
    CHECK(lx_host_file_seek(old, 0, 0, &position) == LX_HOST_FS_OK);
    CHECK(lx_host_file_read(old, &after, 1, &n) == LX_HOST_FS_OK && n == 1 && after == before);
    lx_host_file_close(old);
    lx_host_file_close(current);
    free(root);
    return 0;
}

static int writable_test(const char *root, const char *control)
{
    char directory[4096], path[4096], moved[4096];
    CHECK(snprintf(directory, sizeof directory, "%s/Example Directory", root) <
          (int)sizeof directory);
    CHECK(snprintf(path, sizeof path, "%s/Example file.txt", directory) < (int)sizeof path);
    CHECK(snprintf(moved, sizeof moved, "%s/Renamed file.txt", root) < (int)sizeof moved);
    uint64_t total, available, initial;
    CHECK(lx_host_path_space(root, &total, &initial) == LX_HOST_FS_OK && initial);
    CHECK(lx_host_path_mkdir(directory) == LX_HOST_FS_OK);
    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(
        path, LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE,
        0, &error);
    CHECK(file);
    size_t actual;
    CHECK(lx_host_file_write(file, "hello", 5, &actual) == LX_HOST_FS_OK && actual == 5);
    uint64_t position;
    CHECK(lx_host_file_seek(file, 10, 0, &position) == LX_HOST_FS_OK);
    CHECK(lx_host_file_write(file, "world", 5, &actual) == LX_HOST_FS_OK && actual == 5);
    CHECK(lx_host_file_resize(file, 13) == LX_HOST_FS_OK);
    LxHostFile *copy = lx_host_file_duplicate(file, &error);
    CHECK(copy);
    CHECK(lx_host_file_seek(copy, 0, 0, &position) == LX_HOST_FS_OK);
    unsigned char data[20] = {0};
    CHECK(lx_host_file_read(copy, data, sizeof data, &actual) == LX_HOST_FS_OK && actual == 13);
    CHECK(!memcmp(data, "hello\0\0\0\0\0wor", 13));
    FILE *descriptor = fopen(control, "rb+");
    CHECK(descriptor);
    CHECK(!fseek(descriptor, 9, SEEK_SET) && fputc(1, descriptor) == 1 && !fclose(descriptor));
    CHECK(lx_host_file_write(file, "x", 1, &actual) == LX_HOST_FS_READ_ONLY && !actual);
    CHECK(lx_host_file_resize(copy, 0) == LX_HOST_FS_READ_ONLY);
    descriptor = fopen(control, "rb+");
    CHECK(descriptor);
    CHECK(!fseek(descriptor, 9, SEEK_SET) && fputc(0, descriptor) == 0 && !fclose(descriptor));
    CHECK(lx_host_file_readonly(file, 1) == LX_HOST_FS_OK);
    CHECK(lx_host_file_write(file, "x", 1, &actual) == LX_HOST_FS_DENIED);
    CHECK(lx_host_file_readonly(file, 0) == LX_HOST_FS_OK);
    lx_host_file_close(file);
    lx_host_file_close(copy);
    CHECK(lx_host_path_move(path, moved) == LX_HOST_FS_OK);
    CHECK(lx_host_path_remove(directory, 1) == LX_HOST_FS_OK);
    CHECK(lx_host_path_info(moved, &(LxHostFileInfo){0}) == LX_HOST_FS_OK);
    CHECK(lx_host_path_remove(moved, 0) == LX_HOST_FS_OK);
    /* An existing HPFS root may move from general space into its reserved
       directory band. That releases sectors; the cycle must not leak space. */
    CHECK(lx_host_path_space(root, &total, &available) == LX_HOST_FS_OK && available >= initial);
    /* Keep one file for independent filesystem readers. */
    file = lx_host_file_open(
        moved, LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE, 0, &error);
    CHECK(file);
    CHECK(lx_host_file_write(file, "floppy write verified", 21, &actual) == LX_HOST_FS_OK &&
          actual == 21);
    lx_host_file_close(file);
    return 0;
}

typedef struct ThreadWrite
{
    LxHostFile *file;
    unsigned char byte;
    int failed;
} ThreadWrite;

static void *thread_write(void *value)
{
    ThreadWrite *write = value;
    for (unsigned i = 0; i < 40; i++)
    {
        size_t actual;
        if (lx_host_file_write(write->file, &write->byte, 1, &actual) || actual != 1)
        {
            write->failed = 1;
            break;
        }
    }
    return NULL;
}

static int fragmentation_test(const char *root)
{
    char path[4096], other[4096], names[4096];
    CHECK(snprintf(path, sizeof path, "%s/FRAGMENT.BIN", root) < (int)sizeof path);
    CHECK(snprintf(other, sizeof other, "%s/INTERLEAVE.BIN", root) < (int)sizeof other);
    CHECK(snprintf(names, sizeof names, "%s/Many Long File Names", root) < (int)sizeof names);
    LxHostFsError error;
    unsigned flags =
        LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE;
    LxHostFile *a = lx_host_file_open(path, flags, 0, &error),
               *b = lx_host_file_open(other, flags, 0, &error);
    CHECK(a && b);
    unsigned char expected[96 * 512], data[512];
    size_t actual;
    for (unsigned i = 0; i < 96; i++)
    {
        memset(data, i, sizeof data);
        memcpy(expected + i * 512, data, 512);
        CHECK(lx_host_file_write(a, data, 512, &actual) == LX_HOST_FS_OK && actual == 512);
        CHECK(lx_host_file_write(b, data, 512, &actual) == LX_HOST_FS_OK && actual == 512);
    }
    lx_host_file_close(b);
    uint64_t position;
    CHECK(lx_host_file_seek(a, 0, 0, &position) == LX_HOST_FS_OK);
    for (unsigned i = 0; i < 96; i++)
    {
        CHECK(lx_host_file_read(a, data, 512, &actual) == LX_HOST_FS_OK && actual == 512);
        CHECK(!memcmp(data, expected + i * 512, 512));
    }
    CHECK(lx_host_file_resize(a, 80 * 512 + 7) == LX_HOST_FS_OK);
    lx_host_file_close(a);
    CHECK(lx_host_path_remove(other, 0) == LX_HOST_FS_OK);
    CHECK(lx_host_path_mkdir(names) == LX_HOST_FS_OK);
    for (unsigned i = 0; i < 90; i++)
    {
        char child[4096];
        CHECK(snprintf(child, sizeof child, "%s/A long name for tree splitting number %04u.txt",
                       names, i) < (int)sizeof child);
        LxHostFile *file = lx_host_file_open(child, flags, 0, &error);
        CHECK(file);
        CHECK(lx_host_file_write(file, "data", 4, &actual) == LX_HOST_FS_OK && actual == 4);
        lx_host_file_close(file);
    }
    CHECK(lx_host_path_remove(names, 1) == LX_HOST_FS_NOT_EMPTY);
    for (unsigned i = 0; i < 90; i++)
    {
        char child[4096];
        snprintf(child, sizeof child, "%s/A long name for tree splitting number %04u.txt", names,
                 i);
        CHECK(lx_host_path_remove(child, 0) == LX_HOST_FS_OK);
    }
    CHECK(lx_host_path_remove(names, 1) == LX_HOST_FS_OK);
    CHECK(snprintf(other, sizeof other, "%s/CONCURRENT.BIN", root) < (int)sizeof other);
    a = lx_host_file_open(other, flags, 0, &error);
    CHECK(a);
    b = lx_host_file_duplicate(a, &error);
    CHECK(b);
    ThreadWrite first = {a, 'A', 0}, second = {b, 'B', 0};
    pthread_t t1, t2;
    CHECK(!pthread_create(&t1, NULL, thread_write, &first));
    CHECK(!pthread_create(&t2, NULL, thread_write, &second));
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(!first.failed && !second.failed);
    CHECK(lx_host_file_seek(a, 0, 0, &position) == LX_HOST_FS_OK);
    CHECK(lx_host_file_read(a, data, sizeof data, &actual) == LX_HOST_FS_OK && actual == 80);
    unsigned na = 0, nb = 0;
    for (size_t i = 0; i < actual; i++)
    {
        na += data[i] == 'A';
        nb += data[i] == 'B';
    }
    CHECK(na == 40 && nb == 40);
    lx_host_file_close(a);
    lx_host_file_close(b);
    return 0;
}

static int metadata_test(const char *root)
{
    char path[4096], renamed[4096];
    CHECK(snprintf(path, sizeof path, "%s/File with attributes.dat", root) < (int)sizeof path);
    CHECK(snprintf(renamed, sizeof renamed, "%s/Renamed attributes.dat", root) <
          (int)sizeof renamed);
    LxHostFsError error;
    size_t actual;
    unsigned flags =
        LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE;
    LxHostFile *file = lx_host_file_open(path, flags, 0, &error);
    CHECK(file);
    unsigned char list[6016] = {0};
    const char name[] = ".LONGEA";
    const unsigned name_size = sizeof name - 1;
    unsigned total = 4 + 5 + name_size + 6000;
    list[0] = total;
    list[1] = total >> 8;
    list[4] = 128;
    list[5] = name_size;
    list[6] = 6000 & 255;
    list[7] = 6000 >> 8;
    memcpy(list + 8, name, sizeof name);
    for (unsigned i = 0; i < 6000; i++)
        list[9 + name_size + i] = i % 251;
    CHECK(lx_host_file_metadata_set(file, list, total) == LX_HOST_FS_OK);
    size_t size = 0;
    CHECK(lx_host_file_metadata_get(file, NULL, 0, &size) == LX_HOST_FS_OK && size == total);
    unsigned char copy[sizeof list];
    CHECK(lx_host_file_metadata_get(file, copy, sizeof copy, &size) == LX_HOST_FS_OK &&
          size == total && !memcmp(copy, list, total));
    CHECK(lx_host_file_write(file, "data", 4, &actual) == LX_HOST_FS_OK && actual == 4);
    CHECK(lx_host_file_metadata_get(file, copy, sizeof copy, &size) == LX_HOST_FS_OK &&
          !memcmp(copy, list, total));
    lx_host_file_close(file);
    CHECK(lx_host_path_move(path, renamed) == LX_HOST_FS_OK);
    file = lx_host_file_open(renamed, LX_HOST_FILE_READ, 0, &error);
    CHECK(file);
    CHECK(lx_host_file_metadata_get(file, copy, sizeof copy, &size) == LX_HOST_FS_OK &&
          size == total && !memcmp(copy, list, total));
    lx_host_file_close(file);
    CHECK(lx_host_path_remove(renamed, 0) == LX_HOST_FS_OK);
    file = lx_host_file_open(path, flags, 0, &error);
    CHECK(file);
    CHECK(lx_host_file_metadata_set(file, list, total) == LX_HOST_FS_OK);
    lx_host_file_close(file);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--metadata"))
        return metadata_test(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--fragmentation"))
        return fragmentation_test(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "--writable"))
        return writable_test(argv[2], argv[3]);
    if (argc == 5 && !strcmp(argv[1], "--swap"))
        return swap_test(argv[2], argv[3], argv[4]);
    CHECK(argc == 3 || argc == 5);
    const char *path = argv[1];
    LxHostFileInfo info;
    CHECK(lx_host_path_info(path, &info) == LX_HOST_FS_OK);
    CHECK(info.kind == LX_HOST_FILE_REGULAR && info.readonly);
    size_t size;
    LxHostFsError error;
    unsigned char *bytes = lx_host_file_snapshot(path, 64 * 1024 * 1024, &size, &error);
    CHECK(bytes && error == LX_HOST_FS_OK);
    FILE *reference = fopen(argv[2], "rb");
    CHECK(reference);
    unsigned char buffer[8192];
    size_t at = 0, n;
    while ((n = fread(buffer, 1, sizeof buffer, reference)))
    {
        CHECK(at + n <= size && !memcmp(buffer, bytes + at, n));
        at += n;
    }
    CHECK(!ferror(reference) && at == size);
    fclose(reference);
    free(bytes);
    LxHostFile *file =
        lx_host_file_open(path, LX_HOST_FILE_READ | LX_HOST_FILE_NOINHERIT, 0, &error);
    CHECK(file);
    CHECK(lx_host_file_info(file, &info) == LX_HOST_FS_OK && info.readonly);
    CHECK(lx_host_file_write(file, "x", 1, &size) == LX_HOST_FS_READ_ONLY && !size);
    CHECK(lx_host_file_resize(file, 0) == LX_HOST_FS_READ_ONLY);
    CHECK(lx_host_file_readonly(file, 0) == LX_HOST_FS_READ_ONLY);
    CHECK(lx_host_file_metadata_set(file, "x", 1) == LX_HOST_FS_READ_ONLY);
    LxHostFile *copy = lx_host_file_duplicate(file, &error);
    CHECK(copy);
    CHECK(lx_host_file_write(copy, "x", 1, &size) == LX_HOST_FS_READ_ONLY);
    uint64_t position;
    CHECK(lx_host_file_seek(copy, 0, 2, &position) == LX_HOST_FS_OK && position == info.size);
    lx_host_file_close(file);
    lx_host_file_close(copy);
    CHECK(!lx_host_file_open(path, LX_HOST_FILE_WRITE, 0, &error) && error == LX_HOST_FS_READ_ONLY);
    CHECK(lx_host_path_remove(path, 0) == LX_HOST_FS_READ_ONLY);
    CHECK(lx_host_path_mkdir(path) == LX_HOST_FS_READ_ONLY);
    LxHostFsError moved_error = lx_host_path_move(path, "unused");
    CHECK(moved_error == LX_HOST_FS_READ_ONLY || moved_error == LX_HOST_FS_CROSS_DEVICE);
    CHECK(lx_host_path_readonly(path, 0) == LX_HOST_FS_READ_ONLY);
    char *parent = lx_host_path_canonical(path);
    CHECK(parent);
    char *slash = strrchr(parent, '/');
    CHECK(slash);
    *slash = 0;
    CHECK(lx_host_path_chdir(parent) == LX_HOST_FS_OK);
    LxHostDirectory *dir = lx_host_directory_open(parent, &error);
    CHECK(dir);
    uint64_t start = lx_host_directory_position(dir);
    const char *name;
    CHECK(lx_host_directory_read(dir, &name, &info, &error) == 1);
    lx_host_directory_seek(dir, start);
    CHECK(lx_host_directory_read(dir, &name, NULL, &error) == 1);
    lx_host_directory_close(dir);
    uint64_t total, available;
    CHECK(lx_host_path_space(parent, &total, &available) == LX_HOST_FS_OK && total && !available);
    LxHostVolumeInfo volume;
    CHECK(lx_host_path_volume(parent, &volume) == LX_HOST_FS_OK);
    CHECK(volume.sector_bytes == (!strcmp(volume.filesystem, "CDFS") ? 2048u : 512u));
    CHECK(volume.cluster_bytes >= volume.sector_bytes &&
          !(volume.cluster_bytes % volume.sector_bytes));
    CHECK(volume.removable ==
          (!strcmp(volume.filesystem, "FAT") || !strcmp(volume.filesystem, "CDFS")));
    if (!strcmp(volume.filesystem, "FAT"))
    {
        CHECK(volume.fat_count == 2 && volume.heads == 2 && volume.sectors_per_track);
    }
    CHECK(volume.readonly &&
          (!strcmp(volume.filesystem, "CDFS") || !strcmp(volume.filesystem, "FAT") ||
           !strcmp(volume.filesystem, "FAT32") || !strcmp(volume.filesystem, "EXFAT") ||
           !strcmp(volume.filesystem, "HPFS")));
    if (argc == 5)
    {
        CHECK(!strcmp(volume.label, argv[3]));
        CHECK(!strcmp(volume.filesystem, argv[4]));
    }
    free(parent);
    return 0;
}
