/* Public domain. No rights reserved. Wholly AI-authored. */
/* MCI string commands for the digital-video device, parsed into the same
   parameter blocks mciSendCommand takes. */
#include "os2/mm/mmpm/mci/svmc/video.h"
#include <ctype.h>
#include <errno.h>

static int video_number(const char *text, ULONG *value)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno || !*text || *end || parsed > UINT32_MAX)
        return 0;
    *value = (ULONG)parsed;
    return 1;
}

static LxDgvDevice *video_alias(const char *text)
{
    ULONG id;
    if (video_number(text, &id) && id <= UINT16_MAX)
        return lx_dgv_find((USHORT)id);
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (lx_dgv_devices[i].id && !lx_dgv_devices[i].closing && *lx_dgv_devices[i].alias &&
            !strcasecmp(text, lx_dgv_devices[i].alias))
            return &lx_dgv_devices[i];
    return NULL;
}

static int video_coordinate(const char *text, ULONG *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 0);
    if (errno || !*text || *end || parsed < INT32_MIN || parsed > INT32_MAX)
        return 0;
    *value = (ULONG)(LONG)parsed;
    return 1;
}

static int video_tokens(char *text, char **tokens, unsigned *count)
{
    *count = 0;
    char *read = text, *write = text;
    while (*read)
    {
        while (isspace((unsigned char)*read))
            read++;
        if (!*read)
            break;
        if (*count == 24)
            return 0;
        tokens[(*count)++] = write;
        int quote = 0;
        while (*read && (quote || !isspace((unsigned char)*read)))
        {
            if (*read == '"')
            {
                quote = !quote;
                read++;
            }
            else
                *write++ = *read++;
        }
        if (quote)
            return 0;
        if (*read)
            read++;
        *write++ = 0;
    }
    return *count > 0;
}

/* Scratch guest block for one command: the parameter block at its start,
   element names from STRING_NAME and the alias from STRING_ALIAS. */
enum
{
    STRING_BLOCK_SIZE = 0x3000,
    STRING_NAME = 256,
    STRING_ALIAS = 8192
};

ULONG lx_mci_video_string(const char *command, char *output, USHORT length, ULONG window,
                          USHORT user, int *handled)
{
    *handled = 0;
    if (!command || strlen(command) > 65536)
        return MCIERR_INVALID_BUFFER;
    char *text = strdup(command), *tokens[24];
    unsigned count = 0;
    if (!text)
        return MCIERR_OUT_OF_MEMORY;
    if (!video_tokens(text, tokens, &count))
    {
        free(text);
        return MCIERR_INVALID_BUFFER;
    }
    int opening = !strcasecmp(tokens[0], "open");
    LxDgvDevice *d = count >= 2 && !opening ? video_alias(tokens[1]) : NULL;
    int empty =
        opening && count >= 2 &&
        (!strcasecmp(tokens[1], "digitalvideo") || !strcasecmp(tokens[1], "digitalvideo01"));
    int video = empty || (opening && count >= 2 && lx_dgv_video_name(tokens[1]));
    for (unsigned i = 2; opening && i + 1 < count; i++)
        if (!strcasecmp(tokens[i], "type") && !strcasecmp(tokens[i + 1], "digitalvideo"))
            video = 1;
    if (!video && !d)
    {
        free(text);
        return 0;
    }
    *handled = 1;
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, STRING_BLOCK_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
    {
        free(text);
        return MCIERR_OUT_OF_MEMORY;
    }
    uint8 *p = lx_execution_mapping_host(mapping);
    ULONG base = lx_execution_mapping_address(mapping), flags = 0,
          result = MCIERR_UNSUPPORTED_FUNCTION;
    USHORT message = 0, id = d ? d->id : 0;
    lx_guest_store_u32(p + MCI_PARMS_CALLBACK, window);
    if (opening)
    {
        if (strlen(tokens[1]) >= 4096)
        {
            result = MCIERR_INVALID_BUFFER;
            goto done;
        }
        strcpy((char *)p + STRING_NAME, tokens[1]);
        lx_guest_store_u32(p + MCI_OPEN_PARMS_DEVICE_TYPE, MCI_DEVTYPE_DIGITAL_VIDEO);
        lx_guest_store_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME, base + STRING_NAME);
        flags = MCI_OPEN_TYPE_ID | (empty ? 0 : MCI_OPEN_ELEMENT);
        for (unsigned i = 2; i < count; i++)
        {
            if (!strcasecmp(tokens[i], "type") && i + 1 < count)
                i++;
            else if (!strcasecmp(tokens[i], "alias") && i + 1 < count)
            {
                if (strlen(tokens[++i]) >= 128 || video_alias(tokens[i]))
                {
                    result = MCIERR_INVALID_FLAG;
                    goto done;
                }
                strcpy((char *)p + STRING_ALIAS, tokens[i]);
                lx_guest_store_u32(p + MCI_OPEN_PARMS_ALIAS, base + STRING_ALIAS);
                flags |= MCI_OPEN_ALIAS;
            }
            else if (!strcasecmp(tokens[i], "wait"))
                flags |= MCI_WAIT;
            else if (!strcasecmp(tokens[i], "shareable"))
                flags |= MCI_OPEN_SHAREABLE;
            else if (!strcasecmp(tokens[i], "readonly"))
                flags |= MCI_READONLY;
            else
            {
                result = MCIERR_INVALID_FLAG;
                goto done;
            }
        }
        message = MCI_OPEN;
    }
    else if (!strcasecmp(tokens[0], "load") && (count == 3 || count == 4))
    {
        if (strlen(tokens[2]) >= 4096 || (count == 4 && strcasecmp(tokens[3], "wait")))
        {
            result = MCIERR_INVALID_FLAG;
            goto done;
        }
        strcpy((char *)p + STRING_NAME, tokens[2]);
        lx_guest_store_u32(p + MCI_LOAD_PARMS_ELEMENT_NAME, base + STRING_NAME);
        message = MCI_LOAD;
        flags = count == 4 ? MCI_WAIT : 0;
    }
    else if (!strcasecmp(tokens[0], "status") && count == 3)
    {
        message = MCI_STATUS;
        flags = MCI_STATUS_ITEM;
        ULONG item = !strcasecmp(tokens[2], "length")     ? MCI_STATUS_LENGTH
                     : !strcasecmp(tokens[2], "mode")     ? MCI_STATUS_MODE
                     : !strcasecmp(tokens[2], "position") ? MCI_STATUS_POSITION
                     : !strcasecmp(tokens[2], "ready")    ? MCI_STATUS_READY
                                                          : 0;
        if (!item)
        {
            result = MCIERR_UNSUPPORTED_FUNCTION;
            goto done;
        }
        lx_guest_store_u32(p + MCI_STATUS_PARMS_ITEM, item);
    }
    else if (!strcasecmp(tokens[0], "set") && count == 5 && !strcasecmp(tokens[2], "time") &&
             !strcasecmp(tokens[3], "format"))
    {
        ULONG format = !strcasecmp(tokens[4], "milliseconds") ? MCI_FORMAT_MILLISECONDS
                       : !strcasecmp(tokens[4], "frames")     ? MCI_FORMAT_FRAMES
                                                              : 0;
        if (!format)
        {
            result = MCIERR_INVALID_FLAG;
            goto done;
        }
        message = MCI_SET;
        flags = MCI_SET_TIME_FORMAT;
        lx_guest_store_u32(p + MCI_SET_PARMS_TIME_FORMAT, format);
    }
    else if (!strcasecmp(tokens[0], "window") && (count == 4 || count == 5) &&
             !strcasecmp(tokens[2], "handle"))
    {
        ULONG handle;
        if (!video_number(tokens[3], &handle) || (count == 5 && strcasecmp(tokens[4], "wait")))
        {
            result = MCIERR_INVALID_BUFFER;
            goto done;
        }
        message = MCI_WINDOW;
        flags = MCI_DGV_WINDOW_HWND | (count == 5 ? MCI_WAIT : 0);
        lx_guest_store_u32(p + MCI_VID_WINDOW_PARMS_HWND_DEST, handle);
    }
    else if (!strcasecmp(tokens[0], "put") && (count == 8 || count == 9) &&
             !strcasecmp(tokens[2], "destination") && !strcasecmp(tokens[3], "at"))
    {
        if (count == 9 && strcasecmp(tokens[8], "wait"))
        {
            result = MCIERR_INVALID_FLAG;
            goto done;
        }
        for (unsigned i = 0; i < 4; i++)
        {
            ULONG value;
            if (!video_coordinate(tokens[i + 4], &value) || (i >= 2 && (LONG)value <= 0))
            {
                result = MCIERR_OUTOFRANGE;
                goto done;
            }
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_X + i * 4, value);
        }
        message = MCI_PUT;
        flags = MCI_DGV_PUT_RECT | MCI_DGV_PUT_DESTINATION | (count == 9 ? MCI_WAIT : 0);
    }
    else
    {
        message = !strcasecmp(tokens[0], "play")     ? MCI_PLAY
                  : !strcasecmp(tokens[0], "pause")  ? MCI_PAUSE
                  : !strcasecmp(tokens[0], "resume") ? MCI_RESUME
                  : !strcasecmp(tokens[0], "stop")   ? MCI_STOP
                  : !strcasecmp(tokens[0], "close")  ? MCI_CLOSE
                  : !strcasecmp(tokens[0], "seek")   ? MCI_SEEK
                                                     : 0;
        if (!message)
            goto done;
        for (unsigned i = 2; i < count; i++)
        {
            if (!strcasecmp(tokens[i], "wait"))
                flags |= MCI_WAIT;
            else if (!strcasecmp(tokens[i], "notify"))
                flags |= MCI_NOTIFY;
            else if (message == MCI_PLAY &&
                     (!strcasecmp(tokens[i], "from") || !strcasecmp(tokens[i], "to")) &&
                     i + 1 < count)
            {
                int from = !strcasecmp(tokens[i], "from");
                ULONG value;
                if (!video_number(tokens[++i], &value))
                {
                    result = MCIERR_INVALID_BUFFER;
                    goto done;
                }
                flags |= from ? MCI_FROM : MCI_TO;
                lx_guest_store_u32(p + (from ? MCI_PLAY_PARMS_FROM : MCI_PLAY_PARMS_TO), value);
            }
            else if (message == MCI_SEEK && !strcasecmp(tokens[i], "to") && i + 1 < count)
            {
                if (!strcasecmp(tokens[++i], "start"))
                    flags |= MCI_TO_START;
                else if (!strcasecmp(tokens[i], "end"))
                    flags |= MCI_TO_END;
                else
                {
                    ULONG value;
                    if (!video_number(tokens[i], &value))
                    {
                        result = MCIERR_INVALID_BUFFER;
                        goto done;
                    }
                    lx_guest_store_u32(p + MCI_SEEK_PARMS_TO, value);
                }
            }
            else
            {
                result = MCIERR_INVALID_FLAG;
                goto done;
            }
        }
    }
    {
        int accepted = 0;
        result = lx_mci_video_command(id, message, flags, p, user, &accepted);
        if (!accepted)
            result = MCIERR_INVALID_DEVICE_ID;
    }
    if (!result && output && length)
    {
        int written = 0;
        if (message == MCI_OPEN)
            written =
                snprintf(output, length, "%u", lx_guest_load_u16(p + MCI_OPEN_PARMS_DEVICE_ID));
        else if (message == MCI_STATUS)
        {
            ULONG value = lx_guest_load_u32(p + MCI_STATUS_PARMS_RETURN);
            if (lx_guest_load_u32(p + MCI_STATUS_PARMS_ITEM) == MCI_STATUS_MODE)
                written = snprintf(output, length, "%s",
                                   value == MCI_MODE_PLAY        ? "playing"
                                   : value == MCI_MODE_PAUSE     ? "paused"
                                   : value == MCI_MODE_NOT_READY ? "not ready"
                                                                 : "stopped");
            else
                written = snprintf(output, length, "%u", value);
        }
        else
            output[0] = 0;
        if (written < 0 || written >= length)
            result = MCIERR_INVALID_BUFFER;
    }
done:
    lx_execution_unmap(GProcessState.execution, mapping);
    free(text);
    return result;
}
