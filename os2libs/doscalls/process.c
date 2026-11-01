/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GLoaderState.pib.pib_pchenv;
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
    ULONG pointer = GLoaderState.convert32to1616(text);
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

        if (GLoaderState.execution)
            lx_execution_callback32(GLoaderState.execution, guest_entry, why);
        else
            host_entry(why);
        if (module)
            GLoaderState.freeModule(module);
    }
}

static uint32 processExitCode;
static int processingExitList;
static void finishProcessExit(uint32 exitcode)
{
    processExitCode = exitcode;
    processingExitList = 1;
    runDosExitList(TC_EXIT);
    GLoaderState.terminate(exitcode);
}

VOID DosExit_implementation(ULONG action, ULONG exitcode)
{

    if (action == EXIT_THREAD)
    {

        FIXME("if last thread, treat as DosExit(1)");
        lx_execution_end_thread(GLoaderState.execution);
        lx_host_thread_exit(NULL);
    }

    lx_execution_terminate(GLoaderState.execution, finishProcessExit, exitcode);
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
            newitem->host_entry = GLoaderState.execution ? NULL : fn;
            newitem->guest_entry = GLoaderState.execution ? (uint32)(uintptr_t)fn : 0;
            newitem->priority = arg;
            newitem->module = NULL;
            if (GLoaderState.execution)
                for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
                {
                    if (module == GLoaderState.main_module || module->nativelib)
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
                if (GLoaderState.execution ? item->guest_entry == (uint32)(uintptr_t)fn
                                           : item->host_entry == fn)
                {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    LxModule *module = item->module;
                    free(item);
                    if (module)
                        GLoaderState.freeModule(module);
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

    APIRET err;
    if (!pName || !*pName)
        return ERROR_INVALID_PARAMETER;
    char searched[PATH_MAX];
    const char *program = pName;
    if (!strpbrk(pName, "\\/:"))
    {
        err = DosSearchPath(SEARCH_CUR_DIRECTORY | SEARCH_ENVIRONMENT | SEARCH_IGNORENETERRS,
                            "PATH", pName, (PBYTE)searched, sizeof searched);
        if (err)
            return err;
        program = searched;
    }
    char *pNameUnix = makeUnixPath(program, &err);
    if (!pNameUnix)
        return err;

    char *exe = findExePath(pName, &retval);
    if (!exe)
    {
        free(pNameUnix);
        return retval;
    }

    char **argv = calcArgv(pArg, pName, &retval);
    if (!argv)
    {
        free(exe);
        free(pNameUnix);
        return retval;
    }

    const char *arguments = pArg && *pArg ? pArg + strlen(pArg) + 1 : "";
    char **envp = calcEnvp(pEnv, pNameUnix, arguments, &retval);
    if (!envp)
    {
        free(argv);
        free(exe);
        free(pNameUnix);
        return retval;
    }

    ChildProcess *tracked = execFlag == EXEC_ASYNCRESULT ? calloc(1, sizeof *tracked) : NULL;
    if (execFlag != EXEC_SYNC && execFlag != EXEC_ASYNC && execFlag != EXEC_ASYNCRESULT &&
        execFlag != EXEC_BACKGROUND)
        retval = ERROR_INVALID_FUNCTION;
    else if (execFlag == EXEC_ASYNCRESULT && !tracked)
        retval = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        LxHostChild *child = lx_host_child_spawn(exe, argv, envp);
        if (!child)
            retval = ERROR_NO_PROC_SLOTS;
        else if (execFlag == EXEC_SYNC)
        {
            LxHostChildStatus status;
            if (lx_host_child_wait(child, 0, &status) != 1)
                retval = ERROR_GEN_FAILURE;
            else
                setProcessResultCode(pRes, &status);
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
    for (char **item = envp; *item; item++)
        free(*item);
    free(pNameUnix);
    free(envp);
    free(argv);
    free(exe);

    return retval;
}

APIRET DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv,
                  PRESULTCODES pRes, PSZ pName)
{
    TRACE_NATIVE("DosExecPgm('%s', %u, %u, '%s', '%s', %p, '%s')", pObjname, (uint)cbObjname,
                 (uint)execFlag, pArg, pEnv, pRes, pName);
    if (GLoaderState.trace_native && pArg)
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
    if (cp != 437)
    { // United States
        FIXME("non-US codepages unsupported at the moment");
        return ERROR_INVALID_CODE_PAGE;
    }
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
    ppidinfo->pid = (USHORT)GLoaderState.pib.pib_ulpid;
    ppidinfo->tid = (USHORT)tib2->tib2_ultid;
    ppidinfo->pidParent = (USHORT)GLoaderState.pib.pib_ulppid;
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
char **calcEnvp(const char *os2env, const char *exe, const char *arguments, APIRET *err)
{
    const char *guest = os2env ? os2env : GLoaderState.pib.pib_pchenv;
    if (!guest)
    {
        *err = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    size_t count = 3;
    for (const char *p = guest; *p; p += strlen(p) + 1)
        count++;
    char *const *host = lx_host_environment();
    for (size_t i = 0; host[i]; i++)
        if (hostControlVariable(host[i]) && strncmp(host[i], "TWOINE_CHILD_ARGUMENTS=", 23))
            count++;
    char **result = calloc(count, sizeof *result);
    if (!result)
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    size_t at = 0;
    for (size_t i = 0; host[i]; i++)
        if (hostControlVariable(host[i]) && strncmp(host[i], "TWOINE_CHILD_ARGUMENTS=", 23))
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
    const uint32 cmd1616 = GLoaderState.convert32to1616(GLoaderState.pib.pib_pchcmd);
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
    APIRET ret = DosWaitChild(action, option, &res, &cpid, pid);
    pres->codeTerminate = res.codeTerminate;
    pres->codeResult = res.codeResult;
    *ppid = cpid;
    return ret;
}
