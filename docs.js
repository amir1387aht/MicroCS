/* MicroCS Studio - signatures and short docs for completion, signature help and hover.
 * One line per member (from docs/STDLIB.md and docs/HAL.md):
 *   @Class | summary                 start a class (`new(...)` lines = constructors)
 *   Name(type a, type b = 1): Ret | doc     static method (repeat the line for overloads)
 *   .Name(...): Ret | doc            instance method        .Name: Type | doc   instance property
 *   Name: Type | doc                 static property        const Name: Type | doc   constant
 *   @Class < Other                   also inherits Other's instance members        */
window.MCS_DOCS_SRC = String.raw`
@Console | Text output to the serial console / REPL
WriteLine(): void | Writes a line ending
WriteLine(object value): void | Writes the value (ToString()) and a line ending
WriteLine(string format, params object[] args): void | Composite format: "{0} = {1:F2}"
Write(object value): void | Writes the value without a line ending
Write(string format, params object[] args): void | Composite format without a line ending
ReadLine(): string | Reads a line typed on the console (null at end of input)
Clear(): void | Clears the terminal (ANSI)

@Math | Math functions (double unless noted)
const PI: double | 3.14159265358979
const E: double | 2.71828182845905
const Tau: double | 2π
Abs(number x): number | Absolute value
Max(number a, number b): number | Larger of two values
Min(number a, number b): number | Smaller of two values
Sign(number x): int | -1, 0 or 1
Clamp(number value, number min, number max): number | value limited to [min, max]
DivRem(int a, int b): (int, int) | Quotient and remainder as a tuple
Sqrt(double x): double | Square root
Pow(double x, double y): double | x raised to the power y
Sin(double rad): double | Sine (radians)
Cos(double rad): double | Cosine (radians)
Tan(double rad): double | Tangent (radians)
Asin(double x): double | Arc sine
Acos(double x): double | Arc cosine
Atan(double x): double | Arc tangent
Atan2(double y, double x): double | Angle of the point (x, y), -π..π
Sinh(double x): double | Hyperbolic sine
Cosh(double x): double | Hyperbolic cosine
Tanh(double x): double | Hyperbolic tangent
Exp(double x): double | e^x
Log(double x): double | Natural logarithm
Log(double x, double newBase): double | Logarithm in the given base
Log10(double x): double | Base-10 logarithm
Log2(double x): double | Base-2 logarithm
Cbrt(double x): double | Cube root
Hypot(double x, double y): double | √(x² + y²)
Floor(double x): double | Largest integer ≤ x
Ceiling(double x): double | Smallest integer ≥ x
Truncate(double x): double | Integer part
Round(double x): double | Rounds to the nearest integer (banker's rounding)
Round(double x, int digits): double | Rounds to the given number of decimals
FusedMultiplyAdd(double x, double y, double z): double | x * y + z
IEEERemainder(double x, double y): double | IEEE remainder

@Thread | Threads (MicroCS: one script thread; callbacks run during Sleep)
Sleep(int ms): void | Pauses the script; timers, pin and UART callbacks still run

@Environment | Runtime information
TickCount: int | Milliseconds since start
TickCount64: long | Milliseconds since start (64-bit)
const NewLine: string | "\n"

@GC | Garbage collector
Collect(): void | Runs a full collection
GetTotalMemory(bool forceFullCollection = false): long | Bytes in use on the VM heap
CollectionCount(int generation = 0): int | Number of collections so far

@Debug | Debug output (to the console)
Assert(bool condition): void | Throws when the condition is false
Assert(bool condition, string message): void | Throws with the message when false
WriteLine(object value): void | Writes a debug line
Write(object value): void | Writes debug text

@Stopwatch | Measures elapsed time (milliseconds)
new() | A stopped stopwatch
StartNew(): Stopwatch | Creates and starts a stopwatch
.Start(): void | Starts / resumes timing
.Stop(): void | Stops timing
.Reset(): void | Stops and clears
.Restart(): void | Clears and starts again
.ElapsedMilliseconds: long | Elapsed time in ms
.ElapsedTicks: long | Elapsed ticks
.IsRunning: bool | true while timing

@Random | Pseudo-random numbers (xorshift)
new() | Seeded from the clock
new(int seed) | Repeatable sequence
.Next(): int | Non-negative random int
.Next(int maxExclusive): int | 0 ≤ n < max
.Next(int minInclusive, int maxExclusive): int | min ≤ n < max
.NextDouble(): double | 0.0 ≤ x < 1.0
.NextSingle(): float | 0.0 ≤ x < 1.0
.NextBytes(byte[] buffer): void | Fills the array with random bytes

@Convert | Type conversions
ToInt32(object value): int | Converts (rounds doubles, parses strings)
ToInt32(string value, int fromBase): int | Parses in base 2, 8, 10 or 16
ToInt64(object value): long | Converts to long
ToUInt32(object value): uint | Converts to uint
ToByte(object value): byte | Converts to byte
ToByte(string value, int fromBase): byte | Parses a byte in base 2, 8, 10 or 16
ToSByte(object value): sbyte | Converts to sbyte
ToInt16(object value): short | Converts to short
ToUInt16(object value): ushort | Converts to ushort
ToDouble(object value): double | Converts to double
ToSingle(object value): float | Converts to float
ToBoolean(object value): bool | Converts to bool
ToChar(object value): char | Converts to char
ToString(object value): string | Converts to string
ToString(int value, int toBase): string | Formats in base 2, 8, 10 or 16

@string | UTF-8 text (byte-indexed)
IsNullOrEmpty(string s): bool | null or ""
IsNullOrWhiteSpace(string s): bool | null, "" or only white space
Join(string separator, IEnumerable values): string | Joins the values with the separator
Concat(params object[] values): string | Concatenates the values
Format(string format, params object[] args): string | "{0,5} {1:X2} {2:F1}"
Compare(string a, string b): int | <0, 0, >0
Compare(string a, string b, bool ignoreCase): int | Comparison, optionally ignoring case
CompareOrdinal(string a, string b): int | Byte-wise comparison
Equals(string a, string b): bool | Equal text
.Length: int | Number of characters
.Substring(int start): string | From start to the end
.Substring(int start, int length): string | length characters from start
.IndexOf(string value): int | First position or -1
.IndexOf(string value, int start): int | First position at or after start, or -1
.IndexOf(char value): int | First position or -1
.LastIndexOf(string value): int | Last position or -1
.IndexOfAny(char[] chars): int | First position of any of the chars
.Contains(string value): bool | true when value occurs
.StartsWith(string value): bool | Prefix test
.EndsWith(string value): bool | Suffix test
.Trim(): string | Without leading and trailing white space
.Trim(params char[] chars): string | Without the given leading / trailing chars
.TrimStart(): string | Without leading white space
.TrimEnd(): string | Without trailing white space
.ToUpper(): string | Upper case
.ToLower(): string | Lower case
.ToUpperInvariant(): string | Upper case
.ToLowerInvariant(): string | Lower case
.Replace(string oldValue, string newValue): string | Replaces every occurrence
.PadLeft(int totalWidth): string | Right-aligns with spaces
.PadLeft(int totalWidth, char padding): string | Right-aligns with the char
.PadRight(int totalWidth): string | Left-aligns with spaces
.PadRight(int totalWidth, char padding): string | Left-aligns with the char
.Split(params char[] separators): string[] | Splits at the separators
.Split(char separator, StringSplitOptions options): string[] | StringSplitOptions.RemoveEmptyEntries / TrimEntries
.Split(string separator): string[] | Splits at the string
.ToCharArray(): char[] | The characters
.Insert(int index, string value): string | Inserts value at index
.Remove(int start): string | Removes from start to the end
.Remove(int start, int count): string | Removes count characters
.CompareTo(string other): int | <0, 0, >0
.Equals(string other): bool | Equal text
.Clone(): string | The same string

@StringBuilder | Mutable string buffer (System.Text)
new() | Empty builder
new(string value) | Builder starting with the text
new(int capacity) | Empty builder with room for capacity bytes
.Append(object value): StringBuilder | Appends the value (chainable)
.AppendLine(): StringBuilder | Appends a line ending
.AppendLine(object value): StringBuilder | Appends the value and a line ending
.AppendFormat(string format, params object[] args): StringBuilder | Appends formatted text
.Insert(int index, object value): StringBuilder | Inserts at index
.Remove(int start, int length): StringBuilder | Removes length characters
.Replace(string oldValue, string newValue): StringBuilder | Replaces every occurrence
.Clear(): StringBuilder | Empties the builder
.ToString(): string | The text
.ToString(int start, int length): string | Part of the text
.Length: int | Current length
.Capacity: int | Allocated size

@IEnumerable | LINQ operators (eager: each returns a new List<T> or a value)
.Where(Func<T, bool> predicate): List<T> | Items that match
.Select(Func<T, R> selector): List<R> | Projects every item
.SelectMany(Func<T, IEnumerable<R>> selector): List<R> | Projects and flattens
.Any(): bool | true when not empty
.Any(Func<T, bool> predicate): bool | true when some item matches
.All(Func<T, bool> predicate): bool | true when every item matches
.Count(): int | Number of items
.Count(Func<T, bool> predicate): int | Number of matching items
.Sum(): number | Sum of the items
.Sum(Func<T, number> selector): number | Sum of the selected values
.Average(): double | Mean of the items
.Average(Func<T, number> selector): double | Mean of the selected values
.Min(): T | Smallest item
.Min(Func<T, number> selector): number | Smallest selected value
.Max(): T | Largest item
.Max(Func<T, number> selector): number | Largest selected value
.MinBy(Func<T, K> key): T | Item with the smallest key
.MaxBy(Func<T, K> key): T | Item with the largest key
.First(): T | First item (throws when empty)
.First(Func<T, bool> predicate): T | First matching item
.FirstOrDefault(): T | First item or default
.FirstOrDefault(Func<T, bool> predicate): T | First matching item or default
.Last(): T | Last item
.Last(Func<T, bool> predicate): T | Last matching item
.LastOrDefault(): T | Last item or default
.Single(): T | The only item
.ElementAt(int index): T | Item at index
.ElementAtOrDefault(int index): T | Item at index or default
.OrderBy(Func<T, K> key): List<T> | Sorted ascending by key
.OrderByDescending(Func<T, K> key): List<T> | Sorted descending by key
.ThenBy(Func<T, K> key): List<T> | Secondary ascending key
.ThenByDescending(Func<T, K> key): List<T> | Secondary descending key
.Order(): List<T> | Sorted ascending
.OrderDescending(): List<T> | Sorted descending
.Skip(int count): List<T> | Without the first count items
.Take(int count): List<T> | The first count items
.SkipWhile(Func<T, bool> predicate): List<T> | Skips while the predicate holds
.TakeWhile(Func<T, bool> predicate): List<T> | Takes while the predicate holds
.Distinct(): List<T> | Without duplicates
.DistinctBy(Func<T, K> key): List<T> | Without duplicate keys
.Aggregate(Func<T, T, T> func): T | Folds the items
.Aggregate(A seed, Func<A, T, A> func): A | Folds the items starting from seed
.SequenceEqual(IEnumerable<T> other): bool | Same items in the same order
.Concat(IEnumerable<T> other): List<T> | Both sequences
.Append(T item): List<T> | The items plus one
.Zip(IEnumerable<U> other): List<(T, U)> | Pairs of items
.Chunk(int size): List<T[]> | Groups of size items
.ToDictionary(Func<T, K> key): Dictionary<K, T> | Dictionary keyed by the selector
.ToDictionary(Func<T, K> key, Func<T, V> value): Dictionary<K, V> | Dictionary from key and value selectors
.GroupBy(Func<T, K> key): Dictionary<K, List<T>> | Groups by key (MicroCS: a dictionary of lists)
.ToList(): List<T> | A new list
.ToArray(): T[] | A new array
.Contains(T item): bool | true when the item occurs
.ForEach(Action<T> action): void | Runs action for every item

@List | Growable list (System.Collections.Generic) < IEnumerable
new() | Empty list
new(int capacity) | Empty list with room for capacity items
new(IEnumerable<T> items) | List with the items
.Count: int | Number of items
.Capacity: int | Allocated size
.Add(T item): void | Appends an item
.AddRange(IEnumerable<T> items): void | Appends the items
.Insert(int index, T item): void | Inserts at index
.InsertRange(int index, IEnumerable<T> items): void | Inserts the items at index
.Remove(T item): bool | Removes the first occurrence
.RemoveAt(int index): void | Removes the item at index
.RemoveRange(int index, int count): void | Removes count items
.RemoveAll(Func<T, bool> match): int | Removes matching items, returns how many
.Clear(): void | Removes everything
.IndexOf(T item): int | Position or -1
.LastIndexOf(T item): int | Last position or -1
.Find(Func<T, bool> match): T | First match or default
.FindLast(Func<T, bool> match): T | Last match or default
.FindIndex(Func<T, bool> match): int | Position of the first match or -1
.FindLastIndex(Func<T, bool> match): int | Position of the last match or -1
.FindAll(Func<T, bool> match): List<T> | All matches
.Exists(Func<T, bool> match): bool | true when an item matches
.TrueForAll(Func<T, bool> match): bool | true when every item matches
.ConvertAll(Func<T, R> converter): List<R> | Converted copy
.Sort(): void | Sorts in place
.Sort(Comparison<T> comparison): void | Sorts with (a, b) => a.CompareTo(b)
.Reverse(): void | Reverses in place
.GetRange(int index, int count): List<T> | Copy of a range
.BinarySearch(T item): int | Index in a sorted list (negative when missing)
.CopyTo(T[] array): void | Copies into the array
.TrimExcess(): void | Releases unused capacity
.AsReadOnly(): List<T> | The list

@Array | Arrays (T[] has Length and the List / LINQ methods)
Sort(Array array): void | Sorts in place
Sort(Array array, Comparison<T> comparison): void | Sorts with a comparison
Reverse(Array array): void | Reverses in place
IndexOf(Array array, object value): int | Position or -1
LastIndexOf(Array array, object value): int | Last position or -1
Fill(Array array, object value): void | Sets every element
Copy(Array source, Array dest, int length): void | Copies length elements
Copy(Array source, int srcIndex, Array dest, int destIndex, int length): void | Copies a range
Clear(Array array): void | Sets every element to default
Clear(Array array, int index, int length): void | Clears a range
Exists(Array array, Func<T, bool> match): bool | true when an element matches
TrueForAll(Array array, Func<T, bool> match): bool | true when every element matches
Find(Array array, Func<T, bool> match): T | First match
FindIndex(Array array, Func<T, bool> match): int | Position of the first match
FindAll(Array array, Func<T, bool> match): T[] | All matches
ForEach(Array array, Action<T> action): void | Runs action for every element
BinarySearch(Array array, object value): int | Index in a sorted array
ConvertAll(Array array, Func<T, R> converter): R[] | Converted copy
Empty(): T[] | An empty array

@Array_i | (instance members of T[]) < IEnumerable
.Length: int | Number of elements
.LongLength: long | Number of elements
.Rank: int | Number of dimensions
.GetLength(int dimension): int | Size of a dimension
.Clone(): object | Shallow copy
.CopyTo(Array dest, int index): void | Copies into dest at index
.IndexOf(T item): int | Position or -1
.Reverse(): List<T> | Reversed copy

@Enumerable | Sequence helpers (System.Linq)
Range(int start, int count): List<int> | start, start+1, … (count numbers)
Repeat(T value, int count): List<T> | The value count times
Empty(): List<T> | An empty sequence

@Dictionary | Key/value map, insertion-ordered (System.Collections.Generic) < IEnumerable
new() | Empty dictionary
.Count: int | Number of entries
.Keys: List<K> | The keys
.Values: List<V> | The values
.Add(K key, V value): void | Adds an entry (throws when the key exists)
.TryAdd(K key, V value): bool | Adds when the key is new
.ContainsKey(K key): bool | true when the key exists
.ContainsValue(V value): bool | true when some entry has the value
.TryGetValue(K key, out V value): bool | Looks up without throwing
.GetValueOrDefault(K key): V | Value or default
.GetValueOrDefault(K key, V fallback): V | Value or the fallback
.Remove(K key): bool | Removes the entry
.Clear(): void | Removes everything

@HashSet | Set of unique items (System.Collections.Generic) < IEnumerable
new() | Empty set
new(IEnumerable<T> items) | Set with the items
.Count: int | Number of items
.Add(T item): bool | Adds; false when already present
.Remove(T item): bool | Removes the item
.Contains(T item): bool | Membership test
.Clear(): void | Removes everything
.UnionWith(IEnumerable<T> other): void | Adds the other items
.IntersectWith(IEnumerable<T> other): void | Keeps the common items
.ExceptWith(IEnumerable<T> other): void | Removes the other items
.SymmetricExceptWith(IEnumerable<T> other): void | Items in exactly one of the sets
.IsSubsetOf(IEnumerable<T> other): bool | Every item is in other
.IsSupersetOf(IEnumerable<T> other): bool | Contains every item of other
.Overlaps(IEnumerable<T> other): bool | At least one common item
.SetEquals(IEnumerable<T> other): bool | Same items

@Queue | First-in first-out (System.Collections.Generic) < IEnumerable
new() | Empty queue
.Count: int | Number of items
.Enqueue(T item): void | Adds at the end
.Dequeue(): T | Removes and returns the first item
.Peek(): T | The first item
.TryDequeue(out T item): bool | Dequeues without throwing
.TryPeek(out T item): bool | Peeks without throwing
.Clear(): void | Removes everything

@Stack | Last-in first-out (System.Collections.Generic) < IEnumerable
new() | Empty stack
.Count: int | Number of items
.Push(T item): void | Adds on top
.Pop(): T | Removes and returns the top item
.Peek(): T | The top item
.TryPop(out T item): bool | Pops without throwing
.TryPeek(out T item): bool | Peeks without throwing
.Clear(): void | Removes everything

@File | Files on the device filesystem (System.IO)
ReadAllText(string path): string | Whole file as text
WriteAllText(string path, string text): void | Creates / overwrites
AppendAllText(string path, string text): void | Appends (creates when missing)
ReadAllLines(string path): string[] | Lines without endings
WriteAllLines(string path, IEnumerable<string> lines): void | Writes the lines
AppendAllLines(string path, IEnumerable<string> lines): void | Appends the lines
ReadAllBytes(string path): byte[] | Whole file as bytes
WriteAllBytes(string path, byte[] bytes): void | Creates / overwrites
Exists(string path): bool | true for an existing file
Delete(string path): void | Removes the file
Copy(string source, string dest): void | Copies (fails when dest exists)
Copy(string source, string dest, bool overwrite): void | Copies, optionally overwriting
Move(string source, string dest): void | Renames / moves
GetLength(string path): long | Size in bytes (MicroCS)

@Directory | Folders (System.IO)
Exists(string path): bool | true for an existing folder
CreateDirectory(string path): void | Creates the folder (and parents)
GetFiles(string path): string[] | Files in the folder (full paths)
GetDirectories(string path): string[] | Sub-folders
GetFileSystemEntries(string path): string[] | Files and folders
Delete(string path): void | Removes an empty folder
Delete(string path, bool recursive): void | Removes the folder and its content
GetCurrentDirectory(): string | "/"

@Path | Path strings (System.IO)
Combine(params string[] parts): string | Joins with "/"
GetFileName(string path): string | "a/b.cs" → "b.cs"
GetExtension(string path): string | ".cs"
GetFileNameWithoutExtension(string path): string | "b"
GetDirectoryName(string path): string | "a"
GetFullPath(string path): string | Absolute path

@DriveInfo | Size of a mounted filesystem (System.IO)
new(string path) | The mount holding path
GetDrives(): DriveInfo[] | Every mount
.Name: string | Mount point
.TotalSize: long | Bytes
.AvailableFreeSpace: long | Free bytes
.TotalFreeSpace: long | Free bytes
.DriveFormat: string | "littlefs", "fat", "posix", "ram" …
.IsReady: bool | true when mounted

@BitConverter | Little-endian byte conversions
const IsLittleEndian: bool | true
GetBytes(number value): byte[] | Bytes of an int, short, long, float, double or bool
ToInt16(byte[] bytes, int index = 0): short | 2 bytes → short
ToUInt16(byte[] bytes, int index = 0): ushort | 2 bytes → ushort
ToInt32(byte[] bytes, int index = 0): int | 4 bytes → int
ToUInt32(byte[] bytes, int index = 0): uint | 4 bytes → uint
ToInt64(byte[] bytes, int index = 0): long | 8 bytes → long
ToSingle(byte[] bytes, int index = 0): float | 4 bytes → float
ToDouble(byte[] bytes, int index = 0): double | 8 bytes → double
ToBoolean(byte[] bytes, int index = 0): bool | 1 byte → bool
ToString(byte[] bytes): string | "01-AB-FF"

@Net | Network library info (Wi-Fi, sockets, HTTP)
Has(string feature): bool | "Wifi", "Wifi.AP", "Wifi.Scan", "Wifi.Save", "Wifi.Wps", "Wifi.SmartConfig", "Wifi.Enterprise", "EspNow", "Mdns", "Ping", "Tls", "Sockets", "Http", "HttpServer", "WebSocket", "DnsServer", "Ntp"
Backend: string | "esp32", "rp2" (Pico W), "sim" or "none"
const ApiVersion: int | Network backend API version

@Wifi | Wi-Fi station + soft access point (ESP32, Pico W; simulated with --sim)
Connect(string ssid, string password = null, int timeoutMs = 15000): bool | Join a network and wait for an IP; false on failure (see StatusName / ReasonName)
Connect(): bool | Join the saved network (Wifi.Save / menuconfig)
Connect(string ssid, string password, int timeoutMs, string bssid, int channel = 0): bool | Join one specific access point
Begin(string ssid, string password = null): void | Start connecting in the background; watch Wifi.OnEvent / WaitConnected
ConnectEnterprise(string ssid, string identity, string username, string password, int timeoutMs = 15000, string caPem = null): bool | WPA2/WPA3-Enterprise (PEAP / TTLS)
WaitConnected(int timeoutMs): bool | Wait until the station has an IP address
Reconnect(): void | Connect again with the last settings
Disconnect(): void | Leave the network (the radio stays on)
Off(): void | Stop Wi-Fi completely (saves power)
Scan(bool showHidden = false, int timeoutMs = 0): WifiNetwork[] | Nearby access points, strongest first
StartAp(string ssid, string password = "", int channel = 1, bool hidden = false, int maxClients = 4): void | Run a soft access point (password: 8+ characters or empty = open)
StopAp(): void | Stop the access point
ApClients(): WifiClient[] | Stations connected to our access point
SetApIp(string ip, string gateway, string netmask): void | Address of the access point network
SetStaticIp(string ip, string gateway, string netmask, string dns1 = null, string dns2 = null): void | Fixed station address
UseDhcp(): void | Back to automatic addressing
Save(string ssid, string password): void | Store credentials in flash (NVS, or /.wifi on the filesystem)
Forget(): void | Erase the stored credentials
Wps(int timeoutMs = 120000, bool save = true): bool | Join by pressing the router's WPS button (Net.Has("Wifi.Wps"))
SmartConfig(int timeoutMs = 120000, bool save = true): bool | Receive the network from the ESP-Touch phone app (Net.Has("Wifi.SmartConfig"))
OnEvent(Action<int, int> handler): void | (event, value) callback: Wifi.EventConnected, EventGotIp, EventDisconnected (value = reason)...
EventName(int ev): string | Name of an event code
ReasonName(int reason): string | Text for a disconnect reason (no argument: the last one)
AuthName(int auth): string | "WPA2-PSK", "Open", ...
Mode: int | ModeOff, ModeStation, ModeAccessPoint, ModeStationAp
Status: int | Idle, Connecting, Connected, NoSsid, WrongPassword, ConnectFailed, ConnectionLost, Disconnected, Stopped
StatusName: string | Status as text
IsConnected: bool | Connected with an IP address
DisconnectReason: int | Reason code of the last disconnect
Ssid: string | Network name of the current link
Bssid: string | MAC of the access point
Rssi: int | Signal strength in dBm
Channel: int | Current channel (settable while not connected)
Auth: int | Security of the current link (Wifi.Auth*)
Ip: string | Station IPv4 address
Netmask: string | Station netmask
Gateway: string | Default gateway
Dns: string | Primary DNS server
Dns2: string | Secondary DNS server
Ipv6: string | Link-local IPv6 address (when enabled)
Dhcp: bool | True when the address came from DHCP
Mac: string | Station MAC address (settable)
ApMac: string | Access point MAC address (settable)
ApIp: string | Access point IPv4 address
ApClientCount: int | Number of connected stations
SavedSsid: string | Stored network name ("" when none)
Hostname: string | DHCP / mDNS host name
Country: string | Country code ("DE", "US", "01" = world safe)
AutoReconnect: bool | Reconnect by itself after the link drops
Persistent: bool | Let the driver keep its own config in flash
PowerSave: int | PowerNone, PowerMin, PowerMax
TxPower: int | Transmit power in dBm (2..20)
Protocol: int | Proto11b | Proto11g | Proto11n | ProtoLR | Proto11ax (station)
ApProtocol: int | Protocols of the access point
Bandwidth: int | 20 or 40 MHz
MinAuth: int | Weakest security the station accepts
ListenInterval: int | Beacons between wake-ups in power save
Band: int | Band2G, Band5G, BandAuto (dual-band chips)
Antenna: int | Antenna selection (boards with two antennas)

@WifiNetwork | One scan result
.Ssid: string | Network name ("" when hidden)
.Bssid: string | Access point MAC
.Rssi: int | Signal in dBm
.Channel: int | Channel
.Auth: int | Security (Wifi.Auth*)
.AuthName: string | Security as text
.Secure: bool | Needs a password
.Hidden: bool | Hidden network
.Protocols: int | Wifi.Proto* bits
.Quality: int | Signal 0..100 %

@WifiClient | A station connected to our access point
.Mac: string | Its MAC address
.Ip: string | Address it was given
.Rssi: int | Signal in dBm

@EspNow | Board-to-board packets without a router (ESP32)
Start(): void | Start ESP-NOW (turns the station on)
Stop(): void | Stop ESP-NOW
AddPeer(string mac, int channel = 0, string key = null): void | Allow sending to a board (key: 16 chars = encrypted)
RemovePeer(string mac): void | Remove a peer
SetPmk(string key): void | Primary master key (16 characters)
Send(string mac, object data, int timeoutMs = 1000): bool | Send a string or byte[] (max MaxData bytes); true when acknowledged
Broadcast(object data): bool | Send to every board in range
Receive(int timeoutMs = 0): EspNowMessage | Next packet or null
OnReceive(Action<string, byte[]> handler): void | (mac, data) callback
Peers: string[] | MACs of the added peers
const MaxData: int | Largest payload (250 bytes)

@EspNowMessage | A received ESP-NOW packet
.Mac: string | Sender MAC
.Data: byte[] | Payload
.Text: string | Payload as UTF-8 text
.Length: int | Payload size
.Rssi: int | Signal in dBm

@Ntp | Network time (SNTP)
Start(string server1 = "pool.ntp.org", string server2 = null, string server3 = null): void | Start background time sync
Sync(string server = "pool.ntp.org", int timeoutMs = 10000): bool | Sync and wait for the first answer
Sync(int timeoutMs): bool | Sync with pool.ntp.org
Synced: bool | The clock has been set
Time: int | Unix time (seconds)
LocalTime: string | "yyyy-MM-dd HH:mm:ss" in Ntp.Timezone
UtcTime: string | "yyyy-MM-dd HH:mm:ss" UTC
Timezone: string | POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
UtcOffset: int | Local minus UTC in seconds

@Mdns | Multicast DNS: name.local (ESP32 with the espressif/mdns component)
Start(string hostname, string instanceName = null): void | Answer as hostname.local
Stop(): void | Stop answering
AddService(string service, string protocol, int port, string[] txt = null): void | Advertise e.g. ("_http", "_tcp", 80)
RemoveService(string service, string protocol): void | Stop advertising a service
Resolve(string host, int timeoutMs = 2000): string | IPv4 of a .local name or null

@Ping | ICMP echo
Send(string host, int timeoutMs = 1000): int | Round trip in ms, -1 = no answer

@TcpClient | TCP connection (System.Net.Sockets)
new() | Not connected yet: call Connect
new(string host, int port, int timeoutMs = 10000) | Connect at once (SocketException when refused)
.Connect(string host, int port, int timeoutMs = 10000): void | Open the connection
.ConnectTls(string host, int port, int timeoutMs = 10000, string caPem = null): void | TLS connection (certificate bundle when caPem is null)
.Write(object data): void | Send a string or byte[]
.WriteLine(string line): void | Send text plus "\r\n"
.Flush(): void | Nothing buffered: kept for .NET habits
.Read(int max = 1024): byte[] | Up to max bytes (empty at end of stream)
.ReadString(int max = 1024): string | Up to max bytes as text
.ReadByte(): int | One byte, -1 at end of stream
.ReadLine(): string | One line without the line ending, null at end of stream
.ReadToEnd(): string | Everything until the peer closes
.Available: int | Bytes waiting
.Connected: bool | Still connected
.Timeout: int | Read / write timeout in ms (0 = forever)
.NoDelay: bool | Disable Nagle (send small writes at once)
.KeepAlive: bool | TCP keep-alive probes
.RemoteAddress: string | Peer address
.RemotePort: int | Peer port
.LocalAddress: string | Our address
.LocalPort: int | Our port
.IsTls: bool | Connection is encrypted
.Close(): void | Close the connection
.Dispose(): void | Same as Close (using blocks)

@TcpListener | TCP server
new(int port) | Listen on all interfaces
new(string address, int port) | Listen on one address (port 0 = any free port)
.Start(int backlog = 4): void | Start listening
.Stop(): void | Stop listening
.Pending(): bool | A client is waiting
.AcceptTcpClient(int timeoutMs = -1): TcpClient | Next client, null on timeout
.Accept(int timeoutMs = -1): TcpClient | Same as AcceptTcpClient
.LocalPort: int | Port actually bound
.Active: bool | Listening
.Dispose(): void | Same as Stop

@UdpClient | UDP datagrams
new(int port = 0) | Bind to a port (0 = any)
.Connect(string host, int port): void | Default destination for Send(data)
.Send(object data): void | Send to the connected destination
.Send(object data, string host, int port): void | Send one datagram
.Receive(int timeoutMs = -1): byte[] | Next datagram, null on timeout
.ReceiveString(int timeoutMs = -1): string | Next datagram as text, null on timeout
.RemoteAddress: string | Sender of the last datagram
.RemotePort: int | Sender port of the last datagram
.LocalPort: int | Bound port
.Available: int | Bytes waiting
.EnableBroadcast: bool | Allow 255.255.255.255 / subnet broadcast
.JoinMulticastGroup(string group): void | Receive a multicast group
.DropMulticastGroup(string group): void | Leave a multicast group
.Timeout: int | Default receive timeout in ms
.Close(): void | Close the socket
.Dispose(): void | Same as Close

@Dns | Name resolution
Resolve(string host): string | First IPv4 address (".local" through mDNS)
GetHostAddresses(string host): string[] | All IPv4 addresses
HostName: string | Our host name

@HttpServer | HTTP/1.1 server with routes, files and WebSocket endpoints
new(int port = 80) | Server on all interfaces (simulator: ports < 1024 move to port + 8000)
new(string address, int port) | Server on one address
.Get(string path, Func<HttpRequest, object> handler): void | GET route; "/led/{id}" captures path parameters, "/files/*" a prefix
.Post(string path, Func<HttpRequest, object> handler): void | POST route
.Put(string path, Func<HttpRequest, object> handler): void | PUT route
.Delete(string path, Func<HttpRequest, object> handler): void | DELETE route
.Patch(string path, Func<HttpRequest, object> handler): void | PATCH route
.On(string method, string path, Func<HttpRequest, object> handler): void | Route for any method (or On(path, handler) for all)
.NotFound(Action<HttpRequest> handler): void | Answer requests no route matched
.ServeFiles(string urlPrefix, string folder): void | Static files (index.html, .gz copies, MIME types)
.WebSocket(string path, Action<WebSocket, object> onMessage, Action<WebSocket> onOpen = null, Action<WebSocket> onClose = null): void | WebSocket endpoint
.Broadcast(object message): int | Send a string or byte[] to every WebSocket client; how many got it
.Start(): void | Start listening
.Stop(): void | Stop and close every connection
.Poll(int timeoutMs = 0): int | Handle waiting requests and WebSocket frames (also DnsServer queries)
.Run(): void | Serve until Stop() or the script ends
.Port: int | Port actually bound
.Running: bool | Listening
.Requests: int | Requests answered so far
.WebSockets: int | Open WebSocket clients
.Cors: bool | Add Access-Control-Allow-* headers (and answer OPTIONS)
.Dispose(): void | Same as Stop

@HttpRequest | A request handed to an HttpServer route (return a string, byte[] or object = JSON)
.Method: string | "GET", "POST", ...
.Path: string | "/api/notes"
.Query: string | Text after "?"
.Url: string | Path plus query
.Body: string | Body as text
.Bytes: byte[] | Body as bytes
.Json: object | Body parsed as JSON
.ContentType: string | Content-Type header
.ContentLength: int | Body size
.Headers: string | All request headers
.Header(string name): string | One header (case-insensitive), null when missing
.Host: string | Host header
.Param(string name, string fallback = null): string | Path parameter, query or form field
.HasParam(string name): bool | The parameter exists
.RemoteAddress: string | Client address
.Status: int | Status for the returned value (default 200)
.Sent: bool | A reply was sent already
.Send(int status, string contentType, object body): void | Reply now (also Send(body), Send(status, body))
.SendJson(object value, int status = 200): void | Reply with JSON
.SendStatus(int status): void | Empty reply with a status
.SendFile(string path, string contentType = null): bool | Reply with a file; false when missing
.Redirect(string url, int status = 302): void | Send the browser elsewhere
.SetHeader(string name, string value): void | Extra response header
.Authenticate(string user, string password, string realm = null): bool | Basic auth; false after answering 401

@WebSocket | WebSocket client (ws://, wss://) and server-side connection
new() | Not connected yet: call Connect
new(string url, int timeoutMs = 10000) | Connect at once
.Connect(string url, int timeoutMs = 10000, object headers = null): void | Open ws://host[:port]/path or wss://
.Send(object message): void | Text for a string, binary for byte[]
.SendText(string text): void | Text frame
.SendBinary(byte[] data): void | Binary frame
.Ping(object data = null): void | Ping frame
.Receive(int timeoutMs = -1): object | Next message (string / byte[]), null on timeout or close
.OnMessage(Action<object> handler): void | Callback run by Poll
.Poll(int timeoutMs = 0): int | Handle waiting frames; messages handed to OnMessage
.Close(int code = 1000, string reason = ""): void | Close handshake
.IsOpen: bool | Connection is open
.State: string | "None", "Open" or "Closed"
.CloseStatus: int | Close code from the peer
.CloseReason: string | Close reason from the peer
.Id: int | Connection number (server side)
.Path: string | Requested path (server side) or URL
.Url: string | Same as Path
.RemoteAddress: string | Peer address
.Timeout: int | Read / write timeout in ms
.IsTls: bool | wss:// connection
.Dispose(): void | Same as Close

@DnsServer | Answers every name with one address (captive portals)
new(string address = null) | Address to hand out (default: Wifi.ApIp)
.Start(string address = null, int port = 53): void | Start answering (simulator: port 8053)
.Stop(): void | Stop answering
.Poll(): int | Answer waiting queries (HttpServer.Poll does this too)
.Running: bool | Answering
.Queries: int | Queries answered
.Ttl: int | Seconds clients may cache the answer
.Domain: string | "*" = every name, or only this one
.Address: string | Address in the answers
.Port: int | Port bound
.Dispose(): void | Same as Stop

@Http | HTTP/1.1 client (https:// when Net.Has("Tls"))
Get(string url, object headers = null): HttpResponse | GET request (headers: Dictionary<string,string>, "Name: value" lines or string[])
Head(string url, object headers = null): HttpResponse | HEAD request
Delete(string url, object headers = null): HttpResponse | DELETE request
Post(string url, object body, string contentType = null, object headers = null): HttpResponse | POST a string or byte[] (JSON / text detected)
Put(string url, object body, string contentType = null, object headers = null): HttpResponse | PUT request
Patch(string url, object body, string contentType = null, object headers = null): HttpResponse | PATCH request
Request(string method, string url, object body = null, string contentType = null, object headers = null): HttpResponse | Any method
GetString(string url, object headers = null): string | Body of a GET (HttpRequestException unless 2xx)
GetJson(string url, object headers = null): object | GET, require 2xx, parse JSON (requires Http.Json)
PostJson(string url, object value, object headers = null): HttpResponse | Serialize value and POST application/json; inspect IsSuccess or EnsureSuccess
PutJson(string url, object value, object headers = null): HttpResponse | Serialize value and PUT application/json
PatchJson(string url, object value, object headers = null): HttpResponse | Serialize value and PATCH application/json
Download(string url, string path, object headers = null): int | Stream a body to a file; bytes written
UrlEncode(string text): string | Percent-encode for URLs / forms
Timeout: int | Request timeout in ms
UserAgent: string | User-Agent header
FollowRedirects: bool | Follow 301/302/303/307/308

@HttpResponse | Reply of an Http request
.StatusCode: int | 200, 404, ...
.Reason: string | "OK", "Not Found", ...
.IsSuccess: bool | Status 200..299
.Body: string | Body as text
.Json: object | Parse Body as JSON (does not check HTTP status; requires Http.Json)
.Bytes: byte[] | Body as bytes
.ContentLength: int | Body size
.ContentType: string | Content-Type header
.Url: string | Final URL (after redirects)
.Headers: string | All response headers
.Header(string name): string | One header (case-insensitive), null when missing
.EnsureSuccess(): HttpResponse | Throws HttpRequestException unless 2xx

@Json | Lightweight JSON text ↔ data values (no typed class reconstruction)
Parse(string json): object | Dictionary, List, native number, string, bool or null; strict Unicode/grammar; FormatException on bad input
TryParse(string json, out object result): bool | False and null for malformed/null/non-string input; OOM still propagates
Stringify(object value, object indent = false, bool includeHidden = false): string | Nested collections, fields and computed getters; indent true=2 or integer 0..8; cycles throw
Deserialize(string json): object | Same as Parse; generic type arguments do not reconstruct classes
Serialize(object value, object indent = false, bool includeHidden = false): string | Same as Stringify; underscore-prefixed fields/getters omitted unless includeHidden
Escape(string text): string | Quoted, escaped UTF-8 string (null → JSON null); rejects invalid Unicode

@JsonSerializer | Json aliases, not the full System.Text.Json framework
Serialize(object value, object indent = false, bool includeHidden = false): string | Same as Json.Serialize
Deserialize(string json): object | Same as Json.Parse
Parse(string json): object | Same as Json.Parse
TryParse(string json, out object result): bool | Same as Json.TryParse
Stringify(object value, object indent = false, bool includeHidden = false): string | Same as Json.Stringify
Escape(string text): string | Same as Json.Escape

@Encoding | Text ↔ bytes (System.Text)
UTF8: Encoding | UTF-8 encoding
ASCII: Encoding | ASCII encoding
.GetBytes(string text): byte[] | Encodes the text
.GetString(byte[] bytes): string | Decodes the bytes
.GetString(byte[] bytes, int index, int count): string | Decodes a range
.GetByteCount(string text): int | Encoded size

@int | Integer (long, short, byte, uint … share one 32-bit signed representation; >>> is the logical shift)
Parse(string s): int | Parses (throws FormatException)
TryParse(string s, out int result): bool | Parses without throwing
const MaxValue: int | 2147483647
const MinValue: int | -2147483648
.ToString(): string | Decimal text
.ToString(string format): string | "X4", "D3", "N0" …
.CompareTo(int other): int | <0, 0, >0

@double | 64-bit floating point
Parse(string s): double | Parses (throws FormatException)
TryParse(string s, out double result): bool | Parses without throwing
IsNaN(double d): bool | Not a number
IsInfinity(double d): bool | ±∞
IsPositiveInfinity(double d): bool | +∞
IsNegativeInfinity(double d): bool | -∞
IsFinite(double d): bool | Neither NaN nor ∞
const NaN: double | Not a number
const PositiveInfinity: double | +∞
const NegativeInfinity: double | -∞
const Epsilon: double | Smallest positive value
const MaxValue: double | Largest value
.ToString(string format): string | "F2", "E3", "0.00" …

@bool | true / false
Parse(string s): bool | "true" / "false"
TryParse(string s, out bool result): bool | Parses without throwing

@char | A character
IsDigit(char c): bool | 0-9
IsLetter(char c): bool | Letter
IsLetterOrDigit(char c): bool | Letter or digit
IsWhiteSpace(char c): bool | Space, tab, line ending…
IsUpper(char c): bool | Upper case
IsLower(char c): bool | Lower case
IsPunctuation(char c): bool | Punctuation
IsControl(char c): bool | Control character
IsNumber(char c): bool | Number
IsSymbol(char c): bool | Symbol
ToUpper(char c): char | Upper case
ToLower(char c): char | Lower case
GetNumericValue(char c): double | '7' → 7
Parse(string s): char | The single character

@Scheduler | Cooperative jobs that run while the script sleeps (modules/sched)
Every(int ms, Action action): int | Runs action every ms milliseconds; returns the job id
Every(int ms, Action action, int maxFailures): int | Cancels the job after maxFailures exceptions
After(int ms, Action action): int | Runs action once after ms milliseconds
Cancel(int id): void | Stops a job
CancelAll(): void | Stops every job
Count: int | Number of active jobs

@Hal | The board (modules/hal)
const Board: string | Board / port name
const ApiVersion: int | HAL API version
Micros: long | Microseconds since start
UniqueId: string | Chip id as hex
CpuHz: int | CPU clock in Hz
DroppedEvents: int | Events lost because the queue was full
Has(string name): bool | Side-effect-free firmware capability query: "Wifi", "Ble", "EspNow", "Ble.Central", "Ble.ScanParameters", "Compiler", "Filesystem", and "GPIO", "GPIO.IRQ", "UART", "I2C", "SPI", "ADC", "DAC", "PWM", "Timer", "I2S", "QSPI", "CAN", "Watchdog", "RTC", "SPI.Configure" (full settings objects), "DMA", "DMA.SPI" (background transfers), or a driver: "LedStrip", "ws2812"
Poll(): void | Runs pending callbacks now
Run(): void | Dispatches events forever (until Ctrl-C)
Run(int ms): void | Dispatches events for ms milliseconds
DelayMicroseconds(int us): void | Busy-waits
Reset(): void | Restarts the chip
OnEvent(int n, Action<int, int> handler): void | Handler(source, value) for user event n (C: mcs_hal_post)
Post(int n): void | Queues user event n
Post(int n, int source, int value): void | Queues user event n with data
Options(): List<string> | Board-specific settings for configuration objects ("SPI.queue_size (1..16): ...")
Options(string peripheral): List<string> | Board options of one peripheral: "GPIO", "UART", "SPI"…

@Power | Cooperative delay and capability-gated hardware sleep (docs/POWER.md)
Sleep(int milliseconds): void | Cooperative VM delay, supports cancellation; no hardware low-power promise
SetWakePin(object pin, bool high): void | Select one input and active level; validated again for the chosen sleep mode
ClearWakePin(): void | Clear the VM wake-input selection, not native SDK configuration
WakePin: int | Selected numeric input, -1 if none
WakeHigh: bool | Selected active level
LightSleep(int milliseconds = 0): void | Hardware light/CPU-idle sleep; timeout or pin required, Studio cannot cancel until wake
DeepSleep(int milliseconds = 0): void | Reset-on-wake; RAM/VM state lost, timeout or pin required
Shutdown(): void | Actual port shutdown/poweroff, not a deep-sleep alias; requires board-specific recovery

@GPIO | Digital pins (modules/hal)
const Input: int | Input mode
const Output: int | Push-pull output mode
const InputPullUp: int | Input with pull-up
const InputPullDown: int | Input with pull-down
const OpenDrain: int | Open-drain output
const Analog: int | Analog (ADC) mode
const Rising: int | Edge: low → high
const Falling: int | Edge: high → low
const Both: int | Both edges
const PullNone: int | GpioConfig.Pull: no pull resistor
const PullUp: int | GpioConfig.Pull: pull-up
const PullDown: int | GpioConfig.Pull: pull-down
const PullBoth: int | GpioConfig.Pull: bus keeper (where supported)
const SpeedLow: int | GpioConfig.Speed: slow edges
const SpeedMedium: int | GpioConfig.Speed
const SpeedHigh: int | GpioConfig.Speed
const SpeedVeryHigh: int | GpioConfig.Speed: fastest edges
Configure(pin pin, GpioConfig cfg): void | Mode, pull, speed, drive, Schmitt, hold, invert, initial level, alternate function, filter
Mode(pin pin, int mode): void | Sets the mode: GPIO.Input, Output, InputPullUp, InputPullDown, OpenDrain, Analog
Write(pin pin, bool value): void | Drives the pin: true / 1 = high, false / 0 = low
Write(pin pin, int value): void | Drives the pin: 0 = low, anything else = high
Read(pin pin): bool | Level of the pin
Toggle(pin pin): void | Inverts an output
Pin(string name): int | Pin number for a name: "PA5", "GPIO21", "LED", "A0"
OnChange(pin pin, int edge, Action<bool> handler): void | Interrupt callback (level), edge GPIO.Rising / Falling / Both
OnChange(pin pin, int edge, Action<int, bool> handler): void | Interrupt callback (pin, level)
Off(pin pin): void | Removes the pin's callback
PulseIn(pin pin, bool level): int | Length of the next pulse in µs (ultrasonic sensors, IR)
PulseIn(pin pin, bool level, int timeoutUs): int | Pulse length in µs, 0 on timeout

@Pin | A GPIO pin object (modules/hal)
new(pin pin) | A pin by number or name ("PA5", "GPIO21", "LED")
new(pin pin, int mode) | A pin set to GPIO.Output, Input, InputPullUp…
new(pin pin, GpioConfig cfg) | A pin with full electrical settings
.Configure(GpioConfig cfg): void | Applies new settings
.Write(bool value): void | Drives the pin
.Read(): bool | Reads the level
.Toggle(): void | Inverts the output
.High(): void | Drives high
.Low(): void | Drives low
.SetMode(int mode): void | Changes the mode
.OnChange(int edge, Action<bool> handler): void | Interrupt callback (level)
.Value: bool | Level (get / set)
.Number: int | Pin number

@RotaryEncoder | Hardware-first A/B quadrature: PCNT / TIM / PIO, GPIO fallback
new(pin a, pin b, int steps = 4, pin button = -1, bool reverse = false) | Prefers native hardware; GPIO interrupts only when unavailable; steps 1, 2 or 4 transitions/count
.Backend: string | Selected backend: PCNT, TIM, PIO, or Software (explicit fallback)
.Position: int | Signed 32-bit position from the native counter or GPIO fallback; saturates at min/max
.ButtonPressed: bool | Raw active-low optional button (false when omitted); debounce in the application
.Reset(int position = 0): void | Sets position and rebaselines without stopping or clearing the hardware counter
.Dispose(): void | Disables both interrupts and releases the pins; idempotent

@UART | Serial ports (modules/hal)
const ParityNone: int | No parity
const ParityOdd: int | Odd parity
const ParityEven: int | Even parity
Open(int port, int baud): void | Opens the port, 8N1
Open(int port, int baud, int dataBits, int parity, int stopBits): void | Opens with framing: parity UART.ParityNone / ParityOdd / ParityEven
Open(int port, UartConfig cfg): void | Opens with pins, buffers, inversion, RS-485…
WriteAsync(int port, object data): DmaTransfer | Sends in the background
ReadAsync(int port, DmaBuffer rx): DmaTransfer | Completes when rx is full
Close(int port): void | Closes the port
Write(int port, string data): void | Sends text
Write(int port, byte[] data): void | Sends bytes
WriteLine(int port): void | Sends a line ending
WriteLine(int port, string text): void | Sends text and a line ending
Read(int port, int max): byte[] | Bytes that arrived (up to max)
Read(int port, int max, int timeoutMs): byte[] | Waits up to timeoutMs for data
ReadString(int port, int max): string | Text that arrived
ReadString(int port, int max, int timeoutMs): string | Waits up to timeoutMs for text
ReadLine(int port): string | One line without the ending, or null (1 s timeout)
ReadLine(int port, int timeoutMs): string | One line or null after timeoutMs
Available(int port): int | Bytes waiting
OnReceive(int port, Action<int> handler): void | Callback(available) when data arrives

@I2C | I²C buses (modules/hal)
Open(int bus): void | Opens at 100 kHz
Open(int bus, int hz): void | Opens at hz (100000, 400000…)
Open(int bus, I2cConfig cfg): void | Opens with pins, pull-ups, filters, timeout
WriteAsync(int bus, int addr, object data): DmaTransfer | Background write
ReadAsync(int bus, int addr, DmaBuffer rx): DmaTransfer | Background read
WriteReadAsync(int bus, int addr, object tx, DmaBuffer rx): DmaTransfer | Background write + read
Write(int bus, int addr, byte[] data): void | Writes bytes (7-bit address)
Read(int bus, int addr, int n): byte[] | Reads n bytes
WriteRead(int bus, int addr, byte[] data, int n): byte[] | Write, repeated start, read n bytes
ReadRegister(int bus, int addr, int reg): int | One register byte
ReadRegisters(int bus, int addr, int reg, int n): byte[] | n bytes from reg
WriteRegister(int bus, int addr, int reg, int value): void | Writes one register byte
WriteRegister(int bus, int addr, int reg, byte[] data): void | Writes bytes starting at reg
Scan(int bus): List<int> | Addresses that answer

@I2cDevice | One I²C device (modules/hal)
new(int bus, int address) | The device at the 7-bit address
.Write(byte[] data): void | Writes bytes
.Read(int n): byte[] | Reads n bytes
.WriteRead(byte[] data, int n): byte[] | Write then read n bytes
.ReadRegister(int reg): int | One register byte
.ReadRegisters(int reg, int n): byte[] | n bytes from reg
.WriteRegister(int reg, int value): void | Writes one register byte
.Address: int | 7-bit address
.Bus: int | Bus number

@SPI | SPI buses (modules/hal)
Open(int bus): void | 1 MHz, mode 0
Open(int bus, int hz, int mode = 0, int lsbFirst = 0): void | Clock, mode 0-3, bit order
Open(int bus, SpiConfig cfg): void | Opens with pins, CS, 3-wire, DMA…
TransferAsync(int bus, object tx, DmaBuffer rx): DmaTransfer | Full duplex in the background
TransferAsync(int bus, object tx, DmaBuffer rx, pin csPin): DmaTransfer | Same, drives CS
WriteAsync(int bus, object data): DmaTransfer | Sends in the background (DmaBuffer: any size)
WriteAsync(int bus, object data, pin csPin): DmaTransfer | Same, drives CS
ReadAsync(int bus, DmaBuffer rx): DmaTransfer | Reads in the background
ReadAsync(int bus, DmaBuffer rx, pin csPin): DmaTransfer | Same, drives CS
Transfer(int bus, byte[] data): byte[] | Full duplex
Transfer(int bus, byte[] data, pin csPin): byte[] | Full duplex, drives CS
Write(int bus, byte[] data): void | Sends bytes
Write(int bus, byte[] data, pin csPin): void | Sends bytes, drives CS
Read(int bus, int n): byte[] | Reads n bytes
Read(int bus, int n, pin csPin): byte[] | Reads n bytes, drives CS

@SpiDevice | One SPI device with its own CS and settings (modules/hal)
new(int bus, pin csPin, int hz, int mode) | CS handled, bus re-configured per device
new(int bus, pin csPin, SpiConfig cfg) | Device with full bus settings
.Transfer(byte[] data): byte[] | Full duplex
.Write(byte[] data): void | Sends bytes
.Read(int n): byte[] | Reads n bytes
.WriteRead(byte[] cmd, int n): byte[] | Sends cmd then reads n bytes

@ADC | Analog inputs (modules/hal)
Resolution: int | Bits (12 on ESP32)
ReferenceMillivolts: int | Full-scale voltage
Read(int ch): int | Raw reading
ReadMillivolts(int ch): int | Calibrated millivolts
ReadVoltage(int ch): double | Volts
ReadAverage(int ch): int | Average of 16 raw readings
ReadAverage(int ch, int n): int | Average of n raw readings
Configure(int ch, AdcConfig cfg): void | Resolution, attenuation (input range), oversampling

@DAC | Analog outputs (modules/hal)
Resolution: int | Bits
Write(int ch, int value): void | Raw value
WriteMillivolts(int ch, int mV): void | Output voltage

@PWM | PWM channels: LEDs, motors, servos, buzzers (modules/hal)
Set(int ch, int hz, double duty): void | Frequency and duty 0.0-1.0
SetPermille(int ch, int hz, int permille): void | Duty 0-1000
Configure(int ch, PwmConfig cfg): void | Pin, resolution, inverted, centre-aligned
SetPulse(int ch, int hz, int pulseUs): void | High time in µs
Servo(int ch, double degrees): void | 50 Hz servo, 0-180°, 500-2500 µs (the Servo class does more)
Servo(int ch, double degrees, int minUs, int maxUs): void | Servo with its pulse range
Tone(int ch, int hz): void | 50 % square wave (buzzer)
Stop(int ch): void | Output off

@Timer | Hardware timers - callbacks run between statements (modules/hal)
Start(int id, int periodUs, Action fn): void | Periodic callback every periodUs µs
Once(int id, int delayUs, Action fn): void | One callback after delayUs µs
Stop(int id): void | Stops the timer

@Watchdog | Task watchdog (modules/hal)
Start(int timeoutMs): void | Starts it - feed it or the board resets
Feed(): void | Restarts the countdown

@RTC | Real-time clock (modules/hal)
Now: long | Unix time in seconds
Set(long seconds): void | Sets the clock (Unix seconds)

@I2S | Digital audio (modules/hal)
const Transmit: int | Output (DAC / amplifier)
const Receive: int | Input (microphone)
const Duplex: int | Both directions
Open(int bus, int rate, int bits, int channels): void | Transmit at rate Hz, bits 16/24/32, 1-2 channels
Open(int bus, int rate, int bits, int channels, int direction): void | I2S.Transmit, Receive or Duplex
Open(int bus, int rate, int bits, int channels, int direction, int format): void | With the frame format
WriteSamples(int bus, int[] samples): void | Signed samples, packed to bits
ReadSamples(int bus, int n): int[] | n signed samples
Write(int bus, byte[] data): void | Raw bytes (≤ 256 per call)
Write(int bus, byte[] data, int timeoutMs): void | Raw bytes with a timeout
Read(int bus, int n): byte[] | Raw bytes
Read(int bus, int n, int timeoutMs): byte[] | Raw bytes with a timeout
Close(int bus): void | Stops the bus

@QSPI | Quad SPI / NOR flash style commands (modules/hal)
Open(int bus): void | Default clock
Open(int bus, int hz): void | Clock in Hz
Command(int bus, int instr): void | Instruction only
Command(int bus, int instr, int address): void | Instruction + address
Read(int bus, int instr, int address, int n): byte[] | Reads n bytes (address -1 = none)
Read(int bus, int instr, int address, int n, int dummy, int dataLines, int addrBytes): byte[] | Every option
Write(int bus, int instr, int address, byte[] data): void | Writes bytes
Write(int bus, int instr, int address, byte[] data, int dataLines, int addrBytes): void | Every option
Transfer(int bus, int instr, int instrLines, int address, int addrBytes, int addrLines, int dummy, int dataLines, object bytesOrCount): byte[] | Every phase explicit
Open(int bus, QspiConfig cfg): void | Opens with pins, mode, DDR, flash size
ReadAsync(int bus, int instr, int address, DmaBuffer rx): DmaTransfer | Background read (address -1 = none)
WriteAsync(int bus, int instr, int address, object data): DmaTransfer | Background write

@CAN | Classic CAN / TWAI or external MCP2515 (modules/hal)
Open(int bus): void | 500 kbit/s
Open(int bus, int bitrate): void | Bit rate (125000, 250000, 500000, 1000000)
Send(int bus, int id, byte[] data): void | Standard 11-bit frame
Send(int bus, int id, byte[] data, bool extended): void | 29-bit id when extended
Send(int bus, CanFrame frame): void | Sends a frame
Receive(int bus): CanFrame | Next frame or null
Receive(int bus, int timeoutMs): CanFrame | Waits up to timeoutMs
OnReceive(int bus, Action<int> handler): void | VM callback; pending is a lower bound, drain Receive until null; null unsubscribes
Open(int bus, CanConfig cfg): void | Native bitrate, pins and Normal / ListenOnly / Loopback mode
OpenMcp2515(int bus, int spiBus, int csPin): void | External SPI CAN, 16 MHz crystal and 500 kbit/s defaults
OpenMcp2515(int bus, int spiBus, int csPin, int oscillatorHz, int bitrate, int mode): void | 8/16/20 MHz crystal; configure SPI pins first; requires controller plus transceiver
Close(int bus): void | Stops controller, discards lookahead, unsubscribes callback; idempotent
Send(int bus, CanFrame frame, int timeoutMs): void | Cooperative queue-space timeout, not a delivery acknowledgment
Send(int bus, int id, byte[] data, bool extended, int timeoutMs): void | Explicit queue timeout; zero is nonblocking
.Normal: int | Normal active CAN
.ListenOnly: int | Receive without sending or acknowledging
.Loopback: int | Test mode; legacy TWAI still requires TX-to-RX wiring

@CanConfig | Native classic CAN settings (HAL v5)
new() | 500000 bit/s, board-default pins, normal mode
new(int bitrate) | Nominal bitrate
.Bitrate: int | 1..1000000; must be supported by the backend clock
.Tx: int | ESP32 TX pin, -1 is board default; STM32/Zephyr use firmware wiring
.Rx: int | ESP32 RX pin, -1 is board default
.Mode: int | CAN.Normal, CAN.ListenOnly or CAN.Loopback

@CanFrame | A CAN frame
new(int id, byte[] data) | Standard frame
new(int id, byte[] data, bool extended) | Extended (29-bit) when true
new(int id, byte[] data, bool extended, bool remote) | RTR uses array length as requested DLC (0..8), ignores content
.Id: int | Identifier
.Extended: bool | 29-bit id
.Remote: bool | Remote request; Data is empty, Length preserves requested DLC
.Length: int | Data length
.Data: byte[] | Payload

@GpioConfig | GPIO settings for GPIO.Configure / new Pin (modules/hal, HAL v3)
new() | Everything at the board default
new(int mode) | GPIO.Input, Output, OpenDrain…
new(int mode, int pull) | Mode and GPIO.PullUp / PullDown / PullNone
.Mode: int | GPIO.Input, Output, OpenDrain, Analog…
.Pull: int | GPIO.PullNone, PullUp, PullDown, PullBoth
.Speed: int | GPIO.SpeedLow … SpeedVeryHigh (slew rate)
.DriveMilliamps: int | Drive strength in mA, 0 = default
.Schmitt: bool? | Input hysteresis, null = default
.Hold: bool? | Keep the level through sleep / reset
.Invert: bool? | Invert in hardware
.Initial: int | Level before the pin starts driving, -1 = keep
.Alternate: int | Alternate function number, -1 = GPIO
.FilterNs: int | Input glitch filter in ns
.Set(string name, int value): GpioConfig | A property or board option by name
.Get(string name): int | Value of a property or option
.Reset(): void | Back to all defaults

@UartConfig | UART settings for UART.Open (modules/hal, HAL v3)
new(int baud) | Baud rate, 8N1
new(int baud, pin tx, pin rx) | With pins
.Baud: int | Baud rate
.DataBits: int | 5-9
.Parity: int | UART.ParityNone / Odd / Even
.StopBits: int | 1 or 2
.FlowControl: bool | RTS/CTS
.Tx: pin | TX pin
.Rx: pin | RX pin
.Rts: pin | RTS pin
.Cts: pin | CTS pin
.RxBuffer: int | Receive buffer bytes
.TxBuffer: int | Transmit buffer bytes
.InvertTx: bool | Inverted TX level
.InvertRx: bool | Inverted RX level
.Rs485: bool | Half duplex, RTS drives the transceiver
.Dma: bool? | DMA, null = default
.RxTimeout: int | Idle symbols before data is reported
.Set(string name, int value): UartConfig | A property or board option by name

@I2cConfig | I²C settings for I2C.Open (modules/hal, HAL v3)
new(int hz) | Clock
new(int hz, pin sda, pin scl) | With pins
.Frequency: int | Clock in Hz (up to 1 MHz)
.Sda: pin | SDA pin
.Scl: pin | SCL pin
.PullUps: bool? | Internal pull-ups
.Dma: bool? | DMA
.TimeoutMs: int | Per transfer
.GlitchFilter: int | Digital filter (clock cycles)
.AnalogFilter: bool? | Analog filter
.Set(string name, int value): I2cConfig | A property or board option by name

@SpiConfig | SPI settings for SPI.Open / SpiDevice (modules/hal, HAL v3)
new(int hz) | Clock, mode 0
new(int hz, int mode) | Clock and mode 0-3
new(int hz, int mode, pin sck, pin mosi, pin miso) | With pins
.Frequency: int | Clock in Hz
.Mode: int | 0-3
.Bits: int | Bits per word
.LsbFirst: bool | Bit order
.Sck: pin | Clock pin
.Mosi: pin | Data out pin
.Miso: pin | Data in pin
.Cs: pin | Chip select driven by MicroCS
.CsActiveHigh: bool | CS polarity
.ThreeWire: bool | Half duplex on MOSI (Write / Read only)
.Dma: bool? | DMA, null = default
.InputDelayNs: int | MISO sampling delay
.MaxTransfer: int | Largest transfer in bytes
.Set(string name, int value): SpiConfig | A property or board option ("queue_size")

@QspiConfig | Quad SPI settings for QSPI.Open (modules/hal, HAL v3)
new(int hz) | Clock
.Frequency: int | Clock in Hz
.Mode: int | 0 or 3
.Ddr: bool | Double data rate
.Dma: bool? | DMA
.FlashSizeLog2: int | Memory size 2^n bytes
.Sck: pin | Clock pin
.Cs: pin | Chip select pin
.Io0: pin | IO0 / MOSI
.Io1: pin | IO1 / MISO
.Io2: pin | IO2 / WP
.Io3: pin | IO3 / HOLD

@PwmConfig | PWM settings for PWM.Configure (modules/hal, HAL v3)
new(int hz) | Frequency
new(int hz, pin pin) | Frequency and output pin
.Frequency: int | Hz, 0 = set by PWM.Set
.Pin: pin | Output pin
.ResolutionBits: int | Duty resolution, 0 = finest
.Inverted: bool | Active-low
.CenterAligned: bool | Phase-correct counting

@AdcConfig | ADC settings for ADC.Configure (modules/hal, HAL v3)
new(int attenuation) | ADC.Atten0dB … Atten12dB
.ResolutionBits: int | 0 = native
.Attenuation: int | Input range: ADC.Atten0dB, Atten2_5dB, Atten6dB, Atten12dB
.Oversample: int | Samples averaged per reading
.SampleTime: int | ADC clock cycles

@DmaBuffer | DMA-capable memory for background transfers (modules/hal, HAL v3)
new(int size) | Zero-filled buffer
new(byte[] data) | Copy of bytes (or a string)
.Length: int | Bytes
.Busy: bool | A transfer uses it (writes throw)
.Fill(int value): void | Sets every byte
.Fill(int value, int offset, int count): void | Sets a range
.CopyFrom(byte[] data, int offset): int | Copies bytes in, returns the count
.ToArray(): byte[] | Copy of the contents
.ToArray(int offset, int count): byte[] | Copy of a range
.GetString(): string | Contents as UTF-8 text

@DmaTransfer | A transfer started by an …Async call (modules/hal, HAL v3)
.Wait(): bool | Waits for the end (throws if it failed)
.Wait(int timeoutMs): bool | false on timeout
.IsCompleted: bool | Finished
.Succeeded: bool | Finished without error
.Error: string | Reason of a failure or null
.Result: int | Bytes transferred (waits)
.OnComplete(Action<int> handler): void | Callback(bytes) on the VM thread
.Cancel(): bool | Stops it (best effort)

@Dma | Memory DMA and transfers in flight (modules/hal, HAL v3)
Copy(DmaBuffer dst, object src): int | Copies (DMA engine or CPU)
Copy(DmaBuffer dst, object src, int count, int dstOffset, int srcOffset): int | Copies a range
CopyAsync(DmaBuffer dst, object src): DmaTransfer | Background copy
Fill(DmaBuffer dst, int value): void | Fills
Fill(DmaBuffer dst, int value, int offset, int count): void | Fills a range
FillAsync(DmaBuffer dst, int value): DmaTransfer | Background fill
WaitAll(): bool | Waits for every transfer
WaitAll(int timeoutMs): bool | false on timeout
Pending: int | Transfers in flight
Available: bool | The board has a DMA engine
Channels: int | DMA channels
Alignment: int | DmaBuffer alignment
MaxTransfers: int | Transfers in flight at most

@Drivers | Device drivers compiled into the firmware (modules/drivers, mcs_driver.h)
Has(string name): bool | A driver ("ws2812", "servo", "u8g2") or a class it adds ("LedStrip", "Servo", "U8g2") is available
List: string[] | Names of the registered drivers

@LedStrip | WS2812 / WS2812B / SK6812 ("NeoPixel") addressable RGB LEDs on any pin ("ws2812" driver)
new(int pin, int count) | GRB strip (WS2812B); pin may be a name: "GP16", "NEOPIXEL"
new(int pin, int count, int order) | LedStrip.GRB, LedStrip.RGB (WS2811) or LedStrip.GRBW (SK6812 RGBW)
const GRB: int | Byte order of WS2812 / WS2812B (default)
const RGB: int | Byte order of WS2811 and some clones
const GRBW: int | SK6812 RGBW (colours 0xWWRRGGBB)
Rgb(int r, int g, int b): int | Colour 0xRRGGBB from 0..255 parts
Rgb(int r, int g, int b, int w): int | Colour 0xWWRRGGBB (RGBW strips)
Hsv(int hue, int saturation = 255, int value = 255): int | Colour from hue 0..359 (rainbows)
.this[int index]: int | Colour of one LED (0xRRGGBB); applied by Show()
.SetPixel(int index, int color): void | Sets one LED
.SetPixel(int index, int r, int g, int b): void | Sets one LED from 0..255 parts
.SetPixel(int index, int r, int g, int b, int w): void | RGBW strips
.GetPixel(int index): int | Colour of one LED
.Fill(int color): void | Every LED
.Fill(int color, int first, int count): void | A range of LEDs
.Clear(): void | All off (call Show() to apply)
.Show(): void | Sends the colours to the strip
.Brightness: int | 0..255 scale applied by Show() (default 255)
.Count: int | Number of LEDs
.Pin: int | GPIO of the strip
.Dispose(): void | Frees the pixel buffer

@Servo | Hobby servos and ESCs on a PWM channel, 50 Hz ("servo" driver)
new(int channel) | PWM channel; 500-2500 µs = 0-180°
new(int channel, int minUs, int maxUs) | Pulse range of your servo (Arduino default: 544, 2400)
new(int channel, int minUs, int maxUs, int maxAngle) | Pulse range and travel in degrees
.Angle: int | Degrees 0..MaxAngle; assign an int or double (ArgumentOutOfRangeException outside)
.Write(double angle): void | Same as assigning Angle
.Read(): int | Last angle written
.Pulse: int | Pulse width in µs, MinPulse..MaxPulse; assign to drive the raw width
.WritePulse(int us): void | Same as assigning Pulse
.MoveTo(double angle, int ms): void | Eased move in 20 ms frames; blocks (callbacks keep running)
.Attach(): void | Resumes the last pulse after Detach()
.Detach(): void | Stops the signal (the servo goes limp); Stop() is the same
.Attached: bool | The signal is on
.MinPulse: int | Shortest pulse in µs
.MaxPulse: int | Longest pulse in µs
.MaxAngle: int | Travel in degrees
.Channel: int | PWM channel
.Dispose(): void | Stops the signal and releases the object

@U8g2 | Monochrome OLED / LCD / e-paper graphics with u8g2 (firmware built with u8g2, docs/U8G2.md)
new(string display) | Display on hardware I2C bus 0, address 0x3C (u8g2 name, e.g. "ssd1306_i2c_128x64_noname")
I2C(string display, int bus = 0, int address = 0x3C, int reset = -1): U8g2 | Hardware I2C; 7-bit address (0x3D), optional reset pin
SoftI2C(string display, int scl, int sda, int reset = -1): U8g2 | Bit-banged I2C on any two pins
SPI(string display, int bus, int cs, int dc, int reset = -1): U8g2 | Hardware SPI (mode and clock from u8g2)
SoftSPI(string display, int clock, int data, int cs, int dc, int reset = -1): U8g2 | Bit-banged 4-wire SPI
Soft3WireSPI(string display, int clock, int data, int cs, int reset = -1): U8g2 | 3-wire SPI (9-bit, no DC pin)
Parallel8080(string display, int[] data, int wr, int cs, int dc, int reset = -1): U8g2 | 8080 parallel bus (8 data pins)
Parallel6800(string display, int[] data, int e, int cs, int dc, int reset = -1): U8g2 | 6800 parallel bus (8 data pins)
KS0108(string display, int[] data, int e, int dc, int cs0, int cs1, int cs2, int reset = -1): U8g2 | KS0108 128x64 LCDs
Displays: string[] | Displays compiled into the firmware
Fonts: string[] | U8g2 fonts in flash (others load from /fonts)
const EventNone: int | Menu key: nothing pressed
const EventSelect: int | Menu key: select
const EventNext: int | Menu key: next
const EventPrev: int | Menu key: previous
const EventHome: int | Menu key: home / abort
const EventUp: int | Menu key: up
const EventDown: int | Menu key: down
const R0: int | Rotation 0°
const R1: int | Rotation 90°
const R2: int | Rotation 180°
const R3: int | Rotation 270°
const Mirror: int | Mirrored
const MirrorVertical: int | Mirrored vertically
const ColorClear: int | Draw colour 0
const ColorSet: int | Draw colour 1
const ColorXor: int | Draw colour 2 (XOR)
const DrawUpperRight: int | Circle / ellipse quadrant
const DrawUpperLeft: int | Circle / ellipse quadrant
const DrawLowerLeft: int | Circle / ellipse quadrant
const DrawLowerRight: int | Circle / ellipse quadrant
const DrawAll: int | All quadrants
const BtnBW0: int | Button: no border
const BtnBW1: int | Button: 1 px border
const BtnBW2: int | Button: 2 px border
const BtnBW3: int | Button: 3 px border
const BtnShadow0: int | Button shadow
const BtnShadow1: int | Button shadow 1 px
const BtnShadow2: int | Button shadow 2 px
const BtnInv: int | Inverted button
const BtnHCenter: int | Centre on x
const BtnXFrame: int | Double frame
.Begin(): void | Init, clear and wake the display
.Begin(int select, int next, int prev): void | ... and menu buttons to GND (pull-ups by u8g2)
.Begin(int select, int next, int prev, int up, int down, int home): void | Six menu buttons
.InitDisplay(): void | u8g2 init sequence only
.ClearDisplay(): void | Clears the display
.Clear(): void | Clears display and buffer, cursor home
.RefreshDisplay(): void | Refresh (e-paper)
.SetPowerSave(bool on): void | true = display off (picture kept)
.Sleep(): void | Display off
.Wake(): void | Display on
.SetContrast(int value): void | 0..255
.SetFlipMode(bool on): void | Rotates 180° in the controller
.BusClock: int | Bus clock in Hz used by Begin()
.SetBusClock(int hz): void | Bus clock for the next Begin()
.I2CAddress: int | 7-bit I2C address
.SetI2CAddress(int address): void | I2C address for the next Begin()
.Display: string | u8g2 setup name
.Bus: string | I2C, SPI, ...
.Cols: int | Width in 8x8 tiles
.Rows: int | Height in 8x8 tiles
.SetFont(string name): void | Built-in font, else /fonts/u8g2_font_<name>.bin, or a path
.SetFont(byte[] font): void | Font data from bytes
.Font: string | Current font name
.SetCursor(int x, int y): void | Cursor for Print (U8g2: pixels, baseline; U8x8: tiles)
.Home(): void | Cursor to 0, 0
.CursorX: int | Cursor x
.CursorY: int | Cursor y
.Print(object value): void | Prints at the cursor (\n = new line)
.Println(): void | New line
.Println(object value): void | Prints and starts a new line
.UserInterfaceSelectionList(string title, int start, string items): int | Menu ("a\nb\nc"); 1-based choice, 0 = home
.UserInterfaceMessage(string title1, string title2, string title3, string buttons): int | Message box (" Ok \n Cancel "); 1-based button
.UserInterfaceInputValue(string title, string pre, int value, int lo, int hi, int digits, string post): int | Number input; -1 = aborted
.GetMenuEvent(): int | Polls the menu keys once (EventNone ...)
.SetMenuInput(Func<int> keys): void | Menu keys from code (encoder, remote); null = the Begin() pins
.LogBegin(int cols, int rows, bool autoRedraw = true): void | Text terminal (u8log)
.Log(string text): void | Appends text; scrolls
.LogLine(): void | New line in the log
.LogLine(string text): void | Text and a new line
.DrawLog(int x, int y): void | Draws the log (U8g2: first baseline; U8x8: tile)
.LogRedrawOnNewline: bool | Redraw the screen on each newline
.LogLineHeightOffset: int | Extra pixels between log lines
.Dispose(): void | Frees the buffers; the picture stays
.ClearBuffer(): void | Clears the frame buffer
.SendBuffer(): void | Sends the frame buffer to the display
.UpdateDisplay(): void | Sends the buffer (no clear)
.UpdateDisplayArea(int tx, int ty, int tw, int th): void | Sends only these tiles (8x8 blocks)
.SetAutoPageClear(bool on): void | Page mode: clear each page before drawing
.FirstPage(): void | Page loop start (names ending _1 / _2)
.NextPage(): bool | Next page; false when done
.Draw(Action draw): void | Runs the page loop (or clear + send) around your drawing code
.Draw(Action<U8g2> draw): void | Same, the display is passed in
.BufferSize: int | Frame buffer bytes
.FullBuffer: bool | true for _f displays
.BufferTileWidth: int | Buffer width in tiles
.BufferTileHeight: int | Buffer height in tiles (per page)
.BufferCurrTileRow: int | Tile row of the current page
.GetBuffer(): byte[] | Copy of the frame buffer
.SetBuffer(byte[] data): void | Restores a saved buffer
.GetPixel(int x, int y): int | 0 or 1 in the buffer
.Dump(): void | Buffer as text art on the console
.WriteBufferPBM(): void | Buffer as PBM image on the console
.WriteBufferXBM(): void | Buffer as XBM C array on the console
.DrawPixel(int x, int y): void | One pixel
.DrawHLine(int x, int y, int w): void | Horizontal line
.DrawVLine(int x, int y, int h): void | Vertical line
.DrawHVLine(int x, int y, int len, int dir): void | dir 0 right, 1 down, 2 left, 3 up
.DrawLine(int x0, int y0, int x1, int y1): void | Line
.DrawBox(int x, int y, int w, int h): void | Filled rectangle
.DrawFrame(int x, int y, int w, int h): void | Rectangle outline
.DrawRBox(int x, int y, int w, int h, int r): void | Filled rounded rectangle
.DrawRFrame(int x, int y, int w, int h, int r): void | Rounded rectangle outline
.DrawCircle(int x, int y, int r): void | Circle
.DrawCircle(int x, int y, int r, int quadrants): void | Parts of a circle (U8g2.DrawUpperRight | ...)
.DrawDisc(int x, int y, int r): void | Filled circle
.DrawDisc(int x, int y, int r, int quadrants): void | Parts of a filled circle
.DrawEllipse(int x, int y, int rx, int ry): void | Ellipse
.DrawEllipse(int x, int y, int rx, int ry, int quadrants): void | Parts of an ellipse
.DrawFilledEllipse(int x, int y, int rx, int ry): void | Filled ellipse
.DrawFilledEllipse(int x, int y, int rx, int ry, int quadrants): void | Parts of a filled ellipse
.DrawArc(int x, int y, int r, int start, int end): void | Arc; angles 0..255 = full turn
.DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2): void | Filled triangle
.DrawPolygon(int[] xy): void | Filled polygon from x, y pairs
.DrawXBM(int x, int y, int w, int h, byte[] bitmap): void | XBM bitmap (LSB = left)
.DrawXBMP(int x, int y, int w, int h, byte[] bitmap): void | XBM bitmap (same as DrawXBM)
.DrawBitmap(int x, int y, int bytesPerRow, int h, byte[] bitmap): void | Bitmap, MSB = left
.SetBitmapMode(int transparent): void | 1 = only set bits are drawn
.DrawStr(int x, int y, string text): int | Text at the baseline; returns the width
.DrawUTF8(int x, int y, string text): int | UTF-8 text; returns the width
.DrawGlyph(int x, int y, int code): int | One glyph (icon fonts: codes)
.DrawStrX2(int x, int y, string text): int | Double-size text
.DrawUTF8X2(int x, int y, string text): int | Double-size UTF-8 text
.DrawGlyphX2(int x, int y, int code): int | Double-size glyph
.DrawUTF8Line(int x, int y, int w, string text, int border, int inverted): void | Text bar of width w
.DrawUTF8Lines(int x, int y, int w, int lineHeight, string text): int | Centred lines (\n); returns the height
.DrawButtonUTF8(int x, int y, int flags, int width, int padH, int padV, string text): void | Button (U8g2.BtnBW1 | BtnInv ...)
.DrawButtonFrame(int x, int y, int flags, int width, int padH, int padV): void | Button frame around existing text
.GetStrWidth(string text): int | Width in pixels
.GetUTF8Width(string text): int | Width of UTF-8 text
.GetXOffsetUTF8(string text): int | Left offset of the first glyph
.GetXOffsetGlyph(int code): int | Left offset of a glyph
.GetGlyphWidth(int code): int | Advance width of a glyph
.IsGlyph(int code): bool | The font has this glyph
.GetAscent(): int | Font ascent (upstream u8g2 API)
.GetDescent(): int | Font descent, negative (upstream u8g2 API)
.GetMaxCharHeight(): int | Tallest glyph (upstream u8g2 API)
.GetMaxCharWidth(): int | Widest glyph (upstream u8g2 API)
.Ascent: int | Compatibility alias for GetAscent()
.Descent: int | Compatibility alias for GetDescent()
.MaxCharHeight: int | Compatibility alias for GetMaxCharHeight()
.MaxCharWidth: int | Compatibility alias for GetMaxCharWidth()
.SetFontMode(int transparent): void | 1 = transparent text background
.SetFontDirection(int dir): void | 0 normal, 1 down, 2 upside down, 3 up
.SetFontPosBaseline(): void | y = baseline (default)
.SetFontPosBottom(): void | y = bottom of the text
.SetFontPosTop(): void | y = top of the text
.SetFontPosCenter(): void | y = middle of the text
.SetFontRefHeightAll(): void | Ascent / descent over all glyphs
.SetFontRefHeightExtendedText(): void | ... over text glyphs incl. extended
.SetFontRefHeightText(): void | ... over text glyphs
.SetDrawColor(int color): void | 0 clear, 1 set, 2 XOR
.DrawColor: int | Current draw colour
.SetClipWindow(int x0, int y0, int x1, int y1): void | Draw only inside this window
.SetMaxClipWindow(): void | Whole display again
.IsIntersection(int x0, int y0, int x1, int y1): bool | The box is inside the clip window
.SetDisplayRotation(int rotation): void | U8g2.R0..R3, Mirror, MirrorVertical
.Width: int | Width in pixels (after rotation)
.Height: int | Height in pixels

@U8x8 | Text-only u8g2 output: 8x8 tiles straight to the display, no frame buffer (docs/U8G2.md)
new(string display) | Display on hardware I2C bus 0, address 0x3C (u8g2 name, e.g. "ssd1306_i2c_128x64_noname")
I2C(string display, int bus = 0, int address = 0x3C, int reset = -1): U8x8 | Hardware I2C; 7-bit address (0x3D), optional reset pin
SoftI2C(string display, int scl, int sda, int reset = -1): U8x8 | Bit-banged I2C on any two pins
SPI(string display, int bus, int cs, int dc, int reset = -1): U8x8 | Hardware SPI (mode and clock from u8g2)
SoftSPI(string display, int clock, int data, int cs, int dc, int reset = -1): U8x8 | Bit-banged 4-wire SPI
Soft3WireSPI(string display, int clock, int data, int cs, int reset = -1): U8x8 | 3-wire SPI (9-bit, no DC pin)
Parallel8080(string display, int[] data, int wr, int cs, int dc, int reset = -1): U8x8 | 8080 parallel bus (8 data pins)
Parallel6800(string display, int[] data, int e, int cs, int dc, int reset = -1): U8x8 | 6800 parallel bus (8 data pins)
KS0108(string display, int[] data, int e, int dc, int cs0, int cs1, int cs2, int reset = -1): U8x8 | KS0108 128x64 LCDs
Displays: string[] | Displays compiled into the firmware
Fonts: string[] | U8x8 fonts in flash (others load from /fonts)
const EventNone: int | Menu key: nothing pressed
const EventSelect: int | Menu key: select
const EventNext: int | Menu key: next
const EventPrev: int | Menu key: previous
const EventHome: int | Menu key: home / abort
const EventUp: int | Menu key: up
const EventDown: int | Menu key: down
.Begin(): void | Init, clear and wake the display
.Begin(int select, int next, int prev): void | ... and menu buttons to GND (pull-ups by u8g2)
.Begin(int select, int next, int prev, int up, int down, int home): void | Six menu buttons
.InitDisplay(): void | u8g2 init sequence only
.ClearDisplay(): void | Clears the display
.Clear(): void | Clears display and buffer, cursor home
.RefreshDisplay(): void | Refresh (e-paper)
.SetPowerSave(bool on): void | true = display off (picture kept)
.Sleep(): void | Display off
.Wake(): void | Display on
.SetContrast(int value): void | 0..255
.SetFlipMode(bool on): void | Rotates 180° in the controller
.BusClock: int | Bus clock in Hz used by Begin()
.SetBusClock(int hz): void | Bus clock for the next Begin()
.I2CAddress: int | 7-bit I2C address
.SetI2CAddress(int address): void | I2C address for the next Begin()
.Display: string | u8g2 setup name
.Bus: string | I2C, SPI, ...
.Cols: int | Width in 8x8 tiles
.Rows: int | Height in 8x8 tiles
.SetFont(string name): void | Built-in font, else /fonts/u8g2_font_<name>.bin, or a path
.SetFont(byte[] font): void | Font data from bytes
.Font: string | Current font name
.SetCursor(int x, int y): void | Cursor for Print (U8g2: pixels, baseline; U8x8: tiles)
.Home(): void | Cursor to 0, 0
.CursorX: int | Cursor x
.CursorY: int | Cursor y
.Print(object value): void | Prints at the cursor (\n = new line)
.Println(): void | New line
.Println(object value): void | Prints and starts a new line
.UserInterfaceSelectionList(string title, int start, string items): int | Menu ("a\nb\nc"); 1-based choice, 0 = home
.UserInterfaceMessage(string title1, string title2, string title3, string buttons): int | Message box (" Ok \n Cancel "); 1-based button
.UserInterfaceInputValue(string title, string pre, int value, int lo, int hi, int digits, string post): int | Number input; -1 = aborted
.GetMenuEvent(): int | Polls the menu keys once (EventNone ...)
.SetMenuInput(Func<int> keys): void | Menu keys from code (encoder, remote); null = the Begin() pins
.LogBegin(int cols, int rows, bool autoRedraw = true): void | Text terminal (u8log)
.Log(string text): void | Appends text; scrolls
.LogLine(): void | New line in the log
.LogLine(string text): void | Text and a new line
.DrawLog(int x, int y): void | Draws the log (U8g2: first baseline; U8x8: tile)
.LogRedrawOnNewline: bool | Redraw the screen on each newline
.LogLineHeightOffset: int | Extra pixels between log lines
.Dispose(): void | Frees the buffers; the picture stays
.ClearLine(int row): void | Clears one tile row
.FillDisplay(): void | Every pixel on
.DrawString(int col, int row, string text): void | Text at a tile position
.DrawUTF8(int col, int row, string text): void | UTF-8 text
.DrawGlyph(int col, int row, int code): void | One character
.Draw1x2String(int col, int row, string text): void | Double-height text
.Draw1x2UTF8(int col, int row, string text): void | Double-height UTF-8 text
.Draw1x2Glyph(int col, int row, int code): void | Double-height character
.Draw2x2String(int col, int row, string text): void | Double-size text
.Draw2x2UTF8(int col, int row, string text): void | Double-size UTF-8 text
.Draw2x2Glyph(int col, int row, int code): void | Double-size character
.DrawTile(int col, int row, int count, byte[] tiles): void | Raw 8x8 tiles (8 bytes each, a byte per column)
.SetInverseFont(bool on): void | Inverted text
.GetUTF8Len(string text): int | Characters (tiles) of UTF-8 text
.FontWidth: int | Font width in tiles
.FontHeight: int | Font height in tiles

@Ble | Bluetooth LE radio: advertising, scanning, GATT server, connections, security (opt-in firmware, docs/BLE.md; simulated with --sim)
const ApiVersion: int | BLE API version
Has(string feature): bool | "Ble", "Ble.Advertise", "Ble.Scan", "Ble.ScanParameters", "Ble.Server", "BleUart", "Ble.Central", "Ble.Client", "Ble.Security", "Ble.Bonds", "Ble.Parameters", "Ble.Rssi", "Ble.Read", "Ble.Write", "Ble.Subscribe"
Backend: string | "nimble", "btstack", "zephyr", "arduinoble" or "sim"
Enable(): void | Starts the stack (automatic on first use)
Disable(): void | Stops the stack (connections drop; service definitions and callbacks retained)
Reset(): void | Stops radio, invalidates old GATT objects and clears services/callbacks; preserves bonds/settings; outside callbacks only
OptionNames: string[] | Exact supported-option vocabulary, without enabling radio
CanReadOption(string name): bool | Backend implements this option getter; no radio startup
CanSetOption(string name): bool | Backend implements this option setter; no radio startup
ScanInterval: int | Scan interval ms (5..10240), default 100; requires Ble.ScanParameters
ScanWindow: int | Scan window ms (5..interval), default 50; change only while not scanning
Enabled: bool | The stack is running
Name: string | GAP device name, advertised (at most 29 characters)
Address: string | This board's address, "C0:11:22:33:44:55"
AddressType: string | Reads address-kind name; accepts 0 public / 1 static random when CanSetOption is true
TxPower: int | Transmit power (dBm)
Mtu: int | Preferred / configured ATT MTU; check CanSetOption before changing
Appearance: int | GAP appearance (0x0341 = heart-rate belt …)
Phy: int | Preferred PHY: Ble.Phy1M, Phy2M, PhyCoded
IoCapability: int | Pairing IO: Ble.IoNone (Just Works), IoDisplayOnly, IoDisplayYesNo, IoKeyboardOnly, IoKeyboardDisplay
Bonding: bool | Store keys after pairing
Mitm: bool | Require MITM protection (passkey / numeric comparison)
SecureConnections: bool | LE Secure Connections
Passkey: int | Fixed passkey 0..999999 (-1 = random)
MaxConnections: int | Connections the firmware tracks
Timeout: int | GATT client timeout (ms)
AutoAdvertise: bool | Advertise again after a peripheral connection ends (default true)
DroppedEvents: int | Events lost because the queue was full
StartAdvertising(): void | Advertises Ble.Name and the added services' UUIDs
StartAdvertising(string name): void | Advertises another name
StartAdvertising(BleAdvertisement adv): void | Advertises your data (beacons, manufacturer data …)
StopAdvertising(): void | Stops advertising
Advertising: bool | Advertising now
Scan(int ms, bool active = true): BleDevice[] | Scans and returns the devices found (scan responses merged)
StartScan(Action<BleDevice> onDevice, bool active = true, bool duplicates = false): void | Reports devices in the background
StopScan(): void | Stops a background scan
Scanning: bool | Scanning now
AddService(BleService service): void | Adds a local GATT service (while not advertising or connected)
ClearServices(): void | Removes every local service
Services: BleService[] | The local services
Connect(string address, int timeoutMs = 10000, int addressType = 0): BleConnection | Connects as central (TimeoutException)
Connections: BleConnection[] | Open connections
Connected: bool | At least one connection
OnConnect(Action<BleConnection> handler): void | A connection opened (either role)
OnDisconnect(Action<BleConnection, int> handler): void | A connection closed: (connection, HCI reason)
OnEvent(Action<int, int, int> handler): void | Every event: (Ble.Event*, connection handle, value)
OnPasskey(Func<BleConnection, int, int, int> handler): void | Pairing: (connection, Ble.Passkey*, key) → key to enter, or 1 / 0 = accept / reject
Poll(): void | Runs pending BLE callbacks now
EventName(int ev): string | Name of a Ble.Event* value
Bonds: string[] | Addresses of bonded peers
RemoveBond(string address): void | Forgets a bonded peer
ClearBonds(): void | Forgets every bond
const EventConnect: int | event
const EventDisconnect: int | event
const EventConnectFailed: int | event
const EventAdvertisingDone: int | event
const EventScanDone: int | event
const EventMtu: int | event: value = new MTU
const EventSubscribe: int | event
const EventEncrypted: int | event
const EventPairingDone: int | event
const EventConnectionUpdate: int | event
const EventIndicateDone: int | event
const EventPhy: int | event
const IoDisplayOnly: int | IO capability
const IoDisplayYesNo: int | IO capability
const IoKeyboardOnly: int | IO capability
const IoNone: int | IO capability (Just Works)
const IoKeyboardDisplay: int | IO capability
const PasskeyDisplay: int | OnPasskey kind: show the key
const PasskeyEntry: int | OnPasskey kind: return the key the user typed
const PasskeyCompare: int | OnPasskey kind: numeric comparison, return 1 / 0
const PasskeyConfirm: int | OnPasskey kind: Just Works confirmation, return 1 / 0
const SubscribeNotify: int | subscription flag
const SubscribeIndicate: int | subscription flag
const Phy1M: int | 1M PHY
const Phy2M: int | 2M PHY
const PhyCoded: int | Coded PHY (long range)

@BleProperty | GATT characteristic properties and permissions (combine with |)
const Broadcast: int | property
const Read: int | property
const WriteWithoutResponse: int | property
const Write: int | property
const Notify: int | property
const Indicate: int | property
const AuthenticatedSignedWrites: int | property
const ExtendedProperties: int | property
const Encrypted: int | permission: needs an encrypted link
const Authenticated: int | permission: needs MITM-protected pairing

@BleAdvertisement | Advertising data builder
new() | Empty advertisement (Ble.Name is used)
new(string name) | Advertises this name
IBeacon(string uuid, int major, int minor, int measuredPower = -59): BleAdvertisement | Apple iBeacon
EddystoneUrl(string url, int txPower = -20): BleAdvertisement | Eddystone-URL beacon
EddystoneUid(string namespaceHex, string instanceHex, int txPower = -20): BleAdvertisement | Eddystone-UID beacon
.Name: string | Advertised name
.Connectable: bool | Accept connections
.Interval: int | Advertising interval (ms)
.Duration: int | Stop after (ms, 0 = forever)
.IncludeTxPower: bool | Add the TX power field
.Appearance: int | Appearance field
.Flags: int | AD flags
.AddServiceUuid(string uuid): void | Lists a service UUID
.SetManufacturerData(int companyId, byte[] data): void | Manufacturer-specific data
.AddServiceData(string uuid, byte[] data): void | Service data
.Raw: byte[] | Your own advertising bytes (replaces the fields)
.ScanResponse: byte[] | Your own scan response bytes
.Build(): byte[] | The advertising bytes (max 31)
.BuildScanResponse(): byte[] | The scan response bytes

@BleDevice | An advertiser found by Ble.Scan / StartScan
.Address: string | "C0:11:22:33:44:55"
.AddressType: int | 0 public, 1 random
.Name: string | Advertised name (null if none)
.Rssi: int | Signal strength (dBm)
.Connectable: bool | Accepts connections
.TxPower: int | Advertised TX power
.Appearance: int | Advertised appearance
.Flags: int | AD flags
.ServiceUuids: string[] | Advertised service UUIDs
.HasService(string uuid): bool | Advertises this service
.ManufacturerData: byte[] | Manufacturer data (after the company id)
.CompanyId: int | Manufacturer company id (-1 if none)
.ServiceData(string uuid): byte[] | Service data for the UUID
.Data: byte[] | Raw advertising data
.ScanResponse: byte[] | Raw scan response
.IsIBeacon: bool | iBeacon frame
.BeaconUuid: string | iBeacon UUID
.BeaconMajor: int | iBeacon major
.BeaconMinor: int | iBeacon minor
.BeaconPower: int | iBeacon measured power
.EddystoneUrl: string | Eddystone-URL (null if none)
.Connect(int timeoutMs = 10000): BleConnection | Connects as central

@BleService | Local GATT service (peripheral)
new(string uuid) | Primary service: "180F" or a 128-bit UUID
new(string uuid, bool primary) | Primary or secondary
.AddCharacteristic(string uuid, int properties, object value = null, int maxLength = 0): BleCharacteristic | BleProperty flags; value byte[] or string
.Uuid: string | Service UUID
.Primary: bool | Primary service
.Handle: int | Attribute handle (after advertising starts)
.Characteristics: BleCharacteristic[] | Its characteristics

@BleCharacteristic | Local characteristic
.Uuid: string | UUID
.Properties: int | BleProperty flags
.Handle: int | Value handle
.MaxLength: int | Largest value
.Value: byte[] | What a read returns
.SetValue(object value): void | byte[] or string
.StringValue: string | Value as UTF-8 text
.Subscribers: int | Connections subscribed
.Notify(): int | Sends the current value to subscribers; returns how many
.Notify(object value, BleConnection conn = null): int | Sets the value and notifies (one connection or all)
.Indicate(object value = null, BleConnection conn = null): int | Like Notify, acknowledged by the peer
.OnWrite(Action<byte[], BleConnection> handler): void | A peer wrote
.OnSubscribe(Action<BleConnection, int> handler): void | A peer (un)subscribed: 0 off, 1 notify, 2 indicate
.AddDescriptor(string uuid, object value, bool writable = false): BleDescriptor | Adds a descriptor
.Description: string | User description (0x2901)

@BleDescriptor | Local descriptor
.Uuid: string | UUID
.Handle: int | Handle
.Value: byte[] | Value
.StringValue: string | Value as text
.OnWrite(Action<byte[], BleConnection> handler): void | A peer wrote

@BleConnection | A Bluetooth LE link (either role)
.Handle: int | Connection handle
.Address: string | Peer address
.AddressType: int | Peer address type
.Role: string | This board's role: "central" or "peripheral"
.Connected: bool | Still connected
.Mtu: int | ATT MTU
.Rssi: int | Signal strength (dBm)
.Encrypted: bool | Link encrypted
.Authenticated: bool | MITM-protected keys
.Bonded: bool | Peer is bonded
.Interval: int | Connection interval (1.25 ms units)
.Latency: int | Peripheral latency
.SupervisionTimeout: int | Supervision timeout (10 ms units)
.Disconnect(): void | Closes the link
.Pair(): void | Starts pairing / encryption
.RequestMtu(): int | MTU exchange; returns the new MTU
.UpdateParameters(int minInterval, int maxInterval, int latency, int timeout): void | Requests new parameters (1.25 ms / 10 ms units: 12, 24, 0, 400)
.Services(): BleRemoteService[] | Discovers the peer's services (GATT client)
.Service(string uuid): BleRemoteService | One service (null if missing)
.Characteristic(string serviceUuid, string uuid): BleRemoteCharacteristic | One characteristic (null if missing)

@BleRemoteService | A peer's GATT service
.Uuid: string | UUID
.Name: string | Bluetooth SIG name (null if unknown)
.Handle: int | First handle
.EndHandle: int | Last handle
.Characteristics(): BleRemoteCharacteristic[] | Discovers its characteristics
.Characteristic(string uuid): BleRemoteCharacteristic | One characteristic (null if missing)

@BleRemoteCharacteristic | A peer's characteristic
.Uuid: string | UUID
.Name: string | Bluetooth SIG name (null if unknown)
.Handle: int | Value handle
.Properties: int | BleProperty flags
.CanRead: bool | Readable
.CanWrite: bool | Writable
.CanNotify: bool | Notifies
.CanIndicate: bool | Indicates
.Read(): byte[] | Reads the value (long reads)
.ReadString(): string | Reads as UTF-8 text
.Write(object data, bool withResponse = true): void | Writes byte[] or string (long writes)
.Subscribe(Action<byte[], BleConnection> handler, bool indicate = false): void | Notifications / indications
.Unsubscribe(): void | Stops them
.Descriptors(): BleRemoteDescriptor[] | Discovers its descriptors

@BleRemoteDescriptor | A peer's descriptor
.Uuid: string | UUID
.Name: string | Bluetooth SIG name
.Handle: int | Handle
.Read(): byte[] | Reads the value
.Write(object data): void | Writes the value

@BleUart | Nordic UART Service: a serial port over BLE (peripheral)
new() | Advertises Ble.Name
new(string name) | Advertises this name
.Start(): void | Adds the service and advertises
.Write(object data): void | Sends byte[] or text (split into notifications)
.WriteLine(object text): void | Sends text and a line ending
.Available: int | Bytes received and not read
.Read(): byte[] | Everything received
.ReadString(): string | Everything received as text
.ReadLine(): string | One line (null if none yet)
.OnReceive(Action<string> handler): void | Data arrived
.Connected: bool | A central is connected

@BleUuid | UUID helpers
Normalize(object uuid): string | "180d" / 0x180D → "180D"; 128-bit in upper case
ToLong(object uuid): string | Full 128-bit form
Equals(object a, object b): bool | Same UUID (16-bit vs base UUID too)
IsValid(object uuid): bool | Parses as a UUID
Name(object uuid): string | Bluetooth SIG name (null if unknown)

@Exception | Base of every exception
new() | Without a message
new(string message) | With a message
new(string message, Exception inner) | Wrapping another exception
.Message: string | Description
.InnerException: Exception | The wrapped exception
.ToString(): string | Type and message

@TimeSpan | (not available - use Stopwatch / Environment.TickCount)
@DateTime | (not available - use RTC.Now, Unix seconds)
`;
