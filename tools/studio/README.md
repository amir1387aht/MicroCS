# MicroCS Studio

A browser IDE for boards running MicroCS: file manager, C# editor and REPL over USB
(Web Serial). Visual Studio 2022 Dark look.

## Run it

* **Online:** https://amir1387aht.github.io/MicroCS/
* **Offline:** open `index.html` in **Chrome or Edge** on a desktop (double-click works, no server
  or install). Keep the files together.

Click **Connect**, pick the board's serial port, done. Firefox and Safari have no Web Serial:
the editor works there, but it can't connect to a board.

## Files

| File | What it holds |
|---|---|
| `index.html` | page layout |
| `studio.css` | look: Visual Studio 2022 Dark / Light, C# colours (`--t-*` variables) |
| `studio.js` | the app: serial link and shell protocol, editor, file manager, console |
| `templates.js` | templates and examples (`T(category, name, file, description, code)`) and completion snippets |
| `api.js` | class and member names known to the runtime (completion fallback) |
| `docs.js` | signatures and one-line docs of every library member (completion details, parameter info, hover) |
| `lang.js` | C# language service: symbols of the open file, type inference, call context |

No build step: edit a file and reload the page.

## Editor keys

| Keys | Action |
|---|---|
| Ctrl+Space · Ctrl+Shift+Space | completion (with docs) · parameter info (↑/↓ switch overloads) |
| Ctrl+F · Ctrl+H · F3 / Shift+F3 | find · replace · next / previous match |
| Ctrl+G · Ctrl+] | go to line · jump to the matching bracket |
| Shift+Alt+F | format document |
| Alt+↑/↓ · Shift+Alt+↑/↓ · Ctrl+D · Ctrl+Shift+K | move line · copy line · duplicate · delete line |
| Ctrl+/ · Tab / Shift+Tab | toggle comment · indent / outdent |
| F5 / Ctrl+Enter · Ctrl+Shift+Enter · Ctrl+S | save and run · run the selection · save |

## Document an API

`docs.js` holds one line per member, e.g.

```
@GPIO | Digital pins (modules/hal)
Write(pin pin, bool value): void | Drives the pin: true / 1 = high, false / 0 = low
.Value: bool | (a leading "." = instance member)      const Rising: int | constant
```

Repeat a line for each overload; `new(...)` lines are constructors.

## Connection notes

Studio leaves DTR/RTS as the browser opens the port (both asserted), which does **not** reset
ESP32 boards with the usual auto-reset circuit or the native USB-Serial-JTAG. *Reset board*
pulses EN like esptool. A board that sits in the ROM download mode is reset once
automatically. If a board needs the lines low, enable ⋯ → *Release DTR/RTS on connect*.

## Add a template

Append to `templates.js`:

```js
T("GPIO", "My blinker", "myblink.cs",
  "One line shown in the list.", String.raw`
var led = new Pin(2, GPIO.Output);
led.Toggle();
`);
```

`String.raw` keeps `\n` and other escapes as they are, so you can paste C# straight in.
`node tests/studio/test_templates.js ./mcs` compiles and runs every C# template on the
simulated board. `tests/studio/test_studio.js` tests the page in a headless browser.

Protocol and features: [docs/STANDALONE.md](../../docs/STANDALONE.md#6-in-the-browser-microcs-studio).
