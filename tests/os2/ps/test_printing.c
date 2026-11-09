/* Public domain. No rights reserved. Wholly AI-authored. */
/* Typed printer contexts, page boundaries, cancellation and actual output. */
#include "os2/include/private/export.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/pmspl.h"
#include "os2/include/private/pmp.h"
#include "os2/include/private/pmgrep.h"
#include "host/print.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(test)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(test))                                                                               \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void store_environment(const char *path)
{
#ifdef _WIN32
    CHECK(_putenv_s("TWOINE_PRINT_OUTPUT", path) == 0);
#else
    CHECK(setenv("TWOINE_PRINT_OUTPUT", path, 1) == 0);
#endif
}

static void draw_page(HPS ps, LONG color)
{
    CHECK(GpiSetColor(ps, color));
    POINTL origin = {100, 100}, end = {400, 300};
    CHECK(GpiMove(ps, &origin) && GpiBox(ps, 1, &end, 0, 0));
    uint32_t pixel = 0;
    CHECK(lx_pm_raster_read(ps, 150, 150, 1, 1, &pixel));
    CHECK(pixel == (color == CLR_RED ? 0xff0000 : 0x0000ff));
    CHECK(GpiSetColor(ps, CLR_BLACK));
    POINTL text = {100, 400};
    CHECK(GpiCharStringAt(ps, &text, 15, "2ine Print Test") > 0);
}

static void check_default_queue(int expected)
{
    const ULONG levels[] = {3, 4, 6};
    for (unsigned i = 0; i < sizeof levels / sizeof levels[0]; i++)
    {
        unsigned char buffer[512];
        ULONG returned, total, needed;
        CHECK(SplEnumQueue(NULL, levels[i], buffer, sizeof buffer, &returned, &total, &needed,
                           NULL) == 0);
        CHECK(returned == 2 && total == 2);
        size_t record = levels[i] == 6 ? 52 : 44;
        CHECK(buffer[10] == (expected == 0 ? 4 : 0) && buffer[11] == 0);
        CHECK(buffer[record + 10] == (expected == 1 ? 4 : 0) && buffer[record + 11] == 0);
        for (int queue = 0; queue < 2; queue++)
        {
            CHECK(SplQueryQueue(NULL, queue ? "2INEPCL" : "2INEPDF", levels[i], buffer,
                                sizeof buffer, &needed) == 0);
            CHECK(buffer[10] == (queue == expected ? 4 : 0) && buffer[11] == 0);
        }
    }
}

static int file_present(const char *path)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (file)
        fclose(file);
    return file != NULL;
}

static void check_profile_default(const char *directory)
{
    const char *user_path = getenv("TWOINE_USER_PROFILE"),
               *system_path = getenv("TWOINE_SYSTEM_PROFILE");
    int user_present = file_present(user_path), system_present = file_present(system_path);
    char value[128];
    CHECK(PrfQueryProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", NULL, value,
                                sizeof value) == 8);
    CHECK(!strcmp(value, "2INEPDF;"));
    check_default_queue(0);
    CHECK(file_present(user_path) == user_present && file_present(system_path) == system_present);
    char private_path[4096];
    CHECK(snprintf(private_path, sizeof private_path, "%s/private-printer.profile", directory) <
          (int)sizeof private_path);
    HINI private_profile = lx_profile_open_host(private_path);
    CHECK(private_profile);
    CHECK(PrfQueryProfileString(private_profile, "PM_SPOOLER", "QUEUE", "private", value,
                                sizeof value) == 7);
    CHECK(!strcmp(value, "private") && !file_present(private_path));
    CHECK(PrfWriteProfileString(private_profile, "PM_SPOOLER", "QUEUE", "Private;"));
    CHECK(PrfQueryProfileString(private_profile, "PM_SPOOLER", "QUEUE", NULL, value,
                                sizeof value) == 8);
    CHECK(!strcmp(value, "Private;"));
    check_default_queue(0);
    CHECK(PrfCloseProfile(private_profile));
    const char *aliases[] = {user_path, system_path};
    for (unsigned i = 0; i < sizeof aliases / sizeof aliases[0]; i++)
    {
        if (!aliases[i])
            continue;
        HINI same_file = lx_profile_open_host(aliases[i]);
        CHECK(same_file);
        CHECK(PrfQueryProfileString(same_file, "PM_SPOOLER", "QUEUE", "private", value,
                                    sizeof value) == 7);
        CHECK(!strcmp(value, "private"));
        CHECK(PrfCloseProfile(same_file));
    }
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", "2INEPCL;"));
    CHECK(PrfQueryProfileString(HINI_USERPROFILE, "PM_SPOOLER", "PRINTER", NULL, value,
                                sizeof value) == 8);
    CHECK(!strcmp(value, "2INEPCL;"));
    check_default_queue(1);
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", ""));
    check_default_queue(-1);
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", "Office;"));
    check_default_queue(-1);
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", NULL));
    CHECK(PrfWriteProfileString(HINI_SYSTEMPROFILE, "PM_SPOOLER", "QUEUE", "2INEPCL;"));
    CHECK(PrfQueryProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", NULL, value,
                                sizeof value) == 8);
    CHECK(!strcmp(value, "2INEPCL;"));
    check_default_queue(1);
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", "2INEPDF;"));
    check_default_queue(0);
    CHECK(PrfWriteProfileString(HINI_USERPROFILE, "PM_SPOOLER", "QUEUE", NULL));
    CHECK(PrfWriteProfileString(HINI_SYSTEMPROFILE, "PM_SPOOLER", "QUEUE", NULL));
    check_default_queue(0);
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "--spool"))
    {
        FILE *source = fopen(argv[3], "rb");
        CHECK(source);
        int accepted = lx_host_print_submit_helper(argv[2], "Test;$(should_not_run)",
                                                   "Print \"quoted\" $(touch ignored)", 1, source);
        CHECK(accepted == atoi(argv[4]));
        fclose(source);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--cups"))
    {
        size_t count = 0;
        char **printers = lx_host_print_printers(&count);
        CHECK(printers && count == 1 && !strcmp(printers[0], "Fixture_Printer"));
        free(printers[0]);
        free(printers);
        FILE *source = fopen(argv[2], "rb");
        CHECK(source &&
              lx_host_print_submit_pcl("Fixture_Printer", "Packaged printing", 1, source));
        fclose(source);
        return 0;
    }
    CHECK(argc == 2);
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    char queues[26];
    ULONG returned = 99, total = 99, needed = 99;
    CHECK(SplEnumQueue(NULL, 0, NULL, 0, &returned, &total, &needed, NULL) == 234);
    CHECK(!returned && total == 2 && needed == 26);
    CHECK(SplEnumQueue(NULL, 0, queues, sizeof queues, &returned, &total, &needed, NULL) == 0);
    CHECK(returned == 2 && !strcmp(queues, "2INEPDF") && !strcmp(queues + 13, "2INEPCL"));
    char profile[128];
    CHECK(PrfQueryProfileString(HINI_PROFILE, "PM_SPOOLER", "QUEUE", NULL, profile,
                                sizeof profile) == 8);
    CHECK(!strcmp(profile, "2INEPDF;"));
    CHECK(PrfQueryProfileSize(HINI_SYSTEMPROFILE, "PM_SPOOLER_QUEUE_DD", NULL, &needed));
    CHECK(needed == 17);
    CHECK(PrfQueryProfileString(HINI_SYSTEMPROFILE, "PM_SPOOLER_QUEUE_DD", NULL, NULL, profile,
                                sizeof profile) == 16);
    CHECK(!strcmp(profile, "2INEPDF") && !strcmp(profile + 8, "2INEPCL"));
    check_profile_default(argv[1]);
    for (unsigned kind = 0; kind < 2; kind++)
    {
        char filename[4096];
        CHECK(snprintf(filename, sizeof filename, "%s/printing.%s", argv[1], kind ? "pcl" : "pdf") <
              (int)sizeof filename);
        store_environment(filename);
        PSZ driver = kind ? "2INEPCL" : "2INEPDF";
        PSZ fields[4] = {driver, driver, NULL, "PM_Q_STD"};
        HDC dc = DevOpenDC(anchor, 2, "*", 4, fields, 0);
        CHECK(dc);
        LONG caps[42];
        CHECK(DevQueryCaps(dc, 0, 42, caps));
        CHECK(caps[2] == 3 && caps[4] == 2480 && caps[5] == 3508 && caps[37] == 300);
        unsigned char form[72];
        memset(form, 0xcc, sizeof form);
        CHECK(DevQueryHardcopyCaps(dc, 0, 0, NULL) == 1);
        CHECK(DevQueryHardcopyCaps(dc, 0, 1, form) == 1 && !strcmp((char *)form, "A4"));
        CHECK(form[68] == 0xcc);
        SIZEL size = {0, 0};
        HPS ps = GpiCreatePS(anchor, dc, &size, PU_PELS | GPIA_ASSOC);
        CHECK(ps && DevCloseDC(dc) == (ULONG)-1);
        FONTMETRICS metrics;
        CHECK(GpiQueryFontMetrics(ps, sizeof metrics, &metrics));
        CHECK(metrics.sXDeviceRes == 300 && metrics.sYDeviceRes == 300 &&
              metrics.sNominalPointSize >= 100 && metrics.sNominalPointSize <= 140);
        CHECK(GpiSetBitmap(ps, 0) == (ULONG)-1);
        CHECK(DevEscape(dc, 8151, 0, NULL, NULL, NULL) == -1);
        CHECK(DevEscape(dc, 8150, 11, "Print Test", NULL, NULL) == 1);
        CHECK(DevEscape(dc, 8150, 11, "Print Test", NULL, NULL) == -1);
        draw_page(ps, CLR_RED);
        CHECK(DevEscape(dc, 16300, 0, NULL, NULL, NULL) == 1);
        uint32_t pixel = 0;
        CHECK(lx_pm_raster_read(ps, 150, 150, 1, 1, &pixel) && pixel == 0xffffff);
        draw_page(ps, CLR_BLUE);
        CHECK(DevEscape(dc, 8151, 0, NULL, NULL, NULL) == 1);
        FILE *file = fopen(filename, "rb");
        CHECK(file);
        char header[8];
        CHECK(fread(header, 1, sizeof header, file) == sizeof header);
        CHECK(kind ? !memcmp(header, "\033%-12345", 8) : !memcmp(header, "%PDF-1.4", 8));
        fclose(file);
        store_environment("");
        CHECK(DevEscape(dc, 8150, 11, "Cancelled!", NULL, NULL) == 0);
        store_environment(filename);
        CHECK(DevEscape(dc, 8150, 11, "Abort Test", NULL, NULL) == 1);
        CHECK(DevEscape(dc, 8153, 0, NULL, NULL, NULL) == 1);
        CHECK(DevEscape(dc, 8151, 0, NULL, NULL, NULL) == -1);
        CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0);
    }
    CHECK(WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    puts("Printer queue, drawing, two-page PDF/PCL, cancellation and abort contracts passed");
    return 0;
}
