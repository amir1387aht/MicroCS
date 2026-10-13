// Browser test for MicroCS Studio (tools/studio/index.html): a fake Web Serial port is wired to
// `mcs --repl --echo --sim --fs <dir>` (the device shell on the host).
//   npm i playwright-core   (or playwright)
//   CHROME=/usr/bin/google-chrome node tests/studio/test_studio.js [./mcs]
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  ({ chromium } = require("playwright-core"));
}
const { spawn } = require("child_process");
const fs = require("fs"),
  path = require("path");
const OUT = path.resolve(process.env.OUT || "build/studio-project-browser");
const MCS = path.resolve(process.argv[2] || "./mcs");
const HTML = "file://" + path.resolve(__dirname, "../../tools/studio/index.html");
const ROOT = OUT + "/fs";
fs.mkdirSync(OUT, { recursive: true });
fs.rmSync(ROOT, { recursive: true, force: true });
fs.mkdirSync(ROOT + "/lib", { recursive: true });
fs.writeFileSync(ROOT + "/hello.cs", 'Console.WriteLine("hi from file");\n');
fs.writeFileSync(ROOT + "/lib/util.cs", "// util\n");
fs.writeFileSync(ROOT + "/data.bin", Buffer.from([0, 1, 2, 3, 255, 4]));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const bounded = async (task, ms = 15000) => {
  let timer;
  try {
    return await Promise.race([
      task,
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error("Project timeout")), ms);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
};
let hostProcess, activeBrowser;
let fails = 0;
const check = (c, w) => {
  console.log((c ? "PASS " : "FAIL ") + w);
  if (!c) fails++;
};
(async () => {
  const proc = spawn(
    MCS,
    [
      process.env.MODE === "shell" ? "--shell" : "--repl",
      ...(process.env.ECHO === "0" ? [] : ["--echo"]),
      "--sim",
      "--fs",
      ROOT,
    ],
    { stdio: ["pipe", "pipe", "inherit"] },
  );
  hostProcess = proc;
  const browser = await chromium.launch({
    executablePath: process.env.CHROME || undefined,
    args: ["--no-sandbox"],
  });
  activeBrowser = browser;
  const page = await browser.newPage({ viewport: { width: 1400, height: 860 } });
  page.on("pageerror", (e) => {
    console.log("PAGEERROR", e.message);
    fails++;
  });
  page.on("console", (m) => {
    if (m.type() === "error") console.log("CONSOLE", m.text());
  });
  await page.exposeFunction("__serialWrite", (arr) => {
    proc.stdin.write(Buffer.from(arr));
  });
  let chain = Promise.resolve();
  proc.stdout.on("data", (d) => {
    const a = [...d];
    chain = chain.then(() => page.evaluate((b) => window.__serialPush(b), a).catch(() => {}));
  });
  await page.route("https://**/*", (route) => route.abort());
  await page.addInitScript(() => {
    let ctl;
    const port = {
      readable: null,
      writable: null,
      async open() {
        // fresh streams on every open, like a real port
        if (window.__gone) throw new DOMException("Failed to open serial port.", "NetworkError");
        port.readable = new ReadableStream({
          start(c) {
            ctl = c;
          },
        });
        // window.__rom: the "board" sits in the ESP32 ROM bootloader until RTS resets it
        port.writable = new WritableStream({
          write(chunk) {
            // window.__boot: the board is booting (prints an ESP-IDF log, ignores input) until that time
            if (window.__boot && Date.now() < window.__boot) {
              if (!window.__bootSaid) {
                window.__bootSaid = 1;
                const log =
                  "ESP-ROM:esp32s3-20210327\r\nrst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\nI (27) boot: ESP-IDF v5.2 2nd stage bootloader\r\nI (114) esp_image: segment 1: paddr=00020020 vaddr=3c0a0020 size=02e8ch\r\n";
                setTimeout(() => ctl.enqueue(new TextEncoder().encode(log)), 300);
              }
              return;
            }
            if (window.__rom) {
              if (!window.__romSaid) {
                window.__romSaid = 1;
                ctl.enqueue(
                  new TextEncoder().encode(
                    "rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))\r\nwaiting for download\r\n",
                  ),
                );
              }
              return;
            }
            return window.__serialWrite(Array.from(chunk));
          },
        });
      },
      async close() {
        port.readable = port.writable = null;
      },
      async setSignals(s) {
        window.__signals = (window.__signals || []).concat([s]);
        if (s.requestToSend) window.__rom = 0;
      },
    };
    window.__serialPush = (b) => ctl.enqueue(new Uint8Array(b));
    // a framing error: Chrome errors the stream and hands out a new one on the next port.readable
    window.__serialError = () => {
      const old = ctl;
      port.readable = new ReadableStream({
        start(c) {
          ctl = c;
        },
      });
      old.error(new DOMException("Framing error", "FramingError"));
    };
    // window.__unplug(): the USB device vanishes (board reset on native USB) until __gone is cleared
    window.__unplug = () => {
      window.__gone = 1;
      ctl.close();
    };
    Object.defineProperty(navigator, "serial", {
      value: {
        requestPort: async () => port,
        getPorts: async () => (window.__gone ? [] : [port]),
        addEventListener() {},
      },
    });
  });
  await page.goto(HTML);
  await page.click("#btnConnect");
  await page.waitForFunction(
    () => document.getElementById("connText").textContent.startsWith("MicroCS"),
    null,
    { timeout: 8000 },
  );
  check(
    await page.evaluate(() => App.capabilities.get("Compiler") === true),
    "real host reports compiler",
  );
  await page.evaluate(() => {
    App.tabs = [];
    App.active = null;
    ed.tab = null;
    window.lib = App.openTab({
      name: "library.cs",
      text: "// library\r\npublic class Meter { public int Read() { return 42; } }\r\n",
      saved: null,
    });
    window.entry = App.openTab({
      name: "main.cs",
      text: "var meter = new Meter();\nConsole.WriteLine(meter.Read());\n",
      saved: null,
    });
  });
  await page.click("#btnMore");
  await page.getByText("Create project from open C# files…", { exact: true }).click();
  await page.waitForSelector("#dlgIn");
  check(
    (await page.textContent("#modal")).includes("library.cs, main.cs") &&
      (await page.textContent("#modal")).includes("main.cs runs last"),
    "creation dialog states membership and entry order",
  );
  await page.fill("#dlgIn", "sensor");
  await page.click("#dlgOk");
  check(
    await page.evaluate(() => App.project.members.length === 2 && App.project.entryId === entry.id),
    "explicit project created through UI",
  );
  await page.evaluate(() => {
    window.unrelated = App.openTab({
      name: "unrelated.cs",
      text: 'Console.WriteLine("UNRELATED");',
      saved: null,
    });
    App.activate(entry.id);
  });
  check(
    await page.evaluate(() => App.project.members.length === 2),
    "later unrelated tab not imported",
  );
  // Let the editor finish its deferred tab selection/scroll restoration before
  // synthetic keystrokes (a real paint/user interaction happens between them).
  await page.evaluate(() => new Promise(requestAnimationFrame));
  // Exercise actual editor completion/signature/hover against project exports.
  await page.fill("#ta", "var meter = new Meter();\nmeter.");
  await page.press("#ta", "Control+End");
  await page.press("#ta", "Control+Space");
  await sleep(100);
  check(
    (await page.textContent("#complete")).includes("Read"),
    "project member completion in actual editor",
  );
  await page.press("#ta", "Escape");
  await page.fill("#ta", "var meter = new Meter();\nmeter.Read(");
  await page.press("#ta", "Control+End");
  await page.press("#ta", "Control+Shift+Space");
  await sleep(100);
  check(
    (await page.isVisible(".sighelp")) && (await page.textContent(".sighelp")).includes("Read"),
    "project member signature in actual editor",
  );
  check(
    await page.evaluate(() => {
      const v = ed.ta.value,
        a = v.indexOf("Read");
      return !!ed.describe(v, { a, b: a + 4, word: "Read" });
    }),
    "project member hover uses actual source model",
  );
  await page.press("#ta", "Escape");
  await page.fill("#ta", "var meter = new Meter();\nConsole.WriteLine(meter.Read());\n");
  await page.evaluate(() => {
    ed.ta.setSelectionRange(ed.ta.value.indexOf("Read") + 1, ed.ta.value.indexOf("Read") + 1);
    ed.ta.focus();
  });
  await page.keyboard.press("F12");
  await sleep(100);
  check(
    await page.evaluate(
      () =>
        App.active === lib.id &&
        ed.ta.value.slice(ed.ta.selectionStart, ed.ta.selectionEnd) === "Read",
    ),
    "F12 selects exact cross-file member after CRLF",
  );
  await page.evaluate(() => {
    App.activate(entry.id);
    term.clear();
    ed.ta.focus();
  });
  await page.keyboard.press("Shift+F5");
  await page.waitForFunction(
    () =>
      /finished in/.test(document.getElementById("term").innerText) &&
      !dev.running &&
      dev.pending === 0,
    null,
    { timeout: 10000 },
  );
  let tt = await page.innerText("#term");
  check(
    /run project sensor\n42\n/.test(tt) && !tt.includes("UNRELATED"),
    "real serial shell executes project without unrelated tab",
  );
  check(
    !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "successful run removes only temporary bundle",
  );
  check(
    !fs.existsSync(ROOT + "/library.cs") &&
      !fs.existsSync(ROOT + "/main.cs") &&
      fs.readFileSync(ROOT + "/hello.cs", "utf8") === 'Console.WriteLine("hi from file");\n',
    "unsaved originals and existing device files untouched",
  );
  check(
    await page.evaluate(() => lib.dirty && entry.dirty),
    "project execution does not pretend originals saved",
  );
  // Real runtime error in a non-entry member maps back to its source snapshot.
  await page.evaluate(() => {
    App.activate(lib.id);
    ed.ta.focus();
    ed.insert(
      "public class Meter { public int Read() { return missingSymbol; } }",
      0,
      ed.ta.value.length,
    );
    App.activate(entry.id);
    term.clear();
  });
  await bounded(page.evaluate(() => App.runProject()));
  tt = await page.innerText("#term");
  check(/failed|unknown|undefined|error/i.test(tt), "real runtime reports unresolved source name");
  check(
    await page.evaluate(() => App.active === lib.id && ed.errLine === 1),
    "project diagnostic selects original non-entry source",
  );
  check(
    !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "failed run cleans temporary bundle",
  );
  await page.evaluate(() => {
    App.activate(lib.id);
    ed.ta.focus();
    ed.insert(
      'public class Meter { public int Read() { throw new InvalidOperationException("diagnostic:line 999"); } }',
      0,
      ed.ta.value.length,
    );
    App.activate(entry.id);
    term.clear();
  });
  await bounded(page.evaluate(() => App.runProject()));
  check(
    await page.evaluate(() => App.active === lib.id && ed.errLine === 1),
    "exact project stack frame wins over line-like exception message",
  );
  await page.evaluate(() => {
    App.activate(lib.id);
    ed.ta.focus();
    ed.insert(
      "public class Meter { public int Read() { int x = ; return 42; } }",
      0,
      ed.ta.value.length,
    );
    App.activate(entry.id);
    term.clear();
  });
  await bounded(page.evaluate(() => App.runProject()));
  check(
    await page.evaluate(() => App.active === lib.id && ed.errLine === 1),
    "actual compiler error maps original project source",
  );
  await page.evaluate(() => {
    App.activate(lib.id);
    ed.ta.focus();
    ed.insert("// changed\n" + ed.ta.value, 0, ed.ta.value.length);
    ed.stash();
    App.activate(entry.id);
    App.showProjectLocation(App.lastProjectRun.path, 1);
  });
  check(
    await page.evaluate(() => App.active === entry.id),
    "stale diagnostic never navigates to wrong position",
  );
  // Cooperative Stop must recover device and clean temporary source.
  await page.evaluate(() => {
    App.activate(lib.id);
    ed.ta.focus();
    ed.insert("public class Meter { public int Read() { return 42; } }", 0, ed.ta.value.length);
    App.activate(entry.id);
    ed.ta.focus();
    ed.insert('Console.WriteLine("PROJECT_LOOP"); while (true) {}', 0, ed.ta.value.length);
    term.clear();
  });
  await page.evaluate(() => {
    window.projectJob = App.runProject();
  });
  await page.waitForFunction(
    () => dev.running && document.getElementById("term").innerText.includes("PROJECT_LOOP"),
    null,
    { timeout: 10000 },
  );
  check(
    await page.evaluate(() => App.projectBusy && document.getElementById("btnRun").disabled),
    "project busy covers actual run",
  );
  const bundleCount = fs.readdirSync(ROOT).filter((x) => x.startsWith(".studio_project_")).length;
  await bounded(page.evaluate(() => App.runProject()));
  check(
    fs.readdirSync(ROOT).filter((x) => x.startsWith(".studio_project_")).length === bundleCount,
    "duplicate Run project does not queue a second upload",
  );
  await page.click("#btnStop");
  await bounded(page.evaluate(() => window.projectJob));
  check(
    /stopped/.test(await page.innerText("#term")) &&
      !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "Stop recovers actual shell and cleans bundle",
  );
  await page.evaluate(() => {
    App.activate(entry.id);
    ed.ta.focus();
    ed.insert("Console.WriteLine(new Meter().Read());", 0, ed.ta.value.length);
    term.clear();
  });
  await bounded(page.evaluate(() => App.runProject()));
  check(
    /\n42\n.*finished/s.test(await page.innerText("#term")),
    "normal project run succeeds after Stop",
  );
  // Closed membership and compiler capability prevent upload, not silent subset execution.
  await page.evaluate(() => {
    term.clear();
    window.compilerCapability = App.capabilities.get("Compiler");
    App.capabilities.set("Compiler", false);
  });
  await bounded(page.evaluate(() => App.runProject()));
  check(
    !/run project/.test(await page.innerText("#term")) &&
      !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "known compiler-off firmware does not upload/run source",
  );
  await page.evaluate(() => {
    App.capabilities.set("Compiler", window.compilerCapability);
  });
  const collision = ".studio_project_" + "0".repeat(32) + ".cs";
  fs.writeFileSync(ROOT + "/" + collision, "// existing owner\n");
  await page.evaluate(() => {
    window.savedRandom = crypto.getRandomValues.bind(crypto);
    crypto.getRandomValues = (a) => (a.fill(0), a);
  });
  await bounded(page.evaluate(() => App.runProject()));
  check(
    fs.readFileSync(ROOT + "/" + collision, "utf8") === "// existing owner\n" &&
      !/run project/.test(await page.innerText("#term")),
    "temporary name collision never overwrites or deletes existing file",
  );
  await page.evaluate(() => {
    crypto.getRandomValues = window.savedRandom;
  });
  fs.unlinkSync(ROOT + "/" + collision);
  await page.evaluate(() => { window.closeMember = App.closeTab(lib.id); });
  await page.waitForSelector("#modalBg.show");
  check((await page.textContent("#modal")).includes("Discard changes?"), "closing project member asks before discarding unsaved source");
  await page.click("#dlgOk");
  await page.evaluate(() => window.closeMember);
  await bounded(page.evaluate(() => App.runProject()));
  check(
    !/run project/.test(await page.innerText("#term")) &&
      !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "closed member blocks incomplete project execution",
  );
  await page.click("#btnMore");
  await page.getByText("Close project (keep files)", { exact: true }).click();
  check(
    await page.evaluate(
      () =>
        App.project === null &&
        App.tabs.some((t) => t.id === entry.id) &&
        !Lang.classInfo("Meter", ed.ta.value),
    ),
    "close project keeps tabs and clears exported assistance",
  );
  await sleep(6500); // allow real feedback to expire before representative screenshots
  await page.screenshot({ path: OUT + "/project-desktop.png" });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.click("#btnMore");
  const geometry = await page.locator("#menu").boundingBox();
  check(
    geometry.x >= 0 &&
      geometry.y >= 0 &&
      geometry.x + geometry.width <= 390 &&
      geometry.y + geometry.height <= 844,
    "project menu fits phone viewport",
  );
  await page.screenshot({ path: OUT + "/project-phone-menu.png" });
  fs.writeFileSync(OUT + "/term.txt", await page.innerText("#term"));
  console.log(fails ? `${fails} FAILED` : "ALL PROJECT BROWSER TESTS PASSED");
  await browser.close();
  proc.kill();
  process.exit(fails ? 1 : 0);
})().catch(async (e) => {
  console.error(e);
  hostProcess?.kill();
  if (activeBrowser) await activeBrowser.close().catch(() => {});
  process.exit(2);
});
