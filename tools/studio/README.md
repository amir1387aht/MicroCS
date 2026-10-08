# MicroCS Studio

A browser IDE for boards running MicroCS: file manager, C# editor and REPL over USB
(Web Serial). Visual Studio 2022 Dark look.

## Run it

* **Online:** https://amir1387aht.github.io/MicroCS/
* **Offline:** open `index.html` in **Chrome or Edge** on a desktop (double-click works, no server
  or install). Keep the five files together.

Click **Connect**, pick the board's serial port, done. Firefox and Safari have no Web Serial:
the editor works there, but it can't connect to a board.

## Files

| File | What it holds |
|---|---|
| `index.html` | page layout |
| `studio.css` | look: Visual Studio 2022 Dark / Light, C# colours (`--t-*` variables) |
| `studio.js` | the app: serial link and shell protocol, editor, file manager, console |
| `templates.js` | templates and examples (`T(category, name, file, description, code)`) and completion snippets |
| `api.js` | classes and members offered by completion |

No build step: edit a file and reload the page.

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
