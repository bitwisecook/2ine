/* Public domain. No rights reserved. Wholly AI-authored. */
/* MIDI wire values shared by the file parser, the oscillator and the synthesizers. */
#ifndef TWOINE_MIDI_MESSAGES_H
#define TWOINE_MIDI_MESSAGES_H

/* Status bytes, meta events and controllers. A status byte carries the message
   type in its high nibble and the channel in its low nibble; data bytes are 7-bit. */
enum
{
    MIDI_STATUS_BIT = 0x80,
    MIDI_STATUS_TYPE_MASK = 0xf0,
    MIDI_CHANNEL_MASK = 0x0f,
    MIDI_DATA_MAX = 0x7f,
    MIDI_DATA_BITS = 7,
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_POLY_PRESSURE = 0xa0,
    MIDI_CONTROL_CHANGE = 0xb0,
    MIDI_PROGRAM_CHANGE = 0xc0,
    MIDI_CHANNEL_PRESSURE = 0xd0,
    MIDI_PITCH_BEND = 0xe0,
    MIDI_SYSEX = 0xf0,
    MIDI_SYSEX_END = 0xf7,
    MIDI_META = 0xff,
    MIDI_META_TEMPO = 0x51,
    MIDI_CONTROL_BANK_SELECT = 0,
    MIDI_CONTROL_BANK_SELECT_LSB = 32,
    MIDI_ALL_SOUND_OFF = 120,
    MIDI_ALL_NOTES_OFF = 123
};

/* Channels and programs. General MIDI plays percussion on channel 10. */
enum
{
    MIDI_CHANNELS = 16,
    MIDI_PROGRAMS = 128,
    MIDI_PERCUSSION_CHANNEL = 9
};

/* System-exclusive manufacturer IDs and the reset messages the synthesizers honour:
   GM System On/Off (universal non-real-time, sub-ID 9), Roland GS Reset and
   Yamaha XG System On. */
enum
{
    MIDI_SYSEX_UNIVERSAL_NON_REAL_TIME = 0x7e,
    MIDI_SYSEX_ROLAND = 0x41,
    MIDI_SYSEX_YAMAHA = 0x43,
    MIDI_SYSEX_GENERAL_MIDI = 9
};
#endif
