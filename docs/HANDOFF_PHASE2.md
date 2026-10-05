# Phase 2 handoff

## State
* Version 1.1.0. Phase 1 behaviour unchanged (all Phase 1 tests pass unmodified, image
  format unchanged — no opcodes were added).
* `make check` = 24 script runs (10 programs as source and as image + 4 diagnostics),
  79 C unit checks, 16 shell protocol checks, GC-stress run of every program, 17 feature
  flag / profile builds with `-Werror`. Extra suites: `make lfs-test` (10 checks),
  `make cm-check` (3 Cortex-M targets byte-identical to host + 7 UART protocol checks),
  `tools/verify_dotnet.sh` (3 programs byte-identical to .NET 8).

## Where things are
| Topic | Code | Doc |
|---|---|---|
| Phase 2 core API | `include/mcs.h` (appended section), `src/mcs_vm.c` (`hook_tick`, `hook_budget`) | ARCHITECTURE.md, EMBEDDING.md |
| `ref`/`out`, case patterns | `src/mcs_parser.c` (`parse_args`, `parse_params`, `parse_pattern_list`), `src/mcs_compiler.c` (`compile_args`, `emit_out_writeback`, `emit_ref_writeback`, `predeclare`, `compile_switch`) | LANGUAGE.md |
| Filesystem | `modules/fs/` | FILESYSTEM.md |
| HAL | `modules/hal/` | HAL.md |
| Scheduler | `modules/sched/` | SCHEDULER.md |
| Standalone shell | `modules/shell/`, `tools/mcs_remote.py` | STANDALONE.md |
| Cortex-M | `ports/cortex-m/`, `tools/cm_emu.py`, `tools/cm_check.sh` | PORTING.md, PERFORMANCE.md |

## Rules (in addition to HANDOFF_PHASE1.md)
* Modules use only public headers and must build with every `MCS_ENABLE_*` combination
  in `FLAG_SETS` (Makefile).
* New module state that holds script values must be reachable from the VM (pins or
  pinned containers), never raw C globals.
* Language features: add a test whose output is verified with `tools/verify_dotnet.sh`.
* Every number in docs must come from a command listed in PERFORMANCE.md.

## Known limitations / next steps (priority order)
1. Real hardware bring-up (SF32LB525 + RT-Thread): board HAL table, DFS or LittleFS on
   flash, UART transport — PORTING.md has the outline.
2. RAM: move builtin class metadata to ROM tables (~40 KB/VM), stream compilation per
   declaration (compile working set), share inherited method tables.
3. Streams (`FileStream`), async peripheral events, CAN/I2S/RTC/display bindings, LVGL module.
4. Image signing + shell authentication; cron schedules.
5. Debugger (breakpoints over the shell protocol), fuzzing of the image loader.
