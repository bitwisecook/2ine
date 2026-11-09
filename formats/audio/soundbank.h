/* Public domain. No rights reserved. Wholly AI-authored. */
/* Sound bank files the MIDI synthesizer loads: SoundFont 2/3 (RIFF "sfbk"),
   Downloadable Sounds (RIFF "DLS ") and SFZ text instruments. */
#ifndef TB_FORMATS_SOUNDBANK_H
#define TB_FORMATS_SOUNDBANK_H
#include <stddef.h>
#include <stdint.h>

/* RIFF banks start with this many bytes: "RIFF", the chunk size, the form type. */
#define TB_SOUNDBANK_HEADER_SIZE 12u
/* Largest bank file accepted. */
#define TB_SOUNDBANK_SIZE_LIMIT INT64_C(2147483648)

/* "sf2", "sf3", "sfz" or "dls" from a file name's extension, or NULL. */
const char *tb_soundbank_format(const char *name);
/* Whether the first TB_SOUNDBANK_HEADER_SIZE bytes of a file of file_size
   bytes begin a bank of that format. SFZ banks are text and have no header. */
int tb_soundbank_header_valid(const char *format, const unsigned char *header, uint64_t file_size);
#endif
