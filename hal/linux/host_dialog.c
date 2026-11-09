/* Public domain. No rights reserved. Wholly AI-authored. */
/* Locates and runs the native 2ine-dialog helper: TWOINE_DIALOG, then the
   helper beside the running loader, then PATH. No shell interprets arguments. */
#define _POSIX_C_SOURCE 200809L
#include "host_dialog.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
enum
{
    HELPER_CANCELLED = 1,
    MAX_ARGUMENTS = 256
};
static const char *helper_program(char *buffer, size_t size)
{
    const char *configured = getenv("TWOINE_DIALOG");
    if (configured && *configured)
        return configured;
    ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
    if (length > 0)
    {
        buffer[length] = 0;
        char *slash = strrchr(buffer, '/');
        if (slash && (size_t)(slash - buffer) + sizeof "/2ine-dialog" <= size)
        {
            strcpy(slash, "/2ine-dialog");
            if (!access(buffer, X_OK))
                return buffer;
        }
    }
    return "2ine-dialog";
}
LxHostSaveResult lx_host_dialog_run(const char *const arguments[], char **output)
{
    if (output)
        *output = NULL;
    char program[PATH_MAX];
    const char *argv[MAX_ARGUMENTS + 2];
    size_t count = 0;
    argv[count++] = helper_program(program, sizeof program);
    for (; arguments[count - 1]; count++)
    {
        if (count > MAX_ARGUMENTS)
            return LX_HOST_SAVE_ERROR;
        argv[count] = arguments[count - 1];
    }
    argv[count] = NULL;
    int descriptors[2];
    if (pipe(descriptors))
        return LX_HOST_SAVE_ERROR;
    pid_t child = fork();
    if (!child)
    {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(descriptors[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(descriptors[1]);
    const size_t capacity = 65536;
    char *buffer = malloc(capacity);
    size_t length = 0;
    ssize_t n = 0;
    if (child > 0 && buffer)
        while (length < capacity - 1 &&
               (n = read(descriptors[0], buffer + length, capacity - 1 - length)) != 0)
        {
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            length += (size_t)n;
        }
    close(descriptors[0]);
    int status = 0;
    pid_t waited;
    do
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    while (waited < 0 && errno == EINTR);
    if (child < 0 || waited != child || !WIFEXITED(status) || n < 0 || !buffer ||
        length == capacity - 1)
    {
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    if (WEXITSTATUS(status) == HELPER_CANCELLED)
    {
        free(buffer);
        return LX_HOST_SAVE_CANCEL;
    }
    if (WEXITSTATUS(status) != 0)
    {
        if (WEXITSTATUS(status) == 127)
            fprintf(stderr,
                    "2ine: Cannot run the native dialog helper %s. Install 2ine-dialog "
                    "beside the runtime or set TWOINE_DIALOG.\n",
                    argv[0]);
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    if (!output)
    {
        free(buffer);
        return LX_HOST_SAVE_OK;
    }
    if (length && buffer[length - 1] == '\n')
        length--;
    if (!length || memchr(buffer, 0, length))
    {
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    buffer[length] = 0;
    *output = strdup(buffer);
    free(buffer);
    return *output ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
}
