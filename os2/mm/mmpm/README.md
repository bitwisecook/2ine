<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MMPM.DLL

The merged MMPM/2 library: the media control interface (MDM) with its
waveaudio, sequencer and digital-video devices, and multimedia file I/O
(MMIO).

- IBM source: no build directory of its own is known. The SYM directory
  `mmos2/dll/merge` lists the components linked into it: ampmxmcd, audioif,
  audiomcd, audiosh, fssh, hhp, mdm, midiio, midimcd, mipm, mish, mmio, ssm
  and waveproc (`mmos2/dll/nonmerge` holds the same names built separately).
- Evidence: LX entry table of MMPM.DLL (inventory `modules.json`,
  07dd55b67734…, BLDLEVEL `@#IBM:14.075#@`) and of the MDM and MMIO
  forwarders; `F:\w4mm\src\mm\mci\mdm\*.c` strings in the unpacked MMPM.DLL;
  SYM directories `mm/mci/{mdm,admc,mimc,svmc}` and `mm/mmio/mmio`.
- In Warp 4.52: Real code; MDM and MMIO forward to it. It also holds the
  waveaudio (`MM_mciDriverEntryADMC`) and sequencer (`MM_mciDriverEntryMIMC`)
  drivers; the software-motion-video driver SVMC is a separate DLL.
- In 2ine: Implements the MDM entries (MMPM 1-3) and the MMIO entries
  (MMPM 39-43, 45, 90) at the ordinals Warp's forwarders name. The waveaudio,
  sequencer and digital-video devices are built in; 2ine loads no MCI drivers.
- Exports: `mmpm.def`.

## Units

The sub-directories follow IBM's `mm` tree. The IBM unit names in `mci/mdm/`
come from MMPM.DLL; that a 2ine unit covers the same area as the IBM unit of
that name is inferred from the name. Units without an IBM name are named by
function.

### `mci/mdm/` (IBM `mm\mci\mdm`)

| 2ine unit | IBM unit | APIs and services |
|---|---|---|
| `mmsndcmd.c` | `mmsndcmd.c` | mciSendCommand: routing to the device that owns the ID; MCI_SET volume, MCI_PAUSE/MCI_RESUME; portable parameter-block sizes |
| `mmsndstr.c` | `mmsndstr.c` | mciSendString; mciGetErrorString (IBM unit unknown) |
| `mciopen.c` | `mciopen.c` | MCI_OPEN for the waveaudio (amp-mixer) and sequencer devices |
| `mciclose.c` | `mciclose.c` | MCI_STOP and MCI_CLOSE for those devices |
| `mdmnotfy.c` | `mdmnotfy.c` | MM_MCINOTIFY posting for every device |
| `minit.c` | `minit.c` | Termination: closes every device when MMPM unloads |
| `devices.c` | unknown | Waveaudio/sequencer device table, host audio generator, delivery thread |
| `mdm.h` | — | The device table and the units' interfaces |

### `mci/admc/` (IBM `mm\mci\admc`, the waveaudio driver)

| 2ine unit | APIs and services |
|---|---|
| `dart.c`, `admc.h` | DART: MCI_MIXSETUP, MCI_BUFFER, the mixer write entry, PCM mixing, buffer completion (MIX_WRITE_COMPLETE) |

### `mci/mimc/` (IBM `mm\mci\mimc`, the sequencer driver)

| 2ine unit | APIs and services |
|---|---|
| `sequencer.c`, `mimc.h` | MIDI file open (MCI_OPEN_MMIO), MCI_PLAY, rendering through the synthesizer or the oscillator |

### `mci/svmc/` (IBM `mm\mci\svmc`, software motion video; inferred)

| 2ine unit | APIs and services |
|---|---|
| `video_device.c` | Device table, time formats, the video window, MCI_OPEN, MCI_LOAD elements |
| `video_command.c` | The digital-video command switch and notification |
| `video_string.c` | mciSendString commands for digital video |
| `video_audio.c` | Sound ring and media clock |
| `video_play.c` | Playback worker and stop |
| `video.h`, `digital_video.h` | Shared types; the interface MDM's units call |

### `mmio/mmio/` (IBM `mm\mmio\mmio`; file names unknown)

| 2ine unit | APIs and services |
|---|---|
| `mmio.c` | mmioOpen/Close/Read/Write/Seek, mmioInstallIOProc, mmioGetHeader, DOS-file and memory elements, custom IOProcs |

Shared helpers outside MMPM: `os2/mm/midi/` (synthesizer, MIDI file parser
`midi_sequence.c`, fallback oscillator `midi_oscillator.c`) and
`os2/mm/video/decode/` (FFmpeg decoder). Toolkit headers: `os2me.h`,
`mcios2.h`, `meerror.h`, `os2medef.h`, `mmioos2.h`; guest parameter-block
layouts: `os2/include/private/os2mep.h`.
