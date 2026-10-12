/*
 * BTstack configuration for MicroCS on the Pico W / Pico 2 W (-DMICROCS_BLE=ON; docs/BLE.md).
 * Your own: -DMICROCS_BTSTACK_CONFIG_DIR=/folder/with/btstack_config.h (keep the LE roles,
 * MAX_ATT_DB_SIZE and the CYW43 flow-control settings below).
 */
#ifndef MICROCS_BTSTACK_CONFIG_H
#define MICROCS_BTSTACK_CONFIG_H

/* LE roles: Ble.Advertise + GATT server, Ble.Scan + Ble.Connect + GATT client */
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#define ENABLE_SOFTWARE_AES128
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP          /* pico-sdk always compiles hci_dump_embedded_stdout.c */

/* connections: keep in step with MCS_BLE_MAX_CONN (the front end) */
#ifndef MCS_RP2_BLE_MAX_CONN
#define MCS_RP2_BLE_MAX_CONN 2
#endif
#define MAX_NR_HCI_CONNECTIONS MCS_RP2_BLE_MAX_CONN
#define MAX_NR_GATT_CLIENTS MCS_RP2_BLE_MAX_CONN
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

/* buffers: 251-byte LE data packets */
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
/* fewer controller buffers + controller-to-host flow control: no cyw43 shared-bus overrun */
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

/* bonds: TLV in the flash bank (pico_btstack_flash_bank, the last 8 KB of flash) */
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

/* the local GATT database (att_db_util, rebuilt by Ble.AddService): no malloc */
#ifndef MAX_ATT_DB_SIZE
#define MAX_ATT_DB_SIZE 2048
#endif

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
#endif
