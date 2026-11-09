/* Public domain. No rights reserved. Wholly AI-authored. */
/* Internal ZIP operations; included after the ball context and manifest helpers. */
#include "ball/ball_internal.h"
#include "storage/vendor/libarchive/archive.h"
#include "storage/vendor/libarchive/archive_entry.h"
#include "ball/include/twoine_ball.h"
#include <time.h>
#ifndef TB_SOURCE_VERSION
#define TB_SOURCE_VERSION "unversioned"
#endif
#ifndef TB_SOURCE_REVISION
#define TB_SOURCE_REVISION "unknown"
#endif

static cJSON *archive_build_info(TbBall *c, const cJSON *request, int images)
{
    cJSON *info = cJSON_CreateObject();
    cJSON_AddNumberToObject(info, "formatVersion", 1);
    cJSON_AddBoolToObject(info, "includesDiskImages", images);
    time_t now = time(NULL);
    struct tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    cJSON_AddStringToObject(info, "exportedAt", timestamp);
    cJSON *engine = cJSON_AddObjectToObject(info, "engine");
    cJSON_AddStringToObject(engine, "name", "2ine");
    cJSON_AddStringToObject(engine, "sourceVersion", TB_SOURCE_VERSION);
    cJSON_AddStringToObject(engine, "sourceRevision", TB_SOURCE_REVISION);
    cJSON_AddNumberToObject(engine, "abiVersion", tb_abi_version());
    cJSON_AddStringToObject(engine, "compiledAt", __DATE__ " " __TIME__);
#ifdef __clang_version__
    cJSON_AddStringToObject(engine, "compiler", "Clang " __clang_version__);
#elif defined(__VERSION__)
    cJSON_AddStringToObject(engine, "compiler", __VERSION__);
#endif
    if (cJSON_IsObject(get(request, "exporter")))
        cJSON_AddItemToObject(info, "exporter", cJSON_Duplicate(get(request, "exporter"), 1));
    cJSON *runtime = cJSON_AddObjectToObject(info, "runtime");
    cJSON_AddItemToObject(runtime, "settings", cJSON_Duplicate(get(c->manifest, "runtime"), 1));
    const char *build = field(request, "build");
    if (!build)
        build = field(get(c->manifest, "runtime"), "build");
    if (build)
        cJSON_AddStringToObject(runtime, "buildPath", build);
    if (c->hooks && c->hooks->describe_runtime)
        c->hooks->describe_runtime(c, build, runtime);
    return info;
}

static int archive_relative(const char *path)
{
    if (!path || !*path || *path == '/' || strpbrk(path, "\\:\r\n<>\"|?*"))
        return 0;
    const char *part = path;
    unsigned depth = 0;
    while (*part)
    {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.'))
            return 0;
        /* Windows ignores trailing spaces/dots and interprets device names. */
        if (part[n - 1] == '.' || part[n - 1] == ' ')
            return 0;
        char base[8] = {0};
        size_t b = 0;
        while (b < n && b < sizeof(base) - 1 && part[b] != '.')
        {
            base[b] = (char)toupper((unsigned char)part[b]);
            b++;
        }
        if (!strcmp(base, "CON") || !strcmp(base, "PRN") || !strcmp(base, "AUX") ||
            !strcmp(base, "NUL") ||
            (b == 4 && base[3] >= '1' && base[3] <= '9' &&
             (!strncmp(base, "COM", 3) || !strncmp(base, "LPT", 3))))
            return 0;
        if (++depth > 128)
            return 0;
        if (!end)
            break;
        part = end + 1;
    }
    return 1;
}

static int archive_image(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *extensions[] = {".iso", ".img",  ".ima",  ".dsk",   ".flp", ".vfd",
                                ".vhd", ".vhdx", ".qcow", ".qcow2", ".hdd", NULL};
    for (int i = 0; dot && extensions[i]; i++)
        if (equal_case(dot, extensions[i]))
            return 1;
    return 0;
}

static char *archive_host_path(TbBall *c, const char *path)
{
    char *normalized = tb_ball_keep(c, strdup(path));
    if (!normalized)
        return NULL;
#ifndef _WIN32
    if (normalized[0] != '/')
#endif
        for (char *p = normalized; *p; p++)
            if (*p == '\\')
                *p = '/';
    if (!host_absolute(normalized))
    {
        char *result = tb_ball_keep(c, calloc(strlen(normalized) + 2, 1));
        if (!result)
            return NULL;
        for (const char *part = normalized; *part;)
        {
            const char *end = strchr(part, '/');
            size_t length = end ? (size_t)(end - part) : strlen(part);
            size_t used = strlen(result);
            if (length && !(length == 1 && part[0] == '.'))
            {
                char *last = strrchr(result, '/');
                const char *tail = last ? last + 1 : result;
                if (length == 2 && !strncmp(part, "..", 2) && used && strcmp(tail, ".."))
                {
                    if (last)
                        *last = 0;
                    else
                        *result = 0;
                }
                else
                {
                    if (used)
                        result[used++] = '/';
                    memcpy(result + used, part, length);
                    result[used + length] = 0;
                }
            }
            if (!end)
                break;
            part = end + 1;
        }
        normalized = *result ? result : tb_ball_keep(c, strdup("."));
    }
    return normalized;
}

/* Internal directories travel with the package; external paths are reconnect hints. */
static int archive_normalize(TbBall *c, cJSON *manifest, int exporting)
{
    cJSON *entry;
    cJSON_ArrayForEach(entry, get(manifest, "drives"))
    {
        const char *image = field(entry, "image");
        if (image)
        {
            char *normalized = archive_host_path(c, image);
            if (!normalized)
                return 0;
            put(entry, "image", cJSON_CreateString(normalized));
        }
        const char *directory = str(entry);
        if (!directory)
            continue;
        char *path = archive_host_path(c, directory);
        if (!path)
            return 0;
        if (exporting && native_host_path(path))
        {
            const char *source = host_absolute(path) ? path : tb_ball_join(c, c->ball, path);
            if (!source)
                return 0;
            char *absolute = tb_canonical(source);
            if (!absolute && errno == ENOMEM)
                return tb_ball_fail(c, "Out of memory");
            if (!absolute)
                absolute = tb_absolute(source);
            if (absolute)
            {
                absolute = tb_ball_keep(c, absolute);
                if (!absolute)
                    return 0;
                if (tb_ball_inside(absolute, c->ball))
                {
                    const char *relative = absolute + strlen(c->ball);
                    while (*relative == '/' || *relative == '\\')
                        relative++;
                    path = archive_host_path(c, relative);
                }
                else
                    path = archive_host_path(c, absolute);
            }
        }
        if (!path || !cJSON_SetValuestring(entry, path))
            return tb_ball_fail(c, "Cannot normalize drive path");
    }
    /* Host runtime binaries and build locations are selected anew on the receiver. */
    cJSON_DeleteItemFromObjectCaseSensitive(get(manifest, "runtime"), "build");
    return 1;
}

static int archive_error(TbBall *c, struct archive *archive)
{
    const char *message = archive_error_string(archive);
    return tb_ball_fail(c, "ZIP operation failed: %s", message ? message : "invalid archive");
}

static int archive_add(TbBall *c, struct archive *archive, const char *name, const char *source,
                       const char *text, int directory)
{
    if (!archive_relative(name))
        return tb_ball_fail(c, "Unsafe ZIP path: %s", name);
    if (source && tb_is_link(source))
        return tb_ball_fail(c, "Cannot export symbolic link: %s", source);
    if (source && !directory && !tb_is_file(source))
        return tb_ball_fail(c, "Cannot export non-regular file: %s", source);
    FILE *file = source && !directory ? tb_fopen(source, "rb") : NULL;
    int64_t size = text ? (int64_t)strlen(text) : 0;
    int mode = directory ? 0700 : 0600;
    if (source && !directory && (!file || !tb_file_info(file, &size, &mode)))
    {
        if (file)
            fclose(file);
        return tb_ball_fail(c, "Cannot export regular file: %s", source);
    }
    struct archive_entry *entry = archive_entry_new();
    if (!entry)
    {
        if (file)
            fclose(file);
        return tb_ball_fail(c, "Out of memory");
    }
    archive_entry_set_pathname_utf8(entry, name);
    archive_entry_set_filetype(entry, directory ? AE_IFDIR : AE_IFREG);
    archive_entry_set_perm(entry, mode);
    archive_entry_set_size(entry, size);
    int ok = archive_write_header(archive, entry) == ARCHIVE_OK;
    archive_entry_free(entry);
    if (ok && text)
        ok = archive_write_data(archive, text, (size_t)size) == size;
    unsigned char buffer[65536];
    size_t n;
    while (ok && file && (n = fread(buffer, 1, sizeof(buffer), file)))
        ok = archive_write_data(archive, buffer, n) == (la_ssize_t)n;
    if (file)
    {
        if (ferror(file))
            ok = 0;
        fclose(file);
    }
    return ok ? 1 : archive_error(c, archive);
}

static int archive_tree(TbBall *c, struct archive *archive, const char *relative, int images,
                        const cJSON *image_sources, unsigned depth)
{
    if (depth > 128)
        return tb_ball_fail(c, "Ball folders are nested too deeply");
    char *path = *relative ? tb_ball_join(c, c->ball, relative) : c->ball;
    if (!path)
        return 0;
    if (*relative)
    {
        /* Live IPC, media control files and generated host paths are recreated. */
        if (!strcmp(relative, "ball.json") || !strcmp(relative, "2ine-export.json") ||
            !strcmp(relative, "2ine.cfg") || !strcmp(relative, "audio.conf") ||
            !strcmp(relative, "devices") || !strcmp(relative, "ipc") ||
            !strcmp(relative, "cache/desktop-sessions"))
            return 1;
        char *cache = c->roots[TB_BALL_DRIVE_C]
                          ? tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "OS2/DLL/2INE")
                          : NULL;
        if (cache && !strcmp(path, cache))
            return 1;
        cJSON *source;
        cJSON_ArrayForEach(source, image_sources) if (!strcmp(path, str(source))) return 1;
        if (!images && !tb_is_dir(path) && archive_image(relative))
            return 1;
        if (tb_is_link(path))
            return tb_ball_fail(c, "Cannot export symbolic link: %s", path);
        if (!tb_is_dir(path))
            return archive_add(c, archive, relative, path, NULL, 0);
        if (!archive_add(c, archive, relative, path, NULL, 1))
            return 0;
    }
    if (!tb_directory_readable(path))
        return tb_ball_fail(c, "Cannot read ball folder: %s", path);
    size_t count = 0;
    char **names = tb_names(path, &count);
    int ok = 1;
    for (size_t i = 0; ok && i < count; i++)
    {
        char *child = *relative ? tb_ball_format(c, "%s/%s", relative, names[i])
                                : tb_ball_keep(c, strdup(names[i]));
        ok = child && archive_tree(c, archive, child, images, image_sources, depth + 1);
    }
    tb_names_free(names, count);
    return ok;
}

static int archive_private_drive(TbBall *c, const cJSON *manifest)
{
    const char *drive = field(get(manifest, "drives"), "C");
    if (!drive || !archive_relative(drive))
        return tb_ball_fail(c, "An archived ball must have a relative private C: directory");
    return 1;
}

cJSON *tb_ball_export(TbBall *c, const cJSON *request)
{
    const char *destination = field(request, "path");
    if (!destination)
        return NULL;
    char *target = tb_ball_resolve_path(c, destination, 0);
    char *root = tb_ball_resolve_path(c, c->ball, 1);
    if (!target || !root)
        return NULL;
    c->ball = root;
    /* Resolve the parent as well, so a symlink cannot hide output in the ball. */
    char *parent = tb_ball_keep(c, strdup(target));
    char *slash = parent ? strrchr(parent, '/') : NULL;
#ifdef _WIN32
    char *back = parent ? strrchr(parent, '\\') : NULL;
    if (back && (!slash || back > slash))
        slash = back;
#endif
    if (!slash)
    {
        tb_ball_fail(c, "Choose a ZIP destination outside the ball");
        return NULL;
    }
    if (slash == parent || (slash == parent + 2 && parent[1] == ':'))
        slash[1] = 0;
    else
        *slash = 0;
    parent = tb_ball_resolve_path(c, parent, 1);
    if (!parent || tb_ball_inside(parent, root) || tb_is_dir(target) || tb_is_link(target))
    {
        tb_ball_fail(c, "Choose a ZIP file outside the ball");
        return NULL;
    }
    int images = cJSON_IsTrue(get(request, "includeImages"));
    cJSON *manifest = cJSON_Duplicate(c->manifest, 1);
    if (!archive_normalize(c, manifest, 1) || !archive_private_drive(c, manifest))
    {
        cJSON_Delete(manifest);
        return NULL;
    }
    tb_ball_strip_reference_host_data(manifest);
    cJSON *sources = cJSON_CreateArray(), *members = cJSON_CreateArray();
    cJSON *entry;
    cJSON_ArrayForEach(entry, get(manifest, "drives"))
    {
        const char *image = field(entry, "image");
        if (!image)
            continue;
        int absolute =
            image[0] == '/' || image[0] == '\\' || (strlen(image) > 1 && image[1] == ':');
        char *source =
            tb_ball_resolve_path(c, absolute ? image : tb_ball_join(c, c->ball, image), images);
        if (source)
            cJSON_AddItemToArray(sources, cJSON_CreateString(source));
        if (images && source)
        {
            char *member = tb_ball_format(c, "bundled-media/%s%s", entry->string,
                                          strrchr(source, '.') ? strrchr(source, '.') : ".img");
            cJSON_AddItemToArray(members, cJSON_CreateString(member));
            put(entry, "image", cJSON_CreateString(member));
        }
        /* An image left out stays as a reconnect hint: the receiver sees the
           drive as unavailable until the image is attached or relinked. */
        else if (!field(entry, "device"))
            put(entry, "device", cJSON_CreateString("cdrom"));
    }
    char *text = cJSON_Print(manifest);
    cJSON *info = archive_build_info(c, request, images);
    char *info_text = cJSON_Print(info);
    cJSON_Delete(info);
    char *created = tb_archive_workspace(parent);
    char *workspace = created ? tb_ball_keep(c, created) : NULL;
    if (!workspace)
        tb_ball_fail(c, "Cannot create ZIP workspace");
    char *temporary = workspace ? tb_ball_join(c, workspace, "ball.zip") : NULL;
    FILE *file = temporary ? tb_fopen(temporary, "wb") : NULL;
    struct archive *archive = archive_write_new();
    int ok = !c->error && text && info_text && file && archive;
    if (ok)
        ok = archive_write_set_format_zip(archive) == ARCHIVE_OK &&
             archive_write_open_FILE(archive, file) == ARCHIVE_OK;
    if (ok)
        ok = archive_add(c, archive, "ball.json", NULL, text, 0) &&
             archive_add(c, archive, "2ine-export.json", NULL, info_text, 0) &&
             archive_tree(c, archive, "", images, sources, 0);
    for (int i = 0; ok && i < cJSON_GetArraySize(members); i++)
        ok = archive_add(c, archive, str(cJSON_GetArrayItem(members, i)),
                         str(cJSON_GetArrayItem(sources, i)), NULL, 0);
    if (archive)
    {
        if (archive_write_close(archive) != ARCHIVE_OK)
            ok = 0;
        if (!ok)
            archive_error(c, archive);
        archive_write_free(archive);
    }
    if (file && fclose(file))
        ok = 0;
    if (ok)
        ok = tb_replace(temporary, target);
    if (workspace)
        tb_remove_tree(workspace);
    cJSON_free(text);
    cJSON_free(info_text);
    cJSON_Delete(manifest);
    cJSON_Delete(sources);
    cJSON_Delete(members);
    if (!ok)
    {
        tb_ball_fail(c, "Cannot export ZIP: %s", strerror(errno));
        return NULL;
    }
    return cJSON_CreateString(target);
}

static int extract_ball(TbBall *c, const char *source, const char *destination)
{
    FILE *file = tb_fopen(source, "rb");
    struct archive *archive = archive_read_new();
    int ok = file && archive;
    if (ok)
        ok = archive_read_support_format_zip(archive) == ARCHIVE_OK &&
             archive_read_open_fd(archive, tb_file_descriptor(file), 65536) == ARCHIVE_OK;
    struct archive_entry *entry;
    int status = ARCHIVE_FATAL;
    while (ok && (status = archive_read_next_header(archive, &entry)) == ARCHIVE_OK)
    {
        const char *name = archive_entry_pathname_utf8(entry);
        if (!name)
            name = archive_entry_pathname(entry);
        int directory = archive_entry_filetype(entry) == AE_IFDIR;
        if (!archive_relative(name) || archive_entry_symlink(entry) ||
            archive_entry_hardlink(entry) ||
            (!directory && archive_entry_filetype(entry) != AE_IFREG) ||
            archive_entry_is_encrypted(entry))
        {
            ok = tb_ball_fail(c, "Unsafe or unsupported ZIP entry: %s", name ? name : "(unnamed)");
            break;
        }
        char *path = tb_ball_join(c, destination, name);
        char *parent = path ? tb_ball_keep(c, strdup(path)) : NULL;
        char *slash = parent ? strrchr(parent, '/') : NULL;
#ifdef _WIN32
        char *back = parent ? strrchr(parent, '\\') : NULL;
        if (back && (!slash || back > slash))
            slash = back;
#endif
        if (!slash)
        {
            ok = 0;
            break;
        }
        *slash = 0;
        if (!tb_mkdirs(parent))
        {
            ok = 0;
            break;
        }
        if (directory)
        {
            ok = tb_is_dir(path) || tb_mkdir_new(path);
            continue;
        }
        FILE *out = tb_fopen(path, "wbx");
        if (!out)
        {
            ok = tb_ball_fail(c, "Duplicate or inaccessible ZIP entry: %s", name);
            break;
        }
        unsigned char buffer[65536];
        la_ssize_t n;
        while ((n = archive_read_data(archive, buffer, sizeof(buffer))) > 0)
            if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n)
            {
                ok = 0;
                break;
            }
        if (n < 0)
            ok = 0;
        if (!tb_file_mode(out, archive_entry_perm(entry)))
            ok = 0;
        if (fclose(out))
            ok = 0;
    }
    if (status != ARCHIVE_EOF)
        ok = 0;
    if (archive)
    {
        if (!ok)
            archive_error(c, archive);
        archive_read_free(archive);
    }
    if (file)
        fclose(file);
    return ok ? 1 : tb_ball_fail(c, "Cannot import ZIP: %s", strerror(errno));
}

cJSON *tb_ball_import(TbBall *c, const cJSON *request)
{
    const char *source = field(request, "path"), *destination = field(request, "ball");
    if (!source || !destination)
    {
        tb_ball_fail(c, "A ZIP path and new ball path are required");
        return NULL;
    }
    c->ball = tb_ball_resolve_path(c, destination, 0);
    if (!c->ball)
        return NULL;
    if (!tb_mkdir_new(c->ball))
    {
        tb_ball_fail(c, "Choose a new ball path; existing items cannot be replaced");
        return NULL;
    }
    cJSON *result = NULL;
    if (extract_ball(c, source, c->ball))
    {
        char *text = tb_ball_read_file(c, tb_ball_join(c, c->ball, "ball.json"), NULL, 0);
        cJSON *manifest = text ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
        if (manifest && archive_normalize(c, manifest, 0) && archive_private_drive(c, manifest) &&
            tb_ball_write_json(c, tb_ball_join(c, c->ball, "ball.json"), manifest) &&
            tb_ball_load(c, request))
        {
            /* Never reuse source-machine IPC or media-control state. */
            const char *generated[] = {
                "ipc", "devices", "2ine.cfg", "audio.conf", "cache/desktop-sessions", NULL};
            for (int i = 0; generated[i]; i++)
            {
                char *path = tb_ball_join(c, c->ball, generated[i]);
                if ((tb_is_file(path) || tb_is_dir(path)) && !tb_remove_tree(path))
                    tb_ball_fail(c, "Cannot reset imported runtime state");
            }
            char *cache = tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "OS2/DLL/2INE");
            if (cache && tb_is_dir(cache) && !tb_remove_tree(cache))
                tb_ball_fail(c, "Cannot reset imported runtime snapshots");
            /* Bundled images now live in the ball; their references follow. */
            if (!c->error && tb_ball_save_manifest(c) && tb_ball_regenerate(c))
                result = tb_ball_client_snapshot(c);
        }
        cJSON_Delete(manifest);
        if (!result)
            tb_ball_fail(c, "The ZIP does not contain a valid ball.json and private C: drive");
    }
    if (!result)
        tb_remove_tree(c->ball);
    return result;
}
