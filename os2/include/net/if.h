/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCP/IP network interfaces. The OS2_ prefix keeps these apart from the host's BSD names. */

#ifndef _INCL_NET_IF_H_
#define _INCL_NET_IF_H_

#include "os2/include/sys/socket.h"
#include "os2/include/sys/time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OS2_IFNAMSIZ 16

#define OS2_IFMIB_ENTRIES 42

#define OS2_OPERSTATUS_UP 0x1

#define OS2_OPERSTATUS_DOWN 0x2

#define OS2_OPERSTATUS_TESTING 0x3

#pragma pack(push, 1)

    typedef struct OS2_iftable
    {
        int16_t iftIndex;
        char iftDescr[45];
        int16_t iftType;
        int16_t iftMtu;
        char iftPhysAddr[6];
        int16_t iftOperStatus;
        uint32_t iftSpeed;
        uint32_t iftLastChange;
        uint32_t iftInOctets;
        uint32_t iftOutOctets;
        uint32_t iftOutDiscards;
        uint32_t iftInDiscards;
        uint32_t iftInErrors;
        uint32_t iftOutErrors;
        uint32_t iftInUnknownProtos;
        uint32_t iftInUcastPkts;
        uint32_t iftOutUcastPkts;
        uint32_t iftInNUcastPkts;
        uint32_t iftOutNUcastPkts;
    } OS2_iftable;

    typedef struct OS2_ifmib
    {
        int16_t ifNumber;
        OS2_iftable iftable[OS2_IFMIB_ENTRIES];
    } OS2_ifmib;

    typedef struct OS2_if_data
    {
        uint8_t ifi_type;
        uint8_t ifi_addrlen;
        uint8_t ifi_hdrlen;
        uint32_t ifi_mtu;
        uint32_t ifi_metric;
        uint32_t ifi_baudrate;
        uint32_t ifi_collisions;
        uint32_t ifi_ibytes;
        uint32_t ifi_obytes;
        uint32_t ifi_oqdrops;
        uint32_t ifi_iqdrops;
        uint32_t ifi_ierrors;
        uint32_t ifi_oerrors;
        uint32_t ifi_noproto;
        uint32_t ifi_ipackets;
        uint32_t ifi_opackets;
        uint32_t ifi_imcasts;
        uint32_t ifi_omcasts;
        int16_t ifi_OperStatus;
        OS2_timeval ifi_lastchange;
        uint8_t ifi_descr[45];
    } OS2_if_data;

    typedef struct OS2_if_msghdr
    {
        uint16_t ifm_msglen;
        uint8_t ifm_version;
        uint8_t ifm_type;
        int32_t ifm_addrs;
        int32_t ifm_flags;
        uint16_t ifm_index;
        OS2_if_data ifm_data;
    } OS2_if_msghdr;

    typedef struct OS2_ifa_msghdr
    {
        uint16_t ifam_msglen;
        uint8_t ifam_version;
        uint8_t ifam_type;
        int32_t ifam_addrs;
        int32_t ifam_flags;
        uint16_t ifam_index;
        int32_t ifam_metric;
    } OS2_ifa_msghdr;

    typedef struct OS2_ifreq
    {
        char ifr_name[OS2_IFNAMSIZ];

        union
        {
            OS2_sockaddr ifru_addr;
            OS2_sockaddr ifru_dstaddr;
            OS2_sockaddr ifru_broadaddr;
            int32_t ifru_flags;
            int32_t ifru_metric;
            uint32_t ifru_data;
        } ifr_ifru;
    } OS2_ifreq;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
