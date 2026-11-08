/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"
#include "os2/include/bsedos16.h"
#include "os2/include/private/bsep.h"
#include "emulator/launch_record.h"
#include <ctype.h>

/* Per-launch state reaches the child only through its environment. */
typedef struct LaunchStart
{
    int drive;          /* Zero-based; negative inherits the caller's directories. */
    char *directory;    /* Below the drive root, without a leading backslash. */
    const char *record; /* Shared launch record name, or NULL. */
} LaunchStart;

typedef struct LaunchPlan
{
    char *host_program, *loader;
    char **argv, **envp;
} LaunchPlan;

static char **calcEnvp(const char *os2env, const char *exe, const char *arguments,
                       const LaunchStart *start, APIRET *err);

static void freeLaunchPlan(LaunchPlan *plan)
{
    if (plan->envp)
        for (char **item = plan->envp; *item; item++)
            free(*item);
    free(plan->envp);
    free(plan->argv);
    free(plan->loader);
    free(plan->host_program);
    memset(plan, 0, sizeof *plan);
}

static char *driveDirectoryPath(int drive, const char *directory, const char *name)
{
    size_t size = strlen(directory) + (name ? strlen(name) : 0) + 6;
    char *path = malloc(size);
    if (path)
        snprintf(path, size, "%c:\\%s%s%s", 'A' + drive, directory, name && *directory ? "\\" : "",
                 name ? name : "");
    return path;
}

/* Absolute, drive-relative and current-directory-relative forms all resolve
   against the caller's per-drive state without changing it. */
static APIRET resolveStartDirectory(const char *directory, LaunchStart *start)
{
    const char *path = directory;
    int drive = GProcessState.current_disk - 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
    {
        drive = toupper((unsigned char)path[0]) - 'A';
        path += 2;
    }
    if (drive < 0 || drive >= 26 || !GProcessState.disks[drive])
        return ERROR_INVALID_DRIVE;
    const char *base = *path == '\\' || *path == '/' ? "" : GProcessState.current_dir[drive];
    if (!base)
        base = "";
    while (*path == '\\' || *path == '/')
        path++;
    size_t size = strlen(base) + strlen(path) + 2;
    char *joined = malloc(size);
    if (!joined)
        return ERROR_NOT_ENOUGH_MEMORY;
    snprintf(joined, size, "%s%s%s", base, *base && *path ? "\\" : "", path);
    for (char *p = joined; *p; p++)
        if (*p == '/')
            *p = '\\';
    for (size_t length = strlen(joined); length && joined[length - 1] == '\\'; length--)
        joined[length - 1] = 0;
    char *full = driveDirectoryPath(drive, joined, NULL);
    APIRET result = full ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    char *host = full ? makeUnixPath(full, &result) : NULL;
    LxHostFileInfo information;
    if (host && (lx_host_path_info(host, &information) != LX_HOST_FS_OK ||
                 information.kind != LX_HOST_FILE_DIRECTORY))
        result = ERROR_PATH_NOT_FOUND;
    free(host);
    free(full);
    if (result)
    {
        free(joined);
        return result;
    }
    start->drive = drive;
    start->directory = joined;
    return NO_ERROR;
}

/* The loader runs OS/2 programs only: no VDM or Win-OS/2 exists to take the others. */
static APIRET launchableImage(const char *host, PSZ program)
{
    if (!lx_host_path_exists(host))
        return ERROR_FILE_NOT_FOUND;
    ULONG type = 0;
    APIRET result = DosQueryAppType(program, &type);
    if (result)
        return result;
    if (type & (FAPPTYP_DOS | FAPPTYP_WINDOWSREAL | FAPPTYP_WINDOWSPROT | FAPPTYP_WINDOWSPROT31))
        return ERROR_INVALID_EXE_SIGNATURE;
    if (type & (FAPPTYP_DLL | FAPPTYP_PHYSDRV | FAPPTYP_VIRTDRV | FAPPTYP_PROTDLL))
        return ERROR_BAD_EXE_FORMAT;
    return NO_ERROR;
}

static APIRET prepareLaunch(LaunchPlan *plan, PSZ pArg, PSZ pEnv, PSZ pName,
                            const LaunchStart *start, ULONG flags)
{
    APIRET err = NO_ERROR;
    if (!pName || !*pName)
        return ERROR_INVALID_PARAMETER;
    char searched[PATH_MAX];
    const char *program = pName;
    char *candidate = NULL;
    if ((flags & LX_DOS_LAUNCH_SEARCH_DIRECTORY) && start && start->drive >= 0 && *pName != '\\' &&
        *pName != '/' && pName[1] != ':')
    {
        candidate = driveDirectoryPath(start->drive, start->directory, pName);
        if (!candidate)
            return ERROR_NOT_ENOUGH_MEMORY;
        char *host = makeUnixPath(candidate, &err);
        if (host && lx_host_path_exists(host))
            program = candidate;
        free(host);
        err = NO_ERROR;
    }
    if (program == pName && !strpbrk(pName, "\\/:"))
    {
        err = DosSearchPath(SEARCH_CUR_DIRECTORY | SEARCH_ENVIRONMENT | SEARCH_IGNORENETERRS,
                            "PATH", pName, (PBYTE)searched, sizeof searched);
        if (err)
        {
            free(candidate);
            return err;
        }
        program = searched;
    }
    plan->host_program = makeUnixPath(program, &err);
    if (!plan->host_program)
    {
        free(candidate);
        return err;
    }
    /* A launch reports a bad image now rather than as a failed child later. */
    err = start ? launchableImage(plan->host_program, (PSZ)program) : NO_ERROR;
    free(candidate);
    if (err)
    {
        freeLaunchPlan(plan);
        return err;
    }
    if (!(plan->loader = findExePath(pName, &err)) || !(plan->argv = calcArgv(pArg, pName, &err)))
    {
        freeLaunchPlan(plan);
        return err;
    }
    const char *arguments = pArg && *pArg ? pArg + strlen(pArg) + 1 : "";
    plan->envp = calcEnvp(pEnv, plan->host_program, arguments, start, &err);
    if (!plan->envp)
    {
        freeLaunchPlan(plan);
        return err;
    }
    return NO_ERROR;
}

APIRET DosExecPgm_implementation(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv,
                                 PRESULTCODES pRes, PSZ pName)
{
    APIRET retval = NO_ERROR;

    if (cbObjname < 0)
        return ERROR_INVALID_PARAMETER;
    if (pObjname && cbObjname > 0)
        *pObjname = '\0';

    if ((execFlag == EXEC_TRACE) || (execFlag == EXEC_ASYNCRESULTDB))
    {
        FIXME("tried to launch a process for debugging!");
        return ERROR_ACCESS_DENIED;
    }

    if (execFlag == EXEC_LOAD)
    {
        FIXME("What is this, exactly?");
        return ERROR_GEN_FAILURE;
    }

    LaunchPlan plan = {0};
    retval = prepareLaunch(&plan, pArg, pEnv, pName, NULL, 0);
    if (retval)
        return retval;

    ChildProcess *tracked = execFlag == EXEC_ASYNCRESULT ? calloc(1, sizeof *tracked) : NULL;
    if (execFlag != EXEC_SYNC && execFlag != EXEC_ASYNC && execFlag != EXEC_ASYNCRESULT &&
        execFlag != EXEC_BACKGROUND)
        retval = ERROR_INVALID_FUNCTION;
    else if (execFlag == EXEC_ASYNCRESULT && !tracked)
        retval = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        LxHostChild *child = lx_host_child_spawn(plan.loader, plan.argv, plan.envp);
        if (!child)
            retval = ERROR_NO_PROC_SLOTS;
        else if (execFlag == EXEC_SYNC)
        {
            LxHostChildStatus status = {0};
            int waited = lx_host_child_wait(child, 0, &status);
            if (waited != 1)
                retval = ERROR_GEN_FAILURE;
            else
                setProcessResultCode(pRes, &status);
            TRACE_RET("DosExecPgm('%s', EXEC_SYNC) -> %u, wait=%d reason=%u result=%u", pName,
                      (uint)retval, waited, (uint)status.reason, (uint)status.result);
            lx_host_child_release(child);
        }
        else
        {
            if (pRes)
            {
                pRes->codeTerminate = lx_host_child_id(child);
                pRes->codeResult = 0;
            }
            if (tracked)
            {
                tracked->host = child;
                grabLock(&GMutexDosCalls);
                tracked->next = GChildren;
                GChildren = tracked;
                ungrabLock(&GMutexDosCalls);
                tracked = NULL;
            }
            else
                lx_host_child_release(child);
        }
    }
    free(tracked);
    freeLaunchPlan(&plan);

    return retval;
}

static APIRET launchChild(const LxDosLaunch *launch, LxHostChild **child)
{
    if (!launch || !launch->program || !*launch->program || !child)
        return ERROR_INVALID_PARAMETER;
    LaunchStart start = {.drive = -1, .record = launch->record};
    APIRET result = NO_ERROR;
    if (launch->directory && *launch->directory)
        result = resolveStartDirectory(launch->directory, &start);
    LaunchPlan plan = {0};
    if (!result)
        result = prepareLaunch(&plan, launch->arguments, launch->environment, launch->program,
                               &start, launch->flags);
    if (!result)
    {
        *child = launch->flags & LX_DOS_LAUNCH_SUSPENDED
                     ? lx_host_child_spawn_suspended(plan.loader, plan.argv, plan.envp)
                     : lx_host_child_spawn(plan.loader, plan.argv, plan.envp);
        if (!*child)
            result = ERROR_NO_PROC_SLOTS;
    }
    freeLaunchPlan(&plan);
    free(start.directory);
    return result;
}

APIRET lx_doscalls_launch(const LxDosLaunch *launch, LxHostChild **child)
{
    return launchChild(launch, child);
}

APIRET lx_doscalls_launch_tracked(const LxDosLaunch *launch, PID *pid)
{
    ChildProcess *tracked = calloc(1, sizeof *tracked);
    if (!tracked)
        return ERROR_NOT_ENOUGH_MEMORY;
    APIRET result = launchChild(launch, &tracked->host);
    if (result)
    {
        free(tracked);
        return result;
    }
    *pid = lx_host_child_id(tracked->host);
    grabLock(&GMutexDosCalls);
    tracked->next = GChildren;
    GChildren = tracked;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv,
                  PRESULTCODES pRes, PSZ pName)
{
    TRACE_NATIVE("DosExecPgm('%s', %u, %u, '%s', '%s', %p, '%s')", pObjname, (uint)cbObjname,
                 (uint)execFlag, pArg, pEnv, pRes, pName);
    if (GProcessState.trace_native && pArg)
    {
        fprintf(stderr, "2INE TRACE: DosExecPgm arg block:");
        for (const char *p = pArg; p < (pArg + 1024); p++)
        {
            if (*p)
                fprintf(stderr, " %c", (*p >= 32 && *p < 127) ? *p : '.');
            else
            {
                fprintf(stderr, " |");
                if (p[1] == '\0')
                    break;
            }
        }
        fprintf(stderr, "\n");
    }
    return DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, pRes, pName);
}

APIRET16 Dos16ExecPgm(PCHAR pObjname, SHORT cbObjname, USHORT execFlag, PSZ pArg, PSZ pEnv,
                      PRESULTCODES16 pRes, PSZ pName)
{
    TRACE_NATIVE("Dos16ExecPgm('%s', %d, %u, '%s', '%s', %p, '%s')", pObjname, (int)cbObjname,
                 (uint)execFlag, pArg, pEnv, pRes, pName);
    RESULTCODES res;
    const APIRET ret =
        DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, &res, pName);
    pRes->codeTerminate = (USHORT)res.codeTerminate;
    pRes->codeResult = (USHORT)res.codeResult;
    return ret;
}

static int hostControlVariable(const char *entry)
{
    return !strncmp(entry, "TWOINE_", 7) || !strncmp(entry, "SDL_", 4) ||
           !strncmp(entry, "LD_LIBRARY_PATH=", 16) || !strncmp(entry, "DYLD_LIBRARY_PATH=", 18) ||
           !strncmp(entry, "TRACE_NATIVE=", 13) || !strncmp(entry, "TRACE_EVENTS=", 13) ||
           !strncmp(entry, "DISPLAY=", 8) || !strncmp(entry, "WAYLAND_DISPLAY=", 16) ||
           !strncmp(entry, "XDG_RUNTIME_DIR=", 16) || !strncmp(entry, "LIBGL_", 6) ||
           !strncmp(entry, "GALLIUM_", 8);
}

static int inheritedHostVariable(const char *entry)
{
    return hostControlVariable(entry) && strncmp(entry, "TWOINE_CHILD_ARGUMENTS=", 23) &&
           strncmp(entry, "TWOINE_DRIVE_CWD_", 17) && strncmp(entry, "TWOINE_EXT_LIBPATH_", 19) &&
           strncmp(entry, "TWOINE_CURRENT_DRIVE=", 21) &&
           strncmp(entry, LX_LAUNCH_RECORD_VARIABLE "=", sizeof LX_LAUNCH_RECORD_VARIABLE);
}

static char **calcEnvp(const char *os2env, const char *exe, const char *arguments,
                       const LaunchStart *start, APIRET *err)
{
    const char *guest = os2env ? os2env : GProcessState.pib.environment;
    if (!guest)
    {
        *err = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    size_t count = 34;
    for (const char *p = guest; *p; p += strlen(p) + 1)
        count++;
    char *const *host = lx_host_environment();
    for (size_t i = 0; host[i]; i++)
        if (inheritedHostVariable(host[i]))
            count++;
    char **result = calloc(count, sizeof *result);
    if (!result)
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    size_t at = 0;
    for (size_t i = 0; host[i]; i++)
        if (inheritedHostVariable(host[i]))
        {
            result[at] = strdup(host[i]);
            if (!result[at++])
                goto failed;
        }
    for (const char *p = guest; *p; p += strlen(p) + 1)
        if (!hostControlVariable(p) && strncmp(p, "IS_2INE=", 8))
        {
            result[at] = strdup(p);
            if (!result[at++])
                goto failed;
        }
    const char *extended[] = {GProcessState.begin_libpath, GProcessState.end_libpath,
                              GProcessState.libpath_strict ? "T" : "F"};
    const char *extended_names[] = {"BEGIN", "END", "STRICT"};
    for (unsigned i = 0; i < 3; i++)
    {
        size_t length = strlen(extended[i]) + 40;
        result[at] = malloc(length);
        if (!result[at])
            goto failed;
        snprintf(result[at++], length, "TWOINE_EXT_LIBPATH_%s=%s", extended_names[i], extended[i]);
    }
    const int start_drive = start ? start->drive : -1;
    for (int drive = 0; drive < 26; drive++)
    {
        const char *directory =
            drive == start_drive ? start->directory : GProcessState.current_dir[drive];
        if (directory)
        {
            size_t length = strlen(directory) + 32;
            result[at] = malloc(length);
            if (!result[at])
                goto failed;
            snprintf(result[at++], length, "TWOINE_DRIVE_CWD_%c=%s", 'A' + drive, directory);
        }
    }
    result[at] = malloc(32);
    if (!result[at])
        goto failed;
    snprintf(result[at++], 32, "TWOINE_CURRENT_DRIVE=%c",
             'A' + (start_drive >= 0 ? start_drive : GProcessState.current_disk - 1));
    if (start && start->record)
    {
        size_t length = strlen(start->record) + sizeof LX_LAUNCH_RECORD_VARIABLE + 1;
        result[at] = malloc(length);
        if (!result[at])
            goto failed;
        snprintf(result[at++], length, LX_LAUNCH_RECORD_VARIABLE "=%s", start->record);
    }
    result[at] = malloc(strlen(exe) + 9);
    if (!result[at])
        goto failed;
    sprintf(result[at++], "IS_2INE=%s", exe);
    result[at] = malloc(strlen(arguments) + 24);
    if (!result[at])
        goto failed;
    sprintf(result[at], "TWOINE_CHILD_ARGUMENTS=%s", arguments);
    return result;
failed:
    for (size_t i = 0; i < at; i++)
        free(result[i]);
    free(result);
    *err = ERROR_NOT_ENOUGH_MEMORY;
    return NULL;
}

char **calcArgv(const char *os2args, const char *progpath, APIRET *err)
{
    if ((!os2args) || (!*os2args))
        os2args = "\0";

    /* OS/2 arguments: program NUL arguments NUL. The child worker also needs its own argv[0]. */
    const char *args = os2args + strlen(os2args) + 1;

    size_t maxargs = 3;
    for (const char *ptr = args; *ptr; ptr++)
    {
        if ((*ptr == ' ') || (*ptr == '\t'))
            maxargs++;
    }

    const size_t bufsize = 16 + strlen(progpath) + 1 + strlen(args) + 1;
    char **retval = (char **)malloc((maxargs + 1) * sizeof(char *) + bufsize);
    if (!retval)
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }

    char *dst = (char *)(retval + maxargs + 1);
    size_t idx = 0;

    strcpy(dst, "lx_loader");
    retval[idx++] = dst;
    dst += strlen(dst) + 1;

    strcpy(dst, progpath);
    retval[idx++] = dst;
    dst += strlen(dst) + 1;

    char *start = dst;
    int in_quotes = 0;
    int have_token = 0;

    for (const char *ptr = args;; ptr++)
    {
        const char ch = *ptr;
        if (ch == '"')
        {
            in_quotes = !in_quotes;
            have_token = 1;
            continue;
        }
        else if ((ch == '\0') || ((!in_quotes) && ((ch == ' ') || (ch == '\t'))))
        {
            if (have_token)
            {
                *(dst++) = '\0';
                retval[idx++] = start;
                start = dst;
                have_token = 0;
            }
            if (ch == '\0')
                break;
            continue;
        }

        if (!have_token)
        {
            start = dst;
            have_token = 1;
        }
        *(dst++) = ch;
    }

    retval[idx] = NULL;
    return retval;
}
