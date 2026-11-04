/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared installation-media browser and original OS/2 support-file import. */
#include "media.h"
#include "os2_paths.h"
enum
{
    BASE_SUPPORT_COUNT = 3,
    SUPPORT_COUNT = 11
};
static const char *support_names[] = {"CMD.EXE",     "SYSLEVEL.OS2", "OSO001.MSG",   "REXX.DLL",
                                      "REXXAPI.DLL", "REXXUTIL.DLL", "REXXINIT.DLL", "REX.MSG",
                                      "REXH.MSG",    "RXQUEUE.EXE",  "RXSUBCOM.EXE"};
static const char *support_paths[] = {
    "C:\\OS2\\CMD.EXE",           "C:\\OS2\\SYSLEVEL.OS2",     "C:\\OS2\\SYSTEM\\OSO001.MSG",
    "C:\\OS2\\DLL\\REXX.DLL",     "C:\\OS2\\DLL\\REXXAPI.DLL", "C:\\OS2\\DLL\\REXXUTIL.DLL",
    "C:\\OS2\\DLL\\REXXINIT.DLL", "C:\\OS2\\SYSTEM\\REX.MSG",  "C:\\OS2\\SYSTEM\\REXH.MSG",
    "C:\\OS2\\RXQUEUE.EXE",       "C:\\OS2\\RXSUBCOM.EXE"};
static cJSON *os2_support(Context *c)
{
    cJSON *result = cJSON_CreateObject(), *files = cJSON_AddArrayToObject(result, "files");
    int complete = 1, rexx_complete = 1;
    for (int i = 0; i < SUPPORT_COUNT; i++)
    {
        char *host = host_path(c, support_paths[i], 0);
        if (!host)
        {
            cJSON_Delete(result);
            return NULL;
        }
        int present = tb_is_file(host);
        cJSON *file = cJSON_CreateObject();
        cJSON_AddStringToObject(file, "name", support_names[i]);
        cJSON_AddStringToObject(file, "path", support_paths[i]);
        cJSON_AddBoolToObject(file, "present", present);
        cJSON_AddItemToArray(files, file);
        cJSON_AddBoolToObject(file, "required", i < BASE_SUPPORT_COUNT);
        if (i < BASE_SUPPORT_COUNT)
            complete &= present;
        else
            rexx_complete &= present;
    }
    cJSON_AddBoolToObject(result, "complete", complete);
    cJSON_AddBoolToObject(result, "rexxComplete", rexx_complete);
    size_t media_size = 0;
    char *media_bytes = read_file(c, join(c, c->ball, "os2-media.json"), &media_size, 1);
    cJSON *media =
        media_bytes && media_size ? cJSON_ParseWithLength(media_bytes, media_size) : NULL;
    cJSON *sources = get(media, "sources");
    cJSON_AddItemToObject(result, "mediaSources",
                          cJSON_IsArray(sources) ? cJSON_Duplicate(sources, 1)
                                                 : cJSON_CreateArray());
    cJSON_Delete(media);
    return result;
}
static cJSON *remember_os2_media(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!array_strings(c, sources))
        return NULL;
    cJSON *media = cJSON_CreateObject();
    cJSON_AddItemToObject(media, "sources", cJSON_Duplicate(sources, 1));
    cJSON_AddStringToObject(media, "recorded", now(c));
    int ok = write_json(c, join(c, c->ball, "os2-media.json"), media);
    cJSON_Delete(media);
    return ok ? os2_support(c) : NULL;
}
static int media_candidate(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (!extension)
        return 1;
    if (strlen(extension) > 1 && extension[strlen(extension) - 1] == '_')
        return 1;
    const char *extensions[] = {".zip",   ".iso", ".img", ".ima", ".dsk", ".rar", ".7z",
                                ".lha",   ".lzh", ".tar", ".gz",  ".tgz", ".bz2", ".tbz",
                                ".xz",    ".txz", ".Z",   ".cab", ".wpi", ".exe", ".pack",
                                ".pack2", ".arc", ".arj", ".zoo", NULL};
    for (int i = 0; extensions[i]; i++)
        if (equal_case(extension, extensions[i]))
            return 1;
    return 0;
}
static cJSON *extraction_read_stats(void)
{
    TbMediaReadStats stats = tb_media_read_stats();
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "decompressedBytes", (double)stats.decompressed_bytes);
    cJSON_AddNumberToObject(result, "peakCacheBytes", (double)stats.peak_cache_bytes);
    cJSON_AddNumberToObject(result, "directSlices", (double)stats.direct_slices);
    cJSON_AddNumberToObject(result, "materializedFiles", (double)stats.materialized_files);
    cJSON_AddNumberToObject(result, "containerCacheHits", (double)stats.container_cache_hits);
    cJSON_AddNumberToObject(result, "directoryIndexBuilds", (double)stats.directory_index_builds);
    cJSON_AddNumberToObject(result, "directoryIndexHits", (double)stats.directory_index_hits);
    cJSON_AddNumberToObject(result, "inflateCheckpoints", (double)stats.inflate_checkpoints);
    cJSON_AddNumberToObject(result, "inflateRestores", (double)stats.inflate_restores);
    return result;
}
static cJSON *browse_media(Context *c, const char *input)
{
    if (!input)
    {
        fail(c, "Choose OS/2 installation media or an archive");
        return NULL;
    }
    int host_directory = !tb_media_path(input) && tb_is_dir(input);
    char *path = tb_media_path(input) || host_directory ? keep(c, strdup(input))
                                                        : keep(c, tb_media_root(input));
    if (!path)
        return NULL;
    size_t count = 0;
    TbMediaEntry *items = NULL;
    if (host_directory)
    {
        char **names = tb_names(path, &count);
        if (count != (size_t)-1)
        {
            items = calloc(count ? count : 1, sizeof *items);
            if (!items)
            {
                tb_names_free(names, count);
                fail(c, "Out of memory");
                return NULL;
            }
            for (size_t i = 0; i < count; i++)
            {
                items[i].name = strdup(names[i]);
                char *child = join(c, path, names[i]);
                items[i].directory = tb_is_dir(child);
                tb_path_info(child, &items[i].size, &items[i].modified);
            }
        }
        tb_names_free(names, count == (size_t)-1 ? 0 : count);
    }
    else
        items = tb_media_entries(path, &count);
    if (count == (size_t)-1 && tb_media_path(input))
    {
        path = keep(c, tb_media_root(input));
        if (path)
            items = tb_media_entries(path, &count);
    }
    if (count == (size_t)-1)
    {
        fail(c, "Cannot open this media or archive: %s", strerror(errno));
        return NULL;
    }
    cJSON *result = cJSON_CreateObject(), *entries = cJSON_AddArrayToObject(result, "entries");
    cJSON_AddStringToObject(result, "path", path);
    char *label = tb_media_label(path), *parent = tb_media_parent(path);
    cJSON_AddStringToObject(result, "label", label ? label : input);
    if (parent)
        cJSON_AddStringToObject(result, "parent", parent);
    else
        cJSON_AddNullToObject(result, "parent");
    free(label);
    free(parent);
    for (size_t i = 0; i < count; i++)
    {
        cJSON *entry = cJSON_CreateObject();
        char *child = host_directory ? strdup(join(c, path, items[i].name))
                                     : tb_media_child(path, items[i].name);
        if (!child)
        {
            fail(c, "Cannot name archive member");
            break;
        }
        cJSON_AddStringToObject(entry, "name", items[i].name);
        cJSON_AddStringToObject(entry, "path", child);
        cJSON_AddBoolToObject(entry, "directory", items[i].directory);
        int container = !items[i].directory && media_candidate(items[i].name);
        for (int file = 0; file < SUPPORT_COUNT && !items[i].directory; file++)
            if (equal_case(items[i].name, support_names[file]))
                container = tb_media_container(child);
        cJSON_AddBoolToObject(entry, "container", container);
        cJSON_AddNumberToObject(entry, "size", (double)items[i].size);
        if (items[i].modified != INT64_MIN)
            cJSON_AddNumberToObject(entry, "modified", (double)items[i].modified);
        cJSON_AddItemToArray(entries, entry);
        free(child);
    }
    cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
    tb_media_entries_free(items, count);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}
typedef struct SupportFile
{
    unsigned char *data;
    size_t size;
    char *source;
} SupportFile;
static int support_read(Context *c, const char *source, int index, SupportFile *file)
{
    FILE *input = tb_media_path(source) ? tb_media_fopen(source) : tb_fopen(source, "rb");
    if (!input)
        return fail(c, "Cannot extract %s: %s", support_names[index], strerror(errno));
    int ok = !fseek(input, 0, SEEK_END);
    long size = ok ? ftell(input) : -1;
    rewind(input);
    if (size <= 0 || size > 16 * 1024 * 1024)
    {
        fclose(input);
        return fail(c, "Invalid support file size: %s", support_names[index]);
    }
    unsigned char *data = malloc((size_t)size);
    ok = data && fread(data, 1, (size_t)size, input) == (size_t)size;
    fclose(input);
    if (!ok)
    {
        free(data);
        return fail(c, "Cannot read %s", support_names[index]);
    }
    size_t executable = size >= 64 ? (size_t)data[60] | (size_t)data[61] << 8 |
                                         (size_t)data[62] << 16 | (size_t)data[63] << 24
                                   : 0;
    int valid =
        index == 0 || (index >= 3 && index <= 6) || index >= 9
            ? size >= 64 && data[0] == 'M' && data[1] == 'Z' && executable <= (size_t)size - 2 &&
                  (!memcmp(data + executable, "NE", 2) || !memcmp(data + executable, "LX", 2))
        : index == 1 ? size >= 10 && !memcmp(data, "\xff\xffSYSLEVEL", 10)
                     : size >= 8 && !memcmp(data, "\xffMKMSGF", 7);
    if (!valid)
    {
        free(data);
        return fail(c, "%s is not an original OS/2 support file", support_names[index]);
    }
    if (file->data)
    {
        ok = file->size == (size_t)size && !memcmp(file->data, data, (size_t)size);
        free(data);
        return ok ? 1
                  : fail(
                        c,
                        "Different versions of %s were found. Select one OS/2 release or language.",
                        support_names[index]);
    }
    file->data = data;
    file->size = (size_t)size;
    file->source = strdup(source);
    return file->source != NULL || fail(c, "Out of memory");
}
static cJSON *import_os2_support(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!array_strings(c, sources) || !cJSON_GetArraySize(sources))
    {
        fail(c, "Choose OS/2 CD, floppy images or archives");
        return NULL;
    }
    cJSON *queue = cJSON_Duplicate(sources, 1), *imported = cJSON_CreateArray();
    SupportFile files[SUPPORT_COUNT] = {{0}};
    unsigned visited = 0;
    /* A work queue handles mixed nesting without a recursion-depth limit. */
    for (const cJSON *node = queue->child; node && !c->error; node = node->next)
    {
        if (++visited > 100000u)
        {
            fail(c,
                 "Media search exceeded 100000 entries. Browse to a smaller folder and try again.");
            break;
        }
        const char *input = str(node);
        if (!tb_media_path(input) && tb_is_dir(input))
        {
            size_t count = 0;
            char **names = tb_names(input, &count);
            if (count == (size_t)-1)
                fail(c, "Cannot read media folder");
            else
                for (size_t n = 0; n < count; n++)
                {
                    char *child = join(c, input, names[n]);
                    if (!tb_is_link(child))
                        cJSON_AddItemToArray(queue, cJSON_CreateString(child));
                }
            tb_names_free(names, count == (size_t)-1 ? 0 : count);
            continue;
        }
        const char *base = strrchr(input, '/');
        base = base ? base + 1 : input;
        char *decoded = NULL;
        if (tb_media_path(input))
        {
            decoded = tb_media_label(input);
            if (!decoded)
            {
                fail(c, "Invalid media path");
                break;
            }
            base = strrchr(decoded, '/');
            base = base ? base + 1 : decoded;
            const char *boundary = strstr(base, " ▸ ");
            while (boundary)
            {
                base = boundary + strlen(" ▸ ");
                boundary = strstr(base, " ▸ ");
            }
        }
        int index = -1;
        for (int i = 0; i < SUPPORT_COUNT; i++)
            if (equal_case(base, support_names[i]))
                index = i;
        if (index >= 0)
        {
            char *destination = host_path(c, support_paths[index], 0);
            if (!destination || tb_is_file(destination))
            {
                free(decoded);
                continue;
            }
            /* Some installation disks give a compressed member its final filename. */
            if (!tb_media_container(input))
            {
                support_read(c, input, index, &files[index]);
                free(decoded);
                continue;
            }
        }
        free(decoded);
        size_t count = 0;
        TbMediaEntry *entries = NULL;
        char *root = NULL;
        if (tb_media_path(input))
            entries = tb_media_entries(input, &count);
        else
            count = (size_t)-1;
        if (count == (size_t)-1)
        {
            if (!tb_media_container(input))
            {
                if (visited <= (unsigned)cJSON_GetArraySize(sources))
                    fail(c, "Cannot read selected installation media or archive: %s", input);
                continue;
            }
            root = tb_media_root(input);
            if (!root)
            {
                fail(c, "Cannot open media");
                break;
            }
            entries = tb_media_entries(root, &count);
        }
        const char *directory = root ? root : input;
        if (count == (size_t)-1)
            fail(c, "Cannot read archive or installation image: %s", strerror(errno));
        else
            for (size_t n = 0; n < count; n++)
            {
                int wanted = entries[n].directory || media_candidate(entries[n].name);
                for (int i = 0; i < SUPPORT_COUNT; i++)
                    if (equal_case(entries[n].name, support_names[i]))
                        wanted = 1;
                if (wanted)
                {
                    char *child = tb_media_child(directory, entries[n].name);
                    if (child)
                    {
                        cJSON_AddItemToArray(queue, cJSON_CreateString(child));
                        free(child);
                    }
                    else
                        fail(c, "Cannot name archive member");
                }
            }
        tb_media_entries_free(entries, count == (size_t)-1 ? 0 : count);
        free(root);
    }
    for (int i = 0; i < SUPPORT_COUNT && !c->error; i++)
        if (files[i].data)
        {
            char *destination = host_path(c, support_paths[i], 0);
            char *parent = destination ? keep(c, strdup(destination)) : NULL;
            if (parent)
            {
                char *slash = strrchr(parent, '/');
                if (slash)
                    *slash = 0;
            }
            if (!parent || !tb_mkdirs(parent) || tb_is_file(destination) ||
                !tb_atomic_write(destination, files[i].data, files[i].size))
            {
                fail(c, "Cannot install %s", support_paths[i]);
                break;
            }
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "path", support_paths[i]);
            char *label = tb_media_label(files[i].source);
            cJSON_AddStringToObject(entry, "source", label ? label : files[i].source);
            free(label);
            cJSON_AddItemToArray(imported, entry);
        }
    for (int i = 0; i < SUPPORT_COUNT; i++)
    {
        free(files[i].data);
        free(files[i].source);
    }
    cJSON_Delete(queue);
    if (!c->error)
    {
        cJSON *paths = cJSON_CreateArray();
        for (int i = 0; i < SUPPORT_COUNT; i++)
        {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "path", support_paths[i]);
            cJSON_AddItemToArray(paths, entry);
        }
        os2_configure_paths(c, paths);
        cJSON_Delete(paths);
    }
    if (!c->error && !cJSON_IsFalse(get(request, "rememberSources")))
    {
        cJSON *media = cJSON_CreateObject();
        cJSON_AddItemToObject(media, "sources", cJSON_Duplicate(sources, 1));
        cJSON_AddStringToObject(media, "recorded", now(c));
        write_json(c, join(c, c->ball, "os2-media.json"), media);
        cJSON_Delete(media);
    }
    if (c->error)
    {
        cJSON_Delete(imported);
        return NULL;
    }
    cJSON *result = os2_support(c);
    if (result)
        cJSON_AddItemToObject(result, "imported", imported);
    else
        cJSON_Delete(imported);
    return result;
}
