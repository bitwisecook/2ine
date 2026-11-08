/* Public domain. No rights reserved. Wholly AI-authored. */
/* MCI text commands share the same device state and checked wire parameters. */
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

static VideoDevice *video_alias(const char *text)
{
    ULONG id;
    if (video_number(text, &id) && id <= UINT16_MAX)
        return find((USHORT)id);
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (devices[i].id && !devices[i].closing && *devices[i].alias &&
            !strcasecmp(text, devices[i].alias))
            return &devices[i];
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

ULONG lx_mci_video_string(const char *command, char *output, USHORT length, ULONG window,
                          USHORT user, int *handled)
{
    *handled = 0;
    if (!command || strlen(command) > 65536)
        return V_ERR_BUFFER;
    char *text = strdup(command), *tokens[24];
    unsigned count = 0;
    if (!text)
        return V_ERR_NOMEM;
    if (!video_tokens(text, tokens, &count))
    {
        free(text);
        return V_ERR_BUFFER;
    }
    int opening = !strcasecmp(tokens[0], "open");
    VideoDevice *d = count >= 2 && !opening ? video_alias(tokens[1]) : NULL;
    int empty =
        opening && count >= 2 &&
        (!strcasecmp(tokens[1], "digitalvideo") || !strcasecmp(tokens[1], "digitalvideo01"));
    int video = empty || (opening && count >= 2 && video_name(tokens[1]));
    for (unsigned i = 2; opening && i + 1 < count; i++)
        if (!strcasecmp(tokens[i], "type") && !strcasecmp(tokens[i + 1], "digitalvideo"))
            video = 1;
    if (!video && !d)
    {
        free(text);
        return 0;
    }
    *handled = 1;
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 0x3000, 3, 0);
    if (!mapping)
    {
        free(text);
        return V_ERR_NOMEM;
    }
    uint8 *p = lx_execution_mapping_host(mapping);
    ULONG base = lx_execution_mapping_address(mapping), flags = 0, result = V_ERR_UNSUPPORTED;
    USHORT message = 0, id = d ? d->id : 0;
    lx_guest_store_u32(p, window);
    if (opening)
    {
        if (strlen(tokens[1]) >= 4096)
        {
            result = V_ERR_BUFFER;
            goto done;
        }
        strcpy((char *)p + 256, tokens[1]);
        lx_guest_store_u32(p + 8, 12);
        lx_guest_store_u32(p + 12, base + 256);
        flags = V_OPEN_TYPE_ID | (empty ? 0 : V_OPEN_ELEMENT);
        for (unsigned i = 2; i < count; i++)
        {
            if (!strcasecmp(tokens[i], "type") && i + 1 < count)
                i++;
            else if (!strcasecmp(tokens[i], "alias") && i + 1 < count)
            {
                if (strlen(tokens[++i]) >= 128 || video_alias(tokens[i]))
                {
                    result = V_ERR_FLAG;
                    goto done;
                }
                strcpy((char *)p + 8192, tokens[i]);
                lx_guest_store_u32(p + 16, base + 8192);
                flags |= 0x200;
            }
            else if (!strcasecmp(tokens[i], "wait"))
                flags |= V_WAIT;
            else if (!strcasecmp(tokens[i], "shareable"))
                flags |= 0x2000;
            else if (!strcasecmp(tokens[i], "readonly"))
                flags |= 0x8000;
            else
            {
                result = V_ERR_FLAG;
                goto done;
            }
        }
        message = V_OPEN;
    }
    else if (!strcasecmp(tokens[0], "load") && (count == 3 || count == 4))
    {
        if (strlen(tokens[2]) >= 4096 || (count == 4 && strcasecmp(tokens[3], "wait")))
        {
            result = V_ERR_FLAG;
            goto done;
        }
        strcpy((char *)p + 256, tokens[2]);
        lx_guest_store_u32(p + 4, base + 256);
        message = V_LOAD;
        flags = count == 4 ? V_WAIT : 0;
    }
    else if (!strcasecmp(tokens[0], "status") && count == 3)
    {
        message = V_STATUS;
        flags = 0x100;
        ULONG item = !strcasecmp(tokens[2], "length")     ? 2
                     : !strcasecmp(tokens[2], "mode")     ? 3
                     : !strcasecmp(tokens[2], "position") ? 5
                     : !strcasecmp(tokens[2], "ready")    ? 9
                                                          : 0;
        if (!item)
        {
            result = V_ERR_UNSUPPORTED;
            goto done;
        }
        lx_guest_store_u32(p + 8, item);
    }
    else if (!strcasecmp(tokens[0], "set") && count == 5 && !strcasecmp(tokens[2], "time") &&
             !strcasecmp(tokens[3], "format"))
    {
        ULONG format = !strcasecmp(tokens[4], "milliseconds") ? V_FORMAT_MS
                       : !strcasecmp(tokens[4], "frames")     ? V_FORMAT_FRAMES
                                                              : 0;
        if (!format)
        {
            result = V_ERR_FLAG;
            goto done;
        }
        message = V_SET;
        flags = 0x10000;
        lx_guest_store_u32(p + 4, format);
    }
    else if (!strcasecmp(tokens[0], "window") && (count == 4 || count == 5) &&
             !strcasecmp(tokens[2], "handle"))
    {
        ULONG handle;
        if (!video_number(tokens[3], &handle) || (count == 5 && strcasecmp(tokens[4], "wait")))
        {
            result = V_ERR_BUFFER;
            goto done;
        }
        message = V_WINDOW;
        flags = 0x100 | (count == 5 ? V_WAIT : 0);
        lx_guest_store_u32(p + 4, handle);
    }
    else if (!strcasecmp(tokens[0], "put") && (count == 8 || count == 9) &&
             !strcasecmp(tokens[2], "destination") && !strcasecmp(tokens[3], "at"))
    {
        if (count == 9 && strcasecmp(tokens[8], "wait"))
        {
            result = V_ERR_FLAG;
            goto done;
        }
        for (unsigned i = 0; i < 4; i++)
        {
            ULONG value;
            if (!video_coordinate(tokens[i + 4], &value) || (i >= 2 && (LONG)value <= 0))
            {
                result = V_ERR_RANGE;
                goto done;
            }
            lx_guest_store_u32(p + 4 + i * 4, value);
        }
        message = V_PUT;
        flags = 0x300 | (count == 9 ? V_WAIT : 0);
    }
    else
    {
        message = !strcasecmp(tokens[0], "play")     ? V_PLAY
                  : !strcasecmp(tokens[0], "pause")  ? V_PAUSE
                  : !strcasecmp(tokens[0], "resume") ? V_RESUME
                  : !strcasecmp(tokens[0], "stop")   ? V_STOP
                  : !strcasecmp(tokens[0], "close")  ? V_CLOSE
                  : !strcasecmp(tokens[0], "seek")   ? V_SEEK
                                                     : 0;
        if (!message)
            goto done;
        for (unsigned i = 2; i < count; i++)
        {
            if (!strcasecmp(tokens[i], "wait"))
                flags |= V_WAIT;
            else if (!strcasecmp(tokens[i], "notify"))
                flags |= V_NOTIFY;
            else if (message == V_PLAY &&
                     (!strcasecmp(tokens[i], "from") || !strcasecmp(tokens[i], "to")) &&
                     i + 1 < count)
            {
                int from = !strcasecmp(tokens[i], "from");
                ULONG value;
                if (!video_number(tokens[++i], &value))
                {
                    result = V_ERR_BUFFER;
                    goto done;
                }
                flags |= from ? V_FROM : V_TO;
                lx_guest_store_u32(p + (from ? 4 : 8), value);
            }
            else if (message == V_SEEK && !strcasecmp(tokens[i], "to") && i + 1 < count)
            {
                if (!strcasecmp(tokens[++i], "start"))
                    flags |= 0x100;
                else if (!strcasecmp(tokens[i], "end"))
                    flags |= 0x200;
                else
                {
                    ULONG value;
                    if (!video_number(tokens[i], &value))
                    {
                        result = V_ERR_BUFFER;
                        goto done;
                    }
                    lx_guest_store_u32(p + 4, value);
                }
            }
            else
            {
                result = V_ERR_FLAG;
                goto done;
            }
        }
    }
    {
        int accepted = 0;
        result = lx_mci_video_command(id, message, flags, p, user, &accepted);
        if (!accepted)
            result = V_ERR_DEVICE;
    }
    if (!result && output && length)
    {
        int written = 0;
        if (message == V_OPEN)
            written = snprintf(output, length, "%u", lx_guest_load_u16(p + 4));
        else if (message == V_STATUS)
        {
            ULONG value = lx_guest_load_u32(p + 4);
            if (lx_guest_load_u32(p + 8) == 3)
                written = snprintf(output, length, "%s",
                                   value == V_MODE_PLAY        ? "playing"
                                   : value == V_MODE_PAUSE     ? "paused"
                                   : value == V_MODE_NOT_READY ? "not ready"
                                                               : "stopped");
            else
                written = snprintf(output, length, "%u", value);
        }
        else
            output[0] = 0;
        if (written < 0 || written >= length)
            result = V_ERR_BUFFER;
    }
done:
    lx_execution_unmap(GLoaderState.execution, mapping);
    free(text);
    return result;
}
