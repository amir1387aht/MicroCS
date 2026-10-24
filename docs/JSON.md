# JSON

MicroCS includes a small, dependency-free JSON reader and writer. They work on
all ports, including offline firmware, when `MCS_ENABLE_JSON`,
`MCS_ENABLE_LIST` and `MCS_ENABLE_DICT` are enabled. Networking is optional.
The same engine powers JSON HTTP requests and server responses.

## Quick start

```csharp
var data = new Dictionary<string, object> {
    ["device"] = "sensor-1",
    ["reading"] = 21.5,
    ["enabled"] = true,
    ["samples"] = new object[] { 1, null, "ok" }
};
string text = Json.Serialize(data);
var decoded = Json.Parse(text);
Console.WriteLine(decoded["device"]);
Console.WriteLine(Json.Serialize(decoded, true));

object value;
if (Json.TryParse("{\"ok\":true}", out value))
    Console.WriteLine(value["ok"]);
```

`JsonSerializer` is an alias for `Json`; `using System.Text.Json;` is optional.
It is **not** the .NET System.Text.Json framework: serializer options, attributes,
custom converters and typed class reconstruction are not implemented.

## API

| Call | Result |
| --- | --- |
| `Json.Parse(text)` | Parse one complete document into data values |
| `Json.Deserialize(text)` | Same as `Parse` |
| `Json.TryParse(text, out value)` | `true` and the parsed value, or `false` and null for invalid input |
| `Json.Serialize(value[, indent[, includeHidden]])` | Encode a supported value as JSON text |
| `Json.Stringify(value[, indent[, includeHidden]])` | Same as `Serialize` |
| `Json.Escape(text)` | Quote and escape a string; null becomes the text `null` |

All six names are also available on `JsonSerializer`. `indent` defaults to
false (compact); true means two spaces, or supply an integer width clamped to
0–8. `includeHidden` defaults to false. `TryParse` also returns false for null or
non-string input; allocation failures and execution aborts still propagate.
`Parse` requires a string, and `Escape` accepts only a string or null.

## Values and objects

| Input to serialization | JSON representation |
| --- | --- |
| null, bool, integer, finite floating-point value | Corresponding scalar |
| string, char | Quoted UTF-8 text, escaped where needed |
| `List`, arrays, `byte[]` | JSON array (`byte[]` is numeric, not Base64) |
| `HashSet`, `Stack`, `Queue` | JSON array in the runtime collection's stored order |
| `Dictionary` | JSON object in insertion order |
| Script object | Fields plus readable computed/inherited instance properties |
| Native userdata exposing `ToString()` | Quoted string representation, not resource identity |
| Delegate, class object or other unsupported runtime value | `NotSupportedException` |

Collections and fields can contain mixed types and nest recursively. Dictionary
keys may be strings or scalar integers, finite numbers, booleans or chars; they
are converted to JSON member names. Other key types are rejected. Prefer string
keys: different scalar keys can stringify to the same name. Parsing duplicate
member names keeps the last value. JSON object ordering is not a semantic guarantee.

```csharp
class Reading {
    public string Sensor = "room";
    public double Celsius = 21.5;
    private string _token = "secret";
    public double Fahrenheit => Celsius * 1.8 + 32;
}
Console.WriteLine(Json.Serialize(new Reading(), true));
// Sensor, Celsius and Fahrenheit are included; _token is omitted.
```

Names beginning with `_` are omitted unless the third argument is true.
Compiler/internal names beginning with `$` or `<` are always omitted. Static
properties and methods are not data members. Derived properties shadow inherited
properties; automatic properties are emitted once.

**This naming rule is not access control.** Runtime field metadata does not retain
C# public/private visibility; a private field without an underscore can still be
serialized. Use dedicated data-transfer objects and never rely on serialization
to filter secrets. Computed getters execute script code; their exceptions
propagate, and their side effects occur. Keep getters cheap and side-effect free.

Parsing creates dictionaries, lists, strings, numbers, booleans and null, not
instances of the original class. Generic type arguments do not change this:
`Deserialize<MyClass>` does not populate `MyClass`.

## Strict parsing, numbers and Unicode

- Accepts objects, arrays and top-level scalars, with JSON whitespace. A leading
  UTF-8 BOM is tolerated. Comments, trailing commas, leading-zero numbers and
  extra data after the document are rejected.
- Validates raw UTF-8 and escaped UTF-16 surrogate pairs. Invalid UTF-8, isolated
  surrogates, unescaped control characters and truncated escapes are rejected.
  Embedded NUL is preserved through `\u0000`; strings are length-aware.
- Integral tokens use the configured native integer range, including its minimum
  and maximum. Other numeric tokens use the configured floating-point type.
  The complete number token is parsed without a fixed-length truncation buffer.
- Numbers are **not arbitrary precision**. Out-of-integer-range values may round
  as floating point. Floating overflow and nonzero underflow to zero are errors.
  Represent large identifiers or exact decimal money as strings. With floating
  point disabled, fractions/exponents and out-of-integer-range tokens are errors.
- NaN and infinities are not valid JSON input. For compatibility, serializing
  these runtime values produces JSON null.

Malformed text throws `FormatException` with a byte offset. Invalid string data
also fails serialization rather than emitting malformed Unicode.

## HTTP JSON APIs

Use `Hal.Has("Http.Json")` to detect the new client helpers; `Hal.Has("Json")`
only says the JSON engine exists. HTTPS additionally requires TLS support and
normal certificate verification configuration; see [Networking](NETWORK.md).

```csharp
if (!Hal.Has("Http.Json")) {
    Console.WriteLine("This firmware lacks the JSON HTTP helpers.");
    return;
}
var headers = new Dictionary<string, string> {
    ["Authorization"] = "Bearer replace-with-your-token"
};
var data = Http.GetJson("https://api.example.com/devices/1", headers);
var update = new Dictionary<string, object> { ["enabled"] = true };
var response = Http.PatchJson("https://api.example.com/devices/1", update, headers);
response.EnsureSuccess();
if (response.ContentLength > 0) Console.WriteLine(response.Json);
```

| API | Behavior |
| --- | --- |
| `Http.GetJson(url[, headers])` | GET, require 2xx, then parse the body |
| `Http.PostJson(url, value[, headers])` | Serialize and POST; return `HttpResponse` |
| `Http.PutJson(url, value[, headers])` | Serialize and PUT; return `HttpResponse` |
| `Http.PatchJson(url, value[, headers])` | Serialize and PATCH; return `HttpResponse` |
| `response.Json` | Parse `response.Body`, regardless of HTTP status |

Writes use `application/json; charset=utf-8`. Null sends literal `null`; a string
sends a JSON string, not a pre-encoded document. For raw JSON text use the existing
`Http.Post/Put/Patch(url, text, "application/json"[, headers])` APIs. Empty or
non-JSON bodies fail parsing. Only `GetJson` checks status automatically; call
`EnsureSuccess()` or inspect `IsSuccess` on write responses. Header handling,
timeouts, redirects and TLS follow the existing HTTP client.

On the server, `request.Json` parses the request body and
`request.SendJson(value[, status])` serializes values with this same engine.
Route handlers can return data objects directly. Returning a string still means
an HTML/text response, and returning null still means an empty response; use
`SendJson` to explicitly send a JSON string or JSON null.

## Memory and firmware compatibility

The implementation adds no external library or per-VM JSON cache. Parse results
and output strings use the existing VM allocator; temporary text buffers are
released on success, malformed input and fatal allocation failure. Allocation
failure tests also cover recovery on the same VM. Dictionary keys and values
share one allocation, making growth atomic without a staged second buffer. Cycles throw
`InvalidOperationException`; shared but acyclic objects are supported.

This is an in-memory API, not a streaming parser. Input, parsed graph and output
can coexist, and getters may allocate. GC is paused during a conversion to keep
native traversal references safe; it resumes afterward, including on errors.
Avoid building very large documents on an MCU, discard old graphs, and collect
between independent batches if needed. Available pool space remains a real limit.

`MCS_JSON_MAX_DEPTH` defaults to 32 and can be configured from 1 to 64. Keep it
small on targets with limited C stack. Parsing counts nested containers;
serialization also bounds recursive values. Raising the limit does not increase
the VM pool or MCU stack automatically.

Existing firmware may provide the older core JSON methods without these additions.
Check `Hal.Has("Json.TryParse")`, `Hal.Has("Json.ComputedProperties")` and
`Hal.Has("Http.Json")` before using the respective new features. Builds without
JSON retain their previous feature configuration; the HTTP helpers are registered
only when both HTTP and JSON are available.
