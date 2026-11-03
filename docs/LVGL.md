# LVGL graphics

MicroCS can drive [LVGL](https://lvgl.io) (8.x and 9.x) from C# scripts. The script calls LVGL with **LVGL's own C names**, so the LVGL
documentation and examples translate almost line by line:

```csharp
var btn = lv_button_create(lv_screen_active());          // lv_btn_create on LVGL 8 works too
lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
var label = lv_label_create(btn);
lv_label_set_text(label, "Click me");
lv_obj_add_event_cb(btn, e => Console.WriteLine("clicked"), LV_EVENT_CLICKED, null);
```

Contents: [Design](#design) · [Turning it on](#turning-it-on) · [What the firmware must do](#what-the-firmware-must-do) ·
[Writing scripts](#writing-scripts) · [Files, images and fonts](#files-images-and-fonts) · [Configuration](#configuration) ·
[The C API](#the-c-api) · [Trying it on a PC](#trying-it-on-a-pc) · [Studio](#studio) · [Tests](#tests) ·
[How the API tables are generated](#how-the-api-tables-are-generated) · [Limits](#limits)

## Design

* **MicroCS does not bundle LVGL and does not do your porting.** Your firmware (or your project) owns LVGL: it links LVGL, calls
  `lv_init()`, creates the display and the input devices, feeds `lv_tick_inc()` and runs `lv_timer_handler()`. MicroCS only exposes the
  LVGL API to scripts.
* **No hand-written wrapper per widget.** `tools/lvgl_gen` parses the LVGL headers (pycparser) and generates the name tables and one
  wrapper per function for **every LVGL release 8.0 to 9.x** into one file, guarded by `LVGL_VERSION_*` and the `LV_USE_*` switches of
  your `lv_conf.h`. The same MicroCS sources therefore build against whatever LVGL you link. The hand-written part
  (`modules/lvgl/mcs_lvgl.c`, `mcs_lvgl_more.inc`, `mcs_lvgl_macros.inc`) only covers value conversion, object lifetime, callbacks and the
  file-system driver.
* **Costs no RAM until used.** The tables are flat `const` flash tables; a name is looked up when a script uses it (`MCS_ROM_GLOBALS_MAX`
  allows the two tables the binding needs). A firmware without `MCS_ENABLE_LVGL` has nothing of this in it.
* **Tested on the host** against LVGL 8.3.11 and 9.2.2 (`make lvgl-test`, `tests/lvgl/t01`…`t14`) and compiled against the oldest, a middle and the
  newest release of each major in CI. See [Limits](#limits) for what has not been run on real targets.

## Turning it on

You need an LVGL checkout (or component/package) and `MCS_ENABLE_LVGL=1`. Everything else (include path, tables) is found by the build
files. The tables are generated at configure time from **your** LVGL headers (needs Python 3 and `pip install pycparser`), or you pass a
ready-made table file (`MICROCS_LVGL_API_INC`, no Python needed; build it once with `make lvgl-api` which writes
`modules/lvgl/mcs_lvgl_api.inc` for all releases).

| Build | How |
|---|---|
| **CMake** (plain, host, any project) | `-DMICROCS_LVGL=ON -DMICROCS_LVGL_DIR=/path/to/lvgl`. Without `MICROCS_LVGL_DIR` the build uses an existing `lvgl` target, an `lvgl/` folder or `ZEPHYR_LVGL_MODULE_DIR`. `-DMICROCS_LVGL_API_INC=file.inc` uses ready-made tables. `-DMICROCS_LVGL_BUILD=ON` (host) also compiles LVGL and the in-memory display, giving `mcs --lvgl`. Also: `MICROCS_LVGL_FS_LETTER`, `MICROCS_LVGL_AUTO_HANDLER`, `MICROCS_LVGL_CLEANUP_ON_CLOSE`. The CMake logic is in `cmake/MicroCSLvgl.cmake`. |
| **ESP-IDF** | Add the `lvgl/lvgl` component to your project, then `menuconfig` → MicroCS → LVGL (`CONFIG_MICROCS_LVGL*`). The MicroCS component adds `lvgl__lvgl` / `lvgl` to its requirements when the project has it. |
| **Zephyr** | `CONFIG_LVGL=y` and `CONFIG_MICROCS_LVGL=y` (depends on LVGL); the module finds the Zephyr LVGL module. |
| **Arduino** | `python3 tools/make_arduino.py --lvgl --lvgl-dir /path/to/lvgl` (or `--lvgl-api-inc file.inc`) writes the tables into the package and adds `depends=lvgl`. Install the Arduino `lvgl` library and put your `lv_conf.h` where that library expects it. |
| **PlatformIO** | `build_flags = -DMCS_ENABLE_LVGL=1` and `lib_deps = lvgl/lvgl`; `tools/pio_build.py` generates the tables (ini options `custom_microcs_lvgl_dir`, `custom_microcs_lvgl_api_inc`). |
| **Makefile** (`microcs.mk`) | `MICROCS_LVGL_DIR=...` or `MICROCS_LVGL_API_INC=...` before `include microcs.mk`. |
| **Other build** | Define `MCS_ENABLE_LVGL=1`, put LVGL on the include path (or define `MCS_LVGL_INCLUDE="path/lvgl.h"`), add `modules/lvgl/mcs_lvgl.c`, and define `MCS_LVGL_API_INC="path/to/mcs_lvgl_api.inc"`. |

A script can ask whether the firmware has it:

```csharp
if (!Hal.Has("Lvgl")) { Console.WriteLine("no LVGL in this firmware"); return; }
```

## What the firmware must do

MicroCS calls LVGL only from the thread that runs the VM, and LVGL is not thread-safe, so:

```c
lv_init();
/* create the display (flush callback, buffers) and the input devices (touch, encoder, keys) */
mcs_vm_t* vm = mcs_new(...);
mcs_lvgl_open_lib(vm);               /* makes the lv_* names visible to scripts of this VM */
```

* **Tick and handler.** Keep calling `lv_tick_inc()` (timer, `LV_TICK_CUSTOM`, or an OS hook) and `lv_timer_handler()`. Either your main
  loop calls the handler, or you set `MCS_LVGL_AUTO_HANDLER=1` and the runtime calls it while a script sleeps (`Thread.Sleep`,
  `Scheduler`). Scripts can also call `lv_timer_handler()` themselves in a loop, like `while (true) { lv_timer_handler(); Thread.Sleep(5); }`.
* **With an RTOS GUI task** define `MCS_LVGL_LOCK()` / `MCS_LVGL_UNLOCK()` (for example `lvgl_port_lock(-1)` / `lvgl_port_unlock()`); every
  LVGL call and every callback into a script is then wrapped.
* `mcs_lvgl_open_lib` also registers the LVGL file system (see below) when the VM has a VFS.
* Closing the VM (`mcs_free`) removes the callbacks, timers and animations that the scripts registered and, with
  `MCS_LVGL_CLEANUP_ON_CLOSE=1` (default), deletes the objects the scripts created, so a re-uploaded script starts on a clean screen.

## Writing scripts

### Names and versions

Every function, enum constant, `LV_*` macro constant, `LV_SYMBOL_*` string, built-in font (`lv_font_montserrat_14`...) and struct type
keeps its C name. Only the names your LVGL really has exist; a name that is missing (an `LV_USE_*` feature that is off, a function
from a newer LVGL) raises `MissingMemberException` when it is used. For example, animation deleted callbacks and LVGL 8 messages require 8.3 or later; descriptor-based event removal requires 9.1 or later on LVGL 9.

**v8 and v9 spellings both work.** `lv_screen_active()` and `lv_scr_act()`, `lv_button_create` and `lv_btn_create`, `lv_obj_delete` and
`lv_obj_del`, `lv_obj_send_event` and `lv_event_send`, `lv_binfont_create` and `lv_font_load`... map to whatever your LVGL has (taken from
LVGL's own `lv_api_map_*.h` plus a short list in the generator). A build for one LVGL gets the other major's names through
`tools/lvgl_gen/renames.json`, which the all-releases run writes. The one thing the maps cannot fix is a function whose *meaning* changed
between releases; check the LVGL changelog for those.

### Types

| LVGL | C# |
|---|---|
| integers, `bool`, `lv_coord_t`, enums, `LV_*` constants | `int` (enums are plain ints, so `LV_ALIGN_CENTER`, `LV_EVENT_CLICKED | ...` as ints work) |
| `lv_color_t` | `int` `0xRRGGBB`: `lv_color_hex(0xFF8800)`, `lv_color_make(r, g, b)`, `lv_palette_main(LV_PALETTE_RED)` return and accept ints |
| `lv_opa_t`, `lv_part_t`, `lv_state_t` | `int` (`LV_OPA_50`, `LV_PART_MAIN | LV_STATE_PRESSED`) |
| `const char*` argument | `string` |
| `char*` / `uint8_t*` data in | `byte[]` or `string`; arrays of numbers: `int[]`, `byte[]` |
| `const char*` result | `string` |
| `lv_obj_t*`, `lv_style_t*`, `lv_font_t*`, `lv_anim_t*`... | an object whose class is the C type name |
| struct values (`lv_point_t`, `lv_area_t`, `lv_color16_t`...) | an object of that type with its members: `a.x1`, `p.y` |
| out-parameters / structs to fill | create it and pass it: `var a = new lv_area_t(); lv_obj_get_coords(o, a); a.x2` |
| arrays of pointers or structs (`const char*[]` maps, `lv_point_t[]`, grid descriptors) | C# arrays: `new string[] { "A", "B", "" }`, `new int[] { LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST }` |
| callbacks (`lv_event_cb_t`, `lv_timer_cb_t`, `lv_anim_exec_xcb_t`...) | a lambda or a method group, see below |
| `void* user_data` | any C# value (kept alive for as long as LVGL can call back) |
| `NULL` | `null` |
| macros used like functions (`LV_PCT`, `LV_GRID_FR`, `LV_SIZE_CONTENT`, `lv_color_hex`...) | the same names, as functions or constants |

Rules worth knowing:

* **Objects have identity.** The same C object always gives the same C# value, so `lv_event_get_target(e) == btn` works and an object
  can be a dictionary key.
* **Objects that LVGL deletes are disposed.** After `lv_obj_delete(o)`, or after a parent that contained `o` was deleted, using `o`
  raises `ObjectDisposedException` instead of touching freed memory.
* **Memory the C API wants to keep** (a `lv_style_t`, an `lv_anim_t`, label text buffers of `lv_label_set_text_static`, button-matrix
  maps, chart point arrays, grid descriptors, `lv_line` points) is allocated by the binding and kept until the owner or the VM goes
  away, so scripts can pass temporary C# arrays and strings. `new lv_style_t()` storage lives until the VM closes, because LVGL keeps
  pointers to styles.
* **Strings returned by LVGL** (`lv_label_get_text`, `lv_dropdown_get_selected_str`) are copied into C# strings.
* Not wrapped: `lv_init`, `lv_deinit`, driver and port-level callbacks (`lv_display_set_flush_cb`, `lv_indev_set_read_cb`...), raw memory
  (`lv_malloc`...), `lv_ll`/`lv_rb`/cache/OS helpers and draw-engine internals. The complete list with the reason is the `unsupported`
  object in `build/lvgl-api/lvgl_api.json` (written by `make lvgl-api`).

### Events

```csharp
var count = 0;
lv_obj_add_event_cb(btn, e =>
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) { count++; lv_label_set_text(label, "Clicked " + count); }
}, LV_EVENT_ALL, null);
```

The event argument is an `lv_event_t` that is valid only inside the callback. `lv_event_get_target(e)`, `lv_event_get_current_target(e)`,
`lv_event_get_user_data(e)`, `lv_event_get_param(e)` and the typed getters work. A lambda may declare fewer parameters than LVGL passes. An exception thrown in a callback ends the callback, is kept
pending and surfaces in the script call that started LVGL (`lv_timer_handler()`, `lv_obj_send_event()`...), or is reported by the runtime when
there is none; handlers stay registered. `lv_obj_remove_event_cb`, `lv_obj_remove_event_dsc` and `lv_obj_remove_event`
remove handlers again. The delete event of an object still reaches its handler.

### Timers, animations and async calls

```csharp
var t = lv_timer_create(tm => Console.WriteLine("tick"), 500, null);
lv_timer_set_repeat_count(t, 10);

var a = new lv_anim_t();
lv_anim_init(a);
lv_anim_set_var(a, obj);
lv_anim_set_exec_cb(a, (o, v) => lv_obj_set_x(o, v));          // lv_anim_set_custom_exec_cb on LVGL 9
lv_anim_set_values(a, 0, 100);
lv_anim_set_time(a, 500);
lv_anim_set_path_cb(a, lv_anim_path_ease_in_out);
lv_anim_start(a);

lv_async_call(x => Console.WriteLine("later"), null);
```

Timers, animations and async calls are removed when the VM is freed. Callbacks run inside `lv_timer_handler()` (or the call that sends the event), on the thread that runs the VM; see [THREADS](THREADS.md).

### Styles, layouts and widgets

```csharp
var st = new lv_style_t();
lv_style_init(st);
lv_style_set_radius(st, 12);
lv_style_set_bg_color(st, lv_color_hex(0x2196F3));
lv_obj_add_style(btn, st, 0);
lv_obj_add_style(btn, pressed, LV_STATE_PRESSED);

lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
var cols = new int[] { 100, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
var rows = new int[] { 40, 40, LV_GRID_TEMPLATE_LAST };
lv_obj_set_grid_dsc_array(grid, cols, rows);
lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
```

Charts (`lv_chart_set_ext_y_array`, `lv_chart_set_series_values`...), `lv_line_set_points`, `lv_buttonmatrix_set_map`, `lv_keyboard_set_map`,
`lv_dropdown`/`lv_roller` options, `lv_canvas`, `lv_calendar`, `lv_msg`, observers/subjects (LVGL 9), `lv_obj_tree_walk`, `lv_log` and
`lv_obj_set_user_data` all work through the conversions above. The Studio template category **LVGL graphics** has a runnable example for each
topic (hello label, events, styles, layouts, animations, timers, widgets, a live chart, screens, images and fonts from files,
keyboard input, calendar, canvas, an arc dashboard, tree walking, and version-independent code). Five standalone examples are also in
[`examples/lvgl`](../examples/lvgl/README.md).

## Files, images and fonts

When the firmware has a file system (`MCS_ENABLE_FS` and a VFS: LittleFS, YAFFS, RAM, POSIX...), `mcs_lvgl_open_lib` registers it as the LVGL
drive **`M:`** (`MCS_LVGL_FS_LETTER`, `0` = none). Script files are what scripts already see in `File`:

```csharp
lv_image_set_src(img, "/img/logo.png");              // a path starting with / gets the drive letter: "M:/img/logo.png"
lv_image_set_src(img, "M:/img/logo.bin");            // or write the letter yourself
var font = lv_binfont_create("/fonts/big.bin");      // lv_font_load on LVGL 8; an LVGL binary font from lv_font_conv
lv_obj_set_style_text_font(label, font, 0);
var ttf = lv_tiny_ttf_create_file("/fonts/Roboto.ttf", 24);   // TinyTTF, when LV_USE_TINY_TTF is on
```

* Image formats are the ones your `lv_conf.h` enables decoders for: **PNG** (`LV_USE_LODEPNG`/`LV_USE_LIBPNG`), **JPG**
  (`LV_USE_TJPGD`/`LV_USE_LIBJPEG_TURBO`), **BMP** (`LV_USE_BMP`), **GIF** (`LV_USE_GIF`, `lv_gif_create`/`lv_gif_set_src`), QR codes and barcodes
  (`LV_USE_QRCODE`, `LV_USE_BARCODE`) and LVGL's own `.bin`.
* A missing or unreadable file shows nothing; MicroCS keeps running.
* Free fonts loaded from files with `lv_binfont_destroy` / `lv_font_free` / `lv_tiny_ttf_destroy` once nothing uses them. These fonts are **not** automatically freed when the VM closes. A script interrupted before cleanup can leak them; arrange cleanup in your host lifecycle if interruption is possible. The wrapper passed to the destroy function is disposed, but independently obtained aliases are not invalidated: never reuse any alias after freeing a font.
* Call `lv_style_reset` on each script-created style after deleting its users or removing the style from them. Closing the VM frees the style struct, but does not reset its LVGL-allocated properties. Similarly, do not leave a surviving object (including the original screen) referring to script-owned styles, static strings, arrays, or fonts after the VM closes. With `MCS_LVGL_CLEANUP_ON_CLOSE=0`, the host must remove such references before closing. Automatic cleanup cannot safely infer ownership of references on host objects.
* Put the files on the device with your normal tooling (Studio's file manager, `mcs --fs DIR` on the PC, ...). See [FILESYSTEM](FILESYSTEM.md).

## Configuration

Set these in `mcs_user_config.h` or with `-D` (documented in [CONFIGURATION](CONFIGURATION.md)):

| Option | Default | Meaning |
|---|---|---|
| `MCS_ENABLE_LVGL` | `0` | compile the binding (needs LVGL on the include path) |
| `MCS_LVGL_INCLUDE` | `<lvgl.h>` search | header to include instead, e.g. `"lvgl/lvgl.h"` |
| `MCS_LVGL_API_INC` | set by the build files | path of the generated tables |
| `MCS_LVGL_FS_LETTER` | `'M'` | LVGL drive letter of the MicroCS VFS, `0` = no file system driver |
| `MCS_LVGL_AUTO_HANDLER` | `0` | `1`: `lv_timer_handler()` runs while scripts sleep and in the runtime loop |
| `MCS_LVGL_CLEANUP_ON_CLOSE` | `1` | closing the VM deletes the objects, timers and animations of its scripts |
| `MCS_LVGL_LOCK()` / `MCS_LVGL_UNLOCK()` | empty | wrap every LVGL call (GUI mutex); define both |
| `MCS_ROM_GLOBALS_MAX` | `4` | flat flash tables per VM; the binding uses one or two |

## The C API

`include/mcs_lvgl.h`:

```c
bool     mcs_lvgl_open_lib(mcs_vm_t* vm);       /* after lv_init(); false when LVGL is not compiled in */
void     mcs_lvgl_close_lib(mcs_vm_t* vm);      /* called by mcs_free; call earlier to detach */
bool     mcs_lvgl_fs_register(void* vfs, char letter);   /* serve a MicroCS VFS as an LVGL drive */
uint32_t mcs_lvgl_poll(mcs_vm_t* vm);           /* one lv_timer_handler(); returns ms to the next timer */
uint32_t mcs_lvgl_version(void);                /* MAJOR*10000 + MINOR*100 + PATCH of the LVGL it was built against */
```

## Trying it on a PC

`make mcs-lvgl` builds LVGL and MicroCS into `build/mcs_lvgl` with an in-memory display and a simulated pointer, so scripts run without
hardware:

```sh
make fetch-lvgl LVGL_VERSION=v8.3.11          # clones LVGL into build/third_party (any 8.x/9.x tag)
make -j8 mcs-lvgl LVGL_DIR=build/third_party/lvgl-v8.3.11
build/mcs_lvgl --sim --lvgl=320x240 tools/studio/templates/33-lvgl/hello_label.cs      # Ctrl+C stops it; --fs DIR serves script files
```

* `--lvgl[=WxH]` creates the display (default 320x240) and an input pointer.
* Host-only helpers for tests, screenshots and demos: `lvgl_host_run(ms)` advances LVGL time and runs the timers,
  `lvgl_host_pixel(x, y)` returns the rendered colour `0xRRGGBB`, `lvgl_host_touch(x, y, down)` moves, presses or releases the pointer and
  `lvgl_host_screenshot("/shot.ppm")` writes the screen as a PPM through the host file system.
* `mcs --features` lists `lvgl` for such a build. `LVGL_TABLES=all` uses the all-releases tables instead of tables for exactly this LVGL.
* `make lvgl-test LVGL_DIR=...` runs `tests/lvgl/*.cs` against their `.out` files.
* With CMake: `-DMICROCS_LVGL=ON -DMICROCS_LVGL_DIR=... -DMICROCS_LVGL_BUILD=ON`.

## Studio

[MicroCS Studio](../tools/studio/README.md) knows the LVGL API: completion lists every `lv_*` function, `LV_*` constant and type with its
signature, hover shows the C prototype and signature help follows the cursor. The data
(`tools/studio/lvgl_api.js`, about 350 KB) loads lazily the first time a script mentions `lv_` or `LV_`, so Studio starts as fast as before.
The **LVGL graphics** template category needs a firmware that reports the `Lvgl` capability; other firmwares hide it.

## Tests

* `make lvgl-test LVGL_DIR=<v8 or v9 checkout>`: `tests/lvgl/t01…t17` run on the in-memory display and are compared with their `.out`. The first
  line of a test is `// args: …` (options for `mcs`), an optional second line `// only: v8` or `// only: v9` skips it on the other major.
  Covered: objects, styles, events, timers and animations, deleted objects, v8 names on both, file system (BMP, PNG, binary fonts, TinyTTF),
  widgets, arrays, callbacks, observers, `lv_msg`, the capability, object/callback/GC churn, and explicit font cleanup.
* `node tests/studio/test_lvgl.js`: IntelliSense data; `node tests/studio/test_templates.js build/mcs_lvgl --only=lvgl`: the Studio LVGL templates run
  on the in-memory display.
* ASan/UBSan with LVGL using the C allocator and LeakSanitizer expose native lifetime issues that the default LVGL pool allocator can hide. In LVGL 9.2.2, drawing a partially populated line chart can trigger a signed-overflow UBSan report in LVGL `lv_chart.c:847` (the `LV_CHART_POINT_NONE` sentinel); this is upstream code, not a clean sanitizer result.
* CI (`.github/workflows/ci.yml`): `lvgl` (8.3.11 and 9.2.2: tests and templates), `lvgl-api` (tables for all releases, compile against
  8.0.0, 8.2.0, 8.4.0, 9.0.0, 9.1.0 and 9.6.0, generated Studio data is current), `lvgl-build-files` (CMake host builds, a project with its own `lvgl` target,
  the Arduino package, `microcs.mk`).

## How the API tables are generated

```text
tools/lvgl_gen/extract.py      preprocesses one LVGL release with pycparser: functions, structs, enums, macro constants
tools/lvgl_gen/gen.py          merges releases (names, signatures, version and LV_USE_ guards) and writes the tables + wrappers
tools/lvgl_gen/build_api.py    --all: fetches v8.0.0 ... v9.6.0 and writes modules/lvgl/mcs_lvgl_api.inc, lvgl_api.json and renames.json
                               --lvgl DIR: tables for one checkout (what the build files run at configure time)
tools/lvgl_gen/studio_data.py  lvgl_api.json -> tools/studio/lvgl_api.js (IntelliSense; --check in CI)
tools/lvgl_gen/renames.json    v9 name -> v8 name pairs for single-LVGL builds (written by the all-releases run, committed)
```

The generated `.inc` files are **not committed** (they are large and depend on the LVGL releases); `make lvgl-api` or the build files
create them, and the CI `lvgl-api` job publishes the all-releases tables as the `mcs_lvgl_api` artifact. Functions whose signature the generator cannot map
(function-pointer arguments that need a hand-written trampoline, unions, pointer-to-pointer outputs...) are skipped and listed in
`unsupported`; the ones scripts need most are in `OVERRIDE` in `gen.py` with a hand-written wrapper.

## Limits

* LVGL runs on one thread. Use `MCS_LVGL_LOCK` with an RTOS GUI task; do not call LVGL from a second script thread without it.
* Scripts are interpreted: they are fine for building screens and reacting to events, not for per-pixel drawing in a draw callback.
  Heavy work (image decoding, fonts) is LVGL's C code and runs at full speed.
* Memory: LVGL's own pool (`LV_MEM_SIZE`) holds the widgets; MicroCS adds only the C# values that refer to them. See [MEMORY](MEMORY.md).
* Tested on the host with LVGL 8.3.11 and 9.2.2 and compiled against 8.0.0 … 9.6.0. The **ESP-IDF, Zephyr and PlatformIO integrations are written but
  have not been built here** (no toolchains), and nothing has run on a real display yet; please report problems.
* The v8/v9 name maps cover renames, not behaviour changes between LVGL releases.
