/* Public domain. No rights reserved. Wholly AI-authored. */
/* The Linux print dialogs pass literal arguments to the native helper and
   interpret its answer, cancellation and failure statuses. */
#define _POSIX_C_SOURCE 200809L
#include "hal/linux/host_dialog.h"
#include "hal/print.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (errno %d)\n", __FILE__, __LINE__, #x, errno);              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char directory[] = "/tmp/2ine-dialog-test-XXXXXX";
static char helper[256], record[256];

/* The fake helper records its arguments, then answers with $ANSWER and $STATUS. */
static void write_helper(void)
{
    snprintf(helper, sizeof helper, "%s/helper", directory);
    snprintf(record, sizeof record, "%s/arguments", directory);
    FILE *file = fopen(helper, "w");
    CHECK(file);
    fprintf(file,
            "#!/bin/sh\n"
            "for argument in \"$@\"; do printf '%%s\\n' \"$argument\"; done > '%s'\n"
            "[ -z \"$ANSWER\" ] || printf '%%s\\n' \"$ANSWER\"\n"
            "exit \"${STATUS:-0}\"\n",
            record);
    CHECK(!fclose(file));
    CHECK(!chmod(helper, 0700));
    CHECK(!setenv("TWOINE_DIALOG", helper, 1));
}

static void answer(const char *value, const char *status)
{
    CHECK(!setenv("ANSWER", value, 1));
    CHECK(!setenv("STATUS", status, 1));
}

static char *recorded(void)
{
    static char text[1024];
    FILE *file = fopen(record, "r");
    CHECK(file);
    size_t length = fread(text, 1, sizeof text - 1, file);
    text[length] = 0;
    fclose(file);
    return text;
}

char **lx_host_print_printers(size_t *count)
{
    char **printers = calloc(2, sizeof *printers);
    CHECK(printers);
    printers[0] = strdup("Office Laser");
    printers[1] = strdup("$(touch pwned)");
    *count = 2;
    return printers;
}

void lx_host_print_destination_clear(LxHostPrintDestination *destination)
{
    free(destination->path);
    free(destination->printer);
    memset(destination, 0, sizeof *destination);
}

int main(void)
{
    CHECK(mkdtemp(directory));
    write_helper();
    unsetenv("TWOINE_PRINT_OUTPUT");
    unsetenv("TWOINE_PRINT_PRINTER");
    unsetenv("TWOINE_PRINT_DEVICE");

    unsigned kind = 0;
    answer("pcl", "0");
    CHECK(lx_host_print_choose_device(&kind) == LX_HOST_SAVE_OK && kind == 2);
    CHECK(!strcmp(recorded(), "print-device\n"));
    answer("", "1");
    CHECK(lx_host_print_choose_device(&kind) == LX_HOST_SAVE_CANCEL);
    answer("", "2");
    CHECK(lx_host_print_choose_device(&kind) == LX_HOST_SAVE_ERROR);
    answer("xps", "0");
    CHECK(lx_host_print_choose_device(&kind) == LX_HOST_SAVE_ERROR);

    char *path = NULL;
    answer("/home/user/Quarterly.pdf", "0");
    CHECK(lx_host_print_save_path("Q1/Report", "pdf", &path) == LX_HOST_SAVE_OK);
    CHECK(!strcmp(path, "/home/user/Quarterly.pdf"));
    CHECK(!strcmp(recorded(), "save\nSave Print Output\nQ1/Report\npdf\n"));
    free(path);
    answer("relative.pdf", "0");
    CHECK(lx_host_print_save_path("Report", "pdf", &path) == LX_HOST_SAVE_ERROR && !path);

    /* Printer names are literal arguments; only listed printers are accepted. */
    LxHostPrintDestination destination;
    answer("$(touch pwned)", "0");
    CHECK(lx_host_print_choose_pcl("Job", &destination) == LX_HOST_SAVE_OK);
    CHECK(!strcmp(destination.printer, "$(touch pwned)"));
    CHECK(!strcmp(recorded(), "print-pcl\nOffice Laser\n$(touch pwned)\n"));
    CHECK(access("pwned", F_OK) != 0);
    lx_host_print_destination_clear(&destination);
    answer("Unlisted", "0");
    CHECK(lx_host_print_choose_pcl("Job", &destination) == LX_HOST_SAVE_ERROR);
    lx_host_print_destination_clear(&destination);

    const char *fatal[] = {"fatal", "Word Pro", "Stopped", "", NULL};
    answer("", "0");
    CHECK(lx_host_dialog_run(fatal, NULL) == LX_HOST_SAVE_OK);
    CHECK(!strcmp(recorded(), "fatal\nWord Pro\nStopped\n\n"));

    CHECK(!setenv("TWOINE_DIALOG", "/nonexistent/2ine-dialog", 1));
    CHECK(lx_host_dialog_run(fatal, NULL) == LX_HOST_SAVE_ERROR);

    unlink(record);
    unlink(helper);
    rmdir(directory);
    puts("host dialog protocol passed");
    return 0;
}
