/* Public domain. No rights reserved. Wholly AI-authored. */
/* Category-based OS/2 media planning and staged extraction into a ball. */
#include "system_modules.h"
static const char *extract_categories[] = {"cmd", "fonts", "assets", "printers", "help", "full"};
static int extract_category(const char *name)
{
    for (int i = 0; i < 6; i++)
        if (equal_case(name, extract_categories[i]))
            return i;
    return -1;
}
static cJSON *extraction_read_stats(void)
{
    TbMediaReadStats stats = tb_media_read_stats();
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "decompressedBytes", (double)stats.decompressed_bytes);
    cJSON_AddNumberToObject(result, "peakCacheBytes", (double)stats.peak_cache_bytes);
    cJSON_AddNumberToObject(result, "directSlices", (double)stats.direct_slices);
    cJSON_AddNumberToObject(result, "materializedFiles", (double)stats.materialized_files);
    return result;
}
static int extract_extension(const char *name, const char *extension)
{
    const char *dot = strrchr(name, '.');
    return dot && equal_case(dot, extension);
}
static int extract_video(const char *name)
{
    static const char *extensions[] = {".AVI", ".MPG", ".MPEG", ".MOV",  ".FLI",
                                       ".FLC", ".MP4", ".MKV",  ".WEBM", NULL};
    for (int i = 0; extensions[i]; i++)
        if (extract_extension(name, extensions[i]))
            return 1;
    return 0;
}
static char *extract_child(Context *c, const char *directory, const char *name)
{
    return tb_media_path(directory) ? keep(c, tb_media_child(directory, name))
                                    : join(c, directory, name);
}
static int extract_directory(const char *path)
{
    if (!tb_media_path(path))
        return tb_is_dir(path);
    size_t count = 0;
    TbMediaEntry *entries = tb_media_entries(path, &count);
    tb_media_entries_free(entries, count == (size_t)-1 ? 0 : count);
    return count != (size_t)-1;
}
static void extract_queue(cJSON *queue, const char *source, const char *layout, int directory,
                          double size)
{
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "source", source);
    cJSON_AddStringToObject(entry, "layout", layout);
    cJSON_AddBoolToObject(entry, "directory", directory);
    cJSON_AddNumberToObject(entry, "size", size);
    cJSON_AddItemToArray(queue, entry);
}
static int extract_children(Context *c, cJSON *queue, const char *source, const char *layout)
{
    if (!tb_media_path(source) && tb_is_dir(source))
    {
        size_t count = 0;
        char **names = tb_names(source, &count);
        if (count == (size_t)-1)
            return fail(c, "Cannot read media folder: %s", source);
        for (size_t i = 0; i < count; i++)
        {
            char *child = join(c, source, names[i]);
            if (child && !tb_is_link(child))
                extract_queue(queue, child, format(c, "%s/%s", layout, names[i]), tb_is_dir(child),
                              0);
        }
        tb_names_free(names, count);
        return !c->error;
    }
    size_t count = 0;
    TbMediaEntry *entries = tb_media_entries(source, &count);
    if (count == (size_t)-1)
        return fail(c, "Cannot read media or archive: %s", source);
    for (size_t i = 0; i < count && !c->error; i++)
    {
        char *child = extract_child(c, source, entries[i].name);
        if (!child)
            fail(c, "Cannot name media member");
        else
            extract_queue(queue, child,
                          *layout ? format(c, "%s/%s", layout, entries[i].name) : entries[i].name,
                          entries[i].directory, (double)entries[i].size);
    }
    tb_media_entries_free(entries, count);
    return !c->error;
}
static cJSON *open_os2_source(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!array_strings(c, sources) || !cJSON_GetArraySize(sources))
    {
        fail(c, "Choose OS/2 installation media");
        return NULL;
    }
    cJSON *queue = cJSON_CreateArray(), *detected = cJSON_CreateArray();
    const cJSON *source;
    cJSON_ArrayForEach(source, sources)
        extract_queue(queue, str(source), "", extract_directory(str(source)), 0);
    for (int at = 0; at < cJSON_GetArraySize(queue) && !c->error; at++)
    {
        if (at >= 100000)
        {
            fail(c, "Media search exceeded 100000 entries. Choose a smaller source.");
            break;
        }
        cJSON *node = cJSON_GetArrayItem(queue, at);
        const char *path = field(node, "source");
        char *name = keep(c, tb_media_member_name(path));
        if (name && extract_extension(name, ".iso"))
        {
            cJSON_AddItemToArray(detected, cJSON_CreateString(path));
            continue;
        }
        /* Folder selection stays at the chosen folder. Archive directories are
           searched for disc images, without descending into the images. */
        if (!tb_media_path(path) && tb_is_dir(path))
            continue;
        if (cJSON_IsTrue(get(node, "directory")))
            extract_children(c, queue, path, "");
        else if (name && media_candidate(name) && tb_media_container(path))
        {
            char *root = keep(c, tb_media_root(path));
            if (root)
                extract_children(c, queue, root, "");
        }
    }
    cJSON_Delete(queue);
    if (c->error)
    {
        cJSON_Delete(detected);
        return NULL;
    }
    int found = cJSON_GetArraySize(detected);
    cJSON *chosen = found ? detected : cJSON_Duplicate(sources, 1);
    if (!found)
        cJSON_Delete(detected);
    cJSON *directory = browse_media(c, str(cJSON_GetArrayItem(chosen, 0)));
    if (!directory)
    {
        cJSON_Delete(chosen);
        return NULL;
    }
    int installation_directory = 0;
    if (found == 1)
    {
        cJSON *entry;
        cJSON_ArrayForEach(
            entry, get(directory, "entries")) if (cJSON_IsTrue(get(entry, "directory")) &&
                                                  equal_case(field(entry, "name"), "os2image"))
        {
            char *path = keep(c, strdup(field(entry, "path")));
            cJSON *focused = browse_media(c, path);
            if (focused)
            {
                cJSON_Delete(directory);
                directory = focused;
                cJSON_Delete(chosen);
                chosen = cJSON_CreateArray();
                cJSON_AddItemToArray(chosen, cJSON_CreateString(path));
                installation_directory = 1;
            }
            break;
        }
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "sources", chosen);
    cJSON_AddItemToObject(result, "directory", directory);
    cJSON_AddBoolToObject(result, "detectedISO", found != 0);
    cJSON_AddBoolToObject(result, "installationDirectory", installation_directory);
    cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
    return result;
}
static char *extract_extra_path(Context *c, const char *layout, const char *source)
{
    char *label = keep(c, tb_media_label(source));
    const char *relative = layout;
    if (label && tb_media_path(source))
    {
        const char *boundary = strstr(label, " ▸ ");
        if (boundary)
        {
            char *root = label;
            for (char *p = label; p < boundary; p++)
                if (*p == '/')
                    root = p + 1;
            char *copy = keep(c, strdup(root));
            if (!copy)
                return NULL;
            char *at;
            while ((at = strstr(copy, " ▸ ")))
            {
                *at = '/';
                memmove(at + 1, at + strlen(" ▸ "), strlen(at + strlen(" ▸ ")) + 1);
            }
            relative = copy;
        }
    }
    return format(c, "OS2/INSTALL/MEDIA/%s", relative);
}
/* Match the actual installation layout rather than placing an ISO's setup
   tree under C:. PACK/PACK2 members retain their original target directories. */
static char *extract_destination(Context *c, const char *layout, const char *source, int *category,
                                 int *provided, int *priority)
{
    *priority = 1;
    char *path = keep(c, strdup(layout)), *upper = keep(c, strdup(layout));
    if (!path || !upper)
        return NULL;
    for (char *p = upper; *p; p++)
        *p = *p == '\\' ? '/' : (char)toupper((unsigned char)*p);
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
    const char *base = strrchr(upper, '/');
    base = base ? base + 1 : upper;
    for (int i = 0; i < SUPPORT_COUNT; i++)
        if (equal_case(base, support_names[i]))
        {
            *category = 0;
            *priority = 3;
            return keep(c, strdup(support_paths[i]));
        }
    /* These services execute through 2ine's system/API bridges. HELPMGR is
       deliberately absent: its original viewer is an optional help payload. */
    static const char *protected_files[] = {
        "OS2KRNL", "OS2KRNLI", "OS2KRNLB",   "OS2KRNLD", "OS2LDR",     "OS2BOOT", "OS2DUMP",
        "OS2VER",  "EA",       "CONFIG.SYS", "OS2.INI",  "OS2SYS.INI", NULL};
    *provided = os2_module_provided(c, base);
    for (int i = 0; protected_files[i]; i++)
        if (equal_case(base, protected_files[i]))
            *provided = 1;
    *category = 5;
    if (extract_extension(base, ".PFB") || extract_extension(base, ".PFM") ||
        extract_extension(base, ".TTF") || extract_extension(base, ".TTC") ||
        extract_extension(base, ".FON") || extract_extension(base, ".FNT") ||
        extract_extension(base, ".OFM"))
        *category = 1;
    else if (extract_extension(base, ".WAV") || extract_extension(base, ".MID") ||
             extract_extension(base, ".BMP") || extract_extension(base, ".ICO") ||
             extract_extension(base, ".ANI") || extract_extension(base, ".MET") ||
             extract_video(base))
        *category = 2;
    else if (extract_extension(base, ".DRV") || extract_extension(base, ".PDR") ||
             strstr(upper, "/SPOOL/") || strstr(upper, "/PRINTER/"))
        *category = 3;
    else if (extract_extension(base, ".HLP") || extract_extension(base, ".INF") ||
             !strcmp(base, "HELPMGR.DLL") || !strcmp(base, "HPMGRMRI.DLL") ||
             !strcmp(base, "VIEWDOC.EXE") || !strcmp(base, "VIEW.EXE") ||
             !strcmp(base, "HMHELP.HLP"))
        *category = 4;
    char *label = keep(c, tb_media_label(source));
    if (label)
    {
        for (char *p = label; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (strstr(label, "PMDD_") || strstr(label, "/PRINTER/"))
            *category = 3;
    }
    static const char *roots[] = {"OS2",    "MMOS2",    "PSFONTS", "TCPIP",       "MPTN", "IBMLAN",
                                  "IBMCOM", "LANGUAGE", "DESKTOP", "MAINTENANCE", NULL};
    char *anchor = NULL;
    for (char *part = upper; *part;)
    {
        char *slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        for (int i = 0; roots[i]; i++)
            if (strlen(roots[i]) == length && !strncmp(part, roots[i], length) && slash)
            {
                anchor = path + (part - upper);
                break;
            }
        if (anchor || !slash)
            break;
        part = slash + 1;
    }
    const char *boundary = label ? strstr(label, " ▸ ") : NULL;
    int packed_layout = boundary && strstr(boundary + strlen(" ▸ "), " ▸ ");
    *priority = anchor ? 3 : (strchr(path, '/') || packed_layout ? 2 : 1);
    /* The installation boot disk can contain reduced/older copies of tools
       also shipped on the base disk set. Prefer the base disk counterpart. */
    if (label && strstr(label, "/DISK_0/"))
        *priority = 0;
    /* Alternate Windows language/display packages lose ties to the base disk
       set; explicitly selecting one still imports its own files. */
    if (label && (strstr(label, "/W_AR/") || strstr(label, "/W_CE/") || strstr(label, "/W_EL/") ||
                  strstr(label, "/W_HE/") || strstr(label, "/W_TH/") || strstr(label, "/W_TR/") ||
                  strstr(label, "/DISP_")))
        *priority = 1;
    char *relative = anchor;
    /* Win-OS/2 is a separate compatibility tree. Its flat Windows packages
       must not collide with native OS/2 help books, DLLs or printer drivers. */
    int windows = label && strstr(label, "/DISK_W");
    if (windows)
    {
        *category = 5;
        if (!relative)
        {
            int system = extract_extension(base, ".DLL") || extract_extension(base, ".DRV") ||
                         extract_extension(base, ".FON") || extract_extension(base, ".CPL") ||
                         extract_extension(base, ".VBX") || extract_extension(base, ".386") ||
                         extract_extension(base, ".TTF");
            relative =
                format(c, "%s/%s", system ? "OS2/MDOS/WINOS2/SYSTEM" : "OS2/MDOS/WINOS2", base);
        }
    }
    if (!relative && *category == 1)
        relative = format(c, "%s/%s",
                          extract_extension(base, ".PFB")   ? "PSFONTS"
                          : extract_extension(base, ".PFM") ? "PSFONTS/PFM"
                                                            : "OS2/DLL",
                          base);
    else if (!relative && *category == 2)
        relative = format(c, "%s/%s",
                          extract_video(base) ? "MMOS2/MOVIE"
                          : extract_extension(base, ".WAV") || extract_extension(base, ".MID")
                              ? "MMOS2/SOUNDS"
                              : "OS2/BITMAP",
                          base);
    else if (!relative && *category == 3)
        relative = format(c, "OS2/DLL/%s", path);
    else if (!relative && *category == 4)
        relative = format(c, "%s/%s",
                          extract_extension(base, ".DLL")   ? "OS2/DLL"
                          : extract_extension(base, ".INF") ? "OS2/BOOK"
                          : extract_extension(base, ".HLP") ? "OS2/HELP"
                                                            : "OS2",
                          base);
    else if (!relative && (extract_extension(base, ".EXE") || extract_extension(base, ".DLL") ||
                           extract_extension(base, ".SYS") || extract_extension(base, ".DCP") ||
                           extract_extension(base, ".MSG") || extract_extension(base, ".ADD") ||
                           extract_extension(base, ".DMD") || extract_extension(base, ".IFS") ||
                           extract_extension(base, ".FLT")))
        relative =
            format(c, "%s/%s",
                   extract_extension(base, ".DLL")                                  ? "OS2/DLL"
                   : extract_extension(base, ".SYS") && strcmp(base, "COUNTRY.SYS") ? "OS2/BOOT"
                                                                                    : "OS2",
                   base);
    if (!relative)
        relative = *provided ? format(c, "%s", base) : extract_extra_path(c, path, source);
    if ((label && strstr(label, "/DEBUG/")) || extract_extension(base, ".SYM") ||
        extract_extension(base, ".MAP"))
    {
        *category = 5;
        *priority = 1;
        relative = extract_extra_path(c, path, source);
    }
    /* Media names are untrusted even when their target prefix is recognized. */
    for (const char *part = relative; *part;)
    {
        const char *slash = strchr(part, '/');
        size_t n = slash ? (size_t)(slash - part) : strlen(part);
        if (!n || (n == 1 && *part == '.') || (n == 2 && !strncmp(part, "..", 2)) ||
            memchr(part, ':', n))
            return NULL;
        if (!slash)
            break;
        part = slash + 1;
    }
    char *destination = format(c, "C:\\%s", relative);
    for (char *p = destination; *p; p++)
        if (*p == '/')
            *p = '\\';
    return destination;
}
static cJSON *plan_os2_extraction(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!array_strings(c, sources) || !cJSON_GetArraySize(sources))
    {
        fail(c, "Choose an OS/2 source first");
        return NULL;
    }
    cJSON *result = cJSON_CreateObject(), *files = cJSON_AddArrayToObject(result, "files"),
          *queue = cJSON_CreateArray(), *unmapped = cJSON_AddArrayToObject(result, "unmapped");
    const cJSON *source;
    cJSON_ArrayForEach(source, sources)
    {
        char *name = keep(c, tb_media_member_name(str(source)));
        if (name)
            extract_queue(queue, str(source), name, extract_directory(str(source)), 0);
    }
    for (int at = 0; at < cJSON_GetArraySize(queue) && !c->error; at++)
    {
        if (at >= 100000)
        {
            fail(c, "Media search exceeded 100000 entries. Browse to a smaller source.");
            break;
        }
        const cJSON *node = cJSON_GetArrayItem(queue, at);
        const char *path = field(node, "source"), *layout = field(node, "layout");
        if (!tb_media_path(path) && tb_is_link(path))
        {
            fail(c, "Symbolic links cannot be imported");
            break;
        }
        if (cJSON_IsTrue(get(node, "directory")) || (!tb_media_path(path) && tb_is_dir(path)))
        {
            extract_children(c, queue, path, layout);
            continue;
        }
        const char *base = strrchr(layout, '/');
        base = base ? base + 1 : layout;
        if (media_candidate(base) && tb_media_container(path))
        {
            char *root = keep(c, tb_media_root(path));
            if (root)
                extract_children(c, queue, root, "");
            continue;
        }
        int category = 5, provided = 0, priority = 1;
        char *destination = extract_destination(c, layout, path, &category, &provided, &priority);
        if (!destination)
        {
            cJSON_AddItemToArray(unmapped, cJSON_CreateString(layout));
            continue;
        }
        char *host = host_path(c, destination, 0);
        if (!host)
            break;
        cJSON *file = cJSON_CreateObject();
        cJSON_AddStringToObject(file, "path", destination);
        cJSON_AddStringToObject(file, "source", path);
        cJSON_AddStringToObject(file, "category", extract_categories[category]);
        cJSON_AddBoolToObject(file, "provided", provided);
        const char *filename = strrchr(destination, '\\');
        cJSON_AddBoolToObject(file, "disabled",
                              os2_module_disabled(c, filename ? filename + 1 : destination));
        cJSON_AddNumberToObject(file, "priority", priority);
        cJSON_AddBoolToObject(file, "present", tb_is_file(host));
        cJSON_AddNumberToObject(file, "size", get(node, "size")->valuedouble);
        cJSON_AddItemToArray(files, file);
    }
    cJSON_Delete(queue);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
    return result;
}
static int extraction_wants_module(const cJSON *modules, const char *path)
{
    if (!modules)
        return 1;
    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    size_t length = strlen(base);
    const cJSON *item;
    cJSON_ArrayForEach(item, modules)
    {
        const char *name = str(item);
        if (equal_case(base, name))
            return 1;
        if (length > 4 && equal_case(base + length - 4, ".dll") && strlen(name) == length - 4)
        {
            size_t at = 0;
            while (at < length - 4 &&
                   toupper((unsigned char)base[at]) == toupper((unsigned char)name[at]))
                at++;
            if (at == length - 4)
                return 1;
        }
    }
    return 0;
}
static char *extract_destination_key(Context *c, const char *path)
{
    char *key = keep(c, strdup(path));
    if (key)
        for (char *p = key; *p; p++)
            *p = (char)toupper((unsigned char)*p);
    return key;
}
/* A complete distribution contains mutually exclusive language/display
   payloads. Keep every distinct ambiguous version without guessing which
   should become the executable at the shared destination. */
static int extract_preserve_variant(Context *c, cJSON *staged, cJSON *file)
{
    const char *original = field(file, "path"), *digest = field(file, "sha256");
    const char *base = strrchr(original, '\\');
    char *path = format(c, "C:\\OS2\\INSTALL\\MEDIA\\%s\\%s", digest, base ? base + 1 : original);
    char *key = path ? extract_destination_key(c, path) : NULL;
    if (!key)
    {
        cJSON_Delete(file);
        return 0;
    }
    put(file, "originalPath", cJSON_CreateString(original));
    put(file, "path", cJSON_CreateString(path));
    if (get(staged, key))
        cJSON_Delete(file);
    else
        cJSON_AddItemToObject(staged, key, file);
    return 1;
}
static cJSON *extract_os2_categories(Context *c, const cJSON *request)
{
    const cJSON *categories = get(request, "categories"), *category;
    const cJSON *modules = get(request, "modules");
    if (modules && !array_strings(c, modules))
        return NULL;
    if (!array_strings(c, categories) || !cJSON_GetArraySize(categories))
    {
        fail(c, "Select at least one extraction option");
        return NULL;
    }
    int selected[6] = {0};
    cJSON_ArrayForEach(category, categories)
    {
        int index = extract_category(str(category));
        if (index < 0)
        {
            fail(c, "Unknown OS/2 extraction option");
            return NULL;
        }
        selected[index] = 1;
    }
    cJSON *plan = plan_os2_extraction(c, request);
    if (!plan)
        return NULL;
    char *workspace = keep(c, tb_archive_workspace(c->ball));
    cJSON *staged = cJSON_CreateObject(), *imported = cJSON_CreateArray(), *file;
    cJSON *priorities = cJSON_CreateObject();
    cJSON *ambiguous = cJSON_CreateObject(), *variants = cJSON_CreateArray();
    cJSON_ArrayForEach(file, get(plan, "files"))
    {
        int index = extract_category(field(file, "category"));
        if (cJSON_IsTrue(get(file, "provided")) || cJSON_IsTrue(get(file, "present")) ||
            (!selected[5] && !selected[index]) ||
            !extraction_wants_module(modules, field(file, "path")))
            continue;
        char *key = extract_destination_key(c, field(file, "path"));
        const cJSON *old = get(priorities, key);
        double priority = get(file, "priority")->valuedouble;
        if (!old || old->valuedouble < priority)
        {
            cJSON_DeleteItemFromObjectCaseSensitive(priorities, key);
            cJSON_AddNumberToObject(priorities, key, priority);
        }
    }
    if (!workspace)
        fail(c, "Cannot create extraction workspace");
    uint64_t total = 0;
    int number = 0;
    cJSON_ArrayForEach(file, get(plan, "files"))
    {
        int index = extract_category(field(file, "category"));
        if (c->error || cJSON_IsTrue(get(file, "provided")) || cJSON_IsTrue(get(file, "present")) ||
            (!selected[5] && !selected[index]) ||
            !extraction_wants_module(modules, field(file, "path")))
            continue;
        const char *source = field(file, "source"), *destination = field(file, "path");
        char *key = extract_destination_key(c, destination);
        if (get(file, "priority")->valuedouble < get(priorities, key)->valuedouble)
            continue;
        FILE *input = tb_media_path(source) ? tb_media_fopen(source) : tb_fopen(source, "rb");
        if (!input || fseek(input, 0, SEEK_END))
        {
            if (input)
                fclose(input);
            fail(c, "Cannot read %s", destination);
            break;
        }
        long size = ftell(input);
        rewind(input);
        /* Individual members are bounded by the media reader; a complete
           extraction may stage at most 2 GiB before committing any files. */
        if (size < 0 || size > 256 * 1024 * 1024 ||
            total + (uint64_t)size > UINT64_C(2) * 1024 * 1024 * 1024)
        {
            fclose(input);
            fail(c, "OS/2 extraction exceeds the file or total size limit");
            break;
        }
        unsigned char *data = malloc(size ? (size_t)size : 1);
        int ok = data && fread(data, 1, (size_t)size, input) == (size_t)size;
        fclose(input);
        if (!ok)
        {
            free(data);
            fail(c, "Cannot extract %s", destination);
            break;
        }
        /* Case-insensitive destination identity is shared by OS/2 drives. */
        const cJSON *previous = get(staged, key);
        int preserve = get(ambiguous, key) != NULL;
        if (previous)
        {
            FILE *other = tb_fopen(field(previous, "temporary"), "rb");
            ok = other != NULL;
            unsigned char buffer[65536];
            size_t offset = 0, count;
            while (ok && (count = fread(buffer, 1, sizeof buffer, other)))
            {
                ok = count <= (size_t)size - offset && !memcmp(buffer, data + offset, count);
                offset += count;
            }
            ok = ok && offset == (size_t)size && !ferror(other);
            if (other)
                fclose(other);
            if (ok)
            {
                free(data);
                continue;
            }
            if (!selected[5] || modules)
            {
                free(data);
                fail(c, "Different versions of %s were found. Choose one release or language.",
                     destination);
                continue;
            }
            cJSON *old = cJSON_DetachItemFromObjectCaseSensitive(staged, key);
            if (!extract_preserve_variant(c, staged, old))
            {
                free(data);
                break;
            }
            cJSON_AddBoolToObject(ambiguous, key, 1);
            preserve = 1;
        }
        /* Retain the original format validation for CMD/REXX support files. */
        if (index == 0)
            for (int i = 0; i < SUPPORT_COUNT && !c->error; i++)
                if (equal_case(destination, support_paths[i]))
                {
                    SupportFile check = {0};
                    support_read(c, source, i, &check);
                    free(check.data);
                    free(check.source);
                }
        char *temporary = format(c, "%s/%d", workspace, number++);
        if (!c->error && !tb_atomic_write(temporary, data, (size_t)size))
            fail(c, "Cannot stage %s", destination);
        SHA256_CTX hash;
        unsigned char digest[32];
        sha256_init(&hash);
        sha256_update(&hash, data, (size_t)size);
        sha256_final(&hash, digest);
        free(data);
        total += (uint64_t)size;
        cJSON_AddStringToObject(file, "temporary", temporary);
        cJSON_AddStringToObject(file, "sha256", hex(c, digest));
        cJSON *copy = cJSON_Duplicate(file, 1);
        if (preserve)
            extract_preserve_variant(c, staged, copy);
        else
            cJSON_AddItemToObject(staged, key, copy);
    }
    cJSON_ArrayForEach(file, staged)
    {
        if (c->error)
            break;
        const char *path = field(file, "path");
        char *destination = host_path(c, path, 0),
             *parent = destination ? keep(c, strdup(destination)) : NULL;
        if (parent && strrchr(parent, '/'))
            *strrchr(parent, '/') = 0;
        int present_variant = 0;
        if (field(file, "originalPath") && destination && tb_is_file(destination))
        {
            unsigned char digest[32];
            present_variant = digest_file(c, destination, digest) &&
                              !strcmp(hex(c, digest), field(file, "sha256"));
        }
        if (!present_variant &&
            (!parent || !tb_mkdirs(parent) || !tb_copy_new(field(file, "temporary"), destination)))
        {
            fail(c, "Cannot install %s; existing files have been preserved", path);
            break;
        }
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", path);
        char *label = keep(c, tb_media_label(field(file, "source")));
        cJSON_AddStringToObject(entry, "source", label ? label : field(file, "source"));
        cJSON_AddStringToObject(entry, "sha256", field(file, "sha256"));
        if (field(file, "originalPath"))
        {
            cJSON_AddStringToObject(entry, "originalPath", field(file, "originalPath"));
            cJSON_AddItemToArray(variants, cJSON_Duplicate(entry, 1));
        }
        if (present_variant)
            cJSON_Delete(entry);
        else
            cJSON_AddItemToArray(imported, entry);
    }
    if (workspace)
        tb_remove_tree(workspace);
    cJSON_Delete(priorities);
    cJSON_Delete(ambiguous);
    cJSON_Delete(staged);
    cJSON_Delete(plan);
    if (c->error)
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
        return NULL;
    }
    int remembered = 1;
    if (!cJSON_IsFalse(get(request, "rememberSources")))
    {
        cJSON *media = cJSON_CreateObject();
        cJSON_AddItemToObject(media, "sources", cJSON_Duplicate(get(request, "sources"), 1));
        cJSON_AddStringToObject(media, "recorded", now(c));
        remembered = write_json(c, join(c, c->ball, "os2-media.json"), media);
        cJSON_Delete(media);
    }
    if (!remembered)
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
        return NULL;
    }
    char *variant_log = NULL;
    if (cJSON_GetArraySize(variants))
    {
        variant_log = join(c, c->ball, "logs/os2-extraction-variants.json");
        size_t previous_size = 0;
        char *previous = read_file(c, variant_log, &previous_size, 1);
        cJSON *history =
            previous && previous_size ? cJSON_ParseWithLength(previous, previous_size) : NULL;
        if (!cJSON_IsObject(history))
        {
            cJSON_Delete(history);
            history = cJSON_CreateObject();
        }
        const cJSON *variant;
        cJSON_ArrayForEach(variant, variants)
            put(history, field(variant, "path"), cJSON_Duplicate(variant, 1));
        int saved = write_json(c, variant_log, history);
        cJSON_Delete(history);
        if (!saved)
        {
            cJSON_Delete(imported);
            cJSON_Delete(variants);
            return NULL;
        }
    }
    cJSON *result = os2_support(c);
    if (result)
    {
        cJSON_AddItemToObject(result, "imported", imported);
        cJSON_AddItemToObject(result, "preservedVariants", variants);
        if (variant_log)
            cJSON_AddStringToObject(result, "variantManifestPath", variant_log);
    }
    else
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
    }
    return result;
}
