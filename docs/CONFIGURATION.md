# Configuring MicroCS

Every MicroCS feature and limit is a compile-time `MCS_*` option. You can set them in a
**project config header, `mcs_user_config.h`**, with compiler `-D` options, through your
build system (CMake, menuconfig, Kconfig, Make), or pick a ready-made **profile**. All of
these can be combined.

Defaults live in [`include/mcs_config.h`](../include/mcs_config.h) and in the module
headers. Do not edit those files: put your choices in your own project instead.

## 1. The project config header

Copy the template [`config/mcs_user_config.h`](../config/mcs_user_config.h) into your
project and keep the name `mcs_user_config.h` — that enables it. It already holds **every
option set to its default value** (the full profile), with a one-line description, grouped
by area: profile and target, numbers, compiler and bytecode, standard library, C library,
VM speed, VM limits, modules, filesystem, HAL and drivers, scheduler and shell, and the
port options for ESP32, RP2040/RP2350, STM32, Zephyr and Arduino. An unmodified copy builds
exactly like no header at all; change the values you need:

```c
#ifndef MCS_ENABLE_TINYFS
#define MCS_ENABLE_TINYFS         1    /* was 0 */
#endif
```

Each value is wrapped in `#ifndef`, so `-D` options and the build system still win over it
(section 2). Values that follow another option are written as that option, e.g.
`#define MCS_ENABLE_FLASH MCS_ENABLE_FS`, and keep following it. The few options whose
default depends on the compiler, CPU or board (`MCS_COMPACT_VALUES`, `MCS_COMPUTED_GOTO`,
`MCS_STM32_HAL_HEADER`, `MCS_RP2_FS_SIZE`, …) stay commented out: uncomment them only to
force a value.

**Another profile's defaults:** the template holds the full profile's values, so a profile
picked by the build system would not change them (MicroCS warns:
*"mcs_user_config.h holds the defaults of another profile …"*). Generate the header for
the profile you want instead:

```sh
python3 tools/gen_config.py --profile lowram -o mcs_user_config.h
python3 tools/gen_config.py --profile auto --ram-kb 20 --flash-kb 128 -o mcs_user_config.h
python3 tools/gen_config.py --profile min -D MCS_ENABLE_TINYFS=1 -o mcs_user_config.h
```

The generated file is the same template with that profile's values (and
`MCS_USER_CONFIG_PROFILE` / `MCS_PROFILE` set to it). A short header that sets only a few
options (like the example below) still works too: everything it leaves out comes from the
profile and the defaults.

```c
/* my_project/mcs_user_config.h */
#ifndef MCS_USER_CONFIG_H
#define MCS_USER_CONFIG_H

#define MCS_PROFILE          MCS_PROFILE_MCU   /* precompiled images, all modules */
#define MCS_FLOAT_DOUBLE     0                 /* single precision on the M4F's FPU */
#define MCS_DEFAULT_STACK    192
#define MCS_ENABLE_WS2812    0
#define MCS_STM32_HAL_HEADER "stm32g4xx_hal.h" /* port options go here too */

#endif
```

`include/mcs_config.h` includes the header before anything else, so it applies to the
core, the modules, the ports and your application alike.

**Every file that includes a MicroCS header** (the library sources and your own code) must
see the same header, otherwise they disagree about the configuration (for example the
size of `mcs_value_t`). The build integrations below take care of that; with a
hand-written build, put the folder that contains it on the include path of the whole
build.

### How the header is found

| Build | Where to put `mcs_user_config.h` | Pick another file |
|---|---|---|
| any compiler | a folder on the include path of the library and the app | `-DMCS_USER_CONFIG_FILE='"path/my_config.h"'` |
| **CMake** (`add_subdirectory`) | next to the top-level `CMakeLists.txt`, or in its `include/`, `src/` or `main/` folder (configure prints `MicroCS: config header …`) | `set(MICROCS_CONFIG path/my_config.h)` |
| **ESP-IDF** | the project folder, its `main/` or `include/` folder | `menuconfig → MicroCS → Project config header`, or `MICROCS_CONFIG` |
| **Zephyr** | the application folder, its `include/` or `src/` | `CONFIG_MICROCS_USER_CONFIG_FILE="…"` in `prj.conf`, or `-DMICROCS_CONFIG=…` |
| **pico-sdk** | as for CMake | `set(MICROCS_CONFIG …)` |
| **PlatformIO** | the project's `include/` folder | `build_flags = -DMCS_USER_CONFIG_FILE='"…"'` |
| **Arduino IDE** | the packaged library's `src/mcs_user_config.h` (written by `tools/make_arduino.py`, edit it there) | `make_arduino.py --config my_config.h` |
| **Make** (`microcs.mk`) | one of the project's include folders (`C_INCLUDES`) | `MICROCS_CONFIG := app/my_config.h` |
| CubeIDE / Keil / IAR | a folder in the project's include paths | `MCS_USER_CONFIG_FILE` in the preprocessor symbols |

Automatic discovery uses `__has_include`, available in GCC 5+, Clang, Arm Compiler 6,
IAR 8+ and MSVC 2017+. With an older compiler define `MCS_USER_CONFIG=1` (the header is
then required) or name it with `MCS_USER_CONFIG_FILE`. `MCS_USER_CONFIG=0` ignores any
`mcs_user_config.h` (CMake / Make / Zephyr: `MICROCS_CONFIG=NONE`).

## 2. Compiler options still work

Everything that worked before still works: `-DMCS_ENABLE_FLOAT=0`, CMake's
`target_compile_definitions`, PlatformIO `build_flags`, the Kconfig / menuconfig choices.
**A value from the command line wins over the header.** Set each option in one place; if
you want the header to provide a value that a build may override with `-D`, wrap it:

```c
#ifndef MCS_HEAP_RESERVE
#define MCS_HEAP_RESERVE 4096
#endif
```

([`examples/lowram/mcs_user_config.h`](../examples/lowram/mcs_user_config.h) does this, so
the Cortex-M build can reuse it with another profile.)

The older way of selecting a profile, `-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_lowram.h"'`,
also still works.

## 3. Profiles

A profile is a coherent set of defaults for a class of targets
([`include/profiles/`](../include/profiles/), measured in [Small MCUs](LOW_RESOURCE.md)):

| `MCS_PROFILE` | For | Name in CMake / Make / Kconfig |
|---|---|---|
| `MCS_PROFILE_FULL` | everything on (the default) | `full` |
| `MCS_PROFILE_AUTO` | picks one of the others from the target's RAM / flash (`MCS_TARGET_RAM_KB`, `MCS_TARGET_FLASH_KB`, Zephyr `CONFIG_SRAM_SIZE`, STM32 / RP2 / nRF52 / SAMD device macros) | `auto` |
| `MCS_PROFILE_EMBEDDED` | 96–256 KB RAM: on-device compiler, REPL, all modules | `embedded` |
| `MCS_PROFILE_MCU` | 64–96 KB RAM: precompiled images, all modules | `mcu` |
| `MCS_PROFILE_LOWRAM` | 32–64 KB RAM: images only, single-precision floats, small VM limits | `lowram` |
| `MCS_PROFILE_TINY` | < 256 KB flash: no float, Dictionary or modules | `tiny` |
| `MCS_PROFILE_MIN` | 64 KB flash / 16 KB RAM firmware | `min` |
| `MCS_PROFILE_LINUX` | embedded Linux / desktop: 64-bit ints, big stacks | `linux` |

Select one with `#define MCS_PROFILE MCS_PROFILE_LOWRAM` in `mcs_user_config.h`, with
`-DMCS_PROFILE=MCS_PROFILE_LOWRAM`, or through the build system: CMake
`set(MICROCS_PROFILE lowram)`, `microcs.mk` `MICROCS_PROFILE := lowram`, menuconfig /
Kconfig `MICROCS_PROFILE_*`, `make_arduino.py --profile lowram`. The build system's choice
arrives as `MCS_DEFAULT_PROFILE`, so a profile in the header (or `-DMCS_PROFILE`) wins over
it. Options set in the header or with `-D` win over the profile's values.

## 4. Precedence

From strongest to weakest — the first place that defines an option wins:

1. the compiler command line (`-D…`, CMake / Kconfig / menuconfig / PlatformIO flags);
2. the project config header `mcs_user_config.h` (or `MCS_USER_CONFIG_FILE`);
3. the profile (`MCS_PROFILE`, else the build system's `MCS_DEFAULT_PROFILE`, else full);
4. the defaults in `include/mcs_config.h` and the module headers.

Some values are also run-time settings: `mcs_config_t` (heap, stack slots, frames, step
budget, `cfg.stdlib`) is filled from these defaults by `mcs_config_default()` and can be
changed per VM — see [Embedding](EMBEDDING.md#1-create-a-vm).

## 5. Checking the result

- `make test` builds the library with `tests/c/config/mcs_user_config.h` found
  automatically, named with `MCS_USER_CONFIG_FILE`, forced with `MCS_USER_CONFIG=1` and
  skipped with `MCS_USER_CONFIG=0`, and checks the precedence in
  [`tests/c/test_config.c`](../tests/c/test_config.c); CI repeats it through CMake
  (`cmake -S tests/c/config -B build/cfg`).
- `make test` runs `tools/gen_config.py --check`: the template lists every option, and its
  values equal the defaults of `include/mcs_config.h`; it also generates the header for
  every profile and checks the profile-mismatch warning.
- `make check` also builds with the unmodified template (`-Iconfig`) and with every
  generated profile header, `-Werror`.
- In your own build, a `#if`/`#error` against the option in any source file shows what the
  compiler sees, e.g. `#if MCS_ENABLE_COMPILER` … `#error "compiler on"`.
