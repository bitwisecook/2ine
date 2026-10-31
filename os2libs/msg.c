/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "msg.h"

#include "../runtime/host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msg-lx.h"

APIRET DosPutMessage(HFILE handle, ULONG size, PCHAR message)
{
    TRACE_NATIVE("DosPutMessage(%u, %u, %p)", (uint) handle, (uint) size, message);
    uint32 actual;
    if (GLoaderState.writeFile) return GLoaderState.writeFile(handle, message, size, &actual);
    /* Programs importing MSG alone have only the initial standard handles. */
    LxHostFile *file = lx_host_file_standard(handle);
    if (!file) return ERROR_INVALID_HANDLE;
    size_t written;
    const LxHostFsError error = lx_host_file_write(file, message, size, &written);
    lx_host_file_close(file); return error ? ERROR_WRITE_FAULT : NO_ERROR;
}
APIRET16 Dos16PutMessage(USHORT handle, USHORT size, PCHAR message)
{
    return (APIRET16) DosPutMessage(handle, size, message);
}

//
// OS/2 message files (the output of MKMSGF) start with:
//
//   0x00  BYTE   0xFF
//   0x01  CHAR[7] "MKMSGF\0"
//   0x08  CHAR[3] message-number prefix ("EDC", "SYS", ...)
//   0x0B  USHORT  number of messages
//   0x0D  USHORT  number of the first message
//   0x0F  USHORT  format: 0x0200 = 32-bit table offsets,
//                          0x0201 = 16-bit table offsets
//   0x11  BYTE    reserved
//   0x12  USHORT  offset of the message-offset table
//   0x14  USHORT  end of the message-offset table
//
// The offset table holds one entry per message; message N's text runs from
// table[N] to table[N+1] (the end of the table doubles as the final end).
// Message text is a byte string, not NUL-terminated on disk.
//
#define MSGFILE_SIGNATURE_OFFSET 0x01
#define MSGFILE_SIGNATURE        "MKMSGF"
#define MSGFILE_PREFIX_OFFSET    0x08
#define MSGFILE_COUNT_OFFSET     0x0B
#define MSGFILE_FIRST_OFFSET     0x0D
#define MSGFILE_FORMAT_OFFSET    0x0F
#define MSGFILE_TABLE_OFFSET     0x12
#define MSGFILE_TABLE_END_OFFSET 0x14
#define MSGFILE_FORMAT_OFF32     0x0200
#define MSGFILE_FORMAT_OFF16     0x0201

static uint16 readLE16(const uint8 *ptr)
{
    return (uint16) (ptr[0] | (ptr[1] << 8));
}

static uint32 readLE32(const uint8 *ptr)
{
    return (uint32) ptr[0] | ((uint32) ptr[1] << 8) |
           ((uint32) ptr[2] << 16) | ((uint32) ptr[3] << 24);
}

/* Use guest drive mappings for message files. */
static uint8 *loadMessageFile(const char *filename, size_t *outlen)
{
    uint32 err = 0;
    char *unixpath = GLoaderState.makeUnixPath(filename, &err);
    if (!unixpath)
        return NULL;

    size_t len;
    uint8 *data = lx_host_file_snapshot(unixpath, UINT32_MAX, &len, NULL);
    free(unixpath);
    if (!data) return NULL;
    if (len < 0x15) { free(data); return NULL; }

    if (memcmp(data + MSGFILE_SIGNATURE_OFFSET, MSGFILE_SIGNATURE,
               strlen(MSGFILE_SIGNATURE)) != 0) {
        free(data);
        return NULL;
    }

    *outlen = (size_t) len;
    return data;
}

/* Return a span into the file buffer; records are not NUL-terminated. */
static const uint8 *findMessage(const uint8 *data, const size_t datalen,
                                const uint32 msgnum, size_t *outlen)
{
    const uint16 count = readLE16(data + MSGFILE_COUNT_OFFSET);
    const uint16 first = readLE16(data + MSGFILE_FIRST_OFFSET);
    const uint16 format = readLE16(data + MSGFILE_FORMAT_OFFSET);
    const uint16 tableoff = readLE16(data + MSGFILE_TABLE_OFFSET);
    const uint16 tableend = readLE16(data + MSGFILE_TABLE_END_OFFSET);
    const int off32 = (format != MSGFILE_FORMAT_OFF16);
    const size_t entsize = off32 ? 4 : 2;

    if (msgnum < first)
        return NULL;
    const uint32 index = msgnum - first;
    if (index >= count)
        return NULL;
    if (tableoff >= datalen)
        return NULL;
    if (((size_t) tableoff + ((size_t) index + 1) * entsize) > datalen)
        return NULL;

    const uint8 *ent = data + tableoff + (size_t) index * entsize;
    const uint32 start = off32 ? readLE32(ent) : readLE16(ent);
    uint32 end;
    if ((index + 1) < count) {
        const uint8 *next = ent + entsize;
        end = off32 ? readLE32(next) : readLE16(next);
    } else {
        end = (uint32) datalen;  // last message runs to the end of the file.
        while ((end > start) && (data[end - 1] == '\0'))
            end--;
    }

    if ((start > datalen) || (end > datalen) || (end < start))
        return NULL;

    *outlen = (size_t) (end - start);
    return data + start;
}

/* Expand bare %1..%9 substitutions using the caller's pointer table. */
static APIRET formatMessage(const uint8 *msg, const size_t msglen,
                            PCHAR *pTable, const ULONG cTable,
                            PCHAR pBuf, const ULONG cbBuf, PULONG pcbMsg)
{
    ULONG out = 0;
    size_t i = 0;

    // Each on-disk record begins with a one-byte message class:
    //   'I' information, 'W' warning, 'E' error, 'S' severe, 'P' prompt,
    //   '?' empty. DosGetMessage does not return it.
    if ((msglen > 0) && (msg[0] >= '?') && (msg[0] <= 'Z'))
        i = 1;

    for (; i < msglen; i++) {
        const char ch = (char) msg[i];

        if (ch == '\\') {
            // Message files escape newlines and backslashes with a backslash.
            if ((i + 1) < msglen) {
                const char esc = (char) msg[++i];
                char outch = esc;
                if (esc == 'n')
                    outch = '\n';
                else if (esc == 'r')
                    outch = '\r';
                else if (esc == 't')
                    outch = '\t';
                if (out >= cbBuf)
                    return ERROR_MR_MSG_TOO_LONG;
                pBuf[out++] = outch;
                continue;
            }
        } else if ((ch == '%') && ((i + 1) < msglen) &&
                   (msg[i + 1] >= '1') && (msg[i + 1] <= '9')) {
            const ULONG arg = (ULONG) (msg[i + 1] - '1');
            // "%1$02d" is a positional printf specifier the caller expands
            //  itself; only a bare substitution uses the table.
            const int positional = ((i + 2) < msglen) && (msg[i + 2] == '$');
            if ((!positional) && (arg < cTable) && pTable && pTable[arg]) {
                const char *sub = pTable[arg];
                i++;
                while (*sub) {
                    if (out >= cbBuf)
                        return ERROR_MR_MSG_TOO_LONG;
                    pBuf[out++] = *(sub++);
                }
                continue;
            }
        }

        if (out >= cbBuf)
            return ERROR_MR_MSG_TOO_LONG;
        pBuf[out++] = ch;
    }

    if (pcbMsg)
        *pcbMsg = out;
    return NO_ERROR;
}

APIRET lx_message_get(PVOID msgseg, PVOID pTable, ULONG cTable, PCHAR pData,
                         ULONG cbBuf, ULONG msgnum, PCHAR pFilename, PULONG pcbMsg)
{
    TRACE_NATIVE("DosTrueGetMessage(%p, %p, %u, %p, %u, %u, '%s', %p)", msgseg, pTable, cTable, pData, cbBuf, msgnum, pFilename, pcbMsg);
    if ((!pData) || (!cbBuf) || (!pFilename) || (!*pFilename))
        return ERROR_INVALID_PARAMETER;

    size_t datalen = 0;
    uint8 *data = loadMessageFile(pFilename, &datalen);
    if (!data)
        return ERROR_MR_UN_ACC_MSGF;

    size_t msglen = 0;
    const uint8 *msg = findMessage(data, datalen, msgnum, &msglen);
    if (GLoaderState.trace_native) fprintf(stderr, "2INE TRACE: message file %s len=%u msgnum=%u found=%d\n", pFilename, (uint) datalen, (uint) msgnum, msg != NULL);
    if (!msg) {
        free(data);
        return ERROR_MR_MID_NOT_FOUND;
    }

    const APIRET rc = formatMessage(msg, msglen, (PCHAR *) pTable, cTable,
                                    pData, cbBuf, pcbMsg);
    free(data);
    return rc;
}

APIRET16 Dos16TrueGetMessage(PVOID pTable, USHORT cTable, PCHAR pData, USHORT cbBuf, USHORT msgnum, PCHAR pFilename, PUSHORT pcbMsg, PVOID msgseg)
{
    TRACE_NATIVE("Dos16TrueGetMessage(%p, %u, %p, %u, %u, %p, %p, %p)", pTable, (uint) cTable, pData, (uint) cbBuf, (uint) msgnum, pFilename, pcbMsg, msgseg);

    if ((!pData) || (!cbBuf) || (!pFilename) || (!*pFilename))
        return ERROR_INVALID_PARAMETER;

    size_t datalen = 0;
    uint8 *data = loadMessageFile(pFilename, &datalen);
    if (!data)
        return ERROR_MR_UN_ACC_MSGF;

    size_t msglen = 0;
    const uint8 *msg = findMessage(data, datalen, msgnum, &msglen);
    if (!msg) {
        free(data);
        return ERROR_MR_MID_NOT_FOUND;
    }

    // The 16-bit caller's substitution table entries are 16:16 pointers.
    PCHAR flatTable[9] = {0};
    LxGuestArgument substitutions[9] = {0};
    const uint16 ntable = (cTable < 9) ? cTable : 9;
    for (uint16 i = 0; i < ntable; i++) {
        uint32 pointer = lx_guest_load_u32((uint8 *) pTable + i * 4);
        if (!lx_guest_argument_strings(GLoaderState.execution, pointer, 1, 65536, 1, &substitutions[i])) {
            for (uint16 j = 0; j < i; j++) lx_guest_argument_finish(&substitutions[j], 0);
            free(data); return ERROR_INVALID_PARAMETER;
        }
        flatTable[i] = substitutions[i].host;
    }

    ULONG realLen = 0;
    const APIRET rc = formatMessage(msg, msglen, flatTable, ntable,
                                    pData, cbBuf, &realLen);
    if (pcbMsg)
        *pcbMsg = (USHORT) realLen;
    for (uint16 i = 0; i < ntable; i++) lx_guest_argument_finish(&substitutions[i], 0);
    free(data);
    return rc;
}
