/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "include/twoine_ball.h"
#include "vendor/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *p = n ? malloc((size_t)n * sizeof(*p)) : NULL;
    if (p && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n))
    {
        free(p);
        return NULL;
    }
    return p;
}
static void env_set(const char *key, const char *value)
{
    wchar_t *k = wide(key), *v = value ? wide(value) : NULL;
    if (k)
        SetEnvironmentVariableW(k, v);
    free(k);
    free(v);
}
static int spawn(char **argv, int count, const char *cwd)
{
    wchar_t **args = calloc((size_t)count, sizeof(*args));
    size_t capacity = 1;
    if (!args)
        return 2;
    for (int i = 0; i < count; i++)
    {
        args[i] = wide(argv[i]);
        if (!args[i])
        {
            for (int j = 0; j < i; j++)
                free(args[j]);
            free(args);
            return 2;
        }
        capacity += wcslen(args[i]) * 2 + 4;
    }
    wchar_t *command = malloc(capacity * sizeof(*command)), *at = command;
    if (!command)
    {
        for (int i = 0; i < count; i++)
            free(args[i]);
        free(args);
        return 2;
    }
    /* Windows CreateProcess takes one command line. Quote each structured
       argument using the CRT's backslash-before-quote/end rules. */
    for (int i = 0; i < count; i++)
    {
        if (i)
            *at++ = L' ';
        *at++ = L'"';
        size_t slashes = 0;
        for (const wchar_t *p = args[i]; *p; p++)
        {
            if (*p == L'\\')
            {
                slashes++;
                continue;
            }
            size_t n = *p == L'"' ? slashes * 2 + 1 : slashes;
            while (n--)
                *at++ = L'\\';
            slashes = 0;
            *at++ = *p;
        }
        slashes *= 2;
        while (slashes--)
            *at++ = L'\\';
        *at++ = L'"';
    }
    *at = 0;
    wchar_t *directory = wide(cwd);
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {0};
    int code = 2;
    if (CreateProcessW(args[0], command, NULL, NULL, TRUE, 0, NULL, directory, &startup, &process))
    {
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD status = 2;
        GetExitCodeProcess(process.hProcess, &status);
        code = (int)status;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    free(directory);
    free(command);
    for (int i = 0; i < count; i++)
        free(args[i]);
    free(args);
    return code;
}
#else
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
static void env_set(const char *key, const char *value)
{
    if (value)
        setenv(key, value, 1);
    else
        unsetenv(key);
}
static int spawn(char **argv, int count, const char *cwd)
{
    (void)count;
    pid_t child = fork();
    if (child < 0)
        return 2;
    if (!child)
    {
        if (chdir(cwd))
            _exit(2);
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            return 2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? -WTERMSIG(status) : 2;
}
#endif
static int invoke(cJSON *request, char **result)
{
    char *text = cJSON_PrintUnformatted(request), *error = NULL;
    int status = tb_request(text, result, &error);
    cJSON_free(text);
    if (status)
        fprintf(stderr, "2ine-ball: %s\n", error ? error : "operation failed");
    tb_free(error);
    return status;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--help"))
    {
        puts("2ine-ball [--execute]: read one UTF-8 JSON request from stdin; see docs/balls.md");
        return 0;
    }
    int execute = argc > 1 && !strcmp(argv[1], "--execute");
    size_t length = 0, capacity = 4096;
    char *text = malloc(capacity);
    if (!text)
        return 2;
    int ch;
    while ((ch = getchar()) != EOF)
    {
        if (length + 1 >= capacity)
        {
            if (capacity >= 64 * 1024 * 1024)
            {
                free(text);
                return 2;
            }
            capacity *= 2;
            char *next = realloc(text, capacity);
            if (!next)
            {
                free(text);
                return 2;
            }
            text = next;
        }
        text[length++] = (char)ch;
    }
    text[length] = 0;
    cJSON *request = cJSON_ParseWithOpts(text, NULL, 1);
    free(text);
    if (!cJSON_IsObject(request))
    {
        fputs("Invalid JSON request\n", stderr);
        cJSON_Delete(request);
        return 2;
    }
    char *result = NULL;
    int status = invoke(request, &result);
    if (status)
    {
        cJSON_Delete(request);
        return status;
    }
    if (!execute)
    {
        puts(result);
        tb_free(result);
        cJSON_Delete(request);
        return 0;
    }
    cJSON *plan = cJSON_Parse(result);
    tb_free(result);
    cJSON *command = cJSON_GetObjectItemCaseSensitive(plan, "command");
    int count = cJSON_GetArraySize(command);
    const char *cwd = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(plan, "cwd"));
    if (!count || !cwd)
    {
        cJSON_Delete(plan);
        cJSON_Delete(request);
        return 2;
    }
    char **arguments = calloc((size_t)count + 1, sizeof(*arguments));
    if (!arguments)
    {
        cJSON_Delete(plan);
        cJSON_Delete(request);
        return 2;
    }
    for (int i = 0; i < count; i++)
        arguments[i] = cJSON_GetStringValue(cJSON_GetArrayItem(command, i));
    cJSON *entry;
    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(plan, "unsetEnvironment"))
        env_set(entry->valuestring, NULL);
    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(plan, "environment"))
        env_set(entry->string, entry->valuestring);
    int code = spawn(arguments, count, cwd);
    free(arguments);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(plan, "installer")))
    {
        cJSON *receipt = cJSON_CreateObject();
        cJSON_AddStringToObject(receipt, "action", "record-install");
        cJSON_AddItemToObject(
            receipt, "ball", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(request, "ball"), 1));
        cJSON_AddItemToObject(
            receipt, "receipt",
            cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(plan, "receipt"), 1));
        cJSON_AddItemToObject(
            receipt, "programHost",
            cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(plan, "programHost"), 1));
        cJSON_AddNumberToObject(receipt, "exitCode", code);
        char *saved = NULL;
        status = invoke(receipt, &saved);
        tb_free(saved);
        cJSON_Delete(receipt);
    }
    cJSON_Delete(plan);
    cJSON_Delete(request);
    return status ? status : code < 0 ? 128 - code : code;
}
