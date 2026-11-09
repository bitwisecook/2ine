/* Public domain. No rights reserved. Wholly AI-authored. */
/* Browsing guest directories and copying host files into a drive. */
#include "ball/ball_internal.h"
#include "storage/volume.h"

cJSON *tb_ball_browse(TbBall *c, const char *path)
{
    char *host = tb_ball_host_path(c, path, 0);
    if (host && tb_is_file(host))
    {
        host = tb_ball_keep(c, strdup(host));
        if (host)
        {
            char *slash = strrchr(host, '/');
#ifdef _WIN32
            char *back = strrchr(host, '\\');
            if (back && (!slash || back > slash))
                slash = back;
#endif
            if (slash)
                *slash = 0;
        }
    }
    if (!host || !tb_is_dir(host) || !tb_directory_readable(host))
    {
        tb_ball_fail(c, "Choose an accessible mapped directory");
        return NULL;
    }

    /* Preserve the drive the user chose for nested or duplicate mappings. */
    int drive = path && isalpha((unsigned char)path[0]) && path[1] == ':'
                    ? toupper((unsigned char)path[0]) - 'A'
                    : tb_ball_current_drive(c);
    const char *tail = host + strlen(c->roots[drive]);
    while (*tail == '/' || *tail == '\\')
        tail++;
    char *guest = tb_ball_format(c, "%c:\\%s", 'A' + drive, tail);
    if (!guest)
        return NULL;
    for (char *p = guest; *p; p++)
        if (*p == '/')
            *p = '\\';
    cJSON *result = cJSON_CreateObject(), *entries = cJSON_AddArrayToObject(result, "entries");
    if (strlen(guest) > 3)
    {
        const char *last = strrchr(guest, '\\');
        size_t length = last == guest + 2 ? 3 : (size_t)(last - guest);
        cJSON_AddStringToObject(result, "parent", tb_ball_format(c, "%.*s", (int)length, guest));
    }
    else
        cJSON_AddNullToObject(result, "parent");
    cJSON_AddStringToObject(result, "path", guest);
    cJSON_AddStringToObject(result, "hostPath", host);
    size_t count = 0;
    char **names = tb_names(host, &count);
    if (!names && count == (size_t)-1)
    {
        cJSON_Delete(result);
        tb_ball_fail(c, "Cannot read directory: %s", guest);
        return NULL;
    }
    if (count > 1)
        qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0; i < count; i++)
    {
        char *child = tb_ball_join(c, host, names[i]);
        if (strpbrk(names[i], "\\:"))
            continue;
        char *resolved = tb_ball_case_path(c, child);
        if (!resolved || !tb_ball_inside(resolved, c->roots[drive]))
            continue;
        int directory = tb_is_dir(child);
        if (!directory && !tb_is_file(child))
            continue;
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", names[i]);
        cJSON_AddStringToObject(entry, "path",
                                tb_ball_format(c, "%s%s%s", guest,
                                               guest[strlen(guest) - 1] == '\\' ? "" : "\\",
                                               names[i]));
        cJSON_AddStringToObject(entry, "hostPath", child);
        cJSON_AddBoolToObject(entry, "directory", directory);
        uint64_t size = 0;
        int64_t modified = INT64_MIN;
        if (tb_path_info(child, &size, &modified))
        {
            cJSON_AddNumberToObject(entry, "size", (double)size);
            if (modified != INT64_MIN)
                cJSON_AddNumberToObject(entry, "modified", (double)modified);
        }
        cJSON_AddItemToArray(entries, entry);
    }
    tb_names_free(names, count);
    return result;
}

int tb_ball_plan_copy(TbBall *c, cJSON *plan, const char *source, const char *destination,
                      unsigned depth)
{
    if (depth > 64 || cJSON_GetArraySize(plan) > 100000)
        return tb_ball_fail(c, "The dropped folder is too deep or contains too many files");
    if (tb_is_link(source))
        return tb_ball_fail(c, "Symbolic links cannot be imported: %s", source);
    if (!tb_is_dir(source) && !tb_is_file(source))
        return tb_ball_fail(c, "Not a regular file or folder: %s", source);
    char *target = tb_ball_case_path(c, destination);
    if (!target)
        return 0;
    if (tb_is_dir(target) || tb_is_file(target) || tb_is_link(target))
        return tb_ball_fail(c, "A file or folder already exists: %s. Nothing was replaced.",
                            target);
    cJSON *other;
    cJSON_ArrayForEach(
        other,
        plan) if (equal_case(field(other, "destination"),
                             target)) return tb_ball_fail(c,
                                                          "Dropped items have conflicting names");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "source", source);
    cJSON_AddStringToObject(entry, "destination", target);
    cJSON_AddBoolToObject(entry, "directory", tb_is_dir(source));
    cJSON_AddItemToArray(plan, entry);
    if (tb_is_dir(source))
    {
        if (!tb_directory_readable(source))
            return tb_ball_fail(c, "Cannot read dropped folder: %s", source);
        size_t count = 0;
        char **names = tb_names(source, &count);
        if (!names && count == (size_t)-1)
            return tb_ball_fail(c, "Cannot read dropped folder: %s", source);
        int ok = 1;
        for (size_t i = 0; i < count && ok; i++)
        {
            if (strpbrk(names[i], "\\:"))
            {
                ok = tb_ball_fail(c, "Filename is not an OS/2 filename: %s", names[i]);
                break;
            }
            ok = tb_ball_plan_copy(c, plan, tb_ball_join(c, source, names[i]),
                                   tb_ball_join(c, target, names[i]), depth + 1);
        }
        tb_names_free(names, count);
        return ok;
    }
    return 1;
}

cJSON *tb_ball_import_files(TbBall *c, const cJSON *request)
{
    const char *directory = field(request, "path");
    char *destination = directory ? tb_ball_host_path(c, directory, 0) : NULL;
    if (destination && tb_image_path(destination))
    {
        TbImageInfo info;
        if (!tb_image_info(destination, &info) || info.readonly)
        {
            tb_ball_fail(c, "This image drive is read-only or not ready");
            return NULL;
        }
    }
    cJSON *sources = get(request, "sources");
    if (!destination || !tb_is_dir(destination) || !tb_ball_array_strings(c, sources) ||
        !cJSON_GetArraySize(sources))
    {
        tb_ball_fail(c, "Choose a mapped destination and files to copy");
        return NULL;
    }
    cJSON *plan = cJSON_CreateArray(), *source;
    cJSON_ArrayForEach(source, sources)
    {
        const char *raw = str(source);
        if (tb_is_link(raw))
        {
            tb_ball_fail(c, "Symbolic links cannot be imported: %s", raw);
            break;
        }
        char *path = tb_ball_resolve_path(c, raw, 1);
        if (!path)
            break;
        const char *name = path;
        for (const char *p = path; *p; p++)
            if (*p == '/' || *p == '\\')
                name = p + 1;
        if (!*name || strpbrk(name, ":\\"))
        {
            tb_ball_fail(c, "Choose a file or named folder to copy");
            break;
        }
        if (tb_ball_inside(destination, path))
        {
            tb_ball_fail(c, "Cannot copy a folder into itself");
            break;
        }
        if (!tb_ball_plan_copy(c, plan, path, tb_ball_join(c, destination, name), 0))
            break;
    }
    if (!c->error)
    {
        cJSON *entry;
        cJSON_ArrayForEach(entry, plan)
        {
            const char *src = field(entry, "source"), *dst = field(entry, "destination");
            if (tb_is_link(src) || (cJSON_IsTrue(get(entry, "directory")) ? !tb_mkdir_new(dst)
                                                                          : !tb_copy_new(src, dst)))
            {
                tb_ball_fail(
                    c,
                    "Copy stopped at %s. Existing files were kept; earlier copied items may remain.",
                    dst);
                break;
            }
        }
    }
    cJSON_Delete(plan);
    return c->error ? NULL : tb_ball_browse(c, directory);
}
