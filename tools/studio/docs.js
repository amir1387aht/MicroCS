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
Has(string name): bool | "GPIO", "GPIO.IRQ", "UART", "I2C", "SPI", "ADC", "DAC", "PWM", "Timer", "I2S", "QSPI", "CAN", "Watchdog", "RTC", or a driver: "LedStrip", "ws2812"
Poll(): void | Runs pending callbacks now
Run(): void | Dispatches events forever (until Ctrl-C)
Run(int ms): void | Dispatches events for ms milliseconds
DelayMicroseconds(int us): void | Busy-waits
Reset(): void | Restarts the chip
OnEvent(int n, Action<int, int> handler): void | Handler(source, value) for user event n (C: mcs_hal_post)
Post(int n): void | Queues user event n
Post(int n, int source, int value): void | Queues user event n with data

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
.Write(bool value): void | Drives the pin
.Read(): bool | Reads the level
.Toggle(): void | Inverts the output
.High(): void | Drives high
.Low(): void | Drives low
.SetMode(int mode): void | Changes the mode
.OnChange(int edge, Action<bool> handler): void | Interrupt callback (level)
.Value: bool | Level (get / set)
.Number: int | Pin number

@UART | Serial ports (modules/hal)
const ParityNone: int | No parity
const ParityOdd: int | Odd parity
const ParityEven: int | Even parity
Open(int port, int baud): void | Opens the port, 8N1
Open(int port, int baud, int dataBits, int parity, int stopBits): void | Opens with framing: parity UART.ParityNone / ParityOdd / ParityEven
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
Transfer(int bus, byte[] data): byte[] | Full duplex
Transfer(int bus, byte[] data, pin csPin): byte[] | Full duplex, drives CS
Write(int bus, byte[] data): void | Sends bytes
Write(int bus, byte[] data, pin csPin): void | Sends bytes, drives CS
Read(int bus, int n): byte[] | Reads n bytes
Read(int bus, int n, pin csPin): byte[] | Reads n bytes, drives CS

@SpiDevice | One SPI device with its own CS and settings (modules/hal)
new(int bus, pin csPin, int hz, int mode) | CS handled, bus re-configured per device
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

@DAC | Analog outputs (modules/hal)
Resolution: int | Bits
Write(int ch, int value): void | Raw value
WriteMillivolts(int ch, int mV): void | Output voltage

@PWM | PWM channels: LEDs, motors, servos, buzzers (modules/hal)
Set(int ch, int hz, double duty): void | Frequency and duty 0.0-1.0
SetPermille(int ch, int hz, int permille): void | Duty 0-1000
SetPulse(int ch, int hz, int pulseUs): void | High time in µs
Servo(int ch, double degrees): void | 50 Hz servo, 0-180°, 500-2500 µs
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

@CAN | CAN bus / TWAI (modules/hal)
Open(int bus): void | 500 kbit/s
Open(int bus, int bitrate): void | Bit rate (125000, 250000, 500000, 1000000)
Send(int bus, int id, byte[] data): void | Standard 11-bit frame
Send(int bus, int id, byte[] data, bool extended): void | 29-bit id when extended
Send(int bus, CanFrame frame): void | Sends a frame
Receive(int bus): CanFrame | Next frame or null
Receive(int bus, int timeoutMs): CanFrame | Waits up to timeoutMs
OnReceive(int bus, Action<int> handler): void | Callback(pending) when frames arrive

@CanFrame | A CAN frame
new(int id, byte[] data) | Standard frame
new(int id, byte[] data, bool extended) | Extended (29-bit) when true
.Id: int | Identifier
.Extended: bool | 29-bit id
.Remote: bool | Remote request
.Length: int | Data length
.Data: byte[] | Payload

@Drivers | Device drivers compiled into the firmware (modules/drivers, mcs_driver.h)
Has(string name): bool | A driver ("ws2812") or a class it adds ("LedStrip") is available
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
