# MicroCS Studio

A browser IDE for boards running MicroCS: file manager, C# editor and REPL over USB
(Web Serial). A focused dark/light workbench with keyboard-first commands.

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
| `workbench.js` | command palette, open-file switcher, explorer visibility and format availability; browser-only |
| `studio.js` | the app: serial link and shell protocol, editor, file manager, console |
| `templates/` | the templates and examples: one folder per category, plain `.cs` files + a `templates.txt` describing them ([how to add one](templates/README.md)) |
| `templates.js` | **generated** from `templates/` by `python3 gen_templates.py` (the page loads it; CI checks it is current) |
| `snippets.js` | completion snippets (`label`, `detail`, `text` with `$0` for the caret) |
| `api.js` | class and member names known to the runtime (completion fallback) |
| `docs.js` | signatures and one-line docs of every library member (completion details, parameter info, hover) |
| `format.js` | offline, whitespace-only C# formatter; protects literals and comments |
| `lang.js` | C# language service: symbols of the open file, type inference, call context |

No build step for the app: edit a file and reload the page. After changing a template run
`python3 tools/studio/gen_templates.py` (or `make studio-templates`).

## Editor keys

| Keys | Action |
|---|---|
| Ctrl+Shift+P · Ctrl+P | command palette · switch between open files (no device scan) |
| Ctrl+B | hide/show the explorer |
| Ctrl+Space · Ctrl+Shift+Space | completion (with docs) · parameter info (↑/↓ switch overloads) |
| Ctrl+F · Ctrl+H · F3 / Shift+F3 | find · replace · next / previous match |
| Ctrl+G · Ctrl+] | go to line · jump to the matching bracket |
| Shift+Alt+F | format document |
| Alt+↑/↓ · Shift+Alt+↑/↓ · Ctrl+D · Ctrl+Shift+K | move line · copy line · duplicate · delete line |
| Ctrl+/ · Tab / Shift+Tab | toggle comment · indent / outdent |
| F5 | save/build, then run the active file or the Project jobs |
| Ctrl+Enter · Ctrl+Shift+Enter | save/build and run the active file · run the selection |
| Ctrl+` · Ctrl+J · Alt+Tab | hide/show the bottom terminal (also the toolbar terminal button or panel ×) |
| Ctrl+S / Cmd+S | save/upload to the connected device and compile-check (no execution); works with non-Latin keyboard layouts |

The terminal can be hidden without stopping the device or clearing output. Its height,
Output/Plotter selection and visibility are remembered. Many operating systems reserve
**Alt+Tab** for app switching; use **Ctrl+J** or the terminal button when that happens.

## Formatting and save/build defaults

The ⋯ menu has three independent options, **on by default**:

* **Automatic formatting while typing**: indent/space the current C# line after `;` or `}`;
  paste formats indentation/spacing. IME composition and read-only/non-C# files are excluded.
* **Format C# on save**: format the whole document, including Allman-style braces.
  `Shift+Alt+F` formats explicitly. This is a conservative formatter, not Roslyn: it leaves
  ambiguous operators/generic brackets alone and preserves strings and comments byte-for-byte.
  Multiline literal/comment bytes remain unchanged while surrounding code can be reflowed.
Ctrl+S/Cmd+S never opens the browser Save Page dialog or downloads an offline draft.
Without a connected device it asks you to connect; use **⋯ → Download** for a computer copy.
While a dialog is open, the shortcut is suppressed without saving files behind it.

* **Save/upload all open C# files and compile**: Ctrl+S saves and compile-checks every
  editable open C# tab in **File mode**, plus the explicitly saved file. F5, Run and
  Run file also save these tabs when this option is enabled, but compile-check and
  execute **only the selected entrypoint**. Its namespace dependencies are resolved
  by the firmware; unrelated open applications cannot block its compilation. Saved files
  are verified against the device and identical uploads are skipped; known changed
  File-mode files upload directly. **Project mode** verifies all candidates before
  uploading only new/changed files (see below). New/local tabs
  ask for a device path; duplicate open paths are rejected. Compile-only checks never execute
  scripts. A cancelled/failed upload or compilation prevents the requested run.

Compile checks use the firmware `check` command. On older firmware without it, File mode
still saves/uploads and clearly warns that compile-only validation was skipped. Run compiles
and executes only the selected file; Studio never runs other sources as a compatibility probe.
Project mode still requires compile-only checks and will request a firmware update rather
than starting an unchecked project. Offline local saves download
only the selected file. Selection-run is an explicit exception: it executes just the selection.

Parameter info closes on outside pointer clicks, Escape, its × button, focus loss and moving
out of the call; it should not block other controls.

IntelliSense combines the symbols from all open C# tabs, so a variable whose type is declared
in another open file offers that class's fields and methods. Built-in class completion also
opens for a single capital letter (`P` includes `Path`). The firmware compiler resolves source
dependencies: when a `.cs` file uses `using App.Display;`, file execution scans the mounted
VFS for `.cs` files declaring `namespace App.Display`, including providers in different
project folders, and installs their type declarations before compiling/running the requested
file. It follows transitive dependencies once and breaks namespace cycles. Dependency top-level statements and implicit `Main` methods do not run.

## Project mode

1. Create a device folder (or use `/`), with C# scripts and a `jobs.cfg` file. Use the existing
   boot/jobs templates or create a text file. For a project at `/demo`, for example:

   ```text
   startup /demo/init.cs
   every 1s /demo/blink.cs restart=always
   ```

2. Click **Project…** and choose `/demo`. The toolbar mode changes to Project; the choice
   and folder are remembered in this browser.
3. **F5 / Run project** formats open project C# tabs and considers them plus an open project
   `jobs.cfg` for saving. Only new/changed files are uploaded, then it
   recursively compile-checks the device folder's `.cs` files, including closed files.
   New/local C# tabs must be saved inside the project folder. Other device projects are left
   alone. Hidden subfolders and `.studio` temporary scripts are excluded from discovery
   (limits: 128 folders, 512 scripts).
4. Firmware reads the required `jobs.cfg`, validates its syntax and each configured source,
   then replaces all existing jobs with exactly those entries. It does **not** run the active
   editor tab implicitly. Empty configs, relative paths and non-`.cs` job paths are rejected.
   `startup /main.cs` runs once here; the boot-only duplicate-main exclusion does not apply.

**Ctrl+S** in Project mode saves/builds without launching/reloading jobs. **Run file / Ctrl+Enter**
remains an explicit single-file action, even in Project mode; it uses the normal open-C# save
option but compile-checks/runs only its selected entrypoint, not unrelated open files. It retains
existing file jobs according to the normal Run cleanup settings. Switch the toolbar back to File for single-file F5.
Changing the selector controls the **next** Save/F5 action; it does not terminate jobs already
running. Mode is held in memory and only best-effort persisted, so blocked browser storage
cannot make the selector disagree with the action. Each action snapshots mode before waiting
for a foreground run to stop. The Stop button cancels all jobs after a project launch;
⋯ → Stop all jobs is also available.

Project saves compare formatted editor bytes with the actual device file before skipping a
transfer. An unchanged or reverted file sends no `put`; unchanged sources are still compile-checked.
A missing/changed file is uploaded even when the tab says saved. No previous-device cache is
trusted across reconnect/reset (or even between saves). This needs a read per candidate but
avoids flash writes for identical files. Communication/read failures block launch; successful
upload/verification is required before marking the text saved. Local files still ask for a path.

Project mode needs firmware with the compiler, scheduler and `project` command. Checks compile
individual scripts and their namespace dependencies across the mounted VFS, not a persistent linked `.mcsb`
assembly. Dependency resolution happens inside each VM, including isolated delayed/periodic job
VMs. For a two-file display app, only the application needs to be a job:

```text
startup /app.cs
```

If `app.cs` contains `using App.Display;` and `oled.cs` declares `namespace App.Display`,
`oled.cs` contributes declarations automatically without executing its top-level statements.
Runtime errors and actual device-resource availability are still determined at execution time.

Each file upload is atomic, **not the entire project**. Failed checks retain the previous job
configuration, not previous file contents; old file jobs may observe uploaded changes before
launch succeeds. Stop all jobs first when updating a live application. Uploading a nested
`jobs.cfg` does not change boot's `/jobs.cfg`; configure boot separately if persistence is wanted.

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
its file is deleted). So that re-running a script does not stack copies, *Run file* first sends
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

### Memory snapshot

The left panel shows **VM pool used / currently backed usable total**, followed
by an explicit **Free** value on the same line, in exact bytes:

```text
VM heap: 15,528 B / 32,712 B · Free: 17,184 B
```

**Total − used = free.** These are the allocator's physical pool counters, not
VM-tracked payload alone, a configured allocation/backing ceiling or device heap.
Only RAM already acquired by the VM pool is counted. The total can grow when the
provider actually obtains another region; possible future growth is not counted
as free memory. Native/BLE allocations and protected native reserves are not
added to the VM total.

Free is aggregate free pool space, not the maximum size of one object. Individual
allocations require a contiguous block and allocator overhead; inspect **largest**
in the expanded snapshot. Future pool growth remains conditional on native
headroom, reserves and configured ceilings. Limits are separately labeled.

Click to expand for the same used/total/free values, pool peak/largest block,
tracked allocations, stack/frames, backing ceiling and device heap. Device and
VM numbers are **not additive**. Firmware without pool-capacity telemetry shows
**VM heap capacity unavailable**, rather than guessing usable free memory from
VM limits, peaks or device counters.

The snapshot is refreshed after existing save/build/run workflows, including
failures; it is not a continuously polled meter. See
[memory budgets](../../docs/MEMORY.md) for scope, exclusions and the 40 KiB
source-Project regression/configuration.

## Older firmware and template compatibility

The gallery defaults to **Supported only**, remembered locally. On a connected board,
only examples whose exact requirements and minimum firmware are confirmed are shown;
unsupported examples and unknown legacy requirements are hidden. Turn the toggle off
to browse every category and copy/open examples for adaptation. Offline, all examples
remain visible and are marked **Not checked**. Adding a template does not run it.

Each preview shows **Requirements supported**, **Support unknown**, **Not supported**,
or **Not checked**, and lists exact missing/unreported requirements and any declared
minimum firmware. A missing legacy report is unknown, not proof that hardware exists.
The old compiler feature bit can establish source compilation support; HAL/network bits
do not establish individual peripherals. Checks cannot prove that external sensors are
attached or that every custom API matches. Optional `min firmware: major.minor.patch`
metadata belongs before a template's description. See the [template authoring guide](templates/README.md).

## Device Explorer tree

Expand/collapse folders without replacing the root view. Multiple branches and their files
remain visible together, with indentation and folder-first sorting. Closed folders are not
scanned; Refresh reloads expanded branches. The breadcrumb identifies the target folder for
uploads and new files. Opening a file selects its parent as that target. Keyboard navigation:
Up/Down, Home/End, Right to expand/enter, Left to collapse/go to parent, Enter/Space to open.
Folder load errors remain visible with a retry hint; late replies from a disconnected or
replaced device cannot repopulate the tree. Explorer traversal is bounded to 128 expanded
folders / 32 levels. Rename/move updates descendant tab paths and expanded branches.

Identifier highlighting is lexical rather than a full semantic compiler: declared fields,
locals and parameters keep a consistent color irrespective of name casing or `this.` access.
User-declared types, methods, keywords, literals and comments retain distinct classifications.

### Files changed by scripts

Clean device tabs reload after a run or Explorer Refresh, even for same-size
changes and files in collapsed folders. Selecting a tab or returning focus to
Studio checks the visible file when the connection is idle in machine mode.

**Periodic device polling is off by default.** There are no recurring filesystem
reads or serial transfers for this feature unless you enable **⋯ → Live device
file refresh (uses serial bandwidth)**. The optional mode checks only the visible
clean tab every 2.5 seconds while idle. File reads still use MCU filesystem time
and serial bandwidth; this is not a zero-cost real-time filesystem watcher.
Turning the option off prevents future periodic reads (an already-started read
may finish). The preference is saved in the browser, not in firmware.

Checks do not interrupt foreground scripts, active transfers, or the REPL.
While a foreground script owns the connection, its changes become visible after
it finishes/stops. Unsaved edits and local computer files are never overwritten;
unreadable/deleted device files keep their last editor copy. Automatic updates
retain the editor selection and scroll without moving keyboard focus. This fix
changes no MCU/runtime code, timers, scheduling, or firmware configuration. The
bounded post-run/on-demand file reads still have a transport/filesystem cost.

### Duplicate tabs and upload speed

Repeated clicks on one device path share a single download and focus one tab.
Normalized aliases such as `/lib/../app.cs` also reuse that tab. Existing unsaved
edits are not replaced; conflicting drafts from old sessions are recovered as
untitled `recovered-...` copies requiring an explicit destination when saved.

For repeated File-mode saves/runs, saved files are checked against live device
bytes instead of uploaded again. Missing/externally changed files still upload;
checks and automatic application cleanup are not skipped. No persistent device
content cache is trusted. Dirty files upload directly in File mode, avoiding an
extra pre-upload download on the common edited-file path.

UART speed is still limited by baud rate, filesystem writes and the firmware's
acknowledged upload windows. A higher baud rate must be configured **both** in
the firmware console and in Studio; changing only Studio breaks communication.
Test a supported higher rate on the actual board/cable. USB serial implementations
may not use the UART baud setting for physical throughput. Do not disable upload
flow control or enlarge firmware buffers blindly on a low-RAM board. These Studio
changes require no firmware update and add no polling or application-runtime work.


## Studio 1.19 editor improvements

Dark and Light use the Visual Studio 2022 palette declared at the top of
`studio.css`: neutral charcoal editor/tool surfaces in Dark, white/light-gray
surfaces in Light, and blue reserved for selection and active-control accents.
Workbench layout rules do not override the palette with blue-gray colors.

The command palette lists available actions and marks device-dependent commands
unavailable while offline. Escape restores the previous focus. Ctrl+P searches
only already-open tabs, without reading the device or overwriting drafts.
Document tabs support Left/Right and Home/End navigation. The editor starts
directly below the tabs, without a duplicate filename/origin strip. Tabs retain
unsaved-change indicators and full-path tooltips; the bottom status bar retains
the active path.

IntelliSense filters declarations by lexical scope and infers `this`, `base`,
unqualified method results, nested generic arguments and dictionary value indexing.
Raw strings and multiline comments cannot introduce fake declarations. Unknown
receivers offer broad fallback members only on explicit Ctrl+Space. This remains
an offline lightweight language service, not Roslyn or a compiler diagnostic engine.

Formatter options accept `indentSize: 2`, `4` (default), or `8` through the
`StudioFormat.format` JavaScript API. The editor keeps its four-space convention.
Formatting remains whitespace-only, preserves literal/comment bytes and CRLF,
and maps selections/carets through edits. No new serial polling is introduced.
