/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCPIP32 flat 32-bit ABI: socket, select, ioctl and resolver bridges. */
static int tcpip_borrow(LxExecution *execution, uint32 address, size_t size, unsigned flags,
                        LxGuestBorrow *borrow)
{
    if (lx_execution_borrow(execution, address, size, flags, borrow))
        return 1;
    lx_tcpip_socket_failure(SOCEFAULT);
    return 0;
}

static int tcpip_read(LxExecution *execution, uint32 address, void *output, size_t size)
{
    if (lx_execution_read(execution, address, output, size))
        return 1;
    lx_tcpip_socket_failure(SOCEFAULT);
    return 0;
}

static uint32 bridge32_OS2_sysctl(uint8 *a)
{
    uint32 count = lx_guest_load_u32(a + 4), result = (uint32)-1;
    if (lx_guest_load_u32(a + 16) || lx_guest_load_u32(a + 20))
    {
        errno = EPERM;
        return lx_tcpip_socket_result(-1);
    }
    if (count > 16)
    {
        errno = EINVAL;
        return lx_tcpip_socket_result(-1);
    }
    LxGuestBorrow name = {0}, length = {0}, output = {0};
    if (!tcpip_borrow(GProcessState.execution, lx_guest_load_u32(a), count * 4, LX_GUEST_READ,
                      &name) ||
        !tcpip_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4,
                      LX_GUEST_READ | LX_GUEST_WRITE, &length))
    {
        errno = EFAULT;
        goto done;
    }
    size_t n = lx_guest_load_u32(length.host);
    if (lx_guest_load_u32(a + 8) &&
        (n > 16 * 1024 * 1024 || !tcpip_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), n,
                                               LX_GUEST_WRITE, &output)))
    {
        errno = EFAULT;
        goto done;
    }
    result = OS2_sysctl(name.host, count, output.host, &n, NULL, 0);
    lx_guest_store_u32(length.host, (uint32)n);
done:
    lx_execution_release(&output);
    lx_execution_release(&length);
    lx_execution_release(&name);
    return lx_tcpip_socket_result((int32_t)result);
}

static uint32 tcpip_ioctl(uint8 *a, int has_length)
{
    uint32 command = lx_guest_load_u32(a + 4);
    uint32 length = has_length ? lx_guest_load_u32(a + 12) : (command >> 16) & 0x1fff;
    uint32 result = (uint32)-1;
    if (length > INT32_MAX)
        return lx_tcpip_socket_failure(SOCEINVAL);
    LxGuestBorrow outer = {0}, entries = {0};
    if (!tcpip_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), length,
                      LX_GUEST_READ | LX_GUEST_WRITE, &outer))
        return result;
    unsigned cmd = command & 0xffff;
    if ((cmd == 0x6914 || cmd == 0x6924) && length >= 8)
    {
        uint32 capacity = lx_guest_load_u32(outer.host);
        size_t used;
        if (capacity <= INT32_MAX &&
            tcpip_borrow(GProcessState.execution, lx_guest_load_u32((uint8 *)outer.host + 4),
                         capacity, LX_GUEST_WRITE, &entries))
        {
            result =
                lx_tcpip_socket_result(lx_os2_network_ifconf(entries.host, capacity, &used, 1));
            if (!result)
                lx_guest_store_u32(outer.host, (uint32)used);
        }
    }
    else
        result = OS2_os2_ioctl(lx_guest_load_u32(a), command, outer.host, length);
    lx_execution_release(&entries);
    lx_execution_release(&outer);
    return result;
}

static uint32 bridge32_OS2_ioctl(uint8 *a)
{
    return tcpip_ioctl(a, 0);
}

static uint32 bridge32_OS2_os2_ioctl(uint8 *a)
{
    return tcpip_ioctl(a, 1);
}

#include "os2/mptn/common/bindings_resolver.h"

static uint32 tcpip_word(const uint8 *a, unsigned i)
{
    return lx_guest_load_u32(a + i * 4);
}

static uint32 tcpip_host(OS2_hostent *h)
{
    socket_guest_error(*OS2_tcp_h_errno1());
    if (!h)
        return 0;
    LxHostSocketHost host = {h->h_name, h->h_aliases, h->h_addrtype, h->h_length, h->h_addr_list};
    return socket_guest_host(&host);
}

static uint32 tcpip_protocol(OS2_protoent *p)
{
    return p ? socket_guest_database(p->p_name, p->p_aliases, p->p_proto, NULL) : 0;
}

static uint32 tcpip_service(OS2_servent *p)
{
    return p ? socket_guest_database(p->s_name, p->s_aliases, p->s_port, p->s_proto) : 0;
}

static uint32 bridge32_OS2_socket(uint8 *a)
{
    return OS2_socket(tcpip_word(a, 0), tcpip_word(a, 1), tcpip_word(a, 2));
}

static uint32 bridge32_OS2_soclose(uint8 *a)
{
    return OS2_soclose(tcpip_word(a, 0));
}

static uint32 bridge32_OS2_sock_errno(uint8 *a)
{
    (void)a;
    return OS2_sock_errno();
}

static uint32 bridge32_OS2_sock_init(uint8 *a)
{
    (void)a;
    return OS2_sock_init();
}

static uint32 bridge32_OS2_res_init(uint8 *a)
{
    (void)a;
    return OS2_res_init();
}

static uint32 bridge32_OS2_gethostid(uint8 *a)
{
    (void)a;
    return OS2_gethostid();
}

static uint32 bridge32_OS2_shutdown(uint8 *a)
{
    return OS2_shutdown(tcpip_word(a, 0), tcpip_word(a, 1));
}

static uint32 bridge32_OS2_htons(uint8 *a)
{
    return OS2_htons((uint16_t)tcpip_word(a, 0));
}

static uint32 bridge32_OS2_ntohs(uint8 *a)
{
    return OS2_ntohs((uint16_t)tcpip_word(a, 0));
}

static uint32 bridge32_OS2_htonl(uint8 *a)
{
    return OS2_htonl(tcpip_word(a, 0));
}

static uint32 bridge32_OS2_ntohl(uint8 *a)
{
    return OS2_ntohl(tcpip_word(a, 0));
}

static uint32 bridge32_OS2_setprotoent(uint8 *a)
{
    OS2_setprotoent(tcpip_word(a, 0));
    return 0;
}

static uint32 bridge32_OS2_endprotoent(uint8 *a)
{
    (void)a;
    OS2_endprotoent();
    return 0;
}

static uint32 bridge32_OS2_setservent(uint8 *a)
{
    OS2_setservent(tcpip_word(a, 0));
    return 0;
}

static uint32 bridge32_OS2_endservent(uint8 *a)
{
    (void)a;
    OS2_endservent();
    return 0;
}

static uint32 bridge32_OS2_inet_ntoa(uint8 *a)
{
    OS2_in_addr ip = {tcpip_word(a, 0)};
    return socket_guest_text(OS2_inet_ntoa(ip));
}

static uint32 bridge32_OS2_gethostbyname(uint8 *a)
{
    char *text = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    uint32 result = tcpip_host(OS2_gethostbyname(text));
    free(text);
    return result;
}

static uint32 bridge32_OS2_gethostbyaddr(uint8 *a)
{
    uint32 address;
    if (tcpip_word(a, 1) != 4 ||
        !tcpip_read(GProcessState.execution, tcpip_word(a, 0), &address, 4))
        return tcpip_host(OS2_gethostbyaddr(NULL, 0, 0));
    return tcpip_host(OS2_gethostbyaddr(&address, 4, tcpip_word(a, 2)));
}

static uint32 bridge32_OS2_getprotobyname(uint8 *a)
{
    char *text = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    uint32 result = text ? tcpip_protocol(OS2_getprotobyname(text)) : 0;
    free(text);
    return result;
}

static uint32 bridge32_OS2_getprotobynumber(uint8 *a)
{
    return tcpip_protocol(OS2_getprotobynumber(tcpip_word(a, 0)));
}

static uint32 bridge32_OS2_inet_addr(uint8 *a)
{
    char *text = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    uint32 result = text ? (uint32)OS2_inet_addr(text) : UINT32_MAX;
    free(text);
    return result;
}

static uint32 bridge32_OS2_psock_errno(uint8 *a)
{
    char *text = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    OS2_psock_errno(text);
    free(text);
    return 0;
}

static uint32 bridge32_OS2_getservbyname(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    char *protocol = tcpip_word(a, 1)
                         ? lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 1), 64)
                         : NULL;
    uint32 result = name && (!tcpip_word(a, 1) || protocol)
                        ? tcpip_service(OS2_getservbyname(name, protocol))
                        : 0;
    free(name);
    free(protocol);
    return result;
}

static uint32 bridge32_OS2_getservbyport(uint8 *a)
{
    char *protocol = tcpip_word(a, 1)
                         ? lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 1), 64)
                         : NULL;
    uint32 result = !tcpip_word(a, 1) || protocol
                        ? tcpip_service(OS2_getservbyport(tcpip_word(a, 0), protocol))
                        : 0;
    free(protocol);
    return result;
}

static uint32 bridge32_OS2_gethostname(uint8 *a)
{
    LxGuestBorrow b = {0};
    uint32 result = (uint32)-1, n = tcpip_word(a, 1);
    if (n && n <= INT32_MAX &&
        tcpip_borrow(GProcessState.execution, tcpip_word(a, 0), n, LX_GUEST_WRITE, &b))
        result = OS2_gethostname(b.host, n);
    lx_execution_release(&b);
    return result;
}

static uint32 bridge32_OS2_gettimeofday(uint8 *a)
{
    LxGuestBorrow time = {0}, zone = {0};
    uint32 result = (uint32)-1;
    if (tcpip_word(a, 0) &&
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 0), 8, LX_GUEST_WRITE, &time))
        goto done;
    if (tcpip_word(a, 1) &&
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), 8, LX_GUEST_WRITE, &zone))
        goto done;
    result = OS2_gettimeofday(time.host, zone.host);
done:
    lx_execution_release(&time);
    lx_execution_release(&zone);
    return result;
}

static uint32 tcpip_address_input(uint8 *a, int connecting)
{
    OS2_sockaddr address;
    if (tcpip_word(a, 2) < 16 ||
        !tcpip_read(GProcessState.execution, tcpip_word(a, 1), &address, 16))
        return (uint32)-1;
    return connecting ? OS2_connect(tcpip_word(a, 0), &address, 16)
                      : OS2_bind(tcpip_word(a, 0), &address, 16);
}

static uint32 bridge32_OS2_bind(uint8 *a)
{
    return tcpip_address_input(a, 0);
}

static uint32 bridge32_OS2_connect(uint8 *a)
{
    return tcpip_address_input(a, 1);
}

static uint32 tcpip_address_output(uint8 *a, int peer)
{
    LxGuestBorrow address = {0}, count = {0};
    uint32 result = (uint32)-1;
    if (!tcpip_borrow(GProcessState.execution, tcpip_word(a, 2), 4, LX_GUEST_READ | LX_GUEST_WRITE,
                      &count))
        goto done;
    int n = lx_guest_load_u32(count.host);
    if (n < 16 ||
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), 16, LX_GUEST_WRITE, &address))
        goto done;
    result = peer ? OS2_getpeername(tcpip_word(a, 0), address.host, &n)
                  : OS2_getsockname(tcpip_word(a, 0), address.host, &n);
    if (!result)
        lx_guest_store_u32(count.host, n);
done:
    lx_execution_release(&address);
    lx_execution_release(&count);
    return result;
}

static uint32 bridge32_OS2_getsockname(uint8 *a)
{
    return tcpip_address_output(a, 0);
}

static uint32 bridge32_OS2_getpeername(uint8 *a)
{
    return tcpip_address_output(a, 1);
}

static uint32 bridge32_OS2_sendto(uint8 *a)
{
    OS2_sockaddr address;
    LxGuestBorrow b = {0};
    uint32 n = tcpip_word(a, 2), result = (uint32)-1;
    if (n <= INT32_MAX && tcpip_word(a, 5) >= 16 &&
        tcpip_read(GProcessState.execution, tcpip_word(a, 4), &address, 16) &&
        tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), n, LX_GUEST_READ, &b))
        result = OS2_sendto(tcpip_word(a, 0), b.host, n, tcpip_word(a, 3), &address, 16);
    lx_execution_release(&b);
    return result;
}

static uint32 bridge32_OS2_recvfrom(uint8 *a)
{
    LxGuestBorrow b = {0}, address = {0}, count = {0};
    uint32 n = tcpip_word(a, 2), result = (uint32)-1;
    if (n > INT32_MAX ||
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), n, LX_GUEST_WRITE, &b))
        goto done;
    int length = 16;
    if (tcpip_word(a, 4))
    {
        if (!tcpip_borrow(GProcessState.execution, tcpip_word(a, 5), 4,
                          LX_GUEST_READ | LX_GUEST_WRITE, &count))
            goto done;
        length = lx_guest_load_u32(count.host);
        if (length < 16 ||
            !tcpip_borrow(GProcessState.execution, tcpip_word(a, 4), 16, LX_GUEST_WRITE, &address))
            goto done;
    }
    result = OS2_recvfrom(tcpip_word(a, 0), b.host, n, tcpip_word(a, 3), address.host, &length);
    if ((int32_t)result >= 0 && count.host)
        lx_guest_store_u32(count.host, length);
done:
    lx_execution_release(&b);
    lx_execution_release(&address);
    lx_execution_release(&count);
    return result;
}

static uint32 tcpip_transfer(uint8 *a, int writing)
{
    LxGuestBorrow b = {0};
    uint32 n = tcpip_word(a, 2), result = (uint32)-1;
    if (n <= INT32_MAX && tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), n,
                                       writing ? LX_GUEST_READ : LX_GUEST_WRITE, &b))
        result = writing ? OS2_send(tcpip_word(a, 0), b.host, n, tcpip_word(a, 3))
                         : OS2_recv(tcpip_word(a, 0), b.host, n, tcpip_word(a, 3));
    lx_execution_release(&b);
    return result;
}

static uint32 bridge32_OS2_send(uint8 *a)
{
    return tcpip_transfer(a, 1);
}

static uint32 bridge32_OS2_recv(uint8 *a)
{
    return tcpip_transfer(a, 0);
}

static uint32 bridge32_OS2_setsockopt(uint8 *a)
{
    LxGuestBorrow b = {0};
    uint32 n = tcpip_word(a, 4), result = (uint32)-1;
    if (n <= 65536 && tcpip_borrow(GProcessState.execution, tcpip_word(a, 3), n, LX_GUEST_READ, &b))
        result = OS2_setsockopt(tcpip_word(a, 0), tcpip_word(a, 1), tcpip_word(a, 2), b.host, n);
    lx_execution_release(&b);
    return result;
}

static uint32 bridge32_OS2_getsockopt(uint8 *a)
{
    LxGuestBorrow b = {0}, count = {0};
    uint32 result = (uint32)-1;
    if (!tcpip_borrow(GProcessState.execution, tcpip_word(a, 4), 4, LX_GUEST_READ | LX_GUEST_WRITE,
                      &count))
        goto done;
    int n = lx_guest_load_u32(count.host);
    if (n < 0 || n > 65536 ||
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 3), n, LX_GUEST_WRITE, &b))
        goto done;
    result = OS2_getsockopt(tcpip_word(a, 0), tcpip_word(a, 1), tcpip_word(a, 2), b.host, &n);
    if (!result)
        lx_guest_store_u32(count.host, n);
done:
    lx_execution_release(&b);
    lx_execution_release(&count);
    return result;
}

static uint32 bridge32_OS2_os2_select(uint8 *a)
{
    int reads = tcpip_word(a, 1), writes = tcpip_word(a, 2), errors = tcpip_word(a, 3);
    if (reads < 0 || writes < 0 || errors < 0 || reads > 1024 || writes > 1024 || errors > 1024)
        return lx_tcpip_socket_failure(SOCEINVAL);
    LxGuestBorrow b = {0};
    uint32 result = (uint32)-1;
    if (tcpip_borrow(GProcessState.execution, tcpip_word(a, 0), (reads + writes + errors) * 4,
                     LX_GUEST_READ | LX_GUEST_WRITE, &b))
        result = OS2_os2_select(b.host, reads, writes, errors, (int32_t)tcpip_word(a, 4));
    lx_execution_release(&b);
    return result;
}

static uint32 bridge32_OS2_select(uint8 *a)
{
    LxGuestBorrow sets[3] = {{0}}, time = {0};
    uint32 result = (uint32)-1;
    for (unsigned i = 0; i < 3; i++)
        if (tcpip_word(a, i + 1))
        {
            if (!tcpip_borrow(GProcessState.execution, tcpip_word(a, i + 1), sizeof(OS2_fd_set),
                              LX_GUEST_READ | LX_GUEST_WRITE, &sets[i]))
                goto done;
            if (lx_guest_load_u16(sets[i].host) > OS2_FD_SETSIZE)
            {
                lx_tcpip_socket_failure(SOCEINVAL);
                goto done;
            }
        }
    if (tcpip_word(a, 4) &&
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 4), 8, LX_GUEST_READ, &time))
        goto done;
    result = OS2_select(tcpip_word(a, 0), sets[0].host, sets[1].host, sets[2].host, time.host);
done:
    for (unsigned i = 0; i < 3; i++)
        lx_execution_release(&sets[i]);
    lx_execution_release(&time);
    return result;
}

static uint32 bridge32_OS2_writev(uint8 *a)
{
    uint32 count = tcpip_word(a, 2), result = (uint32)-1;
    if (count > 1024)
        return lx_tcpip_socket_failure(SOCEINVAL);
    OS2_iovec *vectors = calloc(count ? count : 1, sizeof *vectors);
    LxGuestBorrow *buffers = calloc(count ? count : 1, sizeof *buffers), array = {0};
    if (!vectors || !buffers ||
        !tcpip_borrow(GProcessState.execution, tcpip_word(a, 1), count * 8, LX_GUEST_READ, &array))
        goto done;
    for (unsigned i = 0; i < count; i++)
    {
        uint8 *v = (uint8 *)array.host + i * 8;
        uint32 n = lx_guest_load_u32(v + 4);
        if (n > 16 * 1024 * 1024 || !tcpip_borrow(GProcessState.execution, lx_guest_load_u32(v), n,
                                                  LX_GUEST_READ, &buffers[i]))
            goto done;
        vectors[i].buffer = buffers[i].host;
        vectors[i].length = n;
    }
    result = OS2_writev(tcpip_word(a, 0), vectors, count);
done:
    if (buffers)
        for (unsigned i = 0; i < count; i++)
            lx_execution_release(&buffers[i]);
    lx_execution_release(&array);
    free(buffers);
    free(vectors);
    return result;
}

static uint32 bridge32_OS2_inet_network(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, tcpip_word(a, 0), 1024);
    uint32 result = name ? OS2_inet_network(name) : UINT32_MAX;
    free(name);
    return result;
}

static uint32 bridge32_OS2_tcp_h_errno1(uint8 *a)
{
    (void)a;
    return socket_guest_error(*OS2_tcp_h_errno1());
}
