/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCP/IP routing tables and messages. The OS2_ prefix keeps these apart from the host's BSD names. */

#ifndef _INCL_NET_ROUTE_H_
#define _INCL_NET_ROUTE_H_

#include "os2/include/sys/socket.h"
#include "os2/include/net/if.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OS2_RTENTRY_COUNT 512

#define OS2_RTF_UP 0x1

#define OS2_RTF_GATEWAY 0x2

#define OS2_RTF_HOST 0x4

#define OS2_RTM_VERSION 4

#define OS2_RTM_GET 0x4

#define OS2_RTM_NEWADDR 0xc

#define OS2_RTM_IFINFO 0xe

#define OS2_RTA_DST 0x1

#define OS2_RTA_GATEWAY 0x2

#define OS2_RTA_NETMASK 0x4

#define OS2_RTA_GENMASK 0x8

#define OS2_RTA_IFP 0x10

#define OS2_RTA_IFA 0x20

#define OS2_RTA_AUTHOR 0x40

#define OS2_RTA_BRD 0x80

#pragma pack(push, 1)

    typedef struct OS2_rt_metrics
    {
        uint32_t rmx_locks;
        uint32_t rmx_mtu;
        uint32_t rmx_hopcount;
        uint32_t rmx_expire;
        uint32_t rmx_recvpipe;
        uint32_t rmx_sendpipe;
        uint32_t rmx_ssthresh;
        uint32_t rmx_rtt;
        uint32_t rmx_rttvar;
        uint32_t rmx_pksent;
        uint32_t rmx_filler[4];
    } OS2_rt_metrics;

    typedef struct OS2_ortentry
    {
        uint32_t rt_hash;
        OS2_sockaddr rt_dst;
        OS2_sockaddr rt_gateway;
        int16_t rt_flags;
        int16_t rt_refcnt;
        uint32_t rt_use;
        uint32_t rt_ifp;
        int32_t metric1;
        int32_t metric2;
        int32_t metric3;
        int32_t metric4;
    } OS2_ortentry;

    typedef struct OS2_rtentries
    {
        int16_t hostcount;
        int16_t netcount;
        OS2_ortentry rttable[OS2_RTENTRY_COUNT];
    } OS2_rtentries;

    typedef struct OS2_rt_msghdr
    {
        uint16_t rtm_msglen;
        uint8_t rtm_version;
        uint8_t rtm_type;
        uint16_t rtm_index;
        int16_t rtm_unit;
        int32_t rtm_flags;
        int32_t rtm_addrs;
        int32_t rtm_pid;
        int32_t rtm_seq;
        int32_t rtm_errno;
        int32_t rtm_use;
        uint32_t rtm_inits;
        uint32_t rtm_netmask;
        char rtm_name[OS2_IFNAMSIZ];
        OS2_rt_metrics rtm_rmx;
    } OS2_rt_msghdr;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
