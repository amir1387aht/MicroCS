/* lwIP configuration of the MicroCS Pico W / Pico 2 W firmware (-DMICROCS_WIFI=ON):
 * FreeRTOS (NO_SYS = 0) with BSD sockets for TcpClient / TcpListener / UdpClient / Http /
 * HttpServer / WebSocket, DHCP client, DNS (+ .local names over mDNS), SNTP, mDNS responder,
 * raw ICMP for Ping and IGMP for multicast. A copy next to your own project overrides it. */
#ifndef MCS_RP2_LWIPOPTS_H
#define MCS_RP2_LWIPOPTS_H

#define NO_SYS                          0
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    1
#define LWIP_COMPAT_SOCKETS             1
#define LWIP_POSIX_SOCKETS_IO_NAMES     1
#define LWIP_SOCKET_SELECT              1
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1
#define LWIP_SO_RCVBUF                  1
#define LWIP_FIONREAD_LINUXMODE         1
#define SO_REUSE                        1
#define LWIP_TIMEVAL_PRIVATE            0     /* struct timeval from <sys/time.h> */
#define LWIP_ERRNO_STDINCLUDE           1     /* errno / E* from <errno.h> */
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1
#define LWIP_NETIF_API                  1

#define MEM_LIBC_MALLOC                 0
#define MEM_ALIGNMENT                   4
#if defined(PICO_RP2350) && PICO_RP2350        /* Pico 2 W: 520 KB of SRAM */
#define MCS_LWIP_BIG                    1
#else                                           /* Pico W: 264 KB shared with the VM heap */
#define MCS_LWIP_BIG                    0
#endif
#ifndef MEM_SIZE
#define MEM_SIZE                        (MCS_LWIP_BIG ? 32 * 1024 : 16 * 1024)
#endif
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_ARP_QUEUE              10
#define MEMP_NUM_NETCONN                16
#define MEMP_NUM_TCP_PCB                10
#define MEMP_NUM_TCP_PCB_LISTEN         6
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_RAW_PCB                2
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_SYS_TIMEOUT            (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#define PBUF_POOL_SIZE                  (MCS_LWIP_BIG ? 24 : 12)

#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_IPV4                       1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DNS                        1
#define LWIP_DHCP                       1
#define LWIP_IGMP                       1
#define LWIP_TCP_KEEPALIVE              1
#define LWIP_DNS_SUPPORT_MDNS_QUERIES   1
#define DNS_MAX_SERVERS                 2
#define LWIP_DHCP_DOES_ACD_CHECK        0
#define DHCP_DOES_ARP_CHECK             0
#define LWIP_DHCP_GET_NTP_SRV           0

#define TCP_MSS                         1460
#define TCP_WND                         ((MCS_LWIP_BIG ? 8 : 4) * TCP_MSS)
#define TCP_SND_BUF                     ((MCS_LWIP_BIG ? 8 : 4) * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG              1

#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_TX_SINGLE_PBUF       1
#define LWIP_NUM_NETIF_CLIENT_DATA      1     /* mDNS */
#define LWIP_CHKSUM_ALGORITHM           3

/* FreeRTOS threads and mailboxes */
#define TCPIP_THREAD_STACKSIZE          2048
#define TCPIP_THREAD_PRIO               3     /* above the VM task (1), below the cyw43 driver (4) */
#define DEFAULT_THREAD_STACKSIZE        1024
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_ACCEPTMBOX_SIZE         8

/* SNTP (Ntp.Start): sets the C library clock (time(), DateTime.Now) */
#define SNTP_SERVER_DNS                 1
#define SNTP_MAX_SERVERS                3
#define SNTP_STARTUP_DELAY              0
void mcs_rp2_sntp_set_time(unsigned long sec, unsigned long us);
#define SNTP_SET_SYSTEM_TIME_US(sec, us) mcs_rp2_sntp_set_time((unsigned long)(sec), (unsigned long)(us))

/* mDNS responder (Mdns.Start / AddService) */
#define LWIP_MDNS_RESPONDER             1
#define MDNS_MAX_SERVICES               4
#define LWIP_MDNS_SEARCH                0

#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0
#define LWIP_DEBUG                      0
#endif
