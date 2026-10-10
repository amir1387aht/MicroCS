# Small-MCU firmware example

A complete firmware skeleton for small parts — down to **16 KB of RAM and 64 KB of flash**:
the MicroCS library is built with the [lowram profile](../../include/profiles/mcs_profile_lowram.h)
(or the [min profile](../../include/profiles/mcs_profile_min.h) for 64 KB of flash), selected
by the project config header [`mcs_user_config.h`](mcs_user_config.h) in this folder, the
script is precompiled on the host and executed in place from flash, and the VM gets a
single static pool (32 KB on a 48 KB part, 12 KB on a 16 KB part). The C side uses no
`printf` (`NODE_PUTS` = your console write), so it links without libc's formatter.

| File | |
|---|---|
| [`node.cs`](node.cs) | the script: ring buffer, state machine, custom exception, `switch` expression |
| [`node_image.h`](node_image.h) | `node.cs` precompiled: `./mcs -C node.cs -n node_image -o node_image.h` |
| [`mcs_user_config.h`](mcs_user_config.h) | the build configuration: lowram profile, no filesystem / HAL / scheduler (found because this folder is on the include path; `-DMCS_PROFILE=MCS_PROFILE_MIN` overrides it for `m0-64k`) |
| [`lowram_firmware.c`](lowram_firmware.c) | the C side: pool heap, reduced stdlib, `mcs_exec_image_xip`, step budget, heap stats, optional C-stack high-water mark (`NODE_STACK_HIGH_WATER`) |
| [`node.expected`](node.expected) | expected output, checked by `make test` (host) and `make cm-check` (Cortex-M0) |

```sh
make example-lowram          # host build (64-bit: uses a 72 KB pool, see the source)
make cm-check                # also builds it for Cortex-M0 (m0-node: 48 KB, m0-16k: 16 KB RAM,
                             # m0-64k: min profile in 64 KB flash / 16 KB RAM) and runs it in the emulator
```

Measured on the emulated Cortex-M0 (arm-none-eabi-gcc 13.2.1 `-Os`, not real silicon), 24
`Node.Tick()` calls:

| Target | Flash | RAM | Pool | Heap after `mcs_new` | Pool peak | Collections | C stack peak |
|---|---:|---:|---:|---:|---:|---:|---:|
| `m0-node` (lowram) | 157.2 KB | 48 KB | 32 KB | 1.8 KB | 13.8 KB | 2 | 1.4 KB |
| `m0-16k` (lowram) | 157.2 KB | 16 KB | 12 KB | 1.8 KB | 11.0 KB | 5 | 1.4 KB |
| `m0-64k` (min) | 60.6 KB | 16 KB | 12 KB | 1.8 KB | 10.4 KB | 4 | 1.3 KB |

Flash includes newlib-nano (and, except for `m0-64k`, float `printf` and libm). The tuning steps behind these numbers are
explained in [docs/LOW_RESOURCE.md](../../docs/LOW_RESOURCE.md).
