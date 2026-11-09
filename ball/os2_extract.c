/* Public domain. No rights reserved. Wholly AI-authored. */
/* Category-based OS/2 media planning and staged extraction into a ball. */
#include "ball/ball_internal.h"
#include "storage/container.h"
#include "ball/drives.h"
#include "ball/os2_component_store.h"
#include "ball/os2_extract.h"
#include "ball/os2_groups.h"
#include "ball/os2_paths.h"
#include "ball/os2_som.h"
#include "ball/os2_support.h"
#include "ball/os2_tree.h"
#include "ball/paths.h"
#include "ball/system_modules.h"

int extract_extension(const char *name, const char *extension)
{
    const char *dot = strrchr(name, '.');
    return dot && equal_case(dot, extension);
}

int extract_video(const char *name)
{
    static const char *extensions[] = {".AVI", ".MPG", ".MPEG", ".MOV",  ".FLI",
                                       ".FLC", ".MP4", ".MKV",  ".WEBM", NULL};
    for (int i = 0; extensions[i]; i++)
        if (extract_extension(name, extensions[i]))
            return 1;
    return 0;
}

/* The host supplies kernel device/volume roles; guest kernel drivers have no
   load path. Printer resources and code-page/country data remain selectable. */
static int extract_host_driver(const char *path)
{
    if (strlen(path) >= 1024)
        return 0;
    char normalized[1024];
    for (size_t i = 0; i <= strlen(path); i++)
        normalized[i] = path[i] == '/' ? '\\' : (char)toupper((unsigned char)path[i]);
    const char *relative = !strncmp(normalized, "C:\\", 3) ? normalized + 3 : normalized;
    const char *base = strrchr(relative, '\\');
    if (!base)
        return 0;
    base++;
    if (!strcmp(base, "COUNTRY.SYS") || !strcmp(base, "CONFIG.SYS"))
        return 0;
    int kernel = !strncmp(relative, "OS2\\BOOT\\", 9) ||
                 (!strncmp(relative, "OS2\\", 4) && !strchr(relative + 4, '\\'));
    if (kernel && (extract_extension(base, ".ADD") || extract_extension(base, ".IFS") ||
                   extract_extension(base, ".DMD") || extract_extension(base, ".FLT")))
        return 1;
    int network_driver =
        !strncmp(relative, "IBMCOM\\MACS\\", 12) || !strncmp(relative, "IBMCOM\\PROTOCOL\\", 16);
    if (extract_extension(base, ".SYS") &&
        (kernel || network_driver || !strncmp(relative, "OS2\\MDOS\\", 9) ||
         !strncmp(relative, "MMOS2\\", 6)))
        return 1;
    return network_driver && extract_extension(base, ".OS2");
}

static char *extract_child(Context *c, const char *directory, const char *name)
{
    return tb_media_path(directory) ? tb_ball_keep(c, tb_media_child(directory, name))
                                    : tb_ball_join(c, directory, name);
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
            return tb_ball_fail(c, "Cannot read media folder: %s", source);
        for (size_t i = 0; i < count; i++)
        {
            char *child = tb_ball_join(c, source, names[i]);
            if (child && !tb_is_link(child))
                extract_queue(queue, child, tb_ball_format(c, "%s/%s", layout, names[i]),
                              tb_is_dir(child), 0);
        }
        tb_names_free(names, count);
        return !c->error;
    }
    size_t count = 0;
    TbMediaEntry *entries = tb_media_entries(source, &count);
    if (count == (size_t)-1)
        return tb_ball_fail(c, "Cannot read media or archive: %s", source);
    for (size_t i = 0; i < count && !c->error; i++)
    {
        char *child = extract_child(c, source, entries[i].name);
        if (!child)
            tb_ball_fail(c, "Cannot name media member");
        else
            extract_queue(queue, child,
                          *layout ? tb_ball_format(c, "%s/%s", layout, entries[i].name)
                                  : entries[i].name,
                          entries[i].directory, (double)entries[i].size);
    }
    tb_media_entries_free(entries, count);
    return !c->error;
}

/* Optional sibling component trees are discovered by exact installed-root
   paths inside the same container. Direct folder plans never call this. */
static char *extract_optional_directory(Context *c, const char *parent, const char *relative)
{
    char *current = tb_ball_keep(c, strdup(parent)), *parts = tb_ball_keep(c, strdup(relative));
    for (char *part = parts; current && part && *part;)
    {
        char *next = strchr(part, '/');
        if (next)
            *next++ = 0;
        cJSON *listing = browse_media(c, current);
        if (!listing)
        {
            free(c->error);
            c->error = NULL;
            return NULL;
        }
        const cJSON *entry;
        char *found = NULL;
        cJSON_ArrayForEach(entry,
                           get(listing, "entries")) if (cJSON_IsTrue(get(entry, "directory")) &&
                                                        equal_case(field(entry, "name"), part))
        {
            found = tb_ball_keep(c, strdup(field(entry, "path")));
            break;
        }
        cJSON_Delete(listing);
        current = found;
        part = next;
    }
    return current;
}

int extract_expand_installation_roots(Context *c, cJSON *sources)
{
    if (cJSON_GetArraySize(sources) != 1)
        return 0;
    const char *base = str(cJSON_GetArrayItem(sources, 0));
    char *member = tb_ball_keep(c, tb_media_member_name(base));
    if (!tb_media_path(base) || !member || !equal_case(member, "OS2IMAGE"))
        return 0;
    char *parent = tb_ball_keep(c, tb_media_parent(base));
    if (!parent)
        return 0;
    static const char *roots[] = {"CID/SERVER/MPTS/MPTN", "CID/SERVER/MPTS/IBMCOM",
                                  "CID/SERVER/BOOKS", NULL};
    for (int i = 0; roots[i]; i++)
    {
        char *path = extract_optional_directory(c, parent, roots[i]);
        if (path)
            cJSON_AddItemToArray(sources, cJSON_CreateString(path));
    }
    return 1;
}

cJSON *open_os2_source(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!tb_ball_array_strings(c, sources) || !cJSON_GetArraySize(sources))
    {
        tb_ball_fail(c, "Choose OS/2 installation media");
        return NULL;
    }
    cJSON *queue = cJSON_CreateArray(), *detected = cJSON_CreateArray();
    const cJSON *source;
    cJSON_ArrayForEach(source, sources)
        extract_queue(queue, str(source), "", extract_directory(str(source)), 0);
    int at = 0;
    int selected_sources = cJSON_GetArraySize(sources);
    /* Children are appended while walking. Advancing the linked list visits
       those additions without rescanning the entire queue for each file. */
    for (const cJSON *node = queue->child; node && !c->error; node = node->next, at++)
    {
        if (at >= 100000)
        {
            tb_ball_fail(c, "Media search exceeded 100000 entries. Choose a smaller source.");
            break;
        }
        const char *path = field(node, "source");
        char *name = tb_ball_keep(c, tb_media_member_name(path));
        if (name && extract_extension(name, ".iso"))
        {
            cJSON_AddItemToArray(detected, cJSON_CreateString(path));
            continue;
        }
        /* Explicit folders, including remembered folders inside an ISO, stay
           at that folder. Discovered archive directories are searched for ISOs. */
        if ((!tb_media_path(path) && tb_is_dir(path)) ||
            (at < selected_sources && name && *name && cJSON_IsTrue(get(node, "directory"))))
            continue;
        if (cJSON_IsTrue(get(node, "directory")))
            extract_children(c, queue, path, "");
        else if (name && media_candidate(name) && tb_media_container(path))
        {
            char *root = tb_ball_keep(c, tb_media_root(path));
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
    cJSON *chosen = found ? cJSON_CreateArray() : cJSON_Duplicate(sources, 1),
          *skipped = cJSON_CreateArray(), *directory = NULL;
    if (found)
    {
        const cJSON *image;
        cJSON_ArrayForEach(image, detected)
        {
            cJSON *opened = browse_media(c, str(image));
            if (!opened)
            {
                cJSON *warning = cJSON_CreateObject();
                char *label = tb_media_label(str(image));
                cJSON_AddStringToObject(warning, "path", label ? label : str(image));
                cJSON_AddStringToObject(warning, "reason",
                                        c->error ? c->error : "Unreadable ISO image");
                cJSON_AddItemToArray(skipped, warning);
                free(label);
                free(c->error);
                c->error = NULL;
                continue;
            }
            cJSON_AddItemToArray(chosen, cJSON_Duplicate(image, 1));
            if (!directory)
                directory = opened;
            else
                cJSON_Delete(opened);
        }
        found = cJSON_GetArraySize(chosen);
        if (!found)
            tb_ball_fail(c, "No readable ISO images were found: %s",
                         field(cJSON_GetArrayItem(skipped, 0), "reason"));
    }
    else
        directory = browse_media(c, str(cJSON_GetArrayItem(chosen, 0)));
    cJSON_Delete(detected);
    if (!directory)
    {
        cJSON_Delete(chosen);
        cJSON_Delete(skipped);
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
            char *path = tb_ball_keep(c, strdup(field(entry, "path")));
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
    installation_directory |= extract_expand_installation_roots(c, chosen);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "sources", chosen);
    cJSON_AddItemToObject(result, "directory", directory);
    cJSON_AddBoolToObject(result, "detectedISO", found != 0);
    cJSON_AddBoolToObject(result, "installationDirectory", installation_directory);
    cJSON_AddItemToObject(result, "skippedSources", skipped);
    cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
    return result;
}

static char *extract_extra_path(Context *c, const char *layout, const char *source)
{
    char *label = tb_ball_keep(c, tb_media_label(source));
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
            char *copy = tb_ball_keep(c, strdup(root));
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
    return tb_ball_format(c, "OS2/INSTALL/MEDIA/%s", relative);
}

/* Match the actual installation layout rather than placing an ISO's setup
   tree under C:. PACK/PACK2 members retain their original target directories. */
static char *extract_destination(Context *c, const char *layout, const char *source, int *category,
                                 int *provided, int *priority)
{
    *priority = 1;
    char *path = tb_ball_keep(c, strdup(layout)), *upper = tb_ball_keep(c, strdup(layout));
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
            return tb_ball_keep(c, strdup(support_paths[i]));
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
    *category = EXTRACT_FULL;
    if (extract_extension(base, ".PFB") || extract_extension(base, ".PFA") ||
        extract_extension(base, ".PFM") || extract_extension(base, ".AFM") ||
        extract_extension(base, ".TTF") || extract_extension(base, ".TTC") ||
        extract_extension(base, ".OTF") || extract_extension(base, ".FON") ||
        extract_extension(base, ".FNT") || extract_extension(base, ".OFM"))
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
    else if (extract_module_group(base) >= 0)
        *category = extract_module_group(base);
    else if (extract_som_file(base))
        *category = EXTRACT_SOM;
    if (!strcmp(base, "OPENGL.DLL") || !strcmp(base, "PGL.DLL"))
    {
        *category = EXTRACT_OPENGL;
        *provided = 0;
    }
    char *label = tb_ball_keep(c, tb_media_label(source));
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
    if (*category == EXTRACT_FULL && anchor)
    {
        const char *root = upper + (anchor - path);
        if (!strncmp(root, "MMOS2/", 6))
            *category = EXTRACT_MULTIMEDIA;
        else if (!strncmp(root, "TCPIP/", 6) || !strncmp(root, "MPTN/", 5) ||
                 !strncmp(root, "IBMLAN/", 7) || !strncmp(root, "IBMCOM/", 7))
            *category = EXTRACT_NETWORK;
    }
    {
        const char *root = anchor ? upper + (anchor - path) : "";
        if (!strncmp(root, "OS2/MDOS/", 9))
            *category = EXTRACT_LEGACY;
        else if (!strncmp(root, "LANGUAGE/", 9) || !strncmp(root, "OS2/LANGUAGE/", 13) ||
                 extract_extension(base, ".DCP") || extract_extension(base, ".NLS") ||
                 !strcmp(base, "COUNTRY.SYS") || !strcmp(base, "GLOCALE.EXE"))
            *category = EXTRACT_LANGUAGES;
        else if (!strncmp(root, "MAINTENANCE/", 12) || strstr(root, "/TRACE/") ||
                 !strcmp(base, "PSTAT.EXE") || !strcmp(base, "TRACE.EXE") ||
                 !strcmp(base, "TRACEFMT.EXE") || !strcmp(base, "STRACE.EXE") ||
                 !strcmp(base, "CHKDSK.EXE") || !strcmp(base, "CHKDSK32.EXE") ||
                 !strcmp(base, "FDISK.EXE") || !strcmp(base, "LVM.EXE") ||
                 !strcmp(base, "PROCDUMP.EXE") || !strcmp(base, "SYSLOGPM.EXE"))
            *category = EXTRACT_DIAGNOSTICS;
        else if ((!strncmp(root, "OS2/APPS/", 9) || !strcmp(base, "E.EXE")) &&
                 *category != EXTRACT_HELP && *category != EXTRACT_FONTS)
            *category = EXTRACT_UTILITIES;
    }
    /* Win-OS/2 is a separate compatibility tree. Its flat Windows packages
       must not collide with native OS/2 help books, DLLs or printer drivers. */
    int windows = label && strstr(label, "/DISK_W");
    if (windows)
    {
        *category = EXTRACT_LEGACY;
        if (!relative)
        {
            int system = extract_extension(base, ".DLL") || extract_extension(base, ".DRV") ||
                         extract_extension(base, ".FON") || extract_extension(base, ".CPL") ||
                         extract_extension(base, ".VBX") || extract_extension(base, ".386") ||
                         extract_extension(base, ".TTF");
            relative = tb_ball_format(c, "%s/%s",
                                      system ? "OS2/MDOS/WINOS2/SYSTEM" : "OS2/MDOS/WINOS2", base);
        }
    }
    if (!relative && *category == 1)
        relative = tb_ball_format(c, "%s/%s",
                                  extract_extension(base, ".PFB") || extract_extension(base, ".PFA")
                                      ? "PSFONTS"
                                  : extract_extension(base, ".PFM") ? "PSFONTS/PFM"
                                  : extract_extension(base, ".AFM") ? "PSFONTS/AFM"
                                                                    : "OS2/DLL",
                                  base);
    else if (!relative && *category == 2)
        relative = tb_ball_format(
            c, "%s/%s",
            extract_video(base)                                                  ? "MMOS2/MOVIE"
            : extract_extension(base, ".WAV") || extract_extension(base, ".MID") ? "MMOS2/SOUNDS"
                                                                                 : "OS2/BITMAP",
            base);
    else if (!relative && *category == 3)
        relative = tb_ball_format(c, "OS2/DLL/%s", path);
    else if (!relative && *category == 4)
        relative = tb_ball_format(c, "%s/%s",
                                  extract_extension(base, ".DLL")   ? "OS2/DLL"
                                  : extract_extension(base, ".INF") ? "OS2/BOOK"
                                  : extract_extension(base, ".HLP") ? "OS2/HELP"
                                                                    : "OS2",
                                  base);
    else if (!relative && *category == EXTRACT_SOM && !extract_extension(base, ".DLL"))
        relative = tb_ball_format(c, "%s/%s",
                                  !strcmp(base, "SOM.IR") ? "OS2/ETC"
                                  : !strncmp(base, "SOMDIMPL.", 9) || !strncmp(base, "SOMDCLS.", 8)
                                      ? "OS2/ETC/DSOM"
                                  : extract_extension(base, ".MSG") ? "OS2/SYSTEM"
                                                                    : "OS2",
                                  base);
    else if (!relative && (extract_extension(base, ".EXE") || extract_extension(base, ".DLL") ||
                           extract_extension(base, ".SYS") || extract_extension(base, ".DCP") ||
                           extract_extension(base, ".MSG") || extract_extension(base, ".ADD") ||
                           extract_extension(base, ".DMD") || extract_extension(base, ".IFS") ||
                           extract_extension(base, ".FLT")))
        relative = tb_ball_format(c, "%s/%s",
                                  extract_extension(base, ".DLL") ? "OS2/DLL"
                                  : extract_extension(base, ".SYS") && strcmp(base, "COUNTRY.SYS")
                                      ? "OS2/BOOT"
                                      : "OS2",
                                  base);
    if (!relative)
        relative = *provided ? tb_ball_format(c, "%s", base) : extract_extra_path(c, path, source);
    if ((label && strstr(label, "/DEBUG/")) || extract_extension(base, ".SYM") ||
        extract_extension(base, ".MAP"))
    {
        *category = EXTRACT_DIAGNOSTICS;
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
    char *destination = tb_ball_format(c, "C:\\%s", relative);
    for (char *p = destination; *p; p++)
        if (*p == '/')
            *p = '\\';
    if (extract_host_driver(destination))
        *provided = 1;
    return destination;
}

cJSON *plan_os2_extraction(Context *c, const cJSON *request)
{
    const cJSON *sources = get(request, "sources");
    if (!tb_ball_array_strings(c, sources) || !cJSON_GetArraySize(sources))
    {
        tb_ball_fail(c, "Choose an OS/2 source first");
        return NULL;
    }
    cJSON *result = cJSON_CreateObject(), *files = cJSON_AddArrayToObject(result, "files"),
          *queue = cJSON_CreateArray(), *unmapped = cJSON_AddArrayToObject(result, "unmapped");
    cJSON_AddItemToObject(result, "groups", extract_groups());
    const cJSON *source;
    cJSON_ArrayForEach(source, sources)
    {
        char *name = tb_ball_keep(c, tb_media_member_name(str(source)));
        if (name)
            extract_queue(queue, str(source), name, extract_directory(str(source)), 0);
    }
    int at = 0;
    for (const cJSON *node = queue->child; node && !c->error; node = node->next, at++)
    {
        if (!(at % 64) && component_cancelled(c))
            break;
        if (at >= 100000)
        {
            tb_ball_fail(c, "Media search exceeded 100000 entries. Browse to a smaller source.");
            break;
        }
        const char *path = field(node, "source"), *layout = field(node, "layout");
        if (!tb_media_path(path) && tb_is_link(path))
        {
            tb_ball_fail(c, "Symbolic links cannot be imported");
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
            char *root = tb_ball_keep(c, tb_media_root(path));
            if (root)
                extract_children(c, queue, root, "");
            continue;
        }
        int category = EXTRACT_FULL, provided = 0, priority = 1;
        char *destination = extract_destination(c, layout, path, &category, &provided, &priority);
        if (!destination)
        {
            cJSON_AddItemToArray(unmapped, cJSON_CreateString(layout));
            continue;
        }
        char *host = tb_ball_host_path(c, destination, 0);
        if (!host)
            break;
        cJSON *file = cJSON_CreateObject();
        cJSON_AddStringToObject(file, "path", destination);
        cJSON_AddStringToObject(file, "source", path);
        cJSON_AddStringToObject(file, "category", extract_categories[category]);
        cJSON_AddBoolToObject(file, "provided", provided);
        if (extract_host_driver(destination))
            cJSON_AddStringToObject(
                file, "providedReason",
                "The host supplies this kernel driver role; original kernel drivers cannot be loaded by 2ine.");
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
    cJSON_AddItemToObject(result, "tree", extract_tree(c, files));
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
    char *key = tb_ball_keep(c, strdup(path));
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
    char *path =
        tb_ball_format(c, "C:\\OS2\\INSTALL\\MEDIA\\%s\\%s", digest, base ? base + 1 : original);
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

static cJSON *extract_os2_categories_impl(Context *c, const cJSON *request)
{
    const cJSON *categories = get(request, "categories"), *category;
    const cJSON *modules = get(request, "modules");
    const cJSON *file_selection = get(request, "fileSelection");
    const cJSON *configure = get(request, "configureComponents"), *component;
    int configure_som = 0;
    if (configure && !tb_ball_array_strings(c, configure))
        return NULL;
    cJSON_ArrayForEach(component, configure)
    {
        if (extract_category(str(component)) < 0)
        {
            tb_ball_fail(c, "Unknown OS/2 component configuration option");
            return NULL;
        }
        if (equal_case(str(component), "som"))
            configure_som = 1;
    }
    if (modules && !tb_ball_array_strings(c, modules))
        return NULL;
    if ((!categories && !file_selection) ||
        (categories && (!tb_ball_array_strings(c, categories) ||
                        (!file_selection && !cJSON_GetArraySize(categories)))))
    {
        tb_ball_fail(c, "Select at least one extraction option");
        return NULL;
    }
    int selected[EXTRACT_COUNT] = {0};
    cJSON_ArrayForEach(category, categories)
    {
        int index = extract_category(str(category));
        if (index < 0)
        {
            tb_ball_fail(c, "Unknown OS/2 extraction option");
            return NULL;
        }
        selected[index] = 1;
    }
    if (!file_selection && selected[EXTRACT_SOM])
        selected[EXTRACT_RUNTIME] = 1;
    cJSON *plan = plan_os2_extraction(c, request);
    if (!plan)
        return NULL;
    ExtractFileSelection selection = {0};
    if (!extract_selection(c, request, plan, &selection))
    {
        cJSON_Delete(plan);
        return NULL;
    }
    if (selection.explicit)
    {
        memset(selected, 0, sizeof selected);
        const cJSON *file;
        cJSON_ArrayForEach(
            file, get(plan, "files")) if (extract_file_selected(&selection, selected, file))
            selected[extract_category(field(file, "category"))] = 1;
        if (configure_som)
        {
            int som_files = 0;
            cJSON_ArrayForEach(file,
                               get(plan, "files")) if (equal_case(field(file, "category"), "som") &&
                                                       !cJSON_IsTrue(get(file, "provided")))
            {
                som_files++;
                if (!extract_file_selected(&selection, selected, file))
                    tb_ball_fail(c, "Select every SOM file before configuring its service");
            }
            if (!som_files)
                tb_ball_fail(c, "The selected source contains no SOM service files");
            if (c->error)
            {
                cJSON_Delete(plan);
                return NULL;
            }
        }
    }
    int som_configured = 0;
    char *workspace = tb_ball_keep(c, tb_archive_workspace(c->ball));
    cJSON *staged = cJSON_CreateObject(), *imported = cJSON_CreateArray(), *file;
    cJSON *priorities = cJSON_CreateObject();
    cJSON *skipped = cJSON_CreateArray();
    cJSON *ambiguous = cJSON_CreateObject(), *variants = cJSON_CreateArray();
    cJSON_ArrayForEach(file, get(plan, "files"))
    {
        if (cJSON_IsTrue(get(file, "provided")) || cJSON_IsTrue(get(file, "present")) ||
            !extract_file_selected(&selection, selected, file) ||
            (!selection.explicit && !extraction_wants_module(modules, field(file, "path"))))
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
    if (c->component_operation)
        put(c->component_operation->progress, "totalCount",
            cJSON_CreateNumber(cJSON_GetArraySize(priorities)));
    if (!workspace)
        tb_ball_fail(c, "Cannot create extraction workspace");
    uint64_t total = 0;
    int number = 0;
    cJSON_ArrayForEach(file, get(plan, "files"))
    {
        int index = extract_category(field(file, "category"));
        if (c->error || cJSON_IsTrue(get(file, "provided")) || cJSON_IsTrue(get(file, "present")) ||
            !extract_file_selected(&selection, selected, file) ||
            (!selection.explicit && !extraction_wants_module(modules, field(file, "path"))))
            continue;
        const char *source = field(file, "source"), *destination = field(file, "path");
        char *key = extract_destination_key(c, destination);
        int alternate = selection.explicit &&
                        get(file, "priority")->valuedouble < get(priorities, key)->valuedouble;
        if (!selection.explicit &&
            get(file, "priority")->valuedouble < get(priorities, key)->valuedouble)
            continue;
        if (component_cancelled(c))
            break;
        component_progress(c, "extracting", source, destination);
        if (c->error)
            break;
        FILE *input = tb_media_path(source) ? tb_media_fopen(source) : tb_fopen(source, "rb");
        if (!input || fseek(input, 0, SEEK_END))
        {
            int reason = errno;
            if (input)
                fclose(input);
            if (index == 1 && !modules &&
                (reason == EILSEQ || reason == EINVAL || reason == EIO || reason == ENOTSUP))
            {
                cJSON *warning = cJSON_CreateObject();
                cJSON_AddStringToObject(warning, "path", destination);
                char *label = tb_media_label(source);
                cJSON_AddStringToObject(warning, "source", label ? label : source);
                free(label);
                cJSON_AddStringToObject(
                    warning, "reason",
                    reason == EILSEQ ? "The source archive failed its size or CRC integrity check."
                                     : "The source font could not be read or decompressed.");
                cJSON_AddItemToArray(skipped, warning);
                continue;
            }
            tb_ball_fail(c, "Cannot read %s from the selected media: %s", destination,
                         reason == EILSEQ ? "archive size or CRC integrity check failed"
                                          : strerror(reason));
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
            tb_ball_fail(c, "OS/2 extraction exceeds the file or total size limit");
            break;
        }
        unsigned char *data = malloc(size ? (size_t)size : 1);
        int ok = data && fread(data, 1, (size_t)size, input) == (size_t)size;
        fclose(input);
        if (!ok)
        {
            free(data);
            tb_ball_fail(c, "Cannot extract %s", destination);
            break;
        }
        /* Case-insensitive destination identity is shared by OS/2 drives. */
        const cJSON *previous = alternate ? NULL : get(staged, key);
        int preserve = alternate || get(ambiguous, key) != NULL;
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
            if (!selection.explicit && (!selected[EXTRACT_FULL] || modules))
            {
                free(data);
                tb_ball_fail(c,
                             "Different versions of %s were found. Choose one release or language.",
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
        char *temporary = tb_ball_format(c, "%s/%d", workspace, number++);
        if (!c->error && !tb_atomic_write(temporary, data, (size_t)size))
            tb_ball_fail(c, "Cannot stage %s", destination);
        SHA256_CTX hash;
        unsigned char digest[32];
        sha256_init(&hash);
        sha256_update(&hash, data, (size_t)size);
        sha256_final(&hash, digest);
        free(data);
        total += (uint64_t)size;
        cJSON_AddStringToObject(file, "temporary", temporary);
        cJSON_AddStringToObject(file, "sha256", tb_ball_hex(c, digest));
        component_progress(c, "staged", source, destination);
        cJSON *copy = cJSON_Duplicate(file, 1);
        if (preserve)
            extract_preserve_variant(c, staged, copy);
        else
            cJSON_AddItemToObject(staged, key, copy);
    }
    if (c->component_operation)
        put(c->component_operation->progress, "totalCount",
            cJSON_CreateNumber(cJSON_GetArraySize(staged)));
    cJSON_ArrayForEach(file, staged)
    {
        if (component_cancelled(c))
            break;
        const char *path = field(file, "path");
        component_progress(c, "installing", field(file, "source"), path);
        if (c->error)
            break;
        char *destination = component_private_path(c, path),
             *parent = destination ? tb_ball_keep(c, strdup(destination)) : NULL;
        if (parent && strrchr(parent, '/'))
            *strrchr(parent, '/') = 0;
        int present_variant = 0;
        if (field(file, "originalPath") && destination && tb_is_file(destination))
        {
            unsigned char digest[32];
            present_variant = tb_ball_digest_file(c, destination, digest) &&
                              !strcmp(tb_ball_hex(c, digest), field(file, "sha256"));
        }
        if (!present_variant &&
            (!parent || !tb_mkdirs(parent) || !tb_copy_new(field(file, "temporary"), destination)))
        {
            tb_ball_fail(c, "Cannot install %s; existing files have been preserved", path);
            break;
        }
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", path);
        char *label = tb_ball_keep(c, tb_media_label(field(file, "source")));
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
        {
            cJSON_AddItemToArray(imported, entry);
            component_record(c, path, field(file, "source"), field(file, "category"),
                             field(file, "sha256"));
            component_progress(c, "installing", field(file, "source"), path);
        }
    }
    if (workspace)
        tb_remove_tree(workspace);
    cJSON_Delete(priorities);
    cJSON_Delete(ambiguous);
    cJSON_Delete(staged);
    component_progress(c, "finishing", NULL, NULL);
    if (!component_cancelled(c))
    {
        cJSON *paths = cJSON_CreateArray();
        cJSON_ArrayForEach(file, get(plan, "files"))
        {
            if (!cJSON_IsTrue(get(file, "provided")) &&
                extract_file_selected(&selection, selected, file) &&
                (selection.explicit || extraction_wants_module(modules, field(file, "path"))))
                cJSON_AddItemToArray(paths, cJSON_Duplicate(file, 1));
        }
        os2_configure_paths(c, paths);
        cJSON_Delete(paths);
        if ((selection.explicit && configure_som) ||
            (!selection.explicit && !modules && (selected[EXTRACT_SOM] || selected[EXTRACT_FULL])))
        {
            if (os2_som_provision(c, NULL) && os2_som_complete(c))
                som_configured = 1;
        }
    }
    cJSON_Delete(plan);
    if (c->error)
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
        cJSON_Delete(skipped);
        return NULL;
    }
    int remembered = 1;
    if (!cJSON_IsFalse(get(request, "rememberSources")))
    {
        cJSON *media = cJSON_CreateObject();
        cJSON_AddItemToObject(media, "sources", cJSON_Duplicate(get(request, "sources"), 1));
        cJSON_AddStringToObject(media, "recorded", tb_ball_now(c));
        cJSON *selection = cJSON_AddArrayToObject(media, "selectedCategories");
        for (int i = 0; i < EXTRACT_COUNT; i++)
            if (selected[i])
                cJSON_AddItemToArray(selection, cJSON_CreateString(extract_categories[i]));
        remembered = tb_ball_write_json(c, tb_ball_join(c, c->ball, "os2-media.json"), media);
        cJSON_Delete(media);
    }
    if (!remembered)
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
        cJSON_Delete(skipped);
        return NULL;
    }
    char *variant_log = NULL;
    if (cJSON_GetArraySize(variants))
    {
        variant_log = tb_ball_join(c, c->ball, "logs/os2-extraction-variants.json");
        size_t previous_size = 0;
        char *previous = tb_ball_read_file(c, variant_log, &previous_size, 1);
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
        int saved = tb_ball_write_json(c, variant_log, history);
        cJSON_Delete(history);
        if (!saved)
        {
            cJSON_Delete(imported);
            cJSON_Delete(variants);
            cJSON_Delete(skipped);
            return NULL;
        }
    }
    cJSON *result = os2_support(c);
    if (result)
    {
        cJSON_AddStringToObject(result, "selectionMode",
                                selection.explicit ? "files" : "categories");
        cJSON_AddBoolToObject(result, "automaticRequirementsApplied", !selection.explicit);
        cJSON_AddBoolToObject(result, "dependenciesChecked", 0);
        cJSON *configured = cJSON_AddArrayToObject(result, "configuredComponents");
        if (som_configured)
            cJSON_AddItemToArray(configured, cJSON_CreateString("som"));
        cJSON *selection = cJSON_CreateArray();
        put(result, "selectedCategories", selection);
        for (int i = 0; i < EXTRACT_COUNT; i++)
            if (selected[i])
                cJSON_AddItemToArray(selection, cJSON_CreateString(extract_categories[i]));
        cJSON_AddItemToObject(result, "groups", extract_groups());
        cJSON_AddItemToObject(result, "imported", imported);
        cJSON_AddItemToObject(result, "preservedVariants", variants);
        cJSON_AddItemToObject(result, "skippedFiles", skipped);
        cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
        if (variant_log)
            cJSON_AddStringToObject(result, "variantManifestPath", variant_log);
    }
    else
    {
        cJSON_Delete(imported);
        cJSON_Delete(variants);
        cJSON_Delete(skipped);
    }
    return result;
}

/* Dependency recovery uses the same ownership and cooperative locking contract. */
cJSON *extract_os2_categories(Context *c, const cJSON *request)
{
    int owns_job = c->component_operation == NULL;
    cJSON *result = NULL;
    if ((!owns_job || component_begin(c, request, "install")) && !component_cancelled(c))
        result = extract_os2_categories_impl(c, request);
    if (owns_job)
        component_finish(c, result);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}
