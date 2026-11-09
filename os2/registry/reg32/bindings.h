/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest strings are copied; output buffers are borrowed for the call. Invalid
   guest memory returns ERROR_INVALID_PARAMETER without touching Open32's last
   error, which REGISTRY does not own. */
static char *registry_string(uint32 pointer)
{
    return pointer ? lx_execution_copy_string(GProcessState.execution, pointer, 32768) : NULL;
}

static BOOL registry_span(ULONG address, uint64_t size, unsigned access, BOOL optional,
                          LxGuestBorrow *borrow)
{
    if (!address && (optional || !size))
        return TRUE;
    return size <= 16u * 1024u * 1024u && size <= UINT32_MAX &&
           (uint64_t)address + size <= (UINT64_C(1) << 32) &&
           lx_execution_borrow(GProcessState.execution, address, (size_t)size, access, borrow);
}

/* A null subkey stays null. */
static BOOL registry_subkey(ULONG address, PSZ *output)
{
    *output =
        address ? lx_execution_copy_string(GProcessState.execution, address, 1024u * 1024u) : NULL;
    return !address || *output;
}

static uint32 bridge32_RegCloseKey(uint8 *a)
{
    return (uint32)RegCloseKey(lx_guest_load_u32(a));
}

static uint32 bridge32_RegOpenKey(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = registry_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8),
                                                  4, LX_GUEST_WRITE, &output))
    {
        ULONG handle = 0;
        result = (uint32)RegOpenKey(lx_guest_load_u32(a), name, &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_RegCreateKey(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = registry_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8),
                                                  4, LX_GUEST_WRITE, &output))
    {
        ULONG handle = 0;
        result = (uint32)RegCreateKey(lx_guest_load_u32(a), name, &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_RegOpenKeyEx(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = registry_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 16), 4, LX_GUEST_WRITE,
                            &output))
    {
        ULONG handle = 0;
        result = (uint32)RegOpenKeyEx(lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8),
                                      lx_guest_load_u32(a + 12), &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_RegCreateKeyEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), cp = lx_guest_load_u32(a + 12),
                 dp = lx_guest_load_u32(a + 32);
    char *name = registry_string(np), *class_name = registry_string(cp);
    LxGuestBorrow output = {0}, disposition = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) && (!cp || class_name) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 28), 4, LX_GUEST_WRITE,
                            &output) &&
        (!dp || lx_execution_borrow(GProcessState.execution, dp, 4, LX_GUEST_WRITE, &disposition)))
    {
        ULONG handle = 0, created = 0;
        result = (uint32)RegCreateKeyEx(
            lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8), class_name,
            lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20),
            (void *)(uintptr_t)lx_guest_load_u32(a + 24), &handle, dp ? &created : NULL);
        if (!result)
        {
            lx_guest_store_u32(output.host, handle);
            if (dp)
                lx_guest_store_u32(disposition.host, created);
        }
    }
    lx_execution_release(&disposition);
    lx_execution_release(&output);
    free(class_name);
    free(name);
    return result;
}

static uint32 bridge32_RegDeleteValue(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = registry_string(pointer);
    uint32 result = pointer && !name ? ERROR_INVALID_PARAMETER
                                     : (uint32)RegDeleteValue(lx_guest_load_u32(a), name);
    free(name);
    return result;
}

static uint32 bridge32_RegDeleteKey(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = registry_string(pointer);
    uint32 result = pointer && !name ? ERROR_INVALID_PARAMETER
                                     : (uint32)RegDeleteKey(lx_guest_load_u32(a), name);
    free(name);
    return result;
}

static uint32 bridge32_RegSetValueEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), dp = lx_guest_load_u32(a + 16),
                 size = lx_guest_load_u32(a + 20);
    char *name = registry_string(np);
    LxGuestBorrow data = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) &&
        (!size || lx_execution_borrow(GProcessState.execution, dp, size, LX_GUEST_READ, &data)))
        result = (uint32)RegSetValueEx(lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8),
                                       lx_guest_load_u32(a + 12), data.host, size);
    lx_execution_release(&data);
    free(name);
    return result;
}

static uint32 bridge32_RegQueryValueEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), rp = lx_guest_load_u32(a + 8),
                 tp = lx_guest_load_u32(a + 12), dp = lx_guest_load_u32(a + 16),
                 sp = lx_guest_load_u32(a + 20);
    char *name = registry_string(np);
    LxGuestBorrow type = {0}, size = {0}, data = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) && !rp &&
        (!tp || lx_execution_borrow(GProcessState.execution, tp, 4, LX_GUEST_WRITE, &type)) &&
        (!sp || lx_execution_borrow(GProcessState.execution, sp, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                                    &size)))
    {
        ULONG count = sp ? lx_guest_load_u32(size.host) : 0, kind = 0;
        uint8 empty = 0;
        if ((!dp || sp) &&
            (!dp || !count ||
             lx_execution_borrow(GProcessState.execution, dp, count, LX_GUEST_WRITE, &data)))
        {
            result = (uint32)RegQueryValueEx(lx_guest_load_u32(a), name, NULL, tp ? &kind : NULL,
                                             dp ? (count ? data.host : &empty) : NULL,
                                             sp ? &count : NULL);
            if (result == NO_ERROR || result == ERROR_MORE_DATA)
            {
                if (sp)
                    lx_guest_store_u32(size.host, count);
                if (tp)
                    lx_guest_store_u32(type.host, kind);
            }
        }
    }
    lx_execution_release(&data);
    lx_execution_release(&size);
    lx_execution_release(&type);
    free(name);
    return result;
}

static uint32 bridge32_RegEnumValue(uint8 *a)
{
    uint32 np = lx_guest_load_u32(a + 8), sp = lx_guest_load_u32(a + 12),
           tp = lx_guest_load_u32(a + 20), dp = lx_guest_load_u32(a + 24),
           cp = lx_guest_load_u32(a + 28);
    LxGuestBorrow name = {0}, length = {0}, type = {0}, data = {0}, size = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (!np || lx_guest_load_u32(a + 16) ||
        !lx_execution_borrow(GProcessState.execution, sp, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                             &length) ||
        (tp && !lx_execution_borrow(GProcessState.execution, tp, 4, LX_GUEST_WRITE, &type)) ||
        (cp && !lx_execution_borrow(GProcessState.execution, cp, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                                    &size)))
        goto done;
    ULONG characters = lx_guest_load_u32(length.host),
          count = cp ? lx_guest_load_u32(size.host) : 0, kind = 0;
    if (characters > 32767)
    {
        result = ERROR_MORE_DATA;
        goto done;
    }
    if ((characters &&
         !lx_execution_borrow(GProcessState.execution, np, characters, LX_GUEST_WRITE, &name)) ||
        (dp && (!cp || (count && !lx_execution_borrow(GProcessState.execution, dp, count,
                                                      LX_GUEST_WRITE, &data)))))
        goto done;
    char empty = 0;
    result =
        (uint32)RegEnumValue(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                             characters ? name.host : &empty, &characters, NULL, tp ? &kind : NULL,
                             dp ? (count ? data.host : &empty) : NULL, cp ? &count : NULL);
    if (!result || result == ERROR_MORE_DATA)
    {
        lx_guest_store_u32(length.host, characters);
        if (cp)
            lx_guest_store_u32(size.host, count);
        if (tp)
            lx_guest_store_u32(type.host, kind);
    }
done:
    lx_execution_release(&size);
    lx_execution_release(&data);
    lx_execution_release(&type);
    lx_execution_release(&length);
    lx_execution_release(&name);
    return result;
}

static uint32 bridge32_RegEnumKeyEx(uint8 *a)
{
    uint32 np = lx_guest_load_u32(a + 8), sp = lx_guest_load_u32(a + 12);
    uint32 cp = lx_guest_load_u32(a + 20), cs = lx_guest_load_u32(a + 24);
    uint32 tp = lx_guest_load_u32(a + 28), result = ERROR_INVALID_PARAMETER;
    LxGuestBorrow name = {0}, size = {0}, class_name = {0}, class_size = {0}, timestamp = {0};
    if (!np || lx_guest_load_u32(a + 16) ||
        !lx_execution_borrow(GProcessState.execution, sp, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                             &size) ||
        (cp && !cs) ||
        (cs && !lx_execution_borrow(GProcessState.execution, cs, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                                    &class_size)) ||
        (tp && !lx_execution_borrow(GProcessState.execution, tp, 8, LX_GUEST_WRITE, &timestamp)))
        goto done;
    ULONG length = lx_guest_load_u32(size.host);
    ULONG class_length = cs ? lx_guest_load_u32(class_size.host) : 0;
    if (length > 32768 || class_length > 32768)
    {
        result = ERROR_MORE_DATA;
        goto done;
    }
    if ((length &&
         !lx_execution_borrow(GProcessState.execution, np, length, LX_GUEST_WRITE, &name)) ||
        (cp && class_length &&
         !lx_execution_borrow(GProcessState.execution, cp, class_length, LX_GUEST_WRITE,
                              &class_name)))
        goto done;
    char empty = 0;
    result = (uint32)RegEnumKeyEx(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                  length ? name.host : &empty, &length, NULL,
                                  cp ? (class_length ? class_name.host : &empty) : NULL,
                                  cs ? &class_length : NULL, timestamp.host);
    if (!result)
    {
        lx_guest_store_u32(size.host, length);
        if (cs)
            lx_guest_store_u32(class_size.host, class_length);
    }
done:
    lx_execution_release(&timestamp);
    lx_execution_release(&class_size);
    lx_execution_release(&class_name);
    lx_execution_release(&size);
    lx_execution_release(&name);
    return result;
}

static uint32 bridge32_RegEnumKey(uint8 *a)
{
    uint32 capacity = lx_guest_load_u32(a + 12);
    LxGuestBorrow output = {0};
    if (!lx_guest_load_u32(a + 8))
        return ERROR_INVALID_PARAMETER;
    if (capacity > 32768)
        return ERROR_MORE_DATA;
    if (capacity && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8),
                                         capacity, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    char empty = 0;
    uint32 result = (uint32)RegEnumKey(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                       capacity ? output.host : &empty, capacity);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_RegQueryInfoKey(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    ULONG v7 = lx_guest_load_u32(a + 28);
    ULONG v8 = lx_guest_load_u32(a + 32);
    ULONG v9 = lx_guest_load_u32(a + 36);
    ULONG v10 = lx_guest_load_u32(a + 40);
    ULONG v11 = lx_guest_load_u32(a + 44);
    LxGuestBorrow b2 = {0};
    ULONG n2 = 0;
    if (v2)
    {
        valid = registry_span(v2, 4, 3, FALSE, &b2) && valid;
        if (b2.host)
            n2 = lx_guest_load_u32(b2.host);
    }
    LxGuestBorrow b3 = {0};
    ULONG n3 = 0;
    if (v3)
    {
        valid = registry_span(v3, 4, 3, FALSE, &b3) && valid;
        if (b3.host)
            n3 = lx_guest_load_u32(b3.host);
    }
    LxGuestBorrow b4 = {0};
    ULONG n4 = 0;
    if (v4)
    {
        valid = registry_span(v4, 4, 3, FALSE, &b4) && valid;
        if (b4.host)
            n4 = lx_guest_load_u32(b4.host);
    }
    LxGuestBorrow b5 = {0};
    ULONG n5 = 0;
    if (v5)
    {
        valid = registry_span(v5, 4, 3, FALSE, &b5) && valid;
        if (b5.host)
            n5 = lx_guest_load_u32(b5.host);
    }
    LxGuestBorrow b6 = {0};
    ULONG n6 = 0;
    if (v6)
    {
        valid = registry_span(v6, 4, 3, FALSE, &b6) && valid;
        if (b6.host)
            n6 = lx_guest_load_u32(b6.host);
    }
    LxGuestBorrow b7 = {0};
    ULONG n7 = 0;
    if (v7)
    {
        valid = registry_span(v7, 4, 3, FALSE, &b7) && valid;
        if (b7.host)
            n7 = lx_guest_load_u32(b7.host);
    }
    LxGuestBorrow b8 = {0};
    ULONG n8 = 0;
    if (v8)
    {
        valid = registry_span(v8, 4, 3, FALSE, &b8) && valid;
        if (b8.host)
            n8 = lx_guest_load_u32(b8.host);
    }
    LxGuestBorrow b9 = {0};
    ULONG n9 = 0;
    if (v9)
    {
        valid = registry_span(v9, 4, 3, FALSE, &b9) && valid;
        if (b9.host)
            n9 = lx_guest_load_u32(b9.host);
    }
    LxGuestBorrow b10 = {0};
    ULONG n10 = 0;
    if (v10)
    {
        valid = registry_span(v10, 4, 3, FALSE, &b10) && valid;
        if (b10.host)
            n10 = lx_guest_load_u32(b10.host);
    }
    LxGuestBorrow b1 = {0};
    valid = registry_span(v1, n2, 2, TRUE, &b1) && valid;
    LxGuestBorrow b11 = {0};
    valid = registry_span(v11, 8, 2, TRUE, &b11) && valid;
    if (valid)
        result = (ULONG)RegQueryInfoKey((ULONG)v0, b1.host, v2 ? &n2 : NULL, v3 ? &n3 : NULL,
                                        v4 ? &n4 : NULL, v5 ? &n5 : NULL, v6 ? &n6 : NULL,
                                        v7 ? &n7 : NULL, v8 ? &n8 : NULL, v9 ? &n9 : NULL,
                                        v10 ? &n10 : NULL, b11.host);
    if (b2.host)
        lx_guest_store_u32(b2.host, n2);
    if (b3.host)
        lx_guest_store_u32(b3.host, n3);
    if (b4.host)
        lx_guest_store_u32(b4.host, n4);
    if (b5.host)
        lx_guest_store_u32(b5.host, n5);
    if (b6.host)
        lx_guest_store_u32(b6.host, n6);
    if (b7.host)
        lx_guest_store_u32(b7.host, n7);
    if (b8.host)
        lx_guest_store_u32(b8.host, n8);
    if (b9.host)
        lx_guest_store_u32(b9.host, n9);
    if (b10.host)
        lx_guest_store_u32(b10.host, n10);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    lx_execution_release(&b4);
    lx_execution_release(&b5);
    lx_execution_release(&b6);
    lx_execution_release(&b7);
    lx_execution_release(&b8);
    lx_execution_release(&b9);
    lx_execution_release(&b10);
    lx_execution_release(&b11);
    return result;
}

static uint32 bridge32_RegQueryValue(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b3 = {0};
    ULONG n3 = 0;
    if (v3)
    {
        valid = registry_span(v3, 4, 3, FALSE, &b3) && valid;
        if (b3.host)
            n3 = lx_guest_load_u32(b3.host);
    }
    PSZ s1 = NULL;
    valid = registry_subkey(v1, &s1) && valid;
    LxGuestBorrow b2 = {0};
    valid = registry_span(v2, n3, 2, TRUE, &b2) && valid;
    if (valid)
        result = (ULONG)RegQueryValue((ULONG)v0, s1, b2.host, v3 ? &n3 : NULL);
    if (b3.host)
        lx_guest_store_u32(b3.host, n3);
    free(s1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_RegSetValue(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s1 = NULL;
    valid = registry_subkey(v1, &s1) && valid;
    LxGuestBorrow b3 = {0};
    valid = registry_span(v3, v4, 1, FALSE, &b3) && valid;
    if (valid)
        result = (ULONG)RegSetValue((ULONG)v0, s1, (ULONG)v2, b3.host, (ULONG)v4);
    free(s1);
    lx_execution_release(&b3);
    return result;
}
