/* Public domain. No rights reserved. Wholly AI-authored. */
/* Workplace class registry: WinRegisterObjectClass, WinDeregisterObjectClass,
   WinEnumObjectClasses and WinReplaceObjectClass. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "os2/wpsh/pmwp/internal.h"
#include "os2/include/bsedos.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <strings.h>

BOOL WinRegisterObjectClass(PSZ name, PSZ module)
{
    TRACE_NATIVE("WinRegisterObjectClass('%s', '%s')", name, module);
    if (!name || !*name || !module || !*module || strnlen(name, OBJECT_LIMIT) == OBJECT_LIMIT ||
        strnlen(module, OBJECT_LIMIT) == OBJECT_LIMIT)
        return FALSE;
    /* Registration records a case-sensitive class-to-module association. The
       module need not be present until the class is instantiated. */
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && PrfWriteProfileString(profile, "classes", name, module);
    store_end(profile, lock);
    return result;
}

BOOL WinDeregisterObjectClass(PSZ name)
{
    TRACE_NATIVE("WinDeregisterObjectClass('%s')", name);
    if (!name || !*name || strnlen(name, OBJECT_LIMIT) == OBJECT_LIMIT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG size;
    BOOL result = profile && PrfQueryProfileSize(profile, "classes", name, &size) &&
                  PrfWriteProfileData(profile, "classes", name, NULL, 0);
    store_end(profile, lock);
    return result;
}

/* Class enumeration retains the original variable-length OBJCLASS layout:
   three guest pointers followed by the two ASCIIZ strings, with no host padding. */
static const char *workplace_classes[] = {
    "WPObject", "WPAbstract", "WPTransient", "WPFileSystem", "WPFolder",      "WPDataFile",
    "WPDisk",   "WPProgram",  "WPDesktop",   "WPShadow",     "WPClassManager"};

static BOOL builtin_class(const char *name)
{
    for (size_t i = 0; i < sizeof workplace_classes / sizeof *workplace_classes; i++)
        if (!strcmp(name, workplace_classes[i]))
            return TRUE;
    return FALSE;
}

BOOL WinEnumObjectClasses(PVOID guest_output, PULONG capacity)
{
    if (!capacity || (uintptr_t)guest_output > UINT32_MAX)
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    if (!profile)
    {
        store_end(profile, lock);
        return FALSE;
    }
    ULONG list_size = 0;
    PrfQueryProfileSize(profile, "classes", NULL, &list_size);
    char *list = list_size && list_size <= OBJECT_LIMIT ? calloc(list_size + 1, 1) : NULL;
    if (list && !PrfQueryProfileString(profile, "classes", NULL, NULL, list, list_size))
    {
        free(list);
        list = NULL;
    }

    typedef struct Entry
    {
        char *name, *module;
    } Entry;

    Entry *entries = NULL;
    size_t count = 0, total = 0;
    BOOL ok = TRUE;
    for (size_t i = 0; i < sizeof workplace_classes / sizeof *workplace_classes; i++)
    {
        Entry *next = realloc(entries, (count + 1) * sizeof *entries);
        if (!next)
        {
            ok = FALSE;
            break;
        }
        entries = next;
        entries[count].name = strdup(workplace_classes[i]);
        entries[count].module = strdup("PMWP");
        if (!entries[count].name || !entries[count].module)
        {
            free(entries[count].name);
            free(entries[count].module);
            ok = FALSE;
            break;
        }
        total += 14 + strlen(entries[count].name) + strlen(entries[count].module);
        count++;
    }
    for (char *p = list; ok && p && p < list + list_size && *p; p += strlen(p) + 1)
    {
        if (builtin_class(p))
            continue;
        ULONG bytes = 0;
        if (!PrfQueryProfileSize(profile, "classes", p, &bytes) || !bytes || bytes > OBJECT_LIMIT)
            continue;
        char *module = calloc(bytes + 1, 1);
        if (!module)
        {
            ok = FALSE;
            break;
        }
        PrfQueryProfileString(profile, "classes", p, NULL, module, bytes + 1);
        Entry *next = realloc(entries, (count + 1) * sizeof *entries);
        if (!next)
        {
            free(module);
            ok = FALSE;
            break;
        }
        entries = next;
        entries[count].name = strdup(p);
        entries[count].module = module;
        if (!entries[count].name)
        {
            free(module);
            ok = FALSE;
            break;
        }
        total += 14 + strlen(p) + strlen(module);
        count++;
    }
    ULONG supplied = *capacity;
    *capacity = (ULONG)total;
    if (total > OBJECT_LIMIT)
        ok = FALSE;
    if (ok && guest_output)
    {
        if (supplied < total)
            ok = FALSE;
        else
        {
            uint8 *wire = calloc(total ? total : 1, 1);
            if (!wire)
                ok = FALSE;
            else
            {
                size_t at = 0;
                for (size_t i = 0; i < count; i++)
                {
                    size_t n = strlen(entries[i].name) + 1, m = strlen(entries[i].module) + 1,
                           next = at + 12 + n + m;
                    uint32 base = (uint32)(uintptr_t)guest_output;
                    if ((uint64_t)base + next > UINT32_MAX)
                    {
                        ok = FALSE;
                        break;
                    }
                    lx_guest_store_u32(wire + at, i + 1 < count ? base + next : 0);
                    lx_guest_store_u32(wire + at + 4, base + at + 12);
                    lx_guest_store_u32(wire + at + 8, base + at + 12 + n);
                    memcpy(wire + at + 12, entries[i].name, n);
                    memcpy(wire + at + 12 + n, entries[i].module, m);
                    at = next;
                }
                if (ok)
                    ok = lx_execution_write(GProcessState.execution,
                                            (uint32)(uintptr_t)guest_output, wire, total);
                free(wire);
            }
        }
    }
    for (size_t i = 0; i < count; i++)
    {
        free(entries[i].name);
        free(entries[i].module);
    }
    free(entries);
    free(list);
    store_end(profile, lock);
    return ok;
}

BOOL WinReplaceObjectClass(PSZ old_name, PSZ new_name, BOOL replace)
{
    if (!old_name || !*old_name || !new_name || !*new_name || strlen(old_name) > 255 ||
        strlen(new_name) > 255 || !strcmp(old_name, new_name))
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL ok = FALSE;
    ULONG size;
    if (profile &&
        (builtin_class(old_name) || PrfQueryProfileSize(profile, "classes", old_name, &size)) &&
        (builtin_class(new_name) || PrfQueryProfileSize(profile, "classes", new_name, &size)))
    {
        char name[256];
        snprintf(name, sizeof name, "%s", new_name);
        BOOL cycle = FALSE;
        for (unsigned i = 0; i < 256; i++)
        {
            char next[256];
            if (!strcmp(name, old_name))
            {
                cycle = TRUE;
                break;
            }
            if (!PrfQueryProfileString(profile, "replacements", name, NULL, next, sizeof next))
                break;
            snprintf(name, sizeof name, "%s", next);
            if (i == 255)
                cycle = TRUE;
        }
        if (!cycle)
            ok =
                PrfWriteProfileString(profile, "replacements", old_name, replace ? new_name : NULL);
    }
    store_end(profile, lock);
    return ok;
}
