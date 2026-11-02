# LVGL support: work-in-progress handoff

Status: **WIP.** The binding compiles and runs against LVGL 8.3.11 and 9.2.2 (session 3).
Tests, remaining callbacks, build files, docs and Studio are still to do (see "Next steps").

Goal, design and generator/runtime details: sections "Goal" .. "Docs / Studio" of the previous version of this file: `git show 0b5a304:docs/LVGL_HANDOFF.md` (still valid; its status/progress parts are replaced by this file).

## Constraint: generated files are NOT committed
`modules/lvgl/mcs_lvgl_api.inc` (2 MB for all tags, ~0.5 MB for one) and `lvgl_api.json` (0.7 MB) are far too big for
the `push_files` tool, so they are `.gitignore`d and always generated:
* `python3 tools/lvgl_gen/build_api.py --lvgl <LVGL dir> --out <file>`: tables for exactly that LVGL (what `make mcs-lvgl` does;
  `-DMCS_LVGL_API_INC="<file>"` tells mcs_lvgl.c where it is). Firmware builds can do the same.
* `python3 tools/lvgl_gen/build_api.py --all` (or `make lvgl-api`): every release v8.0.0..v9.6.0 (git clone --depth 1 each tag)
  -> `modules/lvgl/mcs_lvgl_api.inc`, one file that works with any 8.x/9.x. CI should build it and publish it as release artifact.
* Studio IntelliSense data must be generated at Studio build time (`--api-json`), not committed.
* Pushing big files costs many tokens (file content must be printed + sent). Keep new code in NEW small files.

## Set-up checklist (new session)
1. `git clone https://github.com/amir1387aht/MicroCS; git checkout feature/lvgl-support; pip install pycparser`
2. LVGL trees for tests: `git clone --filter=blob:none https://github.com/lvgl/lvgl /data/lv/repo`, then for each tag
   `git -C /data/lv/repo worktree add -f ../<tag> <tag>` (need at least v8.3.11 and v9.2.2).
3. `make -j2 mcs-lvgl LVGL_DIR=/data/lv/v8.3.11` / `v9.2.2` -> `build/mcs_lvgl_v8`, `build/mcs_lvgl_v9` (and `build/mcs_lvgl`, the last one built).
   (The Makefile needs the line `include tools/lvgl_gen/lvgl.mk`; its push is pending, see Progress log.)
4. Smoke: `echo 'var s = lv_obj_create(null); lv_scr_load(s); Console.WriteLine(lv_obj_get_width(s));' > a.cs; build/mcs_lvgl_v8 --lvgl a.cs`

## Progress log
### Session 2 (pushed)
Core VM ROM global tables, extractor fixes, generator, runtime module, host display, config (see git log).
### Session 3
* DONE step 1: `make mcs-lvgl LVGL_DIR=...` (rules in `tools/lvgl_gen/lvgl.mk`, included from the Makefile; `fetch-lvgl`,
  `lvgl-api` too). Builds LVGL `src/**/*.c` with `tests/lvgl/v8|v9/lv_conf.h` into `build/lvgl-<api>-<cksum>/`, generates a
  single-version `.inc`, links `build/mcs_lvgl_v8|v9`. v8.3.11 and v9.2.2 compile with `-Wall -Wextra -Werror` for MicroCS and
  run the smoke script (hello label, lv_scr_load on v9 via the v8 alias, lv_screen_load on v8 via reverse alias with --all tables).
* Generator fixes: guard_expr compared tag objects with strings (struct ctor/field guards were missing); struct feature guards;
  `struct _lv_obj_t` == `lv_obj_t` canonical class; skip `*_INCLUDE` string macros; const by-value struct params;
  unused-parameter prologue; reverse aliases (v9 names on v8, e.g. lv_screen_load -> lv_scr_load); v8 names on v9 alias entries
  no longer skipped when the old function exists natively on another version.
* mcs_lvgl.c: dropped lv_color_t class check, forward decl of lvb_ret_created, parentheses, unused attrs, `MCS_LVGL_API_INC`.
* extract.py: running several tags in parallel in one work dir races (shared fake/ dir): `build_api.py` uses one work dir per tag.
* PENDING PUSH (local only in /data/MicroCS, backed up by the sandbox): Makefile include line, modules/lvgl/mcs_lvgl.c edits.

## Next steps (in order)
2. tests/lvgl/*.cs + .out (objects, styles, events, timers/anims, fonts+images from files, pixels, deleted objects, old v8 names),
   `lvgl-test` target (in lvgl.mk), CI job (.github/workflows/ci.yml: matrix 8.3.11, 9.2.2 + all-tags compile).
3. Callbacks listed in gen.py OVERRIDE that do not exist yet (observers, chart ext arrays, calendar dates, canvas buffer,
   grid dsc arrays, style transitions, tree walk, lv_msg, log cb, dropdown/roller get_selected_str); `mcs_lvgl_close_lib` before mcs_free in ports/unix/main.c.
4. Build files: CMake (`cmake/MicroCSLvgl.cmake`), Kconfig, Zephyr, ESP-IDF (idf_component.yml), Arduino (tools/make_arduino.py --lvgl), PlatformIO (library.json), mcs_capabilities.def.
5. docs/LVGL.md, README, other docs, CHANGELOG, Studio template 33-lvgl + IntelliSense from `--api-json`.
Note: an older, different-design attempt exists on branch `feat/host-owned-lvgl` (useful for tests/build files only).
