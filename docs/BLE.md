# Bluetooth Low Energy (`Ble`, `BleService`, `BleConnection` …)

C# scripts can use the Bluetooth LE radio of the board in every role: **advertiser** (beacons),
**observer** (scanning), **peripheral / GATT server** (your own services, notifications) and
**central / GATT client** (connect to sensors, read, write, subscribe), plus pairing and
bonding, and a ready-made **Nordic UART** serial port.

> [!IMPORTANT]
> BLE is **opt-in**: off by default and left out of every release firmware (the stacks cost
> 100–250 KB of flash and 30–60 KB of RAM). Turn it on for your build as shown in
> [Turn it on](#turn-it-on).

```csharp
// peripheral: a battery service phones understand
var battery = new BleService("180F");
var level = battery.AddCharacteristic("2A19", BleProperty.Read | BleProperty.Notify, new byte[] { 90 });
Ble.AddService(battery);
Ble.StartAdvertising();                 // name + service UUIDs; again after every disconnect
level.Notify(new byte[] { 89 });

// observer
foreach (var d in Ble.Scan(3000)) Console.WriteLine($"{d.Address} {d.Name} {d.Rssi} dBm");

// central
var conn = Ble.Connect("C0:FF:EE:00:00:01");
Console.WriteLine(conn.Characteristic("180F", "2A19").Read()[0] + " %");
conn.Characteristic("180D", "2A37").Subscribe(data => Console.WriteLine("bpm " + data[1]));
```

**Contents:** [Boards](#boards) · [Try it on the PC](#try-it-on-the-pc) · [Turn it on](#turn-it-on) ·
[C# API](#c-api) · [Events and threads](#events-and-threads) · [Options](#options) ·
[Errors](#errors) · [C API (ports)](#c-api-for-ports)

## Boards

| Port | Stack | Radio | Roles | Security / bonds | Enable |
|---|---|---|---|---|---|
| ESP-IDF | NimBLE | ESP32, ESP32-S3, -C2, -C3, -C5, -C6, -C61, -H2 | all | yes (bonds in NVS) | `sdkconfig.ble` |
| Raspberry Pi Pico W / Pico 2 W | BTstack | CYW43439 | all | yes (bonds in flash) | `-DMICROCS_BLE=ON` |
| Zephyr | Zephyr Bluetooth host | nRF52 / nRF53 / nRF54, any `CONFIG_BT` board | all | yes (bonds with `CONFIG_BT_SETTINGS`) | `overlay-ble.conf` |
| Arduino | ArduinoBLE | Nano 33 BLE / IoT, Nano RP2040 Connect, UNO R4 WiFi, Portenta, Nano ESP32 … | advertiser, observer, peripheral | no | `make_arduino.py --ble` |
| Host CLI | simulated radio | — | all (with simulated peers) | yes | `make mcs-ble`, `--sim` |

`Ble.Has(feature)` tells at run time what the board can do: `"Ble"`, `"Ble.Advertise"`,
`"Ble.Scan"`, `"Ble.Server"`, `"BleUart"`, `"Ble.Central"` (also `"Ble.Client"`),
`"Ble.Security"`, `"Ble.Bonds"`. A call the board lacks throws `NotSupportedException`.
`Ble.Backend` names the stack (`"nimble"`, `"btstack"`, `"zephyr"`, `"arduinoble"`, `"sim"`).

## Try it on the PC

```sh
make mcs-ble                                   # host CLI with the simulated radio
./build/mcs_ble --sim examples/ble/heart_rate.cs
./build/mcs_ble --sim examples/ble/central_client.cs
make ble-test                                  # tests/ble/*.cs in several configurations
```

(CMake: `cmake -S . -B build -DMICROCS_BLE=ON`, then `build/mcs --sim`.) The simulated radio has
a few advertisers (a heart-rate sensor, an iBeacon, a UART device …) that scripts can scan for,
connect to, read, write, subscribe to and pair with; a local GATT server sees a simulated phone
connect, subscribe and write. Without `--sim` the `Ble` classes are not registered.

| Simulated peer | |
|---|---|
| `C0:FF:EE:00:00:01` "MicroCS-HRM" | heart rate `180D` (`2A37` notifies every second), battery `180F`, device information `180A` |
| `C0:FF:EE:00:00:02` "UART-Echo" | Nordic UART: every write to RX comes back on TX |
| `12:34:56:78:9A:BC` | iBeacon `E2C56DB5-DFFB-48D2-B060-D0F5A71096E0`, major 1, minor 42 |
| `DE:AD:BE:EF:00:04` | Eddystone-URL `https://github.com` |
| `C0:FF:EE:00:00:05` "Thermo" | environmental sensing `181A`: temperature `2A6E` (read, indicates every 2 s), humidity `2A6F` (needs `Pair()` first) |
| "Sim Phone" | connects to the board's connectable advertising, raises the MTU to 185, subscribes to every notify / indicate characteristic, writes `"hello"` to the first writable one, answers `BleUart` lines in upper case |

## Turn it on

### ESP32 (ESP-IDF, NimBLE)

`ports/esp32/example/sdkconfig.ble` turns on NimBLE and `CONFIG_MICROCS_BLE`:

```sh
cd ports/esp32/example
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ble" set-target esp32s3 build flash monitor
```

For an already configured project, prefer menuconfig rather than deleting your settings:
*Component config → Bluetooth* → enable Bluetooth, choose the NimBLE host and enable the
controller; keep extended advertising disabled. Then enable the C# API under
*Component config → MicroCS → Bluetooth LE*. That menu remains visible on BLE-capable chips
and displays prerequisite hints. It also exposes the device name, queue / GATT / scan /
discovery limits, subscriptions, timeouts and BleUart buffer. Connections and MTU are set in
*NimBLE Options*; the C# connection table follows NimBLE (the C2 allows at most two).
The supplied overlay uses two connections and enables bond persistence. ESP32 / S3 / C2 /
C3 / C6 / H2 have BLE build coverage on IDF 5.3.2; C5 / C61 use IDF 5.5.2. S2 and P4 have
no onboard BLE radio. H2 has no Wi-Fi. Real-board operation still requires hardware testing.

 `CONFIG_MICROCS_BLE_RESERVE_KB`
(default 48) is kept free for the controller and host when the C# heap is sized. Wi-Fi and BLE run
together (the radio is time-shared). The original ESP32 is tight on IRAM with Wi-Fi + BLE: the
file moves the Wi-Fi / lwIP fast paths to flash and builds with `-Os`.

### Raspberry Pi Pico W / Pico 2 W (BTstack)

```sh
cmake -S ports/rp2/example -B build -DPICO_BOARD=pico_w -DMICROCS_BLE=ON \
      -DMICROCS_FS=littlefs -DMICROCS_LITTLEFS_DIR=$HOME/littlefs
cmake --build build                            # pico-sdk submodules: lib/cyw43-driver lib/btstack
```

BLE alone works without an OS; with Wi-Fi add `-DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=…`
(`MICROCS_WIFI` follows the board). `ports/rp2/ble/btstack_config.h` is the BTstack
configuration (2 connections, 247-byte MTU, LE Secure Connections); point
`-DMICROCS_BTSTACK_CONFIG_DIR=dir` at your own copy to change it. Bonds live in the last 8 KB of
flash, the filesystem ends below them. Changing `Wifi.Country` after `Ble.Enable()` throws (the
CYW43 is shared).

### Zephyr (nRF52840 DK and other `CONFIG_BT` boards)

```sh
west build -b nrf52840dk/nrf52840 ports/zephyr/example -- -DEXTRA_CONF_FILE=overlay-ble.conf
```

`ports/zephyr/example/overlay-ble.conf` turns on the host (peripheral + central, GATT client,
dynamic GATT database, SMP) and `CONFIG_MICROCS_BLE`. The west manifest fetches `mbedtls`, which
`CONFIG_BT_SMP` / LE Secure Connections need. Bonds across resets: add `CONFIG_BT_SETTINGS=y`,
`CONFIG_SETTINGS=y` and a settings backend (`CONFIG_NVS` or `CONFIG_ZMS` on a storage partition).

### Arduino (ArduinoBLE)

```sh
python3 tools/make_arduino.py --ble            # library depends on ArduinoBLE
arduino-cli lib install ArduinoBLE
```

`mcs_arduino_hal_init` registers the backend and services `BLE.poll()` from the HAL event poll,
so callbacks run while the script waits, sleeps or calls `Ble.Poll()`. ArduinoBLE offers no
central GATT client or security API, so `Ble.Connect` and `Pair` are not supported;
characteristics have a fixed maximum length (`AddCharacteristic(…, maxLength)`), and as
ArduinoBLE cannot remove services, changing them restarts the stack (connections drop).

### Your own port

Fill a `mcs_ble_t` table (below) and call `mcs_ble_set(&table)` before the VM opens the HAL
library. Build with `MCS_ENABLE_BLE=1` in your `mcs_user_config.h`.

## C# API

The full member list is generated in [STDLIB.md](STDLIB.md#bluetooth-modulesble). UUIDs are
16-bit numbers or strings (`0x180D`, `"180D"`, `"2A37"`) or full 128-bit strings
(`"6E400001-B5A3-F393-E0A9-E50E24DCCA9E"`). Addresses print as `"C0:11:22:33:44:55"`. Values
are `byte[]` or `string` (UTF-8).

### Radio

| Member | |
|---|---|
| `Ble.Enable()` / `Disable()` / `Enabled` | the stack starts by itself on first use |
| `Ble.Name` (≤ 29 chars), `Appearance`, `TxPower` (dBm), `Mtu` (preferred), `Phy` (`Ble.Phy1M`, `Phy2M`, `PhyCoded`) | GAP settings |
| `Ble.Address`, `AddressType`, `Backend`, `MaxConnections`, `Has(feature)` | |
| `Ble.Timeout` | ms for GATT client calls (default `MCS_BLE_TIMEOUT`) |
| `Ble.Poll()` | run pending callbacks now (they also run while the script sleeps or waits) |
| `Ble.DroppedEvents` | events lost because the queue (`MCS_BLE_QUEUE`) was full |

### Advertising

```csharp
Ble.StartAdvertising();                         // name + service UUIDs of Ble.AddService
Ble.StartAdvertising("Sensor-1");               // another name
var adv = new BleAdvertisement("Thermo");
adv.Interval = 200; adv.Connectable = false;
adv.SetManufacturerData(0xFFFF, new byte[] { 1, 2, 3 });
adv.AddServiceData("181A", new byte[] { 0x10, 0x09 });
Ble.StartAdvertising(adv);
Ble.StartAdvertising(BleAdvertisement.IBeacon("FDA50693-A4E2-4FB1-AFCF-C6EB07647825", 1, 42));
Ble.StartAdvertising(BleAdvertisement.EddystoneUrl("https://github.com"));
```

`BleAdvertisement`: `Name`, `Connectable`, `Interval` (ms), `Duration` (ms, 0 = forever),
`IncludeTxPower`, `Appearance`, `Flags`, `AddServiceUuid`, `SetManufacturerData(companyId, data)`,
`AddServiceData(uuid, data)`, `Raw` / `ScanResponse` (your own AD bytes), `Build()` /
`BuildScanResponse()` (the bytes that go on air; throws when they exceed 31 bytes).
Statics: `IBeacon(uuid, major, minor[, measuredPower])`, `EddystoneUrl(url[, txPower])`,
`EddystoneUid(namespaceHex, instanceHex[, txPower])`. `Ble.AutoAdvertise` (default `true`)
restarts advertising after a peripheral connection ends.

### Scanning

```csharp
foreach (var d in Ble.Scan(3000))               // ms; Scan(ms, active = true)
    if (d.IsIBeacon) Console.WriteLine($"{d.BeaconUuid} {d.BeaconMajor}/{d.BeaconMinor} {d.Rssi}");
Ble.StartScan(d => Console.WriteLine(d), true, false);  // callback, active, report duplicates
Ble.StopScan();
```

`BleDevice`: `Address`, `AddressType`, `Name`, `Rssi`, `Connectable`, `TxPower`, `Appearance`,
`Flags`, `ServiceUuids`, `HasService(uuid)`, `ManufacturerData`, `CompanyId`,
`ServiceData(uuid)`, `Data` / `ScanResponse` (raw), iBeacon (`IsIBeacon`, `BeaconUuid`,
`BeaconMajor`, `BeaconMinor`, `BeaconPower`), `EddystoneUrl`, `Connect([timeoutMs])`.
`Ble.Scan` merges the scan response into the device and returns at most `MCS_BLE_SCAN_MAX`.

### GATT server (peripheral)

```csharp
var svc = new BleService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
var rx = svc.AddCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
                               BleProperty.Write | BleProperty.WriteWithoutResponse, null, 128);
var tx = svc.AddCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BleProperty.Notify);
var cfg = svc.AddCharacteristic("FF01", BleProperty.Read | BleProperty.Write | BleProperty.Encrypted, "auto");
cfg.Description = "Mode";                       // 0x2901 user description
rx.OnWrite((data, conn) => tx.Notify(data));    // echo back to subscribers
tx.OnSubscribe((conn, flags) => Console.WriteLine(flags));   // 0 off, 1 notify, 2 indicate
Ble.AddService(svc);
Ble.StartAdvertising();
```

`new BleService(uuid[, primary])`, `AddCharacteristic(uuid, properties[, value[, maxLength]])`.
`BleProperty`: `Read`, `Write`, `WriteWithoutResponse`, `Notify`, `Indicate`, `Broadcast`,
`AuthenticatedSignedWrites`, `ExtendedProperties` and the permissions `Encrypted`
(needs an encrypted link) and `Authenticated` (needs MITM-protected pairing).

`BleCharacteristic`: `Value` / `SetValue` (what a read returns; up to `MaxLength`),
`StringValue`, `Notify([data[, conn]])` / `Indicate(…)` (all subscribers, or one connection;
without data: the current value), `Subscribers`, `OnWrite((data, conn) => …)`,
`OnSubscribe((conn, flags) => …)`, `AddDescriptor(uuid, value[, writable])`, `Description`,
`Uuid`, `Handle`, `Properties`. The client characteristic configuration descriptor (0x2902)
is added by the stack. Added services reach the stack's database when advertising starts.
Change the services (`AddService`, `ClearServices`) while not advertising and not connected —
otherwise `InvalidOperationException`.

### Connections

`Ble.OnConnect(conn => …)`, `Ble.OnDisconnect((conn, reason) => …)`, `Ble.Connections`,
`Ble.Connected`. `BleConnection`: `Address`, `AddressType`, `Role` (`"central"` /
`"peripheral"`: the role of *this* board), `Connected`, `Mtu`, `Rssi`, `Encrypted`,
`Authenticated`, `Bonded`, `Interval` (1.25 ms units), `Latency`, `SupervisionTimeout` (10 ms
units), `Disconnect()`, `RequestMtu()`, `UpdateParameters(minInterval, maxInterval, latency,
timeout)` (same units: `UpdateParameters(12, 24, 0, 400)` = 15–30 ms, 4 s), `Pair()`.

### GATT client (central)

```csharp
var conn = Ble.Connect("C0:FF:EE:00:00:01", 5000);     // or BleDevice.Connect()
foreach (var s in conn.Services())
    foreach (var c in s.Characteristics())
        Console.WriteLine($"{s.Name ?? s.Uuid} / {c.Name ?? c.Uuid} {(c.CanRead ? "R" : "")}{(c.CanWrite ? "W" : "")}");
var name = conn.Characteristic("1800", "2A00").ReadString();
var ctl = conn.Characteristic("FFE0", "FFE1");
ctl.Write(new byte[] { 1 });                             // with response; Write(data, false) without
ctl.Subscribe((data, c) => Console.WriteLine(BitConverter.ToString(data)));
```

`conn.Services()`, `Service(uuid)`, `Characteristic(serviceUuid, charUuid)` (null when missing).
`BleRemoteService`: `Uuid`, `Name` (Bluetooth SIG name or null), `Handle`, `EndHandle`,
`Characteristics()`, `Characteristic(uuid)`. `BleRemoteCharacteristic`: `Uuid`, `Name`,
`Handle`, `Properties`, `CanRead` / `CanWrite` / `CanNotify` / `CanIndicate`, `Read()` (long
reads past the MTU), `ReadString()`, `Write(data[, withResponse])` (long writes),
`Subscribe(callback[, indicate])`, `Unsubscribe()`, `Descriptors()`. `BleRemoteDescriptor`:
`Read()`, `Write(data)`. Client calls block the script (other callbacks keep running) until the
peer answers or `Ble.Timeout` passes (`TimeoutException`).

### Security and bonding

```csharp
Ble.IoCapability = Ble.IoDisplayOnly;           // IoNone (default: Just Works), IoDisplayYesNo, IoKeyboardOnly, IoKeyboardDisplay
Ble.Bonding = true; Ble.Mitm = true; Ble.SecureConnections = true;
Ble.Passkey = 123456;                           // fixed key shown / entered (-1 = random)
Ble.OnPasskey((conn, kind, key) => {            // kind: Ble.PasskeyDisplay, PasskeyEntry, PasskeyCompare, PasskeyConfirm
    if (kind == Ble.PasskeyDisplay) Console.WriteLine("enter " + key);
    return kind == Ble.PasskeyEntry ? 123456 : 1;   // the key to enter, or 1 / 0 = accept / reject
});
conn.Pair();                                    // or let the peer start it
foreach (var a in Ble.Bonds) Console.WriteLine(a);
Ble.RemoveBond("C0:FF:EE:00:00:01"); Ble.ClearBonds();
```

### Nordic UART (`BleUart`)

```csharp
var uart = new BleUart("MicroCS-UART");         // NUS service 6E400001-…
uart.OnReceive(text => uart.WriteLine("echo: " + text.Trim()));   // or poll: Available / Read / ReadString / ReadLine
uart.Start();                                   // adds the service and advertises
```

Works with "Serial Bluetooth Terminal", nRF Toolbox UART, Adafruit Bluefruit Connect and
similar apps. `Write` splits long data into MTU-sized notifications; received bytes wait in an
`MCS_BLE_UART_RX`-byte buffer.

### Events and helpers

`Ble.OnEvent((ev, conn, value) => …)` sees every event: `Ble.EventConnect`, `EventDisconnect`,
`EventConnectFailed`, `EventAdvertisingDone`, `EventScanDone`, `EventMtu`, `EventSubscribe`,
`EventEncrypted`, `EventPairingDone`, `EventConnectionUpdate`, `EventIndicateDone`, `EventPhy`;
`Ble.EventName(ev)` gives the name. `BleUuid`: `Normalize`, `ToLong` (128-bit form),
`Equals`, `IsValid`, `Name` (Bluetooth SIG name of well-known services / characteristics).

## Events and threads

The Bluetooth stack runs in its own task (NimBLE host task, BTstack run loop, Zephyr's BT RX
thread) or, on Arduino, inside `BLE.poll()`. It never calls into the VM: events, scan reports,
writes and notifications are copied into one queue (`MCS_BLE_QUEUE` bytes) and the VM runs the
C# callbacks on its own thread — while the script sleeps (`Thread.Sleep`), waits in a blocking
BLE call, or calls `Ble.Poll()` — just like GPIO interrupt callbacks. Reads of local
characteristics are answered by the stack at once from the values the script set, so a phone
can read while the script is busy.

## Options

Set them in your `mcs_user_config.h` ([CONFIGURATION.md](CONFIGURATION.md)):

| Option | Default | |
|---|---|---|
| `MCS_ENABLE_BLE` | 0 | the C# BLE classes (needs `MCS_ENABLE_HAL`); the ports set it from their build switch |
| `MCS_BLE_SIM` | host builds | simulated radio for `mcs --sim` |
| `MCS_BLE_NAME` | `"MicroCS"` | default `Ble.Name` |
| `MCS_BLE_QUEUE` | 2048 | bytes of events / reports / data waiting for the script |
| `MCS_BLE_MAX_CONN` | 4 | connections tracked (the stack may allow fewer) |
| `MCS_BLE_MAX_SERVICES` | 8 | local services |
| `MCS_BLE_MAX_ATTRS` | 32 | local characteristics + descriptors |
| `MCS_BLE_VALUE_POOL` | 2048 | bytes for local attribute values |
| `MCS_BLE_MAX_VALUE` | 512 | largest attribute value |
| `MCS_BLE_SCAN_MAX` | 32 | devices `Ble.Scan` returns |
| `MCS_BLE_MAX_DISC` | 32 | services / characteristics / descriptors one discovery returns |
| `MCS_BLE_MAX_SUBS` | 8 | remote characteristics subscribed to at once |
| `MCS_BLE_TIMEOUT` | 5000 | default `Ble.Timeout` (ms) |
| `MCS_BLE_CONNECT_TIMEOUT` | 10000 | default `Ble.Connect` timeout (ms) |
| `MCS_BLE_UART_RX` | 512 | `BleUart` receive buffer |

Port switches: ESP-IDF / Zephyr `CONFIG_MICROCS_BLE` (+ `CONFIG_MICROCS_BLE_RESERVE_KB` on
ESP-IDF), CMake `MICROCS_BLE` (Pico W, host), `tools/make_arduino.py --ble`. The C# front end
adds about 50 KB of flash (Cortex-M4, `-Os`, including the Bluetooth SIG name table) and about
10 KB of RAM with the defaults above, on top of the stack itself.

## Errors

| Exception | When |
|---|---|
| `NotSupportedException` | the board / backend lacks the feature (`Ble.Has`) |
| `TimeoutException` | connect, discovery, read, write or MTU exchange took longer than the timeout |
| `InvalidOperationException` | not connected, services changed while advertising, too many services / attributes / subscriptions |
| `ArgumentException` | bad UUID, address, value too long for `MaxLength`, advertising data over 31 bytes |
| `UnauthorizedAccessException` | the peer refused: insufficient authentication / encryption / authorization (ATT error) |
| `ArgumentOutOfRangeException` | a setting out of range (`Ble.Passkey`, …) |
| `IOException` | any other stack or ATT error (the message has the code) |

## C API (for ports)

`include/mcs_ble.h` documents the table. A port fills what its stack supports and leaves the
rest `NULL`:

| Group | Members |
|---|---|
| radio | `enable`, `address`, `set_name`, `opt_get` / `opt_set` (TX power, MTU, appearance, IO capability, bonding, MITM, SC, PHY, passkey), `poll`, `lock` / `unlock` |
| advertiser / observer | `adv_start(mcs_ble_adv_t*)` (raw AD + scan response bytes built by the front end), `adv_stop`, `scan_start(mcs_ble_scan_t*)`, `scan_stop` |
| GATT server | `gatts_set(svcs, n)` (register the database, fill the handles), `gatts_notify`, `gatts_value` (optional: stacks that keep their own copy of values) |
| connections | `disconnect`, `conn_info`, `conn_rssi`, `conn_update` |
| GATT client | `connect`, `connect_cancel`, `discover`, `read`, `write`, `subscribe`, `exchange_mtu` |
| security | `pair`, `passkey_reply`, `bonds` |

The stack reports with `mcs_ble_on_connect`, `mcs_ble_on_disconnect`, `mcs_ble_on_report`,
`mcs_ble_on_subscribe`, `mcs_ble_on_notify`, `mcs_ble_on_passkey` and `mcs_ble_on_event`
(completions, MTU, encryption …) from any task; peers' reads and writes of the local database
go to `mcs_ble_attr_read` / `mcs_ble_attr_write`.
`modules/ble/mcs_ble_sim.c` is a complete reference backend; the four hardware backends are
`ports/esp32/mcs_port_esp32_ble.c`, `ports/rp2/mcs_port_rp2_ble.c`,
`ports/zephyr/mcs_port_zephyr_ble.c` and `ports/arduino/mcs_arduino_ble.cpp`.
