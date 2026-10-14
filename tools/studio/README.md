# MicroCS Studio

A browser IDE for boards running MicroCS: file manager, C# editor and REPL over USB
(Web Serial). Visual Studio 2022 Dark look.

## Run it

* **Online:** https://amir1387aht.github.io/MicroCS/
* **Offline:** open `index.html` in **Chrome or Edge** on a desktop (double-click works, no server
  or install). Keep the files together.

Click **Connect**, pick the board's serial port, done. Firefox and Safari have no Web Serial:
the editor works there, but it can't connect to a board.

## Files

| File | What it holds |
|---|---|
| `index.html` | page layout |
| `studio.css` | look: Visual Studio 2022 Dark / Light, C# colours (`--t-*` variables) |
| `studio.js` | the app: serial link and shell protocol, editor, file manager, console |
| `templates/` | the templates and examples: one folder per category, plain `.cs` files + a `templates.txt` describing them ([how to add one](templates/README.md)) |
| `templates.js` | **generated** from `templates/` by `python3 gen_templates.py` (the page loads it; CI checks it is current) |
| `snippets.js` | completion snippets (`label`, `detail`, `text` with `$0` for the caret) |
| `api.js` | class and member names known to the runtime (completion fallback) |
| `docs.js` | signatures and one-line docs of every library member (completion details, parameter info, hover) |
| `lang.js` | shared cached lexical contexts, symbols of the open file, type inference, call context |

No build step for the app: edit a file and reload the page. After changing a template run
`python3 tools/studio/gen_templates.py` (or `make studio-templates`).

## Editor keys

| Keys | Action |
|---|---|
| Ctrl+Space · Ctrl+Shift+Space | completion (with docs) · parameter info (↑/↓ switch overloads) |
| Ctrl+F · Ctrl+H · F3 / Shift+F3 | find · replace · next / previous match |
| Ctrl+G · Ctrl+] · F12 | go to line · jump to the matching bracket · go to source definition |
| Shift+Alt+F | format document |
| Alt+↑/↓ · Shift+Alt+↑/↓ · Ctrl+D · Ctrl+Shift+K | move line · copy line · duplicate · delete line |
| Ctrl+/ · Tab / Shift+Tab | toggle comment · indent / outdent |
| F5 / Ctrl+Enter · Ctrl+Shift+Enter · Ctrl+S | save and run · run the selection · save |
| Shift+F5 | run the explicit source project (without saving originals) |
| Ctrl+B | show / hide Device Explorer (separate compact/desktop preference) |

The formatter (Shift+Alt+F) preserves LF/CRLF/CR line endings, including mixed
endings, and never rewrites multiline string payloads or multiline comments.
Whole physical lines touching multiline or unfinished strings are deliberately
left unchanged; code after the literal is still indented. This conservative
behavior prevents formatting from changing program output.

## Interpolated expressions

Completion, parameter help, hover and highlighting use the same bounded lexical
model. Suggestions work inside `$"{Con` and `$"{Console.`, including multiline
`$@"` / `@$"` strings, nested strings and comments. Literal text, escaped braces,
comments and format text after `:` do not trigger code assistance. Alignment
expressions remain code; ternary `?:`, null-coalescing and null-conditional
operators do not prematurely start a format section. Edits invalidate the
single-document cache; cursor context lookup uses binary search. Malformed or
more than 32 nested interpolations are handled conservatively rather than
recursing without a bound. This is editor assistance, not compiler validation
or a full C# semantic model.

Quick regressions:

```sh
node tests/studio/test_lexical.js
node tests/studio/test_highlight.js
node tests/studio/test_format.js ./mcs
CHROME=/path/to/chromium node tests/studio/test_completion_context.js
```

## Document an API

`docs.js` holds one line per member, e.g.

```
@GPIO | Digital pins (modules/hal)
Write(pin pin, bool value): void | Drives the pin: true / 1 = high, false / 0 = low
.Value: bool | (a leading "." = instance member)      const Rising: int | constant
```

Repeat a line for each overload; `new(...)` lines are constructors.

## Templates dialog

`Alt+T` (or *Templates*) opens it: categories on the left (*Recent* holds the last ones you
used), search across names, descriptions and code (every word must match; name hits come
first and are highlighted), a preview with tags, line count, a copy button and a link to the
source file. `↑`/`↓`, `PgUp`/`PgDn`, `Home`/`End` move, `Enter` adds the template as a new file
(named so it doesn't clash with an open tab), `Ctrl+Enter` inserts it at the cursor, `Esc`
clears the search, then closes. Saving a new file over an existing one on the device asks
first.

## Connection notes

Studio leaves DTR/RTS as the browser opens the port (both asserted), which does **not** reset
ESP32 boards with the usual auto-reset circuit or the native USB-Serial-JTAG. *Reset board*
pulses EN like esptool. A board that sits in the ROM download mode is reset once
automatically. If a board needs the lines low, enable ⋯ → *Release DTR/RTS on connect*.

## Jobs

`Scheduler.Every` / `Scheduler.After` jobs keep running after their script returns (and after
its file is deleted). So that re-running a script does not stack copies, *Run* first sends
`cancel scripts` (⋯ → *Stop script jobs before each Run*, on by default); `jobs.cfg` jobs are
left alone. ⋯ → *Show running jobs* lists them, ⋯ → *Stop all jobs* cancels every job until
the next reset.

## Add a template

Append to `templates.js`:

```js
T("GPIO", "My blinker", "myblink.cs",
  "One line shown in the list.", String.raw`
var led = new Pin(2, GPIO.Output);
led.Toggle();
`);
```

`String.raw` keeps `\n` and other escapes as they are, so you can paste C# straight in.
`node tests/studio/test_templates.js ./mcs` compiles and runs every C# template on the
simulated board. Templates that use `U8g2` / `U8x8` start with an
`if (!Drivers.Has("u8g2")) { ...; return; }` hint and are run against the simulated OLED with a
u8g2 build: `make fetch-u8g2 mcs-u8g2 && node tests/studio/test_templates.js build/mcs_u8g2 --only=u8g2`.
`tests/studio/test_studio.js` tests the page in a headless browser.

Protocol and features: [docs/STANDALONE.md](../../docs/STANDALONE.md#6-in-the-browser-microcs-studio).

### Scoped semantic lookup (WIP)

Cached structural scopes and declaration intervals now keep block locals,
method/constructor/lambda parameters, for/foreach/catch variables and nested
functions in their own scope. A closed shadow no longer overrides an outer
variable's inferred type. Class fields are resolved through the enclosing class,
not leaked into other classes' locals. Incomplete bodies keep valid ancestor
scope available. Native assistance includes the real optional LVGL/TFT subset;
constructor signatures are represented as constructors, not fake static factories.
Parameter splitting ignores commas/brackets/whitespace inside literal defaults.

This remains a tolerant lightweight source model, not a complete C# compiler or
project semantic engine. Complex patterns, all declaration variants and true
module/namespace resolution still need dedicated work. The bounded source-project
and definition-navigation workflow below is implemented, not a full compiler
semantic index. Unknown
types are not a promise of unsupported .NET APIs. `tests/studio/test_scope.js`
contains direct visibility/type/default regressions; real editor popup/signature
and full serial integration tests cover the same implementation. Exact Node
host cache-latency samples are recorded with their fixture; do not interpret them
as browser/MCU results or a universal speedup.

The optional TFT capability is `TFT` (class `Tft`). Capability protocol v1 names
must match `[A-Za-z][A-Za-z0-9.]*`; every native name is now tested against that
existing client grammar. Introducing an underscore-containing capability would
invalidate the entire report for older Studio clients, not merely hide the new
optional feature. No protocol version change is needed for the legal new name.

## Explicit source projects and navigation (v2 WIP)

Open the C# files you intend to include, select the entry tab, then choose
**⋯ → Create project from open C# files…**. The confirmation lists the exact
membership; cancel if it includes an unwanted file. Libraries run in tab order,
with the selected entry **last**. Files opened afterward are not imported.
Recreate the project to change membership. Closing a member blocks execution
instead of silently running a subset. **Close project (keep files)** removes
project assistance, not the tabs or device files.

**Shift+F5** validates complete independent source files, joins their unchanged
text with a newline boundary, and uploads one uniquely named temporary source
bundle. It runs that bundle through the actual on-device MicroCS compiler.
Original files are not uploaded/saved or marked clean. The temporary file is
removed after success, failure or Stop; a disconnect/cleanup failure warns that
it may remain. Name collisions never overwrite an existing file. Duplicate runs
are blocked from upload through cleanup. A known `Compiler=0` firmware does not
receive a source bundle. Device storage, heap and compiler limits still apply.
Ordinary **F5**, selection execution and **Ctrl+S** keep their existing behavior.

Project types, enums, root functions and class members feed completion,
parameter help and hover. Foreign locals/parameters/local functions stay in their
own document. **F12** navigates supported locals, parameters, types, functions,
fields and methods to the original source name; multiple overload locations ask
which one to open. Ambiguous foreign class declarations are not silently chosen.
Error stack-frame/compile locations map to the original run snapshot. Clickable
old diagnostics do not jump into changed or closed sources; an older temporary
bundle is not confused with a later run. Canonical source preserves CRLF/CR/LF;
only the textarea and language-assistance view use LF. Diagnostics compare exact
raw snapshots. Firmware/compiler line numbers count LF: a diagnostic spanning
multiple CR-only editor rows is reported as ambiguous instead of guessed.

Limits: 1–32 `.cs` text members, at most 256 Ki UTF-16 units total and 512 KiB
UTF-8 after bundling. Every member must have closed literals/block comments and
balanced structural delimiters; complete EOF line comments are supported.
Active membership and the last run map are **session-only** unless you explicitly
download/reopen the portable snapshot below; run maps are never persisted.
Individual file saves remain separate. This is **source
bundling**, not dynamic imports, packages, .NET assemblies or a new namespace
resolver: the runtime's existing `using`/namespace behavior remains unchanged.
No assets, include paths or external SDKs are packaged. Editor assistance remains
conservative and the compiler remains the execution authority. Passing these
host/browser tests does not make full v2 release-ready.

```sh
node tests/studio/test_project.js ./mcs
CHROME=/path/to/chromium node tests/studio/test_project_browser.js ./mcs
CHROME=/path/to/chromium node tests/studio/test_project_layout.js
```

The browser test uses a Web Serial boundary wired to the **real host shell**, not
fabricated protocol successes. It checks original-file preservation, membership,
completion/signatures/hover, definition selection, mapped errors, stale positions,
Stop/recovery, duplicate runs, compiler-off gating, collisions and temporary
cleanup. It is not physical-device evidence.


## Portable source snapshots (`.microcsproj`, v2 WIP)

After creating an explicit project, use **⋯ → Download source project…**. The
confirmation warns that this is **plaintext**, including unsaved edits. Choose a
filename, not a path. Downloading neither marks individual files saved nor writes
to a board. An incomplete draft can be saved; Run still requires complete source.
Keep private credentials out of source snapshots and share them accordingly.

**⋯ → Open source project…** validates the entire UTF-8 JSON file before changing
membership, then asks for confirmation. It creates **new independent unsaved
local tabs**; existing tabs and device files remain unchanged. The entry file is
selected and runs last. No file is fetched, executed or saved on import. A fresh
offline browser can restore membership/order/entry/source from the file alone.
This is explicit portability, **not automatic project saving or cloud sync**.

Version 1 has exactly these fields (IDs relate members, not browser/device IDs):

```json
{
  "format": "microcs-source-project",
  "version": 1,
  "name": "sensor",
  "entry": "source-2",
  "files": [
    {"id": "source-1", "name": "lib/meter.cs", "text": "class Meter { public int Read() { return 42; } }\n"},
    {"id": "source-2", "name": "main.cs", "text": "Console.WriteLine(new Meter().Read());\n"}
  ]
}
```

Limits: 2 MiB UTF-8 JSON (checked before reading an oversized browser file),
1–32 members, 256 Ki UTF-16 source units total, 1–128 character project name,
1–64 ASCII letter/digit/underscore/hyphen member IDs, and 1–256 character relative
`.cs` labels. IDs must be unique; duplicate display names can have distinct IDs.
Unknown/missing fields, invalid encoding/version, NUL source, controls in labels,
absolute/URL/device paths, backslashes and empty/dot/traversal path segments are
rejected. Labels do not cause filesystem access. Leading `/` on an existing
device-tab label is removed only for export; the reopened tab has no device path.
JSON escaping retains Unicode, CRLF/CR/LF and unfinished literal text. A BOM is
accepted on import. Validation is **not authentication, signing or a sandbox**.
No assets, packages, URL imports, assemblies or Visual Studio `.csproj` support.

The editor now keeps canonical raw source separately from its normalized display.
Opening, switching and untouched re-export no longer silently change CRLF/CR
inside multiline literals. Formatting uses raw source; replacement, raw line
movement/duplication and clipboard paste preserve their tested source segments.
New structural line breaks use the first existing ending (LF for empty source).
Native browser undo remains browser-owned; a bounded side cache (128 snapshots,
2 Mi UTF-16 units across raw+display copies, cleared on tab opening) restores
canonical endings for recent text undo/redo. Arbitrarily old mixed-ending undo
beyond that cache and EOL-only replacements with unchanged display text are
**not proved byte-preserving**; broader editor history remains a v2 release gate.

Long filename/connection labels are ellipsized with exact hover titles. Status
connection/busy/cursor fields stay visible in the tested desktop/phone/short
viewports; encoding is hidden on small screens. Header and tab/tool controls
scroll locally rather than widening the document. This is a scoped status/layout
fix, not a complete phone-editor/side-panel accessibility or performance audit.

```sh
node tests/studio/test_project_file.js ./mcs
node tests/studio/test_source.js
CHROME=/path/to/chromium node tests/studio/test_project_file_browser.js ./mcs
CHROME=/path/to/chromium node tests/studio/test_status_layout.js
```

The portable browser gate exercises actual download/reopen, schema/encoding/size
rejection, confirmations/cancellation, fresh-context source navigation, raw
multiline payload editing and real host-shell execution. Status tests change DOM
labels only for geometry; they do not claim device connection/IO acknowledgement.


## Adaptive editor and resizable panels (v2 WIP)

On viewports at most 600 px wide, Device Explorer starts collapsed to leave useful
source space. The first header button or **Ctrl+B** opens/closes it. Desktop and
compact collapse preferences are independent and persisted locally; changing
viewport/orientation does not overwrite the other preference or saved pane sizes.
Hiding a focused Explorer returns focus to the source (or the toggle if no tab).
Showing/hiding panels does not save, run, close or rewrite source files.

Actual sidebar and console sizes clamp to the current available space without
replacing the preferred desktop sizes. The default short-window split leaves a
useful editor rather than a one-line strip. Invalid persisted sizes use finite
defaults. Editor and console implicit grid columns no longer expand the document;
console modes/actions, tabs/toolbars and header controls scroll locally when
necessary. The encoding status badge remains hidden on narrow screens.

Both splitters support pointer capture for mouse/touch and keyboard focus with
separator semantics and bounded ARIA values. Sidebar Left/Right and console
Down/Up adjust by 20 px; Home/End choose the current minimum/maximum. Touch/drag
cancellation releases dragging state. The resize grips remain narrow desktop
splitters: touch behavior has a real Chromium input regression, **not physical
mobile-device, 44-px touch-target or complete accessibility certification**.
Web Serial board connection still depends on a supported desktop browser; this
layout does not add unsupported phone Bluetooth/USB/serial capability.

```sh
CHROME=/path/to/chromium node tests/studio/test_editor_layout.js
```

The gate checks visible useful source area, viewport boundaries, source bytes and
real controls across 1400×860, 390×844, 667×375 and 320×568, dark/light. Additional
checks use actual keyboard, mouse and Chromium touch input, breakpoint transitions,
independent persisted collapse settings, preferred-size retention, invalid-size
fallback, focus handoff and page reload. Broader zoom/assistive-tech/long-file/
findbar/phone-editor/performance/history audits remain full-v2 gates.

## Explicit bounded EOL-only source history (parallel Studio feature)

An exact paste/replacement can change CRLF/CR/LF bytes while leaving every
textarea character unchanged, including inside multiline literals. Native
textarea undo cannot reliably identify those operations. Studio therefore skips
native insertion for same-display replacements and provides a **separate** raw
history: **Ctrl/Cmd+Alt+Z** undoes an EOL-only change;
**Ctrl/Cmd+Alt+Shift+Z** redoes it. Selection/caret and exact raw source are
restored; dirty state and source/project diagnostic staleness use the raw source.
A replacement with identical raw bytes is a no-op, not a history entry.

This specifically supersedes the EOL-only replacement caveat above for this
explicit mechanism, not for native Ctrl/Cmd+Z. Ordinary textarea undo/redo stays
browser-owned and skips these byte-only operations. The separate history covers
only contiguous EOL-only edits in the currently open editor: a text-changing
edit, native undo/redo request, or tab reopening clears it. It retains at most
128 entries and 2 Mi UTF-16 units across before/after raw copies; eviction stops
at the oldest retained boundary. An oversized change still preserves its exact
bytes, clears this history, and displays a warning that raw undo is unavailable.
No arbitrarily old, cross-tab, interleaved text/raw, or native mixed-EOL history
promise is made. Read-only editors and composing keyboard events do not invoke
raw history. This is not a change to project saving or execution.

Regression commands (browser is offline, no device required):

```sh
node tests/studio/test_source.js
CHROME=$(command -v chromium) node tests/studio/test_source_eol_history_browser.js
```
