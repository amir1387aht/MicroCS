/* MicroCS - the platform's BSD socket API for the network module (internal).
 * POSIX (Linux, macOS), ESP-IDF (lwIP), any lwIP with MCS_NET_LWIP=1 (LWIP_SOCKET 1,
 * LWIP_POSIX_SOCKETS_IO_NAMES 1, NO_SYS 0) and Zephyr (CONFIG_NET_SOCKETS + CONFIG_POSIX_API).
 * A stack without BSD sockets builds with MCS_ENABLE_SOCKETS=0 and keeps Wifi / EspNow / Ntp. */
#ifndef MCS_NET_SYS_H
#define MCS_NET_SYS_H
#include <errno.h>
#include <stdlib.h>
#if defined(ESP_PLATFORM)
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "lwip/sockets.h"
#define MCS__NET_NO_GETHOSTNAME 1
#elif MCS_NET_LWIP
#include <sys/time.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#define MCS__NET_NO_GETHOSTNAME 1
#elif defined(__ZEPHYR__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <zephyr/net/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define MCS__NET_NO_GETHOSTNAME 1
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#ifndef MCS_NET_SLICE_MS
#define MCS_NET_SLICE_MS 20
#endif
#ifdef MSG_NOSIGNAL
#define MCS__SEND_FLAGS MSG_NOSIGNAL
#else
#define MCS__SEND_FLAGS 0
#endif
#ifndef ECONNABORTED
#define ECONNABORTED ECONNRESET
#endif
#endif
