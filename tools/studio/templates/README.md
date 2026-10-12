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

[boot.cs] boot.cs - first script at power-up
Runs first at every boot: hardware setup and a banner.
Save as /boot.cs.

[main_safe_mode.cs] main.cs with safe mode
save as: main.cs
Hold a button at boot to skip the application.
```

* `category:` — the name in Studio's category list (once, before the first block).
* `[file] Display name` — a file of this folder; blocks are listed in this order.
* `save as:` (optional, right after the header) — the file name Studio suggests when several
  templates of a folder are meant to become the same file (`main.cs`, `boot.cs`).
* The lines after it are the description (joined into one paragraph).

Every file of the folder must be described and every described file must exist.

## Add or change a template

1. Write the script into the right folder (or a new `NN-name/` folder with its own
   `templates.txt`). Keep it runnable: CI compiles and runs every C# template on the
   simulated board for 2.5 s, so long loops should end by themselves or check
   `Environment.TickCount` (see the web server templates).
2. `python3 tools/studio/gen_templates.py` — rebuilds `tools/studio/templates.js`, the bundle
   the page loads (Studio is a static page that also runs from `file://`, where it cannot read
   folders). `--check` verifies it is current, `--list` prints everything.
3. `node tests/studio/test_templates.js ./mcs` — runs them all (`make mcs` first).

Studio derives the tags it shows (*Wi-Fi*, *OLED*, *files*, *jobs*, *DMA* …) from the code and
links each preview to its file here.
