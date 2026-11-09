/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMPM's waveaudio (DART) and sequencer devices share one device table and
   monitor. The host audio generator mixes every playing device; a delivery
   thread per playing device returns DART buffers and posts end notifications.
   Fields of a device change only under lx_mci_monitor. */
#ifndef TWOINE_MMPM_MDM_H
#define TWOINE_MMPM_MDM_H
#include "os2/include/private/export.h"
#include "os2/include/private/os2mep.h"
#include "os2/mm/midi/midi_oscillator.h"
#include "os2/mm/midi/midi_sequence.h"
#include "os2/mm/midi/midi_synth.h"
#include "host/sync.h"

enum
{
    MCI_DEVICE_COUNT = 32,
    /* Waveaudio and sequencer IDs stay below the digital-video IDs. */
    MCI_DEVICE_ID_LIMIT = 0x8000,
    /* DART buffers per device */
    MIX_BUFFER_MAX = 64,
    /* The host audio generator mixes interleaved stereo frames. */
    MCI_MIXER_CHANNELS = 2
};

typedef enum LxMciDeviceKind
{
    LX_MCI_WAVEAUDIO = 1,
    LX_MCI_SEQUENCER = 2
} LxMciDeviceKind;

/* One queued DART buffer, converted to stereo float samples. */
typedef struct LxMixPacket
{
    struct LxMixPacket *next;
    float *samples;
    ULONG count, record;
    double position;
} LxMixPacket;

typedef struct LxMciDevice
{
    USHORT id;
    ULONG rate, bits, channels, callback, notify_window, notify_user;
    LxMciDeviceKind kind;
    int stopped, paused, notify, ending;
    float volume;
    /* DART: buffers being played (head..tail) and played buffers awaiting
       MIX_WRITE_COMPLETE (done..done_tail). */
    LxMixPacket *head, *tail, *done, *done_tail;
    LxHostThread *thread;
    LxGuestMapping *buffers[MIX_BUFFER_MAX];
    unsigned buffer_count, pending;
    /* Sequencer */
    LxMidiSequence sequence;
    size_t next_event;
    double time;
    LxMidiOscillator oscillator;
    LxMidiSynth *synth;
} LxMciDevice;

extern LxMciDevice lx_mci_devices[MCI_DEVICE_COUNT];
extern LxHostMonitor *lx_mci_monitor;

/* devices.c */
void lx_mci_devices_initialize(void);
LxMciDevice *lx_mci_find_device(ULONG id);
/* Registers the audio generator and starts the device's delivery thread. */
ULONG lx_mci_start_audio(LxMciDevice *device);
void lx_mci_devices_terminate(void);

/* minit.c: closes every MCI device; MMPM's module shutdown calls it. */
void lx_mdm_terminate(void);

/* mciopen.c */
ULONG lx_mci_open(ULONG flags, unsigned char *parameters);

/* mciclose.c: stops playback; closing also releases the device. */
void lx_mci_stop(LxMciDevice *device, int closing);

/* mdmnotfy.c: posts MM_MCINOTIFY to window. */
void lx_mci_notify(ULONG window, USHORT device, USHORT code, ULONG user);

#if TWOINE_PORTABLE_LOADER
/* mmsndcmd.c: bytes of the parameter block a command reads or writes. */
size_t lx_mci_parameter_size(USHORT message, ULONG flags);
#endif
#endif
