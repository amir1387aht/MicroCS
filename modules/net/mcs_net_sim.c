/*
 * MicroCS - simulated Wi-Fi radio (MCS_NET_SIM): the network behind `mcs --sim`, the test
 * suite and MicroCS Studio. Deterministic, no real radio involved; sockets, DNS and HTTP
 * still use the host's real network stack.
 *
 *   access points   "MicroCS-Lab"  WPA2-PSK, password "microcs123", -48 dBm, channel 6
 *                   "CoffeeShop"   open,                            -67 dBm, channel 1
 *                   "Neighbor"     WPA3-SAE, password "letmein99",  -81 dBm, channel 11
 *                   (hidden)       WPA2-PSK,                        -88 dBm, channel 3
 *   station         DHCP 192.168.1.50/24, gateway + DNS 192.168.1.1, DNS2 8.8.8.8
 *   soft-AP         192.168.4.1/24; a phone (AA:BB:CC:00:00:01) joins and gets 192.168.4.2
 *   ESP-NOW         peer 24:0A:C4:00:00:99 echoes every packet sent to it (or broadcast)
 *   SNTP            the host clock;  mDNS: <hostname>.local and printer.local (192.168.1.20)
 *   Ping            gateway 1 ms, 8.8.8.8 12 ms while connected, anything else times out
 */
#include "mcs_net.h"
#if MCS_ENABLE_NET && MCS_NET_SIM
#include "mcs_hal.h"
#include <string.h>
#include <stdio.h>

typedef struct { const char* ssid; const char* pass; uint8_t bssid[6]; int8_t rssi; uint8_t ch, auth, proto; } sim_ap_t;
static const sim_ap_t g_aps[] = {
    { "MicroCS-Lab", "microcs123", { 0x24, 0x0A, 0xC4, 0x11, 0x22, 0x33 }, -48, 6, MCS_WIFI_AUTH_WPA2_PSK, 7 },
    { "CoffeeShop", "", { 0x9C, 0x53, 0x22, 0x10, 0x20, 0x30 }, -67, 1, MCS_WIFI_AUTH_OPEN, 7 },
    { "Neighbor", "letmein99", { 0x60, 0x38, 0xE0, 0xAA, 0xBB, 0xCC }, -81, 11, MCS_WIFI_AUTH_WPA3_PSK, 0x27 },
    { "", "hidden-net", { 0x60, 0x38, 0xE0, 0x01, 0x02, 0x03 }, -88, 3, MCS_WIFI_AUTH_WPA2_PSK, 7 },
};
#define NAPS ((int)(sizeof g_aps / sizeof g_aps[0]))
static const uint8_t ECHO_PEER[6] = { 0x24, 0x0A, 0xC4, 0x00, 0x00, 0x99 };
#define RXQ 8

typedef struct {
    int mode, status, reason, ap;                 /* ap = index of the joined AP, -1 = none */
    mcs_net_ip_t sta_ip, ap_ip;
    uint8_t sta_static;
    uint8_t mac[2][6];
    int32_t opt[MCS_WIFI_OPT__COUNT];
    char hostname[33], country[3];
    char saved_ssid[33], saved_pass[65];
    int ap_on; mcs_wifi_ap_cfg_t apc;
    int en_on; uint8_t peers[MCS_ESPNOW_MAX_PEERS][6]; int npeers;
    struct { uint8_t mac[6]; uint8_t data[250]; int n; } rx[RXQ]; int rx_head, rx_n;
    int ntp_synced, mdns_on;
} sim_t;
static sim_t g_s;

static void post(int ev, int32_t v) { mcs_net_post(ev, v); }
static int32_t ip_word(const uint8_t ip[4]) { int32_t w; memcpy(&w, ip, 4); return w; }   /* network order */
static void set4(uint8_t* d, int a, int b, int c, int e) { d[0] = (uint8_t)a; d[1] = (uint8_t)b; d[2] = (uint8_t)c; d[3] = (uint8_t)e; }
static void dhcp_defaults(void) {
    memset(&g_s.sta_ip, 0, sizeof g_s.sta_ip);
    set4(g_s.sta_ip.ip, 192, 168, 1, 50); set4(g_s.sta_ip.netmask, 255, 255, 255, 0);
    set4(g_s.sta_ip.gateway, 192, 168, 1, 1); set4(g_s.sta_ip.dns1, 192, 168, 1, 1); set4(g_s.sta_ip.dns2, 8, 8, 8, 8);
    g_s.sta_ip.dhcp = 1;
}

static void sta_down(int reason, int new_status) {
    if (g_s.status == MCS_WIFI_CONNECTED) { post(MCS_NET_EV_LOST_IP, 0); }
    if (g_s.status == MCS_WIFI_CONNECTED || g_s.status == MCS_WIFI_CONNECTING) post(MCS_NET_EV_DISCONNECTED, reason);
    g_s.status = new_status; g_s.reason = reason; g_s.ap = -1;
}
static int set_mode(void* ctx, int mode) {
    (void)ctx;
    if (mode < 0 || mode > MCS_WIFI_APSTA) return MCS_HAL_EINVAL;
    int old = g_s.mode;
    if ((old & MCS_WIFI_STA) && !(mode & MCS_WIFI_STA)) {
        sta_down(MCS_WIFI_REASON_ASSOC_LEAVE, MCS_WIFI_STOPPED);
        post(MCS_NET_EV_STA_STOP, 0);
    }
    if ((old & MCS_WIFI_AP) && !(mode & MCS_WIFI_AP) && g_s.ap_on) { g_s.ap_on = 0; post(MCS_NET_EV_AP_STOP, 0); }
    if (!(old & MCS_WIFI_STA) && (mode & MCS_WIFI_STA)) {
        g_s.status = MCS_WIFI_DISCONNECTED; g_s.reason = 0;
        post(MCS_NET_EV_STA_START, 0);
    }
    if (mode == MCS_WIFI_OFF) { g_s.en_on = 0; g_s.status = MCS_WIFI_STOPPED; }
    g_s.mode = mode;
    return 0;
}
static int get_mode(void* ctx) { (void)ctx; return g_s.mode; }

static int sta_connect(void* ctx, const mcs_wifi_sta_cfg_t* c) {
    (void)ctx;
    if (!(g_s.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    if (g_s.status == MCS_WIFI_CONNECTED) sta_down(MCS_WIFI_REASON_ASSOC_LEAVE, MCS_WIFI_DISCONNECTED);
    g_s.status = MCS_WIFI_CONNECTING;
    int min_auth = c->min_auth >= 0 ? c->min_auth : g_s.opt[MCS_WIFI_OPT_MIN_AUTH];
    int found = -1, below = 0;
    for (int i = 0; i < NAPS; i++) {
        const sim_ap_t* a = &g_aps[i];
        bool match = c->has_bssid ? !memcmp(c->bssid, a->bssid, 6) : (a->ssid[0] && !strcmp(a->ssid, c->ssid));
        if (c->has_bssid && !a->ssid[0] && strcmp(c->ssid, "HiddenLab")) match = false;
        if (!a->ssid[0] && !c->has_bssid && !strcmp(c->ssid, "HiddenLab")) match = true;   /* the hidden network's name */
        if (!match || (c->channel && c->channel != a->ch)) continue;
        if (a->auth < min_auth) { below = 1; continue; }
        found = i; break;
    }
    if (found < 0) {
        g_s.status = MCS_WIFI_NO_SSID;
        g_s.reason = below ? 211 : MCS_WIFI_REASON_NO_AP_FOUND;
        post(MCS_NET_EV_DISCONNECTED, g_s.reason);
        return 0;
    }
    const sim_ap_t* a = &g_aps[found];
    if (c->identity) {                           /* no enterprise network in the simulation */
        g_s.status = MCS_WIFI_CONNECT_FAILED; g_s.reason = 23;
        post(MCS_NET_EV_DISCONNECTED, g_s.reason);
        return 0;
    }
    if (a->auth != MCS_WIFI_AUTH_OPEN && strcmp(a->pass, c->password)) {
        g_s.status = MCS_WIFI_WRONG_PASSWORD; g_s.reason = MCS_WIFI_REASON_AUTH_FAIL;
        post(MCS_NET_EV_DISCONNECTED, g_s.reason);
        return 0;
    }
    g_s.ap = found; g_s.status = MCS_WIFI_CONNECTED; g_s.reason = 0;
    if (!g_s.sta_static) dhcp_defaults();
    post(MCS_NET_EV_CONNECTED, a->ch);
    post(MCS_NET_EV_GOT_IP, ip_word(g_s.sta_ip.ip));
    return 0;
}
static int sta_disconnect(void* ctx) {
    (void)ctx;
    if (g_s.status == MCS_WIFI_CONNECTED || g_s.status == MCS_WIFI_CONNECTING) sta_down(MCS_WIFI_REASON_ASSOC_LEAVE, MCS_WIFI_DISCONNECTED);
    return 0;
}
static void ap_info(int i, mcs_wifi_ap_info_t* o) {
    const sim_ap_t* a = &g_aps[i];
    memset(o, 0, sizeof *o);
    snprintf(o->ssid, sizeof o->ssid, "%s", a->ssid);
    memcpy(o->bssid, a->bssid, 6);
    o->rssi = a->rssi; o->channel = a->ch; o->auth = a->auth; o->protocols = a->proto;
}
static int sta_link(void* ctx, mcs_wifi_link_t* l) {
    (void)ctx;
    memset(l, 0, sizeof *l);
    l->status = g_s.mode & MCS_WIFI_STA ? g_s.status : (g_s.mode == MCS_WIFI_OFF ? MCS_WIFI_STOPPED : MCS_WIFI_IDLE);
    l->reason = g_s.reason;
    if (g_s.ap >= 0) ap_info(g_s.ap, &l->ap);
    return 0;
}
static int scan(void* ctx, mcs_wifi_ap_info_t* out, int max, int hidden, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!(g_s.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    int n = 0;
    for (int i = 0; i < NAPS && n < max; i++) {
        if (!g_aps[i].ssid[0] && !hidden) continue;
        ap_info(i, &out[n++]);
    }
    post(MCS_NET_EV_SCAN_DONE, n);
    return n;
}
static int ap_start(void* ctx, const mcs_wifi_ap_cfg_t* c) {
    (void)ctx;
    if (!(g_s.mode & MCS_WIFI_AP)) return MCS_HAL_EINVAL;
    g_s.apc = *c;
    if (!g_s.apc.channel) g_s.apc.channel = g_s.ap >= 0 ? g_aps[g_s.ap].ch : 1;
    if (!g_s.apc.max_clients) g_s.apc.max_clients = 4;
    g_s.ap_on = 1;
    post(MCS_NET_EV_AP_START, 0);
    post(MCS_NET_EV_AP_JOINED, 1);                /* a phone joins at once ... */
    uint8_t ip[4]; memcpy(ip, g_s.ap_ip.ip, 4); ip[3] = (uint8_t)(ip[3] + 1);
    post(MCS_NET_EV_AP_ASSIGNED, ip_word(ip));    /* ... and gets the next address */
    return 0;
}
static int ap_stop(void* ctx) {
    (void)ctx;
    if (g_s.ap_on) { post(MCS_NET_EV_AP_LEFT, 1); post(MCS_NET_EV_AP_STOP, 0); }
    g_s.ap_on = 0;
    return 0;
}
static int ap_clients(void* ctx, mcs_wifi_client_t* out, int max) {
    (void)ctx;
    if (!g_s.ap_on || max < 1) return 0;
    static const uint8_t phone[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x01 };
    memcpy(out[0].mac, phone, 6); out[0].rssi = -40;
    memcpy(out[0].ip, g_s.ap_ip.ip, 4); out[0].ip[3] = (uint8_t)(out[0].ip[3] + 1);
    return 1;
}
static int ip_get(void* ctx, int iface, mcs_net_ip_t* o) {
    (void)ctx;
    if (iface == MCS_NET_IF_AP) { *o = g_s.ap_ip; return 0; }
    if (g_s.status == MCS_WIFI_CONNECTED || g_s.sta_static) *o = g_s.sta_ip;
    else { memset(o, 0, sizeof *o); o->dhcp = !g_s.sta_static; }
    return 0;
}
static int ip_set(void* ctx, int iface, const mcs_net_ip_t* in) {
    (void)ctx;
    if (iface == MCS_NET_IF_AP) {
        if (in->dhcp) return MCS_HAL_EINVAL;
        g_s.ap_ip = *in; g_s.ap_ip.dhcp = 0;
        return 0;
    }
    if (in->dhcp) { g_s.sta_static = 0; dhcp_defaults(); return 0; }
    g_s.sta_ip = *in; g_s.sta_ip.dhcp = 0; g_s.sta_static = 1;
    if (g_s.status == MCS_WIFI_CONNECTED) post(MCS_NET_EV_GOT_IP, ip_word(g_s.sta_ip.ip));
    return 0;
}
static int mac_get(void* ctx, int iface, uint8_t m[6]) { (void)ctx; memcpy(m, g_s.mac[iface ? 1 : 0], 6); return 0; }
static int mac_set(void* ctx, int iface, const uint8_t m[6]) {
    (void)ctx;
    if (m[0] & 1) return MCS_HAL_EINVAL;          /* multicast bit: not a station address */
    memcpy(g_s.mac[iface ? 1 : 0], m, 6);
    return 0;
}
static int opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    if (opt <= 0 || opt >= MCS_WIFI_OPT__COUNT) return MCS_HAL_EINVAL;
    if (opt == MCS_WIFI_OPT_CHANNEL) { *v = g_s.ap >= 0 ? g_aps[g_s.ap].ch : g_s.ap_on ? g_s.apc.channel : g_s.opt[opt]; return 0; }
    *v = g_s.opt[opt];
    return 0;
}
static int opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_WIFI_OPT_AUTO_RECONNECT: case MCS_WIFI_OPT_STORAGE: v = v != 0; break;
    case MCS_WIFI_OPT_POWER_SAVE: if (v < MCS_WIFI_PS_NONE || v > MCS_WIFI_PS_MAX) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_TX_POWER: if (v < 8 || v > 84) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_PROTOCOL: case MCS_WIFI_OPT_AP_PROTOCOL: if (v <= 0 || (v & ~0x2F)) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_BANDWIDTH: if (v != 20 && v != 40) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_MIN_AUTH: if (v < 0 || v >= MCS_WIFI_AUTH__COUNT) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_CHANNEL: if (v < 1 || v > 13) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_LISTEN_INTERVAL: if (v < 1 || v > 100) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_BAND: if (v == MCS_WIFI_BAND_5G) return MCS_HAL_ENOTSUP; if (v != MCS_WIFI_BAND_2G && v != MCS_WIFI_BAND_AUTO) return MCS_HAL_EINVAL; break;
    case MCS_WIFI_OPT_ANTENNA: if (v < 0 || v > 1) return MCS_HAL_EINVAL; break;
    default: return MCS_HAL_EINVAL;
    }
    g_s.opt[opt] = v;
    return 0;
}
static int str_get(void* ctx, int opt, char* buf, size_t cap) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) snprintf(buf, cap, "%s", g_s.hostname);
    else if (opt == MCS_WIFI_STR_COUNTRY) snprintf(buf, cap, "%s", g_s.country);
    else if (opt == MCS_WIFI_STR_IP6) {
        const uint8_t* m = g_s.mac[0];
        if (g_s.status == MCS_WIFI_CONNECTED)
            snprintf(buf, cap, "fe80::%x:%xff:fe%02x:%x", (m[0] ^ 2) << 8 | m[1], m[2], m[3], m[4] << 8 | m[5]);
        else if (cap) buf[0] = 0;
    } else return MCS_HAL_EINVAL;
    return 0;
}
static int str_set(void* ctx, int opt, const char* v) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) { snprintf(g_s.hostname, sizeof g_s.hostname, "%s", v); return 0; }
    if (opt == MCS_WIFI_STR_COUNTRY) { snprintf(g_s.country, sizeof g_s.country, "%s", v); return 0; }
    return MCS_HAL_EINVAL;
}
static int creds(void* ctx, int op, char ssid[33], char pass[65]) {
    (void)ctx;
    if (op == MCS_WIFI_CREDS_LOAD) {
        if (!g_s.saved_ssid[0]) return MCS_HAL_ENODEV;
        memcpy(ssid, g_s.saved_ssid, 33); memcpy(pass, g_s.saved_pass, 65);
        return 0;
    }
    if (op == MCS_WIFI_CREDS_SAVE) { memcpy(g_s.saved_ssid, ssid, 33); memcpy(g_s.saved_pass, pass, 65); return 0; }
    if (op == MCS_WIFI_CREDS_FORGET) { memset(g_s.saved_ssid, 0, 33); memset(g_s.saved_pass, 0, 65); return 0; }
    return MCS_HAL_EINVAL;
}

/* ---- ESP-NOW */
static int en_start(void* ctx) { (void)ctx; if (g_s.mode == MCS_WIFI_OFF) return MCS_HAL_EINVAL; g_s.en_on = 1; return 0; }
static int en_stop(void* ctx) { (void)ctx; g_s.en_on = 0; g_s.npeers = 0; g_s.rx_n = 0; return 0; }
static int peer_find(const uint8_t m[6]) { for (int i = 0; i < g_s.npeers; i++) if (!memcmp(g_s.peers[i], m, 6)) return i; return -1; }
static int en_peer(void* ctx, const uint8_t m[6], int add, int ch, const uint8_t* lmk) {
    (void)ctx; (void)lmk;
    if (!g_s.en_on) return MCS_HAL_EINVAL;
    int i = peer_find(m);
    if (add) {
        if (ch && g_s.ap >= 0 && ch != g_aps[g_s.ap].ch) return MCS_HAL_EINVAL;   /* the radio stays on the AP's channel */
        if (i >= 0) return 0;
        if (g_s.npeers >= MCS_ESPNOW_MAX_PEERS) return MCS_HAL_EBUSY;
        memcpy(g_s.peers[g_s.npeers++], m, 6);
        return 0;
    }
    if (i < 0) return MCS_HAL_ENODEV;
    memmove(g_s.peers[i], g_s.peers[i + 1], (size_t)(g_s.npeers - i - 1) * 6);
    g_s.npeers--;
    return 0;
}
static int en_send(void* ctx, const uint8_t m[6], const uint8_t* d, size_t n, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!g_s.en_on) return MCS_HAL_EINVAL;
    static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    bool broadcast = !memcmp(m, bc, 6);
    if (!broadcast && peer_find(m) < 0) return MCS_HAL_EINVAL;               /* ESP-IDF: add the peer first */
    bool echo = broadcast || !memcmp(m, ECHO_PEER, 6);
    post(MCS_NET_EV_ESPNOW_SENT, echo || broadcast);
    if (!echo) return MCS_HAL_ENODEV;                                         /* nobody acked */
    if (g_s.rx_n < RXQ) {
        int k = (g_s.rx_head + g_s.rx_n) % RXQ;
        memcpy(g_s.rx[k].mac, ECHO_PEER, 6);
        g_s.rx[k].n = (int)(n > 250 ? 250 : n);
        memcpy(g_s.rx[k].data, d, (size_t)g_s.rx[k].n);
        g_s.rx_n++;
        post(MCS_NET_EV_ESPNOW_RECV, g_s.rx_n);
    }
    return 0;
}
static int en_recv(void* ctx, uint8_t m[6], uint8_t* buf, size_t cap, int* rssi) {
    (void)ctx;
    if (!g_s.rx_n) return 0;
    int k = g_s.rx_head, n = g_s.rx[k].n < (int)cap ? g_s.rx[k].n : (int)cap;
    memcpy(m, g_s.rx[k].mac, 6); memcpy(buf, g_s.rx[k].data, (size_t)n);
    if (rssi) *rssi = -52;
    g_s.rx_head = (g_s.rx_head + 1) % RXQ; g_s.rx_n--;
    return n;
}
static int en_pmk(void* ctx, const uint8_t k[16]) { (void)ctx; (void)k; return g_s.en_on ? 0 : MCS_HAL_EINVAL; }
static int en_peers(void* ctx, uint8_t (*macs)[6], int max) {
    (void)ctx;
    int n = g_s.npeers < max ? g_s.npeers : max;
    for (int i = 0; i < n; i++) memcpy(macs[i], g_s.peers[i], 6);
    return n;
}

/* ---- time, mDNS, ping */
#include <time.h>
static int ntp_start(void* ctx, const char* const* sv, int n) {
    (void)ctx; (void)sv; (void)n;
    g_s.ntp_synced = 1;
    post(MCS_NET_EV_NTP_SYNC, (int32_t)((uint32_t)time(NULL) & 0x7FFFFFFFu));
    return 0;
}
static int ntp_synced(void* ctx) { (void)ctx; return g_s.ntp_synced; }
static int mdns(void* ctx, int op, const char* a, const char* b, int port, const char* const* txt, int ntxt) {
    (void)ctx; (void)b; (void)port; (void)txt; (void)ntxt;
    if (op == MCS_MDNS_START) { snprintf(g_s.hostname, sizeof g_s.hostname, "%s", a); g_s.mdns_on = 1; return 0; }
    if (op == MCS_MDNS_STOP) { g_s.mdns_on = 0; return 0; }
    if (!g_s.mdns_on) return MCS_HAL_EINVAL;
    return op == MCS_MDNS_ADD || op == MCS_MDNS_REMOVE ? 0 : MCS_HAL_EINVAL;
}
static int mdns_resolve(void* ctx, const char* host, uint32_t timeout, uint8_t ip[4]) {
    (void)ctx; (void)timeout;
    char own[48]; snprintf(own, sizeof own, "%s.local", g_s.hostname);
    if (g_s.status != MCS_WIFI_CONNECTED) return MCS_HAL_ETIMEOUT;
    if (!strcmp(host, own) || !strcmp(host, g_s.hostname)) { memcpy(ip, g_s.sta_ip.ip, 4); return 0; }
    if (!strcmp(host, "printer.local") || !strcmp(host, "printer")) { set4(ip, 192, 168, 1, 20); return 0; }
    return MCS_HAL_ETIMEOUT;
}
static int ping(void* ctx, const uint8_t ip[4], uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (g_s.status != MCS_WIFI_CONNECTED) return ip[0] == 127 ? 0 : MCS_HAL_ETIMEOUT;
    if (!memcmp(ip, g_s.sta_ip.ip, 4) || ip[0] == 127) return 0;
    if (!memcmp(ip, g_s.sta_ip.gateway, 4)) return 1;
    if (ip[0] == 8 && ip[1] == 8) return 12;
    if (ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 20) return 4;
    return MCS_HAL_ETIMEOUT;
}

/* WPS / SmartConfig: the "router" (or phone) answers after ten polls (~1 s) with MicroCS-Lab */
static int sim_prov_polls;
static int provision(void* ctx, int method, int op, char ssid[33], char pass[65]) {
    (void)ctx;
    if (method != MCS_WIFI_PROV_WPS && method != MCS_WIFI_PROV_SMARTCONFIG) return MCS_HAL_ENOTSUP;
    if (op == MCS_WIFI_PROV_QUERY || op == MCS_WIFI_PROV_STOP) return 0;
    if (!(g_s.mode & MCS_WIFI_STA)) set_mode(NULL, g_s.mode | MCS_WIFI_STA);
    if (op == MCS_WIFI_PROV_START) { sim_prov_polls = 0; return 0; }
    if (++sim_prov_polls < 10) return 0;
    snprintf(ssid, 33, "MicroCS-Lab"); snprintf(pass, 65, "microcs123");
    return 1;
}
void mcs_net_sim_init(mcs_net_t* n) {
    memset(&g_s, 0, sizeof g_s);
    g_s.ap = -1; g_s.status = MCS_WIFI_STOPPED;
    static const uint8_t sta[6] = { 0x24, 0x0A, 0xC4, 0x00, 0x00, 0x01 };
    memcpy(g_s.mac[0], sta, 6); memcpy(g_s.mac[1], sta, 6); g_s.mac[1][5] = 0x02;
    dhcp_defaults();
    set4(g_s.ap_ip.ip, 192, 168, 4, 1); set4(g_s.ap_ip.netmask, 255, 255, 255, 0); set4(g_s.ap_ip.gateway, 192, 168, 4, 1);
    g_s.opt[MCS_WIFI_OPT_AUTO_RECONNECT] = 1; g_s.opt[MCS_WIFI_OPT_POWER_SAVE] = MCS_WIFI_PS_MIN;
    g_s.opt[MCS_WIFI_OPT_TX_POWER] = 80; g_s.opt[MCS_WIFI_OPT_PROTOCOL] = 7; g_s.opt[MCS_WIFI_OPT_AP_PROTOCOL] = 7;
    g_s.opt[MCS_WIFI_OPT_BANDWIDTH] = 20; g_s.opt[MCS_WIFI_OPT_MIN_AUTH] = MCS_WIFI_AUTH_OPEN;
    g_s.opt[MCS_WIFI_OPT_CHANNEL] = 1; g_s.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] = 3; g_s.opt[MCS_WIFI_OPT_BAND] = MCS_WIFI_BAND_2G;
    snprintf(g_s.hostname, sizeof g_s.hostname, "microcs"); snprintf(g_s.country, sizeof g_s.country, "01");
    memset(n, 0, sizeof *n);
    n->name = "sim";
    n->wifi_set_mode = set_mode; n->wifi_get_mode = get_mode;
    n->sta_connect = sta_connect; n->sta_disconnect = sta_disconnect; n->sta_link = sta_link; n->scan = scan;
    n->ap_start = ap_start; n->ap_stop = ap_stop; n->ap_clients = ap_clients;
    n->ip_get = ip_get; n->ip_set = ip_set; n->mac_get = mac_get; n->mac_set = mac_set;
    n->opt_get = opt_get; n->opt_set = opt_set; n->str_get = str_get; n->str_set = str_set; n->creds = creds;
    n->espnow_start = en_start; n->espnow_stop = en_stop; n->espnow_peer = en_peer; n->espnow_send = en_send;
    n->espnow_recv = en_recv; n->espnow_pmk = en_pmk; n->espnow_peers = en_peers;
    n->ntp_start = ntp_start; n->ntp_synced = ntp_synced;
    n->mdns = mdns; n->mdns_resolve = mdns_resolve; n->ping = ping;
    n->provision = provision;
}
#endif
