/*
 * MicroCS - Bluetooth Low Energy (optional module, MCS_ENABLE_BLE, docs/BLE.md).
 *
 * C# classes:
 *   Ble                 radio on / off, name, address, TX power, scanning, advertising,
 *                       GATT server (AddService), connections, security / bonding, events
 *   BleAdvertisement    advertising data builder (name, UUIDs, manufacturer / service data,
 *                       iBeacon, Eddystone) or raw bytes
 *   BleDevice           an advertiser found by Ble.Scan / Ble.StartScan (parsed AD fields)
 *   BleService  BleCharacteristic  BleDescriptor        local GATT database (peripheral)
 *   BleConnection       a link in either role: MTU, RSSI, pairing, GATT client
 *   BleRemoteService  BleRemoteCharacteristic  BleRemoteDescriptor   (central / GATT client)
 *   BleUart             Nordic UART Service, peripheral side (a serial port over BLE)
 *   BleUuid             UUID helpers
 *
 * Like Wi-Fi (mcs_net.h), the board describes its Bluetooth controller with one table of
 * C functions (mcs_ble_t); NULL entries are features the port lacks (Ble.Has(...) is false
 * and the C# call throws NotSupportedException). Backends in this tree:
 *   ports/esp32   NimBLE (ESP-IDF; CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED, CONFIG_MICROCS_BLE)
 *   ports/rp2     BTstack on the CYW43439 (Pico W / Pico 2 W, -DMICROCS_BLE=ON)
 *   ports/zephyr  Zephyr Bluetooth host (nRF52840 ..., CONFIG_MICROCS_BLE)
 *   ports/arduino ArduinoBLE (Nano 33 BLE, UNO R4 WiFi, Nano ESP32 ...; MICROCS_BLE)
 *   mcs_ble_sim.c a simulated radio for the host CLI (build/mcs_ble --sim) and the tests
 *
 *   mcs_ble_set(&my_ble);           // before the VM starts (the ports above do it)
 *   mcs_hal_open_lib(vm, &hal);     // opens the BLE classes too
 *   mcs_ble_open_lib(vm, &my_ble);  // ... or without a HAL
 *
 * Calls from the front end happen on the VM's thread. The stack's own task (NimBLE host,
 * BTstack run loop, Zephyr's BT RX thread ...) reports back through the mcs_ble_on_*()
 * functions below; they copy the data into a lock-protected queue and wake the VM with an
 * MCS_HAL_EV_BLE event, so the C# callbacks (OnConnect, OnWrite, Subscribe ...) run in
 * script context like GPIO interrupts. Reads of the local GATT database are answered by
 * mcs_ble_attr_read() at once from the values the script set.
 *
 * Every port function returns >= 0 on success or a negative MCS_HAL_E* code (mcs_hal.h);
 * GATT failures may be MCS_BLE_ATT_ERR(att_error_code).
 */
#ifndef MCS_BLE_H
#define MCS_BLE_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MCS_BLE_API_VERSION 1

#define MCS_HAL_EV_BLE 7           /* HAL event type of the BLE queue (source = MCS_BLE_EV_*) */

/* ---- addresses: a[0] is the most significant byte, as printed ("C0:11:22:33:44:55") */
enum { MCS_BLE_ADDR_PUBLIC = 0, MCS_BLE_ADDR_RANDOM = 1,      /* HCI address types */
       MCS_BLE_ADDR_PUBLIC_ID = 2, MCS_BLE_ADDR_RANDOM_ID = 3 };
typedef struct { uint8_t type; uint8_t a[6]; } mcs_ble_addr_t;

/* ---- UUIDs: len 2, 4 or 16 bytes; b[] little-endian (the order on air, as NimBLE / Zephyr) */
typedef struct { uint8_t len; uint8_t b[16]; } mcs_ble_uuid_t;

/* ---- GATT characteristic properties (the declaration byte) + MicroCS permission bits */
enum { MCS_BLE_PROP_BROADCAST = 0x01, MCS_BLE_PROP_READ = 0x02, MCS_BLE_PROP_WRITE_NR = 0x04,
       MCS_BLE_PROP_WRITE = 0x08, MCS_BLE_PROP_NOTIFY = 0x10, MCS_BLE_PROP_INDICATE = 0x20,
       MCS_BLE_PROP_SIGNED_WRITE = 0x40, MCS_BLE_PROP_EXTENDED = 0x80,
       MCS_BLE_PERM_ENCRYPTED = 0x100,      /* read / write need an encrypted link */
       MCS_BLE_PERM_AUTHENTICATED = 0x200,  /* ... paired with MITM protection (passkey / compare) */
       MCS_BLE_PERM_AUTHORIZED = 0x400 };   /* reserved */

/* ---- advertising PDU / report types */
enum { MCS_BLE_ADV_IND = 0,          /* connectable, scannable */
       MCS_BLE_ADV_DIRECT_IND = 1,   /* connectable, directed */
       MCS_BLE_ADV_SCAN_IND = 2,     /* scannable, not connectable */
       MCS_BLE_ADV_NONCONN_IND = 3,  /* neither (beacons) */
       MCS_BLE_ADV_SCAN_RSP = 4 };   /* report: the scan response to an earlier advertisement */

/* ---- events: queued by the mcs_ble_on_*() functions -> Ble.OnEvent((ev, conn, value) => ...) */
enum { MCS_BLE_EV_CONNECT = 1,       /* value = role (MCS_BLE_ROLE_*) */
       MCS_BLE_EV_DISCONNECT,        /* value = HCI reason (0x13 remote user, 0x08 timeout ...) */
       MCS_BLE_EV_CONNECT_FAILED,    /* value = HCI status / MCS_HAL_E* */
       MCS_BLE_EV_ADV_DONE,          /* advertising ended (duration over or a central connected) */
       MCS_BLE_EV_SCAN_DONE,
       MCS_BLE_EV_MTU,               /* value = new ATT MTU */
       MCS_BLE_EV_SUBSCRIBE,         /* value = id << 8 | MCS_BLE_SUB_* bits (local characteristic) */
       MCS_BLE_EV_WRITE,             /* (internal) a central wrote a local attribute */
       MCS_BLE_EV_NOTIFY,            /* (internal) notification / indication from a peripheral */
       MCS_BLE_EV_REPORT,            /* (internal) advertising report */
       MCS_BLE_EV_ENCRYPTED,         /* value = 1 encrypted, 2 + authenticated (MITM), 3 + bonded */
       MCS_BLE_EV_PAIRING_DONE,      /* value = 0 success, else the SMP / HCI error */
       MCS_BLE_EV_PASSKEY,           /* (internal) value = MCS_BLE_PASSKEY_* kind; data = 6-digit key */
       MCS_BLE_EV_CONN_UPDATE,       /* value = new interval in units of 1.25 ms */
       MCS_BLE_EV_INDICATE_DONE,     /* value = 0 confirmed by the client, else error */
       MCS_BLE_EV_PHY,               /* value = tx_phy << 4 | rx_phy */
       MCS_BLE_EV__COUNT };
enum { MCS_BLE_ROLE_CENTRAL = 0, MCS_BLE_ROLE_PERIPHERAL = 1 };   /* our role on the link */
enum { MCS_BLE_SUB_NOTIFY = 1, MCS_BLE_SUB_INDICATE = 2 };
enum { MCS_BLE_PASSKEY_DISPLAY = 1,  /* show `value` to the user */
       MCS_BLE_PASSKEY_ENTRY = 2,    /* the user types the key the peer shows: reply with it */
       MCS_BLE_PASSKEY_COMPARE = 3,  /* numeric comparison: reply 1 = the numbers match */
       MCS_BLE_PASSKEY_CONFIRM = 4 };/* "just works" confirmation: reply 1 = accept */

/* ---- integer options: opt_get / opt_set */
enum { MCS_BLE_OPT_TX_POWER = 1,     /* dBm (advertising / default) */
       MCS_BLE_OPT_MTU,              /* preferred ATT MTU (23..517) */
       MCS_BLE_OPT_APPEARANCE,       /* GAP appearance (0x00C0 watch, 0x0340 heart rate sensor ...) */
       MCS_BLE_OPT_IO_CAP,           /* MCS_BLE_IO_* */
       MCS_BLE_OPT_BONDING,          /* 0/1: store keys */
       MCS_BLE_OPT_MITM,             /* 0/1: require MITM protection when pairing */
       MCS_BLE_OPT_SC,               /* 0/1: LE Secure Connections */
       MCS_BLE_OPT_PASSKEY,          /* fixed 6-digit passkey shown when IO_CAP can display (-1 = random) */
       MCS_BLE_OPT_ADDR_TYPE,        /* own address: MCS_BLE_ADDR_PUBLIC or RANDOM (static random) */
       MCS_BLE_OPT_MAX_CONN,         /* read only: links the controller allows */
       MCS_BLE_OPT_PHY,              /* preferred PHY bits: 1 = 1M, 2 = 2M, 4 = Coded */
       MCS_BLE_OPT__COUNT };
enum { MCS_BLE_IO_DISPLAY_ONLY = 0, MCS_BLE_IO_DISPLAY_YESNO = 1, MCS_BLE_IO_KEYBOARD_ONLY = 2,
       MCS_BLE_IO_NONE = 3, MCS_BLE_IO_KEYBOARD_DISPLAY = 4 };    /* SMP values */

/* ---- ATT error as a port return value: MCS_BLE_ATT_ERR(0x05) = insufficient authentication */
#define MCS_BLE_ATT_ERR(code) (-(0x1000 + (int)(code)))
#define MCS_BLE_IS_ATT_ERR(rc) ((rc) <= -0x1000 && (rc) > -0x1100)
#define MCS_BLE_ATT_CODE(rc) (-(rc) - 0x1000)

typedef struct {
    uint8_t data[31], len;           /* advertising data (AD structures) */
    uint8_t rsp[31], rsp_len;        /* scan response (rsp_len 0 = none) */
    uint8_t connectable, scannable;
    uint16_t interval_ms;            /* 20 .. 10240 (0 = 100 ms) */
    uint32_t duration_ms;            /* 0 = until stopped / connected */
} mcs_ble_adv_t;

typedef struct {
    uint8_t active;                  /* send scan requests (scan responses carry names) */
    uint8_t filter_duplicates;       /* controller drops repeated reports */
    uint16_t interval_ms, window_ms; /* 0 = the port's defaults */
    uint32_t duration_ms;            /* 0 = until scan_stop */
} mcs_ble_scan_t;

/* ---- local GATT database handed to gatts_set (the port fills the handles) */
typedef struct {
    mcs_ble_uuid_t uuid;
    uint16_t flags;                  /* MCS_BLE_PERM_* + MCS_BLE_DSC_WRITE / _READ */
    uint16_t id;                     /* front-end id: mcs_ble_attr_read / _write */
    uint16_t handle;                 /* out */
} mcs_ble_dsc_def_t;
enum { MCS_BLE_DSC_READ = 0x01, MCS_BLE_DSC_WRITE = 0x02 };
typedef struct {
    mcs_ble_uuid_t uuid;
    uint16_t props;                  /* MCS_BLE_PROP_* | MCS_BLE_PERM_* */
    uint16_t id;
    uint16_t max_len;                /* largest value (stacks with fixed attribute sizes) */
    uint16_t handle;                 /* out: value handle */
    uint16_t cccd_handle;            /* out: client configuration descriptor (notify / indicate), 0 = none */
    uint8_t ndsc;
    mcs_ble_dsc_def_t* dsc;
} mcs_ble_chr_def_t;
typedef struct {
    mcs_ble_uuid_t uuid;
    uint8_t primary;
    uint8_t nchr;
    uint16_t handle;                 /* out: service declaration */
    mcs_ble_chr_def_t* chr;
} mcs_ble_svc_def_t;

/* ---- connection and GATT client data */
typedef struct {
    mcs_ble_addr_t peer;
    uint8_t role;                    /* MCS_BLE_ROLE_* */
    uint8_t encrypted, authenticated, bonded;
    uint16_t mtu;
    uint16_t interval;               /* 1.25 ms units */
    uint16_t latency, timeout;       /* events, 10 ms units */
} mcs_ble_conn_info_t;
enum { MCS_BLE_DISC_SERVICES = 1, MCS_BLE_DISC_CHARACTERISTICS = 2, MCS_BLE_DISC_DESCRIPTORS = 3 };
typedef struct {
    uint8_t kind;                    /* MCS_BLE_DISC_* */
    uint8_t props;                   /* characteristics: properties */
    mcs_ble_uuid_t uuid;
    uint16_t handle;                 /* service: start; characteristic: value handle; descriptor: handle */
    uint16_t end;                    /* service: end handle; characteristic: declaration handle */
} mcs_ble_attr_t;
enum { MCS_BLE_BONDS_COUNT = 0, MCS_BLE_BONDS_LIST = 1, MCS_BLE_BONDS_DELETE = 2, MCS_BLE_BONDS_CLEAR = 3 };

typedef struct mcs_ble {
    const char* name;                /* "nimble", "btstack", "zephyr", "arduinoble", "sim" (Ble.Backend) */
    void* ctx;                       /* passed to every function */

    /* ---------------- controller (enable == NULL: no BLE) */
    int (*enable)(void* ctx, int on);                    /* start / stop the stack; on: blocks until ready */
    int (*address)(void* ctx, mcs_ble_addr_t* out);
    int (*set_name)(void* ctx, const char* name);        /* GAP device name (also characteristic 0x2A00) */
    int (*opt_get)(void* ctx, int opt, int32_t* value);
    int (*opt_set)(void* ctx, int opt, int32_t value);
    /* optional: called from every front-end wait loop and Ble.Poll (stacks without a task,
     * e.g. ArduinoBLE's BLE.poll()) */
    void (*poll)(void* ctx);
    /* optional: guards the front end's queue against the stack's task / interrupt
     * (NULL = the stack calls mcs_ble_on_*() from the VM's thread only) */
    void (*lock)(void* ctx);
    void (*unlock)(void* ctx);

    /* ---------------- broadcaster / peripheral (adv_start == NULL: no advertising) */
    int (*adv_start)(void* ctx, const mcs_ble_adv_t* adv);
    int (*adv_stop)(void* ctx);

    /* ---------------- observer / central (scan_start == NULL: no scanning)
     * reports: mcs_ble_on_report(); the end of a timed scan: mcs_ble_on_event(SCAN_DONE) */
    int (*scan_start)(void* ctx, const mcs_ble_scan_t* scan);
    int (*scan_stop)(void* ctx);

    /* ---------------- GATT server (gatts_set == NULL: no local services)
     * Replace the whole local database (n = 0: none). Called with no connection and no
     * advertising running; the port fills the handles. Values live in the front end:
     * read requests -> mcs_ble_attr_read(), writes -> mcs_ble_attr_write(), CCCD writes ->
     * mcs_ble_on_subscribe(). */
    int (*gatts_set)(void* ctx, mcs_ble_svc_def_t* svcs, int n);
    /* send a notification (indicate 0) or an indication (1) of characteristic `chr` to
     * one subscribed connection; indications: confirmations -> mcs_ble_on_event(INDICATE_DONE) */
    int (*gatts_notify)(void* ctx, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate);

    /* ---------------- connections (both roles) */
    int (*disconnect)(void* ctx, int conn);
    int (*conn_info)(void* ctx, int conn, mcs_ble_conn_info_t* out);
    int (*conn_rssi)(void* ctx, int conn);               /* dBm (negative) or MCS_HAL_E* */
    /* request new parameters: intervals in 1.25 ms units, timeout in 10 ms units */
    int (*conn_update)(void* ctx, int conn, int min_interval, int max_interval, int latency, int timeout);

    /* ---------------- central + GATT client (connect == NULL: no central role)
     * Asynchronous: start the operation and return; complete it from the stack's task with
     * mcs_ble_op_attr() / mcs_ble_op_data() and finally mcs_ble_op_done(). One operation
     * runs at a time (the front end waits for it with Ble.Timeout). */
    int (*connect)(void* ctx, const mcs_ble_addr_t* peer, uint32_t timeout_ms);   /* done: value = conn handle */
    int (*connect_cancel)(void* ctx);
    int (*discover)(void* ctx, int conn, int kind, uint16_t start, uint16_t end); /* done: 0 */
    int (*read)(void* ctx, int conn, uint16_t handle);                            /* data + done */
    int (*write)(void* ctx, int conn, uint16_t handle, const uint8_t* data, size_t n, int with_response);
                                     /* with_response 0: may complete at once (done 0 before returning) */
    /* enable notifications / indications (flags MCS_BLE_SUB_*, 0 = off) on a remote characteristic:
     * write the CCCD and route its value updates to mcs_ble_on_notify(); done: 0 */
    int (*subscribe)(void* ctx, int conn, uint16_t value_handle, uint16_t cccd_handle, int flags);
    int (*exchange_mtu)(void* ctx, int conn);                                     /* done: value = MTU */

    /* ---------------- security (pair == NULL: no pairing)
     * results: mcs_ble_on_passkey(), mcs_ble_on_event(ENCRYPTED / PAIRING_DONE) */
    int (*pair)(void* ctx, int conn);
    int (*passkey_reply)(void* ctx, int conn, int kind, int32_t value);   /* ENTRY: key; COMPARE / CONFIRM: 0/1 */
    /* bonds: COUNT returns the number; LIST fills up to max addresses; DELETE addr; CLEAR all */
    int (*bonds)(void* ctx, int op, mcs_ble_addr_t* addrs, int max);

    /* ---------------- optional: stacks that keep the local values themselves (ArduinoBLE)
     * the script set the value of local attribute `id` (gatts_set ids); NULL = every read
     * comes through mcs_ble_attr_read() */
    int (*gatts_value)(void* ctx, uint16_t id, const uint8_t* data, size_t n);
} mcs_ble_t;

#if MCS_ENABLE_BLE
/* The board's Bluetooth (kept for every VM opened afterwards; NULL = none). */
void mcs_ble_set(const mcs_ble_t* ble);
const mcs_ble_t* mcs_ble_get(void);
/* Register the C# Bluetooth classes on vm (mcs_hal_open_lib calls it with mcs_ble_get()). */
void mcs_ble_open_lib(mcs_vm_t* vm, const mcs_ble_t* ble);
/* Ble.Has / Hal.Has: "Ble", "Ble.Advertise", "Ble.Scan", "Ble.Server", "Ble.Central", "Ble.Security" */
bool mcs_ble_has(const mcs_ble_t* ble, const char* feature);
/* Ports whose HAL events travel through their own queue install their poster
 * (default mcs_hal_post(MCS_HAL_EV_BLE, ev, 0)). */
void mcs_ble_set_poster(bool (*post)(int type, int source, int32_t value));

/* ---- called by the port, from any task (copies everything it is given) */
void mcs_ble_on_event(int ev, int conn, int32_t value);
void mcs_ble_on_connect(int conn, const mcs_ble_addr_t* peer, int role);
void mcs_ble_on_disconnect(int conn, int reason);
void mcs_ble_on_report(const mcs_ble_addr_t* addr, int rssi, int type, const uint8_t* data, size_t n);
void mcs_ble_on_subscribe(int conn, uint16_t id, int flags);              /* a client wrote our CCCD */
void mcs_ble_on_notify(int conn, uint16_t handle, const uint8_t* data, size_t n, int indication);
void mcs_ble_on_passkey(int conn, int kind, int32_t value);
/* local GATT database: copy (part of) the value of attribute `id` from `offset`; returns the
 * bytes copied or MCS_BLE_ATT_ERR(..); the full length goes to *total when not NULL */
int mcs_ble_attr_read(uint16_t id, int conn, size_t offset, uint8_t* buf, size_t cap, size_t* total);
/* a client wrote attribute `id` (offset > 0: long write part); 0 or MCS_BLE_ATT_ERR(..) */
int mcs_ble_attr_write(uint16_t id, int conn, size_t offset, const uint8_t* data, size_t n);
int mcs_ble_attr_len(uint16_t id);                                       /* current length, -1 = no such id */
/* GATT client operation results (the one operation the front end waits for) */
void mcs_ble_op_attr(const mcs_ble_attr_t* a);
void mcs_ble_op_data(const uint8_t* data, size_t n);
void mcs_ble_op_done(int status, int32_t value);
/* helpers for ports */
void mcs_ble_uuid16(mcs_ble_uuid_t* u, uint16_t v);
bool mcs_ble_uuid_eq(const mcs_ble_uuid_t* a, const mcs_ble_uuid_t* b);   /* 16-bit == its 128-bit form */
uint16_t mcs_ble_uuid_short(const mcs_ble_uuid_t* u);                    /* 16-bit value or 0 */
int mcs_ble_uuid_str(const mcs_ble_uuid_t* u, char out[37]);             /* "180D", "6E400001-B5A3-..." */
bool mcs_ble_uuid_parse(const char* s, mcs_ble_uuid_t* out);
const char* mcs_ble_event_name(int ev);                                  /* "Connect", "Mtu" ... */

#if MCS_BLE_SIM
/* Simulated radio for the host CLI (--sim), tests and Studio templates (docs/BLE.md#simulator):
 * five advertisers (a heart-rate sensor, a Nordic UART echo device, an iBeacon, an Eddystone-URL
 * beacon, a thermometer), GATT servers behind the connectable ones, and a phone ("Sim Phone")
 * that connects to the board's own advertising, subscribes to every notify / indicate
 * characteristic and writes "hello" to the first writable one. */
void mcs_ble_sim_init(mcs_ble_t* ble);
void mcs_ble_sim_tick(uint32_t now_ms);    /* periodic notifications; the CLI calls it from the HAL poll */
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif
