# MicroCS Studio templates

The *Templates* dialog of [MicroCS Studio](../README.md) (`Alt+T`) lists the files in these
folders. One folder per category, numbered in the order Studio shows them:

```
templates/
  01-getting-started/
    templates.txt        describes every file of the folder
    hello.cs
    loop.cs
  02-boot-and-startup/
    templates.txt
    boot.cs
    main_safe_mode.cs    "save as: main.cs" in templates.txt
    jobs.cfg             not only C#: any text file works
  ...
```

## templates.txt

```
# comment lines start with #
category: Boot and startup
requires: none

[boot.cs] boot.cs - first script at power-up
requires: Board
Runs first at every boot: hardware setup and a banner.
Save as /boot.cs.

[main_safe_mode.cs] main.cs with safe mode
requires: GPIO, Filesystem
save as: main.cs
Hold a button at boot to skip the application.
```

* `category:` — the name in Studio's category list (once, before the first block).
* `requires:` — mandatory folder default, or per-file override before the description.
  Exact, case-sensitive names from `include/mcs_capabilities.def`, comma-separated; `none`
  means no extra requirements. `.cs` files also need `Compiler`. Use operation flags such as
  `Wifi.AP`, `Ble.Central`, `GPIO.IRQ` or `SPI.Configure` when the example uses them.
* `[file] Display name` — a file of this folder; blocks are listed in this order.
* `save as:` (optional, right after the header) — the file name Studio suggests when several
  templates of a folder are meant to become the same file (`main.cs`, `boot.cs`).
* `min firmware:` (optional, before the description) — an explicitly known minimum
  `major.minor.patch`, for example `1.17.0` for the rotary encoder API. Do not guess
  a version from a platform name.
* The lines after it are the description (joined into one paragraph).

Every file of the folder must be described and every described file must exist.

## Add or change a template

1. Write the script into the right folder (or a new `NN-name/` folder with its own
   `templates.txt`). Keep it runnable: CI compiles and runs every C# template on the
   simulated board with a time budget; persistent services should remain alive until Stop,
   yield with `Thread.Sleep` / native Poll, and clean up with `using` / `finally`.
   Radio examples must check `Hal.Has` before using optional classes, so a copied script
   also exits with a useful message on a board without the required radio or role.
2. `python3 tools/studio/gen_templates.py` — rebuilds `tools/studio/templates.js`, the bundle
   the page loads (Studio is a static page that also runs from `file://`, where it cannot read
   folders). `--check` verifies it is current, `--list` prints everything.
3. `node tests/studio/test_templates.js ./mcs` — runs them all (`make mcs` first).

Studio derives the tags it shows (*Wi-Fi*, *OLED*, *files*, *jobs*, *DMA* …) from the code and
links each preview to its file here.

## Connected-board compatibility

Studio sends the allocation-free shell command `caps` after connecting. The
**Supported only** toggle is **on by default** and remembered locally. When connected,
it shows only examples with confirmed exact capabilities and minimum firmware;
missing features (for example Wi-Fi) and unknown legacy requirements are hidden.
Turn it off to show every example with supported, unknown or unsupported badges.
Offline, every example remains visible and is marked not checked.

The preview lists missing/unreported requirements and any declared minimum firmware.
Older firmware without `caps` can still use the gallery: turn off Supported only to
browse/adapt unverified examples. Only explicitly reported feature bits are used;
a HAL/network build bit does not prove a particular peripheral. Reconnecting refreshes
support and clears cached compile-check availability. Opening the gallery does not
start new capability polling or runtime work. For FileStream examples, declare the
exact `Filesystem.Streams` capability, not only `Filesystem`.

Templates can always be copied or opened for adaptation; adding one does not run it.
Studio does not probe buses or start radios to discover support. A supported bus does
**not** mean the external sensor/display/actuator is attached. Pins, wiring, credentials
and API details still need checking. Tags are search labels, not support guarantees.
