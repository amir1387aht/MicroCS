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
    "hal_fns": ("Hardware (modules/hal)", "Hal", "static", "consts `Board`, `ApiVersion`"),
    "gpio_fns": ("Hardware (modules/hal)", "GPIO", "static", "consts `Input Output InputPullUp InputPullDown OpenDrain Analog Rising Falling Both`"),
    "pin_members": ("Hardware (modules/hal)", "Pin", "instance", "`new Pin(pin[, mode])`; pin = number or name (`\"PA5\"`, `\"GPIO21\"`, `\"LED\"`)"),
    "uart_fns": ("Hardware (modules/hal)", "UART", "static", "consts `ParityNone ParityOdd ParityEven`"),
    "i2c_fns": ("Hardware (modules/hal)", "I2C", "static", ""),
    "i2cdev_members": ("Hardware (modules/hal)", "I2cDevice", "instance", "`new I2cDevice(bus, address)`"),
    "spi_fns": ("Hardware (modules/hal)", "SPI", "static", ""),
    "spidev_members": ("Hardware (modules/hal)", "SpiDevice", "instance", "`new SpiDevice(bus, csPin, hz, mode)` — drives CS for you"),
    "adc_fns": ("Hardware (modules/hal)", "ADC", "static", "consts `Resolution`, `ReferenceMillivolts`"),
    "dac_fns": ("Hardware (modules/hal)", "DAC", "static", "const `Resolution`"),
    "pwm_fns": ("Hardware (modules/hal)", "PWM", "static", "duty for `Set` is 0.0–1.0"),
    "timer_fns": ("Hardware (modules/hal)", "Timer", "static", "periods in microseconds"),
    "i2s_fns": ("Hardware (modules/hal)", "I2S", "static", "consts `Tx Rx Duplex`"),
    "qspi_fns": ("Hardware (modules/hal)", "QSPI", "static", "address `-1` = no address phase"),
    "can_fns": ("Hardware (modules/hal)", "CAN", "static", ""),
    "canframe_members": ("Hardware (modules/hal)", "CanFrame", "instance", "`new CanFrame(id, data[, extended])`"),
    "wdt_fns": ("Hardware (modules/hal)", "Watchdog", "static", ""),
    "rtc_fns": ("Hardware (modules/hal)", "RTC", "static", "Unix seconds"),
    "sched_fns": ("Scheduler (modules/sched)", "Scheduler", "static", "`mcs_sched_open_lib`"),
}
HIDDEN = {"rt_fns", "gpio_props"}
ORDER = ["Core", "Strings", "Collections", "Filesystem (modules/fs)", "Hardware (modules/hal)", "Scheduler (modules/sched)"]
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

# Convert.ToBase64String & co. are registered from a second table (src/mcs_lib_bytes.c)
tables["convert_fns"] = tables.get("convert_fns", []) + tables.pop("convert_bytes_fns", [])
missing = [t for t in tables if t not in MAP and t not in HIDDEN]
if missing:
    sys.stderr.write("unmapped tables: %s\n" % ", ".join(missing))

def fmt(items):
    props, meths, seen = [], [], set()
    for name, kind, ar in items:
        if name.startswith("op_") or name.startswith(".") or name.startswith("__"): continue
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
w("be removed per VM with `cfg.stdlib` and at build time with the `MCS_ENABLE_*` flags in")
w("`include/mcs_config.h`.")
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
w("(`Ordinal`, `OrdinalIgnoreCase`, `CurrentCultureIgnoreCase`, `InvariantCultureIgnoreCase`).")
w("")
w("_%d members in %d tables._" % (total, len([t for t in tables if t in MAP])))
print("\n".join(out))
