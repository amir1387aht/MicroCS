/*
 * MicroCS - Arduino Bluetooth LE backend on the ArduinoBLE library (Nano 33 BLE / BLE Sense,
 * Nano RP2040 Connect, UNO R4 WiFi, Nano 33 IoT, MKR WiFi 1010, Portenta, Nicla ...).
 *
 * Off unless the library's src/mcs_user_config.h has #define MCS_ENABLE_BLE 1
 * (python3 tools/make_arduino.py --ble does it); install ArduinoBLE with Tools > Manage
 * Libraries. mcs_arduino_hal_init registers it (mcs_ble_set), Ble.Enable() calls BLE.begin().
 *
 * What ArduinoBLE allows: advertising (raw data from BleAdvertisement), scanning (reports are
 * rebuilt from the parsed name / service UUIDs / manufacturer data), a GATT server with
 * notifications and indications, the connection events, RSSI and Disconnect. Not available
 * (Ble.Has is false, the C# calls throw NotSupportedException): the central role / GATT client,
 * pairing and bonds, the address type, TX power. ArduinoBLE keeps the characteristic values
 * itself (gatts_value keeps them in step) and notifies subscribed clients whenever a value
 * changes; descriptors are read-only. Ble.AddService after the first set of services restarts
 * the stack (BLE.end + BLE.begin): ArduinoBLE cannot remove services.
 *
 * Threads: ArduinoBLE does its work in BLE.poll(), which the front end calls from its wait
 * loops and the HAL from Hal.Poll / the REPL loop, so every callback runs on the VM's thread.
 */
#include "mcs_config.h"
#if MCS_ENABLE_BLE
#include <ArduinoBLE.h>
#include <string.h>
#include "mcs_port_arduino.h"
#include "mcs_ble.h"

#ifndef MCS_ARDUINO_BLE_MAX_CONN
#define MCS_ARDUINO_BLE_MAX_CONN 1           /* ArduinoBLE tracks one central on most boards */
#endif
#define A_MAXC (MCS_ARDUINO_BLE_MAX_CONN < MCS_BLE_MAX_CONN ? MCS_ARDUINO_BLE_MAX_CONN : MCS_BLE_MAX_CONN)

static bool g_on;
static char g_name[32] = MCS_BLE_NAME;
static int32_t g_opt[MCS_BLE_OPT__COUNT];
static BLEDevice g_dev[A_MAXC];
static bool g_used[A_MAXC];
static uint32_t g_adv_end, g_scan_end;       /* millis() deadline, 0 = none */
static bool g_advertising, g_scanning;

/* local GATT database: ArduinoBLE objects, kept until the next gatts_set */
static BLEService* g_svc[MCS_BLE_MAX_SERVICES];
static BLECharacteristic* g_chr[MCS_BLE_MAX_ATTRS];       /* by front-end id */
static BLEDescriptor* g_dsc[MCS_BLE_MAX_ATTRS];
static uint16_t g_chr_props[MCS_BLE_MAX_ATTRS];
static uint8_t g_skip[MCS_BLE_MAX_ATTRS];                /* gatts_value already notified: skip the next gatts_notify */
static int g_nsvc;
static bool g_db_added;

/* ---------------------------------------------------------------- helpers */
static void uuid_str(const mcs_ble_uuid_t* u, char out[37]) {   /* "180D" or the full 128-bit form */
    uint16_t s = mcs_ble_uuid_short(u);
    if (s) { snprintf(out, 37, "%04x", s); return; }
    uint8_t b[16];
    if (u->len == 4) {
        static const uint8_t base[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0, 0, 0, 0 };
        memcpy(b, base, 16); memcpy(b + 12, u->b, 4);
    } else memcpy(b, u->b, 16);
    int k = 0;
    for (int i = 15; i >= 0; i--) {
        k += snprintf(out + k, (size_t)(37 - k), "%02x", b[i]);
        if (i == 12 || i == 10 || i == 8 || i == 6) out[k++] = '-';
    }
    out[k] = 0;
}
static bool addr_parse(const String& s, mcs_ble_addr_t* o) {   /* "aa:bb:cc:dd:ee:ff" */
    memset(o, 0, sizeof *o);
    if (s.length() < 17) return false;
    for (int i = 0; i < 6; i++) o->a[i] = (uint8_t)strtoul(s.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
    return true;
}
static int slot_of(const BLEDevice& d) {
    String a = d.address();
    for (int i = 0; i < A_MAXC; i++) if (g_used[i] && g_dev[i].address() == a) return i;
    return -1;
}
static int chr_id(BLECharacteristic& c) {        /* the value buffer identifies the local characteristic */
    const uint8_t* v = c.value();
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++) if (g_chr[i] && g_chr[i]->value() == v) return i;
    return -1;
}

/* ---------------------------------------------------------------- events (inside BLE.poll) */
static void on_connected(BLEDevice d) {
    int s = slot_of(d);
    if (s < 0) for (int i = 0; i < A_MAXC && s < 0; i++) if (!g_used[i]) s = i;
    if (s < 0) { d.disconnect(); return; }
    g_dev[s] = d; g_used[s] = true;
    if (g_advertising) { g_advertising = false; g_adv_end = 0; BLE.stopAdvertise(); }   /* like a legacy set */
    mcs_ble_addr_t a; addr_parse(d.address(), &a);
    mcs_ble_on_connect(s + 1, &a, MCS_BLE_ROLE_PERIPHERAL);
}
static void on_disconnected(BLEDevice d) {
    int s = slot_of(d);
    if (s < 0) return;
    g_used[s] = false;
    mcs_ble_on_disconnect(s + 1, 0x13);          /* ArduinoBLE does not pass the reason */
}
static void on_discovered(BLEDevice d) {
    uint8_t ad[31]; size_t n = 0;
    if (d.hasLocalName()) {
        String nm = d.localName();
        size_t l = nm.length() > 29 ? 29 : nm.length();
        ad[n++] = (uint8_t)(l + 1); ad[n++] = 0x09; memcpy(ad + n, nm.c_str(), l); n += l;
    }
    for (int i = 0; i < d.advertisedServiceUuidCount(); i++) {
        mcs_ble_uuid_t u;
        if (!mcs_ble_uuid_parse(d.advertisedServiceUuid(i).c_str(), &u)) continue;
        uint16_t s = mcs_ble_uuid_short(&u);
        size_t l = s ? 2 : 16;
        if (n + 2 + l > sizeof ad) break;
        ad[n++] = (uint8_t)(l + 1); ad[n++] = s ? 0x03 : 0x07;
        if (s) { ad[n++] = (uint8_t)s; ad[n++] = (uint8_t)(s >> 8); } else { memcpy(ad + n, u.b, 16); n += 16; }
    }
    int m = d.manufacturerDataLength();
    if (m > 0 && n + 2 + (size_t)m <= sizeof ad) {
        ad[n++] = (uint8_t)(m + 1); ad[n++] = 0xFF;
        d.manufacturerData(ad + n, m); n += (size_t)m;
    }
    mcs_ble_addr_t a; addr_parse(d.address(), &a);
    mcs_ble_on_report(&a, d.rssi(), d.connected() ? MCS_BLE_ADV_IND : MCS_BLE_ADV_IND, ad, n);
}
static void on_written(BLEDevice d, BLECharacteristic c) {
    int id = chr_id(c), s = slot_of(d);
    if (id >= 0) mcs_ble_attr_write((uint16_t)id, s + 1, 0, c.value(), (size_t)c.valueLength());
}
static void on_subscribed(BLEDevice d, BLECharacteristic c) {
    int id = chr_id(c);
    if (id < 0) return;
    int f = (g_chr_props[id] & MCS_BLE_PROP_NOTIFY) ? MCS_BLE_SUB_NOTIFY : MCS_BLE_SUB_INDICATE;
    mcs_ble_on_subscribe(slot_of(d) + 1, (uint16_t)id, f);
}
static void on_unsubscribed(BLEDevice d, BLECharacteristic c) {
    int id = chr_id(c);
    if (id >= 0) mcs_ble_on_subscribe(slot_of(d) + 1, (uint16_t)id, 0);
}

/* ---------------------------------------------------------------- controller */
static void apply_gap(void) {
    BLE.setDeviceName(g_name);
    BLE.setLocalName(g_name);
    if (g_opt[MCS_BLE_OPT_APPEARANCE]) BLE.setAppearance((uint16_t)g_opt[MCS_BLE_OPT_APPEARANCE]);
    BLE.setEventHandler(BLEConnected, on_connected);
    BLE.setEventHandler(BLEDisconnected, on_disconnected);
    BLE.setEventHandler(BLEDiscovered, on_discovered);
}
static int a_enable(void*, int on) {
    if (!on) {
        if (g_on) BLE.end();
        g_on = g_advertising = g_scanning = false;
        g_db_added = false;
        for (int i = 0; i < A_MAXC; i++) g_used[i] = false;
        return 0;
    }
    if (g_on) return 0;
    if (!BLE.begin()) return MCS_HAL_ENODEV;
    g_on = true;
    apply_gap();
    return 0;
}
static int a_address(void*, mcs_ble_addr_t* out) {
    if (!g_on) return MCS_HAL_EBUSY;
    return addr_parse(BLE.address(), out) ? 0 : MCS_HAL_ERR;
}
static int a_set_name(void*, const char* name) {
    strncpy(g_name, name, sizeof g_name - 1); g_name[sizeof g_name - 1] = 0;
    if (g_on) { BLE.setDeviceName(g_name); BLE.setLocalName(g_name); }
    return 0;
}
static int a_opt_get(void*, int opt, int32_t* v) {
    switch (opt) {
    case MCS_BLE_OPT_MTU: *v = 23; return 0;
    case MCS_BLE_OPT_MAX_CONN: *v = A_MAXC; return 0;
    case MCS_BLE_OPT_APPEARANCE: *v = g_opt[opt]; return 0;
    case MCS_BLE_OPT_ADDR_TYPE: *v = MCS_BLE_ADDR_PUBLIC; return 0;
    default: return MCS_HAL_ENOTSUP;
    }
}
static int a_opt_set(void*, int opt, int32_t v) {
    switch (opt) {
    case MCS_BLE_OPT_APPEARANCE: g_opt[opt] = v & 0xFFFF; if (g_on) BLE.setAppearance((uint16_t)g_opt[opt]); return 0;
    case MCS_BLE_OPT_MTU: return MCS_HAL_ENOTSUP;   /* ArduinoBLE negotiates on its own */
    default: return MCS_HAL_ENOTSUP;
    }
}
static void a_poll(void*) {
    if (!g_on) return;
    BLE.poll();
    uint32_t now = millis();
    if (g_advertising && g_adv_end && (int32_t)(now - g_adv_end) >= 0) {
        g_advertising = false; g_adv_end = 0; BLE.stopAdvertise();
        mcs_ble_on_event(MCS_BLE_EV_ADV_DONE, -1, 0);
    }
    if (g_scanning && g_scan_end && (int32_t)(now - g_scan_end) >= 0) {
        g_scanning = false; g_scan_end = 0; BLE.stopScan();
        mcs_ble_on_event(MCS_BLE_EV_SCAN_DONE, -1, 0);
    }
}

/* ---------------------------------------------------------------- advertising + scanning */
static BLEAdvertisingData g_ad, g_sr;
static int a_adv_start(void*, const mcs_ble_adv_t* a) {
    if (!g_on) return MCS_HAL_EBUSY;
    if (g_advertising) BLE.stopAdvertise();
    g_ad.setRawData(a->data, a->len);
    BLE.setAdvertisingData(g_ad);
    g_sr.setRawData(a->rsp, a->rsp_len);
    BLE.setScanResponseData(g_sr);
    BLE.setAdvertisingInterval((uint16_t)((a->interval_ms ? a->interval_ms : 100) * 8 / 5));
    BLE.setConnectable(a->connectable != 0);
    if (!BLE.advertise()) return MCS_HAL_ERR;
    g_advertising = true;
    g_adv_end = a->duration_ms ? millis() + a->duration_ms : 0;
    if (g_adv_end == 0 && a->duration_ms) g_adv_end = 1;
    return 0;
}
static int a_adv_stop(void*) { BLE.stopAdvertise(); g_advertising = false; g_adv_end = 0; return 0; }
static int a_scan_start(void*, const mcs_ble_scan_t* s) {
    if (!g_on) return MCS_HAL_EBUSY;
    if (g_scanning) BLE.stopScan();
    if (!BLE.scan(!s->filter_duplicates)) return MCS_HAL_ERR;   /* ArduinoBLE scans actively */
    g_scanning = true;
    g_scan_end = s->duration_ms ? millis() + s->duration_ms : 0;
    if (g_scan_end == 0 && s->duration_ms) g_scan_end = 1;
    return 0;
}
static int a_scan_stop(void*) { BLE.stopScan(); g_scanning = false; g_scan_end = 0; return 0; }

/* ---------------------------------------------------------------- GATT server */
static void db_free(void) {
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++) { delete g_chr[i]; g_chr[i] = NULL; delete g_dsc[i]; g_dsc[i] = NULL; g_skip[i] = 0; }
    for (int i = 0; i < g_nsvc; i++) { delete g_svc[i]; g_svc[i] = NULL; }
    g_nsvc = 0;
}
static int a_gatts_set(void*, mcs_ble_svc_def_t* svcs, int n) {
    if (!g_on) return MCS_HAL_EBUSY;
    if (n > MCS_BLE_MAX_SERVICES) return MCS_HAL_EINVAL;
    if (g_db_added) {                            /* ArduinoBLE cannot remove services: restart the stack */
        BLE.end();
        if (!BLE.begin()) { g_on = false; return MCS_HAL_ENODEV; }
        apply_gap();
        g_db_added = false;
    }
    db_free();
    char us[37];
    uint8_t tmp[MCS_BLE_MAX_VALUE];
    for (int s = 0; s < n; s++) {
        uuid_str(&svcs[s].uuid, us);
        g_svc[s] = new BLEService(us);
        g_nsvc = s + 1;
        for (int c = 0; c < svcs[s].nchr; c++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            if (cd->id >= MCS_BLE_MAX_ATTRS) return MCS_HAL_EINVAL;
            uuid_str(&cd->uuid, us);
            uint16_t props = (uint16_t)(cd->props & 0x3F);
            BLECharacteristic* ch = new BLECharacteristic(us, props, cd->max_len ? cd->max_len : 20, false);
            g_chr[cd->id] = ch; g_chr_props[cd->id] = props;
            int k = mcs_ble_attr_read(cd->id, -1, 0, tmp, sizeof tmp, NULL);
            if (k > 0) ch->writeValue(tmp, k);
            ch->setEventHandler(BLEWritten, on_written);
            ch->setEventHandler(BLESubscribed, on_subscribed);
            ch->setEventHandler(BLEUnsubscribed, on_unsubscribed);
            for (int d = 0; d < cd->ndsc; d++) {
                mcs_ble_dsc_def_t* dd = &cd->dsc[d];
                if (mcs_ble_uuid_short(&dd->uuid) == 0x2902 || dd->id >= MCS_BLE_MAX_ATTRS) continue;   /* ArduinoBLE adds the CCCD */
                uuid_str(&dd->uuid, us);
                k = mcs_ble_attr_read(dd->id, -1, 0, tmp, sizeof tmp, NULL);
                g_dsc[dd->id] = new BLEDescriptor(us, tmp, k > 0 ? k : 0);
                ch->addDescriptor(*g_dsc[dd->id]);
            }
            g_svc[s]->addCharacteristic(*ch);
        }
        BLE.addService(*g_svc[s]);
    }
    g_db_added = n > 0;
    /* ArduinoBLE assigns handles inside: report sequential ones (the front end only compares them) */
    uint16_t h = 1;
    for (int s = 0; s < n; s++) {
        svcs[s].handle = h++;
        for (int c = 0; c < svcs[s].nchr; c++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            h++; cd->handle = h++;
            cd->cccd_handle = (cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) ? h++ : 0;
            for (int d = 0; d < cd->ndsc; d++)
                cd->dsc[d].handle = mcs_ble_uuid_short(&cd->dsc[d].uuid) == 0x2902 ? cd->cccd_handle : h++;
        }
    }
    return 0;
}
static bool subscribed(int id) {
    for (int i = 0; i < A_MAXC; i++) if (g_used[i]) return g_chr[id]->subscribed();
    return false;
}
static int a_gatts_value(void*, uint16_t id, const uint8_t* data, size_t n) {
    if (id >= MCS_BLE_MAX_ATTRS || !g_chr[id]) return 0;      /* descriptors are fixed in ArduinoBLE */
    g_skip[id] = subscribed(id) && (g_chr_props[id] & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE));
    g_chr[id]->writeValue(data, (int)n);                     /* notifies subscribers itself */
    return 0;
}
static int a_gatts_notify(void*, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate) {
    (void)conn; (void)indicate;                               /* ArduinoBLE picks notify / indicate from the CCCD */
    if (chr->id >= MCS_BLE_MAX_ATTRS || !g_chr[chr->id]) return MCS_HAL_EINVAL;
    if (g_skip[chr->id]) { g_skip[chr->id] = 0; return 0; }  /* sent when the value was set */
    return g_chr[chr->id]->writeValue(data, (int)n) ? 0 : MCS_HAL_EBUSY;
}

/* ---------------------------------------------------------------- connections */
static int a_disconnect(void*, int conn) {
    if (conn < 1 || conn > A_MAXC || !g_used[conn - 1]) return MCS_HAL_ENODEV;
    return g_dev[conn - 1].disconnect() ? 0 : MCS_HAL_ERR;
}
static int a_conn_info(void*, int conn, mcs_ble_conn_info_t* o) {
    if (conn < 1 || conn > A_MAXC || !g_used[conn - 1]) return MCS_HAL_ENODEV;
    memset(o, 0, sizeof *o);
    addr_parse(g_dev[conn - 1].address(), &o->peer);
    o->role = MCS_BLE_ROLE_PERIPHERAL;
    o->mtu = 23;
    return 0;
}
static int a_conn_rssi(void*, int conn) {
    if (conn < 1 || conn > A_MAXC || !g_used[conn - 1]) return MCS_HAL_ENODEV;
    int r = g_dev[conn - 1].rssi();
    return r < 0 ? r : MCS_HAL_ERR;
}

static mcs_ble_t g_ble;
const mcs_ble_t* mcs_arduino_ble(void) {
    if (g_ble.name) return &g_ble;
    g_ble.name = "arduinoble";
    g_ble.readable_options = MCS_BLE_OPTION(MCS_BLE_OPT_MTU) | MCS_BLE_OPTION(MCS_BLE_OPT_MAX_CONN) | MCS_BLE_OPTION(MCS_BLE_OPT_APPEARANCE) | MCS_BLE_OPTION(MCS_BLE_OPT_ADDR_TYPE);
    g_ble.writable_options = MCS_BLE_OPTION(MCS_BLE_OPT_APPEARANCE);
    g_ble.enable = a_enable; g_ble.address = a_address; g_ble.set_name = a_set_name;
    g_ble.opt_get = a_opt_get; g_ble.opt_set = a_opt_set; g_ble.poll = a_poll;
    g_ble.adv_start = a_adv_start; g_ble.adv_stop = a_adv_stop;
    g_ble.scan_start = a_scan_start; g_ble.scan_stop = a_scan_stop;
    g_ble.gatts_set = a_gatts_set; g_ble.gatts_notify = a_gatts_notify; g_ble.gatts_value = a_gatts_value;
    g_ble.disconnect = a_disconnect; g_ble.conn_info = a_conn_info; g_ble.conn_rssi = a_conn_rssi;
    return &g_ble;
}
void mcs_arduino_ble_poll(void) { a_poll(NULL); }
#endif
