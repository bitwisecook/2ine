/* Public domain. No rights reserved. Wholly AI-authored. */
/* A command-line guest has no LaunchServices identity of its own: the Dock
   shows the responsible terminal's icon under the loader's executable name,
   and neither can be changed after the process connects to the window
   server. This file builds one private immutable bundle per launched program
   and loader, presents the program's own OS/2 name and icon through its
   bundle identity, and re-executes the loader through it. Manager launches
   already run through DesktopLaunchBundle.swift and pass a desktop session,
   which this code leaves untouched. Set TWOINE_DESKTOP_BUNDLE=0 to skip. */
#define _DARWIN_C_SOURCE 1
#include "hal/host_desktop_bundle.h"
#include "ball/icon.h"
#include "twoine_ball_icon_png.h"
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Bundles are cached per program and loader, so repeated launches reuse one
   bundle instead of copying the loader every time. */
#define LX_BUNDLE_NAME_LIMIT 180
#define LX_BUNDLE_PROGRAM_LIMIT (64u * 1024u * 1024u)
#define LX_BUNDLE_LOADER_LIMIT (256u * 1024u * 1024u)
#define LX_BUNDLE_KEY_LENGTH 16

static int session_active(const char *name)
{
    const char *value = getenv(name);
    return value && *value && strcmp(value, "0");
}

static int disabled(void)
{
    const char *value = getenv("TWOINE_DESKTOP_BUNDLE");
    return value && *value && !strcmp(value, "0");
}

static char *join_path(const char *parent, const char *child)
{
    char *joined;
    if (asprintf(&joined, "%s/%s", parent, child) < 0)
        return NULL;
    return joined;
}

static char *loader_path(void)
{
    uint32_t size = 0;
    if (_NSGetExecutablePath(NULL, &size))
        return NULL;
    char buffer[size];
    if (_NSGetExecutablePath(buffer, &size))
        return NULL;
    return realpath(buffer, NULL);
}

/* Something.app/Contents/MacOS/exe already carries a bundle identity. */
static int inside_bundle(const char *path)
{
    size_t length = strlen(path);
    static const char suffix[] = ".app/Contents/MacOS/";
    return length >= sizeof suffix - 1 && !strcmp(path + length - (sizeof suffix - 1), suffix);
}

static int ensure_directory(const char *path)
{
    struct stat info;
    if (!mkdir(path, 0700))
        return 1;
    if (errno != EEXIST)
        return 0;
    return !stat(path, &info) && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode);
}

static unsigned char *read_bytes(const char *path, size_t limit, size_t *size)
{
    int file = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (file < 0)
        return NULL;
    struct stat info;
    if (fstat(file, &info) || !S_ISREG(info.st_mode) || (uint64_t)info.st_size > limit)
    {
        close(file);
        return NULL;
    }
    size_t count = (size_t)info.st_size;
    unsigned char *bytes = malloc(count ? count : 1);
    if (!bytes)
    {
        close(file);
        return NULL;
    }
    size_t done = 0;
    while (done < count)
    {
        ssize_t part = read(file, bytes + done, count - done);
        if (part <= 0)
            break;
        done += (size_t)part;
    }
    close(file);
    if (done != count)
    {
        free(bytes);
        return NULL;
    }
    *size = count;
    return bytes;
}

static int write_bytes(const char *path, const unsigned char *bytes, size_t count, mode_t mode)
{
    int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, mode);
    if (file < 0)
        return 0;
    int ok = 1;
    size_t done = 0;
    while (done < count)
    {
        ssize_t part = write(file, bytes + done, count - done);
        if (part <= 0)
        {
            ok = 0;
            break;
        }
        done += (size_t)part;
    }
    if (ok && fchmod(file, mode))
        ok = 0;
    close(file);
    if (!ok)
        unlink(path);
    return ok;
}

static int copy_bytes(const char *from, const char *to, size_t limit, mode_t mode)
{
    size_t size = 0;
    unsigned char *bytes = read_bytes(from, limit, &size);
    if (!bytes)
        return 0;
    int ok = write_bytes(to, bytes, size, mode);
    free(bytes);
    return ok;
}

/* Mirrors tb_icon_rgba_base64: top-down rows, and the AND mask decides the
   alpha of pixels the icon itself leaves transparent. */
static int write_png(const char *path, const TbIconImage *image)
{
    size_t pixels = (size_t)image->width * image->height;
    unsigned char *rgba = malloc(pixels * 4);
    if (!rgba)
        return 0;
    for (unsigned y = 0; y < image->height; y++)
        for (unsigned x = 0; x < image->width; x++)
        {
            size_t source = (size_t)(image->height - 1 - y) * image->width + x,
                   target = ((size_t)y * image->width + x) * 4;
            uint32_t rgb = image->pixels[source];
            rgba[target] = (unsigned char)(rgb >> 16);
            rgba[target + 1] = (unsigned char)(rgb >> 8);
            rgba[target + 2] = (unsigned char)rgb;
            rgba[target + 3] = image->mask[source] ? (rgb ? 128 : 0) : 255;
        }
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, rgba, pixels * 4, NULL);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef rendered =
        provider && space
            ? CGImageCreate(image->width, image->height, 8, 32, (size_t)image->width * 4, space,
                            kCGImageAlphaLast, provider, NULL, false, kCGRenderingIntentDefault)
            : NULL;
    CFURLRef url = rendered ? CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                                      strlen(path), false)
                            : NULL;
    CGImageDestinationRef destination =
        url ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL) : NULL;
    int ok = 0;
    if (destination)
    {
        CGImageDestinationAddImage(destination, rendered, NULL);
        ok = CGImageDestinationFinalize(destination);
    }
    if (destination)
        CFRelease(destination);
    if (url)
        CFRelease(url);
    if (rendered)
        CGImageRelease(rendered);
    if (space)
        CGColorSpaceRelease(space);
    if (provider)
        CGDataProviderRelease(provider);
    free(rgba);
    return ok;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static int write_fallback_png(const char *path)
{
    const char *hex = TWOINE_BALL_ICON_PNG_HEX;
    size_t length = strlen(hex);
    if (length % 2 || length / 2 != TWOINE_BALL_ICON_PNG_SIZE)
        return 0;
    unsigned char *bytes = malloc(TWOINE_BALL_ICON_PNG_SIZE);
    if (!bytes)
        return 0;
    for (size_t i = 0; i < TWOINE_BALL_ICON_PNG_SIZE; i++)
    {
        int high = hex_value(hex[2 * i]), low = hex_value(hex[2 * i + 1]);
        if (high < 0 || low < 0)
        {
            free(bytes);
            return 0;
        }
        bytes[i] = (unsigned char)(high << 4 | low);
    }
    int ok = write_bytes(path, bytes, TWOINE_BALL_ICON_PNG_SIZE, 0644);
    free(bytes);
    return ok;
}

static char *escaped_name(const char *name)
{
    size_t size = strlen(name);
    if (size > LX_BUNDLE_NAME_LIMIT)
        size = LX_BUNDLE_NAME_LIMIT;
    char *escaped = malloc(size * 6 + 1);
    if (!escaped)
        return NULL;
    size_t out = 0;
    for (size_t i = 0; i < size; i++)
    {
        switch (name[i])
        {
            case '&':
                memcpy(escaped + out, "&amp;", 5);
                out += 5;
                break;
            case '<':
                memcpy(escaped + out, "&lt;", 4);
                out += 4;
                break;
            case '>':
                memcpy(escaped + out, "&gt;", 4);
                out += 4;
                break;
            case '\"':
                memcpy(escaped + out, "&quot;", 6);
                out += 6;
                break;
            case '\'':
                memcpy(escaped + out, "&apos;", 6);
                out += 6;
                break;
            default:
                escaped[out++] = name[i];
        }
    }
    escaped[out] = 0;
    return escaped;
}

/* The module description names the program; otherwise the file stem does. */
static char *display_name(const unsigned char *bytes, size_t size, const char *program)
{
    char *description = bytes ? tb_executable_description(bytes, size) : NULL;
    if (description && *description)
    {
        if (strlen(description) > LX_BUNDLE_NAME_LIMIT)
            description[LX_BUNDLE_NAME_LIMIT] = 0;
        return description;
    }
    free(description);
    const char *base = program;
    for (const char *at = program; *at; at++)
        if (*at == '/' || *at == '\\')
            base = at + 1;
    size_t length = strlen(base);
    if (length > LX_BUNDLE_NAME_LIMIT)
        length = LX_BUNDLE_NAME_LIMIT;
    char *name = malloc(length + 1);
    if (!name)
        return NULL;
    memcpy(name, base, length);
    name[length] = 0;
    char *dot = strrchr(name, '.');
    if (dot && dot != name)
        *dot = 0;
    if (!*name)
    {
        free(name);
        return NULL;
    }
    return name;
}

static int write_info(const char *path, const char *name, const char *identifier)
{
    char *escaped = escaped_name(name);
    if (!escaped)
        return 0;
    int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (file < 0)
    {
        free(escaped);
        return 0;
    }
    int ok = dprintf(file,
                     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
                     " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                     "<plist version=\"1.0\">\n<dict>\n"
                     "<key>CFBundleExecutable</key><string>lx_loader</string>\n"
                     "<key>CFBundleName</key><string>%s</string>\n"
                     "<key>CFBundleDisplayName</key><string>%s</string>\n"
                     "<key>CFBundleIdentifier</key><string>%s</string>\n"
                     "<key>CFBundlePackageType</key><string>APPL</string>\n"
                     "<key>CFBundleVersion</key><string>1</string>\n"
                     "<key>CFBundleIconFile</key><string>ApplicationIcon.png</string>\n"
                     "<key>LSUIElement</key><true/>\n"
                     "<key>NSHighResolutionCapable</key><true/>\n"
                     "</dict>\n</plist>\n",
                     escaped, escaped, identifier) > 0;
    if (ok && fchmod(file, 0644))
        ok = 0;
    close(file);
    if (!ok)
        unlink(path);
    free(escaped);
    return ok;
}

static int remove_tree(const char *path, int depth)
{
    struct stat info;
    if (depth > 8 || lstat(path, &info))
        return 0;
    if (S_ISDIR(info.st_mode))
    {
        DIR *directory = opendir(path);
        if (directory)
        {
            struct dirent *entry;
            while ((entry = readdir(directory)))
            {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                    continue;
                char *child = join_path(path, entry->d_name);
                if (child)
                {
                    remove_tree(child, depth + 1);
                    free(child);
                }
            }
            closedir(directory);
        }
        rmdir(path);
        return 1;
    }
    if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
        unlink(path);
    return 1;
}

/* Only this file's own bundle names expire by age; the manager's UUID
   session directories do not match this pattern. */
static int session_bundle_name(const char *name)
{
    size_t length = strlen(name);
    if (length < LX_BUNDLE_KEY_LENGTH + 4)
        return 0;
    for (size_t i = 0; i < LX_BUNDLE_KEY_LENGTH; i++)
        if (hex_value(name[i]) < 0)
            return 0;
    if (length == LX_BUNDLE_KEY_LENGTH + 4)
        return !strcmp(name + LX_BUNDLE_KEY_LENGTH, ".app");
    return length > LX_BUNDLE_KEY_LENGTH + 9 &&
           !strncmp(name + LX_BUNDLE_KEY_LENGTH, ".app.new-", 9);
}

static void prune_sessions(const char *sessions)
{
    DIR *directory = opendir(sessions);
    if (!directory)
        return;
    time_t now = time(NULL);
    struct dirent *entry;
    while ((entry = readdir(directory)))
    {
        if (!session_bundle_name(entry->d_name))
            continue;
        char *path = join_path(sessions, entry->d_name);
        if (!path)
            continue;
        struct stat info;
        int temporary = strlen(entry->d_name) > LX_BUNDLE_KEY_LENGTH + 9;
        if (!stat(path, &info) && S_ISDIR(info.st_mode) &&
            difftime(now, info.st_mtimespec.tv_sec) > (temporary ? 3600 : 14 * 86400))
            remove_tree(path, 0);
        free(path);
    }
    closedir(directory);
}

/* Codesigning seals the new bundle's own resources; presentation works
   unsigned, so a missing or failing codesign never blocks a launch. */
static void sign_bundle(const char *path)
{
    pid_t child = fork();
    if (child < 0)
        return;
    if (!child)
    {
        const char *argv[] = {
            "codesign", "--force", "--sign", "-", "--preserve-metadata=entitlements",
            "--",       path,      NULL};
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
}

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static void bundle_key(char *key, size_t size, const char *program, const struct stat *program_info,
                       const char *loader, const struct stat *loader_info)
{
    uint32_t words[4] = {
        (uint32_t)program_info->st_size, (uint32_t)program_info->st_mtimespec.tv_sec,
        (uint32_t)loader_info->st_size, (uint32_t)loader_info->st_mtimespec.tv_sec};
    uint64_t hash = fnv1a(0xcbf29ce484222325ULL, program, strlen(program) + 1);
    hash = fnv1a(hash, words, sizeof words);
    hash = fnv1a(hash, loader, strlen(loader) + 1);
    snprintf(key, size, "%016llx", (unsigned long long)hash);
}

static char *cache_root(void)
{
    const char *override = getenv("TWOINE_DESKTOP_CACHE");
    if (override && *override == '/')
        return ensure_directory(override) ? strdup(override) : NULL;
    const char *home = getenv("HOME");
    if (!home || *home != '/' || strpbrk(home + 1, "/"))
        return NULL;
    static const char parts[3][26] = {"Library", "Caches", "io.github.bitwisecook.2ine"};
    char *root = strdup(home);
    for (unsigned i = 0; root && i < 3; i++)
    {
        char *next = join_path(root, parts[i]);
        free(root);
        root = next && ensure_directory(next) ? next : (free(next), (char *)NULL);
    }
    return root;
}

static int ensure_bundle(const char *bundle, const char *sessions, const char *key,
                         const char *program, const char *loader)
{
    struct stat existing;
    if (!stat(bundle, &existing) && S_ISDIR(existing.st_mode))
        return 1;
    char staged[96];
    snprintf(staged, sizeof staged, "%s.new-%d", key, getpid());
    char *staging = join_path(sessions, staged);
    if (!staging)
        return 0;
    remove_tree(staging, 0);
    char *contents = join_path(staging, "Contents"), *macho = NULL, *resources = NULL;
    if (contents)
        macho = join_path(contents, "MacOS");
    if (contents)
        resources = join_path(contents, "Resources");
    int ok = 0;
    if (macho && resources && ensure_directory(staging) && ensure_directory(contents) &&
        ensure_directory(macho) && ensure_directory(resources))
    {
        size_t size = 0;
        unsigned char *bytes = read_bytes(program, LX_BUNDLE_PROGRAM_LIMIT, &size);
        TbIconImage *icon = bytes ? tb_icon_executable(bytes, size) : NULL;
        char *name = display_name(bytes, size, program);
        char *executable = join_path(macho, "lx_loader"),
             *icon_path = join_path(resources, "ApplicationIcon.png"),
             *info_path = join_path(contents, "Info.plist");
        char identifier[80];
        snprintf(identifier, sizeof identifier, "io.github.bitwisecook.2ine.session.%s", key);
        if (name && executable && icon_path && info_path &&
            copy_bytes(loader, executable, LX_BUNDLE_LOADER_LIMIT, 0755) &&
            (icon ? write_png(icon_path, icon) : write_fallback_png(icon_path)) &&
            write_info(info_path, name, identifier))
        {
            sign_bundle(staging);
            if (!rename(staging, bundle))
                ok = 1;
            else
            {
                struct stat winner;
                if (errno == ENOTEMPTY || errno == EEXIST || errno == EISDIR ||
                    (!stat(bundle, &winner) && S_ISDIR(winner.st_mode)))
                    ok = 1;
            }
        }
        free(name);
        free(executable);
        free(icon_path);
        free(info_path);
        if (icon)
            tb_icon_free(icon);
        free(bytes);
    }
    free(macho);
    free(resources);
    free(contents);
    if (!ok)
        remove_tree(staging, 0);
    free(staging);
    return ok && !stat(bundle, &existing) && S_ISDIR(existing.st_mode);
}

void lx_desktop_bundle_exec(const char *program, char **argv)
{
    if (!program || !*program || *program == '-' || !argv || !argv[1])
        return;
    if (disabled() || session_active("TWOINE_DESKTOP_SESSION") ||
        session_active("TWOINE_DESKTOP_AUXILIARY"))
        return;
    char *loader = loader_path(), *resolved = NULL;
    struct stat program_info, loader_info;
    if (!loader || inside_bundle(loader) || stat(loader, &loader_info) ||
        !S_ISREG(loader_info.st_mode))
        goto done;
    resolved = realpath(program, NULL);
    if (!resolved || stat(resolved, &program_info) || !S_ISREG(program_info.st_mode))
        goto done;
    {
        char *root = cache_root(), *sessions = root ? join_path(root, "desktop-sessions") : NULL;
        if (!sessions || !ensure_directory(sessions))
        {
            free(sessions);
            free(root);
            goto done;
        }
        char key[LX_BUNDLE_KEY_LENGTH + 1];
        bundle_key(key, sizeof key, resolved, &program_info, loader, &loader_info);
        prune_sessions(sessions);
        char *bundle = join_path(sessions, key);
        if (!bundle)
        {
            free(sessions);
            free(root);
            goto done;
        }
        if (ensure_bundle(bundle, sessions, key, resolved, loader))
        {
            char *executable = join_path(bundle, "Contents/MacOS/lx_loader"), *shared = NULL;
            const char *ipc = getenv("TWOINE_IPC_DIR");
            if ((!ipc || !*ipc) && executable)
            {
                shared = join_path(root, "ipc");
                if (!shared || !ensure_directory(shared))
                {
                    free(shared);
                    shared = NULL;
                }
            }
            if (executable && (ipc || shared))
            {
                unsigned char session[16];
                char value[33];
                arc4random_buf(session, sizeof session);
                for (size_t i = 0; i < sizeof session; i++)
                    snprintf(value + 2 * i, 3, "%02x", session[i]);
                setenv("TWOINE_DESKTOP_SESSION", value, 1);
                setenv("TWOINE_DESKTOP_STANDALONE", "1", 1);
                if (shared)
                    setenv("TWOINE_IPC_DIR", shared, 1);
                execv(executable, argv);
                unsetenv("TWOINE_DESKTOP_SESSION");
                unsetenv("TWOINE_DESKTOP_STANDALONE");
                unsetenv("TWOINE_IPC_DIR");
            }
            free(shared);
            free(executable);
        }
        free(bundle);
        free(sessions);
        free(root);
    }
done:
    free(resolved);
    free(loader);
}
