/* Public domain. No rights reserved. Wholly AI-authored. */
/* Ball commands: drive mappings, settings and launcher records. */
#include "ball/ball_internal.h"
#include "ball/host_environment.h"
#include "ball/runtime_config.h"
#include "storage/volume.h"

int tb_ball_command(TbBall *c, const cJSON *request)
{
    cJSON *args = get(request, "arguments");
    if (!tb_ball_array_strings(c, args))
        return 0;
    int n = cJSON_GetArraySize(args);
    const char *action = str(cJSON_GetArrayItem(args, 0));
    if (!action)
        return tb_ball_fail(c, "A command is required");
    const char *name = str(cJSON_GetArrayItem(args, 1));
    if (!strcmp(action, "map") || !strcmp(action, "map-image") || !strcmp(action, "unmap") ||
        !strcmp(action, "device") || !strcmp(action, "eject") || !strcmp(action, "restore-drive") ||
        !strcmp(action, "create-floppy") || !strcmp(action, "create-disk") ||
        !strcmp(action, "device-lock") || !strcmp(action, "media-readonly"))
    {
        if (!name || !isalpha((unsigned char)name[0]) || (name[1] && strcmp(name + 1, ":")))
            return tb_ball_fail(c, "Choose a drive A-Z other than C:");
        char letter[2] = {(char)toupper((unsigned char)name[0]), 0};
        if (letter[0] == 'C')
            return tb_ball_fail(c, "C: is private to the ball");
        cJSON *drives = object(c->manifest, "drives");
        if (!drives)
            return tb_ball_fail(c, "Invalid drives object");
        cJSON *existing = get(drives, letter);
        if (cJSON_IsTrue(get(existing, "locked")) && strcmp(action, "device-lock"))
            return tb_ball_fail(
                c,
                "Drive %s: is locked to its image. Unlock it in Settings before changing its media or access mode.",
                letter);
        if (!strcmp(action, "device-lock") || !strcmp(action, "media-readonly"))
        {
            const char *value = str(cJSON_GetArrayItem(args, 2));
            if (!value || (strcmp(value, "on") && strcmp(value, "off")))
                return tb_ball_fail(c, "Choose on or off");
            if (!field(existing, "device"))
                return tb_ball_fail(c, "Choose a removable device");
            int enabled = !strcmp(value, "on");
            if (!strcmp(action, "device-lock"))
            {
                if (enabled && !field(existing, "image"))
                    return tb_ball_fail(c, "Attach an image before locking this device");
                put(existing, "locked", cJSON_CreateBool(enabled));
            }
            else
            {
                if (!strcmp(field(existing, "device"), "cdrom") && !enabled)
                    return tb_ball_fail(c, "CD images are read-only");
                put(existing, "readonly", cJSON_CreateBool(enabled));
            }
            return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
        }
        if (!strcmp(action, "create-disk"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2)),
                       *filesystem = str(cJSON_GetArrayItem(args, 3)),
                       *size = str(cJSON_GetArrayItem(args, 4));
            char *end = NULL;
            unsigned long long mib = size ? strtoull(size, &end, 10) : 0;
            if (!source || !filesystem || !size || !end || *end || mib < 8 || mib > 2097152)
                return tb_ball_fail(c, "Choose an image size from 8 MiB to 2 TiB");
            if (strcmp(filesystem, "fat32") && strcmp(filesystem, "exfat") &&
                strcmp(filesystem, "hpfs"))
                return tb_ball_fail(c, "Choose FAT32, exFAT or HPFS");
            if (!strcmp(filesystem, "fat32") && mib < 64)
                return tb_ball_fail(c, "FAT32 images need at least 64 MiB");
            if (!strcmp(filesystem, "hpfs") && (mib > 65536 || mib % 8))
                return tb_ball_fail(
                    c, "Choose an HPFS size from 8 MiB to 64 GiB, in multiples of 8 MiB");
            if (existing &&
                (!field(existing, "device") || strcmp(field(existing, "device"), "disk")))
                return tb_ball_fail(c, "Choose a disk device or an unused drive letter");
            char *path = tb_ball_resolve_path(c, source, 0);
            if (!path)
                return 0;
            if (!tb_image_create_volume(path, filesystem, (uint64_t)mib * 1024 * 1024))
                return tb_ball_fail(c, "Cannot create %s image %s: %s", filesystem, path,
                                    strerror(errno));
            cJSON *mapping = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
            put(mapping, "device", cJSON_CreateString("disk"));
            put(mapping, "image", cJSON_CreateString(path));
            put(drives, letter, mapping);
            return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
        }
        if (!strcmp(action, "create-floppy"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2)),
                       *size = str(cJSON_GetArrayItem(args, 3));
            char *end = NULL;
            unsigned long kib = size ? strtoul(size, &end, 10) : 0;
            if (!source || !size || !end || *end ||
                (kib != 360 && kib != 720 && kib != 1200 && kib != 1440 && kib != 2880))
                return tb_ball_fail(c, "Choose a 360, 720, 1200, 1440 or 2880 KiB floppy image");
            if (existing &&
                (!field(existing, "device") || strcmp(field(existing, "device"), "floppy")))
                return tb_ball_fail(c, "Choose a floppy device or an unused drive letter");
            char *path = tb_ball_resolve_path(c, source, 0);
            if (!path)
                return 0;
            if (!tb_image_create_floppy(path, (unsigned)kib))
                return tb_ball_fail(c, "Cannot create floppy image %s: %s", path, strerror(errno));
            cJSON *mapping = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
            put(mapping, "device", cJSON_CreateString("floppy"));
            put(mapping, "image", cJSON_CreateString(path));
            put(drives, letter, mapping);
            return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
        }
        if (!strcmp(action, "map-image"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2));
            if (!source)
                return tb_ball_fail(c, "An ISO or floppy image is required");
            char *image = tb_ball_resolve_path(c, source, 1);
            if (!image)
                return 0;
            char *root = tb_ball_keep(c, tb_image_root(image));
            TbImageInfo info;
            if (!root || !tb_image_info(root, &info))
            {
                int error = errno;
                return tb_ball_fail(c, "Cannot read ISO 9660 or FAT12/FAT16 image %s: %s", image,
                                    strerror(error));
            }
            cJSON *previous = get(drives, letter);
            cJSON *mapping =
                cJSON_IsObject(previous) ? cJSON_Duplicate(previous, 1) : cJSON_CreateObject();
            const char *detected = !strcmp(info.filesystem, "CDFS")  ? "cdrom"
                                   : !strcmp(info.filesystem, "FAT") ? "floppy"
                                                                     : "disk";
            const char *kind = field(get(drives, letter), "device");
            if (kind && strcmp(kind, detected) &&
                (strcmp(kind, "disk") || !strcmp(detected, "cdrom")))
            {
                cJSON_Delete(mapping);
                return tb_ball_fail(
                    c, "Choose %s media for %s:",
                    !strcmp(kind, "floppy") ? "a FAT12/FAT16 floppy" : "an ISO 9660 CD", letter);
            }
            put(mapping, "device", cJSON_CreateString(kind ? kind : detected));
            put(mapping, "image", cJSON_CreateString(image));
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "device"))
        {
            const char *kind = str(cJSON_GetArrayItem(args, 2));
            if (!kind || (strcmp(kind, "cdrom") && strcmp(kind, "floppy") && strcmp(kind, "disk")))
                return tb_ball_fail(c, "Choose a cdrom, floppy or disk device");
            if (get(drives, letter))
                return tb_ball_fail(c, "Drive %s: is already allocated", letter);
            cJSON *mapping = cJSON_CreateObject();
            cJSON_AddStringToObject(mapping, "device", kind);
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "eject"))
        {
            cJSON *mapping = get(drives, letter);
            const char *kind = field(mapping, "device");
            if (!kind && !field(mapping, "image"))
                return tb_ball_fail(c, "%s: is not a removable device", letter);
            if (!kind)
                cJSON_AddStringToObject(mapping, "device", "cdrom");
            cJSON_DeleteItemFromObjectCaseSensitive(mapping, "image");
            cJSON_DeleteItemFromObjectCaseSensitive(get(c->manifest, "workingDirectories"), letter);
        }
        else if (!strcmp(action, "restore-drive"))
        {
            const char *record = str(cJSON_GetArrayItem(args, 2));
            cJSON *mapping = record ? cJSON_Parse(record) : NULL;
            if (!mapping ||
                (!str(mapping) && !field(mapping, "device") && !field(mapping, "image")))
            {
                cJSON_Delete(mapping);
                return tb_ball_fail(c, "Invalid drive record");
            }
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "map"))
        {
            const char *dir = str(cJSON_GetArrayItem(args, 2));
            if (!dir)
                return tb_ball_fail(c, "A host directory is required");
            char *path = tb_ball_resolve_path(c, dir, 1);
            if (!path)
                return 0;
            if (!tb_is_dir(path))
                return tb_ball_fail(c, "Not a directory: %s", path);
            if (!tb_directory_readable(path))
            {
                int error = errno;
                return tb_ball_fail(c, "Cannot read directory %s: %s", path,
                                    strerror(error ? error : EACCES));
            }
            put(drives, letter, cJSON_CreateString(path));
        }
        else
            cJSON_DeleteItemFromObjectCaseSensitive(drives, letter);
        return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
    }
    if (!strcmp(action, "config") || !strcmp(action, "unset"))
    {
        if (!name)
            return tb_ball_fail(c, "A setting key is required");
        const char *dot = strchr(name, '.');
        if (!dot || !dot[1])
            return tb_ball_fail(c, "Use env.NAME, system.NAME or runtime.backend");
        char *group = tb_ball_keep(c, strdup(name));
        group[dot - name] = 0;
        const char *key = dot + 1;
        if (strcmp(group, "env") && strcmp(group, "system") && strcmp(group, "runtime"))
            return tb_ball_fail(c, "Unknown setting group");
        if (!strcmp(group, "runtime") && strcmp(key, "backend"))
            return tb_ball_fail(c, "Unknown runtime setting");
        cJSON *settings = object(c->manifest, !strcmp(group, "env") ? "environment" : group);
        if (!settings)
            return tb_ball_fail(c, "Invalid settings object");
        if (!strcmp(action, "unset"))
            cJSON_DeleteItemFromObjectCaseSensitive(settings, key);
        else
        {
            const char *value = str(cJSON_GetArrayItem(args, 2));
            if (!value)
                return tb_ball_fail(c, "A setting value is required");
            put(settings, key, cJSON_CreateString(value));
        }
        if (!tb_ball_validate_settings(c, request))
            return 0;
        return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
    }
    if (!strcmp(action, "add-app"))
    {
        int at = 1;
        const char *cwd = NULL;
        if (name && !strcmp(name, "--cwd"))
        {
            cwd = str(cJSON_GetArrayItem(args, 2));
            at = 3;
        }
        name = str(cJSON_GetArrayItem(args, at++));
        const char *program = str(cJSON_GetArrayItem(args, at++));
        if (!name || !program)
            return tb_ball_fail(c, "Application name and executable are required");
        char *host = tb_ball_host_path(c, program, 0);
        if (!host || !tb_is_file(host))
            return tb_ball_fail(c, "Program does not exist: %s", program);
        cJSON *apps = object(c->manifest, "applications");
        if (!apps)
            return tb_ball_fail(c, "Invalid applications object");
        cJSON *existing = get(apps, name);
        cJSON *app = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
        tb_ball_detach_application(c, app);
        put(app, "program", cJSON_CreateString(tb_ball_guest_for_input(c, host, program)));
        cJSON *argv = cJSON_CreateArray();
        put(app, "arguments", argv);
        for (; at < n; at++)
            cJSON_AddItemToArray(argv, cJSON_Duplicate(cJSON_GetArrayItem(args, at), 1));
        if (cwd && *cwd)
        {
            char *directory = tb_ball_host_path(c, cwd, 0);
            if (!directory || !tb_is_dir(directory))
            {
                cJSON_Delete(app);
                return tb_ball_fail(c, "Working directory must be a mapped directory");
            }
            put(app, "cwd", cJSON_CreateString(tb_ball_guest_for_input(c, directory, cwd)));
        }
        put(apps, name, app);
        return tb_ball_save_manifest(c);
    }
    if (!strcmp(action, "restore-app"))
    {
        const char *encoded = str(cJSON_GetArrayItem(args, 2));
        cJSON *app = encoded ? cJSON_ParseWithOpts(encoded, NULL, 1) : NULL;
        if (!name || !cJSON_IsObject(app) || !field(app, "program") ||
            !cJSON_IsArray(get(app, "arguments")))
        {
            cJSON_Delete(app);
            return tb_ball_fail(c, "Invalid application record");
        }
        cJSON *apps = object(c->manifest, "applications");
        if (!apps || get(apps, name))
        {
            cJSON_Delete(app);
            return tb_ball_fail(c, "An application with that name already exists");
        }
        const char *registration = field(get(app, "wpsRegistration"), "handle");
        if (registration)
            cJSON_DeleteItemFromObjectCaseSensitive(get(c->manifest, "wpsHidden"), registration);
        put(apps, name, app);
        return tb_ball_save_manifest(c);
    }
    if (!strcmp(action, "remove-app"))
    {
        if (!name)
            return tb_ball_fail(c, "An application name is required");
        tb_ball_hide_application(c, get(get(c->manifest, "applications"), name));
        cJSON_DeleteItemFromObjectCaseSensitive(get(c->manifest, "applications"), name);
        return tb_ball_save_manifest(c);
    }
    return tb_ball_fail(c, "Unsupported ball command: %s", action);
}
