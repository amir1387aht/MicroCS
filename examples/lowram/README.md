# Small-MCU firmware example

A complete firmware skeleton for a part with **~48 KB of RAM**: the MicroCS library is
built with [`profiles/mcs_profile_lowram.h`](../../include/profiles/mcs_profile_lowram.h),
the script is precompiled on the host and executed in place from flash, and the VM gets a
single static 32 KB pool.

| File | |
|---|---|
| [`node.cs`](node.cs) | the script: ring buffer, state machine, custom exception, `switch` expression |
| [`node_image.h`](node_image.h) | `node.cs` precompiled: `./mcs -C node.cs -n node_image -o node_image.h` |
| [`lowram_firmware.c`](lowram_firmware.c) | the C side: pool heap, reduced stdlib, `mcs_exec_image_xip`, step budget, heap stats |
| [`node.expected`](node.expected) | expected output, checked by `make test` (host) and `make cm-check` (Cortex-M0) |

```sh
make example-lowram          # host build (64-bit: uses a 72 KB pool, see the source)
make cm-check                # also builds it for Cortex-M0 (target m0-node) and runs it in the emulator
```

Measured on the emulated Cortex-M0 (`m0-node`, arm-none-eabi-gcc 13.2.1 `-Os`, not real
silicon): 144 KB of flash including newlib-nano, 13 KB of heap after `mcs_new`, 27.4 KB pool
peak for 24 `Node.Tick()` calls (3 collections). The tuning steps behind these numbers are
explained in [docs/LOW_RESOURCE.md](../../docs/LOW_RESOURCE.md).
