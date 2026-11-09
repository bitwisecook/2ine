/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest and host path translation over the ball's drive table. */
#include "ball/ball_internal.h"
#include "storage/volume.h"

char *tb_ball_case_path(TbBall *c, const char *path)
{
    if (tb_image_path(path))
    {
        TbImageInfo info;
        if (tb_image_info(path, &info))
            return tb_ball_resolve_path(c, path, 1);
        return tb_ball_resolve_path(c, path, errno == ENOENT ? 0 : 1);
    }
    char *absolute = tb_ball_resolve_path(c, path, 0);
    if (!absolute)
        return NULL;
#ifdef _WIN32
    if (tb_is_file(absolute) || tb_is_dir(absolute))
        return tb_ball_resolve_path(c, absolute, 1);
    /* Canonicalize an existing ancestor before appending a new file name, so
       a junction cannot redirect a profile outside the mapped drive. */
    char *copy = tb_ball_keep(c, strdup(absolute));
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '\\');
    if (slash && slash[1])
    {
        char *name = tb_ball_keep(c, strdup(slash + 1));
        *slash = 0;
        /* A bare "C:" names the drive's current directory, not its root. */
        if (strlen(copy) == TB_GUEST_DRIVE_LENGTH && copy[1] == ':')
            copy[2] = '\\', copy[3] = 0;
        char *parent = tb_ball_case_path(c, copy);
        return parent && name ? tb_ball_join(c, parent, name) : NULL;
    }
    return absolute;
#else
    char *result = tb_ball_keep(c, strdup("/"));
    char *parts = tb_ball_keep(c, strdup(absolute));
    if (!result || !parts)
        return NULL;
    char *cursor = NULL;
    for (char *part = strtok_r(parts, "/", &cursor); part; part = strtok_r(NULL, "/", &cursor))
    {
        size_t count = 0;
        char **names = tb_names(result, &count);
        const char *chosen = part;
        for (size_t i = 0; i < count; i++)
            if (!strcmp(names[i], part))
            {
                chosen = names[i];
                break;
            }
        if (chosen == part)
            for (size_t i = 0; i < count; i++)
                if (equal_case(names[i], part))
                {
                    chosen = names[i];
                    break;
                }
        result = tb_ball_join(c, result, chosen);
        tb_names_free(names, count);
        if (!result)
            return NULL;
    }
    if (tb_is_file(result) || tb_is_dir(result))
        return tb_ball_resolve_path(c, result, 1);
    return tb_ball_resolve_path(c, result, 0);
#endif
}

int tb_ball_inside(const char *path, const char *root)
{
    size_t n = strlen(root);
    if (!tb_path_equal(path, root, n))
        return 0;
    return !path[n] || path[n] == '/' || path[n] == '\\' || (n == 1 && root[0] == '/');
}

int tb_ball_current_drive(TbBall *c)
{
    const char *letter = field(c->manifest, "currentDrive");
    return letter && strlen(letter) == 1 && letter[0] >= 'A' && letter[0] <= 'Z' &&
                   c->roots[letter[0] - 'A']
               ? letter[0] - 'A'
               : TB_BALL_DRIVE_C;
}

const char *tb_ball_drive_directory(TbBall *c, int drive)
{
    char key[2] = {(char)('A' + drive), 0};
    const char *path = field(get(c->application, "workingDirectories"), key);
    if (!path)
        path = field(get(c->manifest, "workingDirectories"), key);
    return path ? path : tb_ball_format(c, "%c:\\", 'A' + drive);
}

char *tb_ball_host_path(TbBall *c, const char *name, int explicit_host)
{
    if (!name || !*name)
    {
        tb_ball_fail(c, "An executable or directory path is required");
        return NULL;
    }
    int named_drive = isalpha((unsigned char)name[0]) && name[1] == ':';
    /* Absolute host paths remain available to CLI adapters. Windows callers
       disambiguate host drive letters with explicit_host. */
    int guest = !explicit_host && (named_drive || name[0] == '\\' || name[0] != '/');
    if (guest)
    {
        int drive = named_drive ? toupper((unsigned char)name[0]) - 'A' : tb_ball_current_drive(c);
        if (c->unavailable[drive])
        {
            char key[2] = {(char)('A' + drive), 0};
            if (field(get(get(c->manifest, "drives"), key), "image"))
                tb_ball_fail(
                    c,
                    "No media is inserted in drive %c: because its image is unavailable. Attach an image to reconnect it.",
                    drive + 'A');
            else
                tb_ball_fail(
                    c,
                    "Drive %c: is ejected because its folder is unavailable. Choose a mountpoint in Drives.",
                    drive + 'A');
            return NULL;
        }
        if (!c->roots[drive])
        {
            tb_ball_fail(c, "Drive %c: is not mapped", drive + 'A');
            return NULL;
        }
        const char *tail = name + (named_drive ? TB_GUEST_DRIVE_LENGTH : 0);
        int rooted = *tail == '\\' || *tail == '/';
        while (*tail == '\\' || *tail == '/')
            tail++;
        const char *base = rooted ? "" : tb_ball_drive_directory(c, drive);
        if (!base)
            return NULL;
        if (*base)
        {
            if (strlen(base) < TB_GUEST_ROOT_LENGTH ||
                toupper((unsigned char)base[0]) != drive + 'A' || base[1] != ':' ||
                (base[2] != '\\' && base[2] != '/'))
            {
                tb_ball_fail(c, "Invalid working directory for drive %c:", drive + 'A');
                return NULL;
            }
            base += TB_GUEST_ROOT_LENGTH;
        }
        char *relative = tb_ball_format(c, "%s/%s", base, tail);
        if (!relative)
            return NULL;
        for (char *p = relative; *p; p++)
            if (*p == '\\')
                *p = '/';
        char *path = tb_ball_case_path(c, tb_ball_join(c, c->roots[drive], relative));
        /* A path must stay in its own drive, even when another drive contains it. */
        if (path && tb_ball_inside(path, c->roots[drive]))
            return path;
        tb_ball_fail(c, "Path is outside mapped drive %c: %s", drive + 'A', name);
        return NULL;
    }
    char *path = tb_ball_case_path(c, name);
    if (!path)
        return NULL;
    for (int i = 0; i < TB_BALL_DRIVE_COUNT; i++)
        if (c->roots[i] && tb_ball_inside(path, c->roots[i]))
            return path;
    tb_ball_fail(c, "Path is outside mapped drives: %s; map its directory first", name);
    return NULL;
}

char *tb_ball_guest_path(TbBall *c, const char *host)
{
    int best = -1;
    size_t length = 0;
    for (int i = 0; i < TB_BALL_DRIVE_COUNT; i++)
        if (c->roots[i] && tb_ball_inside(host, c->roots[i]) && strlen(c->roots[i]) >= length)
        {
            length = strlen(c->roots[i]);
            best = i;
        }
    if (best < 0)
    {
        tb_ball_fail(c, "Path is outside mapped drives: %s", host);
        return NULL;
    }
    const char *relative = host + length;
    while (*relative == '/' || *relative == '\\')
        relative++;
    char *result = tb_ball_format(c, "%c:\\%s", best + 'A', relative);
    if (result)
        for (char *p = result + TB_GUEST_ROOT_LENGTH; *p; p++)
            if (*p == '/')
                *p = '\\';
    return result;
}

char *tb_ball_guest_for_input(TbBall *c, const char *host, const char *input)
{
    if (input && isalpha((unsigned char)input[0]) && input[1] == ':')
    {
        int drive = toupper((unsigned char)input[0]) - 'A';
        if (c->roots[drive] && tb_ball_inside(host, c->roots[drive]))
        {
            const char *relative = host + strlen(c->roots[drive]);
            while (*relative == '/' || *relative == '\\')
                relative++;
            char *path = tb_ball_format(c, "%c:\\%s", 'A' + drive, relative);
            if (path)
                for (char *p = path; *p; p++)
                    if (*p == '/')
                        *p = '\\';
            return path;
        }
    }
    return tb_ball_guest_path(c, host);
}

int tb_ball_validate_directories(TbBall *c, const cJSON *directories)
{
    if (!directories)
        return 1;
    if (!cJSON_IsObject(directories))
        return tb_ball_fail(c, "Working directories must be an object");
    cJSON *entry;
    cJSON_ArrayForEach(entry, directories)
    {
        const char *path = str(entry), *key = entry->string;
        if (!key || strlen(key) != 1 || key[0] < 'A' || key[0] > 'Z' ||
            (!c->roots[key[0] - 'A'] && !c->unavailable[key[0] - 'A']) || !path ||
            strlen(path) < TB_GUEST_ROOT_LENGTH || path[0] != key[0] || path[1] != ':' ||
            path[2] != '\\')
            return tb_ball_fail(c, "Use a mapped drive letter and its absolute OS/2 directory");
        if (c->unavailable[key[0] - 'A'])
            continue;
        char *host = tb_ball_host_path(c, path, 0);
        if (!host || !tb_is_dir(host))
            return tb_ball_fail(c, "Working directory does not exist: %s", path);
    }
    return 1;
}
