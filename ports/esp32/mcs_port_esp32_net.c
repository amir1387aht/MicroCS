/*
 * MicroCS - ESP32 Wi-Fi and networking backend (mcs_net_t, include/mcs_net.h, docs/NETWORK.md).
 *
 * Station + soft-AP (esp_wifi / esp_netif), scan, static IP, DHCP host name, country,
 * power save, TX power, protocols, bandwidth, WPA2/WPA3-Enterprise (esp_eap_client),
 * credentials in NVS, auto-reconnect, ESP-NOW, SNTP, ICMP ping, esp-tls (certificate
 * bundle) for HTTPS / TcpClient.ConnectTls, and mDNS when the espressif/mdns component
 * is part of the project. Sockets, DNS and HTTP are the portable modules/net/mcs_net_sock.c
 * on top of lwIP.
 *
 * Nothing is started at boot: the radio, the netifs and the Wi-Fi driver (~50 KB of RAM)
 * come up on the first Wifi / EspNow call (or mcs_esp32_wifi_autoconnect()).
 */
#include "mcs_port_esp32.h"
#if MCS_ESP32_WIFI
#include "mcs_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_idf_version.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "ping/ping_sock.h"
#include "lwip/ip4_addr.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "esp_eap_client.h"
#define MCS_ESP_EAP 1
#endif
#if MCS_ESP32_MDNS
#include "mdns.h"
#endif

#ifndef MCS_ESP32_WIFI_RETRIES
#define MCS_ESP32_WIFI_RETRIES 2        /* extra attempts before Wifi.Connect reports a failure */
#endif
#ifndef MCS_ESP32_ESPNOW_QUEUE
#define MCS_ESP32_ESPNOW_QUEUE 8        /* received ESP-NOW packets kept for EspNow.Receive */
#endif

static const char* TAG = "mcs-net";
typedef struct {
    int inited, started, mode;
    volatile int status, reason, retries, user_disc, connecting, sta_up;
    int32_t opt[MCS_WIFI_OPT__COUNT];
    char hostname[33], country[3];
    esp_netif_t *sta, *ap;
    int8_t ap_aid_rssi;
    struct { uint8_t mac[6]; uint8_t ip[4]; uint8_t used; } cl[16];
    int enterprise;
    /* ESP-NOW */
    int en_on;
    portMUX_TYPE mux;
    struct { uint8_t mac[6]; int8_t rssi; uint8_t n; uint8_t d[250]; } *rx;
    volatile int rx_head, rx_n;
    volatile int tx_done, tx_ok;
    /* SNTP */
    volatile int ntp_synced;
    char ntp_srv[3][48];
} esp_net_t;
static esp_net_t g;
static mcs_net_t g_table;

#define TRY(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) { ESP_LOGW(TAG, "%s: %s", #x, esp_err_to_name(e_)); return map_err(e_); } } while (0)
static int map_err(esp_err_t e) {
    switch (e) {
    case ESP_OK: return 0;
    case ESP_ERR_INVALID_ARG: case ESP_ERR_INVALID_SIZE: return MCS_HAL_EINVAL;
    case ESP_ERR_TIMEOUT: return MCS_HAL_ETIMEOUT;
    case ESP_ERR_NOT_SUPPORTED: return MCS_HAL_ENOTSUP;
    case ESP_ERR_NOT_FOUND: case ESP_ERR_ESPNOW_NOT_FOUND: return MCS_HAL_ENODEV;
    case ESP_ERR_WIFI_NOT_STARTED: case ESP_ERR_WIFI_NOT_INIT: case ESP_ERR_WIFI_MODE: case ESP_ERR_INVALID_STATE: return MCS_HAL_EINVAL;
    case ESP_ERR_NO_MEM: return MCS_HAL_ERR;
    default: return MCS_HAL_ERR;
    }
}
static void post(int ev, int32_t v) { mcs_net_post(ev, v); }
static int32_t ip_word(uint32_t addr) { return (int32_t)addr; }        /* lwIP keeps network order */

/* ------------------------------------------------------------ events */
static bool reason_no_ap(int r) { return r == WIFI_REASON_NO_AP_FOUND || r == 210 || r == 211 || r == 212; }
static bool reason_auth(int r) {
    return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT
        || r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_MIC_FAILURE || r == 23;
}
static void on_wifi(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg; (void)base;
    switch (id) {
    case WIFI_EVENT_STA_START: g.sta_up = 1; post(MCS_NET_EV_STA_START, 0); break;
    case WIFI_EVENT_STA_STOP: g.sta_up = 0; g.status = MCS_WIFI_STOPPED; post(MCS_NET_EV_STA_STOP, 0); break;
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t* e = (wifi_event_sta_connected_t*)data;
#if CONFIG_LWIP_IPV6
        if (g.sta) esp_netif_create_ip6_linklocal(g.sta);
#endif
        post(MCS_NET_EV_CONNECTED, e->channel);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t* e = (wifi_event_sta_disconnected_t*)data;
        int r = e->reason;
        bool was_up = g.status == MCS_WIFI_CONNECTED;
        g.reason = r;
        post(MCS_NET_EV_DISCONNECTED, r);
        if (g.user_disc) { g.status = MCS_WIFI_DISCONNECTED; break; }
        if (g.connecting && !was_up) {                       /* Wifi.Connect in progress */
            if (g.retries < MCS_ESP32_WIFI_RETRIES && !(reason_no_ap(r) && g.retries > 0)) { g.retries++; esp_wifi_connect(); break; }
            g.connecting = 0;
            g.status = reason_no_ap(r) ? MCS_WIFI_NO_SSID : reason_auth(r) ? MCS_WIFI_WRONG_PASSWORD : MCS_WIFI_CONNECT_FAILED;
            break;
        }
        g.status = MCS_WIFI_CONNECTION_LOST;
        if (g.opt[MCS_WIFI_OPT_AUTO_RECONNECT]) esp_wifi_connect();  /* keep trying while the AP is gone */
        break;
    }
    case WIFI_EVENT_SCAN_DONE: { wifi_event_sta_scan_done_t* e = (wifi_event_sta_scan_done_t*)data; post(MCS_NET_EV_SCAN_DONE, e->number); break; }
    case WIFI_EVENT_AP_START: post(MCS_NET_EV_AP_START, 0); break;
    case WIFI_EVENT_AP_STOP: memset(g.cl, 0, sizeof g.cl); post(MCS_NET_EV_AP_STOP, 0); break;
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
        for (int i = 0; i < 16; i++) if (!g.cl[i].used) { memcpy(g.cl[i].mac, e->mac, 6); memset(g.cl[i].ip, 0, 4); g.cl[i].used = 1; break; }
        post(MCS_NET_EV_AP_JOINED, e->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
        for (int i = 0; i < 16; i++) if (g.cl[i].used && !memcmp(g.cl[i].mac, e->mac, 6)) g.cl[i].used = 0;
        post(MCS_NET_EV_AP_LEFT, e->aid);
        break;
    }
    default: break;
    }
}
static void on_ip(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg; (void)base;
    switch (id) {
    case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
        g.status = MCS_WIFI_CONNECTED; g.reason = 0; g.connecting = 0; g.retries = 0;
        post(MCS_NET_EV_GOT_IP, ip_word(e->ip_info.ip.addr));
        break;
    }
    case IP_EVENT_STA_LOST_IP: post(MCS_NET_EV_LOST_IP, 0); break;
#if CONFIG_LWIP_IPV6
    case IP_EVENT_GOT_IP6: post(MCS_NET_EV_GOT_IP6, 0); break;
#endif
    case IP_EVENT_AP_STAIPASSIGNED: {
        ip_event_ap_staipassigned_t* e = (ip_event_ap_staipassigned_t*)data;
        for (int i = 0; i < 16; i++) if (g.cl[i].used && !memcmp(g.cl[i].mac, e->mac, 6)) memcpy(g.cl[i].ip, &e->ip.addr, 4);
        post(MCS_NET_EV_AP_ASSIGNED, ip_word(e->ip.addr));
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------ bring-up */
static int nvs_ready(void) {
    static int done;
    if (done) return 0;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); e = nvs_flash_init(); }
    if (e != ESP_OK) { ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(e)); return map_err(e); }
    done = 1;
    return 0;
}
static int wifi_init(void) {
    if (g.inited) return 0;
    int rc = nvs_ready();
    if (rc < 0) return rc;
    TRY(esp_netif_init());
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return map_err(e);
    if (!g.sta) g.sta = esp_netif_create_default_wifi_sta();
    if (!g.ap) g.ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    TRY(esp_wifi_init(&cfg));
    TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    TRY(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip, NULL, NULL));
    esp_wifi_set_storage(g.opt[MCS_WIFI_OPT_STORAGE] ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);
    if (g.hostname[0]) esp_netif_set_hostname(g.sta, g.hostname);
    g.inited = 1;
    return 0;
}
static int apply_runtime_opts(void) {      /* settings that need a started driver */
    if (g.opt[MCS_WIFI_OPT_POWER_SAVE] >= 0) esp_wifi_set_ps((wifi_ps_type_t)g.opt[MCS_WIFI_OPT_POWER_SAVE]);
    if (g.opt[MCS_WIFI_OPT_TX_POWER] > 0) esp_wifi_set_max_tx_power((int8_t)g.opt[MCS_WIFI_OPT_TX_POWER]);
    if (g.country[0]) esp_wifi_set_country_code(g.country, true);
    if (g.mode & MCS_WIFI_STA) {
        if (g.opt[MCS_WIFI_OPT_PROTOCOL] > 0) esp_wifi_set_protocol(WIFI_IF_STA, (uint8_t)g.opt[MCS_WIFI_OPT_PROTOCOL]);
        if (g.opt[MCS_WIFI_OPT_BANDWIDTH]) esp_wifi_set_bandwidth(WIFI_IF_STA, g.opt[MCS_WIFI_OPT_BANDWIDTH] == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20);
    }
    if ((g.mode & MCS_WIFI_AP) && g.opt[MCS_WIFI_OPT_AP_PROTOCOL] > 0) esp_wifi_set_protocol(WIFI_IF_AP, (uint8_t)g.opt[MCS_WIFI_OPT_AP_PROTOCOL]);
    return 0;
}
static int set_mode(void* ctx, int mode) {
    (void)ctx;
    if (mode < MCS_WIFI_OFF || mode > MCS_WIFI_APSTA) return MCS_HAL_EINVAL;
    if (mode == MCS_WIFI_OFF) {
        if (g.en_on) { esp_now_deinit(); g.en_on = 0; }
        if (g.started) { g.user_disc = 1; esp_wifi_stop(); g.started = 0; }
        g.mode = MCS_WIFI_OFF; g.status = MCS_WIFI_STOPPED;
        return 0;
    }
    int rc = wifi_init();
    if (rc < 0) return rc;
    static const wifi_mode_t m[4] = { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
    TRY(esp_wifi_set_mode(m[mode]));
    g.mode = mode;
    if (!g.started) {
        TRY(esp_wifi_start());
        g.started = 1;
        if (mode & MCS_WIFI_STA) g.status = MCS_WIFI_DISCONNECTED;
    }
    apply_runtime_opts();
    return 0;
}
static int get_mode(void* ctx) { (void)ctx; return g.mode; }

/* ------------------------------------------------------------ station */
static int sta_connect(void* ctx, const mcs_wifi_sta_cfg_t* c) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    g.user_disc = 1;                           /* the disconnect below is ours */
    esp_wifi_disconnect();
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    memcpy(wc.sta.ssid, c->ssid, strnlen(c->ssid, 32));
    memcpy(wc.sta.password, c->password, strnlen(c->password, 64));
    int min_auth = c->min_auth >= 0 ? c->min_auth : g.opt[MCS_WIFI_OPT_MIN_AUTH];
    wc.sta.threshold.authmode = (wifi_auth_mode_t)(min_auth > 0 ? min_auth : WIFI_AUTH_OPEN);
    wc.sta.scan_method = c->channel || c->has_bssid ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.channel = c->channel;
    if (c->has_bssid) { wc.sta.bssid_set = 1; memcpy(wc.sta.bssid, c->bssid, 6); }
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wc.sta.listen_interval = (uint16_t)(g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] > 0 ? g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] : 3);
    TRY(esp_wifi_set_config(WIFI_IF_STA, &wc));
#if MCS_ESP_EAP
    if (c->identity) {
        const char* user = c->username ? c->username : c->identity;
        esp_eap_client_set_identity((const unsigned char*)c->identity, (int)strlen(c->identity));
        esp_eap_client_set_username((const unsigned char*)user, (int)strlen(user));
        esp_eap_client_set_password((const unsigned char*)c->password, (int)strlen(c->password));
        if (c->ca_pem) esp_eap_client_set_ca_cert((const unsigned char*)c->ca_pem, (int)strlen(c->ca_pem) + 1);
        TRY(esp_wifi_sta_enterprise_enable());
        g.enterprise = 1;
    } else if (g.enterprise) { esp_wifi_sta_enterprise_disable(); g.enterprise = 0; }
#else
    if (c->identity) return MCS_HAL_ENOTSUP;
#endif
    if (g.hostname[0]) esp_netif_set_hostname(g.sta, g.hostname);
    g.retries = 0; g.reason = 0; g.user_disc = 0; g.connecting = 1;
    g.status = MCS_WIFI_CONNECTING;
    TRY(esp_wifi_connect());
    return 0;
}
static int sta_disconnect(void* ctx) {
    (void)ctx;
    g.user_disc = 1; g.connecting = 0;
    if (g.started) esp_wifi_disconnect();
    g.status = MCS_WIFI_DISCONNECTED;
    return 0;
}
static uint8_t protos(const wifi_ap_record_t* r) {
    uint8_t p = 0;
    if (r->phy_11b) p |= MCS_WIFI_PROTO_B;
    if (r->phy_11g) p |= MCS_WIFI_PROTO_G;
    if (r->phy_11n) p |= MCS_WIFI_PROTO_N;
    if (r->phy_lr) p |= MCS_WIFI_PROTO_LR;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    if (r->phy_11ax) p |= MCS_WIFI_PROTO_AX;
#endif
    return p;
}
static void ap_rec(const wifi_ap_record_t* r, mcs_wifi_ap_info_t* o) {
    memset(o, 0, sizeof *o);
    memcpy(o->ssid, r->ssid, strnlen((const char*)r->ssid, 32));
    memcpy(o->bssid, r->bssid, 6);
    o->rssi = r->rssi; o->channel = r->primary;
    o->auth = (uint8_t)((int)r->authmode < (int)MCS_WIFI_AUTH__COUNT ? (int)r->authmode : (int) MCS_WIFI_AUTH_ENTERPRISE);
    o->protocols = protos(r);
}
static int sta_link(void* ctx, mcs_wifi_link_t* l) {
    (void)ctx;
    memset(l, 0, sizeof *l);
    l->status = g.mode == MCS_WIFI_OFF ? (g.inited ? MCS_WIFI_STOPPED : MCS_WIFI_IDLE) : (g.mode & MCS_WIFI_STA) ? g.status : MCS_WIFI_IDLE;
    l->reason = g.reason;
    if (g.status == MCS_WIFI_CONNECTED) {
        wifi_ap_record_t r;
        if (esp_wifi_sta_get_ap_info(&r) == ESP_OK) ap_rec(&r, &l->ap);
    }
    return 0;
}
static int scan(void* ctx, mcs_wifi_ap_info_t* out, int max, int hidden, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!(g.mode & MCS_WIFI_STA)) return MCS_HAL_EINVAL;
    wifi_scan_config_t sc;
    memset(&sc, 0, sizeof sc);
    sc.show_hidden = hidden != 0;
    TRY(esp_wifi_scan_start(&sc, true));            /* blocks ~2 s (all channels) */
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > max) n = (uint16_t)max;
    if (!n) { esp_wifi_clear_ap_list(); return 0; }
    wifi_ap_record_t* r = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * n);
    if (!r) { esp_wifi_clear_ap_list(); return MCS_HAL_ERR; }
    esp_err_t e = esp_wifi_scan_get_ap_records(&n, r);
    if (e == ESP_OK) for (int i = 0; i < n; i++) ap_rec(&r[i], &out[i]);
    free(r);
    return e == ESP_OK ? n : map_err(e);
}

/* ------------------------------------------------------------ soft-AP */
static int ap_start(void* ctx, const mcs_wifi_ap_cfg_t* c) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_AP)) return MCS_HAL_EINVAL;
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    size_t sl = strnlen(c->ssid, 32);
    memcpy(wc.ap.ssid, c->ssid, sl); wc.ap.ssid_len = (uint8_t)sl;
    memcpy(wc.ap.password, c->password, strnlen(c->password, 64));
    wc.ap.channel = c->channel ? c->channel : 1;
    if (g.status == MCS_WIFI_CONNECTED) { uint8_t p; wifi_second_chan_t s; if (esp_wifi_get_channel(&p, &s) == ESP_OK) wc.ap.channel = p; }
    wc.ap.authmode = c->password[0] ? (c->auth ? (wifi_auth_mode_t)c->auth : WIFI_AUTH_WPA2_PSK) : WIFI_AUTH_OPEN;
    wc.ap.ssid_hidden = c->hidden;
    wc.ap.max_connection = c->max_clients ? c->max_clients : 4;
    wc.ap.pmf_cfg.required = false;
    TRY(esp_wifi_set_config(WIFI_IF_AP, &wc));
    return 0;
}
static int ap_stop(void* ctx) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_AP)) return 0;
    return set_mode(NULL, g.mode & ~MCS_WIFI_AP);
}
static int ap_clients(void* ctx, mcs_wifi_client_t* out, int max) {
    (void)ctx;
    if (!(g.mode & MCS_WIFI_AP)) return 0;
    wifi_sta_list_t l;
    if (esp_wifi_ap_get_sta_list(&l) != ESP_OK) return 0;
    int n = 0;
    for (int i = 0; i < l.num && n < max; i++) {
        memcpy(out[n].mac, l.sta[i].mac, 6);
        out[n].rssi = l.sta[i].rssi;
        memset(out[n].ip, 0, 4);
        for (int k = 0; k < 16; k++) if (g.cl[k].used && !memcmp(g.cl[k].mac, l.sta[i].mac, 6)) memcpy(out[n].ip, g.cl[k].ip, 4);
        n++;
    }
    return n;
}

/* ------------------------------------------------------------ IP / MAC */
static esp_netif_t* nif(int iface) { return iface == MCS_NET_IF_AP ? g.ap : g.sta; }
static int ip_get(void* ctx, int iface, mcs_net_ip_t* o) {
    (void)ctx;
    memset(o, 0, sizeof *o);
    esp_netif_t* n = nif(iface);
    if (!n) { o->dhcp = iface == MCS_NET_IF_STA; return 0; }
    esp_netif_ip_info_t ii;
    if (esp_netif_get_ip_info(n, &ii) == ESP_OK) {
        memcpy(o->ip, &ii.ip.addr, 4); memcpy(o->netmask, &ii.netmask.addr, 4); memcpy(o->gateway, &ii.gw.addr, 4);
    }
    esp_netif_dns_info_t d;
    if (esp_netif_get_dns_info(n, ESP_NETIF_DNS_MAIN, &d) == ESP_OK) memcpy(o->dns1, &d.ip.u_addr.ip4.addr, 4);
    if (esp_netif_get_dns_info(n, ESP_NETIF_DNS_BACKUP, &d) == ESP_OK) memcpy(o->dns2, &d.ip.u_addr.ip4.addr, 4);
    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;
    if (iface == MCS_NET_IF_STA) { esp_netif_dhcpc_get_status(n, &st); o->dhcp = st != ESP_NETIF_DHCP_STOPPED; }
    return 0;
}
static void set_dns(esp_netif_t* n, esp_netif_dns_type_t t, const uint8_t ip[4]) {
    if (!ip[0] && !ip[1] && !ip[2] && !ip[3]) return;
    esp_netif_dns_info_t d;
    memset(&d, 0, sizeof d);
    d.ip.type = ESP_IPADDR_TYPE_V4;
    memcpy(&d.ip.u_addr.ip4.addr, ip, 4);
    esp_netif_set_dns_info(n, t, &d);
}
static int ip_set(void* ctx, int iface, const mcs_net_ip_t* in) {
    (void)ctx;
    int rc = wifi_init();
    if (rc < 0) return rc;
    esp_netif_t* n = nif(iface);
    if (iface == MCS_NET_IF_STA && in->dhcp) {
        esp_err_t e = esp_netif_dhcpc_start(n);
        return e == ESP_OK || e == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ? 0 : map_err(e);
    }
    if (in->dhcp) return MCS_HAL_EINVAL;
    esp_netif_ip_info_t ii;
    memcpy(&ii.ip.addr, in->ip, 4); memcpy(&ii.netmask.addr, in->netmask, 4); memcpy(&ii.gw.addr, in->gateway, 4);
    if (iface == MCS_NET_IF_STA) {
        esp_netif_dhcpc_stop(n);
        TRY(esp_netif_set_ip_info(n, &ii));
        set_dns(n, ESP_NETIF_DNS_MAIN, in->dns1);
        set_dns(n, ESP_NETIF_DNS_BACKUP, in->dns2);
        if (g.status == MCS_WIFI_CONNECTED) post(MCS_NET_EV_GOT_IP, ip_word(ii.ip.addr));
    } else {
        esp_netif_dhcps_stop(n);
        TRY(esp_netif_set_ip_info(n, &ii));
        esp_netif_dhcps_start(n);
    }
    return 0;
}
static int mac_get(void* ctx, int iface, uint8_t m[6]) {
    (void)ctx;
    if (g.inited && esp_wifi_get_mac(iface == MCS_NET_IF_AP ? WIFI_IF_AP : WIFI_IF_STA, m) == ESP_OK) return 0;
    TRY(esp_read_mac(m, iface == MCS_NET_IF_AP ? ESP_MAC_WIFI_SOFTAP : ESP_MAC_WIFI_STA));
    return 0;
}
static int mac_set(void* ctx, int iface, const uint8_t m[6]) {
    (void)ctx;
    if (m[0] & 1) return MCS_HAL_EINVAL;
    int rc = set_mode(NULL, g.mode | (iface == MCS_NET_IF_AP ? MCS_WIFI_AP : MCS_WIFI_STA));
    if (rc < 0) return rc;
    TRY(esp_wifi_set_mac(iface == MCS_NET_IF_AP ? WIFI_IF_AP : WIFI_IF_STA, m));
    return 0;
}

/* ------------------------------------------------------------ options */
static int opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    if (opt <= 0 || opt >= MCS_WIFI_OPT__COUNT) return MCS_HAL_EINVAL;
    if (g.started) {
        switch (opt) {
        case MCS_WIFI_OPT_POWER_SAVE: { wifi_ps_type_t p; if (esp_wifi_get_ps(&p) == ESP_OK) { *v = p; return 0; } break; }
        case MCS_WIFI_OPT_TX_POWER: { int8_t p; if (esp_wifi_get_max_tx_power(&p) == ESP_OK) { *v = p; return 0; } break; }
        case MCS_WIFI_OPT_CHANNEL: { uint8_t p; wifi_second_chan_t s; if (esp_wifi_get_channel(&p, &s) == ESP_OK) { *v = p; return 0; } break; }
        case MCS_WIFI_OPT_PROTOCOL: case MCS_WIFI_OPT_AP_PROTOCOL: {
            uint8_t p;
            if ((g.mode & (opt == MCS_WIFI_OPT_PROTOCOL ? MCS_WIFI_STA : MCS_WIFI_AP))
                && esp_wifi_get_protocol(opt == MCS_WIFI_OPT_PROTOCOL ? WIFI_IF_STA : WIFI_IF_AP, &p) == ESP_OK) { *v = p; return 0; }
            break;
        }
        case MCS_WIFI_OPT_BANDWIDTH: {
            wifi_bandwidth_t b;
            if ((g.mode & MCS_WIFI_STA) && esp_wifi_get_bandwidth(WIFI_IF_STA, &b) == ESP_OK) { *v = b == WIFI_BW_HT40 ? 40 : 20; return 0; }
            break;
        }
        default: break;
        }
    }
    *v = g.opt[opt];
    return 0;
}
static int opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_WIFI_OPT_AUTO_RECONNECT: g.opt[opt] = v != 0; return 0;
    case MCS_WIFI_OPT_STORAGE:
        g.opt[opt] = v != 0;
        if (g.inited) TRY(esp_wifi_set_storage(v ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM));
        return 0;
    case MCS_WIFI_OPT_POWER_SAVE:
        if (v < MCS_WIFI_PS_NONE || v > MCS_WIFI_PS_MAX) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.started) TRY(esp_wifi_set_ps((wifi_ps_type_t)v));
        return 0;
    case MCS_WIFI_OPT_TX_POWER:
        if (v < 8 || v > 84) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.started) TRY(esp_wifi_set_max_tx_power((int8_t)v));
        return 0;
    case MCS_WIFI_OPT_PROTOCOL: case MCS_WIFI_OPT_AP_PROTOCOL:
        if (v <= 0 || (v & ~0x2F)) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.started && (g.mode & (opt == MCS_WIFI_OPT_PROTOCOL ? MCS_WIFI_STA : MCS_WIFI_AP)))
            TRY(esp_wifi_set_protocol(opt == MCS_WIFI_OPT_PROTOCOL ? WIFI_IF_STA : WIFI_IF_AP, (uint8_t)v));
        return 0;
    case MCS_WIFI_OPT_BANDWIDTH:
        if (v != 20 && v != 40) return MCS_HAL_EINVAL;
        g.opt[opt] = v;
        if (g.started && (g.mode & MCS_WIFI_STA)) TRY(esp_wifi_set_bandwidth(WIFI_IF_STA, v == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20));
        return 0;
    case MCS_WIFI_OPT_MIN_AUTH:
        if (v < 0 || v >= MCS_WIFI_AUTH__COUNT) return MCS_HAL_EINVAL;
        g.opt[opt] = v; return 0;
    case MCS_WIFI_OPT_LISTEN_INTERVAL:
        if (v < 1 || v > 100) return MCS_HAL_EINVAL;
        g.opt[opt] = v; return 0;
    case MCS_WIFI_OPT_CHANNEL: {
        if (v < 1 || v > 165) return MCS_HAL_EINVAL;
        int rc = g.started ? 0 : set_mode(NULL, MCS_WIFI_STA);
        if (rc < 0) return rc;
        TRY(esp_wifi_set_channel((uint8_t)v, WIFI_SECOND_CHAN_NONE));
        g.opt[opt] = v; return 0;
    }
    case MCS_WIFI_OPT_BAND:
        if (v < MCS_WIFI_BAND_2G || v > MCS_WIFI_BAND_AUTO) return MCS_HAL_EINVAL;
#if CONFIG_SOC_WIFI_SUPPORT_5G && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        if (g.started) TRY(esp_wifi_set_band_mode(v == MCS_WIFI_BAND_2G ? WIFI_BAND_MODE_2G_ONLY : v == MCS_WIFI_BAND_5G ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_AUTO));
        g.opt[opt] = v; return 0;
#else
        if (v != MCS_WIFI_BAND_2G && v != MCS_WIFI_BAND_AUTO) return MCS_HAL_ENOTSUP;   /* 2.4 GHz-only chip */
        g.opt[opt] = v; return 0;
#endif
    default: return MCS_HAL_ENOTSUP;
    }
}
static int str_get(void* ctx, int opt, char* buf, size_t cap) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) {
        const char* h = NULL;
        if (g.sta && esp_netif_get_hostname(g.sta, &h) == ESP_OK && h) snprintf(buf, cap, "%s", h);
        else snprintf(buf, cap, "%s", g.hostname[0] ? g.hostname : "espressif");
        return 0;
    }
    if (opt == MCS_WIFI_STR_COUNTRY) {
        char cc[3] = { 0 };
        if (g.started && esp_wifi_get_country_code(cc) == ESP_OK) { snprintf(buf, cap, "%.2s", cc); return 0; }
        snprintf(buf, cap, "%s", g.country[0] ? g.country : "01");
        return 0;
    }
    if (opt == MCS_WIFI_STR_IP6) {
        if (cap) buf[0] = 0;
#if CONFIG_LWIP_IPV6
        if (g.sta && g.status == MCS_WIFI_CONNECTED) {
            esp_ip6_addr_t a[LWIP_IPV6_NUM_ADDRESSES];
            int n = esp_netif_get_all_ip6(g.sta, a);
            size_t o = 0;
            for (int i = 0; i < n && o + 42 < cap; i++)
                o += (size_t)snprintf(buf + o, cap - o, "%s" IPV6STR, i ? " " : "", IPV62STR(a[i]));
        }
#endif
        return 0;
    }
    return MCS_HAL_EINVAL;
}
static int str_set(void* ctx, int opt, const char* v) {
    (void)ctx;
    if (opt == MCS_WIFI_STR_HOSTNAME) {
        snprintf(g.hostname, sizeof g.hostname, "%s", v);
        if (g.sta) TRY(esp_netif_set_hostname(g.sta, g.hostname));   /* used from the next DHCP request */
        return 0;
    }
    if (opt == MCS_WIFI_STR_COUNTRY) {
        if (strlen(v) != 2) return MCS_HAL_EINVAL;
        snprintf(g.country, sizeof g.country, "%s", v);
        if (g.started) TRY(esp_wifi_set_country_code(g.country, true));
        return 0;
    }
    return MCS_HAL_EINVAL;
}
static int creds(void* ctx, int op, char ssid[33], char pass[65]) {
    (void)ctx;
    int rc = nvs_ready();
    if (rc < 0) return rc;
    nvs_handle_t h;
    esp_err_t e = nvs_open("microcs", op == MCS_WIFI_CREDS_LOAD ? NVS_READONLY : NVS_READWRITE, &h);
    if (e != ESP_OK) return op == MCS_WIFI_CREDS_LOAD ? MCS_HAL_ENODEV : map_err(e);
    if (op == MCS_WIFI_CREDS_LOAD) {
        size_t a = 33, b = 65;
        e = nvs_get_str(h, "wifi_ssid", ssid, &a);
        if (e == ESP_OK && nvs_get_str(h, "wifi_pass", pass, &b) != ESP_OK) pass[0] = 0;
        nvs_close(h);
        return e == ESP_OK ? 0 : MCS_HAL_ENODEV;
    }
    if (op == MCS_WIFI_CREDS_SAVE) {
        e = nvs_set_str(h, "wifi_ssid", ssid);
        if (e == ESP_OK) e = nvs_set_str(h, "wifi_pass", pass);
    } else {
        nvs_erase_key(h, "wifi_ssid"); nvs_erase_key(h, "wifi_pass");
        e = ESP_OK;
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return map_err(e);
}

/* ------------------------------------------------------------ ESP-NOW */
static void en_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!g.rx || len <= 0) return;
    portENTER_CRITICAL(&g.mux);
    if (g.rx_n < MCS_ESP32_ESPNOW_QUEUE) {
        int k = (g.rx_head + g.rx_n) % MCS_ESP32_ESPNOW_QUEUE;
        memcpy(g.rx[k].mac, info->src_addr, 6);
        g.rx[k].rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
        g.rx[k].n = (uint8_t)(len > 250 ? 250 : len);
        memcpy(g.rx[k].d, data, g.rx[k].n);
        g.rx_n++;
    }
    int n = g.rx_n;
    portEXIT_CRITICAL(&g.mux);
    post(MCS_NET_EV_ESPNOW_RECV, n);
}
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void en_send_cb(const esp_now_send_info_t* info, esp_now_send_status_t st) {
    (void)info;
#else
static void en_send_cb(const uint8_t* mac, esp_now_send_status_t st) {
    (void)mac;
#endif
    g.tx_ok = st == ESP_NOW_SEND_SUCCESS;
    g.tx_done = 1;
    post(MCS_NET_EV_ESPNOW_SENT, g.tx_ok);
}
static int en_start(void* ctx) {
    (void)ctx;
    if (g.en_on) return 0;
    if (!g.started) { int rc = set_mode(NULL, MCS_WIFI_STA); if (rc < 0) return rc; }
    if (!g.rx) {
        g.rx = calloc(MCS_ESP32_ESPNOW_QUEUE, sizeof *g.rx);
        if (!g.rx) return MCS_HAL_ERR;
    }
    g.rx_head = g.rx_n = 0;
    TRY(esp_now_init());
    esp_now_register_recv_cb(en_recv_cb);
    esp_now_register_send_cb(en_send_cb);
    g.en_on = 1;
    return 0;
}
static int en_stop(void* ctx) {
    (void)ctx;
    if (!g.en_on) return 0;
    esp_now_deinit();
    g.en_on = 0;
    return 0;
}
static wifi_interface_t en_if(void) { return (g.mode & MCS_WIFI_STA) ? WIFI_IF_STA : WIFI_IF_AP; }
static int en_peer(void* ctx, const uint8_t m[6], int add, int ch, const uint8_t* lmk) {
    (void)ctx;
    if (!g.en_on) return MCS_HAL_EINVAL;
    if (!add) { TRY(esp_now_del_peer(m)); return 0; }
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof p);
    memcpy(p.peer_addr, m, 6);
    p.channel = (uint8_t)ch;
    p.ifidx = en_if();
    if (lmk) { p.encrypt = true; memcpy(p.lmk, lmk, 16); }
    esp_err_t e = esp_now_is_peer_exist(m) ? esp_now_mod_peer(&p) : esp_now_add_peer(&p);
    if (e == ESP_ERR_ESPNOW_FULL) return MCS_HAL_EBUSY;
    return map_err(e);
}
static int en_send(void* ctx, const uint8_t m[6], const uint8_t* d, size_t n, uint32_t timeout) {
    (void)ctx;
    if (!g.en_on) return MCS_HAL_EINVAL;
    static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (!memcmp(m, bc, 6) && !esp_now_is_peer_exist(bc)) { int rc = en_peer(NULL, bc, 1, 0, NULL); if (rc < 0) return rc; }
    if (!esp_now_is_peer_exist(m)) return MCS_HAL_EINVAL;   /* EspNow.AddPeer first */
    g.tx_done = 0; g.tx_ok = 0;
    TRY(esp_now_send(m, d, n));
    TickType_t t0 = xTaskGetTickCount(), lim = pdMS_TO_TICKS(timeout ? timeout : 1000);
    while (!g.tx_done) {
        if (xTaskGetTickCount() - t0 > lim) return MCS_HAL_ETIMEOUT;
        vTaskDelay(1);
    }
    return g.tx_ok ? 0 : MCS_HAL_ENODEV;
}
static int en_recv(void* ctx, uint8_t m[6], uint8_t* buf, size_t cap, int* rssi) {
    (void)ctx;
    if (!g.rx) return 0;
    int n = 0;
    portENTER_CRITICAL(&g.mux);
    if (g.rx_n) {
        int k = g.rx_head;
        n = g.rx[k].n < cap ? g.rx[k].n : (int)cap;
        memcpy(m, g.rx[k].mac, 6); memcpy(buf, g.rx[k].d, (size_t)n);
        if (rssi) *rssi = g.rx[k].rssi;
        g.rx_head = (g.rx_head + 1) % MCS_ESP32_ESPNOW_QUEUE; g.rx_n--;
    }
    portEXIT_CRITICAL(&g.mux);
    return n;
}
static int en_pmk(void* ctx, const uint8_t k[16]) { (void)ctx; if (!g.en_on) return MCS_HAL_EINVAL; TRY(esp_now_set_pmk(k)); return 0; }
static int en_peers(void* ctx, uint8_t (*macs)[6], int max) {
    (void)ctx;
    if (!g.en_on) return 0;
    esp_now_peer_info_t p;
    int n = 0;
    static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    for (esp_err_t e = esp_now_fetch_peer(true, &p); e == ESP_OK && n < max; e = esp_now_fetch_peer(false, &p))
        if (memcmp(p.peer_addr, bc, 6)) memcpy(macs[n++], p.peer_addr, 6);
    return n;
}

/* ------------------------------------------------------------ SNTP */
static void ntp_cb(struct timeval* tv) {
    g.ntp_synced = 1;
    post(MCS_NET_EV_NTP_SYNC, (int32_t)((uint32_t)tv->tv_sec & 0x7FFFFFFFu));
}
static int ntp_start(void* ctx, const char* const* sv, int n) {
    (void)ctx;
    int rc = wifi_init();                       /* needs the TCP/IP stack */
    if (rc < 0) return rc;
    if (esp_sntp_enabled()) esp_sntp_stop();
    g.ntp_synced = 0;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    for (int i = 0; i < 3; i++) {
        if (i < n) snprintf(g.ntp_srv[i], sizeof g.ntp_srv[i], "%s", sv[i]); else g.ntp_srv[i][0] = 0;
        if (i < SNTP_MAX_SERVERS) esp_sntp_setservername((u8_t)i, g.ntp_srv[i][0] ? g.ntp_srv[i] : NULL);
    }
    sntp_set_time_sync_notification_cb(ntp_cb);
    esp_sntp_init();
    return 0;
}
static int ntp_synced(void* ctx) { (void)ctx; return g.ntp_synced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED; }

/* ------------------------------------------------------------ mDNS (espressif/mdns) */
#if MCS_ESP32_MDNS
static int md(void* ctx, int op, const char* a, const char* b, int port, const char* const* txt, int ntxt) {
    (void)ctx;
    static int on;
    switch (op) {
    case MCS_MDNS_START:
        if (!on) { TRY(mdns_init()); on = 1; }
        TRY(mdns_hostname_set(a));
        TRY(mdns_instance_name_set(b ? b : a));
        return 0;
    case MCS_MDNS_STOP: if (on) mdns_free(); on = 0; return 0;
    case MCS_MDNS_ADD: {
        if (!on) return MCS_HAL_EINVAL;
        mdns_txt_item_t items[8];
        char keys[8][32];
        int k = 0;
        for (int i = 0; i < ntxt && k < 8; i++) {
            const char* eq = strchr(txt[i], '=');
            size_t kl = eq ? (size_t)(eq - txt[i]) : strlen(txt[i]);
            if (kl >= sizeof keys[k]) kl = sizeof keys[k] - 1;
            memcpy(keys[k], txt[i], kl); keys[k][kl] = 0;
            items[k].key = keys[k]; items[k].value = eq ? eq + 1 : "";
            k++;
        }
        mdns_service_remove(a, b);
        TRY(mdns_service_add(NULL, a, b, (uint16_t)port, k ? items : NULL, (size_t)k));
        return 0;
    }
    case MCS_MDNS_REMOVE: if (!on) return MCS_HAL_EINVAL; TRY(mdns_service_remove(a, b)); return 0;
    default: return MCS_HAL_EINVAL;
    }
}
static int md_resolve(void* ctx, const char* host, uint32_t timeout, uint8_t ip[4]) {
    (void)ctx;
    char h[64];
    snprintf(h, sizeof h, "%s", host);
    size_t n = strlen(h);
    if (n > 6 && !strcmp(h + n - 6, ".local")) h[n - 6] = 0;
    esp_ip4_addr_t a;
    esp_err_t e = mdns_query_a(h, timeout, &a);
    if (e != ESP_OK) return e == ESP_ERR_NOT_FOUND ? MCS_HAL_ETIMEOUT : map_err(e);
    memcpy(ip, &a.addr, 4);
    return 0;
}
#endif

/* ------------------------------------------------------------ ping */
typedef struct { volatile int done; volatile int ms; } ping_wait_t;
static void ping_ok(esp_ping_handle_t h, void* a) {
    uint32_t t = 0;
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof t);
    ((ping_wait_t*)a)->ms = (int)t;
}
static void ping_end(esp_ping_handle_t h, void* a) { (void)h; ((ping_wait_t*)a)->done = 1; }
static int ping(void* ctx, const uint8_t ip[4], uint32_t timeout) {
    (void)ctx;
    if (!g.inited) return MCS_HAL_ETIMEOUT;
    ping_wait_t w = { 0, -1 };
    esp_ping_config_t c = ESP_PING_DEFAULT_CONFIG();
    c.count = 1;
    c.timeout_ms = timeout ? timeout : 1000;
    c.interval_ms = 10;
    ip_addr_t t;
    memset(&t, 0, sizeof t);
    IP_ADDR4(&t, ip[0], ip[1], ip[2], ip[3]);
    c.target_addr = t;
    esp_ping_callbacks_t cb = { .cb_args = &w, .on_ping_success = ping_ok, .on_ping_timeout = NULL, .on_ping_end = ping_end };
    esp_ping_handle_t h;
    TRY(esp_ping_new_session(&c, &cb, &h));
    esp_ping_start(h);
    TickType_t t0 = xTaskGetTickCount();
    while (!w.done && xTaskGetTickCount() - t0 < pdMS_TO_TICKS(c.timeout_ms + 500)) vTaskDelay(pdMS_TO_TICKS(5));
    esp_ping_stop(h);
    esp_ping_delete_session(h);
    return w.ms >= 0 ? w.ms : MCS_HAL_ETIMEOUT;
}

/* ------------------------------------------------------------ TLS (esp-tls) */
static void* tls_open(void* ctx, const char* host, int port, uint32_t timeout, const char* ca, int* err) {
    (void)ctx;
    esp_tls_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.timeout_ms = (int)timeout;
    if (ca) { cfg.cacert_buf = (const unsigned char*)ca; cfg.cacert_bytes = (unsigned int)strlen(ca) + 1; }
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    else cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    esp_tls_t* t = esp_tls_init();
    if (!t) { *err = MCS_HAL_ERR; return NULL; }
    int r = esp_tls_conn_new_sync(host, (int)strlen(host), port, &cfg, t);
    if (r != 1) {
        esp_tls_error_handle_t eh = NULL;
        int code = 0, flags = 0;
        if (esp_tls_get_error_handle(t, &eh) == ESP_OK && eh) esp_tls_get_and_clear_last_error(eh, &code, &flags);
        ESP_LOGW(TAG, "TLS %s:%d failed (0x%x, flags 0x%x)", host, port, code, flags);
        *err = code == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT ? MCS_HAL_ETIMEOUT : MCS_HAL_ERR;
        esp_tls_conn_destroy(t);
        return NULL;
    }
    return t;
}
static int tls_write(void* conn, const uint8_t* d, size_t n) {
    ssize_t r = esp_tls_conn_write((esp_tls_t*)conn, d, n);
    if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) return 0;
    return r < 0 ? MCS_HAL_ERR : (int)r;
}
static int tls_read(void* conn, uint8_t* b, size_t n, uint32_t timeout) {
    esp_tls_t* t = (esp_tls_t*)conn;
    if (esp_tls_get_bytes_avail(t) <= 0) {
        int fd = -1;
        if (esp_tls_get_conn_sockfd(t, &fd) != ESP_OK || fd < 0) return MCS_HAL_ERR;
        fd_set s; FD_ZERO(&s); FD_SET(fd, &s);
        struct timeval tv = { (long)(timeout / 1000), (long)(timeout % 1000) * 1000 };
        int r = select(fd + 1, &s, NULL, NULL, &tv);
        if (r == 0) return MCS_HAL_ETIMEOUT;
        if (r < 0) return MCS_HAL_ERR;
    }
    ssize_t r = esp_tls_conn_read(t, b, n);
    if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) return MCS_HAL_ETIMEOUT;
    if (r < 0) return r == -0x7880 /* MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY */ ? 0 : MCS_HAL_ERR;
    return (int)r;
}
static void tls_close(void* conn) { esp_tls_conn_destroy((esp_tls_t*)conn); }

/* ------------------------------------------------------------ table */
const mcs_net_t* mcs_esp32_net(void) {
    if (g_table.name) return &g_table;
    g.mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    g.status = MCS_WIFI_IDLE;
    g.opt[MCS_WIFI_OPT_AUTO_RECONNECT] = 1;
    g.opt[MCS_WIFI_OPT_POWER_SAVE] = -1;             /* the driver's default (modem sleep) */
    g.opt[MCS_WIFI_OPT_LISTEN_INTERVAL] = 3;
    g.opt[MCS_WIFI_OPT_BAND] = MCS_WIFI_BAND_2G;
    mcs_net_t* n = &g_table;
    n->ctx = &g;
    n->wifi_set_mode = set_mode; n->wifi_get_mode = get_mode;
    n->sta_connect = sta_connect; n->sta_disconnect = sta_disconnect; n->sta_link = sta_link; n->scan = scan;
    n->ap_start = ap_start; n->ap_stop = ap_stop; n->ap_clients = ap_clients;
    n->ip_get = ip_get; n->ip_set = ip_set; n->mac_get = mac_get; n->mac_set = mac_set;
    n->opt_get = opt_get; n->opt_set = opt_set; n->str_get = str_get; n->str_set = str_set; n->creds = creds;
    n->espnow_start = en_start; n->espnow_stop = en_stop; n->espnow_peer = en_peer; n->espnow_send = en_send;
    n->espnow_recv = en_recv; n->espnow_pmk = en_pmk; n->espnow_peers = en_peers;
    n->ntp_start = ntp_start; n->ntp_synced = ntp_synced;
#if MCS_ESP32_MDNS
    n->mdns = md; n->mdns_resolve = md_resolve;
#endif
    n->ping = ping;
    n->tls_open = tls_open; n->tls_write = tls_write; n->tls_read = tls_read; n->tls_close = tls_close;
    n->name = "esp32";
    return n;
}

bool mcs_esp32_wifi_autoconnect(const char* ssid, const char* password) {
    const mcs_net_t* n = mcs_esp32_net();
    mcs_wifi_sta_cfg_t c;
    memset(&c, 0, sizeof c);
    c.min_auth = -1;
    if (ssid && *ssid) {
        snprintf(c.ssid, sizeof c.ssid, "%s", ssid);
        snprintf(c.password, sizeof c.password, "%s", password ? password : "");
    } else if (n->creds(n->ctx, MCS_WIFI_CREDS_LOAD, c.ssid, c.password) < 0 || !c.ssid[0]) {
        return false;
    }
    if (n->wifi_set_mode(n->ctx, g.mode | MCS_WIFI_STA) < 0) return false;
    ESP_LOGI(TAG, "connecting to \"%s\" in the background", c.ssid);
    return n->sta_connect(n->ctx, &c) == 0;
}
#endif
