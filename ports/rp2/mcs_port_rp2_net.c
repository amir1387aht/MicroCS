/*
 * MicroCS - Raspberry Pi Pico W / Pico 2 W Wi-Fi backend (mcs_net_t, include/mcs_net.h,
 * docs/NETWORK.md). Build: -DPICO_BOARD=pico_w (or pico2_w) -DMICROCS_OS=freertos
 * -DMICROCS_WIFI=ON (ports/rp2/README.md).
 *
 * The CYW43439 radio through pico_cyw43_arch_lwip_sys_freertos: station + soft-AP (with a
 * small DHCP server that hands out 192.168.4.16.. and itself as DNS, so DnsServer captive
 * portals work), scan, static IP, DHCP host name, country, power save, TX power, auto-
 * reconnect, link events, SNTP and an mDNS responder from lwIP's apps, ICMP ping over a raw
 * socket. Sockets, DNS, HTTP client / server and WebSocket are the portable modules/net code
 * on lwIP's BSD sockets. Saved credentials (Wifi.Save) live in the file /.wifi.
 * Not on this chip: ESP-NOW, WPS / SmartConfig, WPA2-Enterprise, MAC change, TLS (yet).
 *
 * Thread model: the VM task calls the table; a small "mcs-net" task watches the link every
 * 100 ms and posts MCS_NET_EV_* events; lwIP runs in its tcpip thread, the driver in its
 * async-context task. Raw lwIP / cyw43 calls hold the async-context lock (cyw43_arch_lwip_begin).
 */
#include "mcs_port_rp2.h"
#if MCS_RP2_WIFI
#include "mcs_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/mdns.h"

#ifndef MCS_RP2_WIFI_RETRIES
#define MCS_RP2_WIFI_RETRIES 2          /* extra attempts before Wifi.Connect reports a failure */
#endif
#ifndef MCS_RP2_RECONNECT_MS
#define MCS_RP2_RECONNECT_MS 5000       /* auto-reconnect: one attempt every ... */
#endif
#ifndef MCS_RP2_DHCPS_LEASES
#define MCS_RP2_DHCPS_LEASES 8          /* soft-AP clients with an address (192.168.4.16 ...) */
#endif
#define SCAN_MAX 32
#define AP_MAX 10                       /* CYW43439 soft-AP: up to 10 stations (driver limit) */

typedef struct {
    int inited, mode, ap_on;
    volatile int status, reason, user_disc, connecting, retries, link;
    uint32_t next_try;
    int32_t opt[MCS_WIFI_OPT__COUNT];
    char hostname[33], country[3];
    /* the network Wifi.Connect asked for (auto-reconnect, Wifi.Ssid) */
    char ssid[33], pass[65];
    uint8_t bssid[6], has_bssid, channel, auth;
    uint32_t cy_auth;
    /* static addresses (ip_set) */
    int sta_static, ap_custom;
    mcs_net_ip_t sta_ip, ap_ip;
    /* soft-AP stations */
    uint8_t ap_macs[AP_MAX][6];
    int ap_n;
    uint32_t ap_poll;
    /* scan results (driver task) */
    mcs_wifi_ap_info_t sc[SCAN_MAX];
    volatile int sc_n;
    /* SNTP */
    volatile int ntp_synced;
    char ntp_srv[3][48];
    /* mDNS */
    int md_inited, md_on, md_sta, md_ap;
    char md_host[33], md_inst[33];
    struct { char svc[24], proto[8]; uint16_t port; char txt[6][48]; int ntxt; int used; } md_svc[MDNS_MAX_SERVICES];
} rp2_net_t;
static rp2_net_t g;
static mcs_net_t g_table;

/* Events go through a FreeRTOS queue: lwIP and the driver run on either core, the HAL ring
 * is filled from core 0 only (the watcher task below, pinned next to the VM). */
typedef struct { int16_t ev; int32_t v; } net_ev_t;
static QueueHandle_t g_evq;
static void post(int ev, int32_t v) { net_ev_t e = { (int16_t)ev, v }; if (g_evq) xQueueSend(g_evq, &e, 0); }
static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static struct netif* sta_nif(void) { return &cyw43_state.netif[CYW43_ITF_STA]; }
static struct netif* ap_nif(void) { return &cyw43_state.netif[CYW43_ITF_AP]; }
#define LOCK() cyw43_arch_lwip_begin()
#define UNLOCK() cyw43_arch_lwip_end()

static uint32_t country_code(void) {
    if (!g.country[0] || !strcmp(g.country, "01")) return CYW43_COUNTRY_WORLDWIDE;
    return CYW43_COUNTRY(g.country[0], g.country[1], 0);
}
static int init_radio(void) {
    if (g.inited) return 0;
    if (cyw43_arch_init_with_country(country_code()) != 0) return MCS_HAL_ENODEV;
    g.inited = 1;
    return 0;
}
#if MCS_RP2_BLE
int mcs_rp2_radio_init(void) { return init_radio(); }
#endif

/* ------------------------------------------------------------ DHCP server (soft-AP) */
static struct udp_pcb* dhcps;
static struct { uint8_t mac[6]; uint8_t used, bound; } lease[MCS_RP2_DHCPS_LEASES];
static uint32_t ap_addr(void) { return ip4_addr_get_u32(netif_ip4_addr(ap_nif())); }        /* network order */
static uint32_t ap_mask(void) { return ip4_addr_get_u32(netif_ip4_netmask(ap_nif())); }
static uint32_t lease_ip(int i) { return (ap_addr() & ap_mask()) | lwip_htonl((uint32_t)(16 + i)); }
static uint8_t* opt_find(uint8_t* o, uint8_t* end, uint8_t code) {
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        if (o + 1 >= end || o + 2 + o[1] > end) return NULL;
        if (*o == code) return o;
        o += 2 + o[1];
    }
    return NULL;
}
static uint8_t* opt_put(uint8_t* o, uint8_t code, uint8_t n, const void* v) { *o++ = code; *o++ = n; memcpy(o, v, n); return o + n; }
static void dhcps_recv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* src, u16_t port) {
    (void)arg; (void)src; (void)port;
    uint8_t m[548];
    size_t n = pbuf_copy_partial(p, m, sizeof m, 0);
    pbuf_free(p);
    if (n < 240 || m[0] != 1 || m[236] != 99 || m[237] != 130 || m[238] != 83 || m[239] != 99) return;
    uint8_t* end = m + n;
    uint8_t* t = opt_find(m + 240, end, 53);
    if (!t || t[1] < 1) return;
    int type = t[2], slot = -1, reply;
    const uint8_t* mac = m + 28;
    for (int i = 0; i < MCS_RP2_DHCPS_LEASES; i++) if (lease[i].used && !memcmp(lease[i].mac, mac, 6)) slot = i;
    if (type == 1) {                                    /* DISCOVER -> OFFER */
        if (slot < 0) for (int i = 0; i < MCS_RP2_DHCPS_LEASES && slot < 0; i++) if (!lease[i].used) slot = i;
        if (slot < 0) return;                           /* full */
        memcpy(lease[slot].mac, mac, 6); lease[slot].used = 1; lease[slot].bound = 0;
        reply = 2;
    } else if (type == 3) {                             /* REQUEST -> ACK / NAK */
        uint8_t* r = opt_find(m + 240, end, 50);
        uint32_t want = 0;
        if (r && r[1] == 4) memcpy(&want, r + 2, 4); else memcpy(&want, m + 12, 4);
        if (slot < 0) for (int i = 0; i < MCS_RP2_DHCPS_LEASES && slot < 0; i++) if (!lease[i].used && lease_ip(i) == want) slot = i;
        if (slot >= 0 && lease_ip(slot) == want) { memcpy(lease[slot].mac, mac, 6); lease[slot].used = 1; reply = 5; }
        else reply = 6;
    } else if (type == 7) {                             /* RELEASE */
        if (slot >= 0) lease[slot].used = lease[slot].bound = 0;
        return;
    } else return;
    m[0] = 2;                                           /* BOOTREPLY; keep htype, hlen, xid, flags, chaddr */
    m[3] = 0;
    memset(m + 12, 0, 4);                               /* ciaddr */
    uint32_t yi = reply == 6 ? 0 : lease_ip(slot), me = ap_addr(), mask = ap_mask();
    memcpy(m + 16, &yi, 4); memcpy(m + 20, &me, 4); memset(m + 24, 0, 4);
    memset(m + 44, 0, 192);                             /* sname, file */
    uint8_t* o = m + 240;
    uint8_t ty = (uint8_t)reply;
    uint32_t life = lwip_htonl(24u * 3600u);
    o = opt_put(o, 53, 1, &ty);
    o = opt_put(o, 54, 4, &me);
    if (reply != 6) {
        o = opt_put(o, 51, 4, &life);
        o = opt_put(o, 1, 4, &mask);
        o = opt_put(o, 3, 4, &me);                      /* router */
        o = opt_put(o, 6, 4, &me);                      /* DNS: the Pico itself (DnsServer, captive portals) */
    }
    *o++ = 255;
    size_t len = (size_t)(o - m);
    if (len < 300) { memset(o, 0, 300 - len); len = 300; }
    struct pbuf* q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!q) return;
    pbuf_take(q, m, (u16_t)len);
    udp_sendto_if(pcb, q, IP4_ADDR_BROADCAST, 68, ap_nif());
    pbuf_free(q);
    if (reply == 5 && !lease[slot].bound) { lease[slot].bound = 1; post(MCS_NET_EV_AP_ASSIGNED, (int32_t)yi); }
}
static void dhcps_start(void) {        /* lock held */
    if (dhcps) return;
    memset(lease, 0, sizeof lease);
    dhcps = udp_new();
    if (!dhcps) return;
    ip_set_option(dhcps, SOF_BROADCAST);
    udp_bind_netif(dhcps, ap_nif());
    udp_bind(dhcps, IP4_ADDR_ANY, 67);
    udp_recv(dhcps, dhcps_recv, NULL);
}
static void dhcps_stop(void) { if (dhcps) { udp_remove(dhcps); dhcps = NULL; } memset(lease, 0, sizeof lease); }

/* ------------------------------------------------------------ mDNS attach (netifs come and go with the modes) */
static void md_txt(struct mdns_service* service, void* ud) {
    int i = (int)(intptr_t)ud;
    for (int k = 0; k < g.md_svc[i].ntxt; k++)
        mdns_resp_add_service_txtitem(service, g.md_svc[i].txt[k], (u8_t)strlen(g.md_svc[i].txt[k]));
}
static void md_add_services(struct netif* n) {
    for (int i = 0; i < MDNS_MAX_SERVICES; i++) if (g.md_svc[i].used)
        mdns_resp_add_service(n, g.md_inst, g.md_svc[i].svc, strcmp(g.md_svc[i].proto, "_udp") ? DNSSD_PROTO_TCP : DNSSD_PROTO_UDP,
                              g.md_svc[i].port, md_txt, (void*)(intptr_t)i);
}
static void md_attach(void) {          /* lock held: (re)announce on the interfaces that are up */
    if (!g.md_on) return;
    int want_sta = (g.mode & MCS_WIFI_STA) != 0, want_ap = g.ap_on;
    if (g.md_sta && (!want_sta || !mdns_resp_netif_active(sta_nif()))) { mdns_resp_remove_netif(sta_nif()); g.md_sta = 0; }
    if (g.md_ap && (!want_ap || !mdns_resp_netif_active(ap_nif()))) { mdns_resp_remove_netif(ap_nif()); g.md_ap = 0; }
    if (want_sta && !g.md_sta && mdns_resp_add_netif(sta_nif(), g.md_host) == ERR_OK) { g.md_sta = 1; md_add_services(sta_nif()); }
    if (want_ap && !g.md_ap && mdns_resp_add_netif(ap_nif(), g.md_host) == ERR_OK) { g.md_ap = 1; md_add_services(ap_nif()); }
}
static void md_detach(void) {
    if (g.md_sta) mdns_resp_remove_netif(sta_nif());
    if (g.md_ap) mdns_resp_remove_netif(ap_nif());
    g.md_sta = g.md_ap = 0;
}

/* ------------------------------------------------------------ link watcher */
static int join(void) {                /* (re)start joining g.ssid */
    int rc;
    LOCK();
    rc = cyw43_wifi_join(&cyw43_state, strlen(g.ssid), (const uint8_t*)g.ssid, strlen(g.pass), (const uint8_t*)g.pass,
                         g.pass[0] ? g.cy_auth : CYW43_AUTH_OPEN, g.has_bssid ? g.bssid : NULL,
                         g.channel ? g.channel : CYW43_CHANNEL_NONE);
    UNLOCK();
    return rc;
}
static int cur_channel(void) {
    uint8_t b[4] = { 0 };
    if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_CHANNEL, sizeof b, b, CYW43_ITF_STA) != 0) return 0;
    return b[0];
}
static int fail_reason(int ls) {
    return ls == CYW43_LINK_NONET ? MCS_WIFI_REASON_NO_AP_FOUND : ls == CYW43_LINK_BADAUTH ? MCS_WIFI_REASON_AUTH_FAIL
         : MCS_WIFI_REASON_CONNECTION_FAIL;
}
static void watch_once(void) {
    int ls = (g.mode & MCS_WIFI_STA) ? cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) : CYW43_LINK_DOWN;
    int prev = g.link;
    bool up = ls == CYW43_LINK_NOIP || ls == CYW43_LINK_UP, was_up = prev == CYW43_LINK_NOIP || prev == CYW43_LINK_UP;
    g.link = ls;
    if (up && !was_up) post(MCS_NET_EV_CONNECTED, cur_channel());
    if (ls == CYW43_LINK_UP && prev != CYW43_LINK_UP) {
        g.status = MCS_WIFI_CONNECTED; g.reason = 0; g.connecting = 0; g.retries = 0;
        post(MCS_NET_EV_GOT_IP, (int32_t)ip4_addr_get_u32(netif_ip4_addr(sta_nif())));
    }
    if (prev == CYW43_LINK_UP && ls == CYW43_LINK_NOIP) post(MCS_NET_EV_LOST_IP, 0);
    if (was_up && !up) {
        if (prev == CYW43_LINK_UP) post(MCS_NET_EV_LOST_IP, 0);
        if (g.user_disc || !(g.mode & MCS_WIFI_STA)) { g.status = (g.mode & MCS_WIFI_STA) ? MCS_WIFI_DISCONNECTED : MCS_WIFI_STOPPED; post(MCS_NET_EV_DISCONNECTED, MCS_WIFI_REASON_ASSOC_LEAVE); }
        else {
            g.status = MCS_WIFI_CONNECTION_LOST; g.reason = MCS_WIFI_REASON_BEACON_TIMEOUT;
            post(MCS_NET_EV_DISCONNECTED, g.reason);
            g.next_try = now_ms() + 500;
        }
    }
    if (ls < 0 && prev != ls && !g.user_disc) {          /* the join failed */
        int r = fail_reason(ls);
        g.reason = r;
        post(MCS_NET_EV_DISCONNECTED, r);
        if (g.connecting) {
            if (g.retries < MCS_RP2_WIFI_RETRIES && !(ls == CYW43_LINK_NONET && g.retries > 0)) { g.retries++; join(); g.link = CYW43_LINK_JOIN; }
            else {
                g.connecting = 0;
                g.status = ls == CYW43_LINK_NONET ? MCS_WIFI_NO_SSID : ls == CYW43_LINK_BADAUTH ? MCS_WIFI_WRONG_PASSWORD : MCS_WIFI_CONNECT_FAILED;
            }
        } else if (g.status == MCS_WIFI_CONNECTION_LOST) g.next_try = now_ms() + MCS_RP2_RECONNECT_MS;
    }
    if (g.status == MCS_WIFI_CONNECTION_LOST && g.opt[MCS_WIFI_OPT_AUTO_RECONNECT] && !g.user_disc && g.ssid[0]
        && (g.link == CYW43_LINK_DOWN || g.link < 0) && (int32_t)(now_ms() - g.next_try) >= 0) {
        g.next_try = now_ms() + MCS_RP2_RECONNECT_MS;
        if (join() == 0) g.link = CYW43_LINK_JOIN;
    }
    /* soft-AP stations: who joined / left */
    if (g.ap_on && (int32_t)(now_ms() - g.ap_poll) >= 0) {
        g.ap_poll = now_ms() + 1000;
        uint8_t macs[AP_MAX][6];
        int n = AP_MAX;
        cyw43_wifi_ap_get_stas(&cyw43_state, &n, &macs[0][0]);
        if (n < 0) n = 0;
        if (n > AP_MAX) n = AP_MAX;
        for (int i = 0; i < n; i++) {
            bool known = false;
            for (int k = 0; k < g.ap_n; k++) if (!memcmp(g.ap_macs[k], macs[i], 6)) known = true;
            if (!known) post(MCS_NET_EV_AP_JOINED, i + 1);
        }
        for (int k = 0; k < g.ap_n; k++) {
            bool still = false;
            for (int i = 0; i < n; i++) if (!memcmp(g.ap_macs[k], macs[i], 6)) still = true;
            if (!still) {
                LOCK();
                for (int j = 0; j < MCS_RP2_DHCPS_LEASES; j++) if (lease[j].used && !memcmp(lease[j].mac, g.ap_macs[k], 6)) lease[j].bound = 0;
                UNLOCK();
                post(MCS_NET_EV_AP_LEFT, k + 1);
            }
        }
        memcpy(g.ap_macs, macs, sizeof macs);
        g.ap_n = n;
    }
}
static void watch_task(void* arg) {
    (void)arg;
    uint32_t next = now_ms();
    for (;;) {
        net_ev_t e;
        int32_t wait = (int32_t)(next - now_ms());
        if (xQueueReceive(g_evq, &e, pdMS_TO_TICKS(wait > 0 ? wait : 0))) { mcs_net_post(e.ev, e.v); continue; }
        if (g.inited) watch_once();
        next = now_ms() + 100;
    }
}

/* ------------------------------------------------------------ modes */
static uint32_t pm_value(void) {
    int ps = g.opt[MCS_WIFI_OPT_POWER_SAVE];
    int li = g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] > 0 ? g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] : 1;
    if (ps == MCS_WIFI_PS_NONE) return CYW43_NONE_PM;
    if (ps == MCS_WIFI_PS_MAX) return cyw43_pm_value(CYW43_PM1_POWERSAVE_MODE, 10, (uint8_t)li, (uint8_t)li, 10);
    return cyw43_pm_value(CYW43_PM2_POWERSAVE_MODE, 200, (uint8_t)li, 1, 10);
}
static void apply_sta_ip(void) {       /* lock held */
    struct netif* n = sta_nif();
    netif_set_hostname(n, g.hostname);
    if (!g.sta_static) return;
    ip4_addr_t a, m, gw, d;
    dhcp_stop(n);
    IP4_ADDR(&a, g.sta_ip.ip[0], g.sta_ip.ip[1], g.sta_ip.ip[2], g.sta_ip.ip[3]);
    IP4_ADDR(&m, g.sta_ip.netmask[0], g.sta_ip.netmask[1], g.sta_ip.netmask[2], g.sta_ip.netmask[3]);
    IP4_ADDR(&gw, g.sta_ip.gateway[0], g.sta_ip.gateway[1], g.sta_ip.gateway[2], g.sta_ip.gateway[3]);
    netif_set_addr(n, &a, &m, &gw);
    for (int i = 0; i < 2; i++) {
        const uint8_t* s = i ? g.sta_ip.dns2 : g.sta_ip.dns1;
        if (s[0] | s[1] | s[2] | s[3]) { IP4_ADDR(&d, s[0], s[1], s[2], s[3]); ip_addr_t da; ip_addr_copy_from_ip4(da, d); dns_setserver((u8_t)i, &da); }
    }
}
static void apply_ap_ip(void) {        /* lock held */
    if (!g.ap_custom) return;
    ip4_addr_t a, m, gw;
    IP4_ADDR(&a, g.ap_ip.ip[0], g.ap_ip.ip[1], g.ap_ip.ip[2], g.ap_ip.ip[3]);
    IP4_ADDR(&m, g.ap_ip.netmask[0], g.ap_ip.netmask[1], g.ap_ip.netmask[2], g.ap_ip.netmask[3]);
    IP4_ADDR(&gw, g.ap_ip.gateway[0], g.ap_ip.gateway[1], g.ap_ip.gateway[2], g.ap_ip.gateway[3]);
    netif_set_addr(ap_nif(), &a, &m, &gw);
}
static int set_mode(void* ctx, int mode);
static int ap_stop(void* ctx);
static int set_mode(void* ctx, int mode) {
    (void)ctx;
    if (mode < MCS_WIFI_OFF || mode > MCS_WIFI_APSTA) return MCS_HAL_EINVAL;
    int rc = init_radio();
    if (rc < 0) return rc;
    int had_sta = g.mode & MCS_WIFI_STA;
    if (!(mode & MCS_WIFI_AP) && g.ap_on) ap_stop(NULL);
    if ((mode & MCS_WIFI_STA) && !had_sta) {
        g.user_disc = 1;
        cyw43_arch_enable_sta_mode();
        LOCK();
        apply_sta_ip();
        UNLOCK();
        cyw43_wifi_pm(&cyw43_state, pm_value());
        if (g.opt[MCS_WIFI_OPT_TX_POWER] > 0) g_table.opt_set(NULL, MCS_WIFI_OPT_TX_POWER, g.opt[MCS_WIFI_OPT_TX_POWER]);
        g.status = MCS_WIFI_DISCONNECTED; g.link = CYW43_LINK_DOWN;
        g.mode |= MCS_WIFI_STA;
        post(MCS_NET_EV_STA_START, 0);
    } else if (!(mode & MCS_WIFI_STA) && had_sta) {
        g.user_disc = 1; g.connecting = 0;
        LOCK();
        if (g.md_sta) { mdns_resp_remove_netif(sta_nif()); g.md_sta = 0; }
        UNLOCK();
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        cyw43_arch_disable_sta_mode();
        g.mode &= ~MCS_WIFI_STA;
        g.status = MCS_WIFI_STOPPED; g.link = CYW43_LINK_DOWN;
        post(MCS_NET_EV_STA_STOP, 0);
    }
    g.mode = (g.mode & MCS_WIFI_STA) | (mode & MCS_WIFI_AP);     /* the AP itself starts in ap_start */
    LOCK();
    md_attach();
    UNLOCK();
    return 0;
}
static int get_mode(void* ctx) { (void)ctx; return g.mode; }

/* ------------------------------------------------------------ station */
static int sta_connect(void* ctx, const mcs_wifi_sta_cfg_t* c) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    if (c->identity) return MCS_HAL_ENOTSUP;            /* no WPA2-Enterprise in the cyw43 driver */
    g.user_disc = 1;
    if (g.link != CYW43_LINK_DOWN) { cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA); vTaskDelay(pdMS_TO_TICKS(50)); }
    snprintf(g.ssid, sizeof g.ssid, "%s", c->ssid);
    snprintf(g.pass, sizeof g.pass, "%s", c->password);
    g.has_bssid = c->has_bssid;
    memcpy(g.bssid, c->bssid, 6);
    g.channel = c->channel;
    int min_auth = c->min_auth >= 0 ? c->min_auth : g.opt[MCS_WIFI_OPT_MIN_AUTH];
    g.cy_auth = min_auth == MCS_WIFI_AUTH_WPA3_PSK ? CYW43_AUTH_WPA3_SAE_AES_PSK
              : min_auth == MCS_WIFI_AUTH_WPA2_WPA3_PSK ? CYW43_AUTH_WPA3_WPA2_AES_PSK
              : min_auth == MCS_WIFI_AUTH_WPA_PSK || min_auth == MCS_WIFI_AUTH_WPA_WPA2_PSK ? CYW43_AUTH_WPA2_MIXED_PSK
              : CYW43_AUTH_WPA2_AES_PSK;
    g.auth = (uint8_t)(c->password[0] ? MCS_WIFI_AUTH_WPA2_PSK : MCS_WIFI_AUTH_OPEN);
    LOCK();
    netif_set_hostname(sta_nif(), g.hostname);
    UNLOCK();
    g.retries = 0; g.reason = 0; g.connecting = 1;
    g.status = MCS_WIFI_CONNECTING;
    g.link = CYW43_LINK_JOIN;
    g.user_disc = 0;
    int rc = join();
    if (rc != 0) { g.connecting = 0; g.status = MCS_WIFI_CONNECT_FAILED; return MCS_HAL_ERR; }
    return 0;
}
static int sta_disconnect(void* ctx) {
    (void)ctx;
    g.user_disc = 1; g.connecting = 0;
    if (g.mode & MCS_WIFI_STA) cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    g.status = (g.mode & MCS_WIFI_STA) ? MCS_WIFI_DISCONNECTED : g.status;
    return 0;
}
static int sta_link(void* ctx, mcs_wifi_link_t* l) {
    (void)ctx;
    memset(l, 0, sizeof *l);
    l->status = !(g.mode & MCS_WIFI_STA) ? (g.inited ? MCS_WIFI_STOPPED : MCS_WIFI_IDLE) : g.status;
    l->reason = g.reason;
    if (g.status == MCS_WIFI_CONNECTED && (g.mode & MCS_WIFI_STA)) {
        int32_t rssi = 0;
        snprintf(l->ap.ssid, sizeof l->ap.ssid, "%s", g.ssid);
        cyw43_wifi_get_rssi(&cyw43_state, &rssi);
        cyw43_wifi_get_bssid(&cyw43_state, l->ap.bssid);
        l->ap.rssi = (int8_t)rssi;
        l->ap.channel = (uint8_t)cur_channel();
        l->ap.auth = g.auth;
        l->ap.protocols = MCS_WIFI_PROTO_B | MCS_WIFI_PROTO_G | MCS_WIFI_PROTO_N;
    }
    return 0;
}
static int scan_cb(void* env, const cyw43_ev_scan_result_t* r) {
    int show_hidden = (int)(intptr_t)env;
    if (!r || (!r->ssid_len && !show_hidden)) return 0;
    for (int i = 0; i < g.sc_n; i++) if (!memcmp(g.sc[i].bssid, r->bssid, 6)) {
        if (r->rssi > g.sc[i].rssi) g.sc[i].rssi = (int8_t)r->rssi;
        return 0;
    }
    if (g.sc_n >= SCAN_MAX) return 0;
    mcs_wifi_ap_info_t* o = &g.sc[g.sc_n];
    memset(o, 0, sizeof *o);
    memcpy(o->ssid, r->ssid, r->ssid_len > 32 ? 32 : r->ssid_len);
    memcpy(o->bssid, r->bssid, 6);
    o->rssi = (int8_t)(r->rssi < -127 ? -127 : r->rssi);
    o->channel = (uint8_t)r->channel;
    int a = r->auth_mode;
    o->auth = (uint8_t)((a & 4) && (a & 2) ? MCS_WIFI_AUTH_WPA_WPA2_PSK : (a & 4) ? MCS_WIFI_AUTH_WPA2_PSK
            : (a & 2) ? MCS_WIFI_AUTH_WPA_PSK : (a & 1) ? MCS_WIFI_AUTH_WEP : MCS_WIFI_AUTH_OPEN);
    o->protocols = 0;
    g.sc_n++;
    return 0;
}
static int scan(void* ctx, mcs_wifi_ap_info_t* out, int max, int hidden, uint32_t timeout) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    cyw43_wifi_scan_options_t o;
    memset(&o, 0, sizeof o);
    g.sc_n = 0;
    if (cyw43_wifi_scan(&cyw43_state, &o, (void*)(intptr_t)(hidden != 0), scan_cb) != 0) return MCS_HAL_EBUSY;
    uint32_t t0 = now_ms(), lim = timeout ? timeout : 10000;
    vTaskDelay(pdMS_TO_TICKS(50));
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        if (now_ms() - t0 > lim) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    int n = g.sc_n;
    for (int i = 1; i < n; i++) {                       /* strongest first */
        mcs_wifi_ap_info_t t = g.sc[i];
        int k = i - 1;
        while (k >= 0 && g.sc[k].rssi < t.rssi) { g.sc[k + 1] = g.sc[k]; k--; }
        g.sc[k + 1] = t;
    }
    if (n > max) n = max;
    memcpy(out, g.sc, sizeof *out * (size_t)n);
    post(MCS_NET_EV_SCAN_DONE, g.sc_n);
    return n;
}

/* ------------------------------------------------------------ soft-AP */
static int ap_start(void* ctx, const mcs_wifi_ap_cfg_t* c) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_AP)) return MCS_HAL_EINVAL;
    if (c->password[0] && strlen(c->password) < 8) return MCS_HAL_EINVAL;
    if (g.ap_on) ap_stop(NULL), g.mode |= MCS_WIFI_AP;
    int ch = c->channel ? c->channel : g.opt[MCS_WIFI_OPT_CHANNEL] ? g.opt[MCS_WIFI_OPT_CHANNEL] : 1;
    if (g.status == MCS_WIFI_CONNECTED) { int cc = cur_channel(); if (cc) ch = cc; }   /* one radio: the station's channel */
    cyw43_wifi_ap_set_channel(&cyw43_state, (uint32_t)ch);
    uint32_t auth = c->auth == MCS_WIFI_AUTH_WPA_WPA2_PSK || c->auth == MCS_WIFI_AUTH_WPA_PSK ? CYW43_AUTH_WPA2_MIXED_PSK
                  : c->auth == MCS_WIFI_AUTH_WPA3_PSK ? CYW43_AUTH_WPA3_SAE_AES_PSK
                  : c->auth == MCS_WIFI_AUTH_WPA2_WPA3_PSK ? CYW43_AUTH_WPA3_WPA2_AES_PSK : CYW43_AUTH_WPA2_AES_PSK;
    cyw43_arch_enable_ap_mode(c->ssid, c->password[0] ? c->password : NULL, auth);
    LOCK();
    apply_ap_ip();
    dhcps_start();
    if (g.mode & MCS_WIFI_STA) netif_set_default(sta_nif());   /* internet through the station */
    UNLOCK();
    g.ap_on = 1; g.ap_n = 0; g.ap_poll = now_ms() + 500;
    LOCK();
    md_attach();
    UNLOCK();
    post(MCS_NET_EV_AP_START, 0);
    return 0;
}
static int ap_stop(void* ctx) {
    (void)ctx;
    if (!g.ap_on) { g.mode &= ~MCS_WIFI_AP; return 0; }
    LOCK();
    if (g.md_ap) { mdns_resp_remove_netif(ap_nif()); g.md_ap = 0; }
    dhcps_stop();
    UNLOCK();
    cyw43_arch_disable_ap_mode();
    g.ap_on = 0; g.ap_n = 0;
    g.mode &= ~MCS_WIFI_AP;
    post(MCS_NET_EV_AP_STOP, 0);
    return 0;
}
static int ap_clients(void* ctx, mcs_wifi_client_t* out, int max) {
    (void)ctx;
    if (!g.ap_on) return 0;
    uint8_t macs[AP_MAX][6];
    int n = AP_MAX;
    cyw43_wifi_ap_get_stas(&cyw43_state, &n, &macs[0][0]);
    if (n < 0) n = 0;
    if (n > max) n = max;
    LOCK();
    for (int i = 0; i < n; i++) {
        memcpy(out[i].mac, macs[i], 6);
        out[i].rssi = 0;
        memset(out[i].ip, 0, 4);
        for (int k = 0; k < MCS_RP2_DHCPS_LEASES; k++)
            if (lease[k].used && lease[k].bound && !memcmp(lease[k].mac, macs[i], 6)) { uint32_t a = lease_ip(k); memcpy(out[i].ip, &a, 4); }
    }
    UNLOCK();
    return n;
}

/* ------------------------------------------------------------ IP / MAC */
static void put4(uint8_t o[4], uint32_t a) { memcpy(o, &a, 4); }
static int ip_get(void* ctx, int iface, mcs_net_ip_t* o) {
    (void)ctx;
    memset(o, 0, sizeof *o);
    if (!g.inited) { o->dhcp = iface == MCS_NET_IF_STA; return 0; }
    struct netif* n = iface == MCS_NET_IF_AP ? ap_nif() : sta_nif();
    LOCK();
    bool on = iface == MCS_NET_IF_AP ? g.ap_on : (g.mode & MCS_WIFI_STA) != 0;
    if (on) {
        put4(o->ip, ip4_addr_get_u32(netif_ip4_addr(n)));
        put4(o->netmask, ip4_addr_get_u32(netif_ip4_netmask(n)));
        put4(o->gateway, ip4_addr_get_u32(netif_ip4_gw(n)));
    }
    if (iface == MCS_NET_IF_STA) {
        const ip_addr_t* d0 = dns_getserver(0); const ip_addr_t* d1 = dns_getserver(1);
        if (d0) put4(o->dns1, ip4_addr_get_u32(ip_2_ip4(d0)));
        if (d1) put4(o->dns2, ip4_addr_get_u32(ip_2_ip4(d1)));
        o->dhcp = !g.sta_static;
    } else if (on) memcpy(o->dns1, o->ip, 4);
    UNLOCK();
    return 0;
}
static int ip_set(void* ctx, int iface, const mcs_net_ip_t* in) {
    (void)ctx;
    int rc = init_radio();
    if (rc < 0) return rc;
    if (iface == MCS_NET_IF_STA) {
        LOCK();
        if (in->dhcp) {
            g.sta_static = 0;
            if (g.mode & MCS_WIFI_STA) dhcp_start(sta_nif());
        } else {
            g.sta_static = 1; g.sta_ip = *in;
            if (g.mode & MCS_WIFI_STA) apply_sta_ip();
        }
        UNLOCK();
        if (!in->dhcp && g.status == MCS_WIFI_CONNECTED) { uint32_t a; memcpy(&a, in->ip, 4); post(MCS_NET_EV_GOT_IP, (int32_t)a); }
        return 0;
    }
    if (in->dhcp) return MCS_HAL_EINVAL;
    g.ap_custom = 1; g.ap_ip = *in;
    LOCK();
    if (g.ap_on) { apply_ap_ip(); memset(lease, 0, sizeof lease); }
    UNLOCK();
    return 0;
}
static int mac_get(void* ctx, int iface, uint8_t m[6]) {
    (void)ctx;
    int rc = init_radio();
    if (rc < 0) return rc;
    if (cyw43_wifi_get_mac(&cyw43_state, iface == MCS_NET_IF_AP ? CYW43_ITF_AP : CYW43_ITF_STA, m) != 0) {
        memcpy(m, cyw43_state.mac, 6);                    /* both interfaces share the chip's address */
    }
    return 0;
}

/* ------------------------------------------------------------ options */
static int txpower(int set, int32_t* v) {           /* quarter dBm through the "qtxpower" iovar */
    uint8_t b[16];
    memset(b, 0, sizeof b);
    memcpy(b, "qtxpower", 9);
    if (set) {
        uint32_t q = (uint32_t)*v;
        b[9] = (uint8_t)q; b[10] = (uint8_t)(q >> 8); b[11] = (uint8_t)(q >> 16); b[12] = (uint8_t)(q >> 24);
        return cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, 13, b, CYW43_ITF_STA) == 0 ? 0 : MCS_HAL_ERR;
    }
    if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_VAR, 13, b, CYW43_ITF_STA) != 0) return MCS_HAL_ERR;
    *v = b[0] & 0x7F;
    return 0;
}
static int opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    if (opt <= 0 || opt >= MCS_WIFI_OPT__COUNT) return MCS_HAL_EINVAL;
    switch (opt) {
    case MCS_WIFI_OPT_TX_POWER: if (g.inited && txpower(0, v) == 0) return 0; break;
    case MCS_WIFI_OPT_CHANNEL: if (g.inited && ((g.mode & MCS_WIFI_STA) || g.ap_on)) { int c = cur_channel(); if (c) { *v = c; return 0; } } break;
    case MCS_WIFI_OPT_PROTOCOL: case MCS_WIFI_OPT_AP_PROTOCOL: *v = MCS_WIFI_PROTO_B | MCS_WIFI_PROTO_G | MCS_WIFI_PROTO_N; return 0;
    case MCS_WIFI_OPT_BANDWIDTH: *v = 20; return 0;
    case MCS_WIFI_OPT_BAND: *v = MCS_WIFI_BAND_2G; return 0;
    default: break;
    }
    *v = g.opt[opt];
    return 0;
}
static int opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_WIFI_OPT_AUTO_RECONNECT: g.opt[opt] = v != 0; return 0;
    case MCS_WIFI_OPT_POWER_SAVE: case MCS_WIFI_OPT_LISTEN_INTERVAL:
        if (opt == MCS_WIFI_OPT_POWER_SAVE && (v < MCS_WIFI_PS_NONE || v > MCS_WIFI_PS_MAX)) return MCS_HAL_EINVAL;
        if (opt == MCS_WIFI_OPT_LISTEN_INTERVAL && (v < 1 || v > 100)) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.inited && (g.mode & MCS_WIFI_STA)) return cyw43_wifi_pm(&cyw43_state, pm_value()) == 0 ? 0 : MCS_HAL_ERR;
        return 0;
    case MCS_WIFI_OPT_TX_POWER:
        if (v < 8 || v > 84) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.inited && (g.mode & MCS_WIFI_STA || g.ap_on)) return txpower(1, &v);
        return 0;
    case MCS_WIFI_OPT_MIN_AUTH:
        if (v < 0 || v >= MCS_WIFI_AUTH__COUNT) return MCS_HAL_EINVAL;
        g.opt[opt] = v; return 0;
    case MCS_WIFI_OPT_CHANNEL:                         /* the soft-AP's channel (from the next Wifi.StartAp) */
        if (v < 1 || v > 13) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.inited) cyw43_wifi_ap_set_channel(&cyw43_state, (uint32_t)v);
        return 0;
    case MCS_WIFI_OPT_BAND: return v == MCS_WIFI_BAND_2G || v == MCS_WIFI_BAND_AUTO ? 0 : v == MCS_WIFI_BAND_5G ? MCS_HAL_ENOTSUP : MCS_HAL_EINVAL;
    case MCS_WIFI_OPT_BANDWIDTH: return v == 20 ? 0 : v == 40 ? MCS_HAL_ENOTSUP : MCS_HAL_EINVAL;
    case MCS_WIFI_OPT_STORAGE: return v ? MCS_HAL_ENOTSUP : 0;
    default: return MCS_HAL_ENOTSUP;
    }
}
static int str_get(void* ctx, int opt, char* buf, size_t cap) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) { snprintf(buf, cap, "%s", g.hostname); return 0; }
    if (opt == MCS_WIFI_STR_COUNTRY) { snprintf(buf, cap, "%s", g.country[0] ? g.country : "01"); return 0; }
    if (opt == MCS_WIFI_STR_IP6) { if (cap) buf[0] = 0; return 0; }
    return MCS_HAL_EINVAL;
}
static int str_set(void* ctx, int opt, const char* v) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) {
        if (!v[0] || strlen(v) > 32) return MCS_HAL_EINVAL;
        snprintf(g.hostname, sizeof g.hostname, "%s", v);
        if (g.inited && (g.mode & MCS_WIFI_STA)) { LOCK(); netif_set_hostname(sta_nif(), g.hostname); UNLOCK(); }   /* next DHCP request */
        return 0;
    }
    if (opt == MCS_WIFI_STR_COUNTRY) {
        if (strlen(v) != 2) return MCS_HAL_EINVAL;
        char cc[3] = { (char)(v[0] >= 'a' && v[0] <= 'z' ? v[0] - 32 : v[0]), (char)(v[1] >= 'a' && v[1] <= 'z' ? v[1] - 32 : v[1]), 0 };
        if (!strcmp(cc, g.country)) return 0;
        memcpy(g.country, cc, 3);
#if MCS_RP2_BLE
        if (g.inited && mcs_rp2_ble_started()) return MCS_HAL_EBUSY;   /* Bluetooth runs on the chip: set Country before Ble.Enable */
#endif
        if (g.inited && !(g.mode & MCS_WIFI_STA) && !g.ap_on) {   /* the driver takes the country at power-up */
            md_detach();
            cyw43_arch_deinit();
            g.inited = 0;
            return init_radio();
        }
        return g.inited ? MCS_HAL_EBUSY : 0;           /* Wifi.Mode = Off first, then set Country */
    }
    return MCS_HAL_EINVAL;
}

/* ------------------------------------------------------------ SNTP */
void mcs_rp2_sntp_set_time(unsigned long sec, unsigned long us) {     /* lwIP's SNTP_SET_SYSTEM_TIME_US */
    struct timeval tv;
    tv.tv_sec = (time_t)sec; tv.tv_usec = (suseconds_t)us;
    settimeofday(&tv, NULL);
    g.ntp_synced = 1;
    post(MCS_NET_EV_NTP_SYNC, (int32_t)(sec & 0x7FFFFFFFu));
}
static int ntp_start(void* ctx, const char* const* sv, int n) {
    (void)ctx;
    int rc = init_radio();
    if (rc < 0) return rc;
    g.ntp_synced = 0;
    LOCK();
    if (sntp_enabled()) sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    for (int i = 0; i < 3; i++) {
        if (i < n) snprintf(g.ntp_srv[i], sizeof g.ntp_srv[i], "%s", sv[i]); else g.ntp_srv[i][0] = 0;
        sntp_setservername((u8_t)i, g.ntp_srv[i][0] ? g.ntp_srv[i] : NULL);
    }
    sntp_init();
    UNLOCK();
    return 0;
}
static int ntp_synced(void* ctx) { (void)ctx; return g.ntp_synced; }

/* ------------------------------------------------------------ mDNS (lwIP responder) */
static int md(void* ctx, int op, const char* a, const char* b, int port, const char* const* txt, int ntxt) {
    (void)ctx;
    int rc = init_radio();
    if (rc < 0) return rc;
    LOCK();
    switch (op) {
    case MCS_MDNS_START:
        if (!g.md_inited) { mdns_resp_init(); g.md_inited = 1; }
        md_detach();
        snprintf(g.md_host, sizeof g.md_host, "%s", a);
        snprintf(g.md_inst, sizeof g.md_inst, "%s", b ? b : a);
        g.md_on = 1;
        md_attach();
        rc = 0;
        break;
    case MCS_MDNS_STOP: md_detach(); g.md_on = 0; memset(g.md_svc, 0, sizeof g.md_svc); rc = 0; break;
    case MCS_MDNS_ADD: case MCS_MDNS_REMOVE: {
        if (!g.md_on) { rc = MCS_HAL_EINVAL; break; }
        int slot = -1;
        for (int i = 0; i < MDNS_MAX_SERVICES; i++) if (g.md_svc[i].used && !strcmp(g.md_svc[i].svc, a) && !strcmp(g.md_svc[i].proto, b)) slot = i;
        if (op == MCS_MDNS_REMOVE) { if (slot >= 0) g.md_svc[slot].used = 0; }
        else {
            if (slot < 0) for (int i = 0; i < MDNS_MAX_SERVICES && slot < 0; i++) if (!g.md_svc[i].used) slot = i;
            if (slot < 0) { rc = MCS_HAL_EBUSY; break; }
            snprintf(g.md_svc[slot].svc, sizeof g.md_svc[slot].svc, "%s", a);
            snprintf(g.md_svc[slot].proto, sizeof g.md_svc[slot].proto, "%s", b);
            g.md_svc[slot].port = (uint16_t)port;
            g.md_svc[slot].ntxt = 0;
            for (int i = 0; i < ntxt && i < 6; i++) snprintf(g.md_svc[slot].txt[g.md_svc[slot].ntxt++], 48, "%s", txt[i]);
            g.md_svc[slot].used = 1;
        }
        md_detach();                                    /* lwIP numbers services per netif: re-add them all */
        md_attach();
        rc = 0;
        break;
    }
    default: rc = MCS_HAL_EINVAL; break;
    }
    UNLOCK();
    return rc;
}
static int md_resolve(void* ctx, const char* host, uint32_t timeout, uint8_t ip[4]) {   /* lwIP DNS asks .local names over mDNS */
    (void)ctx; (void)timeout;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    if (lwip_getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return MCS_HAL_ETIMEOUT;
    memcpy(ip, &((struct sockaddr_in*)res->ai_addr)->sin_addr, 4);
    lwip_freeaddrinfo(res);
    return 0;
}

/* ------------------------------------------------------------ ping (raw ICMP socket) */
static int ping(void* ctx, const uint8_t ip[4], uint32_t timeout) {
    (void)ctx;
    if (!g.inited) return MCS_HAL_ETIMEOUT;
    if (!timeout) timeout = 1000;
    int s = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (s < 0) return MCS_HAL_ERR;
    struct timeval tv = { (long)(timeout / 1000), (long)(timeout % 1000) * 1000 };
    lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    static uint16_t seq;
    uint8_t pkt[sizeof(struct icmp_echo_hdr) + 32];
    struct icmp_echo_hdr* h = (struct icmp_echo_hdr*)pkt;
    memset(pkt, 0, sizeof pkt);
    ICMPH_TYPE_SET(h, ICMP_ECHO);
    ICMPH_CODE_SET(h, 0);
    h->id = lwip_htons(0x4D43);
    h->seqno = lwip_htons(++seq);
    for (size_t i = sizeof *h; i < sizeof pkt; i++) pkt[i] = (uint8_t)i;
    h->chksum = inet_chksum(pkt, sizeof pkt);
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_len = sizeof to;
    to.sin_family = AF_INET;
    memcpy(&to.sin_addr, ip, 4);
    uint32_t t0 = now_ms();
    int rc = MCS_HAL_ETIMEOUT;
    if (lwip_sendto(s, pkt, sizeof pkt, 0, (struct sockaddr*)&to, sizeof to) > 0) {
        uint8_t r[96];
        while (now_ms() - t0 < timeout) {
            struct sockaddr_in from; socklen_t fl = sizeof from;
            int n = lwip_recvfrom(s, r, sizeof r, 0, (struct sockaddr*)&from, &fl);
            if (n <= 0) break;
            int ihl = (r[0] & 0x0F) * 4;
            if (n < ihl + (int)sizeof(struct icmp_echo_hdr)) continue;
            struct icmp_echo_hdr* e = (struct icmp_echo_hdr*)(r + ihl);
            if (ICMPH_TYPE(e) == ICMP_ER && e->id == h->id && e->seqno == h->seqno && !memcmp(&from.sin_addr, ip, 4)) {
                rc = (int)(now_ms() - t0);
                break;
            }
        }
    }
    lwip_close(s);
    return rc;
}

/* ------------------------------------------------------------ the Pico W's LED (and the other WL_GPIO pins) */
int mcs_rp2_wl_gpio(int pin, int write, int v) {
    if (pin < MCS_RP2_WL_PIN(0) || pin > MCS_RP2_WL_PIN(2)) return MCS_HAL_EINVAL;
    if (init_radio() < 0) return MCS_HAL_ENODEV;
    if (write) { cyw43_arch_gpio_put((uint)(pin - MCS_RP2_WL_PIN(0)), v != 0); return 0; }
    return cyw43_arch_gpio_get((uint)(pin - MCS_RP2_WL_PIN(0))) ? 1 : 0;
}

/* ------------------------------------------------------------ table */
const mcs_net_t* mcs_rp2_net(void) {
    if (g_table.name) return &g_table;
    memset(&g, 0, sizeof g);
    g.opt[MCS_WIFI_OPT_AUTO_RECONNECT] = 1;
    g.opt[MCS_WIFI_OPT_POWER_SAVE] = MCS_WIFI_PS_MIN;
    g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] = 1;
    g.opt[MCS_WIFI_OPT_BAND] = MCS_WIFI_BAND_2G;
    g.opt[MCS_WIFI_OPT_BANDWIDTH] = 20;
    g.link = CYW43_LINK_DOWN;
    g.status = MCS_WIFI_IDLE;
    snprintf(g.hostname, sizeof g.hostname, "%s", MCS_RP2_HOSTNAME);
    g_evq = xQueueCreate(16, sizeof(net_ev_t));
    /* lwIP and the driver come up now: sockets, DNS and the LED need them even with the radio off */
    if (init_radio() < 0) printf("mcs: cyw43 Wi-Fi chip not found - networking disabled\n");
    TaskHandle_t th;
    if (xTaskCreate(watch_task, "mcs-net", 1024, NULL, 2, &th) == pdPASS) vTaskCoreAffinitySet(th, 1u << 0);
    mcs_net_t* t = &g_table;
    t->name = "rp2";
    t->wifi_set_mode = set_mode; t->wifi_get_mode = get_mode;
    t->sta_connect = sta_connect; t->sta_disconnect = sta_disconnect; t->sta_link = sta_link; t->scan = scan;
    t->ap_start = ap_start; t->ap_stop = ap_stop; t->ap_clients = ap_clients;
    t->ip_get = ip_get; t->ip_set = ip_set; t->mac_get = mac_get;
    t->opt_get = opt_get; t->opt_set = opt_set; t->str_get = str_get; t->str_set = str_set;
    t->ntp_start = ntp_start; t->ntp_synced = ntp_synced;
    t->mdns = md; t->mdns_resolve = md_resolve;
    t->ping = ping;
    return t;
}
#endif /* MCS_RP2_WIFI */
