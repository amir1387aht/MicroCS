# Phase 2 handoff

## State
* Phase 2 shipped as 1.1.0 without new opcodes. **1.2.0** (see [CHANGELOG](../CHANGELOG.md))
  added image format v2 (superinstructions; v1 images still load), tuples, ranges, more
  patterns and library members, loader hardening and a fuzzer. All Phase 1/2 tests pass
  unmodified.
* `make check` = 30 script runs (13 programs as source and as image + 4 diagnostics
  tests), 79 C unit checks, 16 shell protocol checks, examples, GC-stress
  run of every program, 17 feature flag / profile builds with `-Werror`. Extra suites:
  `make lfs-test`, `make cm-check` (3 Cortex-M targets byte-identical to host + 7 UART
  protocol checks), `tools/verify_dotnet.sh` (6 programs byte-identical to .NET 8),
  `tools/fuzz.py` (source and image modes). Details: [TESTING.md](TESTING.md).

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
| 1.2 tuples / ranges / `is` patterns | `src/mcs_parser.c` (`tuple_relabel`, `prescan_tuple_members`, `is_as_switch`, deconstruction desugaring), `src/mcs_lib.c` (`__rt` module, `tuple_class`) | LANGUAGE.md |
| 1.2 superinstructions, field cache | `src/mcs_compiler.c` (`store_pop`, `emit_cond_jump`), `src/mcs_vm.c` | BYTECODE.md, ARCHITECTURE.md |
| Image validation, fuzzing | `src/mcs_bytecode.c` (`validate_code`), `tools/fuzz.py` | BYTECODE.md, TESTING.md |

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
5. Debugger (breakpoints over the shell protocol); a stack-balance verifier for images.
