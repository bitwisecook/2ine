# Public domain. No rights reserved. Wholly AI-authored.
"""Execute the shipped Doom clock and audio-shutdown patch contracts."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class DoomClockTests(unittest.TestCase):
    def test_patched_clock_rollover_and_concurrent_callers(self):
        root = Path(__file__).resolve().parents[2]
        patch = (root / "research/patches/doom110-clock.patch").read_text()
        additions = [line[1:] for line in patch.splitlines() if line.startswith("+")]
        first = additions.index("    ULONG milliseconds;")
        last = additions.index("    return tics;", first)
        body = "\n".join(additions[first : last + 1]).replace(
            "unsigned __int64", "unsigned long long"
        )
        source = (
            r"""
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t ULONG;
#define TICRATE 35
#define QSV_MS_COUNT 14
#define NO_ERROR 0
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); exit(1); } } while (0)
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t now;
static int failing, automatic;
static int DosEnterCritSec(void) { return pthread_mutex_lock(&mutex); }
static int DosExitCritSec(void) { return pthread_mutex_unlock(&mutex); }
static int DosQuerySysInfo(int first, int last, ULONG *output, unsigned long size)
{
    CHECK(first == QSV_MS_COUNT && last == QSV_MS_COUNT && size == sizeof *output);
    if (failing) return 1;
    if (automatic) now++;
    *output = now;
    return 0;
}
int I_GetTime(void) {
"""
            + body
            + r"""
}
static void *caller(void *unused)
{
    (void)unused;
    int previous = 0;
    for (unsigned i = 0; i < 10000; i++) {
        int current = I_GetTime();
        CHECK(current >= previous);
        previous = current;
    }
    return NULL;
}
int main(void)
{
    now = 0xfffff000u;
    CHECK(I_GetTime() == 0);
    now += 59999u;
    CHECK(I_GetTime() == 2099);
    now++;
    CHECK(I_GetTime() == 2100);
    now += 1000u;
    CHECK(I_GetTime() == 2135);
    failing = 1;
    now += 100u;
    CHECK(I_GetTime() == 2135);
    failing = 0;
    CHECK(I_GetTime() == 2138);
    automatic = 1;
    int before = I_GetTime();
    pthread_t threads[8];
    for (unsigned i = 0; i < 8; i++) CHECK(!pthread_create(threads + i, NULL, caller, NULL));
    for (unsigned i = 0; i < 8; i++) CHECK(!pthread_join(threads[i], NULL));
    int elapsed = I_GetTime() - before;
    CHECK(elapsed >= 2800 && elapsed <= 2801);
    puts("patched clock minute/wrap/concurrency checks passed");
    return 0;
}
"""
        )
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a C compiler is required for the clock contract")
        with tempfile.TemporaryDirectory(prefix="twoine-clock-") as temporary:
            directory = Path(temporary)
            (directory / "clock.c").write_text(source)
            subprocess.run(
                [
                    str(compiler),
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    str(directory / "clock.c"),
                    "-o",
                    str(directory / "clock"),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(directory / "clock")], check=True, capture_output=True, text=True, timeout=20
            )
            self.assertIn("minute/wrap/concurrency checks passed", result.stdout)

    def test_patched_audio_shutdown_waits_before_free_and_is_idempotent(self):
        root = Path(__file__).resolve().parents[2]
        patch = (root / "research/patches/doom110-clock.patch").read_text()
        section = patch.split("+++ b/audio.c\n", 1)[1]
        patched = "\n".join(
            line[1:]
            for line in section.splitlines()
            if line.startswith(("+", " ")) and not line.startswith("+++")
        )
        first = patched.index("void audio_close()")
        last = patched.index("\nvoid audio_channel_parms", first)
        body = patched[first:last]
        source = (
            r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
typedef unsigned long ULONG;
typedef void *PVOID;
typedef struct { int unused; } MCI_GENERIC_PARMS;
#define MCIERR_SUCCESS 0
#define MCI_STOP 1
#define MCI_BUFFER 2
#define MCI_CLOSE 3
#define MCI_WAIT 4
#define MCI_DEALLOCATE_MEMORY 8
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); exit(1); } } while (0)
static short *prebuffer;
static unsigned usDeviceID = 17;
static MCI_GENERIC_PARMS buffer_parms;
static int active, pending, failure, frees;
static char order[32];
static unsigned calls;
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int block_stop, stop_entered, allow_stop;
static void DosEnterCritSec(void) { CHECK(!pthread_mutex_lock(&critical)); }
static void DosExitCritSec(void) { CHECK(!pthread_mutex_unlock(&critical)); }
static ULONG mciSendCommand(unsigned id, unsigned command, unsigned flags, PVOID parameters,
                            unsigned user)
{
    CHECK(id == 17 && parameters && !user && calls < sizeof order - 1);
    order[calls++] = command == MCI_STOP ? 'S' : command == MCI_BUFFER ? 'B' : 'C';
    if (command == MCI_STOP) {
        CHECK(flags == MCI_WAIT);
        /* An in-flight completion can touch the work buffer until STOP returns. */
        CHECK(prebuffer);
        prebuffer[0] = 123;
        CHECK(!pthread_mutex_lock(&gate));
        if (block_stop) {
            stop_entered = 1;
            CHECK(!pthread_cond_broadcast(&condition));
            while (!allow_stop) CHECK(!pthread_cond_wait(&condition, &gate));
        }
        CHECK(!pthread_mutex_unlock(&gate));
        if (failure == MCI_STOP) return 1;
        active = 0;
    } else if (command == MCI_BUFFER) {
        CHECK(flags == (MCI_WAIT | MCI_DEALLOCATE_MEMORY) && !active);
        if (failure == MCI_BUFFER) return 1;
        pending = 0;
    } else {
        CHECK(command == MCI_CLOSE && flags == MCI_WAIT && !active && !pending);
        if (failure == MCI_CLOSE) return 1;
    }
    return 0;
}
static void release_buffer(void *buffer)
{
    CHECK(buffer == prebuffer && !active && !pending && !frees);
    order[calls++] = 'F';
    frees++;
    free(buffer);
}
#define free release_buffer
"""
            + body
            + r"""
#undef free
static void begin(int fail)
{
    prebuffer = malloc(32);
    CHECK(prebuffer);
    active = pending = 1;
    failure = fail;
    frees = 0;
    calls = 0;
    memset(order, 0, sizeof order);
}
static void *close_from_thread(void *unused)
{
    (void)unused;
    audio_close();
    return NULL;
}
int main(void)
{
    audio_close();
    CHECK(!calls && !frees);
    begin(0);
    audio_close();
    CHECK(!strcmp(order, "SBCF") && !prebuffer && frees == 1);
    audio_close();
    CHECK(!strcmp(order, "SBCF") && frees == 1);
    begin(MCI_STOP);
    audio_close();
    CHECK(!strcmp(order, "S") && prebuffer && !frees && active);
    failure = 0;
    audio_close();
    CHECK(!strcmp(order, "SSBCF") && !prebuffer && frees == 1);
    begin(MCI_BUFFER);
    audio_close();
    CHECK(!strcmp(order, "SB") && prebuffer && !frees && pending);
    failure = 0;
    audio_close();
    CHECK(!strcmp(order, "SBSBCF") && !prebuffer && frees == 1);
    begin(MCI_CLOSE);
    audio_close();
    CHECK(!strcmp(order, "SBCF") && !prebuffer && frees == 1);
    audio_close();
    CHECK(!strcmp(order, "SBCF") && frees == 1);
    begin(0);
    block_stop = 1;
    pthread_t owner, concurrent[4];
    CHECK(!pthread_create(&owner, NULL, close_from_thread, NULL));
    CHECK(!pthread_mutex_lock(&gate));
    while (!stop_entered) CHECK(!pthread_cond_wait(&condition, &gate));
    CHECK(!pthread_mutex_unlock(&gate));
    for (unsigned i = 0; i < 4; i++)
        CHECK(!pthread_create(concurrent + i, NULL, close_from_thread, NULL));
    for (unsigned i = 0; i < 4; i++) CHECK(!pthread_join(concurrent[i], NULL));
    CHECK(!strcmp(order, "S") && prebuffer && !frees && active);
    CHECK(!pthread_mutex_lock(&gate));
    allow_stop = 1;
    CHECK(!pthread_cond_broadcast(&condition));
    CHECK(!pthread_mutex_unlock(&gate));
    CHECK(!pthread_join(owner, NULL));
    CHECK(!strcmp(order, "SBCF") && !prebuffer && frees == 1);
    puts("patched audio stop-before-free, retry and repeated-close checks passed");
    return 0;
}
"""
        )
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a C compiler is required for the audio shutdown contract")
        with tempfile.TemporaryDirectory(prefix="twoine-doom-audio-") as temporary:
            directory = Path(temporary)
            (directory / "audio.c").write_text(source)
            subprocess.run(
                [
                    str(compiler),
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    str(directory / "audio.c"),
                    "-o",
                    str(directory / "audio"),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(directory / "audio")], check=True, capture_output=True, text=True, timeout=20
            )
            self.assertIn("stop-before-free, retry and repeated-close checks passed", result.stdout)

    def test_patched_large_save_owns_storage_and_preserves_screen(self):
        root = Path(__file__).resolve().parents[2]
        patch = (root / "research/patches/doom110-clock.patch").read_text()
        section = patch.rsplit("+++ b/g_game.c\n", 1)[1]
        patched = "\n".join(
            line[1:]
            for line in section.splitlines()
            if line.startswith(("+", " ")) and not line.startswith("+++")
        )
        first = patched.index("void G_DoSaveGame (void)")
        last = patched.index("\n//\n// G_InitNew", first)
        body = patched[first:last]
        source = (
            r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
#define SAVEGAMESIZE 0x2c000
#define SAVESTRINGSIZE 24
#define VERSIONSIZE 16
#define DOOM_VERSION 109
#define MAXPLAYERS 4
#define SAVEGAMENAME "doomsav"
#define PU_STATIC 1
#define GGSAVED "game saved"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); exit(1); } } while (0)
static byte *save_p, *savebuffer, *screens[5], *allocation;
static char savedescription[24] = "LARGE SAVE";
static int savegameslot = 3, gameskill = 1, gameepisode = 1, gamemap = 3;
static int playeringame[4] = {1, 0, 0, 0}, leveltime = 0x123456;
static int gameaction, consoleplayer, ga_nothing;
static struct { const char *message; } players[4];
static unsigned writes, releases, fills, thinker_bytes = 32768;
static int M_CheckParm(const char *argument) { CHECK(!strcmp(argument, "-cdrom")); return 0; }
static byte *Z_Malloc(unsigned size, int tag, void *owner)
{
    CHECK(size == SAVEGAMESIZE && tag == PU_STATIC && !owner && !allocation);
    allocation = malloc(size + 32);
    CHECK(allocation);
    memset(allocation, 0xa5, size + 32);
    return allocation + 16;
}
static void Z_Free(void *buffer)
{
    CHECK(buffer == allocation + 16 && writes == releases + 1);
    for (unsigned i = 0; i < 16; i++)
        CHECK(allocation[i] == 0xa5 && allocation[SAVEGAMESIZE + 16 + i] == 0xa5);
    releases++;
    free(allocation);
    allocation = NULL;
}
static void archive(unsigned count, unsigned value)
{
    save_p += (4 - ((uintptr_t)save_p & 3)) & 3;
    CHECK(save_p >= savebuffer && save_p + count < savebuffer + SAVEGAMESIZE);
    memset(save_p, value, count);
    save_p += count;
}
static void P_ArchivePlayers(void) { archive(512, 0x11); }
static void P_ArchiveWorld(void) { archive(48000, 0x22); }
static void P_ArchiveThinkers(void) { archive(thinker_bytes, 0x33); }
static void P_ArchiveSpecials(void) { archive(4096, 0x44); }
static void I_Error(const char *message) { fprintf(stderr, "%s\n", message); exit(2); }
static void M_WriteFile(const char *name, byte *buffer, int length)
{
    CHECK(!strcmp(name, "doomsav3.dsg") && buffer == allocation + 16);
    CHECK(length == (int)(52 + 512 + 48000 + thinker_bytes + 4096 + 1));
    CHECK(length > 81406 && length <= SAVEGAMESIZE);
    CHECK(!memcmp(buffer, "LARGE SAVE", 10) && !memcmp(buffer + 24, "version 109", 11));
    CHECK(buffer[40] == 1 && buffer[41] == 1 && buffer[42] == 3 && buffer[43] == 1);
    CHECK(buffer[47] == 0x12 && buffer[48] == 0x34 && buffer[49] == 0x56);
    CHECK(buffer[50] == 0xa5 && buffer[51] == 0xa5);
    for (unsigned i = 52; i < 564; i++) CHECK(buffer[i] == 0x11);
    for (unsigned i = 564; i < 48564; i++) CHECK(buffer[i] == 0x22);
    for (unsigned i = 48564; i < 48564 + thinker_bytes; i++) CHECK(buffer[i] == 0x33);
    for (unsigned i = 48564 + thinker_bytes; i < (unsigned)length - 1; i++)
        CHECK(buffer[i] == 0x44);
    CHECK(buffer[length - 1] == 0x1d);
    writes++;
}
static void R_FillBackScreen(void)
{
    CHECK(!allocation && !savebuffer && !save_p && releases == writes);
    for (unsigned i = 0; i < 5; i++)
        for (unsigned j = 0; j < 64000; j++) CHECK(screens[i][j] == 0x6b);
    fills++;
}
"""
            + body
            + r"""
int main(void)
{
    for (unsigned i = 0; i < 5; i++) {
        screens[i] = malloc(64000);
        CHECK(screens[i]);
        memset(screens[i], 0x6b, 64000);
    }
    G_DoSaveGame();
    CHECK(writes == 1 && releases == 1 && fills == 1 && !savedescription[0]);
    strcpy(savedescription, "LARGE SAVE");
    thinker_bytes = SAVEGAMESIZE - (52 + 512 + 48000 + 4096 + 4);
    G_DoSaveGame();
    CHECK(writes == 2 && releases == 2 && fills == 2 && !savedescription[0]);
    CHECK(players[0].message && !strcmp(players[0].message, GGSAVED));
    for (unsigned i = 0; i < 5; i++) free(screens[i]);
    puts("patched large-save ownership, format, capacity and screen-canary checks passed");
    return 0;
}
"""
        )
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a C compiler is required for the large-save contract")
        with tempfile.TemporaryDirectory(prefix="twoine-doom-save-") as temporary:
            directory = Path(temporary)
            (directory / "save.c").write_text(source)
            subprocess.run(
                [
                    str(compiler),
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(directory / "save.c"),
                    "-o",
                    str(directory / "save"),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(directory / "save")], check=True, capture_output=True, text=True, timeout=20
            )
            self.assertIn("capacity and screen-canary checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
