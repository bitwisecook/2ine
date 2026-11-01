/* Public domain. No rights reserved. Wholly AI-authored. */
/* Per-user audio preferences preserve paths with spaces and bounded input. */
#define _POSIX_C_SOURCE 200809L
#include "os2libs/midi_config.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    char filename[] = "/tmp/twoine-audio-config-XXXXXX";
    int fd = mkstemp(filename);
    CHECK(fd >= 0);
    FILE *stream = fdopen(fd, "w");
    CHECK(stream);
    CHECK(
        fputs(
            "schema=1\r\nsoundfont=/first.sf2\r\n# comment\nunknown=keep\nsoundfont=/bank path/GeneralUser.sf2\nmidi_backend=gm\n",
            stream) >= 0);
    CHECK(!fclose(stream));
    CHECK(!setenv("TWOINE_AUDIO_CONFIG", filename, 1));
    char *value = lx_midi_config_value("soundfont");
    CHECK(value && !strcmp(value, "/bank path/GeneralUser.sf2"));
    free(value);
    value = lx_midi_config_value("midi_backend");
    CHECK(value && !strcmp(value, "gm"));
    free(value);
    CHECK(!lx_midi_config_value("missing"));
    stream = fopen(filename, "w");
    CHECK(stream);
    for (unsigned i = 0; i < 20000; i++)
        CHECK(fputc('x', stream) != EOF);
    CHECK(!fclose(stream));
    CHECK(!lx_midi_config_value("soundfont"));
    CHECK(!unlink(filename));
    CHECK(!lx_midi_config_value("soundfont"));
    unsetenv("TWOINE_AUDIO_CONFIG");
    puts("Audio configuration selection, spaces, CRLF and bounded snapshots passed");
    return 0;
}
