/* MicroCS - BSD socket headers for the network module (POSIX hosts, ESP-IDF's lwIP, or a
 * port's lwIP with MCS_NET_LWIP = 1, e.g. the Pico W). Only for mcs_net_sock.c / mcs_net_srv.c:
 * lwIP's socket names are macros (close, read, write ...). */
#ifndef MCS_NET_POSIX_H
#define MCS_NET_POSIX_H
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#if MCS_NET_LWIP && !defined(ESP_PLATFORM)
/* a bare lwIP (NO_SYS = 0, LWIP_SOCKET, LWIP_COMPAT_SOCKETS, LWIP_POSIX_SOCKETS_IO_NAMES) */
#include <sys/time.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#ifndef TCP_NODELAY
#include "lwip/tcp.h"
#endif
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#if defined(ESP_PLATFORM)
#include "lwip/sockets.h"
#else
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#endif
#endif
#endif
