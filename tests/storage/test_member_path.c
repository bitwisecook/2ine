/* Public domain. No rights reserved. Wholly AI-authored. */
/* Member path normalisation, alone and through the HPFS, FatFs and built-in FAT volumes. */
#define _POSIX_C_SOURCE 200809L
#include "storage/volume/member_path.h"
#include "storage/image_path.h"
#include "storage/volume.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void normalised(const char *member, const char *expected)
{
    char *actual = tb_member_path(member);
    if (!actual || strcmp(actual, expected))
    {
        fprintf(stderr, "'%s' -> '%s', expected '%s'\n", member, actual ? actual : "(null)",
                expected);
        exit(1);
    }
    free(actual);
}

static void rejected(const char *member, int error)
{
    errno = 0;
    char *actual = tb_member_path(member);
    if (actual || errno != error)
    {
        fprintf(stderr, "'%s' -> '%s' errno %d, expected errno %d\n", member ? member : "(null)",
                actual ? actual : "(null)", errno, error);
        exit(1);
    }
}

/* count components "a/" followed by tail. */
static char *deep(size_t count, const char *tail)
{
    char *path = malloc(count * 2 + strlen(tail) + 1);
    CHECK(path);
    for (size_t i = 0; i < count; i++)
        memcpy(path + i * 2, "a/", 2);
    strcpy(path + count * 2, tail);
    return path;
}

static void normaliser(void)
{
    normalised("", "");
    normalised("/", "");
    normalised("./", "");
    normalised("a/..", "");
    normalised("Dir//Sub/./File.Txt/", "Dir/Sub/File.Txt");
    normalised("/a/b/../c", "a/c");
    normalised("a/b/../../c", "c");
    normalised("...", "...");
    normalised("a/.b/..c", "a/.b/..c");
    rejected(NULL, EINVAL);
    rejected("..", EACCES);
    rejected("a/../../b", EACCES);
    rejected("a\\b", EINVAL);
    rejected("c:", EINVAL);
    /* Invalid characters are reported before an escape through "..". */
    rejected("../a:b", EINVAL);
    char *path = deep(TB_MEMBER_PATH_DEPTH, "");
    char *expected = deep(TB_MEMBER_PATH_DEPTH - 1, "a");
    normalised(path, expected);
    free(path);
    free(expected);
    path = deep(TB_MEMBER_PATH_DEPTH, "b");
    rejected(path, ENAMETOOLONG);
    free(path);
}

static char *member(const char *root, const char *name)
{
    char *path = malloc(strlen(root) + strlen(name) + 2);
    CHECK(path);
    sprintf(path, "%s/%s", root, name);
    return path;
}

static int info_error(const char *root, const char *name)
{
    char *path = member(root, name);
    TbImageInfo info;
    errno = 0;
    int ok = tb_image_info(path, &info);
    free(path);
    return ok ? 0 : errno;
}

/* Every volume implementation resolves members through the shared normaliser. */
static void volume(const char *image, const char *filesystem)
{
    char *root = tb_image_root(image);
    CHECK(root);
    char *directory = member(root, "Sub");
    CHECK(tb_image_mkdir(directory));
    free(directory);
    char *file = member(root, "Sub/./File.Txt");
    CHECK(tb_image_create_file(file, 1));
    free(file);
    TbImageInfo info;
    char *path = member(root, "Sub/../Sub//File.Txt");
    CHECK(tb_image_info(path, &info) && !info.directory);
    CHECK(!strcmp(info.filesystem, filesystem));
    free(path);
    path = member(root, "Sub/..");
    CHECK(tb_image_info(path, &info) && info.directory);
    free(path);
    CHECK(info_error(root, "..") == EACCES);
    CHECK(info_error(root, "Sub/../../Sub") == EACCES);
    CHECK(info_error(root, "Sub\\File.Txt") == EINVAL);
    CHECK(info_error(root, "../a:b") == EINVAL);
    path = deep(TB_MEMBER_PATH_DEPTH, "b");
    CHECK(info_error(root, path) == ENAMETOOLONG);
    free(path);
    free(root);
}

int main(void)
{
    normaliser();
    char directory[] = "/tmp/2ine-member-path-XXXXXX";
    CHECK(mkdtemp(directory));

    static const struct
    {
        const char *name, *create, *filesystem;
        unsigned mib;
    } volumes[] = {{"floppy.img", NULL, "FAT", 0},
                   {"fat32.img", "fat32", "FAT32", 64},
                   {"hpfs.img", "hpfs", "HPFS", 64}};

    for (size_t i = 0; i < sizeof volumes / sizeof volumes[0]; i++)
    {
        char *image = member(directory, volumes[i].name);
        CHECK(volumes[i].create ? tb_image_create_volume(image, volumes[i].create,
                                                         volumes[i].mib * 1024ull * 1024)
                                : tb_image_create_floppy(image, 1440));
        volume(image, volumes[i].filesystem);
        CHECK(!unlink(image));
        free(image);
    }
    CHECK(!rmdir(directory));
    puts("member path tests passed");
    return 0;
}
