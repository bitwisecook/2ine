/* Public domain. No rights reserved. Wholly AI-authored. */
/* Queue catalogue for the native PDF and PCL file printers. Record pointer
   fields are 32-bit OS/2 addresses, including on a 64-bit host. */
#include "os2libs/os2native.h"
#include "os2libs/pmspl.h"
#include "os2libs/printer_profile.h"
#include "emulator/abi.h"
#include "os2libs/generated/pmspl-lx.h"

static size_t queue_record_size(ULONG level)
{
    switch (level)
    {
        case 0:
            return 13;
        case 1:
        case 2:
            return 44;
        case 3:
        case 4:
            return 44;
        case 5:
            return 4;
        case 6:
            return 52;
        default:
            return 0;
    }
}

static void queue_string(uint8 *buffer, uint32 base, size_t *tail, uint8 *slot, const char *text)
{
    size_t size = strlen(text) + 1;
    lx_guest_store_u32(slot, base + (uint32)*tail);
    memcpy(buffer + *tail, text, size);
    *tail += size;
}

ULONG lx_spl_catalogue(const char *queue, ULONG level, PVOID output, ULONG capacity, uint32 base,
                       PULONG returned, PULONG total, PULONG needed)
{
    if (!returned || !total || !needed)
        return 87;
    if (!queue_record_size(level))
        return 124; /* ERROR_INVALID_LEVEL */
    int index = queue ? lx_printer_index(queue) : -1;
    if (queue && (index < 0 || strcasecmp(queue, lx_printer_queues[index])))
    {
        *returned = *total = *needed = 0;
        return 2150; /* NERR_QNotFound */
    }
    unsigned count = queue ? 1 : 2;
    size_t record = queue_record_size(level), bytes = record * count;
    for (unsigned i = 0; i < count && level; i++)
    {
        unsigned selected = queue ? (unsigned)index : i;
        if (level != 1 && level != 2)
            bytes += strlen(lx_printer_queues[selected]) + 1;
        if (level != 5)
            bytes += strlen("PMPRINT") + 1 + strlen(lx_printer_descriptions[selected]) + 1;
        if (level == 1 || level == 2)
            bytes += strlen(lx_printer_queues[selected]) + 1;
        if (level == 3 || level == 4 || level == 6)
        {
            bytes +=
                strlen(lx_printer_queues[selected]) + 1 + strlen(lx_printer_drivers[selected]) + 1;
            bytes = ((bytes + 3) & ~(size_t)3) + LX_PRINTER_DRIVER_DATA_SIZE;
        }
    }
    *returned = 0;
    *total = count;
    *needed = (ULONG)bytes;
    if (capacity < bytes)
        return 234; /* ERROR_MORE_DATA */
    if (!output || base > UINT32_MAX - bytes)
        return 87;
    uint8 *buffer = output;
    memset(buffer, 0, bytes);
    size_t tail = record * count;
    int preferred = level == 3 || level == 4 || level == 6 ? lx_printer_default_queue() : -1;
    for (unsigned i = 0; i < count; i++)
    {
        unsigned selected = queue ? (unsigned)index : i;
        uint8 *entry = buffer + i * record;
        if (!level || level == 1 || level == 2)
            memcpy(entry, lx_printer_queues[selected], strlen(lx_printer_queues[selected]) + 1);
        else
            queue_string(buffer, base, &tail, entry, lx_printer_queues[selected]);
        if (!level || level == 5)
            continue;
        if (level == 1 || level == 2)
        {
            lx_guest_store_u16(entry + 14, 1);
            queue_string(buffer, base, &tail, entry + 24, "PMPRINT");
            queue_string(buffer, base, &tail, entry + 28, lx_printer_queues[selected]);
            queue_string(buffer, base, &tail, entry + 36, lx_printer_descriptions[selected]);
        }
        else
        {
            lx_guest_store_u16(entry + 4, 1);
            if ((int)selected == preferred)
                lx_guest_store_u16(entry + 10, 0x0004); /* PRQ3_TYPE_APPDEFAULT */
            queue_string(buffer, base, &tail, entry + 16, "PMPRINT");
            queue_string(buffer, base, &tail, entry + 24, lx_printer_descriptions[selected]);
            queue_string(buffer, base, &tail, entry + 32, lx_printer_queues[selected]);
            queue_string(buffer, base, &tail, entry + 36, lx_printer_drivers[selected]);
            /* DRIVDATA stays inside the caller's queue buffer. */
            tail = (tail + 3) & ~(size_t)3;
            lx_guest_store_u32(entry + 40, base + (uint32)tail);
            lx_printer_driver_data(buffer + tail, selected);
            tail += LX_PRINTER_DRIVER_DATA_SIZE;
        }
    }
    *returned = count;
    return 0;
}

ULONG SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer, ULONG capacity,
                    PULONG needed)
{
    if ((computer && *computer) || !queue || !*queue)
        return 87;
    ULONG returned, total;
    return lx_spl_catalogue(queue, level, buffer, capacity, (uint32)(uintptr_t)buffer, &returned,
                            &total, needed);
}

ULONG SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity, PULONG returned,
                   PULONG total, PULONG needed, PVOID reserved)
{
    if ((computer && *computer) || reserved)
        return 87;
    return lx_spl_catalogue(NULL, level, buffer, capacity, (uint32)(uintptr_t)buffer, returned,
                            total, needed);
}

ULONG lx_spl_device(const char *name, ULONG level, PVOID output, ULONG capacity, uint32 base,
                    PULONG needed)
{
    if (!needed)
        return 87;
    if (level > 3)
        return 124;
    int index = lx_printer_index(name);
    if (index < 0 || strcasecmp(name, lx_printer_queues[index]))
    {
        *needed = 0;
        return 2152; /* NERR_DestNotFound */
    }
    size_t record = level == 0 ? 9 : level == 1 ? 44 : level == 2 ? 4 : 32;
    size_t bytes = record;
    if (level == 2 || level == 3)
        bytes += strlen(lx_printer_queues[index]) + 1;
    if (level == 1)
        bytes += 1;
    if (level == 3)
        bytes += strlen("FILE") + 1 + strlen(lx_printer_descriptions[index]) + 1 +
                 strlen(lx_printer_drivers[index]) + 1 + 2;
    *needed = (ULONG)bytes;
    if (capacity < bytes)
        return 234;
    if (!output || base > UINT32_MAX - bytes)
        return 87;
    uint8 *buffer = output;
    memset(buffer, 0, bytes);
    size_t tail = record;
    if (level == 0 || level == 1)
        strcpy((char *)buffer, lx_printer_queues[index]);
    else
        queue_string(buffer, base, &tail, buffer, lx_printer_queues[index]);
    if (level == 1)
        queue_string(buffer, base, &tail, buffer + 36, "");
    else if (level == 3)
    {
        queue_string(buffer, base, &tail, buffer + 4, "");
        queue_string(buffer, base, &tail, buffer + 8, "FILE");
        queue_string(buffer, base, &tail, buffer + 16, "");
        queue_string(buffer, base, &tail, buffer + 20, lx_printer_descriptions[index]);
        queue_string(buffer, base, &tail, buffer + 24, lx_printer_drivers[index]);
    }
    return 0;
}

ULONG SplQueryDevice(PSZ computer, PSZ device, ULONG level, PVOID buffer, ULONG capacity,
                     PULONG needed)
{
    if ((computer && *computer) || !device || !*device)
        return 87;
    return lx_spl_device(device, level, buffer, capacity, (uint32)(uintptr_t)buffer, needed);
}

ULONG lx_spl_printers(ULONG level, ULONG types, PVOID output, ULONG capacity, uint32 base,
                      PULONG returned, PULONG total, PULONG needed)
{
    if (level)
        return 124;
    if (!returned || !total || !needed || !(types & 0x107) || (types & ~0x107u))
        return 87;
    ULONG selected[4], kinds[4], count = 0;
    for (unsigned i = 0; i < 2; i++)
    {
        if (types & 1)
        {
            selected[count] = i;
            kinds[count++] = 1;
        }
        if (types & 4)
        {
            selected[count] = i;
            kinds[count++] = 4;
        }
    }
    size_t bytes = count * 20;
    for (ULONG i = 0; i < count; i++)
        bytes += 2 + 2 * (strlen(lx_printer_queues[selected[i]]) + 1) +
                 strlen(lx_printer_descriptions[selected[i]]);
    *returned = 0;
    *total = count;
    *needed = bytes;
    if (capacity < bytes)
        return 234;
    if (!count)
        return 0;
    if (!output || base > UINT32_MAX - bytes)
        return 87;
    uint8 *buffer = output;
    memset(buffer, 0, bytes);
    size_t tail = count * 20;
    for (ULONG i = 0; i < count; i++)
    {
        uint8 *entry = buffer + i * 20;
        ULONG printer = selected[i];
        lx_guest_store_u32(entry, kinds[i]);
        queue_string(buffer, base, &tail, entry + 4, "");
        queue_string(buffer, base, &tail, entry + 8, lx_printer_queues[printer]);
        queue_string(buffer, base, &tail, entry + 12, lx_printer_descriptions[printer]);
        queue_string(buffer, base, &tail, entry + 16, lx_printer_queues[printer]);
    }
    *returned = count;
    return 0;
}

ULONG SplEnumPrinter(PSZ computer, ULONG level, ULONG types, PVOID buffer, ULONG capacity,
                     PULONG returned, PULONG total, PULONG needed, PVOID reserved)
{
    if ((computer && *computer) || reserved)
        return 87;
    uint32 base = 0;
    if (capacity &&
        !lx_execution_address(GProcessState.execution, buffer, capacity, LX_GUEST_WRITE, &base))
        return 87;
    return lx_spl_printers(level, types, buffer, capacity, base, returned, total, needed);
}
