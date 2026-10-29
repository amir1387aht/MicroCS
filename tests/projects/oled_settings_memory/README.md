# OLED/settings app memory investigation

## Scope and reproduction

The fixture in this directory preserves the executable code supplied in chat:
settings load/save, all menus, file browsing, U8g2, rotary encoder/timer, reset and
sleep routines. Whitespace is normalized and the commented-out BLE startup block
is omitted. This is **not a byte-for-byte copy** of the uploaded device file.
The fixture is 9,982 UTF-8 bytes; comments/formatting affect the
whole-file source buffer and therefore source-compilation peak RAM.

Run from the repository root on Linux:

```sh
make fetch-u8g2
make oled-settings-memory-test CC="gcc -m32 -malign-double" \
  U8G2_DISPLAYS=ssd1306_i2c_96x16_er U8G2_FONTS=ncenB08_te
```

Needs gcc-multilib/libc development files, Python, binutils/addr2line. The target
writes `build/oled-memory-profile/evidence.json`, per-run logs, physical allocation
traces, per-function code/constant/line capacities and object identities. All
trace storage and backtracing are **host-only, outside the VM pool**; none are
compiled into normal firmware. Every snapshot reconciles traced physical block
sizes (headers/alignment included) to the actual pool occupied counter.
The allocator's own high-water counter remains authoritative: during realloc it
may briefly own both old and replacement blocks before the outer trace wrapper
returns. The JSON records that unobserved overlap separately (48 bytes in two
large-pool workload cases here); it is not assigned a fabricated call-stack
breakdown. The reported compile peak reconciles exactly.

The measured ABI is 32-bit pointers, 12-byte VM values and 8-byte numeric/pool
alignment. Embedded profile; BLE compiled with one connection but not started;
Wi-Fi disabled; threads enabled with **POSIX instead of FreeRTOS**. Real U8g2
2.37.1 setup/font code executes through a simulated HAL. This is not a board/SDK
RAM measurement. The 96x16 simulator validates transmitted OLED data bytes, not
rendered frame geometry. One small text file and a one-item saved-settings array
are used. Reset/light-sleep paths are compiled/retained, not physically exercised.

A compact, address-independent record of this run is in
[`profile-results.json`](profile-results.json); the full traces are generated locally.

## Findings from the 32-bit host test

| Measurement | Physically occupied pool bytes |
| --- | ---: |
| Runtime baseline | 9,112 |
| Source compilation peak (normalized fixture) | 58,264 |
| After source startup and explicit GC | 40,296 |
| Source, warmed menus/settings after explicit GC | 43,112 |
| Debug bytecode, warmed menus/settings after explicit GC | 42,696 |
| Debug bytecode, startup + 1,000-iteration workload peak, partial startup pool | 50,264 |
| Stripped bytecode, same workload peak | 49,016 |

The workload redraws screens, lists the directory, serializes/writes settings and
polls the live rotary/timer binding. **No forced GC inside its 1,000 iterations.**
Warm and final retained usage match exactly. Explicit GC is used only at named
measurement boundaries, not to conceal workload pressure. A larger pool permits
normal GC to wait longer, so a larger total workload peak does not imply a leak.

The source-only compilation peak splits into:

| Allocation group at compile peak | Physical bytes |
| --- | ---: |
| Compiler temporary workspace (AST, tokens, compiler scratch) | 23,216 |
| Whole-file source buffer | 9,992 |
| All other live allocations, including VM and emitted objects | 25,056 |
| Total | 58,264 |

A controlled source-padding test adds an otherwise irrelevant 8,198-byte comment
and separators. Compilation peak rises from 58,264 to
66,464 bytes (allocator-rounded increase
8,200), while post-GC retained usage is
unchanged. These are real whole-file read-buffer bytes, not extra app state.
Even declarations without creating/starting the app peak at
57,952 bytes: compilation, not OLED pixels, dominates startup.

After source startup + GC, major retained allocations are:

| Category | Physical bytes |
| --- | ---: |
| Names and literal strings | 7,824 |
| Classes and member tables | 6,416 |
| VM core, stack and symbol indices | 4,408 |
| Bytecode instructions | 3,488 |
| Function constants | 3,096 |
| Functions and closures | 3,144 |
| Debug line tables | 2,416 |
| Field/method caches | 1,048 |
| Native userdata: U8g2 | 624 |
| App instances | 416 |
| Native userdata: RotaryEncoder | 80 |

This is a selected breakdown, **not additive on top of** the total; the JSON
contains every category. Names include app identifiers, literals and materialized
library symbols. Class/member tables include both app and library classes.
The U8g2 userdata row is its wrapper; its separate 96x16 monochrome pixel payload
is only 192 bytes (plus that allocation's header/alignment). The compiled built-in
font is const data, not a copied font bitmap in this VM pool.

## Reproduced bug and fix

Before the fix, a 158,720-byte requested backing ceiling with a roughly 57,000-byte
actual sealed acquisition kept an unreachable main-VM hard limit. GC derives its
next threshold from that limit and live usage. Repeated settings saves could
exhaust the actual pool **before** GC was requested. A separate minimal 48 KiB
provider test reproduced `out of memory (allocation of 384 bytes failed)` without
OLED or JSON; it retains a 24 KiB string and churns short-lived lists.

`rt_new_vm` now derives the main-VM allocation/GC limit from the **smaller of the
requested ceiling and actual sealed pool capacity**, retaining the existing
RAMFS/2 KiB slack policy. The configured backing ceiling, ownership, provider
reserves and acquisition policy are unchanged. On-demand pools retain their
existing growth behavior; this fix does not diagnose all native-pressure cases
for on-demand pools, pool sharing or a single oversized contiguous allocation.

The negative-control debug-bytecode app exhausted its partial startup pool during
repeated saves. With the fix it completes the workload, preserves the same live
state and releases all pool allocations on teardown. The GC regression also
checks application replacement and backing release. More frequent collection in
a tight pool has CPU cost; the patch adds no tracing or per-allocation callback
to production firmware.

## Practical implications

- Do not treat a 65 KiB high-water mark as 65 KiB of retained user variables.
  Code, names/type metadata, compiler workspace, source text and transient garbage
  are distinct contributors.
- Debug bytecode keeps source line diagnostics and passes this host workload in
  the partial startup pool. Stripping is optional and loses line information;
  it is not required for this workload to pass.
- Source still fails in the roughly 57 KiB owned pool: this GC fix cannot reclaim
  a live source buffer or compiler state while compilation is paused against GC.
- The app saves on **every encoder step** inside `OnRotaryEncoderRotate`, despite
  the comment about saving only when leaving edit mode. This causes transient
  list/dictionary/JSON allocations and filesystem writes. The test deliberately
  keeps that behavior. Saving only on edit commit would reduce churn/write wear,
  but is an app-behavior choice, not silently applied here.
- Directory enumeration retains filenames and temporarily builds directory/path
  results. More files, larger settings/text and active BLE objects require more
  space than this fixture. Actual BLE controller RAM still needs hardware tests.
