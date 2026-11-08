/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* The OS/2 process information block: identifiers, command line and environment. */
#include "emulator/process/internal.h"

void initPib(void)
{
    /* Guest resolver libraries locate RESOLV2/HOSTS through ETC. */
    if (!getenv("ETC"))
        setenv("ETC", "C:\\MPTN\\ETC", 0);
    LxPIB *pib = &GProcessState.pib;
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

    if (!argc || !cmdlinebuflen || cmdlinebuf[cmdlinebuflen - 1])
    {
        fprintf(stderr, "2ine: invalid host command line\n");
        free(cmdlinebuf);
        lx_host_process_exit(1);
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
    /* The loader can be launched by Finder before a guest program is selected. */
    const char *program_name = argc && argv[0] ? argv[0] : "";
    const char *original_arguments =
        GProcessState.subprocess ? getenv("TWOINE_CHILD_ARGUMENTS") : NULL;

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

    len += strlen(program_name) + 1;
    if (original_arguments)
        len += strlen(original_arguments) + 1;

    char *const *envp = lx_host_environment();

    const char *guest_path = getenv("OS2PATH");
    if (!guest_path)
        guest_path = "";
    char *path = malloc(strlen(guest_path) + 6);
    if (!path)
        abort();
    sprintf(path, "PATH=%s", guest_path);
    for (int i = 0; envp[i]; i++)
    {
        const char *str = envp[i];
        if (strncmp(str, "PATH=", 5) == 0)
        {
            if (!GProcessState.subprocess)
                str = path;
        }
        else if (strncmp(str, "IS_2INE=", 8) == 0 || !strncmp(str, "TWOINE_CHILD_ARGUMENTS=", 23))
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
            if (!GProcessState.subprocess)
                str = path;
        }
        else if (strncmp(str, "LIBPATH=", 8) == 0)
        {
            libpath = str + 8;
        }
        else if (strncmp(str, "IS_2INE=", 8) == 0 || !strncmp(str, "TWOINE_CHILD_ARGUMENTS=", 23))
        {
            continue;
        }

        strcpy(ptr, str);
        ptr += strlen(str) + 1;
    }
    *(ptr++) = '\0';

    if (libpath == NULL)
        libpath = "";

    GProcessState.libpath = strdup(libpath);
    GProcessState.libpathlen = strlen(libpath) + 1;

    const char *begin = getenv("TWOINE_EXT_LIBPATH_BEGIN"), *end = getenv("TWOINE_EXT_LIBPATH_END");
    snprintf(GProcessState.begin_libpath, sizeof GProcessState.begin_libpath, "%s",
             begin ? begin : "");
    snprintf(GProcessState.end_libpath, sizeof GProcessState.end_libpath, "%s", end ? end : "");
    const char *strict = getenv("TWOINE_EXT_LIBPATH_STRICT");
    GProcessState.libpath_strict = strict && !strcasecmp(strict, "T");

    strcpy(ptr, program_name);
    ptr += strlen(program_name) + 1;

    char *cmd = ptr;
    strcpy(ptr, program_name);
    ptr += strlen(program_name);
    *(ptr++) = '\0';
    if (original_arguments)
    {
        /* DosExecPgm supplies an OS/2 command tail, not a host argv serialization. */
        strcpy(ptr, original_arguments);
        ptr += strlen(original_arguments);
    }
    for (int i = 1; !original_arguments && i < argc; i++)
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
                /* OS/2 command tails keep backslashes literal inside quotes. */
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

    free(path);
    free(cmdlinebuf);
    free(argv);
}
