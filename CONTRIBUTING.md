# Contributing to MicroCS

Thanks for helping! MicroCS runs on devices where a crash means a bricked field unit, so
the bar is "tested and honest" rather than "big".

## Ground rules
1. **`make check` stays green** — tests, GC-stress run and all 17 feature-flag builds with `-Werror`.
2. **Behaviour is defined by `.out` files.** A new language feature needs a test; if it is
   plain C#, its output must be byte-identical to .NET (`tools/verify_dotnet.sh`).
3. **Docs describe the code, not the plan.** Label anything unfinished as 🧪 experimental or
   🗓️ planned. Every number must come from a command in `docs/PERFORMANCE.md`.
4. **C99, no dependencies, no global state** in the core (state lives in the VM; modules use
   extension slots).
5. **Public API is append-only** (`include/mcs.h`); **opcodes are append-only** and a format
   change bumps `IMG_VERSION`.

## Workflow
```sh
make check                               # must pass
make asan-test                           # sanitizers (then `make clean && make`)
python3 tools/fuzz.py ./mcs 1000 $RANDOM # for parser/compiler/VM changes
tools/verify_dotnet.sh                   # for language/library changes (needs dotnet 8)
make cm-check                            # for anything that could affect 32-bit targets
python3 tools/gen_stdlib_doc.py > docs/STDLIB.md   # after adding library members
```

## Where things go
| Change | Code | Docs / tests |
|---|---|---|
| New syntax | `src/mcs_lexer.c`, `src/mcs_parser.c` (desugar if possible), `src/mcs_compiler.c` | `docs/LANGUAGE.md`, `tests/t*.cs` |
| New library member | `src/mcs_lib*.c` registration tables | regenerate `docs/STDLIB.md`, add to a test |
| New opcode | append to `MCS_OPCODES` in `src/mcs_internal.h`, VM case, disassembler, loader validation | `docs/BYTECODE.md` |
| New module / peripheral | `modules/<name>/`, public headers only, `MCS_ENABLE_<NAME>` flag | `docs/<NAME>.md`, `FLAG_SETS` in the Makefile |
| New port | `ports/<target>/` | `docs/PORTING.md` |

## Style
4-space indent, K&R braces, `snake_case`, `mcs_` prefix for anything public, comments that
explain *why*. Keep hot paths in `src/mcs_vm.c` allocation-free and remember that the GC only
runs at safepoints — new roots must be reachable from `mark_roots`.
