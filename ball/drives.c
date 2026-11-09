/* Public domain. No rights reserved. Wholly AI-authored. */
/* The drive table: folder and device roots and published device control files. */
#include "ball/ball_internal.h"
#include "ball/drives.h"

static char *tb_ball_device_root(Context *c, const char *letter, const char *kind,
                                 const char *image, int readonly)
{
    char *directory = tb_ball_join(c, c->ball, "devices");
    char *control = tb_ball_join(c, directory, tb_ball_format(c, "%s.media", letter));
    if (!directory || !control || !tb_mkdirs(directory))
    {
        tb_ball_fail(c, "Cannot create media device directory: %s", strerror(errno));
        return NULL;
    }
    size_t n = image ? strlen(image) : 0;
    if (n > 32768)
    {
        tb_ball_fail(c, "Image path is too long");
        return NULL;
    }
    unsigned char *bytes = (unsigned char *)tb_ball_keep(c, calloc(1, n + 16));
    if (!bytes)
        return NULL;
    memcpy(bytes, "2INEMED1", 8);
    bytes[8] = !strcmp(kind, "floppy") ? 2 : !strcmp(kind, "disk") ? 3 : 1;
    bytes[9] = readonly || bytes[8] == 1;
    for (unsigned i = 0; i < 4; i++)
        bytes[12 + i] = (unsigned char)(n >> (8 * i));
    if (n)
        memcpy(bytes + 16, image, n);
    unsigned char *old = (unsigned char *)tb_ball_keep(c, malloc(n + 17));
    if (!old)
        return NULL;
    FILE *file = tb_fopen(control, "rb");
    int same = file && fread(old, 1, n + 17, file) == n + 16 && !memcmp(old, bytes, n + 16);
    if (file)
        fclose(file);
    if (!same && !tb_atomic_write(control, bytes, n + 16))
    {
        tb_ball_fail(c, "Cannot update %s: media: %s", letter, strerror(errno));
        return NULL;
    }
    return tb_ball_keep(c, tb_image_device_root(control));
}

int tb_ball_roots(Context *c)
{
    cJSON *drives = get(c->manifest, "drives");
    if (!cJSON_IsObject(drives) || !get(drives, "C"))
        return tb_ball_fail(c, "Invalid ball.json: missing private C: drive");
    memset(c->roots, 0, sizeof(c->roots));
    memset(c->unavailable, 0, sizeof(c->unavailable));
    cJSON *entry;
    cJSON_ArrayForEach(entry, drives)
    {
        if (!entry->string || strlen(entry->string) != 1 || entry->string[0] < 'A' ||
            entry->string[0] > 'Z' ||
            (!str(entry) && !field(entry, "image") && !field(entry, "device")))
            return tb_ball_fail(c, "Invalid drive mapping");
        const char *image = field(entry, "image"), *kind = field(entry, "device");
        if (kind && strcmp(kind, "cdrom") && strcmp(kind, "floppy") && strcmp(kind, "disk"))
            return tb_ball_fail(c, "Unknown removable device type");
        if (image || kind)
        {
            if (image && !host_absolute(image))
            {
                image = tb_ball_join(c, c->ball, image);
                if (!image)
                    return 0;
            }
            if (entry->string[0] == 'C')
                return tb_ball_fail(c, "C: must remain the ball's private directory");
            if (!kind && image)
            {
                char *raw = tb_ball_keep(c, tb_image_root(image));
                TbImageInfo info;
                kind = raw && tb_image_info(raw, &info)
                           ? (!strcmp(info.filesystem, "CDFS")  ? "cdrom"
                              : !strcmp(info.filesystem, "FAT") ? "floppy"
                                                                : "disk")
                           : "cdrom";
                cJSON_AddStringToObject(entry, "device", kind);
            }
            FILE *media = image && native_host_path(image) && tb_is_file(image)
                              ? tb_fopen(image, "rb")
                              : NULL;
            if (image && !media)
            {
                c->unavailable[entry->string[0] - 'A'] = 1;
                image = NULL;
            }
            if (media)
                fclose(media);
            if (c->readonly_drives)
            {
                char *root = image ? tb_image_root(image) : NULL;
                if (root)
                    c->roots[entry->string[0] - 'A'] = tb_ball_keep(c, root);
                else
                    c->unavailable[entry->string[0] - 'A'] = 1;
                continue;
            }
            c->roots[entry->string[0] - 'A'] = tb_ball_device_root(
                c, entry->string, kind, image, cJSON_IsTrue(get(entry, "readonly")));
            continue;
        }
        const char *value = str(entry);
        char *path;
        if (!*value)
        {
            if (entry->string[0] == 'C')
                return tb_ball_fail(c, "Invalid private C: directory");
            c->unavailable[entry->string[0] - 'A'] = 1;
            continue;
        }
        if (strpbrk(value, "\r\n"))
            return tb_ball_fail(c, "Invalid drive mapping");
        const char *source = host_absolute(value) ? value : tb_ball_join(c, c->ball, value);
        if (!source)
            return 0;
        if (entry->string[0] != 'C')
        {
            errno = 0;
            char *resolved = native_host_path(source) ? tb_canonical(source) : NULL;
            if (!resolved && errno == ENOMEM)
                return tb_ball_fail(c, "Out of memory");
            if (!resolved || !tb_is_dir(resolved) || !tb_directory_readable(resolved))
            {
                free(resolved);
                c->unavailable[entry->string[0] - 'A'] = 1;
                continue;
            }
            path = tb_ball_keep(c, resolved);
        }
        else
            path = tb_ball_resolve_path(c, source, 1);
        if (!path || !tb_is_dir(path) || strpbrk(path, "\r\n"))
            return tb_ball_fail(c, "Drive %s: is not an accessible directory", entry->string);
        c->roots[entry->string[0] - 'A'] = path;
    }
    return !c->error;
}
