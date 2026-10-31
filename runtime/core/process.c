/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"
void initPib(void)
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

    char *const *envp = lx_host_environment();

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
