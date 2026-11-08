/* Public domain. No rights reserved. Wholly AI-authored. */
/* Unicode output is explicitly little-endian UTF-16, including unaligned buffers. */
#include "os2/include/pmwinx.h"
#include "emulator/abi.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LONG MultiByteToWideChar(ULONG, ULONG, PSZ, LONG, PVOID, LONG);
LONG WideCharToMultiByte(ULONG, ULONG, PVOID, LONG, PSZ, LONG, PSZ, PULONG);
static _Thread_local ULONG error;
static ULONG ansi = 1252, oem = 437;

void open32SetError(ULONG value)
{
    error = value;
}

ULONG GetACP(void)
{
    return ansi;
}

ULONG GetOEMCP(void)
{
    return oem;
}

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (error %u)\n", __FILE__, __LINE__, #x, error);              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void word(void *buffer, unsigned at, unsigned value)
{
    lx_guest_store_u16((UCHAR *)buffer + 2 * at, (USHORT)value);
}

int main(void)
{
    UCHAR guarded[64], original[64];
    memset(guarded, 0xa5, sizeof guarded);
    error = 0x5151;
    CHECK(MultiByteToWideChar(0, 1, "A\x80\xe9", -1, NULL, 0) == 4 && error == 0x5151);
    CHECK(MultiByteToWideChar(0, 1, "A\x80\xe9", -1, guarded + 1, 4) == 4);
    CHECK(lx_guest_load_u16(guarded + 1) == 'A');
    CHECK(lx_guest_load_u16(guarded + 3) == 0x20ac);
    CHECK(lx_guest_load_u16(guarded + 5) == 0xe9);
    CHECK(!lx_guest_load_u16(guarded + 7) && guarded[0] == 0xa5 && guarded[9] == 0xa5);
    CHECK(MultiByteToWideChar(437, 0, "\x82", 1, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0xe9);
    CHECK(MultiByteToWideChar(850, 0, "\xd5", 1, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0x131);
    ansi = 850;
    CHECK(MultiByteToWideChar(0, 0, "\x9b", 1, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0xf8);
    CHECK(MultiByteToWideChar(1, 0, "\x9b", 1, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0xa2);
    ansi = 1252;
    char embedded[] = {'A', 0, 'B'};
    CHECK(MultiByteToWideChar(1252, 0, embedded, 3, guarded, 3) == 3);
    CHECK(lx_guest_load_u16(guarded + 4) == 'B');
    memcpy(original, guarded, sizeof guarded);
    CHECK(!MultiByteToWideChar(1252, 0, embedded, 3, guarded, 2) && error == 122);
    CHECK(!memcmp(original, guarded, sizeof guarded));
    CHECK(!MultiByteToWideChar(0, 0, NULL, 1, guarded, 1) && error == 87);
    CHECK(!MultiByteToWideChar(0, 0, "x", 1, NULL, 1) && error == 87);
    CHECK(!MultiByteToWideChar(0, 0, "x", 0, guarded, 1) && error == 87);
    CHECK(!MultiByteToWideChar(0, 0, "x", -2, guarded, 1) && error == 87);
    CHECK(!MultiByteToWideChar(0, 0, "x", 1, guarded, -1) && error == 87);
    CHECK(!MultiByteToWideChar(0, 3, "x", 1, guarded, 1) && error == 1004);
    CHECK(!MultiByteToWideChar(0, 16, "x", 1, guarded, 1) && error == 1004);
    CHECK(!MultiByteToWideChar(0, 2, "x", 1, guarded, 1) && error == 50);
    CHECK(!MultiByteToWideChar(0, 4, "x", 1, guarded, 1) && error == 50);
    CHECK(!MultiByteToWideChar(932, 0, "x", 1, guarded, 1) && error == 50);
    CHECK(!MultiByteToWideChar(65001, 1, "x", 1, guarded, 1) && error == 50);
    CHECK(!MultiByteToWideChar(0, 0, (PSZ)guarded, 4, guarded + 1, 4) && error == 87);
    CHECK(!MultiByteToWideChar(0, 0, "x", INT_MAX, guarded, 1) && error == 8);
    CHECK(!MultiByteToWideChar(0, 0, "x", 1, guarded, 8 * 1024 * 1024 + 1) && error == 8);

    CHECK(MultiByteToWideChar(65001, 8, "\xf0\x9f\x98\x80", -1, guarded + 1, 3) == 3);
    CHECK(lx_guest_load_u16(guarded + 1) == 0xd83d && lx_guest_load_u16(guarded + 3) == 0xde00);
    CHECK(!lx_guest_load_u16(guarded + 5));
    CHECK(MultiByteToWideChar(65001, 0, "\xc3\xa9", 2, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0xe9);
    memcpy(original, guarded, sizeof guarded);
    CHECK(!MultiByteToWideChar(65001, 8, "\xc0\xaf", 2, guarded, 3) && error == 1113);
    CHECK(!memcmp(original, guarded, sizeof guarded));
    CHECK(MultiByteToWideChar(65001, 0, "\xc0\xaf", 2, guarded, 3) == 2);
    CHECK(lx_guest_load_u16(guarded) == 0xfffd && lx_guest_load_u16(guarded + 2) == 0xfffd);
    CHECK(MultiByteToWideChar(65001, 0, "\xf0\x9f", 2, guarded, 1) == 1 &&
          lx_guest_load_u16(guarded) == 0xfffd);
    CHECK(!MultiByteToWideChar(65001, 8, "\xed\xa0\x80", 3, guarded, 3) && error == 1113);

    UCHAR wide[32];
    word(wide, 0, 0xd83d);
    word(wide, 1, 0xde00);
    word(wide, 2, 0);
    ULONG used = 99;
    error = 0x5151;
    CHECK(WideCharToMultiByte(65001, 0, wide, -1, NULL, 0, NULL, &used) == 5 && !used &&
          error == 0x5151);
    CHECK(WideCharToMultiByte(65001, 0, wide, 2, (PSZ)guarded + 1, 4, NULL, &used) == 4 && !used);
    CHECK(!memcmp(guarded + 1, "\xf0\x9f\x98\x80", 4));
    word(wide, 0, 0x20ac);
    word(wide, 1, 0xe9);
    word(wide, 2, 0);
    CHECK(WideCharToMultiByte(1252, 0, wide, -1, (PSZ)guarded, 3, NULL, &used) == 3 && !used);
    CHECK(!memcmp(guarded, "\x80\xe9\0", 3));
    word(wide, 0, 0x2603);
    word(wide, 1, 0);
    CHECK(WideCharToMultiByte(1252, 0, wide, -1, (PSZ)guarded, 3, NULL, &used) == 2 && used == 1);
    CHECK(guarded[0] == '?' && !guarded[1]);
    CHECK(WideCharToMultiByte(1252, 0, wide, -1, (PSZ)guarded, 3, "XY", &used) == 3 && used == 1);
    CHECK(!memcmp(guarded, "XY\0", 3));
    used = 99;
    memcpy(original, guarded, sizeof guarded);
    CHECK(!WideCharToMultiByte(1252, 0, wide, -1, (PSZ)guarded, 1, "XY", &used) && error == 122 &&
          used == 99);
    CHECK(!memcmp(original, guarded, sizeof guarded));
    CHECK(!WideCharToMultiByte(1252, 0, wide, 1, (PSZ)guarded, -1, NULL, &used) && error == 122);
    CHECK(!WideCharToMultiByte(1252, 0, NULL, 1, (PSZ)guarded, 1, NULL, &used) && error == 87);
    CHECK(!WideCharToMultiByte(1252, 0, wide, 1, NULL, 1, NULL, &used) && error == 87);
    CHECK(!WideCharToMultiByte(1252, 0x100, wide, 1, (PSZ)guarded, 1, NULL, &used) &&
          error == 1004);
    CHECK(!WideCharToMultiByte(1252, 0x10, wide, 1, (PSZ)guarded, 1, NULL, &used) && error == 1004);
    CHECK(!WideCharToMultiByte(1252, 0x250, wide, 1, (PSZ)guarded, 1, NULL, &used) &&
          error == 1004);
    CHECK(!WideCharToMultiByte(1252, 0x200, wide, 1, (PSZ)guarded, 1, NULL, &used) && error == 50);
    CHECK(!WideCharToMultiByte(1252, 0, wide, 1, (PSZ)guarded, 1, "", &used) && error == 1359);
    CHECK(!WideCharToMultiByte(1252, 0, wide, 1, (PSZ)guarded, 1, "12345678901234", &used) &&
          error == 1359);
    CHECK(!WideCharToMultiByte(1252, 0, wide, 1, (PSZ)wide + 1, 2, NULL, &used) && error == 87);
    word(wide, 0, 0xd800);
    CHECK(WideCharToMultiByte(65001, 0, wide, 1, (PSZ)guarded, 3, NULL, &used) == 3 && used == 1);
    CHECK(!memcmp(guarded, "\xef\xbf\xbd", 3));

    char bytes[256], back[256];
    UCHAR units[512];
    for (unsigned i = 0; i < 256; i++)
        bytes[i] = (char)i;
    ULONG pages[] = {437, 850, 1252};
    for (unsigned i = 0; i < 3; i++)
    {
        CHECK(MultiByteToWideChar(pages[i], 0, bytes, 256, units, 256) == 256);
        used = 99;
        CHECK(WideCharToMultiByte(pages[i], 0, units, 256, back, 256, NULL, &used) == 256 && !used);
        CHECK(!memcmp(bytes, back, 256));
    }
    puts(
        "Open32 counted SBCS/UTF8/UTF16, terminators, errors, guarded output and default substitutions passed");
    return 0;
}
