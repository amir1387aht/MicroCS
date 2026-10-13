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
bundle is not confused with a later run. Textarea line endings are normalized
for edit comparison only, not rewritten by bundle construction.

Limits: 1–32 `.cs` text members, at most 256 Ki UTF-16 units total and 512 KiB
UTF-8 after bundling. Every member must have closed literals/block comments and
balanced structural delimiters; complete EOF line comments are supported.
Membership and the last run map are **session-only**, not a persistent/exported
project manifest. Save/download individual files separately. This is **source
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
