/* Public domain. No rights reserved. Wholly AI-authored. */
/* Queue catalogue for the native PDF and PCL file printers. Record pointer
   fields are 32-bit OS/2 addresses, including on a 64-bit host. */
#include "os2/include/private/export.h"
#include "os2/include/pmspl.h"
#include "os2/include/private/pmsplp.h"
#include "emulator/abi.h"
#include "os2/wpsh/pmspl/internal.h"
#include "os2/wpsh/pmspl/generated/pmspl-lx.h"

/* NERR_QNotFound comes from the LAN Server error range (Toolkit neterr.h, pmerr.h). */
#ifndef NERR_QNotFound
#define NERR_QNotFound 2150
#endif
#ifndef NERR_DestNotFound
#define NERR_DestNotFound 2152
#endif

/* Queue records with 32-bit guest pointers: level 0 is the bare name, levels 1 and 2
   PRQINFO, 3 and 4 PRQINFO3, 5 a name pointer and 6 PRQINFO6. */
enum
{
    QNLEN = 12,
    PRQINFO0_SIZE = QNLEN + 1,
    PRQINFO_SIZE = 44,
    PRQINFO_PRIORITY = 14,
    PRQINFO_PRPROC = 24,
    PRQINFO_DESTINATIONS = 28,
    PRQINFO_COMMENT = 36,
    PRQINFO3_SIZE = 44,
    PRQINFO3_PRIORITY = 4,
    PRQINFO3_TYPE = 10,
    PRQINFO3_PRPROC = 16,
    PRQINFO3_COMMENT = 24,
    PRQINFO3_PRINTERS = 32,
    PRQINFO3_DRIVER_NAME = 36,
    PRQINFO3_DRIVER_DATA = 40,
    PRQINFO5_SIZE = 4,
    PRQINFO6_SIZE = 52,
    /* Every queue reports priority 1 and the PMPRINT queue processor. */
    QUEUE_PRIORITY = 1,
    /* DRIVDATA is ULONG-aligned within the caller's buffer. */
    DRIVDATA_ALIGNMENT = 4
};

static size_t queue_record_size(ULONG level)
{
    switch (level)
    {
        case 0:
            return PRQINFO0_SIZE;
        case 1:
        case 2:
            return PRQINFO_SIZE;
        case 3:
        case 4:
            return PRQINFO3_SIZE;
        case 5:
            return PRQINFO5_SIZE;
        case 6:
            return PRQINFO6_SIZE;
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
        return ERROR_INVALID_PARAMETER;
    if (!queue_record_size(level))
        return ERROR_INVALID_LEVEL;
    int index = queue ? lx_printer_index(queue) : -1;
    if (queue && (index < 0 || !lx_printer_queue_is(queue, (unsigned)index)))
    {
        *returned = *total = *needed = 0;
        return NERR_QNotFound;
    }
    /* The catalogue holds two queues: the PDF and PCL file printers. */
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
            bytes = ((bytes + DRIVDATA_ALIGNMENT - 1) & ~(size_t)(DRIVDATA_ALIGNMENT - 1)) +
                    LX_PRINTER_DRIVER_DATA_SIZE;
        }
    }
    *returned = 0;
    *total = count;
    *needed = (ULONG)bytes;
    if (capacity < bytes)
        return ERROR_MORE_DATA;
    if (!output || base > UINT32_MAX - bytes)
        return ERROR_INVALID_PARAMETER;
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
            lx_guest_store_u16(entry + PRQINFO_PRIORITY, QUEUE_PRIORITY);
            queue_string(buffer, base, &tail, entry + PRQINFO_PRPROC, "PMPRINT");
            queue_string(buffer, base, &tail, entry + PRQINFO_DESTINATIONS,
                         lx_printer_queues[selected]);
            queue_string(buffer, base, &tail, entry + PRQINFO_COMMENT,
                         lx_printer_descriptions[selected]);
        }
        else
        {
            lx_guest_store_u16(entry + PRQINFO3_PRIORITY, QUEUE_PRIORITY);
            if ((int)selected == preferred)
                lx_guest_store_u16(entry + PRQINFO3_TYPE, PRQ3_TYPE_APPDEFAULT);
            queue_string(buffer, base, &tail, entry + PRQINFO3_PRPROC, "PMPRINT");
            queue_string(buffer, base, &tail, entry + PRQINFO3_COMMENT,
                         lx_printer_descriptions[selected]);
            queue_string(buffer, base, &tail, entry + PRQINFO3_PRINTERS,
                         lx_printer_queues[selected]);
            queue_string(buffer, base, &tail, entry + PRQINFO3_DRIVER_NAME,
                         lx_printer_drivers[selected]);
            /* DRIVDATA stays inside the caller's queue buffer. */
            tail = (tail + DRIVDATA_ALIGNMENT - 1) & ~(size_t)(DRIVDATA_ALIGNMENT - 1);
            lx_guest_store_u32(entry + PRQINFO3_DRIVER_DATA, base + (uint32)tail);
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
        return ERROR_INVALID_PARAMETER;
    ULONG returned, total;
    return lx_spl_catalogue(queue, level, buffer, capacity, (uint32)(uintptr_t)buffer, &returned,
                            &total, needed);
}

ULONG SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity, PULONG returned,
                   PULONG total, PULONG needed, PVOID reserved)
{
    if ((computer && *computer) || reserved)
        return ERROR_INVALID_PARAMETER;
    return lx_spl_catalogue(NULL, level, buffer, capacity, (uint32)(uintptr_t)buffer, returned,
                            total, needed);
}

ULONG lx_spl_device(const char *name, ULONG level, PVOID output, ULONG capacity, uint32 base,
                    PULONG needed)
{
    if (!needed)
        return ERROR_INVALID_PARAMETER;
    if (level > 3)
        return ERROR_INVALID_LEVEL;
    int index = lx_printer_index(name);
    if (index < 0 || !lx_printer_queue_is(name, (unsigned)index))
    {
        *needed = 0;
        return NERR_DestNotFound;
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
        return ERROR_MORE_DATA;
    if (!output || base > UINT32_MAX - bytes)
        return ERROR_INVALID_PARAMETER;
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
        return ERROR_INVALID_PARAMETER;
    return lx_spl_device(device, level, buffer, capacity, (uint32)(uintptr_t)buffer, needed);
}

ULONG lx_spl_printers(ULONG level, ULONG types, PVOID output, ULONG capacity, uint32 base,
                      PULONG returned, PULONG total, PULONG needed)
{
    if (level)
        return ERROR_INVALID_LEVEL;
    if (!returned || !total || !needed || !(types & 0x107) || (types & ~0x107u))
        return ERROR_INVALID_PARAMETER;
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
        return ERROR_MORE_DATA;
    if (!count)
        return 0;
    if (!output || base > UINT32_MAX - bytes)
        return ERROR_INVALID_PARAMETER;
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
        return ERROR_INVALID_PARAMETER;
    uint32 base = 0;
    if (capacity &&
        !lx_execution_address(GProcessState.execution, buffer, capacity, LX_GUEST_WRITE, &base))
        return ERROR_INVALID_PARAMETER;
    return lx_spl_printers(level, types, buffer, capacity, base, returned, total, needed);
}
