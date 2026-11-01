/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sys_null.h -- null system driver to aid porting efforts

#include "quakedef.h"
#include <errno.h>
#include <direct.h>
#define INCL_DOS
#include <os2.h>

qboolean isDedicated = false;
static FILE *logfile;
static int quitting;
void OS2_PumpEvents(void);

/*
===============================================================================

FILE IO

===============================================================================
*/

#define MAX_HANDLES 10
FILE *sys_handles[MAX_HANDLES];

int findhandle(void)
{
    int i;

    for (i = 1; i < MAX_HANDLES; i++)
        if (!sys_handles[i])
            return i;
    Sys_Error("out of handles");
    return -1;
}

/*
================
filelength
================
*/
int filelength(FILE *f)
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

int Sys_FileOpenRead(char *path, int *hndl)
{
    FILE *f;
    int i;

    i = findhandle();

    f = fopen(path, "rb");
    if (!f)
    {
        *hndl = -1;
        return -1;
    }
    sys_handles[i] = f;
    *hndl = i;

    return filelength(f);
}

int Sys_FileOpenWrite(char *path)
{
    FILE *f;
    int i;

    i = findhandle();

    f = fopen(path, "wb");
    if (!f)
        Sys_Error("Error opening %s: %s", path, strerror(errno));
    sys_handles[i] = f;

    return i;
}

void Sys_FileClose(int handle)
{
    fclose(sys_handles[handle]);
    sys_handles[handle] = NULL;
}

void Sys_FileSeek(int handle, int position)
{
    fseek(sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    return fread(dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite(int handle, void *data, int count)
{
    return fwrite(data, 1, count, sys_handles[handle]);
}

int Sys_FileTime(char *path)
{
    FILE *f;

    f = fopen(path, "rb");
    if (f)
    {
        fclose(f);
        return 1;
    }

    return -1;
}

void Sys_mkdir(char *path)
{
    mkdir(path);
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) {}

void Sys_Error(char *error, ...)
{
    va_list args;
    char message[2048];
    ULONG written;
    va_start(args, error);
    vsnprintf(message, sizeof(message), error, args);
    va_end(args);
    DosWrite(2, message, strlen(message), &written);
    if (logfile)
    {
        fputs(message, logfile);
        fputs("\n", logfile);
        fflush(logfile);
    }
    VID_Shutdown();
    exit(1);
}

void Sys_Printf(char *fmt, ...)
{
    va_list args;
    char message[4096];
    ULONG written;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    DosWrite(1, message, strlen(message), &written);
    if (logfile)
    {
        fputs(message, logfile);
        fflush(logfile);
    }
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    FILE *f = fopen(file, "a");
    va_list args;
    if (!f)
        return;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

void Sys_Quit(void)
{
    if (!quitting)
    {
        quitting = 1;
        Host_Shutdown();
    }
    Sys_Printf("GLQuake shutdown complete\n");
    if (logfile)
        fclose(logfile);
    exit(0);
}

double Sys_FloatTime(void)
{
    static ULONG previous;
    static double elapsed;
    static int initialized;
    ULONG now;
    DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &now, sizeof(now));
    if (initialized)
        elapsed += (ULONG)(now - previous) * 0.001;
    previous = now;
    initialized = 1;
    return elapsed;
}

char *Sys_ConsoleInput(void)
{
    return NULL;
}

void Sys_Sleep(void)
{
    DosSleep(1);
}

void Sys_SendKeyEvents(void)
{
    OS2_PumpEvents();
}

void Sys_HighFPPrecision(void) {}

void Sys_LowFPPrecision(void) {}

void Sys_SetFPCW(void) {}

int main(int argc, char **argv)
{
    quakeparms_t parms;
    double previous, now, delta;
    int frames = 0, limit = 0, i;
    memset(&parms, 0, sizeof(parms));
    logfile = fopen("glquake.log", "w");
    COM_InitArgv(argc, argv);
    parms.memsize = 24 * 1024 * 1024;
    parms.membase = malloc(parms.memsize);
    if (!parms.membase)
        Sys_Error("Cannot allocate Quake heap");
    parms.basedir = ".";
    parms.argc = com_argc;
    parms.argv = com_argv;
    i = COM_CheckParm("-frames");
    if (i && i + 1 < com_argc)
        limit = atoi(com_argv[i + 1]);
    Host_Init(&parms);
    previous = Sys_FloatTime();
    for (;;)
    {
        now = Sys_FloatTime();
        delta = now - previous;
        if (delta < 0.001)
        {
            DosSleep(1);
            continue;
        }
        previous = now;
        Host_Frame(delta);
        if (limit && ++frames >= limit)
            Sys_Quit();
    }
}
