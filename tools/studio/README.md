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
| `format.js` | offline, whitespace-only C# formatter; protects literals and comments |
| `lang.js` | C# language service: symbols of the open file, type inference, call context |

No build step for the app: edit a file and reload the page. After changing a template run
`python3 tools/studio/gen_templates.py` (or `make studio-templates`).

## Editor keys

| Keys | Action |
|---|---|
| Ctrl+Space · Ctrl+Shift+Space | completion (with docs) · parameter info (↑/↓ switch overloads) |
| Ctrl+F · Ctrl+H · F3 / Shift+F3 | find · replace · next / previous match |
| Ctrl+G · Ctrl+] | go to line · jump to the matching bracket |
| Shift+Alt+F | format document |
| Alt+↑/↓ · Shift+Alt+↑/↓ · Ctrl+D · Ctrl+Shift+K | move line · copy line · duplicate · delete line |
| Ctrl+/ · Tab / Shift+Tab | toggle comment · indent / outdent |
| F5 | save/build, then run the active file or the Project jobs |
| Ctrl+Enter · Ctrl+Shift+Enter | save/build and run the active file · run the selection |
| Ctrl+` · Ctrl+J · Alt+Tab | hide/show the bottom terminal (also the toolbar terminal button or panel ×) |
| Ctrl+S | save/upload and compile-check (no execution) |

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
  Documents with multiline literals/comments retain their line structure.
* **Save/upload all open C# files and compile**: Ctrl+S, F5, and Run file upload every
  editable open C# tab in **File mode**, even unchanged ones, plus the explicitly saved file.
  **Project mode** uploads only new/changed device files (see below). New/local tabs
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
option and retains existing file jobs. Switch the toolbar back to File for single-file F5.
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

The left panel shows compact VM heap telemetry by default
(`VM heap: used · peak`, in KB). Click it to expand the full memory snapshot,
and click again to collapse it. The peak is the lifetime high-water mark, so it
does not decrease when garbage is collected; current usage should settle after
one-time lazy runtime initialization.

The expanded view distinguishes VM-tracked allocations, full physical VM pool
usage/capacity/peak/free/largest block, and optional ESP32 device heap telemetry.
Values there are exact bytes. Device heap already includes the reserved VM pool:
the numbers are not additive. Older firmware explicitly shows missing capacity;
Studio never guesses it from a VM peak. Failed workflows also refresh the
snapshot. See [memory budgets](../../docs/MEMORY.md) for scope, exclusions and the
40 KiB source-Project regression/configuration.

## Older firmware and template compatibility

All templates and categories stay visible, online and offline. Each template shows
**Requirements supported**, **Support unknown**, **Not supported**, or **Not checked**.
The preview lists exact missing/unreported requirements and any declared minimum firmware.
A missing legacy report is unknown, not proof that hardware exists. The old compiler feature
bit can establish source compilation support; HAL/network bits do not establish individual
peripherals. Templates can always be opened/copied for adaptation; that does not execute them.
Support checks cannot prove that external sensors are attached or that every custom API matches.
Optional `min firmware: major.minor.patch` metadata belongs before a template's description.

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
