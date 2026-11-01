/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent profiles and Open32 values across independent process lifetimes. */
#include "os2libs/pmshapi.h"
#include "os2libs/pmwinx.h"
#include "os2libs/helpmgr.h"
#include "os2libs/pmwp.h"
#include "os2libs/pmwin.h"
#include "runtime/runtime.h"
#include <limits.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char *host_path(const char *name, uint32 *error)
{
    *error = 0;
    return strdup(name);
}
static void require_string(HINI profile, char *app, char *key, const char *expected)
{
    char text[64];
    memset(text, 0xcc, sizeof text);
    CHECK(PrfQueryProfileString(profile, app, key, "fallback", text, sizeof text) ==
          strlen(expected));
    CHECK(!strcmp(text, expected));
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    GLoaderState.makeUnixPath = host_path;
    int writing = !strcmp(argv[1], "write");
    ULONG desktop = WinQueryObject("<WP_DESKTOP>");
    CHECK(desktop && !WinDestroyObject(desktop));
    if (writing)
    {
        ULONG folder =
            WinCreateObject("WPFolder", "Suite", "OBJECTID=<TEST_SUITE>;", "<WP_DESKTOP>", 0);
        CHECK(folder && folder == WinQueryObject("<test_suite>"));
        CHECK(!WinCreateObject("WPFolder", "Suite", "", "<WP_DESKTOP>", 0));
        CHECK(!WinCreateObject("Unknown", "Title", "", "<WP_DESKTOP>", 0));
        CHECK(!WinCreateObject("WPProgram", "App", "", "<MISSING>", 0));
        ULONG app =
            WinCreateObject("WPProgram", "Editor", "OBJECTID=<TEST_APP>;EXENAME=C:\\EDITOR.EXE;",
                            "<TEST_SUITE>", 0);
        CHECK(app && !WinDestroyObject(folder));
        CHECK(app ==
              WinCreateObject("WPProgram", "Editor", "PARAMETERS=one^;two;", "<TEST_SUITE>", 2));
        CHECK(app == WinQueryObject("<TEST_APP>"));
        CHECK(WinSetObjectData(app, "OBJECTID=<TEST_RENAMED>;"));
        CHECK(!WinQueryObject("<TEST_APP>") && WinQueryObject("<TEST_RENAMED>") == app);
    }
    else
    {
        ULONG app = WinQueryObject("<TEST_RENAMED>");
        ULONG folder = WinQueryObject("<TEST_SUITE>");
        CHECK(app && folder);
        CHECK(WinDestroyObject(app) && !WinQueryObject("<TEST_RENAMED>"));
        CHECK(!WinSetObjectData(app, "TITLE=stale;"));
        CHECK(WinDestroyObject(folder) && !WinQueryObject("<TEST_SUITE>"));
    }
    HINI profile = PrfOpenProfile(0, "private.ini");
    CHECK(profile && profile != HINI_USERPROFILE && profile != HINI_SYSTEMPROFILE);
    WinGetLastError(anchor);
    char untouched[4] = "xyz";
    CHECK(!PrfQueryProfileString(profile, "Absent", "Missing", NULL, untouched, sizeof untouched));
    CHECK(!strcmp(untouched, "xyz"));
    CHECK((WinGetLastError(anchor) & 0xffff) == 0x1304);
    CHECK(!WinGetLastError(anchor));
    if (writing)
    {
        CHECK(PrfWriteProfileString(profile, "App", "String", "hello"));
        CHECK(PrfWriteProfileString(profile, "App", "Number", "-2147483648"));
        CHECK(PrfWriteProfileString(profile, "App", "Overflow", "2147483648"));
        uint8 bytes[] = {0, 0xff, 0x42, 0};
        CHECK(PrfWriteProfileData(profile, "App", "Binary", bytes, sizeof bytes));
        CHECK(PrfWriteProfileData(profile, "App", "Empty", bytes, 0));
        HINI duplicate = PrfOpenProfile(0, "private.ini");
        CHECK(duplicate && duplicate != profile);
        require_string(duplicate, "App", "String", "hello");
        CHECK(PrfCloseProfile(profile));
        CHECK(!PrfCloseProfile(profile));
        profile = duplicate;
        CHECK(PrfWriteProfileString(profile, "Delete", "Key", "gone"));
        CHECK(PrfWriteProfileString(profile, "Delete", NULL, NULL));
        ULONG size = 99;
        CHECK(!PrfQueryProfileSize(profile, "Delete", "Key", &size));
        CHECK(PrfWriteProfileString(HINI_SYSTEMPROFILE, "Merged", "Both", "system"));
        CHECK(PrfWriteProfileString(HINI_SYSTEMPROFILE, "Merged", "System", "only"));
        CHECK(PrfWriteProfileString(HINI_USERPROFILE, "Merged", "Both", "user"));
    }
    require_string(profile, "App", "String", "hello");
    require_string(profile, "app", "String", "fallback");
    CHECK(PrfQueryProfileInt(profile, "App", "Number", 42) == INT32_MIN);
    CHECK(PrfQueryProfileInt(profile, "App", "Overflow", 42) == 42);
    char truncated[4] = {0};
    CHECK(PrfQueryProfileString(profile, "App", "String", NULL, truncated, 3) == 2);
    CHECK(!strcmp(truncated, "he"));
    ULONG size = 0;
    CHECK(PrfQueryProfileSize(profile, "App", "Binary", &size) && size == 4);
    uint8 binary[5] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    size = 2;
    CHECK(!PrfQueryProfileData(profile, "App", "Binary", binary, &size));
    CHECK(size == 4 && binary[0] == 0 && binary[1] == 0xff && binary[2] == 0xcc);
    size = 4;
    CHECK(PrfQueryProfileData(profile, "App", "Binary", binary, &size));
    CHECK(binary[2] == 0x42 && binary[3] == 0 && binary[4] == 0xcc);
    CHECK(PrfQueryProfileSize(profile, "App", "Empty", &size) && size == 0);
    HINI same_user = PrfOpenProfile(0, getenv("TWOINE_USER_PROFILE"));
    CHECK(same_user);
    require_string(same_user, "Merged", "Both", "user");
    CHECK(PrfCloseProfile(same_user));
    require_string(HINI_PROFILE, "Merged", "Both", "user");
    require_string(HINI_PROFILE, "Merged", "System", "only");
    require_string(HINI_SYSTEMPROFILE, "Merged", "Both", "system");
    char names[64] = {0};
    CHECK(PrfQueryProfileSize(HINI_PROFILE, "Merged", NULL, &size) && size == 13);
    CHECK(PrfQueryProfileString(HINI_PROFILE, "Merged", NULL, NULL, names, sizeof names) == 12);
    CHECK(!memcmp(names, "Both\0System\0\0", 13));
    CHECK(PrfCloseProfile(profile));
    CHECK(!PrfQueryProfileSize(profile, "App", "String", &size));
    CHECK(!PrfCloseProfile(HINI_PROFILE) && !PrfCloseProfile(HINI_USERPROFILE));

    /* A failed save must preserve the last successful in-memory snapshot. */
    profile = PrfOpenProfile(0, "missing-parent/failure.ini");
    CHECK(profile);
    CHECK(!PrfWriteProfileString(profile, "App", "Key", "no"));
    CHECK(!PrfQueryProfileSize(profile, "App", "Key", &size));
    CHECK(PrfCloseProfile(profile));
    profile = PrfOpenProfile(0, argv[2]);
    CHECK(profile);
    require_string(profile, "Native", "Key", "value");
    CHECK(PrfCloseProfile(profile));
    HINI cleared = PrfOpenProfile(0, "cleared.ini");
    CHECK(cleared && PrfWriteProfileString(cleared, "App", "Key", "value"));
    CHECK(PrfWriteProfileData(cleared, NULL, NULL, NULL, 0));
    CHECK(!PrfQueryProfileSize(cleared, "App", "Key", &size));
    CHECK(PrfCloseProfile(cleared));
    CHECK(!PrfOpenProfile(0, "cycle.ini"));
    CHECK(!PrfOpenProfile(0, "truncated.ini"));

    SWCNTRL switch_value = {0};
    switch_value.hwnd = 123;
    switch_value.uchVisibility = 4;
    switch_value.fbJump = 2;
    strcpy(switch_value.szSwtitle, "Suite installer");
    CHECK(sizeof switch_value == 96);
    ULONG switch_handle = WinAddSwitchEntry(&switch_value);
    CHECK(switch_handle && WinQuerySwitchHandle(123, 0) == switch_handle);
    SWCNTRL switch_copy;
    CHECK(!WinQuerySwitchEntry(switch_handle, &switch_copy) && switch_copy.idProcess &&
          !strcmp(switch_copy.szSwtitle, "Suite installer"));
    strcpy(switch_value.szSwtitle, "Suite progress");
    CHECK(!WinChangeSwitchEntry(switch_handle, &switch_value));
    CHECK(!WinQuerySwitchEntry(switch_handle, &switch_copy) &&
          !strcmp(switch_copy.szSwtitle, "Suite progress"));
    CHECK(!WinRemoveSwitchEntry(switch_handle) && WinRemoveSwitchEntry(switch_handle) == 6);
    CHECK(!WinQuerySwitchHandle(123, 0));

    ULONG key = 0, disposition = 0;
    if (writing)
    {
        CHECK(RegOpenKeyEx(0xffffffef, "Software\\Test", 0, 3, &key) == 2);
        CHECK(!RegCreateKeyEx(0xffffffef, "Software\\Test", 0, NULL, 0, 0x001f003f, NULL, &key,
                              &disposition) &&
              disposition == 1);
        CHECK(!RegSetValueEx(key, NULL, 0, 1, "default", 8));
        CHECK(!RegSetValueEx(key, "Raw", 0, 3, binary, 4));
        CHECK(!RegCloseKey(key));
    }
    CHECK(RegOpenKeyEx(0x80000002, "SOFTWARE\\TEST", 0, 0x00200000, &key) == 87);
    CHECK(!RegOpenKeyEx(0x80000002, "SOFTWARE\\TEST\\", 0, 1, &key));
    CHECK(key && !PrfCloseProfile(key));
    ULONG type = 99;
    size = 0;
    CHECK(!RegQueryValueEx(key, "", NULL, &type, NULL, &size) && type == 1 && size == 8);
    char value[16];
    memset(value, 0xcc, sizeof value);
    size = 3;
    CHECK(RegQueryValueEx(key, NULL, NULL, &type, value, &size) == 234 && size == 8);
    CHECK((uint8)value[0] == 0xcc);
    size = sizeof value;
    CHECK(!RegQueryValueEx(key, NULL, NULL, &type, value, &size) && !strcmp(value, "default"));
    size = 4;
    CHECK(!RegQueryValueEx(key, "rAw", NULL, &type, binary, &size) && type == 3 && size == 4);
    CHECK(binary[0] == 0 && binary[1] == 0xff && binary[2] == 0x42);
    CHECK(RegSetValueEx(key, "NoWrite", 0, 1, "", 1) == 5);
    CHECK(RegDeleteValue(key, "Raw") == 5);
    CHECK(RegCreateKeyEx(key, "Child", 0, NULL, 0, 3, NULL, &disposition, NULL) == 5);
    CHECK(!RegCloseKey(key) && RegCloseKey(key) == 6);
    CHECK(RegQueryValueEx(key, NULL, NULL, NULL, NULL, &size) == 6);
    CHECK(!RegCreateKeyEx(0x80000002, "software\\test", 0, NULL, 0, 3, NULL, &key, &disposition) &&
          disposition == 2);
    CHECK(!RegCloseKey(key));
    CHECK(!RegCloseKey(0xffffffef));
    CHECK(RegOpenKey(0xffffffef, "Software\\Absent", &key) == 2);
    CHECK(!RegOpenKey(0xffffffef, "Software\\Test", &key));
    ULONG same = 0;
    CHECK(!RegOpenKey(key, NULL, &same) && same == key);
    CHECK(!RegSetValueEx(key, "Legacy", 0, 1, "value", 6));
    CHECK(!RegDeleteValue(key, "lEgAcY") && RegDeleteValue(key, "Legacy") == 2);
    CHECK(RegDeleteValue(0xdeadbeef, "Legacy") == 6);
    CHECK(!RegCloseKey(key));
    CHECK(RegOpenKey(key, "", &same) == 6);
    CHECK(!RegOpenKey(0xffffffef, "", &same) && same == 0xffffffef);
    CHECK(RegOpenKey(0xffffffef, "", NULL) == 87);
    CHECK(!RegCreateKey(0xffffffef, "Software\\Legacy\\Child", &key));
    CHECK(!RegSetValueEx(key, NULL, 0, 1, "legacy", 7) && !RegCloseKey(key));
    CHECK(!RegOpenKey(0xffffffef, "Software\\Legacy\\Child", &key) && !RegCloseKey(key));
    CHECK(!RegCreateKey(0xffffffef, NULL, &same) && same == 0xffffffef);
    CHECK(RegCreateKey(0xffffffef, "Software", NULL) == 87);
    CHECK(!RegCreateKey(0xffffffef, "Software\\Enumeration", &key));
    CHECK(!RegSetValueEx(key, "MixedName", 0, 1, "text", 5));
    char enum_name[32], enum_data[8];
    ULONG name_size = 2, data_size = sizeof enum_data;
    CHECK(RegEnumValue(key, 0, enum_name, &name_size, NULL, &type, enum_data, &data_size) == 234);
    CHECK(name_size == 2 && data_size == 5 && type == 1);
    name_size = sizeof enum_name;
    data_size = 2;
    CHECK(RegEnumValue(key, 0, enum_name, &name_size, NULL, &type, enum_data, &data_size) == 234);
    CHECK(!strcmp(enum_name, "mixedname") && name_size == 9 && data_size == 5);
    name_size = sizeof enum_name;
    data_size = sizeof enum_data;
    CHECK(!RegEnumValue(key, 0, enum_name, &name_size, NULL, &type, enum_data, &data_size));
    CHECK(!strcmp(enum_data, "text"));
    CHECK(RegEnumValue(key, 1, enum_name, &name_size, NULL, NULL, NULL, NULL) == 259);
    CHECK(!RegCloseKey(key));
    CHECK(MulDiv(5, 1, 2) == 3 && MulDiv(-5, 1, 2) == -3);
    CHECK(MulDiv(5, -1, -2) == 3 && MulDiv(1, 1, 0) == -1);
    CHECK(MulDiv(INT32_MAX, INT32_MAX, 1) == -1);
    CHECK(MulDiv(INT32_MIN, 1, 1) == INT32_MIN);
    CHECK(MulDiv(INT32_MIN, INT32_MIN, INT32_MIN) == INT32_MIN);
    WinInitializeOpen32(0);
    WinInitializeOpen32(0);
    WinTerminateOpen32(0);
    WinTerminateOpen32(0);
    WinTerminateOpen32(0);
    puts(
        "profile persistence, binary INI validation, merged queries and typed Open32 values passed");
    return 0;
}
