/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
#include "os2libs/doscalls/launch.h"
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

APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GProcessState.pib.pib_pchenv;
    const size_t len = strlen(name);
    while (*env)
    {
        if ((strncmp(env, name, len) == 0) && (env[len] == '='))
        {
            *outval = env + len + 1;
            return NO_ERROR;
        }
        env += strlen(env) + 1;
    }

    return ERROR_ENVVAR_NOT_FOUND;
}

APIRET16 Dos16ScanEnv(PSZ name, PULONG value)
{
    if (!name || !value)
        return ERROR_INVALID_PARAMETER;
    PSZ text;
    APIRET rc = DosScanEnv(name, &text);
    if (rc)
        return (APIRET16)rc;
    ULONG pointer = GProcessState.convert32to1616(text);
    if (!pointer)
        return ERROR_INVALID_ADDRESS;
    lx_guest_store_u32(value, pointer);
    return NO_ERROR;
}

void runDosExitList(const uint32 why)
{
    while (GExitList)
    {
        ExitListItem *item = GExitList;
        PFNEXITLIST host_entry = item->host_entry;
        uint32 guest_entry = item->guest_entry;
        LxModule *module = item->module;
        GExitList = item->next;
        free(item);

        if (GProcessState.execution)
            lx_execution_callback32(GProcessState.execution, guest_entry, why);
        else
            host_entry(why);
        if (module)
            GProcessState.freeModule(module);
    }
}

static uint32 processExitCode;
static int processingExitList;

static void finishProcessExit(uint32 exitcode)
{
    processExitCode = exitcode;
    processingExitList = 1;
    /* LINFOSEG rfProcStatus bit 0 marks exit-list processing for 16-bit services. */
    if (linfo)
        linfo->rfProcStatus |= 1;
    releaseThreadSuspension();
    lx_launch_record_exit(exitcode);
    runDosExitList(TC_EXIT);
    GProcessState.terminate(exitcode);
}

static void (*threadExitObserver)(ULONG code);

void lx_doscalls_observe_thread_exit(void (*observer)(ULONG code))
{
    __atomic_store_n(&threadExitObserver, observer, __ATOMIC_RELEASE);
}

VOID DosExit_implementation(ULONG action, ULONG exitcode)
{

    if (action == EXIT_THREAD)
    {
        void (*observer)(ULONG) = __atomic_load_n(&threadExitObserver, __ATOMIC_ACQUIRE);
        if (observer)
            observer(exitcode);
        FIXME("if last thread, treat as DosExit(1)");
        lx_execution_end_thread(GProcessState.execution);
        lx_host_thread_exit(NULL);
    }

    lx_execution_terminate(GProcessState.execution, finishProcessExit, exitcode);
}

VOID DosExit(ULONG action, ULONG exitcode)
{
    TRACE_NATIVE("DosExit(%u, %u)", (uint)action, (uint)exitcode);
    DosExit_implementation(action, exitcode);
}

APIRET DosExitList(ULONG ordercode, PFNEXITLIST fn)
{
    TRACE_NATIVE("DosExitList(%u, %p)", (uint)ordercode, fn);

    const uint8 cmd = ordercode & 0xFF;
    if (fn == NULL && cmd != EXLST_EXIT)
        return ERROR_INVALID_FUNCTION;
    const uint8 arg = (ordercode >> 8) & 0xFF;
    ExitListItem *prev = NULL;
    ExitListItem *item = NULL;

    if ((cmd != EXLST_ADD) && (arg != 0))
        return ERROR_INVALID_DATA;

    switch (cmd)
    {
        case EXLST_ADD:
        {
            ExitListItem *newitem = (ExitListItem *)malloc(sizeof(ExitListItem));
            if (!newitem)
                return ERROR_NOT_ENOUGH_MEMORY;
            newitem->host_entry = GProcessState.execution ? NULL : fn;
            newitem->guest_entry = GProcessState.execution ? (uint32)(uintptr_t)fn : 0;
            newitem->priority = arg;
            newitem->module = NULL;
            if (GProcessState.execution)
                for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
                {
                    if (module == GProcessState.main_module || module->nativelib)
                        continue;
                    for (uint32 i = 0; i < module->num_mmaps; i++)
                    {
                        const LxMmaps *object = &module->mmaps[i];
                        if (newitem->guest_entry >= object->guest_base &&
                            (uint64)newitem->guest_entry - object->guest_base < object->size)
                        {
                            __atomic_add_fetch(&module->refcount, 1, __ATOMIC_ACQ_REL);
                            newitem->module = module;
                            break;
                        }
                    }
                    if (newitem->module)
                        break;
                }
            for (item = GExitList; item; item = item->next)
            {
                if (item->priority >= ((uint32)arg))
                    break;
                prev = item;
            }
            if (prev)
            {
                prev->next = newitem;
                newitem->next = item;
            }
            else
            {
                newitem->next = GExitList;
                GExitList = newitem;
            }
            return NO_ERROR;
        }

        case EXLST_REMOVE:
        {
            for (item = GExitList; item; item = item->next)
            {
                if (GProcessState.execution ? item->guest_entry == (uint32)(uintptr_t)fn
                                            : item->host_entry == fn)
                {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    LxModule *module = item->module;
                    free(item);
                    if (module)
                        GProcessState.freeModule(module);
                    return NO_ERROR;
                }
                prev = item;
            }
            return ERROR_INVALID_FUNCTION;
        }

        case EXLST_EXIT:
            /* This is a transfer, not a return to the terminating handler. The
               current entry was removed before invocation, so continue the list. */
            if (!processingExitList)
                return ERROR_INVALID_FUNCTION;
            finishProcessExit(processExitCode);
            return NO_ERROR;

        default:
            return ERROR_INVALID_DATA;
    }

    return NO_ERROR;
}

void setProcessResultCode(PRESULTCODES output, const LxHostChildStatus *status)
{
    if (!output)
        return;
    const ULONG reasons[] = {TC_EXIT, TC_KILLPROCESS, TC_HARDERROR, TC_EXCEPTION};
    output->codeTerminate = reasons[status->reason];
    output->codeResult = status->result;
}

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

static PID legacyChildPid(USHORT pid)
{
    PID result = pid;
    grabLock(&GMutexDosCalls);
    for (ChildProcess *child = GChildren; child; child = child->next)
        if ((USHORT)lx_host_child_id(child->host) == pid)
        {
            result = lx_host_child_id(child->host);
            break;
        }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET16 Dos16KillProcess(USHORT scope, USHORT pid)
{
    return DosKillProcess(scope, legacyChildPid(pid));
}

APIRET DosKillProcess(ULONG action, PID pid)
{
    TRACE_NATIVE("DosKillProcess(%u, %u)", (unsigned)action, (unsigned)pid);
    if (action > 1)
        return ERROR_INVALID_PARAMETER;
    /* Process-tree termination and unrelated host processes are outside this worker's scope. */
    if (action == 1)
        return ERROR_ACCESS_DENIED;
    APIRET result = ERROR_INVALID_PROCID;
    grabLock(&GMutexDosCalls);
    for (ChildProcess *child = GChildren; child; child = child->next)
        if (lx_host_child_id(child->host) == pid)
        {
            int stopped = lx_host_child_terminate(child->host);
            result = !stopped ? NO_ERROR : stopped > 0 ? ERROR_INVALID_PROCID : ERROR_ACCESS_DENIED;
            break;
        }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosWaitChild(ULONG action, ULONG option, PRESULTCODES result, PPID output_pid, PID pid)
{
    TRACE_NATIVE("DosWaitChild(%u, %u, %p, %p, %u)", (uint)action, (uint)option, result, output_pid,
                 (uint)pid);
    if (action != DCWA_PROCESS || (option != DCWW_WAIT && option != DCWW_NOWAIT))
        return ERROR_INVALID_PARAMETER;
    for (;;)
    {
        grabLock(&GMutexDosCalls);
        ChildProcess *child = NULL;
        LxHostChildStatus status;
        int ready = 0;
        for (ChildProcess *item = GChildren; item; item = item->next)
        {
            if (item->waiting || (pid && lx_host_child_id(item->host) != pid))
                continue;
            if (pid)
            {
                child = item;
                break;
            }
            ready = lx_host_child_wait(item->host, 1, &status);
            if (ready)
            {
                child = item;
                break;
            }
        }
        if (!child)
        {
            int any = GChildren != NULL;
            ungrabLock(&GMutexDosCalls);
            if (pid || !any)
                return pid ? ERROR_INVALID_PROCID : ERROR_WAIT_NO_CHILDREN;
            if (option == DCWW_NOWAIT)
                return ERROR_CHILD_NOT_COMPLETE;
            lx_host_sleep_ms(1);
            continue;
        }
        child->waiting = 1;
        ungrabLock(&GMutexDosCalls);
        if (!ready)
            ready = lx_host_child_wait(child->host, option == DCWW_NOWAIT, &status);
        grabLock(&GMutexDosCalls);
        if (ready == 1)
        {
            for (ChildProcess **link = &GChildren; *link; link = &(*link)->next)
                if (*link == child)
                {
                    *link = child->next;
                    break;
                }
        }
        else
            child->waiting = 0;
        ungrabLock(&GMutexDosCalls);
        if (ready != 1)
            return ready ? ERROR_INVALID_PROCID : ERROR_CHILD_NOT_COMPLETE;
        if (output_pid)
            *output_pid = lx_host_child_id(child->host);
        setProcessResultCode(result, &status);
        lx_host_child_release(child->host);
        free(child);
        return NO_ERROR;
    }
}

APIRET DosR3ExitAddr(void)
{
    TRACE_NATIVE("DosR3ExitAddr()");
    FIXME("I have no idea what this is");
    return ERROR_INVALID_PARAMETER;
}

APIRET DosSetProcessCp(ULONG cp)
{
    TRACE_NATIVE("DosSetProcessCp(%u)", (unsigned int)cp);
    if (cp != 437 && cp != 850)
    { // United States
        FIXME("non-US codepages unsupported at the moment");
        return ERROR_INVALID_CODE_PAGE;
    }
    GProcessState.process_codepage = (USHORT)cp;
    return NO_ERROR;
}

APIRET16 Dos16SetProcCp(USHORT codepage, USHORT reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : (APIRET16)DosSetProcessCp(codepage);
}

APIRET16 Dos16GetPID(PPIDINFO16 ppidinfo)
{
    TRACE_NATIVE("Dos16GetPID(%p)", ppidinfo);
    PTIB2 tib2 = (PTIB2)LX_GETTIB2();

    // This interface has 16-bit fields even when host IDs are wider.
    ppidinfo->pid = (USHORT)GProcessState.pib.pib_ulpid;
    ppidinfo->tid = (USHORT)tib2->tib2_ultid;
    ppidinfo->pidParent = (USHORT)GProcessState.pib.pib_ulppid;
    return NO_ERROR;
}

VOID Dos16Exit(USHORT action, USHORT exitcode)
{
    TRACE_NATIVE("Dos16Exit(%u, %u)", (uint)action, (uint)exitcode);
    DosExit_implementation(action, exitcode);
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
    const char *guest = os2env ? os2env : GProcessState.pib.pib_pchenv;
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

APIRET16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset)
{
    TRACE_NATIVE("Dos16GetEnv(%p, %p)", psel, pcmdoffset);
    const uint32 cmd1616 = GProcessState.convert32to1616(GProcessState.pib.pib_pchcmd);
    if (pcmdoffset)
        *pcmdoffset = (USHORT)(cmd1616 & 0xFFFF);
    if (psel)
        *psel = (USHORT)((cmd1616 >> 16) & 0xFFFF);
    return NO_ERROR;
}

APIRET16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres, PUSHORT ppid, USHORT pid)
{
    TRACE_NATIVE("Dos16CWait(%u, %u, %p, %p, %u)", (uint)action, (uint)option, pres, ppid,
                 (uint)pid);
    RESULTCODES res;
    PID cpid;
    APIRET ret = DosWaitChild(action, option, &res, &cpid, pid ? legacyChildPid(pid) : 0);
    if (ret)
        return ret;
    pres->codeTerminate = res.codeTerminate;
    pres->codeResult = res.codeResult;
    *ppid = cpid;
    return ret;
}
