#!/usr/bin/env python3
"""Generate docs/STDLIB.md from the native registration tables.

The tables (`static const mcs_reg_t NAME[] = {...}`) are the single source of
truth for what scripts can call, so the reference cannot drift from the code.
usage: python3 tools/gen_stdlib_doc.py > docs/STDLIB.md
"""
import glob, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FILES = sorted(glob.glob(os.path.join(ROOT, "src", "*.c")) + glob.glob(os.path.join(ROOT, "modules", "*", "*.c")))

# table name -> (section, C# type, "instance" | "static", note)
MAP = {
    "console_fns": ("Core", "Console", "static", ""),
    "convert_fns": ("Core", "Convert", "static", ""),
    "math_fns": ("Core", "Math", "static", "`MCS_STDLIB_MATH`"),
    "env_fns": ("Core", "Environment", "static", "`Environment.NewLine` is a constant"),
    "thread_fns": ("Core", "Thread", "static", ""),
    "gc_fns": ("Core", "GC", "static", ""),
    "debug_fns": ("Core", "Debug", "static", ""),
    "object_methods": ("Core", "object", "instance", "every value"),
    "object_statics": ("Core", "object", "static", ""),
    "type_methods": ("Core", "Type", "instance", "`x.GetType()`, `typeof(T)`"),
    "type_statics": ("Core", "Type", "static", ""),
    "exc_methods": ("Core", "Exception", "instance", "all exception classes"),
    "value_methods": ("Core", "int / double / bool / char", "instance", ""),
    "int_statics": ("Core", "int (and long, byte, short, uint ...)", "static", ""),
    "dbl_statics": ("Core", "double / float", "static", "`MCS_ENABLE_FLOAT`"),
    "bool_statics": ("Core", "bool", "static", ""),
    "enc_fns": ("Core", "Encoding", "static", "`using System.Text;`"),
    "enc_members": ("Core", "Encoding.UTF8 / Encoding.ASCII", "instance", ""),
    "bitconv_fns": ("Core", "BitConverter", "static", "little-endian; `ToString` gives `01-AB-FF`"),
    "binprim_fns": ("Core", "BinaryPrimitives", "static", "`using System.Buffers.Binary;` optional last `offset` argument (MicroCS extension)"),
    "char_statics": ("Core", "char", "static", ""),
    "random_members": ("Core", "Random", "instance", "xorshift PRNG, seedable"),
    "sw_members": ("Core", "Stopwatch", "instance", "uses `cfg.ticks_fn`"),
    "sw_statics": ("Core", "Stopwatch", "static", ""),
    "delegate_methods": ("Core", "Delegate / Action / Func", "instance", ""),
    "kvp_methods": ("Collections", "KeyValuePair<K,V>", "instance", "`Key`, `Value` fields; deconstructible"),
    "tuple_methods": ("Core", "ValueTuple", "instance", "`Item1..Item7` and element names are fields"),
    "string_methods": ("Strings", "string", "instance", "UTF-8, byte-indexed"),
    "string_statics": ("Strings", "string", "static", ""),
    "sb_members": ("Strings", "StringBuilder", "instance", "`MCS_ENABLE_STRINGBUILDER`"),
    "array_methods": ("Collections", "T[] (arrays)", "instance", "plus every LINQ operator below"),
    "array_statics": ("Collections", "Array", "static", ""),
    "list_methods": ("Collections", "List<T>", "instance", "plus every LINQ operator below"),
    "enumerable_fns": ("Collections", "Enumerable", "static", ""),
    "dict_methods": ("Collections", "Dictionary<K,V>", "instance", "`MCS_ENABLE_DICT`; insertion-ordered; plus the LINQ operators below"),
    "set_methods": ("Collections", "HashSet<T>", "instance", "plus the LINQ operators below"),
    "stack_methods": ("Collections", "Stack<T>", "instance", ""),
    "queue_methods": ("Collections", "Queue<T>", "instance", ""),
    "coll_linq_methods": ("Collections", "Dictionary / HashSet / Stack / Queue", "instance", "LINQ operators, run on a snapshot (dictionaries yield `KeyValuePair`s); `MCS_ENABLE_LINQ`"),
    "file_fns": ("Filesystem (modules/fs)", "File", "static", "`mcs_fs_open_lib`"),
    "dir_fns": ("Filesystem (modules/fs)", "Directory", "static", ""),
    "path_fns": ("Filesystem (modules/fs)", "Path", "static", ""),
    "drive_members": ("Filesystem (modules/fs)", "DriveInfo", "instance", "`new DriveInfo(path)`; size of the mount holding `path`"),
    "drive_statics": ("Filesystem (modules/fs)", "DriveInfo", "static", ""),
    "power_fns": ("Hardware (modules/hal)", "Power", "static", "capability-gated hardware modes; [power management](POWER.md)"),
    "hal_fns": ("Hardware (modules/hal)", "Hal", "static", "consts `Board`, `ApiVersion`"),
    "gpio_fns": ("Hardware (modules/hal)", "GPIO", "static", "consts `Input Output InputPullUp InputPullDown OpenDrain Analog Rising Falling Both`, `PullDefault PullNone PullUp PullDown PullBoth`, `SpeedDefault SpeedLow SpeedMedium SpeedHigh SpeedVeryHigh`; `Configure(pin, GpioConfig)` ([settings](PERIPHERALS.md))"),
    "encoder_members": ("Hardware (modules/hal)", "RotaryEncoder", "instance", "`new RotaryEncoder(a, b[, steps = 4, button = -1, reverse = false])`; [quadrature encoders](ENCODER.md)"),
    "pin_members": ("Hardware (modules/hal)", "Pin", "instance", "`new Pin(pin[, mode or GpioConfig])`; pin = number or name (`\"PA5\"`, `\"GPIO21\"`, `\"LED\"`)"),
    "uart_fns": ("Hardware (modules/hal)", "UART", "static", "consts `ParityNone ParityOdd ParityEven`"),
    "i2c_fns": ("Hardware (modules/hal)", "I2C", "static", ""),
    "i2cdev_members": ("Hardware (modules/hal)", "I2cDevice", "instance", "`new I2cDevice(bus, address)`"),
    "spi_fns": ("Hardware (modules/hal)", "SPI", "static", ""),
    "spidev_members": ("Hardware (modules/hal)", "SpiDevice", "instance", "`new SpiDevice(bus, csPin, hz, mode)` or `(bus, csPin, SpiConfig)` — drives CS for you"),
    "adc_fns": ("Hardware (modules/hal)", "ADC", "static", "consts `Resolution`, `ReferenceMillivolts`, `AttenDefault Atten0dB Atten2_5dB Atten6dB Atten12dB`"),
    "dac_fns": ("Hardware (modules/hal)", "DAC", "static", "const `Resolution`"),
    "pwm_fns": ("Hardware (modules/hal)", "PWM", "static", "duty for `Set` is 0.0–1.0"),
    "timer_fns": ("Hardware (modules/hal)", "Timer", "static", "periods in microseconds"),
    "i2s_fns": ("Hardware (modules/hal)", "I2S", "static", "consts `Tx Rx Duplex`"),
    "qspi_fns": ("Hardware (modules/hal)", "QSPI", "static", "address `-1` = no address phase"),
    "can_members": ("Hardware (modules/hal)", "CanConfig", "instance", "native CAN settings; [CAN](CAN.md)"),
    "can_fns": ("Hardware (modules/hal)", "CAN", "static", "classic CAN / MCP2515; [setup and support](CAN.md)"),
    "canframe_members": ("Hardware (modules/hal)", "CanFrame", "instance", "`new CanFrame(id, data[, extended[, remote]])`"),
    "wdt_fns": ("Hardware (modules/hal)", "Watchdog", "static", ""),
    "rtc_fns": ("Hardware (modules/hal)", "RTC", "static", "Unix seconds"),
    "hal_cfg_fns": ("Hardware (modules/hal)", "Hal", "static", ""),
    "spi_async_fns": ("Hardware (modules/hal)", "SPI", "static", ""),
    "i2c_async_fns": ("Hardware (modules/hal)", "I2C", "static", ""),
    "uart_async_fns": ("Hardware (modules/hal)", "UART", "static", ""),
    "qspi_async_fns": ("Hardware (modules/hal)", "QSPI", "static", ""),
    "dmabuf_members": ("Hardware (modules/hal)", "DmaBuffer", "instance", "`new DmaBuffer(size or bytes)` — DMA-capable memory, indexer `buf[i]` ([DMA](DMA.md))"),
    "xfer_members": ("Hardware (modules/hal)", "DmaTransfer", "instance", "returned by every `…Async` call"),
    "dma_fns": ("Hardware (modules/hal)", "Dma", "static", "memory-to-memory DMA, transfers in flight"),
    "drivers_fns": ("Drivers (modules/drivers)", "Drivers", "static", "registered device drivers (`mcs_driver.h`); `List` = names"),
    "ledstrip_members": ("Drivers (modules/drivers)", "LedStrip", "instance", "`ws2812` driver: `new LedStrip(pin, count[, order])` — WS2812/SK6812 on any pin; indexer `strip[i]`"),
    "ledstrip_statics": ("Drivers (modules/drivers)", "LedStrip", "static", "consts `GRB RGB GRBW`; colours `0xRRGGBB`"),
    "servo_members": ("Drivers (modules/drivers)", "Servo", "instance", "`servo` driver: `new Servo(channel[, minUs, maxUs[, maxAngle]])` — hobby servo on a PWM channel; `MoveTo(angle, ms)` eases"),
    "g_members": ("Displays (modules/drivers, optional U8g2)", "U8g2", "instance", "`new U8g2(display[, bus/pins])` — frame-buffer graphics ([U8G2.md](U8G2.md))"),
    "g_statics": ("Displays (modules/drivers, optional U8g2)", "U8g2", "static", ""),
    "x_members": ("Displays (modules/drivers, optional U8g2)", "U8x8", "instance", "`new U8x8(display)` — 8×8 text without a frame buffer"),
    "x_statics": ("Displays (modules/drivers, optional U8g2)", "U8x8", "static", ""),
    "json_fns": ("Core", "Json", "static", "`Parse` → `Dictionary<string, object>` / `List<object>` / values; `Stringify(value[, indent[, includeHidden]])`; fields + computed getters, `TryParse`; also as `JsonSerializer` ([JSON.md](JSON.md))"),
    "net_fns": ("Network (modules/net)", "Net", "static", "const `ApiVersion`; `Has(feature)`: `Wifi`, `EspNow`, `Tls`, `HttpServer`, … ([NETWORK.md](NETWORK.md))"),
    "wifi_fns": ("Network (modules/net)", "Wifi", "static", "consts `ModeOff ModeStation ModeAccessPoint ModeStationAp`, statuses `Idle Connecting Connected NoSsid WrongPassword ConnectFailed ConnectionLost Disconnected Stopped`, `Auth*`, `Power*`, `Proto*`, `Band*`, `Event*`, `Reason*`"),
    "wnet_members": ("Network (modules/net)", "WifiNetwork", "instance", "returned by `Wifi.Scan()`"),
    "wcli_members": ("Network (modules/net)", "WifiClient", "instance", "returned by `Wifi.ApClients()`"),
    "espnow_fns": ("Network (modules/net)", "EspNow", "static", "const `MaxData`; MACs as `\"24:0A:C4:12:34:56\"`"),
    "enmsg_members": ("Network (modules/net)", "EspNowMessage", "instance", "returned by `EspNow.Receive()`"),
    "ntp_fns": ("Network (modules/net)", "Ntp", "static", "SNTP; `Timezone` = POSIX TZ string"),
    "mdns_fns": ("Network (modules/net)", "Mdns", "static", "ESP32 with the `espressif/mdns` component"),
    "ping_fns": ("Network (modules/net)", "Ping", "static", "round trip in ms, -1 = no answer"),
    "tcp_members": ("Network (modules/net)", "TcpClient", "instance", "`new TcpClient()` or `(host, port[, timeoutMs])`"),
    "lst_members": ("Network (modules/net)", "TcpListener", "instance", "`new TcpListener(port)` or `(address, port)`"),
    "udp_members": ("Network (modules/net)", "UdpClient", "instance", "`new UdpClient([port])`"),
    "dns_fns": ("Network (modules/net)", "Dns", "static", ""),
    "http_fns": ("Network (modules/net)", "Http", "static", "headers: `Dictionary<string, string>`, `\"Name: value\"` lines or `string[]`"),
    "resp_members": ("Network (modules/net)", "HttpResponse", "instance", "returned by every `Http` request"),
    "srv_members": ("Network (modules/net)", "HttpServer", "instance", "`new HttpServer([port])` or `(address, port)`; handlers return a string (HTML), `byte[]` or an object (JSON)"),
    "req_members": ("Network (modules/net)", "HttpRequest", "instance", "passed to every `HttpServer` route"),
    "ws_members": ("Network (modules/net)", "WebSocket", "instance", "`new WebSocket([url[, timeoutMs]])` client, or a server-side connection from `HttpServer.WebSocket`"),
    "dnss_members": ("Network (modules/net)", "DnsServer", "instance", "`new DnsServer([address])` — captive portals"),
    "ble_fns": ("Bluetooth (modules/ble)", "Ble", "static", "opt-in (`MCS_ENABLE_BLE`); `Has(feature)`, `Event*`, `Io*`, `Passkey*`, `Phy*` consts ([BLE.md](BLE.md))"),
    "adv_members": ("Bluetooth (modules/ble)", "BleAdvertisement", "instance", "`new BleAdvertisement([name])` — advertising data"),
    "adv_statics": ("Bluetooth (modules/ble)", "BleAdvertisement", "static", "beacon presets"),
    "dev_members": ("Bluetooth (modules/ble)", "BleDevice", "instance", "returned by `Ble.Scan` / `Ble.StartScan`"),
    "svc_members": ("Bluetooth (modules/ble)", "BleService", "instance", "`new BleService(uuid[, primary])` — local GATT service"),
    "chr_members": ("Bluetooth (modules/ble)", "BleCharacteristic", "instance", "returned by `BleService.AddCharacteristic`; `BleProperty.*` flags"),
    "dsc_members": ("Bluetooth (modules/ble)", "BleDescriptor", "instance", "returned by `BleCharacteristic.AddDescriptor`"),
    "conn_members": ("Bluetooth (modules/ble)", "BleConnection", "instance", "a link in either role (`Ble.Connect`, `Ble.OnConnect`)"),
    "rsvc_members": ("Bluetooth (modules/ble)", "BleRemoteService", "instance", "GATT client: `BleConnection.Services()`"),
    "rchr_members": ("Bluetooth (modules/ble)", "BleRemoteCharacteristic", "instance", "GATT client"),
    "rdsc_members": ("Bluetooth (modules/ble)", "BleRemoteDescriptor", "instance", "GATT client"),
    "uart_members": ("Bluetooth (modules/ble)", "BleUart", "instance", "`new BleUart([name])` — Nordic UART Service"),
    "uuid_fns": ("Bluetooth (modules/ble)", "BleUuid", "static", "UUID helpers"),
    "worker_members": ("Threads (modules/threads)", "Worker", "instance", "returned by `Thread.Run` / `RunOn` / `Every` ([THREADS.md](THREADS.md))"),
    "channel_members": ("Threads (modules/threads)", "Channel", "instance", "`new Channel(name[, capacity])` — messages between threads"),
    "sched_fns": ("Scheduler (modules/sched)", "Scheduler", "static", "`mcs_sched_open_lib`"),
}
HIDDEN = {"rt_fns", "gpio_props"}
ORDER = ["Core", "Strings", "Collections", "Filesystem (modules/fs)", "Hardware (modules/hal)", "Drivers (modules/drivers)", "Displays (modules/drivers, optional U8g2)", "Network (modules/net)", "Bluetooth (modules/ble)", "Threads (modules/threads)", "Scheduler (modules/sched)"]
ENTRY = re.compile(r'MCS_(FN|GET|SET)\(\s*"([^"]+)"\s*,\s*\w+\s*(?:,\s*(-?\d+))?\s*\)')

def strip_comments(s):
    return re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", s), flags=re.S)

src = {f: strip_comments(open(f, encoding="utf-8").read()) for f in FILES}
macros = {}
for s in src.values():
    for m in re.finditer(r"#define[ \t]+(\w+)(?:\([^)]*\))?[ \t]+((?:[^\n]*\\\n)*[^\n]*)", s):
        # first definition wins: in `#if FEATURE ... #else` pairs it is the feature-enabled one
        macros.setdefault(m.group(1), m.group(2).replace("\\\n", " "))
tables = {}
for s in src.values():
    for m in re.finditer(r"static const mcs_reg_t (\w+)\[\]\s*=\s*\{(.*?)MCS_REG_END", s, re.S):
        body = m.group(2)
        # X-macro lists: OPS(REG) where OPS is `X(Name, fn, arity) ...`
        body = re.sub(r"\b([A-Z][A-Z0-9_]+)\(([A-Z][A-Z0-9_]+)\)",
                      lambda k: " ".join('MCS_FN("%s", %s, %s),' % x for x in re.findall(r"X\((\w+),\s*(\w+),\s*(-?\d+)\)", macros.get(k.group(1), ""))) or k.group(0), body)
        for _ in range(3):  # expand helper macros (SEQ_METHODS, SEQ_FLOAT_REGS, ...)
            body = re.sub(r"\b([A-Z][A-Z0-9_]{3,})\b", lambda k: macros.get(k.group(1), k.group(1)) if not k.group(1).startswith("MCS_") else k.group(1), body)
        items = []
        for e in ENTRY.finditer(body):
            kind, name, ar = e.groups()
            items.append((name, kind, ar))
        tables[m.group(1)] = tables.get(m.group(1), []) + items

# Peripheral config tables are generated by CLASS(..., FIELDS). Expand their
# property lists too, so CanConfig and the older settings classes stay in sync.
for source in src.values():
    for match in re.finditer(r'CLASS\((\w+),\s*MCS__CFG_\w+,\s*"(\w+)",\s*(\w+)\)', source):
        prefix, cls, fields = match.groups()
        table = prefix + "_members"
        props = re.findall(r'X\(\w+,\s*\d+,\s*"([^"\n]+)"', macros.get(fields, ""))
        tables[table] = [(name, "GET", None) for name in props] + [("Set", "FN", "2"), ("Get", "FN", "1"), ("Reset", "FN", "0"), ("ToString", "FN", "0")]
        MAP.setdefault(table, ("Hardware (modules/hal)", cls, "instance", "peripheral settings object"))

# tables that add members to a type mapped earlier (Hal.Options, SPI.WriteAsync ...) join its row
_first = {}
for _t, _v in list(MAP.items()):
    _k = (_v[0], _v[1], _v[2])
    if _k not in _first: _first[_k] = _t; continue
    if _t in tables:
        tables[_first[_k]] = tables.get(_first[_k], []) + tables.pop(_t)
    MAP.pop(_t)
# Convert.ToBase64String & co. are registered from a second table (src/mcs_lib_bytes.c)
tables["convert_fns"] = tables.get("convert_fns", []) + tables.pop("convert_bytes_fns", [])
missing = [t for t in tables if t not in MAP and t not in HIDDEN]
if missing:
    sys.stderr.write("unmapped tables: %s\n" % ", ".join(missing))

def fmt(items):
    props, meths, seen = [], [], set()
    for name, kind, ar in items:
        if name.startswith("op_") or name.startswith(".") or name.startswith("__") or name in ("get_Item", "set_Item"): continue
        if kind in ("GET", "SET"):
            if name not in seen: props.append("`%s`" % name); seen.add(name)
        else:
            a = int(ar)
            sig = "%s(%s)" % (name, "…" if a < 0 else ", ".join("·" for _ in range(a)) if a else "")
            key = name + "/" + str(a)
            if key not in seen: meths.append("`%s`" % sig); seen.add(key)
    return props, meths

out = []
w = out.append
w("# Standard library reference")
w("")
w("> [!NOTE]")
w("> Generated by `python3 tools/gen_stdlib_doc.py` from the native registration tables in")
w("> `src/` and `modules/` — this is exactly what a script can call. `·` = one argument,")
w("> `…` = variable argument count (overloads are resolved at run time).")
w("")
w("Behaviour follows .NET wherever the member exists there. Deliberate differences are listed")
w("in [LANGUAGE.md](LANGUAGE.md#deliberate-microcs-behaviour-differs-from-net). Library groups can")
w("be removed per VM with `cfg.stdlib` and at build time with the `MCS_ENABLE_*` flags of")
w("`include/mcs_config.h` (set them in your `mcs_user_config.h`, see [CONFIGURATION.md](CONFIGURATION.md)).")
w("")
w("**Contents:** " + " · ".join("[%s](#%s)" % (s, re.sub(r"[^a-z0-9 -]", "", s.lower()).replace(" ", "-")) for s in ORDER))
w("")
total = 0
for sec in ORDER:
    w("## " + sec)
    w("")
    w("| Type | Kind | Properties | Methods | Notes |")
    w("|---|---|---|---|---|")
    for t, (s, ty, kind, note) in MAP.items():
        if s != sec or t not in tables: continue
        props, meths = fmt(tables[t])
        total += len(props) + len(meths)
        w("| **%s** | %s | %s | %s | %s |" % (ty, kind, " ".join(props) or "—", " ".join(meths) or "—", note))
    w("")
    if sec == "Collections":
        w("LINQ operators are *eager*: each returns a new `List<T>` (or a scalar) immediately.")
        w("They are available directly on arrays, `List<T>`, `Dictionary`, `HashSet`, `Stack`, `Queue` and")
        w("the results of other operators. Building with `MCS_ENABLE_LINQ=0` removes them (and `Enumerable`)")
        w("to save ~12 KB of flash; `List<T>` methods such as `Find`, `ForEach`, `Exists`, `ConvertAll` stay.")
        w("")
exc = sorted(set(re.findall(r'"([A-Za-z]*Exception)"', src[os.path.join(ROOT, "src", "mcs_lib.c")])) - {"InnerException"})
w("## Exceptions and enums")
w("")
w("Exception classes (members `Message`, `InnerException`, `ToString()`; user classes can derive")
w("from any of them; deep recursion raises a catchable `StackOverflowException`):")
w("")
w(" ".join("`%s`" % e for e in exc))
w("")
w("Enums: `StringSplitOptions` (`None`, `RemoveEmptyEntries`, `TrimEntries`), `StringComparison`")
w("(`Ordinal`, `OrdinalIgnoreCase`, `CurrentCulture`, `CurrentCultureIgnoreCase`, `InvariantCulture`,")
w("`InvariantCultureIgnoreCase`; culture variants compare ordinally). A trailing `StringComparison` is honored by")
w("`IndexOf` `LastIndexOf` `Contains` `StartsWith` `EndsWith` `Replace` `Equals` and `string.Equals` / `string.Compare`;")
w("its values are tagged ints, so `IndexOf(s, StringComparison.Ordinal)` is never taken for a start index.")
w("")
w("_%d members in %d tables._" % (total, len([t for t in tables if t in MAP])))
print("\n".join(out))
