/* Public domain. No rights reserved. Wholly AI-authored. */
/* x86 architectural values shared by the execution backends. */
#ifndef TWOINE_EXECUTION_X86_H
#define TWOINE_EXECUTION_X86_H

enum
{
    /* Segment selector: descriptor index above the table-indicator and privilege bits. */
    X86_SELECTOR_INDEX_SHIFT = 3,
    X86_SELECTOR_RPL_MASK = 3,
    X86_SELECTOR_LDT = 4,
    /* OS/2 application selectors live in the LDT at ring 3. */
    X86_SELECTOR_LDT_RING3 = X86_SELECTOR_LDT | X86_SELECTOR_RPL_MASK,

    X86_OPCODE_RET_IMM16 = 0xc2,
    X86_OPCODE_RET = 0xc3,
    X86_OPCODE_RETF_IMM16 = 0xca,
    X86_OPCODE_RETF = 0xcb,
    X86_OPCODE_INT = 0xcd,
    X86_OPCODE_NOP = 0x90,

    X86_PAGE_SIZE = 4096,
    /* One 16-bit segment's addressable range. */
    X86_SEGMENT_SIZE = 0x10000,
    X86_SEGMENT_LIMIT = 0xffff
};

#endif
