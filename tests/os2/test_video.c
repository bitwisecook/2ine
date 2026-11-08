/* Public domain. No rights reserved. Wholly AI-authored. */
/* Decode timing/seek, renderer clipping and MCI digital-video lifecycle. */
#include "os2/include/private/export.h"
#include "os2/include/os2me.h"
#include "os2/mm/video/decode/video_decode.h"
#include "os2/mm/mci/mdm/mdm_video.h"
#include "os2/include/pmwin.h"
#include "os2/ps/pmwin/video_frames.h"
#include "host/clock.h"
#include "host/video_frame.h"
#include "host/sync.h"
#include "emulator/audio_mixer.h"
#include "emulator/features.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char *identity(const char *name, uint32 *error)
{
    *error = 0;
    return strdup(name);
}

static void decoder_contracts(const char *path, int kind)
{
    LxVideoInfo info;
    LxVideoDecoder *decoder = lx_video_decoder_open(path, &info);
    CHECK(decoder && info.width == 64 && info.height == 48 && info.duration_ms == 400);
    CHECK(info.rate_numerator == 10 && info.rate_denominator == 1 && !info.audio_present);
    for (int i = 0; i < 4; i++)
    {
        LxDecodedVideoFrame frame;
        CHECK(lx_video_decoder_next(decoder, &frame) == 1);
        CHECK(frame.milliseconds == i * 100 && frame.duration_ms == 100);
        CHECK(frame.image.format == (kind == 1 ? LX_HOST_FRAME_YUV420P : LX_HOST_FRAME_BGRA));
        CHECK(frame.image.planes[0] && frame.image.strides[0] >= 64);
        if (!kind && !i)
            CHECK(frame.image.planes[0][0] == 0 && frame.image.planes[0][1] == 0 &&
                  frame.image.planes[0][2] == 255);
        if (kind == 2 && !i)
            CHECK(frame.image.planes[0][0] < 12 && frame.image.planes[0][1] < 12 &&
                  frame.image.planes[0][2] > 240);
        lx_video_frame_release(&frame);
        CHECK(!frame.owner);
    }
    LxDecodedVideoFrame frame;
    CHECK(!lx_video_decoder_next(decoder, &frame));
    CHECK(lx_video_decoder_seek(decoder, 100));
    CHECK(lx_video_decoder_next(decoder, &frame) == 1 && frame.milliseconds == 100);
    lx_video_frame_release(&frame);
    CHECK(!lx_video_decoder_seek(decoder, -1));
    lx_video_decoder_close(decoder);
    printf("Decoder %s, four timestamps, EOF and seek passed\n", kind == 1 ? "YUV420 planes"
                                                                 : kind == 2
                                                                     ? "Cinepak compressed frames"
                                                                     : "BGRA planes");
}

static unsigned audio_frames;
static double audio_left, audio_right;

static int audio_sink(void *context, const float *samples, unsigned count, int64_t at)
{
    (void)context;
    CHECK(at >= 0 && count <= 480000);
    for (unsigned i = 0; i < count; i++)
    {
        CHECK(isfinite(samples[i * 2]) && isfinite(samples[i * 2 + 1]));
        audio_left += samples[i * 2] * samples[i * 2];
        audio_right += samples[i * 2 + 1] * samples[i * 2 + 1];
    }
    audio_frames += count;
    return 1;
}

static void audio_contracts(const char *path)
{
    LxVideoInfo info;
    LxVideoDecoder *decoder = lx_video_decoder_open(path, &info);
    CHECK(decoder && info.audio_present);
    lx_video_decoder_audio_sink(decoder, audio_sink, NULL);
    LxDecodedVideoFrame frame;
    unsigned frames = 0;
    int result;
    while ((result = lx_video_decoder_next(decoder, &frame)) == 1)
    {
        frames++;
        lx_video_frame_release(&frame);
    }
    CHECK(result == 0 && frames == 4 && audio_frames == 19200);
    CHECK(audio_left > audio_right * 15 && audio_left < audio_right * 17);
    lx_video_decoder_close(decoder);
    printf("Stereo PCM resampled 44100 to 48000: %u frames, L/R energy %.3f/%.3f\n", audio_frames,
           audio_left, audio_right);
}

static void renderer_contracts(const char *path)
{
    int width, height;
    CHECK(lx_host_video_initialize(&width, &height));
    LxHostWindow *resized = lx_host_window_create("Retained resize", 0, 0, 30, 20, NULL);
    CHECK(resized);
    uint32_t initial[30 * 20];
    LxHostRect initial_bounds = {0, 0, 30, 20};
    CHECK(lx_host_window_read(resized, &initial_bounds, initial));
    for (unsigned i = 0; i < 30 * 20; i++)
        CHECK(initial[i] == 0);
    CHECK(lx_host_window_position(resized, 0, 0, 30, 20));
    for (unsigned pass = 0; pass < 3; pass++)
    {
        int w = pass == 1 ? 45 : 100, h = pass == 1 ? 35 : 70;
        CHECK(lx_host_window_position(resized, 0, 0, w, h));
        LxHostRect bounds = {0, 0, w, h};
        CHECK(lx_host_window_fill(resized, &bounds, &bounds, 20, 80, 140));
        for (unsigned present = 0; present < 2; present++)
        {
            CHECK(lx_host_window_present(resized));
            uint8_t red, green, blue;
            CHECK(lx_host_window_pixel(resized, w - 2, h - 2, &red, &green, &blue));
            CHECK(red == 20 && green == 80 && blue == 140);
        }
        if (!lx_host_window_border(resized, pass != 0) && !pass)
            puts("Native border changes unavailable on this SDL backend");
        lx_host_window_show(resized);
        CHECK(lx_host_window_present(resized));
        uint8_t red, green, blue;
        CHECK(lx_host_window_pixel(resized, 2, 2, &red, &green, &blue));
        CHECK(red == 20 && green == 80 && blue == 140);
    }
    lx_host_window_destroy(resized);
    LxHostWindow *window = lx_host_window_create("Video upload", 0, 0, 80, 60, NULL);
    CHECK(window);
    LxHostRect all = {0, 0, 80, 60}, dest = {0, 0, 64, 48}, clip = {4, 5, 20, 18};
    CHECK(lx_host_window_fill(window, &all, &all, 0, 0, 255));
    LxVideoInfo info;
    LxVideoDecoder *decoder = lx_video_decoder_open(path, &info);
    LxDecodedVideoFrame frame;
    CHECK(decoder && lx_video_decoder_next(decoder, &frame) == 1);
    CHECK(lx_host_window_video_frame(window, &frame.image, &dest, &clip, 1));
    uint8_t r, g, b;
    CHECK(lx_host_window_pixel(window, 10, 10, &r, &g, &b) && r > 240 && g < 12 && b < 12);
    CHECK(lx_host_window_pixel(window, 2, 2, &r, &g, &b) && r == 0 && g == 0 && b == 255);
    LxDecodedVideoFrame next;
    CHECK(lx_video_decoder_next(decoder, &next) == 1);
    /* The first decoder frame remains valid after decoding the next frame. */
    uint8_t uv[64 * 24];
    for (unsigned y = 0; y < 24; y++)
        for (unsigned x = 0; x < 32; x++)
        {
            uv[y * 64 + x * 2] = frame.image.planes[1][y * frame.image.strides[1] + x];
            uv[y * 64 + x * 2 + 1] = frame.image.planes[2][y * frame.image.strides[2] + x];
        }
    frame.image.format = LX_HOST_FRAME_NV12;
    frame.image.planes[1] = uv;
    frame.image.strides[1] = 64;
    frame.image.lengths[1] = sizeof uv;
    clip = (LxHostRect){24, 5, 20, 18};
    CHECK(lx_host_window_video_frame(window, &frame.image, &dest, &clip, 1));
    CHECK(lx_host_window_pixel(window, 30, 10, &r, &g, &b) && r > 240 && g < 12 && b < 12);
    CHECK(lx_host_window_video_frame(window, &next.image, &dest, &clip, 1));
    CHECK(lx_host_window_pixel(window, 30, 10, &r, &g, &b) && r < 12 && g > 240 && b < 12);
    lx_video_frame_release(&next);
    frame.image.lengths[0] = 1;
    CHECK(!lx_host_window_video_frame(window, &frame.image, &dest, &clip, 1));
    lx_video_frame_release(&frame);
    lx_video_decoder_close(decoder);
    lx_host_window_destroy(window);
    lx_host_video_quit();
    puts("Retained YUV420/NV12 uploads, decoder lifetime, clipping and stride bounds passed");
}

static ULONG status(USHORT device, unsigned char *p, ULONG item)
{
    memset(p, 0, 32);
    lx_guest_store_u32(p + 8, item);
    CHECK(!mciSendCommand(device, 10, 0x100, p, 0));
    return lx_guest_load_u32(p + 4);
}

typedef struct AudioGate
{
    LxHostMonitor *monitor;
    unsigned entered, release, removing, removed, calls;
} AudioGate;

static int gated_audio(void *context, float *samples, int count, int rate)
{
    (void)samples;
    (void)count;
    (void)rate;
    AudioGate *gate = context;
    lx_host_monitor_lock(gate->monitor);
    gate->entered = 1;
    lx_host_monitor_wake_all(gate->monitor);
    while (!gate->release)
        lx_host_monitor_wait(gate->monitor, UINT32_MAX);
    gate->calls++;
    lx_host_monitor_unlock(gate->monitor);
    return 1;
}

static void *remove_audio(void *context)
{
    AudioGate *gate = context;
    lx_host_monitor_lock(gate->monitor);
    gate->removing = 1;
    lx_host_monitor_wake_all(gate->monitor);
    lx_host_monitor_unlock(gate->monitor);
    lxUnregisterAudioGenerator(gated_audio, gate);
    lx_host_monitor_lock(gate->monitor);
    gate->removed = 1;
    lx_host_monitor_unlock(gate->monitor);
    return NULL;
}

static void audio_lifetime_contracts(void)
{
    AudioGate gate = {.monitor = lx_host_monitor_create()};
    CHECK(gate.monitor && lxRegisterStereoAudioGenerator(gated_audio, &gate, 0));
    lx_host_monitor_lock(gate.monitor);
    uint64_t deadline = lx_host_video_ticks_ms() + 1500;
    while (!gate.entered && lx_host_video_ticks_ms() < deadline)
        lx_host_monitor_wait(gate.monitor, 20);
    CHECK(gate.entered);
    lx_host_monitor_unlock(gate.monitor);
    LxHostThread *thread = lx_host_thread_create(remove_audio, &gate, NULL);
    CHECK(thread);
    lx_host_monitor_lock(gate.monitor);
    while (!gate.removing && lx_host_video_ticks_ms() < deadline)
        lx_host_monitor_wait(gate.monitor, 20);
    CHECK(gate.removing);
    lx_host_monitor_unlock(gate.monitor);
    lx_host_sleep_ms(30);
    lx_host_monitor_lock(gate.monitor);
    CHECK(!gate.removed);
    gate.release = 1;
    lx_host_monitor_wake_all(gate.monitor);
    lx_host_monitor_unlock(gate.monitor);
    lx_host_thread_join(thread, NULL);
    CHECK(gate.removed && gate.calls >= 1);
    unsigned calls = gate.calls;
    lx_host_sleep_ms(50);
    CHECK(gate.calls == calls);
    lx_host_monitor_destroy(gate.monitor);
    puts("Shared mixer unregister waits for an active callback and prevents subsequent calls");
}

static void count_release(void *context)
{
    ++*(unsigned *)context;
}

static void mailbox_contracts(HWND window, const char *path)
{
    void *target = lx_pm_video_target(window);
    LxVideoInfo info;
    LxVideoDecoder *decoder = lx_video_decoder_open(path, &info);
    LxDecodedVideoFrame frame;
    CHECK(target && decoder && lx_video_decoder_next(decoder, &frame) == 1);
    RECTL rect = {0, 0, 64, 48};
    unsigned releases = 0;
    CHECK(lx_pm_video_submit(target, &frame.image, &rect, &releases, count_release));
    CHECK(lx_pm_video_submit(target, &frame.image, &rect, &releases, count_release));
    CHECK(releases == 1);
    lx_pm_video_pump_current();
    CHECK(releases == 2 && lx_pm_video_ready(target));
    frame.image.lengths[0] = 1;
    CHECK(lx_pm_video_submit(target, &frame.image, &rect, &releases, count_release));
    lx_pm_video_pump_current();
    CHECK(releases == 3 && !lx_pm_video_ready(target));
    CHECK(!lx_pm_video_submit(target, &frame.image, &rect, &releases, count_release));
    CHECK(releases == 4);
    lx_pm_video_release(target);
    lx_video_frame_release(&frame);
    lx_video_decoder_close(decoder);
    puts("Frame supersession releases exactly once; failed uploads reject subsequent frames");
}

static void mci_contracts(const char *path)
{
    int pcm_enabled = lxRuntimeFeatureEnabled(LX_FEATURE_PCM);
    HAB hab = WinInitialize(0);
    CHECK(hab);
    HMQ queue = WinCreateMsgQueue(hab, 0);
    CHECK(queue);
    CHECK(WinRegisterClass(hab, "VideoTest", WinDefWindowProc, CS_SIZEREDRAW, 0));
    HWND window = WinCreateWindow(HWND_DESKTOP, "VideoTest", "Video", WS_VISIBLE, 0, 0, 64, 48,
                                  HWND_DESKTOP, HWND_TOP, 1, NULL, NULL);
    CHECK(window);
    mailbox_contracts(window, path);
    LxExecutionServices services = {0};
    char error[256];
    GProcessState.execution = lx_execution_create(
#if TWOINE_PORTABLE_LOADER
        "unicorn",
#else
        "native",
#endif
        &services, error, sizeof error);
    CHECK(GProcessState.execution);
    GProcessState.makeUnixPath = identity;
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, 0x2000, 3, 0);
    CHECK(mapping);
    unsigned char *p = lx_execution_mapping_host(mapping);
    ULONG base = lx_execution_mapping_address(mapping);
    CHECK(strlen(path) < 4096);
    strcpy((char *)p + 1024, path);
    lx_guest_store_u32(p + 8, 12);
    lx_guest_store_u32(p + 12, base + 1024);
    lx_guest_store_u32(p + 20, window);
    CHECK(lxRuntimeConfigureFeature("video", "false") == 1);
    CHECK(mciSendCommand(0, 1, 0x1000 | 0x100 | 0x1000000, p, 0) == 5020);
    CHECK(lxRuntimeConfigureFeature("video", "true") == 1);
    CHECK(lxRuntimeConfigureFeature("pcm", "false") == 1);
    lx_guest_store_u32(p + 8, 9);
    CHECK(mciSendCommand(0, 1, 0x1000, p, 0) == 5020);
    CHECK(lxRuntimeConfigureFeature("pcm", pcm_enabled ? "true" : "false") == 1);
    CHECK(lxRuntimeConfigureFeature("midi", "false") == 1);
    CHECK(mciSendCommand(0, 1, 0x4000, p, 0) == 5020);
    CHECK(lxRuntimeConfigureFeature("midi", "true") == 1);
    lx_guest_store_u32(p + 8, 12);
    CHECK(!mciSendCommand(0, 1, 0x1000 | 0x100 | 0x1000000, p, 0));
    USHORT device = lx_guest_load_u16(p + 4);
    CHECK(device >= 0x8000);
    CHECK(status(device, p, 2) == 400 && status(device, p, 3) == 4);
    if (!pcm_enabled)
        CHECK(!status(device, p, 13));
    memset(p, 0, 32);
    lx_guest_store_u32(p + 8, 10);
    CHECK(!mciSendCommand(device, 9, 0x200, p, 0) && lx_guest_load_u32(p + 4) == 12);
    lx_guest_store_u32(p + 8, 5);
    CHECK(!mciSendCommand(device, 9, 0x200, p, 0) && !lx_guest_load_u32(p + 4));
    lx_guest_store_u16(p + 12, 4);
    CHECK(!mciSendCommand(device, 9, 0x100, p, 0) && lx_guest_load_u32(p + 4) == 1);
    lx_guest_store_u16(p + 12, 11);
    CHECK(!mciSendCommand(device, 9, 0x100, p, 0) && !lx_guest_load_u32(p + 4));
    memset(p, 0, 32);
    lx_guest_store_u32(p + 16, 50);
    CHECK(!mciSendCommand(device, 12, 0x80000, p, 0));
    CHECK(status(device, p, 8) == 50);
    memset(p, 0, 32);
    lx_guest_store_u32(p + 16, 100);
    CHECK(!mciSendCommand(device, 12, 0x80000, p, 0));
    memset(p, 0, 32);
    lx_guest_store_u32(p, window);
    CHECK(!mciSendCommand(device, 4, 1, p, 7));
    QMSG message;
    uint64_t started = lx_host_video_ticks_ms();
    while (lx_host_video_ticks_ms() - started < 80)
    {
        while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
            WinDispatchMsg(hab, &message);
        lx_host_sleep_ms(2);
    }
    CHECK(!mciSendCommand(device, 7, 0, p, 0));
    ULONG paused = status(device, p, 5);
    lx_host_sleep_ms(120);
    CHECK(status(device, p, 5) == paused && status(device, p, 3) == 2);
    CHECK(!mciSendCommand(device, 29, 0, p, 0));
    started = lx_host_video_ticks_ms();
    while (status(device, p, 3) != 4 && lx_host_video_ticks_ms() - started < 2000)
    {
        while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
            WinDispatchMsg(hab, &message);
        lx_host_sleep_ms(2);
    }
    CHECK(status(device, p, 3) == 4);
    CHECK(!mciSendCommand(device, 6, 0, p, 0));
    memset(p, 0, 32);
    CHECK(!mciSendCommand(device, 5, 0x100, p, 0));
    CHECK(status(device, p, 5) == 0);
    memset(p, 0, 32);
    uint64_t begin = lx_host_video_ticks_ms();
    CHECK(!mciSendCommand(device, 4, 2, p, 0));
    CHECK(lx_host_video_ticks_ms() - begin >= 280);
    CHECK(status(device, p, 3) == 4);
    CHECK(!mciSendCommand(device, 2, 0, p, 0));
    memset(p, 0, 32);
    lx_guest_store_u32(p + 8, 12);
    lx_guest_store_u32(p + 12, base + 1024);
    CHECK(!mciSendCommand(0, 1, 0x1000 | 0x100, p, 0));
    device = lx_guest_load_u16(p + 4);
    CHECK(!mciSendCommand(device, 4, 0, p, 0));
    HWND owned = status(device, p, 0x7000);
    CHECK(owned && WinIsWindow(hab, owned));
    lx_host_sleep_ms(25);
    lx_mci_video_shutdown();
    CHECK(!WinIsWindow(hab, owned));
    CHECK(mciSendCommand(device, 2, 0, p, 0) == 5001);
    char command[8192], reply[128];
    CHECK(!mciSendString("open digitalvideo alias sample wait shareable", reply, sizeof reply,
                         window, 0));
    CHECK(*reply);
    USHORT sample = (USHORT)strtoul(reply, NULL, 10);
    CHECK(!mciSendString("status sample ready", reply, sizeof reply, window, 0));
    CHECK(!strcmp(reply, "0"));
    CHECK(mciSendString("play sample wait", reply, sizeof reply, window, 0) == 5020);
    snprintf(command, sizeof command, "window sample handle %u wait", window);
    CHECK(!mciSendString(command, reply, sizeof reply, window, 0));
    snprintf(command, sizeof command, "load sample \"%s\" wait", path);
    CHECK(!mciSendString(command, reply, sizeof reply, window, 0));
    CHECK(!mciSendString("status sample ready", reply, sizeof reply, window, 0));
    CHECK(!strcmp(reply, "1"));
    CHECK(mciSendString("load sample \"/not-present-2ine-video.avi\" wait", reply, sizeof reply,
                        window, 0) != 0);
    CHECK(
        !mciSendString("put sample destination at 3 4 56 40 wait", reply, sizeof reply, window, 0));
    memset(p, 0, 32);
    CHECK(!mciSendCommand(sample, 51, 0x300, p, 0));
    CHECK(lx_guest_load_u32(p + 4) == 3 && lx_guest_load_u32(p + 8) == 4 &&
          lx_guest_load_u32(p + 12) == 56 && lx_guest_load_u32(p + 16) == 40);
    CHECK(!mciSendString("set sample time format frames", reply, sizeof reply, window, 0));
    CHECK(!mciSendString("status sample length", reply, sizeof reply, window, 0));
    CHECK(!strcmp(reply, "4"));
    CHECK(!mciSendString("set sample time format milliseconds", reply, sizeof reply, window, 0));
    CHECK(!mciSendString("play sample from 100 to 300 wait", reply, sizeof reply, window, 0));
    CHECK(!mciSendString("status sample position", reply, sizeof reply, window, 0));
    CHECK(!strcmp(reply, "300"));
    CHECK(!mciSendString("status sample mode", reply, sizeof reply, window, 0));
    CHECK(!strcmp(reply, "stopped"));
    CHECK(mciSendString("play sample reverse wait", reply, sizeof reply, window, 0) != 0);
    CHECK(!mciSendString("close sample", reply, sizeof reply, window, 0));
    HWND transient = WinCreateWindow(HWND_DESKTOP, "VideoTest", "Transient", WS_VISIBLE, 0, 0, 64,
                                     48, HWND_DESKTOP, HWND_TOP, 2, NULL, NULL);
    CHECK(transient);
    memset(p, 0, 32);
    lx_guest_store_u32(p + 8, 12);
    lx_guest_store_u32(p + 12, base + 1024);
    lx_guest_store_u32(p + 20, transient);
    CHECK(!mciSendCommand(0, 1, 0x1000 | 0x100 | 0x1000000, p, 0));
    device = lx_guest_load_u16(p + 4);
    CHECK(!mciSendCommand(device, 4, 0, p, 0));
    CHECK(!mciSendCommand(device, 7, 0, p, 0));
    CHECK(WinDestroyWindow(transient));
    started = lx_host_video_ticks_ms();
    while (status(device, p, 3) != 4 && lx_host_video_ticks_ms() - started < 1000)
        lx_host_sleep_ms(2);
    CHECK(status(device, p, 3) == 4);
    CHECK(!mciSendCommand(device, 2, 0, p, 0));
    lx_execution_unmap(GProcessState.execution, mapping);
    lx_execution_destroy(GProcessState.execution);
    GProcessState.execution = NULL;
    CHECK(WinDestroyWindow(window));
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(hab));
    puts("MCI type12 open, status, asynchronous play, pause/resume, seek, WAIT and close passed");
}

int main(int argc, char **argv)
{
    CHECK(argc == 6);
    decoder_contracts(argv[1], 0);
    decoder_contracts(argv[2], 1);
    decoder_contracts(argv[5], 2);
    renderer_contracts(argv[2]);
    LxVideoInfo info;
    CHECK(!lx_video_decoder_open(argv[4], &info));
    audio_contracts(argv[3]);
    mci_contracts(argv[1]);
    mci_contracts(argv[3]);
    mci_contracts(argv[5]);
    CHECK(lxRuntimeConfigureFeature("pcm", "false") == 1);
    CHECK(lxRuntimeFeatureEnabled(LX_FEATURE_MIDI));
    LxVideoDecoder *silent = lx_video_decoder_open_audio(argv[3], &info, 0);
    CHECK(silent && !info.audio_present);
    lx_video_decoder_close(silent);
    mci_contracts(argv[3]); /* A movie with sound still plays when PCM is disabled. */
    CHECK(lxRuntimeConfigureFeature("pcm", "true") == 1);
    lx_mci_video_shutdown();
    audio_lifetime_contracts();
    lxAudioShutdown();
    return 0;
}
