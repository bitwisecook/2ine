/* Public domain. No rights reserved. Wholly AI-authored. */
/* Socket error recording shared by TCPIP32 and SO32DLL. */
#include "os2/mptn/common/socket_errors.h"
#include "host/socket.h"

int lx_mptn_socket_error(int *error, int code)
{
    *error = code;
    return -1;
}

int lx_mptn_socket_result(int *error, int result)
{
    return result < 0 ? lx_mptn_socket_error(error, SOCBASEERR + lx_host_socket_bsd_error())
                      : result;
}
