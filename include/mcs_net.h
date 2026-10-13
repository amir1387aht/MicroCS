/*
 * MicroCS - Wi-Fi and networking (optional module, MCS_ENABLE_NET).
 *
 * C# classes (docs/NETWORK.md):
 *   Wifi          station, access point (soft-AP) and both at once: connect, scan, events,
 *                 static IP / DHCP, hostname, power save, TX power, country, protocols,
 *                 saved credentials, WPA2-Enterprise, connected clients ...
 *   EspNow        Espressif's connection-less peer-to-peer radio protocol
 *   Dns  Ping     name resolution, ICMP echo
 *   TcpClient  TcpListener  UdpClient       BSD sockets (TLS: TcpClient.ConnectTls)
 *   Http  HttpResponse                      HTTP/1.1 client, HTTPS where the port has TLS
 *   Ntp           SNTP time synchronisation + time zone
 *   Mdns          multicast DNS: answer to <name>.local, advertise services
 *
 * Like the HAL, the board describes its network with one table of C functions
 * (mcs_net_t). A NULL entry means "not available": the C# class is not registered
 * and Hal.Has("Wifi") / Net.Has("EspNow") return false. The sockets, DNS and the
 * HTTP client need no table - they use the BSD socket API of the platform (lwIP
 * on ESP-IDF, the OS on Linux / macOS), compiled in with MCS_ENABLE_SOCKETS.
 *
 *   mcs_net_set(&my_net);           // before the VM starts (the ESP32 port does it for you)
 *   mcs_hal_open_lib(vm, &hal);     // opens the network classes too
 *   mcs_net_open_lib(vm, &my_net);  // ... or without a HAL
 *
 * Every function returns >= 0 on success or a negative MCS_HAL_E* code
 * (mcs_hal.h): ENOTSUP, ETIMEOUT, ENODEV, EBUSY, EINVAL, ERR. Calls happen on the
 * VM's thread; only mcs_net_post() may be called from any task.
 *
 * Asynchronous news (connected, got an IP, a station joined the AP, an ESP-NOW
 * packet arrived ...) reaches C# as MCS_HAL_EV_NET events through the HAL event
 * queue and runs the Wifi.OnEvent / EspNow.OnReceive callbacks in script context,
 * exactly like GPIO interrupts.
 */
#ifndef MCS_NET_H
#define MCS_NET_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MCS_NET_API_VERSION 2   /* 2: provision (WPS, SmartConfig) */

/* ---- Wi-Fi modes (Wifi.Mode) */
enum { MCS_WIFI_OFF = 0, MCS_WIFI_STA = 1, MCS_WIFI_AP = 2, MCS_WIFI_APSTA = 3 };

/* ---- station status (Wifi.Status; numbers as in Arduino's wl_status_t) */
enum { MCS_WIFI_IDLE = 0, MCS_WIFI_NO_SSID = 1, MCS_WIFI_SCAN_DONE = 2, MCS_WIFI_CONNECTED = 3,
       MCS_WIFI_CONNECT_FAILED = 4, MCS_WIFI_CONNECTION_LOST = 5, MCS_WIFI_DISCONNECTED = 6,
       MCS_WIFI_CONNECTING = 7, MCS_WIFI_WRONG_PASSWORD = 8, MCS_WIFI_STOPPED = 9 };

/* ---- security (Wifi.Auth*; numbers as in ESP-IDF's wifi_auth_mode_t) */
enum { MCS_WIFI_AUTH_OPEN = 0, MCS_WIFI_AUTH_WEP, MCS_WIFI_AUTH_WPA_PSK, MCS_WIFI_AUTH_WPA2_PSK,
       MCS_WIFI_AUTH_WPA_WPA2_PSK, MCS_WIFI_AUTH_ENTERPRISE, MCS_WIFI_AUTH_WPA3_PSK,
       MCS_WIFI_AUTH_WPA2_WPA3_PSK, MCS_WIFI_AUTH_WAPI_PSK, MCS_WIFI_AUTH_OWE,
       MCS_WIFI_AUTH_WPA3_ENT_192, MCS_WIFI_AUTH__COUNT };

/* ---- disconnect reasons (Wifi.DisconnectReason; IEEE 802.11 + ESP-IDF's 2xx codes) */
enum { MCS_WIFI_REASON_UNSPECIFIED = 1, MCS_WIFI_REASON_AUTH_EXPIRE = 2, MCS_WIFI_REASON_AUTH_LEAVE = 3,
       MCS_WIFI_REASON_ASSOC_LEAVE = 8, MCS_WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
       MCS_WIFI_REASON_BEACON_TIMEOUT = 200, MCS_WIFI_REASON_NO_AP_FOUND = 201,
       MCS_WIFI_REASON_AUTH_FAIL = 202, MCS_WIFI_REASON_ASSOC_FAIL = 203,
       MCS_WIFI_REASON_HANDSHAKE_TIMEOUT = 204, MCS_WIFI_REASON_CONNECTION_FAIL = 205 };

/* ---- events: mcs_hal_post(MCS_HAL_EV_NET, MCS_NET_EV_*, value) -> Wifi.OnEvent((ev, value) => ...) */
enum { MCS_NET_EV_STA_START = 1, MCS_NET_EV_STA_STOP, MCS_NET_EV_CONNECTED,      /* value = channel */
       MCS_NET_EV_DISCONNECTED,                                                  /* value = reason */
       MCS_NET_EV_GOT_IP,                                                        /* value = IPv4, network order */
       MCS_NET_EV_LOST_IP, MCS_NET_EV_GOT_IP6, MCS_NET_EV_SCAN_DONE,              /* value = networks found */
       MCS_NET_EV_AP_START, MCS_NET_EV_AP_STOP,
       MCS_NET_EV_AP_JOINED, MCS_NET_EV_AP_LEFT,                                 /* value = association id */
       MCS_NET_EV_AP_ASSIGNED,                                                   /* value = IPv4 given to a client */
       MCS_NET_EV_ESPNOW_RECV,                                                   /* value = packets waiting */
       MCS_NET_EV_ESPNOW_SENT,                                                   /* value = 1 delivered, 0 not acked */
       MCS_NET_EV_NTP_SYNC,                                                      /* value = Unix time (low 31 bits) */
       MCS_NET_EV__COUNT };

/* ---- integer options (Wifi.PowerSave, Wifi.TxPower ...): opt_get / opt_set */
enum { MCS_WIFI_OPT_AUTO_RECONNECT = 1,  /* 0/1: reconnect after the AP was lost (front end + port) */
       MCS_WIFI_OPT_POWER_SAVE,          /* MCS_WIFI_PS_* */
       MCS_WIFI_OPT_TX_POWER,            /* quarter dBm (8 = 2 dBm ... 84 = 21 dBm) */
       MCS_WIFI_OPT_PROTOCOL,            /* MCS_WIFI_PROTO_* bits, station interface */
       MCS_WIFI_OPT_BANDWIDTH,           /* 20 or 40 MHz, station interface */
       MCS_WIFI_OPT_MIN_AUTH,            /* weakest security the station accepts (MCS_WIFI_AUTH_*) */
       MCS_WIFI_OPT_CHANNEL,             /* primary channel (read: current; write: AP / ESP-NOW channel) */
       MCS_WIFI_OPT_LISTEN_INTERVAL,     /* beacons between wake-ups in power save (station) */
       MCS_WIFI_OPT_AP_PROTOCOL,         /* MCS_WIFI_PROTO_* bits, AP interface */
       MCS_WIFI_OPT_BAND,                /* MCS_WIFI_BAND_* (dual-band chips such as the ESP32-C5) */
       MCS_WIFI_OPT_STORAGE,             /* 1 = the driver keeps its configuration in flash (NVS) */
       MCS_WIFI_OPT_ANTENNA,             /* antenna / RF switch selection (port specific) */
       MCS_WIFI_OPT__COUNT };
enum { MCS_WIFI_PS_NONE = 0, MCS_WIFI_PS_MIN = 1, MCS_WIFI_PS_MAX = 2 };
enum { MCS_WIFI_PROTO_B = 1, MCS_WIFI_PROTO_G = 2, MCS_WIFI_PROTO_N = 4, MCS_WIFI_PROTO_LR = 8,
       MCS_WIFI_PROTO_AX = 0x20 };
enum { MCS_WIFI_BAND_2G = 1, MCS_WIFI_BAND_5G = 2, MCS_WIFI_BAND_AUTO = 3 };

/* ---- string options: str_get / str_set */
enum { MCS_WIFI_STR_HOSTNAME = 1,        /* DHCP / mDNS host name of the station */
       MCS_WIFI_STR_COUNTRY,             /* two letters ("US", "DE", "01" = world safe mode) */
       MCS_WIFI_STR_IP6 };               /* station IPv6 addresses, space separated (read only) */

/* ---- interfaces */
enum { MCS_NET_IF_STA = 0, MCS_NET_IF_AP = 1 };

/* ---- credentials store: creds(op) */
enum { MCS_WIFI_CREDS_LOAD = 0, MCS_WIFI_CREDS_SAVE = 1, MCS_WIFI_CREDS_FORGET = 2 };

/* ---- provisioning without a password in the script: provision(method, op, ...) */
enum { MCS_WIFI_PROV_WPS = 1,            /* WPS push button: press the router's WPS key */
       MCS_WIFI_PROV_SMARTCONFIG = 2 };  /* Espressif ESP-Touch / AirKiss: the phone app sends the network */
enum { MCS_WIFI_PROV_QUERY = 0,          /* >= 0 when the method is available */
       MCS_WIFI_PROV_START, MCS_WIFI_PROV_POLL,   /* POLL: 1 = ssid / pass filled, 0 = still waiting, < 0 failed */
       MCS_WIFI_PROV_STOP };

/* ---- mDNS: mdns(op, ...) */
enum { MCS_MDNS_START = 1, MCS_MDNS_STOP, MCS_MDNS_ADD, MCS_MDNS_REMOVE };

typedef struct {
    char ssid[33];
    char password[65];          /* "" = open network */
    uint8_t bssid[6];           /* with has_bssid: only this access point */
    uint8_t has_bssid;
    uint8_t channel;            /* 0 = scan every channel */
    int8_t min_auth;            /* -1 = the driver's default threshold */
    /* WPA2/WPA3-Enterprise (identity != NULL): PEAP / TTLS with username + password.
     * The strings are valid during sta_connect only. */
    const char* identity;
    const char* username;
    const char* ca_pem;         /* optional RADIUS server CA certificate (PEM) */
} mcs_wifi_sta_cfg_t;

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;                /* dBm */
    uint8_t channel;
    uint8_t auth;               /* MCS_WIFI_AUTH_* */
    uint8_t protocols;          /* MCS_WIFI_PROTO_* bits the AP advertises, 0 = unknown */
} mcs_wifi_ap_info_t;

typedef struct {
    int status;                 /* MCS_WIFI_IDLE ... */
    int reason;                 /* last disconnect reason, 0 = none */
    mcs_wifi_ap_info_t ap;      /* the access point while connected */
} mcs_wifi_link_t;

typedef struct {
    char ssid[33];
    char password[65];          /* "" = open; else 8..64 characters */
    uint8_t channel;            /* 1..13 (0 = the port's default) */
    uint8_t auth;               /* MCS_WIFI_AUTH_* (password set: WPA2_PSK by default) */
    uint8_t hidden;             /* do not broadcast the SSID */
    uint8_t max_clients;        /* 0 = the port's default */
} mcs_wifi_ap_cfg_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t ip[4];              /* 0.0.0.0 = no DHCP lease yet */
} mcs_wifi_client_t;

typedef struct {
    uint8_t ip[4], netmask[4], gateway[4], dns1[4], dns2[4];
    uint8_t dhcp;               /* ip_get: address from DHCP; ip_set: 1 = back to DHCP (other fields ignored) */
} mcs_net_ip_t;

typedef struct mcs_net {
    const char* name;           /* "esp32", "sim" (Net.Backend) */
    void* ctx;                  /* passed to every function */

    /* ---------------- Wi-Fi (wifi_set_mode == NULL: no Wi-Fi) */
    int (*wifi_set_mode)(void* ctx, int mode);                 /* MCS_WIFI_OFF stops the radio and frees its RAM */
    int (*wifi_get_mode)(void* ctx);
    int (*sta_connect)(void* ctx, const mcs_wifi_sta_cfg_t* c);  /* start connecting, return at once */
    int (*sta_disconnect)(void* ctx);
    int (*sta_link)(void* ctx, mcs_wifi_link_t* out);
    /* blocking scan, at most `max` results sorted by signal; returns the count */
    int (*scan)(void* ctx, mcs_wifi_ap_info_t* out, int max, int show_hidden, uint32_t timeout_ms);
    int (*ap_start)(void* ctx, const mcs_wifi_ap_cfg_t* c);
    int (*ap_stop)(void* ctx);
    int (*ap_clients)(void* ctx, mcs_wifi_client_t* out, int max);   /* returns the count */
    int (*ip_get)(void* ctx, int iface, mcs_net_ip_t* out);
    int (*ip_set)(void* ctx, int iface, const mcs_net_ip_t* in);
    int (*mac_get)(void* ctx, int iface, uint8_t mac[6]);
    int (*mac_set)(void* ctx, int iface, const uint8_t mac[6]);
    int (*opt_get)(void* ctx, int opt, int32_t* value);
    int (*opt_set)(void* ctx, int opt, int32_t value);
    int (*str_get)(void* ctx, int opt, char* buf, size_t cap);
    int (*str_set)(void* ctx, int opt, const char* value);
    /* saved credentials (NVS on ESP32): LOAD fills ssid/pass (ENODEV: none saved) */
    int (*creds)(void* ctx, int op, char ssid[33], char pass[65]);

    /* ---------------- ESP-NOW (espnow_start == NULL: not available) */
    int (*espnow_start)(void* ctx);
    int (*espnow_stop)(void* ctx);
    /* add (add = 1, channel 0 = current, lmk = 16-byte key or NULL) or remove a peer */
    int (*espnow_peer)(void* ctx, const uint8_t mac[6], int add, int channel, const uint8_t* lmk);
    /* send and wait for the MAC-layer ack: 0 delivered, MCS_HAL_ENODEV not acked, ETIMEOUT */
    int (*espnow_send)(void* ctx, const uint8_t mac[6], const uint8_t* data, size_t n, uint32_t timeout_ms);
    /* next received packet: returns its length (0 = none waiting) */
    int (*espnow_recv)(void* ctx, uint8_t mac[6], uint8_t* buf, size_t cap, int* rssi);
    int (*espnow_pmk)(void* ctx, const uint8_t pmk[16]);
    int (*espnow_peers)(void* ctx, uint8_t (*macs)[6], int max);       /* returns the count */

    /* ---------------- time (ntp_start == NULL: Ntp uses the system clock as it is) */
    int (*ntp_start)(void* ctx, const char* const* servers, int n);    /* (re)start SNTP */
    int (*ntp_synced)(void* ctx);                                      /* 1 once the clock was set */

    /* ---------------- mDNS (mdns == NULL: not available)
     * START a = host name, b = instance name; ADD/REMOVE a = service ("_http"), b = protocol
     * ("_tcp" / "_udp"), port, txt = "key=value" strings */
    int (*mdns)(void* ctx, int op, const char* a, const char* b, int port, const char* const* txt, int ntxt);
    int (*mdns_resolve)(void* ctx, const char* host, uint32_t timeout_ms, uint8_t ip[4]);

    /* ---------------- ICMP echo (NULL: Ping not available): round trip in ms */
    int (*ping)(void* ctx, const uint8_t ip[4], uint32_t timeout_ms);

    /* ---------------- TLS client streams (tls_open == NULL: no HTTPS / ConnectTls)
     * ca_pem NULL = the port's certificate bundle. read: bytes, 0 = closed, ETIMEOUT */
    void* (*tls_open)(void* ctx, const char* host, int port, uint32_t timeout_ms, const char* ca_pem, int* err);
    int (*tls_write)(void* conn, const uint8_t* data, size_t n);
    int (*tls_read)(void* conn, uint8_t* buf, size_t n, uint32_t timeout_ms);
    void (*tls_close)(void* conn);

    /* ---------------- provisioning (NULL: Wifi.Wps / Wifi.SmartConfig not available; API 2).
     * The station is on while it runs; POLL returns the credentials it received. */
    int (*provision)(void* ctx, int method, int op, char ssid[33], char pass[65]);
} mcs_net_t;

#if MCS_ENABLE_NET
/* The board's network (kept for every VM opened afterwards; NULL = sockets only). */
void mcs_net_set(const mcs_net_t* net);
const mcs_net_t* mcs_net_get(void);
/* Register the C# network classes on vm (mcs_hal_open_lib calls it with mcs_net_get()). */
void mcs_net_open_lib(mcs_vm_t* vm, const mcs_net_t* net);
/* Foreground script scope: close resources created/started after the checkpoint,
 * even on Ctrl-C or an exception. Existing REPL/background resources are untouched.
 * Call end only after execution returned, never while a native wait is active. */
uint32_t mcs_net_scope_begin(mcs_vm_t* vm);
void mcs_net_scope_end(mcs_vm_t* vm, uint32_t checkpoint);
/* Hal.Has / Net.Has: "Wifi", "Wifi.AP", "EspNow", "Sockets", "Tls", "Ntp", "Mdns", "Ping" */
bool mcs_net_has(const mcs_net_t* net, const char* feature);
/* Queue a network event from any task (default: mcs_hal_post(MCS_HAL_EV_NET, ev, value)).
 * Ports whose HAL events travel through their own queue install their poster. */
bool mcs_net_post(int ev, int32_t value);
void mcs_net_set_poster(bool (*post)(int type, int source, int32_t value));
const char* mcs_wifi_reason_name(int reason);   /* "NO_AP_FOUND", "AUTH_FAIL" ... */
const char* mcs_wifi_auth_name(int auth);       /* "WPA2_PSK" ... */

/* ---- simulated network for the host CLI (--sim), tests and Studio templates:
 * three access points ("MicroCS-Lab" WPA2 password "microcs123", "CoffeeShop" open,
 * "Neighbor" WPA3), DHCP 192.168.1.50/24, soft-AP 192.168.4.1, an ESP-NOW peer
 * 24:0A:C4:00:00:99 that echoes every packet back, SNTP from the host clock,
 * mDNS answering for its own name, ping to the gateway and itself. */
#if MCS_NET_SIM
void mcs_net_sim_init(mcs_net_t* net);
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif
