/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hex components of /@2ine-image/, /@2ine-device/ and /@2ine-media/ virtual paths. */
#include "storage/container.h"
#include "storage/image_path.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void check_string(char *actual, const char *expected)
{
    CHECK(actual && !strcmp(actual, expected));
    free(actual);
}

static void check_invalid(char *actual)
{
    CHECK(!actual && errno == EINVAL);
}

/* prefix followed by count copies of "61" and suffix. */
static char *repeated(const char *prefix, size_t count, const char *suffix)
{
    size_t start = strlen(prefix);
    char *path = malloc(start + count * 2 + strlen(suffix) + 1);
    CHECK(path);
    strcpy(path, prefix);
    for (size_t i = 0; i < count; i++)
        memcpy(path + start + i * 2, "61", 2);
    strcpy(path + start + count * 2, suffix);
    return path;
}

static void image_paths(void)
{
    check_string(tb_image_root("/a b\xff"), "/@2ine-image/2f612062ff");
    check_string(tb_image_device_root("/c"), "/@2ine-device/2f63");
    check_string(tb_image_source("/@2ine-image/2f612062ff/MEMBER"), "/a b\xff");
    /* Sources are non-empty, lowercase, whole bytes and never contain NUL. */
    check_invalid(tb_image_source("/@2ine-image/"));
    check_invalid(tb_image_source("/@2ine-image//MEMBER"));
    check_invalid(tb_image_source("/@2ine-image/2F61"));
    check_invalid(tb_image_source("/@2ine-image/2f6"));
    check_invalid(tb_image_source("/@2ine-image/2f00"));
    check_invalid(tb_image_source("/@2ine-image/2g"));
    /* At most 65536 hex digits. */
    char *path = repeated("/@2ine-image/", 32768, "/MEMBER");
    char *source = tb_image_source(path);
    CHECK(source && strlen(source) == 32768 && source[0] == 'a');
    free(source);
    free(path);
    path = repeated("/@2ine-image/", 32769, "");
    check_invalid(tb_image_source(path));
    free(path);
}

/* Control files: magic, kind, read-only flag, little-endian length, path. */
static void device_controls(void)
{
    size_t size = 0;
    unsigned char *bytes = tb_image_device_control("floppy", 0, "/x.img", &size);
    static const unsigned char floppy[] = "2INEMED1\x02\x00\x00\x00\x06\x00\x00\x00/x.img";
    CHECK(bytes && size == sizeof floppy - 1 && !memcmp(bytes, floppy, size));
    free(bytes);
    /* CD devices are read-only whatever the slot says; an ejected slot names no path. */
    bytes = tb_image_device_control("cdrom", 0, NULL, &size);
    CHECK(bytes && size == 16 && bytes[8] == 1 && bytes[9] == 1 && !bytes[12]);
    free(bytes);
    bytes = tb_image_device_control("disk", 1, "", &size);
    CHECK(bytes && size == 16 && bytes[8] == 3 && bytes[9] == 1);
    free(bytes);
    char *path = repeated("", 16385, "");
    errno = 0;
    CHECK(!tb_image_device_control("disk", 0, path, &size) && errno == ENAMETOOLONG);
    path[32768] = 0;
    bytes = tb_image_device_control("disk", 0, path, &size);
    CHECK(bytes && size == 16 + 32768);
    free(bytes);
    free(path);
}

static void media_paths(void)
{
    check_string(tb_media_child("/@2ine-media/2f78!", "AB\xff"), "/@2ine-media/2f78!4142ff");
    check_string(tb_media_label("/@2ine-media/2f78!4142"), "/x \xe2\x96\xb8 AB");
    /* An empty component names the container's root. */
    check_string(tb_media_member_name("/@2ine-media/2f78!"), "");
    check_invalid(tb_media_label("/@2ine-media/2F78!4142"));
    check_invalid(tb_media_label("/@2ine-media/2f7!4142"));
    check_invalid(tb_media_member_name("/@2ine-media/2f78!4100"));
    check_invalid(tb_media_member_name("/@2ine-media/2f78!41g2"));
    /* Member components have no length limit. */
    char *path = repeated("/@2ine-media/2f78!", 35000, "");
    char *member = tb_media_member_name(path);
    CHECK(member && strlen(member) == 35000 && member[0] == 'a');
    free(member);
    free(path);
}

int main(void)
{
    image_paths();
    device_controls();
    media_paths();
    puts("virtual path encoding passed");
    return 0;
}
