/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "lib2ine.h"
#include "runtime/host.h"
#include "runtime/host_thread.h"
#include "runtime/host_fs.h"
#include "SDL.h"

extern char **environ;

LxLoaderState GLoaderState;

static LX_HOST_THREAD_LOCAL LxTIB *DefaultTib;
static LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;
LxHostThreadState *lxHostThreadState(void)
{
    return &host_thread_state;
}

static void initPib(void)
{
    LxPIB *pib = &GLoaderState.pib;
    memset(pib, '\0', sizeof(*pib));

    size_t cmdlinebuflen = 0;
    char *cmdlinebuf = lx_host_command_line(&cmdlinebuflen);
    if (!cmdlinebuf)
    {
        abort();
    }

    int argc = 0;
    for (size_t i = 0; i < cmdlinebuflen; i++)
    {
        if (cmdlinebuf[i] == '\0')
        {
            argc++;
        }
    }

    char **argv = (char **)calloc(argc + 1, sizeof(char *));
    if (!argv)
    {
        fprintf(stderr, "Out of memory\n");
        abort();
    }

    argv[0] = cmdlinebuf;
    argc = 0;
    for (size_t i = 0; i < cmdlinebuflen; i++)
    {
        if (cmdlinebuf[i] == '\0')
        {
            argc++;
            argv[argc] = &cmdlinebuf[i + 1];
        }
    }
    argv[argc] = NULL;

    int is_lx_loader = 0;
    {
        const char *ptr = strrchr(argv[0], '/');
        if (ptr)
        {
            ptr++;
        }
        else
        {
            ptr = argv[0];
        }
        is_lx_loader = (strcmp(ptr, "lx_loader") == 0);
    }

    if (is_lx_loader)
    {
        argv++;
        argc--;
    }

    size_t len = 1;
    for (int i = 0; i < argc; i++)
    {
        int needs_escape = 0;
        int num_backslashes = 0;
        for (const char *arg = argv[i]; *arg; arg++)
        {
            const char ch = *arg;
            if ((ch == ' ') || (ch == '\t'))
                needs_escape = 1;
            else if ((ch == '\\') || (ch == '\"'))
                num_backslashes++;
            len++;
        }

        if (needs_escape)
        {
            len += 2 + num_backslashes;
        }

        len++;
    }

    len += strlen(argv[0]) + 1;

    char **envp = environ;

    char path[255];
    snprintf(path, 254, "PATH=%s", getenv("OS2PATH"));
    for (int i = 0; envp[i]; i++)
    {
        const char *str = envp[i];
        if (strncmp(str, "PATH=", 5) == 0)
        {
            if (!GLoaderState.subprocess)
                str = path;
        }
        else if (strncmp(str, "IS_2INE=", 8) == 0)
        {
            continue;
        }
        len += strlen(str) + 1;
    }

    len += 4;

    char *env = NULL;
    if (posix_memalign((void **)&env, 0x10000, 0x10000) != 0)
    {
        fprintf(stderr, "Out of memory\n");
        abort();
    }

    memset(env, 0, 0x10000);
    if (len > 0x10000)
    {
        fprintf(stderr, "OS/2 environment and command exceed one segment\n");
        abort();
    }
    const char *libpath = NULL;
    char *ptr = env;
    for (int i = 0; envp[i]; i++)
    {
        const char *str = envp[i];
        if (strncmp(str, "PATH=", 5) == 0)
        {
            if (!GLoaderState.subprocess)
                str = path;
        }
        else if (strncmp(str, "LIBPATH=", 8) == 0)
        {
            libpath = str + 8;
        }
        else if (strncmp(str, "IS_2INE=", 8) == 0)
        {
            continue;
        }

        strcpy(ptr, str);
        ptr += strlen(str) + 1;
    }
    *(ptr++) = '\0';

    if (libpath == NULL)
        libpath = "";

    GLoaderState.libpath = strdup(libpath);
    GLoaderState.libpathlen = strlen(libpath) + 1;

    strcpy(ptr, argv[0]);
    ptr += strlen(argv[0]) + 1;

    char *cmd = ptr;
    strcpy(ptr, argv[0]);
    ptr += strlen(argv[0]);
    *(ptr++) = '\0';
    for (int i = 1; i < argc; i++)
    {
        int needs_escape = 0;
        for (const char *arg = argv[i]; *arg; arg++)
        {
            const char ch = *arg;
            if ((ch == ' ') || (ch == '\t'))
                needs_escape = 1;
        }

        if (needs_escape)
        {
            *(ptr++) = '"';
            for (const char *arg = argv[i]; *arg; arg++)
            {
                const char ch = *arg;
                if ((ch == '\\') || (ch == '\n'))
                    *(ptr++) = '\\';
                *(ptr++) = ch;
            }
            *(ptr++) = '"';
        }
        else
        {
            for (const char *arg = argv[i]; *arg; arg++)
                *(ptr++) = *arg;
        }

        if (i < (argc - 1))
            *(ptr++) = ' ';
    }

    *(ptr++) = '\0';

    lx_host_process_ids(&pib->pib_ulpid, &pib->pib_ulppid);
    pib->pib_pchcmd = cmd;
    pib->pib_pchenv = env;

    if (is_lx_loader)
    {
        argv--;
    }

    free(cmdlinebuf);
    free(argv);
}

static void dosExit_lib2ine(uint32 action, uint32 result) {}

static void initOs2Tib_lib2ine(uint8 *tibspace, void *_topOfStack, const size_t stacklen,
                               const uint32 tid)
{
    uint8 *topOfStack = (uint8 *)_topOfStack;

    LxTIB *tib = (LxTIB *)tibspace;
    LxTIB2 *tib2 = (LxTIB2 *)(tib + 1);
    FIXME("This is probably 50% wrong");
    uint32 top = (uint32)(uintptr_t)topOfStack;
    uint32 tib_address = 0;
    if (GLoaderState.execution)
    {
        lx_execution_address(GLoaderState.execution, tibspace, LXTIBSIZE, 0, &tib_address);
        uint32 guest_top;
        if (lx_execution_address(GLoaderState.execution, topOfStack, 0, 0, &guest_top))
            top = guest_top;
    }
    tib->tib_pexchain = 0;
    tib->tib_pstack = top - stacklen;
    tib->tib_pstacklimit = top;
    tib->tib_ptib2 = tib_address ? tib_address + sizeof(LxTIB) : 0;
    tib->tib_version = 20;
    tib->tib_ordinal = 79;

    tib2->tib2_ultid = tid;
    tib2->tib2_ulpri = 512;
    tib2->tib2_version = 20;
    tib2->tib2_usMCCount = 0;
    tib2->tib2_fMCForceFlag = 0;
}

static uint16 setOs2Tib_lib2ine(uint8 *tibspace)
{
    DefaultTib = (LxTIB *)tibspace;
    return 1;
}

static LxTIB *getOs2Tib_lib2ine(void)
{
    return DefaultTib;
}

static void deinitOs2Tib_lib2ine(const uint16 selector)
{
    DefaultTib = NULL;
}

static int findSelector_lib2ine(const uint32 addr, uint16 *outselector, uint16 *outoffset,
                                int iscode)
{
    return 0; // always fail. This is for 16-bit support.
}

static void freeSelector_lib2ine(const uint16 selector)
{
    // no-op. This is for 16-bit support.
}

static void *convert1616to32_lib2ine(const uint32 addr1616)
{
    return NULL; // always fail. This is for 16-bit support.
}

static uint32 convert32to1616_lib2ine(void *addr32)
{
    return 0; // always fail. This is for 16-bit support.
}

static LxModule *loadModule_lib2ine(const char *modname)
{
    return NULL;
}

static char *makeUnixPath_lib2ine(const char *os2path, uint32 *err)
{
    // this doesn't convert any OS/2 paths. For native apps, use native paths.
    char *retval = strdup(os2path);
    if (!retval)
    {
        *err = 8;
    }
    return retval;
}

static void *allocSegment_lib2ine(uint16 *selector, const int iscode)
{
    if (selector)
        *selector = 0xFFFF;
    return NULL;
}

static void freeSegment_lib2ine(const uint16 selector) {}

static void __attribute__((noreturn)) terminate_lib2ine(const uint32 exitcode)
{
    exit((int)exitcode);
}

static void cfgWarn(const char *fname, const int lineno, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "2ine cfg warning: [%s:%d] ", fname, lineno);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static int cfgIsSpace(const int ch)
{
    switch (ch)
    {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            return 1;
    }
    return 0;
}

static int cfgIsSymChar(const int ch)
{
    return ((ch == '_') || ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')));
}

static void cfgProcessBoolString(const char *fname, const int lineno, int *output, const char *val)
{
    static const char *y[] = {"1", "yes", "y", "true", "t", "on"};
    static const char *n[] = {"0", "no", "n", "false", "f", "off"};
    int i;
    for (i = 0; i < (sizeof(y) / sizeof(y[0])); i++)
    {
        if (strcasecmp(val, y[i]) == 0)
        {
            *output = 1;
            return;
        }
        if (strcasecmp(val, n[i]) == 0)
        {
            *output = 0;
            return;
        }
    }
    cfgWarn(fname, lineno, "\"%s\" is not valid for this setting. Try 1 or 0.", val);
}

static void cfgProcessFloatString(const char *fname, const int lineno, float *output,
                                  const char *val)
{
    errno = 0;
    const float converted = strtof(val, NULL);
    if (!errno)
    {
        *output = converted;
    }
    else
    {
        cfgWarn(fname, lineno, "\"%s\" is not valid for this setting. Try a number.", val);
    }
}

static void cfgProcessMountPoint(const char *fname, const int lineno, const char *var,
                                 const char *val)
{
    char letter = var[0];
    if ((letter >= 'a') && (letter <= 'z'))
    {
        letter -= 'a' - 'A';
    }

    if ((letter < 'A') || (letter > 'Z') || (var[1] != '\0'))
    {
        cfgWarn(fname, lineno, "Invalid disk \"%s\" (must be between 'A' and 'Z')", var);
        return;
    }

    const int idx = (int)(letter - 'A');
    free(GLoaderState.disks[idx]);
    GLoaderState.disks[idx] = NULL;

    if (*val != '\0')
    { // val=="" means don't mount
        const size_t vallen = strlen(val);
        LxHostFileInfo statbuf;
        if (lx_host_path_info(val, &statbuf) != LX_HOST_FS_OK)
        {
            cfgWarn(fname, lineno, "Path \"%s\" for drive %c:\\ isn't accessible: %s", val, letter,
                    strerror(errno));
        }
        else if (statbuf.kind != LX_HOST_FILE_DIRECTORY)
        {
            cfgWarn(fname, lineno, "Path \"%s\" for drive %c:\\ isn't a directory", val, letter);
        }
        else if ((GLoaderState.disks[idx] = (char *)malloc(vallen + 2)) == NULL)
        {
            cfgWarn(fname, lineno, "Out of memory!");
        }
        else
        {
            char *ptr = GLoaderState.disks[idx];
            strcpy(ptr, val);
            if (ptr[vallen - 1] != '/')
            {
                ptr[vallen] = '/';
                ptr[vallen + 1] = '\0';
            }
        }
    }
}

static void cfgProcessSystem(const char *fname, const int lineno, const char *var, const char *val)
{
    if (strcmp(var, "trace_native") == 0)
    {
        cfgProcessBoolString(fname, lineno, &GLoaderState.trace_native, val);
    }
    else if (strcmp(var, "trace_events") == 0)
    {
        cfgProcessBoolString(fname, lineno, &GLoaderState.trace_events, val);
    }
    else if (strcmp(var, "beep_volume") == 0)
    {
        cfgProcessFloatString(fname, lineno, &GLoaderState.beep_volume, val);
    }
    else
    {
        cfgWarn(fname, lineno, "Unknown variable system.%s", var);
    }
}

static void cfgProcessLine(const char *fname, const int lineno, const char *category,
                           const char *var, const char *val)
{

    if ((*category == '\0') || (*var == '\0'))
    {
        cfgWarn(fname, lineno, "Invalid configuration line");
    }
    else if (strcmp(category, "mountpoint") == 0)
    {
        cfgProcessMountPoint(fname, lineno, var, val);
    }
    else if (strcmp(category, "system") == 0)
    {
        cfgProcessSystem(fname, lineno, var, val);
    }
    else
    {
        cfgWarn(fname, lineno, "Unknown category \"%s\"", category);
    }
}

static void cfgLoad(const char *fname)
{
    FILE *io = fopen(fname, "r");
    char *buffer = NULL;
    size_t buflen = 0;
    unsigned int lineno = 0;
    ssize_t len;

    if (io == NULL)
    {
        return;
    }

    while ((len = getline(&buffer, &buflen, io)) >= 0)
    {
        lineno++;

        while ((len > 0) && (cfgIsSpace(buffer[len - 1])))
        {
            buffer[--len] = '\0';
        }

#define SKIPSPACE()                                                                                \
    while (cfgIsSpace(*ptr))                                                                       \
    {                                                                                              \
        ptr++;                                                                                     \
    }

        char *ptr = buffer;
        SKIPSPACE();
        if ((*ptr == '\0') || (*ptr == ';') || (*ptr == '#') ||
            ((ptr[0] == '/') && (ptr[1] == '/')))
        {

            continue;
        }
        const char *category = ptr;
        while (cfgIsSymChar(*ptr))
        {
            ptr++;
        }
        if (*ptr != '.')
        {
            cfgWarn(fname, lineno, "Invalid configuration line");
            continue;
        }
        *(ptr++) = '\0';
        const char *var = ptr;
        while (cfgIsSymChar(*ptr))
        {
            ptr++;
        }

        if (*ptr == '=')
        {
            *(ptr++) = '\0';
        }
        else if (!cfgIsSpace(*ptr))
        {
            cfgWarn(fname, lineno, "Invalid configuration line");
            continue;
        }
        else
        {
            *(ptr++) = '\0';
            SKIPSPACE();
            if (*ptr != '=')
            {
                cfgWarn(fname, lineno, "Invalid configuration line");
                continue;
            }
            ptr++;
        }
        SKIPSPACE();
        const char *val = ptr;
#undef SKIPSPACE

        cfgProcessLine(fname, lineno, category, var, val);
    }

    free(buffer);
    fclose(io);
}

static void cfgLoadFiles(void)
{
    cfgLoad("/etc/2ine.cfg");

    char *fname = NULL;
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env)
    {
        const size_t buflen = strlen(env) + 32;
        fname = (char *)malloc(buflen);
        if (fname)
        {
            snprintf(fname, buflen, "%s/2ine/2ine.cfg", env);
        }
    }
    else
    {
        env = getenv("HOME");
        if (env)
        {
            const size_t buflen = strlen(env) + 32;
            fname = (char *)malloc(buflen);
            if (fname)
            {
                snprintf(fname, buflen, "%s/.config/2ine/2ine.cfg", env);
            }
        }
    }

    if (fname)
    {
        cfgLoad(fname);
        free(fname);
    }
}

static void prepOs2Drives(void)
{
    const int drive_c = 'C' - 'A';
    const int total = (sizeof(GLoaderState.disks) / sizeof(GLoaderState.disks[0]));
    uint32 diskmap = 0;
    int lowestmounted = 0;
    int lowestcwd = 0;
    int i;

    FIXME("need to pass current dirs and disks to OS/2 subprocesses");

    char *cwd = lx_host_path_current();
    char *rp = cwd ? lx_host_path_canonical(cwd) : NULL;
    if ((cwd == NULL) || (rp == NULL))
    {
        fprintf(stderr, "Can't get cwd (%s), defaulting to \"/\".\n", strerror(errno));
        rp = strdup("/");
        lx_host_path_chdir(rp);
    }

    free(cwd);
    cwd = rp;

    for (i = 0; i < total; i++)
    {
        if (GLoaderState.disks[i] == NULL)
        {
            continue;
        }

        rp = lx_host_path_canonical(GLoaderState.disks[i]);
        if (rp == NULL)
        {
            fprintf(stderr, "Can't realpath \"%s\" (%s), not mounting %c:\\\n",
                    GLoaderState.disks[i], strerror(errno), i + 'A');
            free(GLoaderState.disks[i]);
            GLoaderState.disks[i] = NULL;
            continue;
        }

        if (lowestmounted < 3)
        {
            lowestmounted = i + 1;
        }

        diskmap |= (1 << i);

        const size_t rplen = strlen(rp);
        assert(rplen > 0);
        if ((strncmp(rp, cwd, rplen) == 0) &&
            ((rp[rplen - 1] == '/') || (cwd[rplen] == '/') || (cwd[rplen] == '\0')))
        {

            const char *d = cwd + rplen;
            GLoaderState.current_dir[i] = strdup((*d == '/') ? d + 1 : d);
            char *ptr = GLoaderState.current_dir[i];
            while (*ptr)
            {
                if (*ptr == '/')
                {
                    *ptr = '\\';
                }
                ptr++;
            }

            if (lowestcwd < 3)
            {
                lowestcwd = i + 1;
            }
        }
        else
        {

            GLoaderState.current_dir[i] = strdup("");
        }
        free(rp);
    }

    if (!lowestmounted)
    {

        GLoaderState.disks[drive_c] = strdup("/");
        char *ptr = strdup(cwd + 1);
        GLoaderState.current_dir[drive_c] = ptr;
        while (*ptr)
        {
            if (*ptr == '/')
            {
                *ptr = '\\';
            }
            ptr++;
        }
        GLoaderState.current_disk = drive_c + 1;
    }
    else if (!lowestcwd)
    {
        /* Use the first mapped drive when cwd is outside all mounts; prefer C: over A:/B:. */
        GLoaderState.current_disk = lowestmounted;
    }
    else
    {
        GLoaderState.current_disk = lowestcwd;
    }

    free(cwd);

    GLoaderState.diskmap = diskmap;

#if 0
    fprintf(stderr, "OS/2 disks:\n");
    for (i = 0; i < total; i++) {
        if (GLoaderState.disks[i] == NULL) continue;
        fprintf(stderr, "  %c: -> mountpoint='%s' cwd='%s' current=%d\n", 'A' + i, GLoaderState.disks[i], GLoaderState.current_dir[i], GLoaderState.current_disk == (i+1));
    }
    fprintf(stderr, "\n");
#endif
}

typedef struct LxAudioGeneratorInfo
{
    LxAudioGeneratorFn fn;
    void *data;
    struct LxAudioGeneratorInfo *next;
} LxAudioGeneratorInfo;

static LxAudioGeneratorInfo *audio_generators = NULL;
static SDL_AudioDeviceID sdl_audio_device = 0;
static SDL_AudioSpec sdl_audio_spec;
static Uint32 no_audio_generators_timeout = 0;

static int SDLCALL closeAudioDeviceThread(void *arg)
{
    const SDL_AudioDeviceID dev = (SDL_AudioDeviceID)(size_t)arg;

    SDL_CloseAudioDevice(dev);
    return 0;
}

static void closeAudioDeviceFromAnotherThread(void)
{
    FIXME("Make this atomic");
    const SDL_AudioDeviceID dev = sdl_audio_device;
    if (!sdl_audio_device)
    {
        return;
    }
    sdl_audio_device = 0;

    SDL_Thread *thread =
        SDL_CreateThread(closeAudioDeviceThread, "2ine_close_audio_device", (void *)(size_t)dev);
    if (thread)
    {
        SDL_DetachThread(thread);
    }
    else
    {
        sdl_audio_device = dev;
    }
}

static void SDLCALL audioCallback(void *userdata, Uint8 *stream, int len)
{
    memset(stream, '\0', len);
    LxAudioGeneratorInfo *generator = audio_generators;
    if (!generator)
    {
        if (!no_audio_generators_timeout)
        {
            no_audio_generators_timeout = SDL_GetTicks() + 5000;
        }
        else if (SDL_TICKS_PASSED(SDL_GetTicks(), no_audio_generators_timeout))
        {
            closeAudioDeviceFromAnotherThread();
        }
        return;
    }

    no_audio_generators_timeout = 0;

    const int samples = len / sizeof(float);
    LxAudioGeneratorInfo *prev = NULL;
    while (generator)
    {
        LxAudioGeneratorInfo *next = generator->next;
        if (generator->fn(generator->data, (float *)stream, samples, sdl_audio_spec.freq))
        {
            prev = generator;
        }
        else
        {

            if (prev)
            {
                prev->next = next;
            }
            else
            {
                assert(generator == audio_generators);
                audio_generators = next;
            }
            free(generator);
        }
        generator = next;
    }
}

static int registerAudioGenerator_lib2ine(LxAudioGeneratorFn fn, void *data, const int singleton)
{
    if (!sdl_audio_device)
    {
        SDL_Init(SDL_INIT_AUDIO);
        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.freq = 48000;
        spec.format = AUDIO_F32SYS;
        spec.channels = 1;
        spec.samples = 1024;
        spec.callback = audioCallback;
        sdl_audio_device =
            SDL_OpenAudioDevice(NULL, 0, &spec, &sdl_audio_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!sdl_audio_device)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return 0;
        }
        no_audio_generators_timeout = 0;
        SDL_PauseAudioDevice(sdl_audio_device, 0);
    }

    LxAudioGeneratorInfo *info = (LxAudioGeneratorInfo *)malloc(sizeof(LxAudioGeneratorInfo *));
    if (!info)
    {
        return 0;
    }

    info->fn = fn;
    info->data = data;

    SDL_LockAudioDevice(sdl_audio_device);

    if (singleton)
    {
        for (LxAudioGeneratorInfo *i = audio_generators; i != NULL; i = i->next)
        {
            if (i->fn == fn)
            {
                SDL_UnlockAudioDevice(sdl_audio_device);
                free(info);
                return 2;
            }
        }
    }

    no_audio_generators_timeout = 0;
    info->next = audio_generators;
    audio_generators = info;
    SDL_UnlockAudioDevice(sdl_audio_device);
    return 1;
}

static void lib2ine_shutdown(void)
{
    if (sdl_audio_device)
    {
        /* Allow two audio callbacks to drain the final queued samples. */
        SDL_Delay(((sdl_audio_spec.samples * 1000) / sdl_audio_spec.freq) * 2);
        SDL_CloseAudioDevice(sdl_audio_device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdl_audio_device = 0;
    }

    LxAudioGeneratorInfo *generator = audio_generators;
    audio_generators = NULL;
    while (generator)
    {
        LxAudioGeneratorInfo *next = generator->next;
        free(generator);
        generator = next;
    }

    for (int i = 0; i < (sizeof(GLoaderState.disks) / sizeof(GLoaderState.disks[0])); i++)
    {
        free(GLoaderState.disks[i]);
        GLoaderState.disks[i] = NULL;
    }

    if (!GLoaderState.execution)
        free(GLoaderState.pib.pib_pchenv);
    GLoaderState.pib.pib_pchenv = NULL;

    DefaultTib = NULL;
}

LX_NATIVE_CONSTRUCTOR(lib2ine)
{
    memset(&GLoaderState, '\0', sizeof(GLoaderState));
    GLoaderState.dosExit = dosExit_lib2ine;
    GLoaderState.initOs2Tib = initOs2Tib_lib2ine;
    GLoaderState.setOs2Tib = setOs2Tib_lib2ine;
    GLoaderState.getOs2Tib = getOs2Tib_lib2ine;
    GLoaderState.deinitOs2Tib = deinitOs2Tib_lib2ine;
    GLoaderState.allocSegment = allocSegment_lib2ine;
    GLoaderState.freeSegment = freeSegment_lib2ine;
    GLoaderState.findSelector = findSelector_lib2ine;
    GLoaderState.freeSelector = freeSelector_lib2ine;
    GLoaderState.convert1616to32 = convert1616to32_lib2ine;
    GLoaderState.convert32to1616 = convert32to1616_lib2ine;
    GLoaderState.loadModule = loadModule_lib2ine;
    GLoaderState.makeUnixPath = makeUnixPath_lib2ine;
    GLoaderState.terminate = terminate_lib2ine;
    GLoaderState.registerAudioGenerator = registerAudioGenerator_lib2ine;
    GLoaderState.lib2ine_shutdown = lib2ine_shutdown;

    /* Use a low amplitude to match the PC speaker mix. */
    GLoaderState.beep_volume = 0.05f;

    cfgLoadFiles();
    prepOs2Drives();

    const size_t stack_limit = lx_host_stack_limit();
    GLoaderState.initOs2Tib(GLoaderState.main_tibspace, (void *)&stack_limit, stack_limit, 0);
    GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);

    if (getenv("TRACE_NATIVE"))
    {
        GLoaderState.trace_native = 1;
    }

    if (getenv("TRACE_EVENTS"))
    {
        GLoaderState.trace_events = 1;
    }

    GLoaderState.subprocess = (getenv("IS_2INE") != NULL);

    initPib();
}

LX_NATIVE_DESTRUCTOR(lib2ine)
{
    lib2ine_shutdown();
}
