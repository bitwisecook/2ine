/* Public domain. No rights reserved. Wholly AI-authored. */
/* DOSCALLS ordinal 547: descriptor-driven calls from USE32 code into USE16 code.

   Entry state is registers, not a C prototype: EAX holds the 16:16 target and
   ECX:EDX a 64-bit descriptor consumed from bit 0. Arguments stay in the caller's
   32-bit stack slots after its near return address, and the caller removes them.

   Descriptor stream:
     prefixes  low nibble 0xf: 6 bits if bit 4 is set, then test again; otherwise
               5 bits, after which faults reading arguments return
               ERROR_INVALID_PARAMETER
     count     4 bits of source slots, read in order and pushed in order
     element   00 full slot; 10 low word; x1 16:16 pointer, 5 bits or, when the
               low five bits are all set, 9 bits, with bit 1 meaning copy back
     tail      while bit 2 is set, 3 bits: 0 pushes a zero word, 1 a zero dword
     result    0 DX:AX; 1 16:16 converted to flat; 2 zero-extended AX;
               3 sign-extended AX */
#include "os2/baseos/doscalls/internal.h"

enum
{
    THUNK_SLOT_LIMIT = 15,
    THUNK_FRAME_BYTES = 256,
    THUNK_TILE = 0x10000,
    THUNK_TILED_LIMIT = 0x20000000,
    THUNK_COPY_SIZE_MASK = 0x1ffff,
    THUNK_MAXIMUM_STRING = 0x20000,
    THUNK_SIZE_UNCHECKED = -1,
    THUNK_SIZE_FAULT = -2,
    THUNK_SLOT_SIZE = 4,
    THUNK_PAGE_MASK = 0xFFF
};

/* Descriptor fields, in the order the header comment describes them. */
enum
{
    THUNK_PREFIX_MARK = 0xF,
    THUNK_PREFIX_LONG = 0x10,
    THUNK_PREFIX_SHORT_BITS = 5,
    THUNK_PREFIX_LONG_BITS = 6,
    THUNK_COUNT_MASK = 0xF,
    THUNK_COUNT_BITS = 4,
    THUNK_ELEMENT_POINTER = 0x1,
    THUNK_ELEMENT_LOW_WORD = 0x2,
    THUNK_ELEMENT_BITS = 2,
    THUNK_POINTER_COPY_BACK = 0x2,
    THUNK_POINTER_EXTENDED = 0x1F,
    THUNK_POINTER_EXTENSION_BITS = 4,
    THUNK_POINTER_BITS = 5,
    THUNK_SIZE_CODE_SHIFT = 2,
    THUNK_SIZE_CODE_MASK = 0x7,
    THUNK_TAIL_MORE = 0x4,
    THUNK_TAIL_KIND_MASK = 0x3,
    THUNK_TAIL_ZERO_WORD = 0,
    THUNK_TAIL_ZERO_DWORD = 1,
    THUNK_TAIL_BITS = 3,
    THUNK_RESULT_MASK = 0x3,
    THUNK_RESULT_DX_AX = 0,
    THUNK_RESULT_FAR_POINTER = 1,
    THUNK_RESULT_WORD = 2,
    THUNK_RESULT_SIGNED_WORD = 3,
    THUNK_RESULT_FIELD_SHIFT = 2
};

/* Pointer size codes. Without the extension: 0 is a string (or, with the copy-back
   bit, a USHORT result in ULONG storage), 1 to 4 a power-of-two size, 5 and 6 a
   size held in the previous or next slot, 7 no check. With it: 0 to 3 a count in an
   adjacent slot scaled to bytes, 4 a USHORT length prefix, 5 and 6 fixed records. */
enum
{
    THUNK_SIZE_STRING = 0,
    THUNK_SIZE_PREVIOUS_SLOT = 5,
    THUNK_SIZE_NEXT_SLOT = 6,
    THUNK_SIZE_NONE = 7,
    THUNK_SIZE_NEXT_INDIRECT = 0x20,
    THUNK_EXTENDED_SCALED_FROM_PREVIOUS = 0,
    THUNK_EXTENDED_LENGTH_PREFIX = 4,
    THUNK_EXTENDED_RECORD_24 = 5,
    THUNK_EXTENDED_RECORD_12 = 6,
    THUNK_EXTENDED_NONE = 7,
    THUNK_EXTENDED_RECORD_24_BYTES = 24,
    THUNK_EXTENDED_RECORD_12_BYTES = 12,
    /* Codes 0 and 1 scale by four; code n above that scales by 2^(n+1). */
    THUNK_EXTENDED_MINIMUM_SHIFT = 2
};

typedef struct ThunkCopy
{
    LxGuestMapping *mapping;
    uint32 original, size;
    uint16 selector;
    BOOL copy_back;
} ThunkCopy;

typedef struct ThunkCall
{
    uint32 target, slots;
    uint64 descriptor;
    BOOL faults_return_error;
    ThunkCopy copies[THUNK_SLOT_LIMIT];
    unsigned copy_count;
} ThunkCall;

static uint32 flatToFar(uint32 address)
{
    if (address < THUNK_TILE || address == UINT32_MAX)
        return address;
    if (address >= THUNK_TILED_LIMIT)
        return 0;
    uint16 selector, offset;
    if (!lx_execution_find_selector(GProcessState.execution, address, &selector, &offset, -1))
        return 0;
    return ((uint32)ldtSelector(selector) << SELECTOR_OFFSET_BITS) | offset;
}

static uint32 farToFlat(uint32 pointer)
{
    if (pointer < THUNK_TILE)
        return pointer;
    void *memory = lx_execution_far_to_host(GProcessState.execution, pointer);
    uint32 address = 0;
    return memory && lx_execution_address(GProcessState.execution, memory, 1, 0, &address) ? address
                                                                                           : 0;
}

static int readGuestU32(uint32 address, uint32 *value)
{
    uint8 bytes[sizeof(uint32)];
    if (!lx_execution_read(GProcessState.execution, address, bytes, sizeof bytes))
        return 0;
    *value = lx_guest_load_u32(bytes);
    return 1;
}

static APIRET thunkFault(const ThunkCall *call, const char *what, uint32 address)
{
    if (!call->faults_return_error)
        fprintf(stderr, "2ine: THK_UNITHUNK %s at %08x (target %08x, descriptor %016llx)\n", what,
                (unsigned)address, (unsigned)call->target, (unsigned long long)call->descriptor);
    return ERROR_INVALID_PARAMETER;
}

/* Byte size of the pointee at slot index; d begins at the size-coded element. */
static int64_t pointerSize(const ThunkCall *call, uint64 d, BOOL extended, unsigned index,
                           uint32 pointer)
{
    const unsigned code = (unsigned)(d >> THUNK_SIZE_CODE_SHIFT) & THUNK_SIZE_CODE_MASK;
    const uint32 previous = call->slots + (index - 1) * THUNK_SLOT_SIZE,
                 next = call->slots + (index + 1) * THUNK_SLOT_SIZE;
    uint32 value = 0;
    if (extended)
    {
        if (code == THUNK_EXTENDED_NONE)
            return THUNK_SIZE_UNCHECKED;
        if (code == THUNK_EXTENDED_RECORD_24)
            return THUNK_EXTENDED_RECORD_24_BYTES;
        if (code == THUNK_EXTENDED_RECORD_12)
            return THUNK_EXTENDED_RECORD_12_BYTES;
        if (code == THUNK_EXTENDED_LENGTH_PREFIX)
        {
            uint8 bytes[sizeof(uint16)];
            if (!lx_execution_read(GProcessState.execution, pointer, bytes, sizeof bytes))
                return THUNK_SIZE_FAULT;
            return lx_guest_load_u16(bytes);
        }
        if (!readGuestU32(code != THUNK_EXTENDED_SCALED_FROM_PREVIOUS ? next : previous, &value))
            return THUNK_SIZE_FAULT;
        return (uint32)(value << (code < THUNK_EXTENDED_MINIMUM_SHIFT ? THUNK_EXTENDED_MINIMUM_SHIFT
                                                                      : code + 1));
    }
    switch (code)
    {
        case THUNK_SIZE_STRING:
            if (d & THUNK_POINTER_COPY_BACK)
            {
                /* A USHORT result delivered into ULONG storage. */
                const uint8 zero[sizeof(uint16)] = {0, 0};
                return lx_execution_write(GProcessState.execution, pointer + sizeof(uint16), zero,
                                          sizeof zero)
                           ? (int64_t)sizeof(uint16)
                           : THUNK_SIZE_FAULT;
            }
            else
            {
                char *text = lx_execution_copy_string(GProcessState.execution, pointer,
                                                      THUNK_MAXIMUM_STRING);
                if (!text)
                    return THUNK_SIZE_FAULT;
                const int64_t size = (int64_t)strlen(text) + 1;
                free(text);
                return size;
            }
        case THUNK_SIZE_PREVIOUS_SLOT:
            return readGuestU32(previous, &value) ? (int64_t)value : THUNK_SIZE_FAULT;
        case THUNK_SIZE_NEXT_SLOT:
            /* A set low bit on the next element marks that slot as a pointer to the count. */
            if (!readGuestU32(next, &value) ||
                ((d & THUNK_SIZE_NEXT_INDIRECT) && !readGuestU32(value, &value)))
                return THUNK_SIZE_FAULT;
            return value;
        case THUNK_SIZE_NONE:
            return THUNK_SIZE_UNCHECKED;
        default:
            return (int64_t)1 << code;
    }
}

/* Replace a pointee that reaches past its tile with a copy inside one tile. */
static APIRET copyAcrossTile(ThunkCall *call, uint32 *pointer, uint32 size, BOOL copy_back)
{
    ThunkCopy *copy = &call->copies[call->copy_count];
    const size_t bytes = ((size_t)size + THUNK_PAGE_MASK) & ~(size_t)THUNK_PAGE_MASK;
    copy->mapping = lx_execution_map_aligned(GProcessState.execution, 0, bytes,
                                             LX_GUEST_READ | LX_GUEST_WRITE, THUNK_TILE);
    if (!copy->mapping)
        return ERROR_NOT_ENOUGH_MEMORY;
    call->copy_count++;
    copy->original = *pointer;
    copy->size = size;
    copy->copy_back = copy_back;
    if (!lx_execution_read(GProcessState.execution, *pointer,
                           lx_execution_mapping_host(copy->mapping), size))
        return thunkFault(call, "cannot read argument", *pointer);
    *pointer = lx_execution_mapping_address(copy->mapping);
    uint16 offset;
    if (!lx_execution_find_selector(GProcessState.execution, *pointer, &copy->selector, &offset, 0))
        return ERROR_NOT_ENOUGH_MEMORY;
    return NO_ERROR;
}

static uint32 thunkOperation(void *argument)
{
    ThunkCall *call = argument;
    uint8 frame[THUNK_FRAME_BYTES];
    size_t cursor = sizeof frame;
    uint64 d = call->descriptor;
    while ((d & THUNK_PREFIX_MARK) == THUNK_PREFIX_MARK)
    {
        if (!(d & THUNK_PREFIX_LONG))
        {
            d >>= THUNK_PREFIX_SHORT_BITS;
            call->faults_return_error = TRUE;
            break;
        }
        /* This prefix tells kernel-side 16-bit entries to accept flat pointers.
           2ine's 16-bit entries take 16:16 pointers only, so pointers stay converted. */
        d >>= THUNK_PREFIX_LONG_BITS;
    }
    const unsigned count = (unsigned)d & THUNK_COUNT_MASK;
    d >>= THUNK_COUNT_BITS;
    for (unsigned index = 0; index < count; index++)
    {
        const uint32 slot = call->slots + index * THUNK_SLOT_SIZE;
        uint32 value;
        if (!readGuestU32(slot, &value))
            return thunkFault(call, "cannot read argument slot", slot);
        if (!(d & THUNK_ELEMENT_POINTER))
        {
            if (d & THUNK_ELEMENT_LOW_WORD)
            {
                cursor -= sizeof(uint16);
                lx_guest_store_u16(frame + cursor, (uint16)value);
            }
            else
            {
                cursor -= sizeof(uint32);
                lx_guest_store_u32(frame + cursor, value);
            }
            d >>= THUNK_ELEMENT_BITS;
            continue;
        }
        const BOOL extended = (d & THUNK_POINTER_EXTENDED) == THUNK_POINTER_EXTENDED;
        if (extended)
            d >>= THUNK_POINTER_EXTENSION_BITS;
        if (value)
        {
            const int64_t size = pointerSize(call, d, extended, index, value);
            if (size == THUNK_SIZE_FAULT)
                return thunkFault(call, "cannot size pointer argument", value);
            if (size != THUNK_SIZE_UNCHECKED && (uint32)(uint16)size + (uint16)value >= THUNK_TILE)
            {
                APIRET error =
                    copyAcrossTile(call, &value, (uint32)size, (d & THUNK_POINTER_COPY_BACK) != 0);
                if (error)
                    return error;
            }
            value = flatToFar(value);
        }
        cursor -= sizeof(uint32);
        lx_guest_store_u32(frame + cursor, value);
        d >>= THUNK_POINTER_BITS;
    }
    while (d & THUNK_TAIL_MORE)
    {
        if ((d & THUNK_TAIL_KIND_MASK) == THUNK_TAIL_ZERO_WORD)
        {
            cursor -= sizeof(uint16);
            lx_guest_store_u16(frame + cursor, 0);
        }
        else if ((d & THUNK_TAIL_KIND_MASK) == THUNK_TAIL_ZERO_DWORD)
        {
            cursor -= sizeof(uint32);
            lx_guest_store_u32(frame + cursor, 0);
        }
        d >>= THUNK_TAIL_BITS;
    }
    if (d >> THUNK_RESULT_FIELD_SHIFT)
    {
        fprintf(stderr, "2ine: THK_UNITHUNK unknown descriptor %016llx: bits %llx remain\n",
                (unsigned long long)call->descriptor,
                (unsigned long long)(d >> THUNK_RESULT_FIELD_SHIFT));
        return ERROR_INVALID_PARAMETER;
    }
    uint32 result;
    if (!lx_execution_call16(GProcessState.execution, call->target, frame + cursor,
                             (uint32)(sizeof frame - cursor), &result))
    {
        fprintf(stderr, "2ine: THK_UNITHUNK cannot call target %08x\n", (unsigned)call->target);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (unsigned i = 0; i < call->copy_count; i++)
    {
        const ThunkCopy *copy = &call->copies[i];
        if (copy->copy_back && !lx_execution_write(GProcessState.execution, copy->original,
                                                   lx_execution_mapping_host(copy->mapping),
                                                   copy->size & THUNK_COPY_SIZE_MASK))
            return thunkFault(call, "cannot copy back argument", copy->original);
    }
    switch (d & THUNK_RESULT_MASK)
    {
        case THUNK_RESULT_FAR_POINTER:
            return farToFlat(result);
        case THUNK_RESULT_WORD:
            return result & UINT16_MAX;
        case THUNK_RESULT_SIGNED_WORD:
            return (uint32)(int32_t)(int16_t)result;
        default:
            return result;
    }
}

static void thunkCleanup(void *argument)
{
    ThunkCall *call = argument;
    for (unsigned i = 0; i < call->copy_count; i++)
    {
        ThunkCopy *copy = &call->copies[i];
        if (copy->selector)
            lx_execution_free_selector(GProcessState.execution, copy->selector);
        lx_execution_unmap(GProcessState.execution, copy->mapping);
    }
}

APIRET THK_UNITHUNK(VOID)
{
    LxExecutionRegisters32 caller;
    if (!lx_execution_api_registers32(GProcessState.execution, &caller))
    {
        fprintf(stderr, "2ine: THK_UNITHUNK has no suspended 32-bit caller\n");
        return ERROR_INVALID_FUNCTION;
    }
    TRACE_NATIVE("THK_UNITHUNK(target=%08x, descriptor=%08x:%08x)", (uint)caller.eax,
                 (uint)caller.ecx, (uint)caller.edx);
    ThunkCall call;
    memset(&call, 0, sizeof call);
    call.target = caller.eax;
    call.slots = caller.esp;
    call.descriptor = ((uint64)caller.ecx << 32) | caller.edx;
    return lx_host_guarded_call(thunkOperation, &call, thunkCleanup, &call);
}
