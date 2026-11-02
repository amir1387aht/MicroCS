# LVGL support: work-in-progress handoff

Status: **early WIP, nothing works end to end yet.** This branch has the design
and the first piece of the binding generator (`tools/lvgl_gen/extract.py`, not
tested yet). No core VM, module, build, docs or Studio changes are in it yet.

## Goal
* LVGL **8.0.0 → 9.6.0** behind one optional module (`MCS_ENABLE_LVGL`, default 0).
* C# calls LVGL with the **same names as C**: `lv_obj_create(null)`, `LV_ALIGN_CENTER`,
  `LV_SYMBOL_OK`, `lv_font_montserrat_14`, `lv_obj_t`, `new lv_style_t()`.
* The user does the porting (display/indev drivers, `lv_init`, tick). MicroCS finds the
  LVGL headers and exposes the API.
* LVGL reads files through the MicroCS VFS (LittleFS / YAFFS / TinyFS / RAM / POSIX),
  so it can load fonts and images of any format LVGL can decode.
* Docs, README, Studio templates and IntelliSense, tests, CI.

## Local environment used (not in repo)
* LVGL clone: `/data/lv/repo` (blobless). Worktrees for every release tag in `/data/lv/<tag>`
  (v8.0.0 … v8.3.11, v8.4.0, v9.0.0 … v9.6.0). To recreate one:
  `git clone --filter=blob:none https://github.com/lvgl/lvgl lv/repo && cd lv/repo && git worktree add -f ../v9.2.2 v9.2.2`
* `pip install pycparser`. gcc only (cmake can be installed with pip).
* Extractor test run (was still running at handoff):
  `cd /data/lv && python3 MicroCS/tools/lvgl_gen/extract.py v9.2.2 work v9.2.2 work/v9.2.2.json`

## What `tools/lvgl_gen/extract.py` does (per LVGL tag)
1. Writes an "everything on" `lv_conf.h` from `lv_conf_template.h`: all widgets, themes,
   layouts, decoders and extras on; drivers, GPUs, OS, fs drivers and demos off.
2. Runs `gcc -E` on `lvgl.h` with a small fake libc. Missing third-party headers are
   stubbed automatically. Then it parses the result with pycparser.
3. Records functions (including static inline ones), enumerators, extern objects
   (fonts, classes), typedefs and complete structs/unions, along with:
   * the **`#if` conditions** of the declaring header line, plus the conditions on
     the include chain (from linemarkers). Include guards and `__cplusplus` are dropped.
     These conditions become feature guards in the generated C.
   * Doxygen brief/@param/@return text (for Studio IntelliSense and the docs).
4. Sorts the object-like `LV_*` macros with the real compiler into int constants,
   string constants (`LV_SYMBOL_*`) and aliases (`#define old new`, including the
   v8→v9 api_map files). It also lists the function-like macros.

Likely next fixes: pycparser may choke on some GNU-isms, and stub headers may need
typedefs (FreeType, ThorVG, ffmpeg). Iterate until all tags parse.

## Design to implement (decided)
### 1. Core VM: lazy ROM global tables (src/mcs_lib.c, mcs_vm.c, mcs_compiler.c, include/mcs.h)
```c
typedef struct {
    const mcs_reg_t* fns;   uint32_t nfns;     /* sorted by strcmp; 'f' = function, 'g' = value getter (strings, fonts) */
    const mcs_const_t* consts; uint32_t nconsts; /* sorted */
    const mcs_class_def_t* const* classes; uint32_t nclasses; /* sorted by name */
} mcs_rom_globals_t;
void mcs_register_rom_globals(mcs_vm_t* vm, const mcs_rom_globals_t* t); /* static storage, ~4 per VM */
mcs_native_fn mcs_native_fn_of(mcs_value_t v); /* map native C# value -> C fn (for callbacks like lv_anim_path_ease_out) */
```
* Binary search on name/len. Entries resolve on first `GET_GLOBAL` of an UNDEF slot
  (`mcs_lazy_resolve`, around `src/mcs_vm.c:1150`), and in `resolve_guarded` /
  `mcs_global_exists` (vm.c:62-80) and `is_toplevel_name` (compiler ~426).
* Only **classes** count as types (`mcs_lazy_known`, used by `known_type_name`
  compiler ~231 and `looks_like_interface` ~2012). Functions and constants must
  **not** count as types. Add a separate `mcs_lazy_known_any`.
* Classes are created with `define_native_class` (mcs_lib.c), not `lazy_add`.
* Natives carry no user data (`mcs_native_t {fn, arity, name}`), so you need one C wrapper per LVGL function.

### 2. Module `modules/lvgl/` + `include/mcs_lvgl.h`
* It must compile to nothing when `MCS_ENABLE_LVGL=0`: the Makefile auto-builds
  `modules/*/*.c`, and `make check` uses `-Werror`.
* Header detection: `MCS_LVGL_INCLUDE` → `__has_include(<lvgl.h>)` → `"lvgl/lvgl.h"` →
  `#error` (same pattern as `mcs_drv_u8g2.c`). Version:
  `MCS_LV_VER = MAJOR*10000+MINOR*100+PATCH`. `#error` below 8.0.
* C API: `mcs_lvgl_open_lib(vm)`, `mcs_lvgl_close_lib(vm)`, `mcs_lvgl_fs_register(vfs, letter)`
  (default `'M'` via `MCS_LVGL_FS_LETTER`; paths starting with `/` get the `M:` prefix
  automatically), `mcs_lvgl_poll(vm)`. Options: `MCS_LVGL_AUTO_HANDLER` (run
  `lv_timer_handler` from the idle hook and the runtime loop),
  `MCS_LVGL_LOCK/UNLOCK`, `MCS_LVGL_CLEANUP_ON_CLOSE`.
* Open it automatically in `modules/runtime/mcs_runtime.c rt_open_modules` (~161) and in
  `ports/unix/main.c`. Add a new ext slot `MCS_EXT_LVGL` in `include/mcs.h:457`.
* Type mapping:
  * `lv_color_t` ↔ int `0xRRGGBB`.
  * Integer typedefs and enums ↔ int; `lv_value_precise_t` ↔ number; `const char*` ↔ string.
  * `char*` buffers ↔ `byte[]` (there is no StringBuilder).
  * Opaque pointers ↔ userdata classes named like the C type, with `op_Equality`/`op_Inequality`.
  * Complete structs ↔ value classes with field get/set (`new lv_area_t()`).
  * Scalar `T*` ↔ array (copy back for out params).
  * `void*` ↔ null, wrapper, string or byte[] (only for the duration of the call).
  * `user_data` is stored as a C# value in our record.
* `lv_obj_t` tracking: hash map ptr→record plus an LV_EVENT_DELETE hook. A deleted object
  raises ObjectDisposedException, and its callbacks and kept-alive buffers are freed.
  C# values are rooted in **one pinned List with a freelist** (`mcs_pin` only has 16 slots).
* Keep-alive for functions that store pointers: button/keyboard maps, image src,
  dropdown text, line points, chart ext arrays, calendar dates, canvas buffer,
  grid dsc arrays, style transition props, `*_static` setters.
* Callback trampolines: events (`lv_obj_add_event_cb`, the remove variants,
  `lv_event_get_user_data`), timers, anims (refcounted record in anim user_data;
  `custom_exec_cb` / `deleted_cb`; native LVGL fns passed directly), `lv_async_call`,
  group focus/edge, observers (v9), `lv_obj_tree_walk`, v8 `lv_msg`, log print cb,
  theme apply cb.
* Callback errors: during a nested call the exception stays pending (`vm->run_depth > 0`,
  see `mcs_call_value` vm.c:2170). Skip any further callbacks while `mcs_has_exception`.
* Not wrapped (list these in the docs): `lv_init`/`lv_deinit`, display/indev driver
  callbacks, raw mem/ll/rb/cache/os/draw-internal functions, anything with
  unrepresentable types (the generator emits an "unsupported" report).
* VM close: load the initial screen, delete objects the script created, remove our
  event dscs, timers and anims.

### 3. Generator `tools/lvgl.py` (uses extract.py; subcommands `gen`, `fetch`, `where` like tools/u8g2.py)
* Merge the per-tag DBs:
  * Version guard = range of tags where the entry exists. Open-ended for the latest
    tag; no lower bound when it starts at v8.0.0.
  * Group tags by C signature; each group gets its own wrapper.
  * Emit **nested** `#if` blocks: version first, then feature conditions. This avoids
    undefined function-like macros showing up in `#if`.
* Output `modules/lvgl/mcs_lvgl_api.inc`:
  * wrappers using the C type spelled as in the headers (casts), so the compiler checks
    them across versions;
  * sorted ROM tables;
  * alias tables (v8 names on v9 via the api_map macros; v9 names on v8 where the
    signature matches);
  * struct classes;
  * a native→C fn map.
* Also output Studio IntelliSense data (`tools/studio/lvgl_api.js`) and the API list for the docs.

### 4. Build integration (follow the u8g2 precedent everywhere)
* Files: `include/mcs_config.h` (~418), `config/mcs_user_config.h` (~256), `Kconfig`,
  `zephyr/Kconfig` + `CMakeLists.txt`, `cmake/MicroCSLvgl.cmake` (`MICROCS_LVGL=ON`, link
  the `lvgl` target), `idf_component.yml` (lvgl/lvgl), `tools/make_arduino.py --lvgl`,
  `library.json` / PlatformIO, `include/mcs_capabilities.def` (`Lvgl`).
* Makefile targets: `fetch-lvgl`, `mcs-lvgl LVGL_VERSION=…`, `lvgl-test`.
  Test configs go in `tests/lvgl/v8/lv_conf.h` and `tests/lvgl/v9/lv_conf.h`.
* Host CLI: `--lvgl[=WxH]` in-memory display, screenshot dump, simulated touch, pixel query.

### 5. Tests / CI
* `tests/lvgl/*.cs` + `.out` (first line `// args:`), run on 8.3 and 9.x host builds:
  objects, styles, events, timers/anims, fs font/image loading through the VFS,
  pixels, deleted-object safety, aliases.
* Compile matrix against every tag. Update `.github/workflows/ci.yml`.

### 6. Docs / Studio
* New `docs/LVGL.md` (full guide). Update `README.md`, `docs/README.md`, DRIVERS,
  STDLIB, TESTING, MEMORY and `CHANGELOG.md`.
* Studio:
  * templates in `tools/studio/templates/33-lvgl/`, then
    `python3 tools/studio/gen_templates.py`;
  * `api.js` / `snippets.js` / `docs.js` and the Studio README;
  * `tests/studio/test_templates.js --only=lvgl`.
* Run `make docs-check`, `make test` and `make check`.
