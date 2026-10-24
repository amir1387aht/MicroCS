# Changelog

All notable changes. Versions follow `MCS_VERSION_*` in `include/mcs.h`.

## Unreleased

* Refresh clean Studio device tabs after runs and explorer refreshes, including
  same-size changes and files outside the selected folder. Check the visible
  file on demand while idle in machine mode without overwriting unsaved edits,
  stealing focus, or interrupting foreground scripts and the REPL. Recurring
  file polling is opt-in and off by default to avoid ongoing device/serial work.
  Keep Save busy until its existing refresh completes, preventing late file
  replies from racing the next save/build snapshot without extra device reads.

* Extend the existing dependency-free JSON engine with `TryParse`, computed and
  inherited instance getters, mixed nested collections, explicit cycle detection
  and optional inclusion of underscore-prefixed members.
* Validate UTF-8 and surrogate pairs, preserve embedded NUL, reject malformed
  number syntax, and parse complete numeric tokens without truncation. Native
  numeric precision is unchanged; JSON numbers are not arbitrary precision.
* Add `Http.GetJson`, `PostJson`, `PutJson`, `PatchJson` and `HttpResponse.Json`,
  plus feature capabilities for older-firmware checks. JSON works offline on every
  port with List and Dictionary; HTTP still needs the existing socket backend.
* Release JSON scratch buffers on fatal OOM and fix partially committed dictionary
  growth that could corrupt allocation accounting after a failed resize. Store
  dictionary keys/values in one compact allocation, retain atomic OOM recovery
  without a staged buffer, and compact power-of-two index checks to preserve
  the unchanged 256 KiB Cortex-M0 flash limit.
* Add a JSON guide, Studio signatures and templates, strict malformed-input and
  allocation-failure tests, and 1,000 round trips in an unchanged 40 KiB test pool.
* Allow custom Studio UART baud rates with preset suggestions including 74880,
  19200 and 38400. Validate positive 32-bit integer rates before connecting, retain
  115200 as the default, and remember custom rates across reloads/reconnects.

* Fix compiler-free host builds: only compile the shell result-report helper
  when its source-compiler commands are enabled. Close the browser transport before
  terminating its runtime fixture to prevent teardown-only broken-pipe failures.
* Add a closeable Studio terminal with a toolbar/menu toggle, Alt+Tab when delivered
  by the OS, Ctrl+J fallback and Ctrl+` to reopen/focus. Preserve output, plotter
  selection and resized height; remember visibility without stopping the device.

* Automatically replace the previous application on runtime file/Project Run.
  Stop workers, jobs, GPIO/timer callbacks and encoders; release native objects,
  old globals and bytecode before compiling the replacement in the same pool.
  Preserve filesystem mounts/RAM files, uploaded scripts and console/baud.
* Studio Run requests replacement before build checks when firmware advertises
  Runtime.Replace. Save/Check stay non-destructive; REPL/selection and explicit
  `run <file> shared` retain shared state. Legacy firmware gets an explicit warning.
* Add unchanged-app 2,000-cycle real-U8g2 64 KiB rerun coverage without manual
  script cleanup, plus native cleanup, RAM-file, OOM/retry and browser regressions.

## 1.17.0 — Long-run upload memory recovery

* Separate short-lived compiler/VFS buffers from retained VM objects using
  opposite ends of the same fixed pool; preserve allocation accounting, OOM
  cleanup, live application state and GPIO/timer callbacks during compile checks.
* Collect unreachable prior-run objects before the first idle source-file read.
* Reduce 32-bit compiler tokens from 16 to 12 bytes while preserving numeric
  precision, source line/column diagnostics and string-prefix locations.
* Add an optional compiler-scratch allocator, synchronized runtime wrappers and
  per-worker high-end scratch allocation without a second heap or larger pool.
* Strengthen the larger-app regression to 2,000 content-edit/upload/check/run
  cycles with 7,500 extra bytes retained in the unchanged 64 KiB application pool.
  Local 10,000-cycle stress passes; the unchanged-main negative control fails on
  cycle 4 with a 512-byte OOM. The separate hard 40 KiB OLED gate remains intact.
* Expand mixed-allocator, exhaustive OOM-recovery and diagnostic coverage. No
  language features, fonts or default execution ceilings are removed. Host
  simulation and sanitizer results do not substitute for physical-board validation.

## 1.16.0 — Source discovery, bounded memory and Studio projects

### Source discovery hardening

* Discover providers at nested VFS mount roots even when no backing parent
  directory exists; deduplicate files already reached through the root walk.
* Replace recursive namespace-dependency visits with a growing, VFS-allocator-backed
  traversal stack. Long chains no longer consume native stack per provider; all
  scratch storage is released after success, compile errors, denied execution or OOM.
* List every RAMFS directory entry using bounded scratch pages while preserving
  creation order, rather than silently dropping entries after 64.
* Grow sorted POSIX directory listings beyond 256 entries; fail explicitly and
  clean up on allocation or enumeration errors rather than returning partial success.
* Add nested-mount, no-execute, large-directory, moving-allocation, deep-cycle and
  exhaustive scratch-allocation failure regressions to `make project-test`.

### Bounded memory and accurate telemetry

* Compact paged compiler tokens and AST storage; release consumed declarations,
  trim completed methods, and preserve configured execution limits while returning
  large idle stack/frame reservations during compilation.
* Compact internal symbol keys, best-fit fixed pools and optional cache allocation
  fallback. Runtime source/read buffers now count against the physical pool.
* Shell/Studio report tracked VM allocations, full pool capacity/usage/peak/free/
  largest block and optional ESP32 device heap separately, including after OOM.
* Add a 32-bit real-U8g2, fixed-40-KiB, 20-Project-cycle regression with
  profile-default stack/frame ceilings; no feature/profile/precision reduction.
* ESP32 example pool ceiling/stack/frame settings and fail-closed allocation;
  existing defaults remain unchanged. See `docs/MEMORY.md` for scope/exclusions.

### Studio project workflow

* **CI readiness** — wait for asynchronous storage/memory telemetry before asserting
  values, deliberately delay initial memory replies, and summarize failed assertions.

* **Studio formatting** — safe offline C# whitespace formatter, explicit document formatting,
  default-on line/paste formatting and format-on-save; preserve literal/comment contents.
* **Parameter info** — reliably dismiss on outside pointer clicks, Escape, close button,
  focus loss and leaving the call context.
* **Save/build** — default-on upload of all editable open C# tabs and compile-only checks
  before Save/F5/Run file; individual toggles, duplicate-path protection and action locking.
* **Multi-file source** — IntelliSense combines all open C# tabs. Firmware file execution resolves
  namespace providers anywhere in the mounted VFS referenced by `using`, including transitive dependencies and
  isolated job VMs, while suppressing dependency top-level statements and implicit `Main`.
  Single-capital completion now offers built-in classes such as `Path`.
* **Editor safety** — toolbar format-on-save replaces the document even while the toolbar has
  focus instead of inserting the formatted text at the previous editor caret.
* **Project mode** — remembered device folder, required `jobs.cfg`, recursive source checks,
  F5/global Run scheduling only configured jobs; separate Run file / Ctrl+Enter action.
* **Firmware** — guarded `mcs_check_source` / `mcs_check_file`, `check` shell command and
  validated `project` job replacement without reboot. Failed validation preserves the job
  table, not already-uploaded file contents. Updated firmware required for the new workflow.
* **Tests/docs** — formatter, compiler/project transaction and real-shell browser regressions;
  usage, compatibility and deployment caveats documented. No physical-board validation.

### Power management, memory recovery and documentation audit

* **Repeated-run memory recovery** — collect dead objects before an idle top-level
  source run starts compilation; preserve live globals and callbacks. Fixed-pool
  regressions run the Studio CPU benchmark 20 times without manual collection.
* **File memory safety** — stable reads no longer double their temporary buffer to
  detect EOF. Failed resizing never returns uninitialized capacity as file content.
  Native OOM cleanup releases whole-file read buffers, sequence-write handles and
  directory iterators; tests cover forced failures and 100 Studio upload/run cycles.
* **U8g2 naming** — add upstream-style `GetAscent()`, `GetDescent()`,
  `GetMaxCharHeight()` and `GetMaxCharWidth()`; retain the old property aliases and
  update the reference, Studio completion and OLED text template.
* **Power API / HAL v4** — cooperative `Sleep`, per-VM `SetWakePin` / `ClearWakePin`,
  `WakePin` / `WakeHigh`, capability-gated `LightSleep`, reset-on-wake `DeepSleep`,
  and actual `Shutdown`. Optional append-only callbacks/masks preserve source
  compatibility for old ports; unsupported modes never become delay fallbacks.
* **Port support** — shared ESP-IDF / Arduino-ESP32 light/deep sleep and chip-specific
  GPIO/timer wake; RP2 WFE CPU idle with sampled GPIO; STM32 CPU SLEEP and limited
  F4/L4/G4 PA0/high standby, L4 shutdown; optional Zephyr `CONFIG_POWEROFF`.
  No claim of RP2 dormant, STM32 STOP, generic Zephyr deep sleep or ESP shutdown.
* **Safety** — validate before entry, reject worker VMs / active MicroCS DMA / worker
  subsystem use, require a wake source for light/deep sleep, and reject live ESP radios.
  `Wifi.Off` now checks driver stop errors and sets the SDK mode to NULL for safe entry.
* **Docs and Studio** — power support matrix, wake/USB/watchdog/recovery caveats,
  six guarded capability-filtered templates, completion/hover reference and generated
  API reference. Fix stale release/BLE claims and current HAL version; publish power
  and previously omitted BLE documentation in the wiki navigation.
* **Tests** — power policy, simulated calls, SDK wake-backend variants, gallery filtering,
  and documentation/API/template consistency; no physical power-current claims.

## 1.15.0 — Radio-enabled releases, Bluetooth LE and Studio reliability

* **Release firmware** — Wi-Fi + ESP-NOW and NimBLE on all supported ESP32-family
  targets according to their onboard radios, including S2, H2, P4, C5 and C61;
  explicit Wi-Fi on Pico W / Pico 2 W. Builds check requested radio configuration
  and linked backend symbols before publishing. Back up scripts before upgrading
  ESP32: the larger app partition moves storage from older no-Wi-Fi releases.
* **Release capability detection** — the 1.14.0 release predates the shell `caps`
  report, while Studio now requires it for peripheral templates. This new version
  produces fresh firmware with capability reporting instead of reusing the old
  release. No hardware is guessed from board names or feature bits.
* **Studio upload responsiveness** — save, upload and Run Selection stop a running
  foreground script before entering its occupied operation queue, with a bounded
  wait and an actionable failure instead of an indefinitely queued transfer.
* **RP2 USB output** — raw block writes replace byte-by-byte TinyUSB writes, avoiding
  a USB write/task/flush cycle per output byte; binary output is unchanged.
* **Templates** — the two-display OLED example requires U8g2 and gets an OLED tag.
  CPU benchmark collection workloads fit fixed 96 / 128 KB release pools, with
  accurate labels and preallocated list capacity rather than a 4,000-entry list
  whose growth needs old and new tagged-value buffers at once.

* **Studio console input** — machine-mode `Console.ReadLine` no longer echoes input
  a second time; raw REPL echo is preserved. Studio forwards trailing spaces in script
  input unchanged, with echo-on/off, multiple-prompt and runtime regressions.


* **Cortex-M flash budget** — shared capability lookup / direct peripheral predicates;
  LTO on the 256 KB Cortex-M0 reference targets and benchmark preserves configured
  features while accommodating GCC/newlib size differences.
* **Capability-aware templates** — allocation-free shell `caps` and expanded `Hal.Has`,
  explicit `requires:` metadata for every template, connected-board filtering with offline
  browsing and conservative older-firmware fallback. Radio examples check Wi-Fi / BLE /
  ESP-NOW and required roles before referencing optional classes. Software/configuration
  flags and peripheral subfeatures are checked separately; bus support is not device detection.
* **BLE API v2 and examples** — backend option read/write discovery without starting the
  radio, configurable scan interval/window (including ESP menuconfig), operation-level role
  flags and `Ble.Reset` for clean reruns without erasing bonds. Added seven BLE templates
  and matching standalone examples. Fixed lost/ignored passkey writes and misleading no-op
  MTU/PHY/security setters. NimBLE role reports honor the configured roles. BLE remains opt-in in custom builds.


* **Web templates / foreground network lifecycle** — moved the raw HTTP example into
  Web server and WebSocket; servers now run until Stop rather than two seconds and use
  port 80. Raw TCP listeners share the host privileged-port fallback used by HttpServer.
  Close sockets, listeners, DNS servers and WebSockets at foreground run exit
  (normal, error or abort), preserving earlier interactive resources. Native server
  loops service idle work and Stop even with busy/stalled clients; propagate cancellation
  through native tail calls and nested route callbacks. Added shell lifecycle regressions.
* **Studio C# highlighting** — balanced interpolated/verbatim strings (`$@"` and `@$"`),
  including multiline HTML, nested quoted expressions, escaped braces and comments;
  added a dedicated highlighter regression suite.

* **ESP BLE menuconfig** — discoverable BLE submenu with prerequisite hints, device name,
  queue / GATT / scan / discovery limits, subscriptions, timeouts and UART buffer. The
  front-end connection limit follows NimBLE (C2 supports at most two); the BLE overlay uses
  two connections. Added BLE builds for C2 / C6 / H2 and IDF 5.5.2 builds for C5 / C61,
  plus non-BLE S2 / P4 checks. Guarded legacy CAN on CAN-FD-only chips (IDF omits
  the legacy TWAI driver there), fixing C5 firmware link failures. C5 / C61 target defaults
  use a 2.5 MB app slot; older chips retain their existing flash layout. Corrected TX-power clamping on C6 / C61 (minimum -15 dBm)
  and the +20 dBm readback on newer controllers. BLE remains opt-in, absent from release builds.
* **HTTP client diagnostics / framing** — distinguish a server closing before its status
  line from truncated headers, body or chunks; reject invalid / overflowing chunk lengths,
  invalid chunk terminators, incomplete trailers and overlong response lines. Added local
  wire-format regressions. The basic HTTP GET template no longer depends on a public time
  API and prints a web page instead.

* **Bluetooth LE** ([docs/BLE.md](docs/BLE.md)) — opt-in (`MCS_ENABLE_BLE`), not in any release
  firmware. C# `Ble` (radio, name, TX power, MTU, PHY, events), `BleAdvertisement` (name, UUIDs,
  manufacturer / service data, iBeacon, Eddystone-URL / -UID, raw data), scanning into
  `BleDevice` (parsed AD fields, beacons), a GATT server (`BleService`, `BleCharacteristic`,
  `BleDescriptor`: read / write / notify / indicate, `OnWrite`, `OnSubscribe`, encrypted /
  authenticated permissions), `BleConnection` in both roles (MTU, RSSI, parameters, pairing), a
  GATT client (`BleRemoteService` / `Characteristic` / `Descriptor`: discovery, long reads and
  writes, subscriptions), security (IO capabilities, passkeys, Secure Connections, bonds),
  `BleUart` (Nordic UART Service) and `BleUuid`. Backends: ESP-IDF NimBLE
  (`ports/esp32/example/sdkconfig.ble`), Pico W / Pico 2 W BTstack (`-DMICROCS_BLE=ON`, with or
  without FreeRTOS / Wi-Fi), Zephyr Bluetooth host (`overlay-ble.conf`; the west manifest now
  fetches `mbedtls`), ArduinoBLE (`tools/make_arduino.py --ble`; no central / security), and a
  simulated radio with peers for the PC (`make mcs-ble`, CMake `-DMICROCS_BLE=ON`, `--sim`).
  `make ble-test` (in `make check`) and CI builds for every backend.
* **Fixed: Pico W / Pico 2 W filesystem** — the Wi-Fi firmware (~1.1 MB with the CYW43 blob)
  overlapped the filesystem, which started at 1 MB, so the board silently fell back to a RAM
  disk. Builds with the radio now keep 1.5 MB for the firmware (`MCS_RP2_FW_RESERVE`) and put
  the filesystem after it.

* **Templates are plain files** — `tools/studio/templates/<nn-category>/` holds one `.cs`
  (or `.cfg`) file per template and a `templates.txt` that names and describes every file of
  the folder ([how to add one](tools/studio/templates/README.md)). `tools/studio/gen_templates.py`
  bundles them into `templates.js` (Studio still runs from `file://`); `--check` runs in CI and
  `make docs-check`, and fails on undescribed, missing or duplicate files. Snippets moved to
  `tools/studio/snippets.js`.
* **Templates dialog** — search matches every word and ranks name hits first, with
  highlighting; *Recent* category; tags derived from the code (*Wi-Fi*, *OLED*, *files*,
  *jobs*, *threads*, *DMA*, *input*); line count, copy button and a link to the source file;
  `PgUp`/`PgDn`/`Home`/`End`, `Ctrl+Enter` = insert at cursor, `Esc` clears the search first;
  a link to search all categories when the current one has no hit; narrow-window layout.
  Arrow keys no longer re-render the whole dialog.
* **Fixes** — saving a new file (e.g. a template as `main.cs`) over an existing file on the
  device now asks before replacing it; added templates get a name no open tab uses
  (`hello-2.cs`); *Add* / *Insert at cursor* are disabled without a selection (and *Insert*
  for read-only tabs); the save dialog selects the name without the extension for any
  extension (it assumed `.cs`) and accepts a relative path. The *Wi-Fi and networking* folder
  is `28-wifi-and-networking`; `test_templates.js` fails on a template without source or
  description.
* **Robustness pass** (ASan/UBSan, source + shell-protocol fuzzing of tests, examples and
  templates):
  * array / list / buffer allocations check `count * element size` for overflow — on 32-bit
    MCUs `new int[int.MaxValue]` wrapped to a tiny block and then overflowed the heap; it now
    fails with an out-of-memory error like any other too-large allocation;
  * `StringComparison` is honored by `IndexOf`, `LastIndexOf`, `Contains`, `StartsWith`,
    `EndsWith`, `Replace`, `Equals`, `string.Equals` and `string.Compare` (before,
    `IndexOf("x", StringComparison.Ordinal)` took the enum for a start index and
    `string.Compare(a, b, StringComparison.Ordinal)` ignored case);
    added `CurrentCulture` / `InvariantCulture`; `StartsWith` / `EndsWith(v, ignoreCase, culture)`;
  * `IndexOf(value, start, count)` honors `count`; `LastIndexOf(value, startIndex[, count])`
    searches backwards from `startIndex` (both ignored the extra arguments);
  * `Math.Round(x, digits)` returns huge values unchanged (`Math.Round(1e300, 15)` was
    infinity);
  * `StringBuilder.Remove` / `ToString(start, length)`, `LedStrip.Fill` and u8g2 rounded boxes
    no longer overflow `int` in their range checks;
  * `--sim-virtual`: the virtual clock advances on every `Hal.Micros` read, so
    `GPIO.PulseIn` and other busy-waits no longer spin forever; RAM filesystem reads/writes
    of 0 bytes no longer pass a null pointer to `memcpy`;
  * compiler: an unterminated string or char literal ending in `\` (or in an interpolation
    hole) no longer moves the lexer past the end of the source (which then tried to allocate
    gigabytes for the token array); `void.X` in an expression is a syntax error instead of a
    null-pointer `strlen`; `mcs -c` prints each compile error once (it printed the last one twice).
  * native GC roots beyond `MCS_MAX_ROOTS` spill to the heap: recursion through LINQ
    callbacks (or `Sort` comparers ...) ended the script with an uncatchable
    *fatal: too many temporary roots*; it now reaches the normal, catchable
    `StackOverflowException`. A panic also restores the root count (in the REPL every later
    native call failed after one such error);
  * `Json.Parse` no longer roots every open object (16 nested objects were fatal); nesting
    deeper than 32 is a `FormatException` as documented;
  * `Enumerable.Range(start, count)` throws when `start + count - 1` overflows `int`;
  * `tools/fuzz.py` (run in CI) also mutates the examples and every Studio template, and
    alternates `--sim` / `--sim-virtual` runs with compile-only runs.
* **.NET semantics** (`tests/t27_dotnet_semantics.cs`):
  * `int.MinValue / -1` and `int.MinValue % -1` (and `Math.DivRem`) throw `OverflowException`
    like .NET instead of returning `MinValue` / `0`; the compiler no longer folds such constants;
  * `foreach` over a `List<T>` throws `InvalidOperationException` *Collection was modified;
    enumeration operation may not execute.* when the loop body changed the list (`Add`,
    `Insert`, `Remove*`, `Clear`, `Sort`, `Reverse`, an indexer store …); over a `Dictionary`
    when a key was added. Removing entries or clearing the dictionary during `foreach` (allowed
    in .NET 8) no longer skips the entry after a removed one. Arrays never throw, as in .NET.
    `t27` runs in the .NET 8 parity job, so its output is checked against the real runtime;
  * bytecode images are **version 4**: the checked `FOR_ITER` form marks its slot byte with
    bit 7 and uses two more hidden locals. Version 1–3 images still load and run their loops
    unchecked; older VMs reject v4 images, so recompile `.mcsb` files for them.
* **`min` profile** uses `switch` dispatch (`MCS_COMPUTED_GOTO=0`): ~1 KB less Thumb-1 code for
  ~3 % more instructions, so the `m0-64k` firmware keeps ~1 KB of its 64 KB flash free (it had
  264 B left with the CI toolchain).

## 1.14.0 — Web server, WebSockets and Pico W Wi-Fi

The server side of the network, Wi-Fi on the Raspberry Pi Pico W / Pico 2 W, and Studio
templates regrouped by peripheral. Reference: [docs/NETWORK.md](docs/NETWORK.md).

* **`HttpServer`** — `new HttpServer([address, ]port)`; routes `Get` / `Post` / `Put` /
  `Delete` / `Patch` / `On([method, ]path, handler)` with `{param}` and `*` prefix paths,
  `NotFound`, `ServeFiles(urlPrefix, folder)` (index.html, MIME types, `.gz` copies), `Cors`,
  `Start` / `Poll(ms)` / `Run` / `Stop`, `Port`, `Requests`. Handlers return a string (HTML),
  `byte[]` or any object (JSON) — or answer through **`HttpRequest`**: `Method`, `Path`,
  `Query`, `Headers`, `Header(name)`, `Body` / `Bytes` / `Json`, `Param(name[, default])` (path,
  query and form fields), `Send`, `SendJson`, `SendStatus`, `SendFile`, `Redirect`,
  `SetHeader`, `Authenticate` (HTTP Basic).
* **`WebSocket`** — endpoints on an `HttpServer` (`web.WebSocket(path, onMessage[, onOpen[,
  onClose]])`, `Broadcast`) and a client (`Connect("ws://…" / "wss://…")`, `Send` text or
  binary, `Receive`, `OnMessage` + `Poll`, `Ping`, `Close(code, reason)`, `CloseStatus`);
  fragmented messages, pings and close handshakes handled; new `WebSocketException`.
* **`DnsServer`** — answers every name (or one `Domain`) with the board's address for
  captive portals; served by `HttpServer.Poll` too.
* **`Wifi.Wps([timeout[, save]])`** (push button) and **`Wifi.SmartConfig(...)`** (ESP-Touch /
  AirKiss) on the ESP32 and in the simulator; network backends get an optional `provision`
  entry (`MCS_NET_API_VERSION` 2).
* **Raspberry Pi Pico W / Pico 2 W** — new backend `ports/rp2/mcs_port_rp2_net.c` on
  `pico_cyw43_arch_lwip_sys_freertos`: station + AP, scan, events, static IP, hostname,
  country, power save, TX power, auto-reconnect, DHCP server for AP clients, SNTP, mDNS,
  ping, sockets, `Http`, the servers and WebSocket. CMake option `MICROCS_WIFI` (on by
  default for `*_w` boards with `MICROCS_OS=freertos`), `ports/rp2/wifi/lwipopts.h`, the
  on-board LED as pin `"LED"` / `"WL_GPIO0"`, RTC through `settimeofday`. CI builds and the
  release page get `pico_w` and `pico2_w` firmware.
* **Saved networks without NVS** — `Wifi.Save` / `Forget` / `SavedSsid` fall back to the file
  `/.wifi` (`MCS_WIFI_CREDS_FILE`) on ports without their own store; `Net.Has("Wifi.Save")`.
* New `Net.Has` names `"HttpServer"`, `"WebSocket"`, `"DnsServer"`, `"Wifi.Wps"`,
  `"Wifi.SmartConfig"`, `"Wifi.Save"`; `Net.Backend` `"rp2"`. Options
  `MCS_HTTP_SERVER_MAX_ROUTES`, `MCS_HTTP_SERVER_TIMEOUT`, `MCS_WS_MAX_MESSAGE`,
  `MCS_WS_MAX_CLIENTS`, `MCS_RP2_HOSTNAME`, `MCS_RP2_WIFI_RETRIES`, `MCS_RP2_RECONNECT_MS`,
  `MCS_RP2_DHCPS_LEASES`.
* **PC** — servers bind ports below 1024 at *port + 8000* when that needs root (`:80` →
  `:8080`, noted on stderr); `tests/test_net_server.py` tests the HTTP, WebSocket and DNS
  servers against Python clients.
* **Studio** — templates regrouped by peripheral: GPIO, PWM, ADC, UART, I2C and SPI each
  carry their settings and async versions, new categories *QSPI*, *I2S audio*, *CAN*,
  *Board and system*; new **Web server and WebSocket** category (LED web page, REST API,
  live WebSocket dashboard, WebSocket client, captive portal, files from flash, WPS /
  SmartConfig) and **ESP-NOW** (messages, broadcast, encrypted peers) — 194 templates;
  snippets `httpserver`, `route`, `wsserver`, `wsclient`, `captive`, `wps`; completions,
  hover and parameter info for `HttpServer`, `HttpRequest`, `WebSocket`, `DnsServer` and
  the new `Wifi` methods. Template tests run the server templates one at a time.

## 1.13.0 — Wi-Fi & networking

C# scripts get the network on every ESP32 with a radio (ESP32, S2, S3, C2, C3, C5, C6), and
real sockets + HTTP on the PC. Reference: [docs/NETWORK.md](docs/NETWORK.md).

* **`Wifi`** — station, soft access point and both at once: `Connect(ssid, password[, timeout[,
  bssid, channel]])` (returns `false` with `StatusName` / `ReasonName()` instead of throwing),
  `Begin` + `WaitConnected` (non-blocking), `ConnectEnterprise` (WPA2/WPA3-Enterprise, IDF 5.1+),
  `Reconnect`, `Disconnect`, `Off`; `Scan([hidden])` → `WifiNetwork` (SSID, BSSID, RSSI, channel,
  security, protocols, quality); `StartAp` / `StopAp` / `ApClients` (MAC, IP, RSSI) / `SetApIp`;
  `SetStaticIp` / `UseDhcp`, `Ip`, `Netmask`, `Gateway`, `Dns`, `Dns2`, `Ipv6`, `Mac` / `ApMac`;
  options `Hostname`, `Country`, `TxPower`, `PowerSave`, `Protocol` / `ApProtocol` (incl. LR and
  11ax), `Bandwidth`, `Band` (5 GHz on dual-band chips), `MinAuth`, `ListenInterval`,
  `AutoReconnect`, `Persistent`, `Antenna`; WPA3-SAE and PMF automatically.
* **Events** — `Wifi.OnEvent((ev, value) => …)` on the script thread: connected, got/lost IP,
  disconnected (with reason), scan done, AP start/stop, station joined/left/assigned, ESP-NOW,
  NTP sync. They travel through the HAL event ring (`MCS_HAL_EV_NET`).
* **Saved networks** — `Wifi.Save` / `Forget` / `SavedSsid` in NVS; `Wifi.Connect()` joins it.
  menuconfig *Connect at boot* (`CONFIG_MICROCS_WIFI_SSID` / `_PASSWORD` /
  `_AUTOCONNECT`) joins before `/main.cs` runs (`mcs_esp32_wifi_autoconnect()` for your own
  firmware).
* **`EspNow`** — `Start`, `AddPeer(mac[, channel[, key]])`, `RemovePeer`, `SetPmk`, `Send` with
  delivery report, `Broadcast`, `Receive` → `EspNowMessage`, `OnReceive((mac, data) => …)`.
* **`Ntp`** (SNTP, POSIX time zones, `LocalTime` / `UtcTime` / `UtcOffset`), **`Mdns`** (host
  name, services, `.local` lookups — with the `espressif/mdns` component, detected
  automatically), **`Ping`**, **`Net.Has(feature)`** / `Net.Backend` (also via `Hal.Has`).
* **Sockets** — `TcpClient` (connect timeout, `ConnectTls`, `Write` / `WriteLine` / `Read` /
  `ReadLine` / `ReadToEnd` / `ReadByte`, `Timeout`, `NoDelay`, `KeepAlive`, endpoints),
  `TcpListener` (`AcceptTcpClient(timeout)` → `null`, `Pending`, port 0), `UdpClient`
  (`Send` / `Receive(timeout)`, broadcast, multicast), `Dns`. New exceptions `SocketException`
  (an `IOException`) and `HttpRequestException`.
* **`Http`** — HTTP/1.1 client: `Get`, `Head`, `Delete`, `Post`, `Put`, `Patch`, `Request`,
  `GetString`, `Download` (streamed to a file, atomic rename), `UrlEncode`; headers as
  `Dictionary<string, string>`, lines or `string[]`; redirects, chunked bodies, `Timeout`,
  `UserAgent`; `HttpResponse` (`StatusCode`, `Body`, `Bytes`, `Header(name)`,
  `EnsureSuccess()` …). **HTTPS** through esp-tls and the ESP-IDF certificate bundle (or your CA).
* **`Json`** (every port, `MCS_ENABLE_JSON`) — `Json.Parse` → `Dictionary<string, object>` /
  `List<object>` / numbers / strings, `Json.Stringify(value[, indent])` for collections, values
  and script objects, `Json.Escape`, and `JsonSerializer.Serialize` / `Deserialize`.
  Off in the `min` profile and in the 256 KB-flash Cortex-M0 reference firmware (`-DMCS_ENABLE_JSON=0`).
* **Simulator** — `./mcs --sim` adds a simulated radio (networks, wrong-password / not-found
  paths, AP clients, ESP-NOW echo peer, mDNS, ping); sockets and HTTP use the PC's network.
* **ESP32 port** — `ports/esp32/mcs_port_esp32_net.c`; menuconfig *MicroCS → Wi-Fi and
  networking* (`CONFIG_MICROCS_WIFI`, on by default on chips with Wi-Fi;
  `CONFIG_MICROCS_WIFI_RESERVE_KB` keeps RAM for the driver). The driver starts only when a
  script first uses Wi-Fi. **The example's `partitions.csv` now has a 2 MB app partition**
  (Wi-Fi + TLS ≈ 1.6 MB) and ~1.9 MB LittleFS — reflashing it re-formats the `storage`
  partition, so copy your scripts off first (or keep the old layout: `partitions_nowifi.csv`
  with `CONFIG_MICROCS_WIFI=n`).
* **Release files stay without Wi-Fi** — the ready-to-flash ESP32 images are built with
  `CONFIG_MICROCS_WIFI=n` and the unchanged 1.5 MB app / 2.4 MB LittleFS layout
  (`partitions_nowifi.csv`), so updating them keeps your files; the Arduino library zip leaves
  `modules/net` out. Wi-Fi is for firmware you build from `ports/esp32/example`.
* **Studio** — 16 *Wi-Fi and networking* templates (connect, events, scan, soft AP, options,
  saved network, HTTP GET/POST JSON, HTTPS download, web server, TCP, UDP, NTP clock, ESP-NOW,
  mDNS + ping, MQTT publish), 10 new snippets, completions for every new class.
* **Config** — `MCS_ENABLE_NET`, `MCS_ENABLE_SOCKETS`, `MCS_NET_SIM`, `MCS_NET_TIMEOUT`,
  `MCS_WIFI_CONNECT_TIMEOUT`, `MCS_WIFI_SCAN_MAX`, `MCS_NET_MAX_READ`, `MCS_HTTP_MAX_BODY`,
  `MCS_HTTP_MAX_HEADER`, `MCS_HTTP_MAX_REDIRECTS`, `MCS_ESPNOW_MAX_PEERS`, `MCS_NET_SLICE_MS`,
  `MCS_ESP32_WIFI_RETRIES`, `MCS_ESP32_ESPNOW_QUEUE`, `MCS_ENABLE_JSON` — all in the
  `config/mcs_user_config.h` template.
* **Fixes** — when a native stops early because the run must end (Ctrl-C, Studio *Stop*,
  `--time-limit`), the run now ends with the abort instead of a follow-up error such as a
  `NullReferenceException` on the native's result. New public C API `mcs_dict_len` /
  `mcs_dict_at` for modules.
* **Docs** — new [NETWORK.md](docs/NETWORK.md); README, ESP32 port README and guide,
  ARCHITECTURE, HAL, PORTING, CONFIGURATION, TESTING and STDLIB (now also listing the
  network, threads and U8g2 classes) updated.
* **Tests** — `t24_net` (whole network API on the simulated radio, TCP/UDP over loopback),
  `t25_json`, `tests/test_net.py` (25 HTTP checks against a local server, part of `make test`).

## 1.12.1 — Reliable uploads on ESP32 native USB; Studio templates for U8g2, peripheral settings and DMA

* **Reliable uploads on ESP32 USB-Serial-JTAG (ESP32-C3 / C6 / S3 / H2 native USB).** Saving a
  script from Studio sometimes stalled ("upload stalled - retrying slower") because ESP-IDF's
  USB-Serial-JTAG driver silently drops received bytes when its 256-byte buffer is full —
  which happened while LittleFS erased a flash block or while the console waited on UART0.
  Fixes: `put <f> <len> <window>` — the device acknowledges every window with `\x04MORE`
  (`MCS_SHELL_PUT_WINDOW`, 256), so the host can never overrun it (Studio and
  `tools/mcs_remote.py` use it; older firmware still gets the plain stream); the ESP32 console
  installs the driver with a 4 KB receive buffer (`MCS_ESP32_USB_RXBUF`) and sleeps on the USB
  port instead of UART0 while a USB host is attached.
  RP2040 was never affected: TinyUSB holds the host off (USB NAK) instead of dropping bytes.
* **Studio templates for U8g2 / U8x8 displays.** A new *U8g2 displays* category with 20
  scripts that together cover the whole API: hello, every bus (`I2C`, `SoftI2C`, `SPI`,
  `SoftSPI`, `Soft3WireSPI`, 8080 / 6800, `KS0108`), all shapes, text / alignment / font metrics,
  fonts from files and `byte[]`, XBM bitmaps and icons, rotation / flip / contrast / sleep /
  draw colours / clipping, page mode (`Draw`, `FirstPage` / `NextPage`), `Print`, buttons and
  text bars, the built-in menus (scripted with `SetMenuInput`), a custom menu with
  `GetMenuEvent`, the u8log terminal, a dashboard, a clock, an animation with
  `UpdateDisplayArea`, buffer screenshots (`GetBuffer`, `Dump`, `WriteBufferXBM`), U8x8 text and
  status screens, two displays. Snippets `u8g2`, `u8frame`, `u8page`, `u8x8`; Studio completion,
  signature help and hover for every `U8g2` / `U8x8` member. `tests/studio/test_templates.js`
  runs them on the simulated SSD1306 with a u8g2 build (new CI step).
* **Studio templates for peripheral settings and DMA.** 17 new ready-to-run templates in two
  new categories: *Peripheral settings* (what the board supports, button / LED / open-drain
  `GpioConfig`, I2C, SPI and UART on your own pins, `PwmConfig`, `AdcConfig`, board options and
  reusable settings) and *DMA* (`DmaBuffer`, `SPI.WriteAsync` in the background, a
  double-buffered display loop, `I2C.WriteReadAsync`, `UART.ReadAsync` with timeout / cancel,
  `QSPI.ReadAsync`, `Dma.Copy` / `Fill`). New snippets `gpiocfg`, `i2ccfg`, `spicfg`, `uartcfg`,
  `dmabuf`, `spiasync`. Studio now has 169 C# templates and 61 snippets; CI runs every one on the
  simulated board.

## 1.12.0 — Peripheral configuration and DMA (HAL API v3)

* **Configuration objects for every peripheral.** `GpioConfig`, `UartConfig`, `I2cConfig`,
  `SpiConfig`, `QspiConfig`, `PwmConfig` and `AdcConfig` carry the settings a vendor SDK takes in
  its init struct — pins, pulls, slew rate / speed, drive strength, Schmitt trigger, pad hold,
  hardware inversion, glitch filters, initial output level, alternate functions, buffer sizes,
  RS-485, three-wire SPI, sampling delay, ADC attenuation / oversampling, PWM resolution /
  polarity / centre alignment, DMA on / off. New overloads: `GPIO.Configure`, `new Pin(pin, cfg)`,
  `pin.Configure`, `UART.Open(port, cfg)`, `I2C.Open(bus, cfg)`, `SPI.Open(bus, cfg)`,
  `new SpiDevice(bus, cs, cfg)`, `QSPI.Open(bus, cfg)`, `PWM.Configure`, `ADC.Configure`.
  Unset properties stay at the board default; values are range-checked on assignment; a board
  that cannot honour a setting throws `NotSupportedException` / `ArgumentException` with the
  reason (`DriveMa: 2, 4, 8 or 12 mA`). Nothing is silently ignored.
* **Board options**: chip-specific settings by name (`cfg.Set("queue_size", 8)`), listed with
  their range and help by `Hal.Options([peripheral])`.
* **DMA and asynchronous transfers.** `DmaBuffer` (DMA-capable memory), `SPI.TransferAsync /
  WriteAsync / ReadAsync`, `I2C.WriteAsync / ReadAsync / WriteReadAsync`, `UART.WriteAsync /
  ReadAsync`, `QSPI.ReadAsync / WriteAsync`, device-object variants, and `Dma.Copy / Fill /
  CopyAsync / FillAsync / WaitAll`. Each returns a `DmaTransfer` (`Wait`, `OnComplete`,
  `Cancel`, `Result`, `Error`). Transfers stay in order per bus, keep their buffers alive and
  complete through the event queue, never in an interrupt. Synchronous `SPI.Write / Read` and
  `QSPI.Read` accept `DmaBuffer`s of any size. Boards without DMA run the same calls
  synchronously, so scripts are portable. `Hal.Has("DMA.SPI")` & co. tell what really runs in
  the background.
* **ESP32 port**: everything — runtime pins for UART / I²C / SPI / QSPI / PWM (no more C pin
  tables needed), GPIO filters (pin and flex glitch filters), hold, invert, drive; UART buffers,
  inversion, RS-485 half duplex, RX timeout; I²C pull-ups and filter; SPI three-wire, input delay,
  max transfer; LEDC resolution / invert; ADC attenuation / resolution / oversampling; SPI and
  QSPI transfers on DMA, async memcpy on GDMA chips, `DmaBuffer` in internal DMA-capable RAM.
* **RP2040 / RP2350 port**: GPIO pulls, drive, slew, Schmitt, alternate function, invert (pad
  overrides or software), glitch-free initial level; runtime UART / I²C / SPI pins, UART
  inversion and RTS / CTS, I²C spike filter, pull-ups and timeout; PWM resolution, polarity and
  phase-correct mode; ADC oversampling; SPI transfers and memory copy / fill on DMA.
* **STM32 port**: `GpioConfig` (pull, speed, open drain, alternate function) and `PwmConfig`
  (polarity, centre-aligned). Zephyr, Arduino and Cortex-M keep the basic settings.
* **Simulator** implements and validates every setting and runs DMA transfers through the event
  queue, so scripts can be tested on a PC (`mcs --sim --sim-virtual`).
* **C API**: `MCS_HAL_API_VERSION` 3. New optional `mcs_hal_t` members (`*_configure`, `options`,
  `dma_alloc` / `dma_free` / `dma_align` / `dma_channels`, `dma_copy`, `dma_abort`,
  `spi_transfer_async`, `i2c_transfer_async`, `uart_write_async`, `uart_read_async`,
  `qspi_command_async`), `mcs_*_config_init()`, `mcs_hal_opt()`, `mcs_hal_error()`,
  `MCS_HAL_EV_DMA`; `MCS_HAL_MAX_DMA`, `MCS_HAL_MAX_OPTS` and `MCS_ENABLE_HAL_DMA` (leave the DMA
  classes out, ~11 KB; off in the 256 KB Cortex-M0 reference build) in `mcs_user_config.h`;
  `mcs_userdata_of()`. A port can return `MCS_HAL_ENOTSUP` from an async call to get the
  synchronous fallback. Board tables written for v1 / v2 compile and work unchanged.
* Docs: [Peripheral configuration](docs/PERIPHERALS.md), [DMA](docs/DMA.md), step-by-step
  guides for [peripheral settings](docs/guides/PERIPHERALS.md) and [DMA](docs/guides/DMA.md),
  example `17_config_dma.cs`, Studio completions for every new class.

## 1.11.0 — U8g2 displays (optional)

* **C# `U8g2` and `U8x8` on olikraus' u8g2 (2.37.1, the latest release)** for 365 monochrome
  OLED / LCD / e-paper displays (SSD1306, SH1106, SSD1309, ST7920, ST7565, PCD8544, KS0108, …)
  on hardware I²C / SPI, bit-banged I²C / SPI / 3-wire SPI, 8080 / 6800 parallel and KS0108 pins.
  The wrapper covers both APIs: full-buffer and page mode (`Draw(() => …)`,
  `FirstPage`/`NextPage`), every drawing call, text and UTF-8 in any font, metrics, rotation,
  clipping, bitmaps, buttons, the three u8g2 menus (buttons on pins or `SetMenuInput(() => …)`),
  u8log terminals, Arduino-style `Print`, buffer export (`GetBuffer`, PBM, XBM, `Dump`), and the
  U8x8 tile API. Display names work as u8g2 writes them, including Arduino class names.
  Errors say what is wrong (`no display answers at I2C address 0x3C on bus 0`, the list of
  compiled-in displays / fonts).
* **Optional, never in the release firmware.** u8g2 is not bundled. CMake (`-DMICROCS_U8G2=ON`, or
  `MCS_ENABLE_U8G2 1` in `mcs_user_config.h`), ESP-IDF and Zephyr (`CONFIG_MICROCS_U8G2`), Arduino
  (`make_arduino.py --u8g2` + the U8g2 library), PlatformIO and make (`make mcs-u8g2`) find the
  sources in `MICROCS_U8G2_DIR` or the usual places. Otherwise they stop with instructions, or
  download 2.37.1 once with `MICROCS_U8G2_DOWNLOAD=ON` / `make fetch-u8g2`.
* **Displays and fonts are chosen before the build** (`MCS_U8G2_DISPLAYS`, `MCS_U8G2_FONTS`,
  `MCS_U8X8_FONTS`, or `MICROCS_U8G2_DISPLAYS` / `_FONTS` lists that are checked at configure
  time). Only those are linked; the defaults are four common OLEDs and six small fonts (≈ 7 KB).
  **Every other font still works**: `tools/u8g2.py extract` writes it as a file, and
  `SetFont("name")` loads `/fonts/u8g2_font_name.bin` at run time (or a path / `byte[]`).
* `tools/u8g2.py`: `where`, `fetch`, `displays`, `fonts` (with sizes), `extract`, and `config`
  (prints the lines for `mcs_user_config.h`, CMake, make and PlatformIO).
* **Simulator OLED:** `mcs --oled[=sh1106,128x32,0x3D]` emulates an SSD1306 / SH1106 on the
  simulated I²C bus and draws each frame in the terminal. `make u8g2-test` compares the frames of
  the test scripts and smoke-runs `examples/u8g2/` (7 examples). CI runs it, builds with CMake,
  and builds a Pico firmware and the Arduino library with u8g2.
* HAL: `mcs_hal_spi_config(vm, bus, cfg)` reconfigures an open SPI bus (mode, clock) for drivers.
* Docs: [docs/U8G2.md](docs/U8G2.md).

## 1.10.0 — real OS threads (optional): FreeRTOS, Zephyr, POSIX, second core

* **C# on real OS threads, opt-in.** Choose an OS (ESP-IDF menuconfig *Real threads*,
  Zephyr `CONFIG_MICROCS_THREADS=y`, CMake `-DMICROCS_OS=freertos|posix`, `make OS=posix`,
  or `MCS_OS` in `mcs_user_config.h`) and scripts and functions run on their own OS tasks,
  in parallel: `Thread.Start(path [, core [, heapKB]])`, `Thread.Run(path, fn, args…)`,
  `Thread.RunOn(core, path, fn, args…)` for the **second core of the ESP32 / ESP32-S3 /
  RP2040** (FreeRTOS SMP) and SMP Zephyr boards, and `Thread.Every(ms, …)` for OS-timed periodic
  work. A `Worker` has `Join`, `Stop`, `Result`, `State` and `Error`; named `Channel`s copy
  values between threads. Each thread gets its own VM and heap. The filesystem, console and
  peripherals are shared under locks. Interrupt callbacks stay with the main VM.
* **The scheduler uses the OS too:** with an OS chosen, `jobs.cfg` file jobs run on their own
  OS thread, timed by the OS (`MCS_SCHED_THREADS`, default on with threads). `startup` jobs and
  jobs marked `main` stay on the main VM; when no thread slot or heap is free, a job falls back
  to the main VM. Per job: `core=`, `prio=`, `stack=`, `heap=` (e.g. `every 10ms /control.cs
  core=1 heap=24k prio=1`). Without an OS these options are ignored and the job is polled as
  before. `jobs` / `.jobs` show thread jobs.
* **Release firmware runs on FreeRTOS:** the ready-to-flash ESP32 images are built with
  threads on ESP-IDF's FreeRTOS, and the Pico / Pico 2 / RP2040-Zero `.uf2` files with
  Raspberry Pi's FreeRTOS SMP kernel (the RTOS pico-examples uses; pico-sdk itself has none),
  so `Thread.RunOn(1, …)` uses the second core out of the box. STM32 images stay bare-metal.
* **Headers found or asked for:** CMake picks up a `freertos_kernel` / `FreeRTOS-Kernel` /
  CubeMX target, `MICROCS_FREERTOS_PATH` + `_PORT` + `_CONFIG_DIR` or `FREERTOS_KERNEL_PATH`, or
  a FreeRTOS-Kernel folder in the project. If none is found, configure stops and names the
  variable to set. `make OS=freertos` and IDE builds stop the same way.
* `ports/rp2/example`: `-DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=…` (Raspberry Pi's
  FreeRTOS-Kernel fork) runs the REPL on core 0 under FreeRTOS SMP and leaves core 1 for C#,
  on RP2040 and RP2350. `ports/zephyr/example/overlay-threads.conf`.
* C API (`mcs_threads.h`, `mcs_os.h`): `mcs_thread_start/stop/join/info`. `mcs_runtime` sets
  threads up by itself (`cfg.thread_setup`, `cfg.thread_heap`, `cfg.thread_stack`).
  Filesystems can be locked (`mcs_vfs_t.lock`). Worker VMs can open the HAL without callbacks
  (`mcs_hal_open_lib_ex`).
* **No OS = no change:** `MCS_OS_NONE` is the default. Without an OS, no thread code is
  compiled, and the core gains only `Thread.Cores` (1), `Thread.CurrentCore` (0) and
  `Thread.Os` (`"None"`).
* Core: the parser and `OrderBy().ThenBy()` no longer use static buffers, so separate VMs can
  run at the same time.
* Tests: `make threads-test` (in `make test`), `make tsan-test` (ThreadSanitizer),
  `make freertos-test` (the FreeRTOS POSIX simulator port), and CI builds of ESP32,
  ESP32-C3, Zephyr and RP2040 with threads.

## 1.9.1 — ready-to-flash STM32 firmware

* **STM32 firmware in every release:** `microcs-<version>-<board>.bin` and `.hex` for
  Nucleo-F401RE, Nucleo-F411RE, Nucleo-F446RE, Black Pill F411CE, Nucleo-G474RE, Nucleo-L476RG and
  Nucleo-H743ZI. Copy the `.bin` to the Nucleo's ST-LINK drive (Black Pill: USB DFU), open the
  virtual COM port at 115200 baud or MicroCS Studio and use the C# REPL — no CubeMX project or
  toolchain needed. Each image sets up the clock from the internal oscillator (no crystal
  needed), the console UART, `new Pin("LED")` and every GPIO, I2C1 on the Arduino D14/D15 pins,
  four PWM / `Servo` outputs on TIM3 and TinyFS in the chip's own flash (28 KB on F4, 105 KB on
  G474/L476, 307 KB on H743) for `/boot.cs`, `/main.cs` and uploads. Pins and flash layout:
  [ports/stm32/firmware/README.md](ports/stm32/firmware/README.md).
* `tools/build_stm32_firmware.sh <board>` builds them (downloads the STM32Cube HAL + CMSIS of
  the family); the board setup is one file, `ports/stm32/firmware/board.c`.
* CI builds the 7 images on every push and boots the F4 and H743 ones in the Renode emulator
  (`tools/renode_check.py`: REPL, `Hal.Board`, a file written to and read back from the internal
  flash, the LED pin).
* STM32 flash driver: `mcs_stm32_flash_init()` with an explicit address on F2/F4/F7 may now use a
  run of equal small sectors (e.g. the 16 KB sectors 1–3) when the linker script keeps them free
  and marks them with `__mcs_fs_flash_start` / `__mcs_fs_flash_end`.

## 1.9.0 — TinyFS on internal flash, Servo driver, complete config header

* **TinyFS, a built-in flash filesystem for the MCU's own flash:** STM32s (and any other chip)
  without an external NOR/NAND can keep `/boot.cs`, `/main.cs`, uploads and data files in a few KB
  of internal flash — e.g. 8 KB of a 64/128 KB F103. Part of MicroCS (`modules/fs/mcs_vfs_tinyfs.c`,
  MIT, no sources to fetch, ~5 KB of Thumb-2 code, ~1 KB of static RAM, no heap): a log of CRC'd
  records, power-fail safe (garbage collection ends with a commit record), wear levelling across
  the erase blocks, works from 2 erase blocks. Selected like LittleFS and YAFFS2:
  `MCS_ENABLE_TINYFS 1`, CMake `-DMICROCS_FS=tinyfs`, ESP-IDF menuconfig *TinyFS*
  (`CONFIG_MICROCS_FS_TINYFS`), Zephyr `overlay-tinyfs.conf` (`CONFIG_MICROCS_TINYFS`), Arduino
  `make_arduino.py --fs tinyfs`; `mcs_flashfs_mount(..., MCS_FLASHFS_TINYFS, ...)` or
  `MCS_FLASHFS_DEFAULT`. Options `MCS_TINYFS_MAX_FILES` / `_CHUNK` / `_HANDLES` / `_MAX_UNIT`.
* **Internal-flash size:** `MCS_INTFLASH_SIZE` (CMake `-DMICROCS_FS_SIZE=8192`) sets how much of
  the chip's flash holds files on STM32 (`MCS_STM32_FS_SIZE`) and RP2 (`MCS_RP2_FS_SIZE`). With
  TinyFS alone the STM32 driver uses each flash page as a block (`MCS_STM32_FS_BLOCK` overrides).
* **Flash port for new MCUs:** fill a `mcs_flash_port_t` (region size, erase unit, program unit,
  `read` / `write` / `erase`) and `mcs_intflash_init()` makes the `mcs_flash_t` that TinyFS,
  LittleFS and YAFFS2 use (`include/mcs_flash.h`, skeleton in `ports/template`). STM32, RP2040 /
  RP2350, ESP32 and Zephyr ship theirs.
* **`Servo` driver** (built-in `servo` driver, like `ws2812`): `new Servo(channel[, minUs, maxUs[,
  maxAngle]])`, `Angle` / `Write`, `Pulse` / `WritePulse`, eased `MoveTo(deg, ms)`, `Detach` /
  `Attach`. Default backend: the board's HAL PWM at 50 Hz on every port and the simulator; register
  `MCS_SERVO_DRIVER(ops, ctx)` for a PCA9685 or a servo bus. Off with `MCS_ENABLE_SERVO 0`, CMake
  `-DMICROCS_SERVO=OFF`, `CONFIG_MICROCS_SERVO=n`, `make_arduino.py --no-servo`,
  `MICROCS_SERVO := 0`. Studio: Servo templates (sweep, smooth motion, pan/tilt with two servos),
  a `servo` snippet, completions and docs.
* **Config header with the defaults set:** [`config/mcs_user_config.h`](config/mcs_user_config.h)
  now holds every option *set* to its default value (each in `#ifndef`, so `-D` and the build
  systems still win) — copying it enables it, then edit the values. Only the options detected per
  compiler / CPU / board stay commented. `tools/gen_config.py --profile lowram -o
  mcs_user_config.h` writes it with another profile's values (`--profile auto --ram-kb --flash-kb`,
  `-D NAME=VALUE`); MicroCS warns when the header's profile differs from the build's.
  `make_arduino.py --profile` uses it. `make test` checks the template against
  `include/mcs_config.h` (`gen_config.py --check`), `make check` builds every generated profile.
* **CI:** 18 popular STM32 boards (Blue Pill, Black Pill F401/F411, F4 / F429 Discovery, Nucleo
  F072/F446/F767/G071/G431/G474/H563/H743/L073/L432/L476/U575/WB55) compile in parallel jobs, each
  in 8 configurations at once (default, no ws2812/servo, no drivers, auto profile, internal flash +
  LittleFS / YAFFS2 / TinyFS) with the ST headers cached. `make tinyfs-test` (power cut at every
  flash operation) runs in `make test` and the flash-filesystem job.

## 1.8.0 — project config header

* **`mcs_user_config.h`, one header for every build option:** copy the new template
  [`config/mcs_user_config.h`](config/mcs_user_config.h) — every `MCS_*` option of the core,
  modules and ports, commented out with its default and a description — into your project and
  uncomment what you change. `include/mcs_config.h` includes it before anything else, so the
  library, the ports and the application all see it. It is found automatically when it is on the
  include path (`__has_include`); `-DMCS_USER_CONFIG_FILE="…"` names another file,
  `-DMCS_USER_CONFIG=1` forces it on compilers without `__has_include`, `-DMCS_USER_CONFIG=0`
  ignores it. Guide: [docs/CONFIGURATION.md](docs/CONFIGURATION.md).
* **Compiler options keep working and win:** precedence is `-D` (CMake, Kconfig, menuconfig,
  PlatformIO flags) → the header → the profile → the defaults.
* **Profiles by name:** `#define MCS_PROFILE MCS_PROFILE_LOWRAM` (or `-DMCS_PROFILE=…`) with
  `MCS_PROFILE_FULL/AUTO/EMBEDDED/MCU/LOWRAM/TINY/MIN/LINUX`. Build systems pass their choice as
  `MCS_DEFAULT_PROFILE`, so a profile in the header wins over CMake `MICROCS_PROFILE`, menuconfig or
  Kconfig. The old `-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_x.h"'` still works.
* **Build integrations find the header:** CMake (next to the top-level `CMakeLists.txt` or in its
  `include/`, `src/`, `main/`; `MICROCS_CONFIG=path|NONE`), ESP-IDF (project folder / `main/`,
  menuconfig *Project config header*), Zephyr (application folder, `include/`, `src/`;
  `CONFIG_MICROCS_USER_CONFIG_FILE`), PlatformIO (project `include/`), `microcs.mk`
  (`MICROCS_CONFIG`, `MICROCS_PROFILE` now accepts every profile), Arduino
  (`tools/make_arduino.py` writes `src/mcs_user_config.h`; new `--config` and `--profile`; it no
  longer patches `mcs_config.h`).
* **Ports:** the STM32 port reads `MCS_STM32_HAL_HEADER` from the header too; every port documents
  its options in the template.
* **Examples and tests:** `examples/lowram` is configured by its own `mcs_user_config.h`;
  `make test` checks discovery, `MCS_USER_CONFIG_FILE`, `MCS_USER_CONFIG=0/1` and the precedence
  (`tests/c/test_config.c`), CI repeats it through CMake, `make check` builds the unmodified
  template and the profiles by name.
* **Releases and CI:** the release also ships an ESP32-C2 image (40 MHz crystal); a manual run of
  the Release workflow rebuilds the current version and refreshes its files. Every CI run keeps
  the Pico `.uf2` and ESP32 `.bin` it built as downloadable artifacts (14 days).

## 1.7.0 — device drivers, optional LED strips, console games

* **Releases with ready-to-flash firmware:** `.github/workflows/release.yml` publishes a GitHub release
  for every new `MCS_VERSION_STRING` — `.uf2` for Pico / Pico 2 / RP2040-Zero, single-image `.bin`
  for ESP32 / -S3 / -C3 / -C6, the Linux `mcs` binary and the Arduino library zip.
* **Project:** README demo (`assets/demo.gif`, recorded by `tools/make_demo_gif.js`), social preview
  image (`assets/social-preview.png`), issue and pull request templates, `SECURITY.md`,
  `CODE_OF_CONDUCT.md`.
* **Studio templates:** four playable console games under *Fun and games* — Guess the number,
  Rock, paper, scissors, Tic-tac-toe (against a simple computer player) and Hangman.
* **Fix:** declaring a type again as an `enum` (or with a different kind) crashed the compiler; it
  is now the error `the type 'X' is already defined` (partial classes/structs/interfaces still merge).
* **Device drivers** (`include/mcs_driver.h`, `modules/drivers/`, [docs/DRIVERS.md](docs/DRIVERS.md)):
  a driver = a C# front end + a per-MCU backend (`ops` table + context), kept in a small registry
  (`mcs_driver_register` / `_register_default` / `_unregister` / `_find`). Firmware can add its own
  drivers or replace a built-in backend without editing MicroCS; C# `Drivers.Has(name)`,
  `Drivers.List`, and `Hal.Has` also answers for driver and class names. The template port shows a
  `ws2812` backend and a complete custom driver (`Buzzer`).
* **`LedStrip` is now the built-in `ws2812` driver and optional:** `MCS_ENABLE_WS2812` (CMake
  `-DMICROCS_WS2812=OFF` — on RP2 it also drops `hardware_pio`; ESP-IDF menuconfig and Zephyr
  `CONFIG_MICROCS_WS2812`; `make_arduino.py --no-ws2812`; `microcs.mk` `MICROCS_WS2812 := 0`).
  The unreleased `mcs_hal_t.ledstrip_write` hook is gone (the HAL API stays at v2); backends
  register `MCS_WS2812_DRIVER(&ops, ctx)`. `include/mcs_ledstrip_drivers.h` is now
  `include/mcs_ws2812_backends.h` (`mcs_ws2812_pio_write`, `mcs_ws2812_rmt_write`).
* **Filesystem sources are looked up, not downloaded behind your back:** `cmake/MicroCSFS.cmake`
  uses `MICROCS_LITTLEFS_DIR` / `MICROCS_YAFFS2_DIR` (CMake or environment), then a copy next to the
  project or MicroCS (`third_party/`, `lib/`, `external/`, `Middlewares/Third_Party/`, siblings, the
  west workspace, `make fetch-lfs` output), then an earlier download; it downloads only with
  `-DMICROCS_FS_DOWNLOAD=ON` / `MICROCS_FS_DOWNLOAD=1`, else stops with a message saying what to
  set. LittleFS must be v2.x. The Pico example now needs one of those for its default LittleFS.
* **Arduino:** `tools/make_arduino.py --fs littlefs|yaffs2 [--fs-dir DIR]` bundles a flash
  filesystem into the library (for `mcs_flashfs_mount` on any `mcs_flash_t`), `--define NAME=VALUE`
  sets build options in the packaged `mcs_config.h`.
* **Scheduler jobs run while a script sleeps:** `Thread.Sleep` in a main loop now runs due
  jobs (Scheduler.Every/After delegates and jobs.cfg scripts); before, a looping `/main.cs`
  starved every job. Nested script jobs keep the outer script's top-level variables intact.
  `startup /main.cs` in jobs.cfg no longer runs main twice.
* **No-space handling:** `File.WriteAllText` / `WriteAllBytes` / `AppendAllText` / `Copy` check
  free space first and throw `IOException` without touching the old file (it used to be
  truncated); the shell's `put` answers `ERR not enough space: N bytes, M free` before the data
  is sent; Studio refuses uploads / saves that do not fit.
* **Studio Reset / reconnect:** Reset reboots through the firmware (`Hal.Reset`, works on RP2040,
  STM32 and native-USB ESP32 boards without reset wiring), falling back to the RTS/DTR pulse,
  and stops a running script first. After a reset or unplug Studio keeps retrying to reopen the
  port for a minute instead of giving up after the first failed open, and tells you when
  connecting stopped a running script (e.g. a looping `/main.cs`).
* **Fix: `Console.ReadLine` on devices** — scripts run by the shell / `mcs_runtime` now read the
  line typed in the console (`mcs_shell_readline`: input queue, backspace, CR LF, echo). Before,
  it fell back to `fgets(stdin)`, which never saw the Studio's input and blocked Ctrl-C / Stop.
  Ctrl-C while waiting stops the script; the wait honours the time limit and idle callback.
* **Studio:** a console line typed while a script runs goes to the script (`Console.ReadLine`)
  instead of waiting for the script to end; Refresh / Run re-read open, unmodified files that
  changed on the device (e.g. a log the script appended to); double-click on a template = Add.
* `List<int>.Sort()` / `Array.Sort(int[])` sort in place without extra memory (was 16 bytes per
  element); the *CPU benchmark* template sizes fit a 160 KB heap (RP2040).
* **`LedStrip` — WS2812 / NeoPixel / SK6812 on any pin** (the `ws2812` driver, see *Device drivers* above): `new LedStrip(pin, count[, order])`, indexer, `SetPixel`, `Fill`, `Clear`, `Show`,
  `Brightness`, `LedStrip.Rgb` / `LedStrip.Hsv`, GRB / RGB / GRBW orders. Drivers: RP2040/RP2350 PIO,
  ESP32 RMT (all chips with RMT), STM32 cycle-timed bit-bang, Zephyr `led_strip` (alias `led-strip`),
  Arduino (ESP32 RMT, Arduino-Pico PIO, Adafruit_NeoPixel), simulator log. Pin name `"NEOPIXEL"`
  maps to the on-board RGB LED (RP2040-Zero GP16, ESP32-S3 GPIO48, C3/C6 GPIO8).
  Studio template *WS2812 / NeoPixel LED strip* now uses it; the SPI version stays as
  *WS2812 LED strip via SPI*.
* **Fix:** indexers (`this[int]`) on native classes whose members are registered lazily from ROM.

* **Studio template: WS2812 / NeoPixel LED strip** (Displays) — addressable RGB LEDs driven through
  SPI MOSI at 2.4 MHz (3 SPI bits per LED bit, timing done by the SPI hardware): colour wipe,
  rainbow, theater chase, brightness limit; up to 28 LEDs per `SPI.Write` (more with a larger
  `MCS_HAL_MAX_XFER`).

* **Files on internal flash in every port:** `mcs_flashfs_mount()` puts LittleFS or YAFFS2 on
  any `mcs_flash_t` in one call (formats a blank partition). New internal-flash drivers:
  `mcs_rp2_flash_init()` (RP2040/RP2350 QSPI flash via `flash_safe_execute`),
  `mcs_stm32_flash_init()` (every STM32 family: page and sector flash, ECC flash words,
  L0/L1 inverted erase), `mcs_esp32_partition_flash()` + `mcs_esp32_flash_fs()` and
  `mcs_zephyr_flash_area_init()`. The RP2 and STM32 examples now keep `/boot.cs`, `/main.cs` and
  uploads on flash. Build choice: CMake `-DMICROCS_FS=littlefs|yaffs2` (`cmake/MicroCSFS.cmake`
  downloads the sources), ESP-IDF menuconfig *Filesystem on the "storage" partition*, Zephyr
  `overlay-yaffs2.conf`. `mcs_flash_t.write_size` (program unit) sets LittleFS's `prog_size`.
  Tested on simulated L4-, H7- and RP2-like internal flash; CI builds Pico 2, ESP32-S3 and Zephyr
  with YAFFS2 and runs `native_sim` with both filesystems.
* **Byte utilities in the core library** (no HAL needed, `MCS_ENABLE_BYTES`): `Convert.ToBase64String`
  / `FromBase64String` / `ToHexString` / `ToHexStringLower` / `FromHexString`, `BinaryPrimitives`
  (`Read/Write{Int16,UInt16,Int32,UInt32,Int64}{Big,Little}Endian`), `BitConverter.ToString(bytes,
  start, length)`. `Encoding.UTF8/ASCII` and `BitConverter` moved from the HAL module into the core,
  so they work on every build (off in the `min` profile). Test `t18_utils` is checked against .NET 8.
* README: performance chart legend says "MicroCS 1.6"; footprint chart label fixed; status and
  roadmap updated (internal-flash filesystems done; USB mass-storage is a separate roadmap item).

* **Zephyr port completed:** files on LittleFS (`mcs_zephyr_fs_mount()` on `storage_partition`,
  or any mounted Zephyr filesystem through `mcs_zephyr_fs_ops`, e.g. FAT on SD), so `/boot.cs`,
  `/jobs.cfg`, `/main.cs` and Studio uploads persist; I2S (`mcs-i2sN` aliases, memory-slab
  streaming), two CAN buses (`mcs-can0/1`), hardware RTC (`rtc` alias), watchdog fallbacks
  (`wdt0`, `wdt`, `iwdg`), polled UART fallback for drivers without interrupts, USB CDC ACM
  console (`overlay-usb.conf` + `usb.overlay`), Kconfig options (`MICROCS_HEAP_SIZE`,
  `MICROCS_FS`, `MICROCS_RAMFS_SIZE`, `MICROCS_CONSOLE_ECHO`), board overlays for the nRF52840
  DK and the Pico, a west manifest (`ports/zephyr/west.yml`). Fixed: the example's
  `app.overlay` did not build (missing PWM include), `clock()` link error with newlib, `native_sim`
  profile detection (`CONFIG_SRAM_SIZE=0`), the host-only POSIX backend compiled into Zephyr.
  CI builds `native_sim`, nRF52840 DK (UART + USB), Pico and Nucleo-F429ZI with Zephyr 4.1 and
  runs `tests/zephyr/smoke.py` on `native_sim`.
* **Arduino port completed:** files on any `fs::FS` (ESP32 LittleFS / SPIFFS / FFat / SD /
  SD_MMC, RP2040 LittleFS / SDFS) or the Arduino SD library (`MCS_ARDUINO_FS`,
  `MCS_ARDUINO_SD_FS`, `mcs_arduino_fs_ops`); `MicroCS_REPL` is now a full device firmware
  (LittleFS, boot scripts, Studio), new `MicroCS_SD` example; ESP32 I2S, CAN (TWAI), task
  watchdog and calibrated `ADC.ReadMillivolts`; RP2040 I2S and watchdog; 12/16-bit PWM duty on
  ESP32 / RP2040 / Teensy; `Hal.UniqueId` on RP2040, nRF52, SAMD, STM32. CI compiles every
  example for ESP32, S3, C3, Pico, Pico 2 and Nano 33 BLE.
* **Jobs:** shell `cancel all|scripts|files`, REPL `.cancel`, C `mcs_sched_cancel_all()`;
  Studio stops the jobs earlier scripts left running before each Run (option) and has ⋯ →
  *Stop all jobs*. Docs explain that `Scheduler.Every` jobs outlive their script and file.
* **MicroCS Studio** (`tools/studio/`, online at https://amir1387aht.github.io/MicroCS/): a
  browser IDE over Web Serial — device file manager (upload/download/rename/delete, drag & drop,
  free space), C# editor with Visual Studio 2022 Dark colours, API completion and 40+ snippets
  that saves and runs on the device, 85 templates and examples (boot scripts, `jobs.cfg`,
  scheduler patterns, every HAL peripheral, files, C# features) and a REPL / shell console.
  Connecting recovers an ESP32 left in its ROM bootloader by the port's DTR/RTS lines. Tested
  in CI against the host shell (`mcs --repl --echo`, new `--echo` flag); every template is
  compiled and run on the simulated board.
* **Studio: connecting no longer resets the board.** Studio used to set DTR/RTS low right after
  opening the port; Chrome applies the two lines one after the other, which briefly gives
  DTR=0/RTS=1 - "hold EN low" on ESP32 auto-reset circuits and on the native USB-Serial-JTAG -
  so the board rebooted on every connect, and Studio then also reset it again when it did not
  answer at once. Now the lines are left as the browser opens them (like `mcs_remote.py`), a
  booting board gets up to 14 s (its boot log is shown live), only a board in the ROM download
  mode is reset (esptool order, one line per call), read errors are reported and survived, and
  the error message says what was received. Optional: ⋯ → *Release DTR/RTS on connect*.
* **Studio IntelliSense:** signatures and docs for the whole library (`docs.js`) and a small
  C# language service (`lang.js`): member lists with return types and a doc panel, parameter
  info while typing a call (active parameter, overloads with ↑/↓, hints such as
  `GPIO.Input, Output…`), hover tooltips, `using` namespace completion, type inference for
  `var x = new T()`, method return types (`I2C.Scan(0)` → `List<int>`), `foreach` variables,
  and the classes, fields, methods and local functions of the open file; camel-hump matching.
* **Studio editor:** find / replace (Ctrl+F / Ctrl+H, case / word / regex, F3), go to line
  (Ctrl+G), format document (Shift+Alt+F), move / copy lines (Alt+↑↓, Shift+Alt+↑↓), delete line
  (Ctrl+Shift+K), bracket matching and Ctrl+] jump; unsaved files survive a reload.
* **Studio serial plotter:** Console → *Plotter* charts the numbers a script prints
  (`temp:21.5 hum:40` or `12 34`), up to 8 series, click to pause.
* **Studio templates:** 125 (was 85) - new categories Sensors (MPU6050, DS3231, ADS1115, INA219,
  AHT20/SHT31, DHT22, BH1750, joystick), Displays (MAX7219, LCD1602, TM1637, OLED text),
  Motors and actuators (H-bridge, steppers, relay, smooth servo), Input devices (rotary encoder,
  keypad, button gestures, touch), Control and filters (PID, moving average / median / EMA,
  hysteresis, Kalman), Protocols and data (CRC-8/16/32, Modbus RTU, NMEA, Base64, command shell,
  JSON, ring buffer), Serial plotter demos, Fun and games, Benchmarks.
* Docs: the I2S direction constants are `I2S.Transmit` / `Receive` / `Duplex` (HAL.md said `Tx`/`Rx`).
* **ESP32: scripts on flash** — the example mounts LittleFS on the `storage` partition
  (`mcs_esp32_littlefs()`), so uploaded files survive resets and power cycles.
* **Free space**: C# `DriveInfo` (`new DriveInfo("/").AvailableFreeSpace`, `TotalSize`,
  `DriveFormat`, `DriveInfo.GetDrives()`), shell/`mcs_remote.py` command `df`, and
  `mcs_vfs_statfs()` with an optional `statfs` backend op (RAM, POSIX, LittleFS, YAFFS2,
  ESP32 LittleFS).

## 1.6.0 — fast bytecode images

Focus: a precompiled image is now clearly the fast path. Up to 1.5 an image held the same
bytecode the on-device compiler emits, so it only saved the compile step (a few ms) and was
larger than the source. Numbers: [PERFORMANCE.md](docs/PERFORMANCE.md#bytecode-images).

### Speed (emulated Cortex-M4F instructions, image run, 1.5 → 1.6)
- `fib` 2.43 → 1.43 M (1.7×), `loop` 15.0 → 6.70 M (2.2×), `objects` 5.64 → 3.19 M (1.8×),
  `sensor` 0.87 → 0.68 M (1.3×), `strings` 0.63 → 0.49 M (1.3×); `demo.cs` image 3.44 → 2.18 M
  on M4F and 4.54 → 2.96 M on M0. Host: fib(30) 73 → 41 ms, 10 M loop 322 → 95 ms.
- Images now run 1.1–2.0× faster than the same script from source and need about half the RAM
  peak (no compiler state): e.g. `fib` 31.4 → 15.1 KB, `sensor` 40.7 → 17.0 KB.

### Bytecode optimizer (`src/mcs_opt.c`)
- 71 **superinstructions** (opcodes 86–156, image version 2+): local/immediate/constant
  arithmetic with optional store, compare-and-branch on locals/immediates/constants,
  accumulate (`x += e`), `a[i]` / `a[i] = v` and `obj.f` / `obj.f = v` on locals,
  `return local`. Jump threading, loop rotation (one branch per iteration), dead-code removal.
- `mcs -c` / `mcs -C` / `mcs_compile_image*` optimize by default; `mcs -O0` and
  `MCS_IMAGE_NO_OPT` produce plain bytecode. Source run on the device is unchanged unless
  `MCS_OPTIMIZE_SOURCE=1`.
- New switches `MCS_ENABLE_SUPEROPS` (default 1; **0 in `mcs_profile_min.h`**, which also turns
  `MCS_FIELD_CACHE` off) and
  `MCS_ENABLE_OPTIMIZER`. A VM without superinstructions rejects optimized images with a clear
  error; `examples/lowram/node_image.h` is now built with `-O0` so it runs on every profile.

### VM
- Stack pointer kept in a register; in-place int fast paths for arithmetic, shifts, compares,
  `a[i]`, `a[i] = v`; `array.Length` / `List.Count` / `string.Length` without lookup.
- `INVOKE` method inline cache per call site, constructor cache for `new C(...)`, direct
  closure entry, cheaper `RETURN`; caches are invalidated by a class epoch.

### Image format v3
- Varints, delta-coded line tables, every string stored once, f32 constants when exact.
  Images are 25–45 % smaller (host `demo.cs` 2763 → 1942 B; `bench/mcu/loop.cs` image 166 B for
  232 B of source). v1 and v2 images still load.

### Fixes
- Typed top-level variables used inside functions declared before them had the wrong type
  (`double ema = 0; void F() { ema = ema * 0.5; }` did integer arithmetic).
- Calls of hoisted top-level functions keep the declared return type (no extra conversion).
- `foreach (var p in list) acc = …` no longer emitted a stray tuple-names call.
- An interpolation format spec longer than 95 characters overflowed a compiler buffer (found by
  `tools/fuzz.py`; present since 1.0).
- Image loader: `FIELD`'s default-value operand is range-checked (found by `tools/fuzz.py --image`).
- CLI builds with `MCS_ENABLE_COMPILER=0` / `MCS_ENABLE_BYTECODE_SAVE=0` compile warning-free.

### Tooling
- `make mcu-bench` (`tools/mcu_bench.sh`, `bench/mcu/*.cs`, `ports/cortex-m/bench.c`):
  image vs `-O0` image vs source on emulated M4F/M0, outputs checked against the host.
- `make check`: +3 flag builds and +3 full-suite configurations (`MCS_ENABLE_SUPEROPS=0`,
  `MCS_OPTIMIZE_SOURCE=1`, switch dispatch); new `tests/t17_optimizer.cs`.

### Costs
- Flash: +8–11 KB on Cortex-M with superinstructions (`m0-runtime` 208.5 KB, `m4-full`
  253.1 KB); the `min` build (superinstructions and inline caches off) stays at 60.6 KB of 64 KB.
- Compiling an image on the PC takes longer (optimizer): host `demo.cs` 83 → 120 µs.

## 1.5.0 — 16 KB RAM / 64 KB flash, per-MCU configuration, flash filesystems

Focus: run on the smallest 32-bit parts (the same 16 KB RAM floor as MicroPython, but in
64 KB of flash), configure every port from the chip's resources, and put real filesystems
on external NOR and NAND flash. Nothing was removed: every reduction is a compile-time switch.

### Small targets
- **Lazy class tables** (`MCS_LAZY_CLASSES=1`, default): built-in and module classes are created
  the first time they are named; constants live in ROM (`mcs_const_t`, `MCS_CONST`,
  `mcs_register_consts`). A VM now starts in **1.7–8 KB** of heap instead of 13–25 KB
  (Cortex-M0: `m0-node` 13.0 → 1.7 KB, `m0-lowram` 19.9 → 5.0 KB, `m0-runtime` 25.1 → 7.8 KB).
- **`profiles/mcs_profile_min.h`**: a complete firmware in **64 KB of flash and 16 KB of RAM**
  (`m0-64k`: 60.9 KB, run by `make cm-check`). New switches, all default on:
  `MCS_ENABLE_STRING_EXTRA`, `MCS_ENABLE_ARRAY_EXTRA`, `MCS_ENABLE_STACK_QUEUE`,
  `MCS_ENABLE_CONVERT`, `MCS_ENABLE_DIAGNOSTICS`, `MCS_ENABLE_STDIO`, `MCS_ENABLE_MALLOC`;
  `MCS_TINY_PRINTF=1` uses the new built-in `snprintf` (`src/mcs_fmt.c`) instead of libc's.
  Flash cost of each switch: [LOW_RESOURCE.md](docs/LOW_RESOURCE.md#8a-fitting-64-kb-of-flash-profilesmcs_profile_minh).
- `examples/lowram` runs in a **16 KB** part (`m0-16k`: 12 KB pool, 11.2 KB peak); the firmware
  no longer needs `printf` and can report its C-stack high-water mark.
- `lowram` profile: 128 stack slots, 24 frames, 12 handlers, 16 roots.
- All profile headers are overridable (`#ifndef` guards), so `-D` flags win over a profile.
- `cfg.alloc_overhead` (+ `MCS_POOL_OVERHEAD`): the GC threshold and `heap_limit` count the
  allocator's per-block headers (set automatically for the built-in pool).

### Per-MCU configuration
- **`profiles/mcs_profile_auto.h`** picks min / tiny / lowram / mcu / embedded / default from
  `MCS_TARGET_RAM_KB` / `MCS_TARGET_FLASH_KB` (CMake `MICROCS_RAM_KB` / `MICROCS_FLASH_KB`),
  Zephyr `CONFIG_SRAM_SIZE` / `CONFIG_FLASH_SIZE`, or the STM32 / RP2040 / RP2350 / nRF52 /
  SAMD device macro. Board ports (`MCS_PORT_HAL=1`) keep the HAL classes on parts with
  ≥ 128 KB of flash. Default for CMake `MICROCS_PORT=stm32` and for Zephyr (new
  `MICROCS_PROFILE_*` Kconfig choice); ESP-IDF gets a menuconfig choice.
- **STM32 parts below 16 KB RAM or 64 KB flash are no longer supported**: their device macros
  (C011/C031/C051, F030x4–x8, F031, F038, F042, F048, F051, F058, F070x6, F100, F101x4–xB,
  F102, F103x4/x6, F301x6, F302x6, F303x6/x8, F328, F334, G030/G031/G041, L010x4–x8,
  L011–L063, L100/L151/L152 small variants, U031) stop the build
  (`include/profiles/mcs_target_stm32.h`; `MCS_ALLOW_SMALL_TARGET=1` overrides).
- CI compiles every STM32 family twice (default and auto profile, `PORT_CFLAGS`).

### ESP32-C2
- ESP32-C2 / ESP8684 support: embedded profile by default (`sdkconfig.defaults.esp32c2`),
  ROM nano-printf disabled (with a `#warning` if it is re-enabled with floats), LEDC clock
  from the 60 MHz PLL divider, hardware timer count clamped to the chip, adaptive VM heap in
  the example (128 KB on the C2, clamped to the largest free block), C2 pin map, 26 MHz
  crystal note. Built in CI.

### Flash filesystems (`modules/fs`)
- **`include/mcs_flash.h`** (`MCS_ENABLE_FLASH`): a NOR/NAND device description plus generic
  **SPI NOR** (JEDEC id, 3-/4-byte addressing, 4/64 KB erase, power-up unlock) and **SPI NAND**
  (W25N-style command set, on-die ECC status, bad-block markers, cached page) drivers, and
  `mcs_flash_hal_xfer` to run them over a MicroCS HAL SPI bus + GPIO chip select. ~2.6 KB.
- **LittleFS** on NOR and NAND: `mcs_lfs_flash_config()` fills an `lfs_config` for a
  partition; NAND bad blocks → `LFS_ERR_CORRUPT` (relocation), failing blocks get marked.
- **YAFFS2** backend `mcs_yaffs_ops` (`MCS_ENABLE_YAFFS`) + `mcs_yaffs_flash_dev()`: in-band
  tags on NOR and (by default) NAND, spare-area tags optional, full bad-block handling;
  optional single-threaded OS glue (`MCS_YAFFS_OSGLUE=1`). YAFFS2 is GPLv2 — not bundled.
- Tests on simulated chips with real command sets (`tests/c/flash_sim.h`): `make test`
  (drivers, 42 checks), `make lfs-test` (now also SPI NOR + SPI NAND with bad blocks),
  new `make yaffs-test` (downloads a pinned yaffs2 revision; NAND in-band + spare tags with a
  block wearing out, NOR). CI job "Flash filesystems".

### Fixes
- GC: the string intern table no longer doubles while dead strings are waiting for the next
  collection (it grows from the live count and defers growth near a collection), and it
  shrinks after a collection; this removed the collection storm on `m0-lowram`
  (now 3 collections) and made the 16 KB targets possible.
- GC: when the live data was close to ¾ of `heap_limit`, the next collection threshold
  landed at or below the bytes already allocated and the VM collected at every safepoint;
  it is now placed halfway to the limit. Allocator block headers were not counted, so a
  small pool could run out before the GC threshold was reached (`cfg.alloc_overhead`).
- Images: constant tables are allocated at their exact size when loading.
- `{0:F<huge>}` format precision could overflow an `int`; exponent formatting no longer uses
  `sprintf` (and cannot truncate).
- The compiler treated not-yet-created built-in classes as unknown names / interfaces.

### Docs
- New/updated: LOW_RESOURCE.md (16 KB / 64 KB, switch costs, auto profile), FILESYSTEM.md
  (flash layer, LittleFS vs YAFFS2), STM32 supported-parts list, ESP32-C2 section, footprint
  tables re-measured.

## 1.4.0 — hardware, ports and the REPL

Focus: run MicroCS on real boards with every peripheral, from any build system — either as a
library inside existing firmware or as a complete C# firmware with a REPL.

### Hardware API v2 (`modules/hal`)
- New classes: `Pin`, `I2cDevice`, `SpiDevice`, `DAC`, `Timer`, `I2S`, `QSPI`, `CAN` + `CanFrame`,
  `Watchdog`, `RTC`; many new members (`GPIO.OnChange/Off/PulseIn/Pin`, `UART.Open(port, baud,
  bits, parity, stop)`, `ReadLine`, `WriteLine`, `OnReceive`, `I2C.Scan/ReadRegister(s)/WriteRegister`,
  `SPI.Open(bus, hz, mode)`, `ADC.ReadMillivolts/ReadVoltage/ReadAverage`, `PWM.Servo/Tone/SetPulse/Stop`,
  `Hal.Poll/Run/Micros/DelayMicroseconds/Reset/UniqueId/CpuHz/OnEvent/Post/DroppedEvents`).
- Interrupts and timers: ISR-safe event ring (`mcs_hal_post`) or driver queue (`poll_event`);
  callbacks run in script context and adapt to their parameter count.
- Pin names (`"PA5"`, `"GPIO21"`, `"P0.13"`, `"LED"`) via `pin_lookup` / `mcs_hal_parse_pin`.
- `Encoding.UTF8`/`ASCII` and `BitConverter` helpers for packet work.
- Fixed: `GPIO.Write(pin, 0)` drove the pin high (an `int` 0 was treated as truthy).
- The simulator board gained interrupts, timers, DAC, I²S, QSPI flash, CAN and an MPU-6050-style
  register device. v1 board tables compile unchanged.

### Ports
- `ports/stm32` — every STM32Cube family (C0, F0–F7, G0, G4, H5, H7, L0–L5, U5, WB, WL), using
  CubeMX handles; compile-checked against 21 family/device combinations.
- `ports/esp32` — ESP-IDF 5.x for ESP32, S2, S3, C2, C3, C5, C6, H2, P4 (no Wi-Fi/BLE needed).
- `ports/rp2` — RP2040 and RP2350 with the pico-sdk; example firmware for Pico and Pico 2.
- `ports/zephyr` — devicetree-driven port + Zephyr module (beta).
- `ports/arduino` — any 32-bit Arduino core, Arduino library packager (beta).
- `ports/template` rewritten: every HAL member, both entry points.

### Firmware runtime and REPL
- `mcs_runtime` (`modules/runtime`): a complete firmware in one call — REPL, filesystem, boot
  scripts, scheduler, callbacks, upload protocol; or `mcs_runtime_start/step` from your own loop.
- Shell REPL: multi-line input, expression printing, Ctrl-C, Ctrl-E paste mode, Ctrl-A machine
  mode, dot commands. CLI: `mcs --repl`; `tools/mcs_remote.py repl` terminal.
- Core: `mcs_arity`, `mcs_ticks`, `mcs_sleep` (dispatches callbacks while sleeping),
  `mcs_safepoint`, `mcs_set_idle`; `Thread.Sleep` is abortable.

### Build integration
- `CMakeLists.txt` (plain CMake, pico-sdk, CubeMX CMake, ESP-IDF component), `microcs.mk`,
  `idf_component.yml`, `library.json` (PlatformIO), `zephyr/` module, `include/MicroCS.h`.
- CI: CMake host build, Pico/Pico 2 firmware, ESP-IDF builds (ESP32/S3/C3/C6), STM32 port checks.

### Docs and examples
- New README (two ways to use MicroCS, build-system matrix, ports, comparison with MicroPython and
  nanoFramework), rewritten HAL, PORTING and STANDALONE guides, a README per port.
- 16 commented hardware examples (`examples/hardware/`) run by `make test`; `examples/quickstart_embed.c`.

## 1.3.0 — small-MCU release

Focus: RAM and flash on weak microcontrollers, without changing script behaviour.
Guide: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md).

### Memory & speed (all measured, emulated Cortex-M unless noted)
- **`MCS_COMPACT_VALUES`** (default on for 32-bit targets): values packed to 4-byte alignment,
  12 instead of 16 bytes with double/int64 payloads. On Cortex-M0 + newlib-nano this removed a
  `memcpy` call per value copy: the demo image runs in **4.56 M instead of 18.2 M instructions**.
- **Execute-in-place images**: `mcs_exec_image_xip()` runs bytecode from the image buffer
  (flash) instead of copying it to the heap; `mcs --xip`; `MCS_ENABLE_XIP`.
- VM baseline after `mcs_new` on Cortex-M: **50 136 B → 25 688 B** (M0, full stdlib). Interned-string
  set stores keys only, a global's slot lives in its name string (no global index table),
  built-in exception classes share one field layout, hash tables start at `MCS_TABLE_MIN_CAP` (4).
- **Compact `Dictionary`/`HashSet` index**: 1-, 2- or 4-byte position slots instead of full
  key/value entries; `HashSet` stores no values array; `Remove` no longer rehashes. Host:
  700-string `HashSet` 104 → 58 KB, 100-entry `Dictionary<int,int>` 12.1 → 4.3 KB.
- Compiler RAM: smaller AST nodes and tokens (24 bytes on 32-bit), token array grows by the
  observed density instead of doubling. On-device compile + run of the demo on the M4:
  pool peak **131 528 B → 90 480 B**, compile instructions −12 %.
- Lexer keyword lookup by length + first character, cheaper primitive-type check.
- Host (`make bench`, same machine, same session as 1.2.0): run times unchanged, `mcs_new`
  43 → 30 µs, demo compile 201 → 140 µs, demo heap peak 116 438 → 87 510 B.

### New options and profiles
- `profiles/mcs_profile_lowram.h` for 32–64 KB RAM parts (images only, single-precision
  floats → 8-byte values, small VM limits, 4 KB first GC, 4-byte pool alignment).
- `MCS_ENABLE_LINQ` (LINQ operators + `Enumerable`, ~12 KB flash on M0), `MCS_POOL_ALIGN`,
  `MCS_TABLE_MIN_CAP`, `MCS_ERROR_SIZE`, `MCS_MAX_PINS`, `MCS_COMPACT_VALUES`, `MCS_ENABLE_XIP`.
- Cortex-M targets `m0-lowram` (64 KB RAM, 40 KB pool) and `m0-node` (the new
  `examples/lowram` firmware, 48 KB RAM, 32 KB pool) in `make cm-check`.
- `tools/cm_emu.py --profile N` (hottest functions) and `--callers SYMBOL`.

### Library
- LINQ operators on `Dictionary`, `HashSet`, `Stack` and `Queue` (`Where`, `Select`, `Skip`,
  `Take`, `Last`, `Average`, `GroupBy`, … — previously only lists/arrays had the full set).
- `HashSet`, `Stack` and `Queue` are accepted wherever a sequence is: `string.Join`,
  `string.Concat`, `new List<T>(…)`, `AddRange`, `Concat`, `Zip`, `SequenceEqual`, `new Stack/Queue(…)`
  (previously `string.Join(",", set)` printed the type name).
- `HashSet.IsSupersetOf`, `SetEquals`, `SymmetricExceptWith`.

### Fixes
- `base(msg)` into a lazily registered native constructor (e.g. `class E : IOException`) failed
  with "does not contain .ctor".
- `class X : IOException` was treated as an interface by the `I`+uppercase heuristic.

### Docs, examples, tests
- New: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md), [examples/lowram/](examples/lowram/).
- New test `t14_low_resource.cs` (exception subclasses of built-ins, globals, string churn,
  dictionary index widths, collection interop) — byte-identical to .NET 8.
- Every script test now also runs as an XIP image (46 runs); 104 C unit checks (XIP images,
  300 globals, intern churn, shared layouts, dictionary index); `make check` adds the lowram
  profile and five alternative configurations that run the whole suite.

## 1.2.0

### Language
- **Tuples**: literals `(1, "a")`, named elements `(x: 1, y: 2)`, tuple types `(int x, int y)` in
  locals, fields, parameters, `out` parameters, return types and generic arguments
  (`Dictionary<(int, int), string>`); `Item1..Item7`; structural `==`/`!=`, hashing and `ToString()`.
- Tuple element **name projection** (`var t = (a, b); t.a`) and names that survive later
  assignments, `out` parameters, tuple-typed fields/properties and object initializers.
- **Deconstruction**: `var (a, b) = t;`, `(int a, var b) = t;`, swaps `(a, b) = (b, a)` without
  allocation, `_` discards, `foreach (var (k, v) in dict)`.
- **Index from end and ranges**: `x[^1]`, `x[1..^1]`, `x[..3]`, `x[2..]` on strings, arrays and lists.
- **Patterns in `is`**: relational, `and`, `or`, `not` (`t is < 0 or > 100`); `and` also in switch
  arms and `case` labels.
- `sizeof(builtin)`, `checked(...)` / `unchecked(...)` expressions, `static` local functions with
  tuple return types; `.HasValue` / `.Value` / `.GetValueOrDefault()` on nullable values.
- Declarations used as embedded statements (`while (c) int i = 0;`) are now a compile error (as in C#).

### Library
- LINQ `Zip` (tuple and selector forms) and `Chunk`; `Stack.TryPop` / `TryPeek`,
  `Queue.TryDequeue` / `TryPeek`.
- Generated API reference `docs/STDLIB.md` (`tools/gen_stdlib_doc.py`).

### Performance
- Image format **v2** with superinstructions: `SET_LOCAL_POP`, `SET_GLOBAL_POP` and fused
  compare-and-branch `JF_EQ/NE/LT/LE/GT/GE`. v1 images still load.
- Per-site inline cache for field access (`MCS_FIELD_CACHE`), exact-arity call fast path,
  integer `/` `%` fast path.
- Token array freed right after parsing. Host: fib −20 %, loop −22 %; Cortex-M4 demo −17 %
  instructions; on-device compile peak 163 KB → 132 KB.

### Fixes
- Unbounded growth of the string intern table with many temporary strings (eventually OOM).
- Stack-slot leak when an unbraced loop/if body introduced locals (`out var`, deconstruction temps).
- Lexer: `1..3` is a range, not `1.` followed by `.3`.

### Hardening & tooling
- Image loader validates local/upvalue operands, closure captures and that every branch lands
  on an instruction boundary.
- `mcs -d file.mcsb` disassembles images (`mcs_disassemble_image`).
- `tools/fuzz.py` mutation fuzzer (source and `--image` modes), clean under ASan/UBSan;
  `make asan-test` runs the whole suite with sanitizers; GitHub Actions CI.
- `.NET 8` parity check extended to `t11`, `t13`, `examples/tour.cs`.
- New examples (`blink.cs`, `sensor_logger.cs`, `tour.cs`) run by `make test`.

## 1.1.0 — Phase 2
- Virtual filesystem (RAM, POSIX, LittleFS) with C# `File` / `Directory` / `Path`.
- HAL with C# `GPIO`, `UART`, `I2C`, `SPI`, `ADC`, `PWM` + simulator board.
- Job scheduler, standalone shell with UART upload protocol, execution limits.
- `ref` / `out` / `in`, `case` patterns with `when`.
- Cortex-M0/M4/M33 reference port verified in an emulator. Memory fixes in the compiler.

## 1.0.0 — Phase 1
- Lexer, parser, single-pass compiler, stack VM with computed goto, precise GC, stdlib,
  bytecode images, CLI/REPL.
