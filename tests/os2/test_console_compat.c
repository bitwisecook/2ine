/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/include/bseerr.h"
#include "os2/include/bsedos.h"
#include "os2/include/bsedos16.h"
#include "os2/include/bsesub.h"
#include "emulator/runtime.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(test)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(test))                                                                               \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define LOAD(module, function)                                                                     \
    __typeof__(function) *call_##function = dlsym(module, #function);                              \
    CHECK(call_##function)

static void keyboard(void *module)
{
    LOAD(module, KbdGetStatus);
    LOAD(module, KbdSetStatus);
    LOAD(module, KbdGetCp);
    LOAD(module, KbdSetCp);
    LOAD(module, KbdFlushBuffer);
    LOAD(module, KbdPeek);
    LOAD(module, KbdCharIn);
    LOAD(module, KbdStringIn);

    struct
    {
        USHORT before;
        KBDINFO info;
        USHORT after;
    } output = {0xa55a, {10}, 0x5aa5};

    CHECK(sizeof(KBDINFO) == 10);
    CHECK(!call_KbdGetStatus(&output.info, 0));
    CHECK(output.before == 0xa55a && output.after == 0x5aa5);
    CHECK(output.info.cb == 10 && output.info.chTurnAround == '\r');
    output.info.fsMask = 3;
    CHECK(call_KbdSetStatus(&output.info, 0) == ERROR_KBD_INVALID_ECHO_MASK);
    output.info.fsMask = 12;
    CHECK(call_KbdSetStatus(&output.info, 0) == ERROR_KBD_INVALID_INPUT_MASK);
    output.info.fsMask = 0x004a;
    output.info.chTurnAround = ';';
    CHECK(!call_KbdSetStatus(&output.info, 0));
    CHECK(!call_KbdGetStatus(&output.info, 0));
    CHECK(output.info.fsMask == 10 && output.info.chTurnAround == ';');
    USHORT cp;
    CHECK(!call_KbdSetCp(0, 850, 0));
    CHECK(!call_KbdGetCp(0, &cp, 0) && cp == 850);
    CHECK(call_KbdSetCp(0, 999, 0) == ERROR_KBD_INVALID_CODEPAGE);
    CHECK(!call_KbdGetCp(0, &cp, 0) && cp == 850);
    CHECK(call_KbdFlushBuffer(1) == ERROR_KBD_INVALID_HANDLE);
    CHECK(!call_KbdFlushBuffer(0));
    int input[2];
    CHECK(pipe(input) == 0);
    CHECK(write(input[1], "AB\nCD", 5) == 5);
    close(input[1]);
    int saved = dup(STDIN_FILENO);
    CHECK(saved >= 0 && dup2(input[0], STDIN_FILENO) >= 0);
    close(input[0]);
    KBDKEYINFO key;
    CHECK(!call_KbdPeek(&key, 0) && key.chChar == 'A' && (key.fbStatus & 0x40));
    CHECK(!call_KbdPeek(&key, 0) && key.chChar == 'A');
    CHECK(!call_KbdCharIn(&key, 1, 0) && key.chChar == 'A');
    CHECK(!call_KbdPeek(&key, 0) && key.chChar == 'B');
    CHECK(!call_KbdFlushBuffer(0));
    CHECK(!call_KbdCharIn(&key, 1, 0) && key.chChar == '\r');
    STRINGINBUF length = {1, 0};
    CHAR bytes[2] = {0, 0x55};
    CHECK(!call_KbdStringIn(bytes, &length, 1, 0) && length.cchIn == 1 && bytes[0] == 'C');
    CHECK(bytes[1] == 0x55);
    CHECK(!call_KbdCharIn(&key, 1, 0) && key.chChar == 'D');
    CHECK(!call_KbdPeek(&key, 0) && !key.fbStatus);
    CHECK(dup2(saved, STDIN_FILENO) >= 0);
    close(saved);
}

static void video(void *module)
{
    LOAD(module, VioGetMode);
    LOAD(module, VioSetMode);
    LOAD(module, VioGetConfig);
    LOAD(module, VioGetAnsi);
    LOAD(module, VioSetCp);
    LOAD(module, VioGetCp);
    LOAD(module, VioWrtTty);
    LOAD(module, VioReadCellStr);
    LOAD(module, VioScrollUp);
    LOAD(module, VioGetCurPos);
    LOAD(module, VioGetCurType);
    LOAD(module, VioSetCurType);
    LOAD(module, VioReadCharStr);
    LOAD(module, VioScrollDn);
    CHECK(sizeof(VIOCONFIGINFO) == 32);

    struct
    {
        USHORT before;
        unsigned char bytes[34];
        USHORT after;
    } output;

    memset(&output, 0xcc, sizeof(output));
    VIOMODEINFO *mode = (VIOMODEINFO *)output.bytes;
    mode->cb = 12;
    CHECK(!call_VioGetMode(mode, 0));
    CHECK(output.before == 0xcccc && output.after == 0xcccc);
    for (unsigned i = 12; i < 34; i++)
        CHECK(output.bytes[i] == 0xcc);
    VIOCONFIGINFO config;
    memset(&config, 0xcc, sizeof(config));
    config.cb = 10;
    CHECK(!call_VioGetConfig(0, &config, 0));
    CHECK(config.adapter == 3 && config.display == 4 && config.cbMemory == 262144);
    for (unsigned i = 10; i < sizeof(config); i++)
        CHECK(((unsigned char *)&config)[i] == 0xcc);
    USHORT cp, ansi;
    CHECK(!call_VioGetAnsi(&ansi, 0) && ansi == 0);
    CHECK(!call_VioSetCp(0, 850, 0));
    CHECK(!call_VioGetCp(0, &cp, 0) && cp == 850);
    CHECK(call_VioSetCp(0, 999, 0) == ERROR_VIO_BAD_CP);
    mode->col = 8;
    mode->row = 2;
    CHECK(!call_VioSetMode(mode, 0));
    CHECK(!call_VioWrtTty("AB\bC\r\nD\tE", 9, 0));
    USHORT row, column;
    CHECK(!call_VioGetCurPos(&row, &column, 0) && row == 1 && column == 1);
    unsigned char cells[32];
    USHORT length = sizeof(cells);
    CHECK(!call_VioReadCellStr((PCH)cells, &length, 0, 0, 0));
    CHECK(length == 32 && cells[0] == 'D' && cells[16] == 'E');
    UCHAR blank[2] = {' ', 7};
    CHECK(!call_VioScrollUp(0, 0, 1, 7, 1, blank, 0));
    length = sizeof(cells);
    CHECK(!call_VioReadCellStr((PCH)cells, &length, 0, 0, 0));
    CHECK(cells[0] == 'E' && cells[16] == ' ');
    CHECK(!call_VioScrollUp(0, 0, 0, 0, 0, blank, 0));
    length = 2;
    CHECK(!call_VioReadCellStr((PCH)cells, &length, 0, 0, 0) && cells[0] == ' ');
    VIOCURSORINFO cursor = {1, 2, 3, 0xffff}, actual;
    CHECK(!call_VioSetCurType(&cursor, 0));
    CHECK(!call_VioGetCurType(&actual, 0) && !memcmp(&actual, &cursor, sizeof(cursor)));
    CHECK(!call_VioSetMode(mode, 0));
    CHECK(!call_VioWrtTty("12345678ABCDEFG", 15, 0));
    CHECK(!call_VioScrollDn(0, 2, 1, 4, 1, blank, 0));
    CHAR text[20];
    memset(text, 0xcc, sizeof(text));
    length = sizeof(text);
    CHECK(!call_VioReadCharStr(text, &length, 0, 0, 0) && length == 16);
    CHECK(!memcmp(text, "12   678AB345FG ", 16));
    CHECK((unsigned char)text[16] == 0xcc);
    CHECK(!call_VioScrollDn(1, 7, 1, 7, 0, blank, 0));
    CHECK(call_VioReadCharStr(text, &length, 2, 0, 0) == ERROR_VIO_ROW);
}

static void nls(void *module)
{
    LOAD(module, Dos16GetCollate);
    LOAD(module, Dos16CaseMap);
    LOAD(module, Dos16GetCtryInfo);
    LOAD(module, DosQueryCollate);
    COUNTRYCODE16 country = {1, 437};
    unsigned char weights[258];
    memset(weights, 0xcc, sizeof(weights));
    USHORT length;
    CHECK(call_Dos16GetCollate(16, &country, (PCHAR)weights + 1, &length) ==
          ERROR_NLS_TABLE_TRUNCATED);
    CHECK(length == 256 && weights[0] == 0xcc && weights[17] == 0xcc);
    CHECK(!call_Dos16GetCollate(256, &country, (PCHAR)weights + 1, &length));
    CHECK(weights[0] == 0xcc && weights[257] == 0xcc);
    CHECK(weights['a' + 1] == weights['A' + 1]);
    CHECK(weights[0x82 + 1] == 'E' && weights[0x81 + 1] == 'U');
    COUNTRYCODE country32 = {1, 437};

    struct
    {
        ULONG before, length, after;
    } actual32 = {0xa55aa55a, 0, 0x5aa55aa5};

    unsigned char copy[258];
    memset(copy, 0xcc, sizeof(copy));
    CHECK(!call_DosQueryCollate(256, &country32, (PCHAR)copy + 1, &actual32.length));
    CHECK(actual32.before == 0xa55aa55a && actual32.after == 0x5aa55aa5 && actual32.length == 256);
    CHECK(!memcmp(copy, weights, sizeof(copy)));
    unsigned char string[] = {'a', 0x81, 0x82, 0xa4};
    CHECK(!call_Dos16CaseMap(sizeof(string), &country, (PCHAR)string));
    CHECK(string[0] == 'A' && string[1] == 0x9a && string[2] == 'E' && string[3] == 0xa5);
    country.codepage = 850;
    string[0] = 0x82;
    CHECK(!call_Dos16CaseMap(1, &country, (PCHAR)string) && string[0] == 0x90);
    country.country = 99;
    CHECK(call_Dos16GetCollate(256, &country, (PCHAR)weights, &length) == ERROR_NLS_NO_CTRY_CODE);
    country.country = 1;
    country.codepage = 999;
    CHECK(call_Dos16GetCollate(256, &country, (PCHAR)weights, &length) ==
          ERROR_CODE_PAGE_NOT_FOUND);
    country.codepage = 437;

    struct
    {
        unsigned char before;
        unsigned char bytes[38];
        unsigned char after;
    } output;

    memset(&output, 0xcc, sizeof(output));
    CHECK(!call_Dos16GetCtryInfo(6, &country, (PCOUNTRYINFO16)output.bytes, &length));
    CHECK(length == 38 && output.before == 0xcc && output.after == 0xcc);
    for (unsigned i = 6; i < 38; i++)
        CHECK(output.bytes[i] == 0xcc);
}

/* DOSCALLS implements the DBCS vector once; NLS exports it by forwarding. The
   request is a COUNTRYCODE of two ULONGs (two USHORTs for the 16-bit entry),
   0 selects the default, and the SBCS code pages return only zeros. */
static void dbcs(void *module)
{
    LOAD(module, DosQueryDBCSEnv);
    LOAD(module, Dos16GetDBCSEv);
    unsigned char vector[12];
    COUNTRYCODE request = {0, 0};
    memset(vector, 0xcc, sizeof(vector));
    CHECK(!call_DosQueryDBCSEnv(sizeof(vector), &request, (PCHAR)vector));
    for (unsigned i = 0; i < sizeof(vector); i++)
        CHECK(vector[i] == 0);
    request = (COUNTRYCODE){1, 0};
    CHECK(!call_DosQueryDBCSEnv(sizeof(vector), &request, (PCHAR)vector));
    request = (COUNTRYCODE){1, 850};
    CHECK(!call_DosQueryDBCSEnv(sizeof(vector), &request, (PCHAR)vector));
    request = (COUNTRYCODE){0, 437};
    memset(vector, 0xcc, sizeof(vector));
    CHECK(!call_DosQueryDBCSEnv(4, &request, (PCHAR)vector));
    CHECK(!vector[0] && !vector[3] && vector[4] == 0xcc);
    CHECK(!call_DosQueryDBCSEnv(0, &request, NULL));
    request = (COUNTRYCODE){99, 0};
    CHECK(call_DosQueryDBCSEnv(sizeof(vector), &request, (PCHAR)vector) == ERROR_NLS_NO_CTRY_CODE);
    request = (COUNTRYCODE){0, 999};
    CHECK(call_DosQueryDBCSEnv(sizeof(vector), &request, (PCHAR)vector) ==
          ERROR_CODE_PAGE_NOT_FOUND);
    CHECK(call_DosQueryDBCSEnv(sizeof(vector), NULL, (PCHAR)vector) == ERROR_INVALID_PARAMETER);
    CHECK(call_DosQueryDBCSEnv(sizeof(vector), &request, NULL) == ERROR_CODE_PAGE_NOT_FOUND);
    request = (COUNTRYCODE){0, 0};
    CHECK(call_DosQueryDBCSEnv(sizeof(vector), &request, NULL) == ERROR_INVALID_PARAMETER);

    COUNTRYCODE16 request16 = {1, 437};
    memset(vector, 0xcc, sizeof(vector));
    CHECK(!call_Dos16GetDBCSEv(4, &request16, (PCHAR)vector));
    CHECK(!vector[0] && !vector[3] && vector[4] == 0xcc);
    request16 = (COUNTRYCODE16){99, 0};
    CHECK(call_Dos16GetDBCSEv(4, &request16, (PCHAR)vector) == ERROR_NLS_NO_CTRY_CODE);
    request16 = (COUNTRYCODE16){0, 999};
    CHECK(call_Dos16GetDBCSEv(4, &request16, (PCHAR)vector) == ERROR_CODE_PAGE_NOT_FOUND);
    CHECK(call_Dos16GetDBCSEv(4, NULL, (PCHAR)vector) == ERROR_INVALID_PARAMETER);
}

int main(int argc, char **argv)
{
    CHECK(argc == 4);
    /* Referencing the process state links lib2ine at startup: its TLS must be
       in the static block before the DLLs that use it are opened. */
    GProcessState.process_codepage = 0;
    void *kbd = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    void *vio = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    void *doscalls = dlopen(argv[3], RTLD_NOW | RTLD_LOCAL);
    CHECK(kbd && vio && doscalls);
    keyboard(kbd);
    video(vio);
    nls(doscalls);
    dbcs(doscalls);
    dlclose(doscalls);
    dlclose(vio);
    dlclose(kbd);
    return 0;
}
