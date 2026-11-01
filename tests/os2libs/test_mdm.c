/* Public domain. No rights reserved. Wholly AI-authored. */
/* PCM completion, memory-file MIDI ownership and invalid-device contracts. */
#include "os2libs/os2native.h"
#include "os2libs/mdm.h"
#include "os2libs/mmio.h"
#include "os2libs/midi_sequence.h"
#include "hal/host.h"
#include <math.h>
#include <errno.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#if !TWOINE_PORTABLE_LOADER
static unsigned completions;
#endif
static void init_tib(uint8 *p, void *stack, size_t size, uint32 id)
{
    (void)p;
    (void)stack;
    (void)size;
    (void)id;
}
static uint16 attach_tib(uint8 *p)
{
    (void)p;
    return 0;
}
static void detach_tib(uint16 token)
{
    (void)token;
}
#if !TWOINE_PORTABLE_LOADER
static ULONG OS2API completed(ULONG status, PVOID record, ULONG flags)
{
    CHECK(!status && record && flags == 2);
    __atomic_add_fetch(&completions, 1, __ATOMIC_RELEASE);
    return 1;
}
#endif
static void put_big32(unsigned char *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}
static int parse_track(LxMidiSequence *sequence, const unsigned char *track, size_t size)
{
    CHECK(size <= UINT32_MAX && size <= SIZE_MAX - 22);
    unsigned char *file = calloc(1, size + 22);
    CHECK(file);
    memcpy(file, "MThd", 4);
    file[7] = 6;
    file[11] = 1;
    file[13] = 96;
    memcpy(file + 14, "MTrk", 4);
    put_big32(file + 18, (uint32_t)size);
    memcpy(file + 22, track, size);
    int result = lx_midi_sequence_parse(sequence, file, size + 22);
    /* Parsed SysEx messages must outlive their original media buffer. */
    memset(file, 0xcc, size + 22);
    free(file);
    return result;
}
static void midi_sequence_contracts(void)
{
    const unsigned char track[] = {
        0,    0xff, 0x51, 3,    0x07, 0xa1, 0x20, 0,    0xc1, 10,   0,    0xf0, 4,    0x41, 0x10,
        0x16, 0x12, 48,   0xff, 0x51, 3,    0x03, 0xd0, 0x90, 48,   0xf7, 4,    0x20, 0,    0x5a,
        0xf7, 0,    0x91, 60,   100,  0,    0xf7, 13,   0xf0, 0x7e, 0x7f, 9,    1,    0xf7, 0xf0,
        0x41, 0x10, 0x16, 0x12, 1,    0xf7, 96,   0x81, 60,   0,    0,    0xff, 0x2f, 0};
    const unsigned char continued[] = {0xf0, 0x41, 0x10, 0x16, 0x12, 0x20, 0, 0x5a, 0xf7};
    const unsigned char escaped1[] = {0xf0, 0x7e, 0x7f, 9, 1, 0xf7};
    const unsigned char escaped2[] = {0xf0, 0x41, 0x10, 0x16, 0x12, 1, 0xf7};
    LxMidiSequence sequence;
    CHECK(parse_track(&sequence, track, sizeof track));
    CHECK(sequence.count == 8);
    for (unsigned i = 0; i < 8; i++)
        CHECK(sequence.events[i].order == i);
    CHECK(sequence.events[0].tempo == 500000 && sequence.events[0].time == 0);
    CHECK(sequence.events[1].status == 0xc1 && sequence.events[1].key == 10);
    CHECK(sequence.events[2].tempo == 250000 && sequence.events[2].tick == 48);
    CHECK(fabs(sequence.events[2].time - 0.25) < 0.000001);
    CHECK(sequence.events[3].tick == 96 && sequence.events[3].sysex_size == sizeof continued);
    CHECK(!memcmp(sequence.events[3].sysex, continued, sizeof continued));
    CHECK(fabs(sequence.events[3].time - 0.375) < 0.000001);
    CHECK(sequence.events[4].status == 0x91 && sequence.events[4].key == 60 &&
          sequence.events[4].value == 100);
    CHECK(sequence.events[5].sysex_size == sizeof escaped1 &&
          !memcmp(sequence.events[5].sysex, escaped1, sizeof escaped1));
    CHECK(sequence.events[6].sysex_size == sizeof escaped2 &&
          !memcmp(sequence.events[6].sysex, escaped2, sizeof escaped2));
    CHECK(sequence.events[7].status == 0x81 && sequence.events[7].tick == 192);
    CHECK(fabs(sequence.events[7].time - 0.625) < 0.000001);
    CHECK(fabs(sequence.duration - 0.725) < 0.000001);
    lx_midi_sequence_clear(&sequence);
    CHECK(!sequence.events && !sequence.count);
    const unsigned char bad[][18] = {
        {0, 0xf0, 2, 0x41, 0x10, 0, 0xff, 0x2f, 0}, /* Missing final continuation. */
        {0, 0xf0, 5, 0x41, 0x10},                   /* Declared packet extends beyond the track. */
        {0, 0xf0, 1, 0x41, 0, 0xf0, 2, 1, 0xf7},    /* Nested F0 packet. */
        {0, 0xf0, 1, 0x41, 0, 0x90, 60, 1},         /* A voice message interrupts a SysEx. */
        {0, 0xf7, 2, 0xf3, 1},                      /* Unsupported raw Song Select escape. */
        {0, 0xf7, 2, 0xf0, 0x41},                   /* Escaped SysEx lacks its terminator. */
        {0, 0xf0, 3, 0x41, 0x90, 0xf7},             /* A channel status inside the payload. */
        {0, 0x90, 60, 1, 0, 0xf0, 2, 0x41, 0xf7, 0, 61, 1}, /* SysEx cancels running status. */
        {0, 0x90, 60, 1, 0, 0xff, 1, 0, 0, 61, 1},          /* Meta cancels running status too. */
        {0, 0xf0, 0x81, 0x80, 0x80, 0x80, 0x00}, /* Packet length VLQ exceeds four bytes. */
        {0, 0xf0, 2, 0x41, 0xf7, 0, 0xf7, 2, 0x01, 0x02}, /* Failure frees an earlier message. */
        {0, 0xff, 0x51, 2, 0x07, 0xa1}, /* A tempo meta-event must include all three bytes. */
    };
    const size_t sizes[] = {9, 5, 9, 8, 5, 5, 6, 12, 11, 7, 10, 6};
    CHECK(sizeof sizes / sizeof sizes[0] == sizeof bad / sizeof bad[0]);
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        CHECK(!parse_track(&sequence, bad[i], sizes[i]));
        CHECK(!sequence.events && !sequence.count);
    }
    size_t payload_size = 1024 * 1024, huge_size = payload_size + 9;
    unsigned char *huge = malloc(huge_size);
    CHECK(huge);
    huge[0] = 0;
    huge[1] = 0xf0;
    huge[2] = 0xc0;
    huge[3] = 0x80;
    huge[4] = 0;
    memset(huge + 5, 1, payload_size);
    huge[payload_size + 4] = 0xf7;
    memcpy(huge + payload_size + 5, "\0\xff\x2f\0", 4);
    CHECK(!parse_track(&sequence, huge, huge_size));
    CHECK(!sequence.events && !sequence.count);
    free(huge);
    puts("MIDI SysEx framing, continuation/escape preservation, timing and bounds passed");
}
static void check_stereo_capture(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file);
    float frame[2];
    double left = 0, right = 0;
    float peak_left = 0, peak_right = 0;
    unsigned different = 0, frames = 0;
    while (fread(frame, sizeof frame, 1, file) == 1)
    {
        CHECK(isfinite(frame[0]) && isfinite(frame[1]));
        left += frame[0] * frame[0];
        right += frame[1] * frame[1];
        if (fabsf(frame[0]) > peak_left)
            peak_left = fabsf(frame[0]);
        if (fabsf(frame[1]) > peak_right)
            peak_right = fabsf(frame[1]);
        different += frame[0] != frame[1];
        frames++;
    }
    CHECK(!ferror(file) && feof(file));
    CHECK(!fclose(file));
    /* Independent signals survive DART conversion, resampling, volume and SDL. */
    CHECK(frames > 2048 && different > 1000);
    CHECK(left > right * 12 && left < right * 20);
    CHECK(peak_left > 0.24f && peak_left < 0.245f);
    CHECK(peak_right > 0.060f && peak_right < 0.062f);
    printf(
        "Stereo PCM disk output: %u frames, %u different L/R, energy %.6f/%.6f, peaks %.6f/%.6f\n",
        frames, different, left, right, peak_left, peak_right);
}
int main(void)
{
    midi_sequence_contracts();
    const char *capture = getenv("TWOINE_TEST_STEREO_CAPTURE");
    if (capture)
    {
        CHECK(!strcmp(getenv("SDL_AUDIO_DRIVER") ? getenv("SDL_AUDIO_DRIVER") : "", "disk"));
        CHECK(!remove(capture) || errno == ENOENT);
        CHECK(!strcmp(getenv("SDL_AUDIO_DISK_OUTPUT_FILE") ? getenv("SDL_AUDIO_DISK_OUTPUT_FILE")
                                                           : "",
                      capture));
    }
    LxExecutionServices services = {0};
    char error[256];
    services.tib_size = LXTIBSIZE;
    services.tib2_offset = sizeof(LxTIB);
    services.tls_offset = sizeof(LxTIB) + sizeof(LxTIB2);
#if TWOINE_PORTABLE_LOADER
    GLoaderState.execution = lx_execution_create("unicorn", &services, error, sizeof error);
#else
    GLoaderState.execution = lx_execution_create("native", &services, error, sizeof error);
#endif
    CHECK(GLoaderState.execution);
    GLoaderState.initOs2Tib = init_tib;
    GLoaderState.setOs2Tib = attach_tib;
    GLoaderState.deinitOs2Tib = detach_tib;
    GLoaderState.registerAudioGenerator = lxRegisterAudioGenerator;
    GLoaderState.registerStereoAudioGenerator = lxRegisterStereoAudioGenerator;
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 0x10000, 3, 0);
    CHECK(mapping);
    unsigned char *p = lx_execution_mapping_host(mapping);
    ULONG base = lx_execution_mapping_address(mapping);
    CHECK(mciSendCommand(777, 2, 0, p, 0) == 5001);
    lx_guest_store_u32(p + 8, 9);
    CHECK(!mciSendCommand(0, 1, 0x1000, p, 0));
    USHORT device = lx_guest_load_u16(p + 4);
    CHECK(device);
    memset(p, 0, 56);
    lx_guest_store_u32(p + 4, 16);
    lx_guest_store_u32(p + 8, 1);
    lx_guest_store_u32(p + 12, 22050);
    lx_guest_store_u32(p + 16, 2);
    lx_guest_store_u32(p + 20, 4);
#if TWOINE_PORTABLE_LOADER
    unsigned char body[] = {0xba, 0, 0, 0, 0, 0xff, 0x02, 0xb8, 1, 0, 0, 0, 0xc3};
    lx_guest_store_u32(body + 1, base + 0x3000);
    memcpy(p + 0x2000, body, sizeof body);
    CHECK(lx_execution_protect(GLoaderState.execution, mapping, 7));
    lx_guest_store_u32(p + 40, base + 0x2000);
#else
    lx_guest_store_u32(p + 40, (ULONG)(uintptr_t)completed);
#endif
    CHECK(!mciSendCommand(device, 63, 0x10000, p, 0));
    ULONG write_entry = lx_guest_load_u32(p + 32);
    CHECK(write_entry);
    memset(p, 0, 32);
    lx_guest_store_u32(p + 8, 2);
    lx_guest_store_u32(p + 12, 4096);
    lx_guest_store_u32(p + 28, base + 128);
    CHECK(!mciSendCommand(device, 62, 0x40000, p, 0));
    lx_guest_store_u32(p + 16, 101);
    CHECK(mciSendCommand(device, 12, 0x80000, p, 0));
    lx_guest_store_u32(p + 16, 50);
    CHECK(!mciSendCommand(device, 12, 0x80000, p, 0));
    for (unsigned b = 0; b < 2; b++)
    {
        ULONG address = lx_guest_load_u32(p + 128 + b * 32 + 4);
        int16_t *samples = lx_execution_memory(GLoaderState.execution, address, 4096, 3);
        CHECK(samples);
        for (unsigned n = 0; n < 1024; n++)
        {
            samples[n * 2] = (int16_t)(sin(n * 0.2) * 16000);
            samples[n * 2 + 1] = (int16_t)(cos(n * 0.37) * 4000);
        }
    }
#if TWOINE_PORTABLE_LOADER
    CHECK(
        !lx_execution_callback32_4(GLoaderState.execution, write_entry, device, base + 128, 2, 0));
    unsigned *completion_count = (unsigned *)(p + 0x3000);
#else
    ULONG (*write_pcm)(ULONG, PVOID, ULONG) = (void *)(uintptr_t)write_entry;
    CHECK(!write_pcm(device, p + 128, 2));
    unsigned *completion_count = &completions;
#endif
    for (unsigned n = 0; n < 500 && __atomic_load_n(completion_count, __ATOMIC_ACQUIRE) < 2; n++)
        lx_host_sleep_ms(2);
    CHECK(*completion_count == 2);
    CHECK(!mciSendCommand(device, 2, 0, p, 0));
    CHECK(mciSendCommand(device, 2, 0, p, 0) == 5001);
    if (capture)
        goto finished;
    const unsigned char midi[] = {'M', 'T', 'h', 'd',  0,   0,   0, 6,    0,    0,  0, 1,
                                  0,   96,  'M', 'T',  'r', 'k', 0, 0,    0,    12, 0, 0x90,
                                  60,  100, 96,  0x80, 60,  0,   0, 0xff, 0x2f, 0};
    memcpy(p + 512, midi, sizeof midi);
    memset(p, 0, 80);
    lx_guest_store_u32(p + 16, sizeof midi);
    lx_guest_store_u32(p + 20, base + 512);
    lx_guest_store_u32(p + 68, 0x204d454d);
    ULONG media = mmioOpen(NULL, p, 4);
    CHECK(media);
    memset(p + 512, 0, sizeof midi);
    size_t length = 0;
    unsigned char *copy = lx_mmio_copy(media, &length);
    CHECK(copy && length == sizeof midi && !memcmp(copy, midi, length));
    free(copy);
    memset(p, 0, 20);
    lx_guest_store_u32(p + 12, media);
    CHECK(!mciSendCommand(0, 1, 0x4000, p, 0));
    device = lx_guest_load_u16(p + 4);
    CHECK(!mciSendCommand(device, 4, 0, p, 0));
    lx_host_sleep_ms(600);
    CHECK(!mciSendCommand(device, 2, 0, p, 0));
    CHECK(!mmioClose(media, 0));
    CHECK(mmioClose(media, 0));
    for (unsigned malformed = 0; malformed < 4; malformed++)
    {
        memcpy(p + 512, midi, sizeof midi);
        if (malformed == 0)
            memset(p + 516, 0xff, 4); /* Header exceeds the retained element. */
        else if (malformed == 1)
            p[512 + 11] = 2; /* A required second track is absent. */
        else if (malformed == 2)
            p[512 + 24] = 0xff; /* Note data cannot carry a status bit. */
        else
            p[512 + 21] = 127; /* Track body exceeds the element. */
        memset(p, 0, 80);
        lx_guest_store_u32(p + 16, sizeof midi);
        lx_guest_store_u32(p + 20, base + 512);
        lx_guest_store_u32(p + 68, 0x204d454d);
        media = mmioOpen(NULL, p, 4);
        CHECK(media);
        memset(p, 0, 20);
        lx_guest_store_u32(p + 12, media);
        CHECK(mciSendCommand(0, 1, 0x4000, p, 0));
        CHECK(!mmioClose(media, 0));
    }
finished:
    lxAudioShutdown();
    if (capture)
        check_stereo_capture(capture);
    CHECK(lx_execution_unmap(GLoaderState.execution, mapping));
    lx_execution_destroy(GLoaderState.execution);
    puts("MMPM PCM delivery and memory MIDI contracts passed");
    return 0;
}
