# Wi-Fi & networking

MicroCS 1.13 brings the network to C# scripts: Wi-Fi (station, soft access point, both at
once), scanning, events, static IP, saved credentials, power and radio options, ESP-NOW,
SNTP time, mDNS, ping — plus `TcpClient`, `TcpListener`, `UdpClient`, `Dns`, an HTTP/1.1
client with HTTPS, and a JSON library.

```csharp
if (Wifi.Connect("MyNetwork", "secret-password"))
{
    var r = Http.Get("https://api.example.com/v1/status");
    var doc = Json.Parse(r.Body);
    Console.WriteLine($"{Wifi.Ip}: {r.StatusCode} {doc["state"]}");
}
```

| Where | What you get |
|---|---|
| **ESP32 family** (ESP-IDF port) — ESP32, S2, S3, C2, C3, C5, C6, C61 | everything: `Wifi`, `EspNow`, `Ntp` (SNTP), `Mdns`\*, `Ping`, sockets, `Http` + HTTPS |
| **PC** (`./mcs`, Linux / macOS) | sockets, `Dns`, `Http` on the real network, `Ntp` = system clock |
| **PC with `--sim`** | additionally a simulated Wi-Fi radio: networks, AP, ESP-NOW echo peer, mDNS, ping |
| other ports | `Json` only, unless the port registers a backend (see [Porting](#porting-a-network-backend)) |

\* `Mdns` needs the [`espressif/mdns`](https://components.espressif.com/components/espressif/mdns)
component in the project (`idf.py add-dependency espressif/mdns`); MicroCS detects it.

Check at run time with `Net.Has(...)`: `"Wifi"`, `"Wifi.AP"`, `"Wifi.Scan"`,
`"Wifi.Enterprise"`, `"EspNow"`, `"Mdns"`, `"Ping"`, `"Tls"` / `"Https"`, `"Sockets"`,
`"Http"`, `"Ntp"`, `"Ntp.Sntp"` (the same names work with `Hal.Has`). `Net.Backend` is
`"esp32"`, `"sim"` or `"none"`.

**Contents:** [Wi-Fi](#wi-fi) · [Events](#events) · [Access point](#soft-access-point) ·
[IP & options](#ip-configuration-and-radio-options) · [Saved networks](#saved-networks) ·
[ESP-NOW](#esp-now) · [Time](#ntp-time) · [mDNS & ping](#mdns-and-ping) ·
[TCP](#tcp) · [UDP](#udp) · [DNS](#dns) · [HTTP](#http-and-https) · [JSON](#json) ·
[Errors](#errors-and-timeouts) · [ESP32 setup](#esp32-setup) · [Configuration](#configuration) ·
[Simulator](#the-simulator) · [Porting](#porting-a-network-backend) · [Limits](#limits)

---

## Wi-Fi

```csharp
bool ok = Wifi.Connect("MyNetwork", "secret-password");          // waits for an IP (15 s)
bool ok2 = Wifi.Connect("MyNetwork", "secret-password", 30000);  // own timeout
Wifi.Connect("Mesh", "pw", 15000, "24:0A:C4:12:34:56", 6);       // one access point (BSSID, channel)
Wifi.Connect();                                                  // the saved network (Wifi.Save / menuconfig)

Wifi.Begin("MyNetwork", "secret-password");   // returns at once; follow Wifi.OnEvent
Wifi.WaitConnected(10000);                    // ... and wait later

Wifi.ConnectEnterprise("eduroam", "anonymous@uni.edu", "me@uni.edu", "pw");  // WPA2/3-Enterprise (PEAP/TTLS)
Wifi.Reconnect();      // same settings again
Wifi.Disconnect();     // leave the network, radio stays on
Wifi.Off();            // stop Wi-Fi and give its RAM back
```

`Connect` returns `false` (it does not throw) when the network is not found, the password
is wrong or no address arrives in time; then `Wifi.StatusName` / `Wifi.ReasonName()` say
why. Bad arguments (empty SSID, a 3-character password) throw `ArgumentException`.
WPA3-SAE, WPA2/WPA3 transition networks and protected management frames are handled
automatically; `Wifi.MinAuth = Wifi.AuthWpa2` rejects weaker networks.

| Status (`Wifi.Status`) | Meaning |
|---|---|
| `Idle`, `Stopped` | not connecting / radio off |
| `Connecting` | association or DHCP running |
| `Connected` | linked and has an IP address (`Wifi.IsConnected`) |
| `NoSsid` | network not found |
| `WrongPassword` | authentication failed |
| `ConnectFailed`, `ConnectionLost`, `Disconnected` | other failures / link dropped |

Link information: `Ssid`, `Bssid`, `Rssi` (dBm), `Channel`, `Auth` (+ `Wifi.AuthName(a)`),
`Mode` (`ModeOff`, `ModeStation`, `ModeAccessPoint`, `ModeStationAp`), `DisconnectReason`
(+ `Wifi.ReasonName(r)`; constants `ReasonAuthFail`, `ReasonNoApFound`,
`ReasonBeaconTimeout`, `ReasonAssocFail`, `ReasonHandshakeTimeout`, `ReasonAssocLeave`).

### Scanning

```csharp
foreach (var n in Wifi.Scan())               // strongest first; Scan(true) includes hidden networks
    Console.WriteLine($"{n.Ssid,-20} {n.Rssi} dBm ch {n.Channel} {n.AuthName} {n.Quality}%");
```

`WifiNetwork`: `Ssid`, `Bssid`, `Rssi`, `Channel`, `Auth`, `AuthName`, `Secure`, `Hidden`,
`Protocols` (`Wifi.Proto*` bits), `Quality` (0–100 %). Security constants: `AuthOpen`,
`AuthWep`, `AuthWpa`, `AuthWpa2`, `AuthWpaWpa2`, `AuthEnterprise`, `AuthWpa3`,
`AuthWpa2Wpa3`, `AuthWapi`, `AuthOwe`, `AuthWpa3Enterprise192`.

## Events

```csharp
Wifi.OnEvent((ev, value) =>
{
    if (ev == Wifi.EventGotIp) Console.WriteLine("online: " + Wifi.Ip);
    if (ev == Wifi.EventDisconnected) Console.WriteLine("lost: " + Wifi.ReasonName(value));
});
```

Handlers run on the script thread, like GPIO interrupts (during `Thread.Sleep`, between
statements, in `Scheduler` loops) — never inside the Wi-Fi driver. Events:
`EventStaStart`, `EventStaStop`, `EventConnected` (value = channel), `EventDisconnected`
(reason), `EventGotIp`, `EventLostIp`, `EventGotIp6`, `EventScanDone`, `EventApStart`,
`EventApStop`, `EventApJoined`, `EventApLeft`, `EventApAssigned`, `EventEspNowRecv`,
`EventEspNowSent`, `EventNtpSync`. `Wifi.EventName(ev)` gives the name.

`Wifi.AutoReconnect = true` (the default) reconnects after the link drops, with back-off.

## Soft access point

```csharp
Wifi.StartAp("MicroCS-Setup", "12345678");          // (ssid, password = open, channel = 1, hidden, maxClients = 4)
Console.WriteLine(Wifi.ApIp);                       // 192.168.4.1
foreach (var c in Wifi.ApClients()) Console.WriteLine(c.Mac + " " + c.Ip + " " + c.Rssi);
Wifi.SetApIp("10.0.0.1", "10.0.0.1", "255.255.255.0");
Wifi.StopAp();
```

Station and AP run together (`ModeStationAp`): connect to the router *and* offer a setup
hotspot. On one radio both use the router's channel.

## IP configuration and radio options

```csharp
Wifi.SetStaticIp("192.168.1.77", "192.168.1.1", "255.255.255.0", "1.1.1.1");   // dns1, dns2 optional
Wifi.UseDhcp();
Console.WriteLine($"{Wifi.Ip} {Wifi.Netmask} {Wifi.Gateway} {Wifi.Dns} {Wifi.Dns2} dhcp={Wifi.Dhcp} {Wifi.Ipv6}");
```

| Property | Values |
|---|---|
| `Hostname` | DHCP / mDNS name (set before connecting) |
| `Country` | `"DE"`, `"US"`, `"JP"`, … — channels and power limits |
| `TxPower` | dBm, 2–20 |
| `PowerSave` | `PowerNone` (fastest), `PowerMin` (default), `PowerMax` (lowest current) |
| `ListenInterval` | beacons between wake-ups with `PowerMax` |
| `Protocol` / `ApProtocol` | `Proto11b \| Proto11g \| Proto11n`, `ProtoLR` (Espressif long range), `Proto11ax` (C5/C6) |
| `Bandwidth` | 20 or 40 MHz |
| `Band` | `Band2G`, `Band5G`, `BandAuto` — 5 GHz on dual-band chips (ESP32-C5) |
| `Channel` | primary channel (while not connected) |
| `MinAuth` | weakest accepted security |
| `Mac` / `ApMac` | read or set the MAC address |
| `AutoReconnect`, `Persistent` | reconnect by itself; let the driver keep its own config in flash |
| `Antenna` | antenna selection on boards with two |

Unsupported values throw `NotSupportedException`, out-of-range ones `ArgumentException`.

## Saved networks

```csharp
Wifi.Save("MyNetwork", "secret-password");   // NVS flash (namespace "microcs")
Wifi.Connect();                              // any later script, no password in the source
Console.WriteLine(Wifi.SavedSsid);           // "" when nothing is saved
Wifi.Forget();
```

With **menuconfig → MicroCS → Wi-Fi → Connect at boot** the firmware joins the saved (or
the menuconfig) network before `boot.cs` / `main.cs` run, so scripts find it online.

## ESP-NOW

Connectionless board-to-board packets (≈1 ms, up to `EspNow.MaxData` = 250 bytes per packet),
no router needed. Both boards must be on the same channel.

```csharp
EspNow.Start();
Console.WriteLine("my MAC: " + Wifi.Mac);
EspNow.AddPeer("24:0A:C4:12:34:56");             // (mac, channel = 0, key = null; 16 chars = encrypted)
bool acked = EspNow.Send("24:0A:C4:12:34:56", "temp=21.5");   // string or byte[]
EspNow.Broadcast(new byte[] { 1, 2, 3 });
EspNow.OnReceive((mac, data) => Console.WriteLine(mac + ": " + Encoding.UTF8.GetString(data)));
var m = EspNow.Receive(500);                     // or poll: EspNowMessage or null
EspNow.Stop();
```

Also: `RemovePeer(mac)`, `SetPmk(key16)`, `Peers`, `MaxData`. `EspNowMessage`: `Mac`,
`Data`, `Text`, `Length`, `Rssi`.

## NTP time

```csharp
Ntp.Timezone = "CET-1CEST,M3.5.0,M10.5.0/3";    // POSIX TZ string
Ntp.Sync();                                      // pool.ntp.org, waits up to 10 s; Sync(server, ms) / Sync(ms)
Console.WriteLine(Ntp.LocalTime + "  UTC " + Ntp.UtcTime + "  unix " + Ntp.Time);
```

`Ntp.Start(s1, s2, s3)` keeps syncing in the background; `Ntp.Synced`, `Ntp.UtcOffset`
(seconds). On the PC the system clock is used.

## mDNS and ping

```csharp
Mdns.Start("greenhouse");                                   // http://greenhouse.local/
Mdns.AddService("_http", "_tcp", 80, new[] { "path=/" });
string ip = Mdns.Resolve("printer.local");                  // null when nobody answers
int ms = Ping.Send("192.168.1.1", 1000);                    // -1 = no answer
```

`Dns.Resolve`, `TcpClient` and `Http` resolve `.local` names through mDNS automatically.

## TCP

```csharp
using var tcp = new TcpClient("192.168.1.10", 5000);       // (host, port, timeoutMs = 10000)
tcp.WriteLine("hello");
string reply = tcp.ReadLine();                               // null when the peer closed
```

`TcpClient`: `Connect(host, port[, ms])`, `ConnectTls(host, port[, ms[, caPem]])`,
`Write(string | byte[])`, `WriteLine`, `Read([max])` → `byte[]`, `ReadString([max])`,
`ReadByte()` (-1 at end), `ReadLine()`, `ReadToEnd()`, `Available`, `Connected`, `Timeout`
(ms, 0 = forever), `NoDelay`, `KeepAlive`, `RemoteAddress/Port`, `LocalAddress/Port`,
`IsTls`, `Close()` / `Dispose()`.

```csharp
var server = new TcpListener(8080);          // or (address, port); port 0 = any free port
server.Start();
while (true)
{
    var client = server.AcceptTcpClient(500);   // null after 500 ms without a client
    if (client == null) continue;               // do other work here
    client.WriteLine("hi " + client.RemoteAddress);
    client.Close();
}
```

`TcpListener`: `Start([backlog])`, `Stop()`, `Pending()`, `AcceptTcpClient([ms])` /
`Accept([ms])`, `LocalPort`, `Active`. Studio's *Tiny web server* template is a complete
HTTP server in ~25 lines.

## UDP

```csharp
var udp = new UdpClient(5000);                       // bound port (0 / none = any)
udp.Send("ping", "192.168.1.20", 5000);
string text = udp.ReceiveString(1000);               // null on timeout; Receive() -> byte[]
Console.WriteLine(udp.RemoteAddress + ":" + udp.RemotePort);
udp.EnableBroadcast = true;  udp.Send("hello", "255.255.255.255", 5000);
udp.JoinMulticastGroup("239.1.2.3");
```

Also `Connect(host, port)` + `Send(data)`, `LocalPort`, `Available`, `Timeout`,
`DropMulticastGroup`, `Close()`.

## DNS

`Dns.Resolve(host)` → first IPv4 string, `Dns.GetHostAddresses(host)` → `string[]`,
`Dns.HostName`. Unknown names throw `SocketException`.

## HTTP and HTTPS

```csharp
var r = Http.Get("https://api.example.com/items?q=" + Http.UrlEncode("blue led"));
if (r.IsSuccess) Console.WriteLine(r.Body);

var headers = new Dictionary<string, string> { ["Authorization"] = "Bearer " + token };
r = Http.Post("https://api.example.com/items", Json.Stringify(item), "application/json", headers);
r.EnsureSuccess();                                   // HttpRequestException unless 2xx

string page = Http.GetString("http://192.168.1.20/status");
long bytes = Http.Download("https://example.com/firmware/app.cs", "/app.cs");   // streamed to flash
```

| | |
|---|---|
| Methods | `Get`, `Head`, `Delete` `(url[, headers])`; `Post`, `Put`, `Patch` `(url, body[, contentType[, headers]])`; `Request(method, url[, body[, contentType[, headers]]])`; `GetString`; `Download(url, path[, headers])` |
| Body | `string` or `byte[]`; Content-Type defaults to JSON when the text looks like JSON, else `text/plain` |
| Headers | `Dictionary<string, string>`, a `"Name: value"` string (lines) or `string[]` |
| Settings | `Http.Timeout` (ms), `Http.UserAgent`, `Http.FollowRedirects` (up to 5) |
| `HttpResponse` | `StatusCode`, `Reason`, `IsSuccess`, `Body`, `Bytes`, `ContentLength`, `ContentType`, `Url`, `Headers`, `Header(name)`, `EnsureSuccess()` |

Chunked transfer encoding and redirects are handled. A 404 is *not* an exception — check
`StatusCode` or call `EnsureSuccess()`. Bodies above `MCS_HTTP_MAX_BODY` (32 KB) throw;
use `Download` for big files (written to `path.tmp`, renamed when complete).

**HTTPS / TLS** (ESP32): server certificates are checked against ESP-IDF's certificate
bundle (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`, on by default). For a private CA pass its PEM
to `TcpClient.ConnectTls(host, port, ms, caPem)`. `Net.Has("Tls")` is false on the PC.

## JSON

Available on every port (with List + Dictionary; `MCS_ENABLE_JSON`).

```csharp
var doc = Json.Parse("{\"temp\": 21.5, \"tags\": [\"a\", \"b\"], \"ok\": true}");
double t = doc["temp"];  string first = doc["tags"][0];
string text = Json.Stringify(new Dictionary<string, object> { ["id"] = 7, ["on"] = true });
string pretty = Json.Stringify(doc, true);          // indented
class Reading { public string Name = "t1"; public double Value { get; set; } }
Console.WriteLine(JsonSerializer.Serialize(new Reading()));   // {"Name":"t1","Value":0}
```

Objects become `Dictionary<string, object>`, arrays `List<object>`, integral numbers `int`
(`long` range with `MCS_INT64`) and others `double`. `Stringify` writes dictionaries,
lists, arrays, `HashSet`, numbers (NaN/∞ → `null`), strings, chars, `bool`, `null` and the
fields of script objects (names starting with `_` are skipped). `Json.Escape(s)` quotes a
string. Errors: `FormatException` with the offset.

## Errors and timeouts

| Exception | When |
|---|---|
| `SocketException` (an `IOException`) | connection refused / reset, unknown host, network down |
| `TimeoutException` | connect / read / accept exceeded its timeout |
| `HttpRequestException` | `EnsureSuccess()` / `GetString()` on a non-2xx status |
| `InvalidOperationException` | using a closed or unconnected socket, no saved network |
| `ArgumentException` | bad SSID / password / MAC / URL / header |
| `NotSupportedException` | the board or port lacks the feature (`Net.Has`) |

Blocking calls wait in short slices (`MCS_NET_SLICE_MS`): Ctrl-C, `Stop` in Studio and
`--time-limit` interrupt them, and event handlers keep running while a script waits.

## ESP32 setup

Nothing to do for the example firmware (`ports/esp32/example`): Wi-Fi is on by default
on chips that have it. In **menuconfig → MicroCS → Wi-Fi and networking**:

| Option | Default | |
|---|---|---|
| `CONFIG_MICROCS_WIFI` | y | the whole network library (off: saves ~600 KB flash, ~70 KB RAM) |
| `CONFIG_MICROCS_WIFI_RESERVE_KB` | 72 (56 on C2) | RAM left outside the script heap for the Wi-Fi driver and lwIP |
| `CONFIG_MICROCS_WIFI_SSID` / `_PASSWORD` | "" | network joined at boot |
| `CONFIG_MICROCS_WIFI_AUTOCONNECT` | y | join the `Wifi.Save()` network at boot |

The example's `partitions.csv` has a **2 MB app** partition (Wi-Fi + TLS make the image
~1.6 MB) and ~1.9 MB LittleFS. In your own firmware call `mcs_esp32_wifi_autoconnect(ssid,
password)` after the VM is created (see `ports/esp32/example/main/main.c`); the port
installs the backend in `mcs_port_esp32_hal_init` by itself. Use `idf.py add-dependency
espressif/mdns` for `Mdns`. Needs ESP-IDF 5.x (Enterprise: 5.1+); CI builds it with 5.3 for ESP32, S3, C2, C3 and C6.

RAM: the driver uses ~50–70 KB (more with TLS: ~40 KB per HTTPS connection during the
handshake). On the ESP32-C2 (272 KB) keep scripts small or lower `MCS_NET_MAX_READ`.

## Configuration

`config/mcs_user_config.h` → *Wi-Fi and networking*:

| Option | Default | |
|---|---|---|
| `MCS_ENABLE_NET` | 1 on ESP-IDF / Linux / macOS | `Net`, `Wifi`, `EspNow`, `Ntp`, `Mdns`, `Ping` |
| `MCS_ENABLE_SOCKETS` | = `MCS_ENABLE_NET` | `TcpClient`, `TcpListener`, `UdpClient`, `Dns`, `Http` |
| `MCS_NET_SIM` | 1 on the PC | the simulated radio (`--sim`) |
| `MCS_NET_TIMEOUT` | 10000 | default socket / DNS / HTTP timeout (ms) |
| `MCS_WIFI_CONNECT_TIMEOUT` | 15000 | `Wifi.Connect` wait (ms) |
| `MCS_WIFI_SCAN_MAX` | 24 | networks returned by `Scan` |
| `MCS_NET_MAX_READ` | 4096 | largest single read / datagram |
| `MCS_HTTP_MAX_BODY` | 32768 | largest body kept in memory |
| `MCS_HTTP_MAX_HEADER` | 2048 | response header bytes kept |
| `MCS_HTTP_MAX_REDIRECTS` | 5 | redirects followed |
| `MCS_ESPNOW_MAX_PEERS` | 20 | peers tracked |
| `MCS_NET_SLICE_MS` | 20 | how often blocking waits check for Stop |
| `MCS_ESP32_WIFI_RETRIES` | 2 | extra attempts before `Connect` fails |
| `MCS_ESP32_ESPNOW_QUEUE` | 8 | received ESP-NOW packets buffered |
| `MCS_ENABLE_JSON` | 1 | `Json`, `JsonSerializer` |

## The simulator

`./mcs --sim script.cs` (and Studio's simulator) adds a fake radio so Wi-Fi scripts run on
the PC: networks **MicroCS-Lab** (WPA2, password `microcs123`), **CoffeeShop** (open),
**Neighbor** (WPA3, `letmein99`) and a hidden **HiddenLab**; DHCP address 192.168.1.50,
gateway 192.168.1.1; a phone that joins your soft AP; an ESP-NOW peer
`24:0A:C4:00:00:99` that echoes packets; `printer.local` = 192.168.1.20; ping answers.
Sockets and HTTP use the PC's real network. `tests/t24_net.cs` exercises all of it.

## Porting a network backend

A port fills a `mcs_net_t` table (`include/mcs_net.h`) — any `NULL` entry simply makes
that feature unavailable — and calls `mcs_net_set(&table)` before the HAL library opens.
Events from driver callbacks go through `mcs_net_post(MCS_NET_EV_*, value)` (ISR / task
safe). Sockets use BSD / lwIP sockets directly, so a port with lwIP gets TCP, UDP, DNS and
HTTP for free; HTTPS needs the four `tls_*` functions. `ports/esp32/mcs_port_esp32_net.c`
is the reference implementation and `modules/net/mcs_net_sim.c` the smallest one.

## Limits

* IPv4 sockets (IPv6 addresses are reported, not yet usable in sockets).
* No server-side TLS (`TcpListener` is plain TCP); HTTP client is HTTP/1.1 without
  keep-alive (one connection per request).
* Wi-Fi Easy Connect (DPP), WPS, FTM and NAN are not exposed.
* Wi-Fi is not available on chips without a radio (ESP32-H2, ESP32-P4 without a
  companion chip), on the Arduino, Pico, STM32 and Zephyr ports yet.
