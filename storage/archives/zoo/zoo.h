/* Public domain. No rights reserved. Wholly AI-authored. */
/* Zoo directory/codec contracts cross-checked against Zoo 2.10 and Deark. */
#define ZOO_SIGNATURE UINT32_C(0xfdc4a7dc)

static int zoo_walk(TbReadStream *file, Visit visit, void *context)
{
    unsigned char global[34];
    if (!tb_stream_read_at(file, 0, global, sizeof global) || u32(global + 20) != ZOO_SIGNATURE ||
        u32(global + 28) != (uint32_t)(0u - u32(global + 24)))
        return bad(EINVAL);
    uint64_t at = u32(global + 24), length = tb_stream_size(file);
    uint64_t *seen = calloc(262144, sizeof *seen);
    if (!seen)
        return 0;
    int result = 0;
    for (unsigned count = 0; count < MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[56];
        if (at < 34 || !tb_stream_read_at(file, at, h, 51) || u32(h) != ZOO_SIGNATURE)
            break;
        size_t slot = (size_t)((at * UINT64_C(11400714819323198485)) >> 46);
        while (seen[slot] && seen[slot] != at)
            slot = (slot + 1) & 262143;
        if (seen[slot] == at)
            break;
        seen[slot] = at;
        uint32_t next = u32(h + 6), data = u32(h + 10), packed = u32(h + 24);
        if (!next)
        {
            result = 1;
            break;
        }
        if (h[4] < 1 || h[4] > 2 || h[5] > 2 || next >= length || data > length ||
            packed > length - data)
            break;
        char short_name[14];
        memcpy(short_name, h + 38, 13);
        short_name[13] = 0;
        char *name = NULL;
        int name_attempted = 0;
        if (h[4] == 2)
        {
            if (!tb_stream_read_at(file, at, h, 56))
                break;
            unsigned variable = u16(h + 51);
            unsigned char *header = malloc(56 + (size_t)variable);
            int ok = header && tb_stream_read_at(file, at, header, 56 + (size_t)variable);
            if (ok)
            {
                uint16_t expected = u16(header + 54);
                header[54] = header[55] = 0;
                ok = legacy_crc16(header, 56 + variable) == expected;
            }
            if (ok && variable >= 2)
            {
                unsigned names = header[56], dirs = header[57];
                if (names + dirs > variable - 2)
                    ok = 0;
                else
                {
                    char joined[515];
                    size_t used = 0;
                    if (dirs)
                    {
                        size_t n = strnlen((char *)header + 58 + names, dirs);
                        memcpy(joined, header + 58 + names, n);
                        used = n;
                        if (used && joined[used - 1] != '/' && joined[used - 1] != '\\')
                            joined[used++] = '/';
                    }
                    const char *base = names ? (char *)header + 58 : short_name;
                    size_t n = names ? strnlen(base, names) : strlen(base);
                    memcpy(joined + used, base, n);
                    joined[used + n] = 0;
                    name_attempted = 1;
                    name = member_name(joined, 0);
                }
            }
            free(header);
            if (!ok)
                break;
        }
        if (!name_attempted)
            name = member_name(short_name, 0);
        if (!h[30] && name && *name)
        {
            int rc = visit(context, name, u32(h + 20), 0, file, at, packed, 96 + h[5],
                           tb_dos_time(u16(h + 14), u16(h + 16)));
            free(name);
            if (rc != 1)
            {
                result = rc;
                break;
            }
        }
        else
            free(name);
        at = next;
    }
    free(seen);
    return result ? result : bad(EINVAL);
}

static unsigned zoo_lsb(LegacyBits *bits, unsigned count)
{
    if (!bits->valid || count > 13 || bits->bit > bits->size * 8 ||
        count > bits->size * 8 - bits->bit)
    {
        bits->valid = 0;
        return 0;
    }
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value |= ((bits->data[bits->bit / 8] >> (bits->bit % 8)) & 1u) << i;
    return value;
}

static int zoo_lzd(const unsigned char *input, size_t packed, unsigned char *output, size_t size)
{
    uint16_t prefix[8192];
    unsigned char suffix[8192], stack[8193];
    LegacyBits bits = {input, packed, 0, 1};
    unsigned width = 9, next = 258, previous = UINT_MAX, first = 0;
    size_t used = 0;
    while (bits.valid)
    {
        unsigned code = zoo_lsb(&bits, width), original = code;
        if (!bits.valid)
            break;
        if (code == 256)
        {
            width = 9;
            next = 258;
            previous = UINT_MAX;
            continue;
        }
        if (code == 257)
            return used == size;
        if (code > next || code >= 8192)
            return 0;
        if (previous == UINT_MAX)
        {
            if (code > 255 || used == size)
                return 0;
            output[used++] = (unsigned char)code;
            previous = code;
            first = code;
            continue;
        }
        unsigned depth = 0;
        if (code == next)
        {
            stack[depth++] = (unsigned char)first;
            code = previous;
        }
        while (code > 255)
        {
            if (code < 258 || code >= next || depth >= 8192)
                return 0;
            stack[depth++] = suffix[code];
            code = prefix[code];
        }
        first = code;
        stack[depth++] = (unsigned char)first;
        if (depth > size - used)
            return 0;
        while (depth)
            output[used++] = stack[--depth];
        if (next < 8192)
        {
            prefix[next] = (uint16_t)previous;
            suffix[next++] = (unsigned char)first;
            if (next >= (1u << width) && width < 13)
                width++;
        }
        previous = original;
    }
    return 0;
}

static TbReadStream *zoo_member(TbReadStream *file, uint64_t offset, uint64_t packed, uint64_t size,
                                int kind)
{
    unsigned char h[51];
    if (packed > LEGACY_ARCHIVE_LIMIT || size > LEGACY_ARCHIVE_LIMIT)
    {
        bad(EFBIG);
        return NULL;
    }
    if (!tb_stream_read_at(file, offset, h, sizeof h) || u32(h) != ZOO_SIGNATURE ||
        u32(h + 24) != packed || u32(h + 20) != size)
        return NULL;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(file, u32(h + 10), input, (size_t)packed);
    if (ok && kind == 96)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && kind == 97)
        ok = zoo_lzd(input, (size_t)packed, output, (size_t)size);
    else if (ok && kind == 98)
        ok = legacy_arj_lzh(input, (size_t)packed, output, (size_t)size, 13);
    else
        ok = 0;
    free(input);
    if (!ok || legacy_crc16(output, (size_t)size) != u16(h + 18))
    {
        free(output);
        bad(EIO);
        return NULL;
    }
    media_session.stats.decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
