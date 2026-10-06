# Examples

| File | What it shows | Run on the host |
|---|---|---|
| [`tour.cs`](tour.cs) | a quick tour of the language: classes, tuples, `out`, LINQ, ranges, patterns, closures, exceptions — output identical to .NET 8 | `./mcs examples/tour.cs` |
| [`blink.cs`](blink.cs) | GPIO output + debounced button input with the scheduler | `./mcs --sim --run-for 3000 examples/blink.cs` |
| [`sensor_logger.cs`](sensor_logger.cs) | I²C temperature sensor, switch-expression classification, CSV logging to the filesystem, statistics | `./mcs --sim --ramfs 32768 --run-for 5500 examples/sensor_logger.cs` |
| [`app.cs`](app.cs) | the script used by the C embedding example | `./mcs examples/app.cs` |
| [`firmware_example.c`](firmware_example.c) | embedding: pool heap, limits, HAL table, C bindings, calling script functions from C, running a flash image | `make example` |
| [`app_image.h`](app_image.h) | `app.cs` precompiled with `./mcs -C` | — |

`--sim` attaches the simulator board (its I²C sensor always reads 25 °C); add `--sim-log` to
trace every peripheral access. On a device the same scripts run against your board's
`mcs_hal_t` table.
