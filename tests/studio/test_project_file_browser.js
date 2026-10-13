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
const OUT = path.resolve(
  process.env.OUT || "build/studio-project-file-browser",
);
const MCS = path.resolve(process.argv[2] || "./mcs");
const HTML =
  "file://" + path.resolve(__dirname, "../../tools/studio/index.html");
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
  const page = await browser.newPage({
    viewport: { width: 1400, height: 860 },
  });
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
    chain = chain.then(() =>
      page.evaluate((b) => window.__serialPush(b), a).catch(() => {}),
    );
  });
  await page.route("https://**/*", (route) => route.abort());
  await page.addInitScript(() => {
    let ctl;
    const port = {
      readable: null,
      writable: null,
      async open() {
        // fresh streams on every open, like a real port
        if (window.__gone)
          throw new DOMException("Failed to open serial port.", "NetworkError");
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
                setTimeout(
                  () => ctl.enqueue(new TextEncoder().encode(log)),
                  300,
                );
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
    () => App.capabilitiesKnown && App.capabilities.get("Compiler") === true,
    null,
    { timeout: 10000 },
  );
  await page.evaluate(() => {
    App.tabs = [];
    App.active = null;
    ed.tab = null;
    window.originalLibrary = App.openTab({
      path: "/lib/util.cs",
      text: "class Meter { public int Read() { return 42; } }\n",
      saved: "// util\n",
    });
    window.originalEntry = App.openTab({
      name: "main.cs",
      text: "Console.WriteLine(new Meter().Read());\n",
      saved: null,
    });
  });
  await page.click("#btnMore");
  await page
    .getByText("Create project from open C# files…", { exact: true })
    .click();
  await page.fill("#dlgIn", "portable project");
  await page.click("#dlgOk");
  await page.evaluate(() => {
    window.unrelated = App.openTab({
      name: "unrelated.cs",
      text: 'Console.WriteLine("UNRELATED");',
      saved: null,
    });
    App.activate(originalEntry.id);
  });
  await page.evaluate(() => new Promise(requestAnimationFrame));
  const originalState = await page.evaluate(() => ({
    ids: App.tabs.map((t) => t.id),
    members: [...App.project.members],
    entry: App.project.entryId,
  }));
  let downloads = 0;
  page.on("download", () => downloads++);
  const openExport = async () => {
    await page.click("#btnMore");
    await page.getByText("Download source project…", { exact: true }).click();
    await page.waitForSelector("#dlgIn");
  };
  await openExport();
  check(
    (await page.textContent("#modal")).includes("plaintext") &&
      (await page.textContent("#modal")).includes("unsaved edits"),
    "export explicitly warns source snapshot is plaintext",
  );
  await page.press("#dlgIn", "Shift+F5");
  await sleep(100);
  check(
    !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")) &&
      !(await page.evaluate(() => dev.running)),
    "modal blocks background Run project shortcut",
  );
  await page.click("#dlgNo");
  check(downloads === 0, "cancelled export produces no file");
  await openExport();
  await page.fill("#dlgIn", "../outside");
  await page.click("#dlgOk");
  await sleep(50);
  check(
    downloads === 0,
    "export rejects a pathname instead of silent filesystem routing",
  );
  await openExport();
  await page.fill("#dlgIn", "my project");
  const downloadJob = page.waitForEvent("download");
  await page.click("#dlgOk");
  const exported = await downloadJob;
  check(
    exported.suggestedFilename() === "my project.microcsproj",
    "dedicated portable project extension",
  );
  const projectFile = OUT + "/download.microcsproj";
  await exported.saveAs(projectFile);
  const saved = JSON.parse(fs.readFileSync(projectFile, "utf8"));
  check(
    saved.files.length === 2 &&
      saved.files[0].name === "lib/util.cs" &&
      saved.entry === "source-2",
    "export retains explicit member order and relative labels, excludes unrelated tab",
  );
  check(
    saved.files[0].text.includes("return 42") &&
      saved.files[1].text === (await page.inputValue("#ta")),
    "snapshot includes unsaved source exactly",
  );
  check(
    await page.evaluate(() => originalLibrary.dirty && originalEntry.dirty),
    "project export does not mark individual source files saved",
  );
  check(
    fs.readFileSync(ROOT + "/lib/util.cs", "utf8") === "// util\n" &&
      !fs.existsSync(ROOT + "/main.cs"),
    "export performs no device write",
  );
  // File boundary limits are enforced before reading an oversized payload.
  await page.evaluate(() => {
    window.projectReadCount = 0;
    const read = File.prototype.arrayBuffer;
    File.prototype.arrayBuffer = function () {
      window.projectReadCount++;
      return read.call(this);
    };
  });
  const rejected = [
    {
      name: "version.microcsproj",
      buffer: Buffer.from(JSON.stringify({ ...saved, version: 999 })),
    },
    { name: "encoding.microcsproj", buffer: Buffer.from([0xc3, 0x28]) },
    {
      name: "unknown.microcsproj",
      buffer: Buffer.from(
        JSON.stringify({ ...saved, url: "https://example.invalid/source.cs" }),
      ),
    },
    {
      name: "large.microcsproj",
      buffer: Buffer.alloc(2 * 1024 * 1024 + 1, 32),
    },
  ];
  for (const candidate of rejected) {
    const before = await page.evaluate(() => window.projectReadCount);
    await page.setInputFiles("#openProject", {
      ...candidate,
      mimeType: "application/json",
    });
    await page.waitForFunction(() => !App.projectImportBusy);
    check(
      await page.evaluate(
        (state) =>
          JSON.stringify(App.tabs.map((t) => t.id)) ===
            JSON.stringify(state.ids) && App.project.entryId === state.entry,
        originalState,
      ),
      "rejected " + candidate.name + " preserves existing tabs/project",
    );
    if (candidate.name === "large.microcsproj")
      check(
        await page.evaluate((n) => window.projectReadCount === n, before),
        "oversized file rejected before arrayBuffer allocation/read",
      );
  }
  await page.setInputFiles("#openProject", projectFile);
  await page.waitForSelector("#modalBg.show");
  check(
    (await page.textContent("#modal")).includes("Nothing is executed") &&
      (await page.textContent("#modal")).includes(
        "existing tabs/files remain unchanged",
      ),
    "import confirmation explains isolated tabs and no device execution",
  );
  await page.click("#dlgNo");
  await page.waitForFunction(() => !App.projectImportBusy);
  check(
    await page.evaluate(
      (state) =>
        JSON.stringify(App.tabs.map((t) => t.id)) ===
          JSON.stringify(state.ids) && App.project.entryId === state.entry,
      originalState,
    ),
    "cancelled import leaves project/tab identities unchanged",
  );
  await page.setInputFiles("#openProject", projectFile);
  await page.waitForSelector("#modalBg.show");
  await page.press("#dlgOk", "Shift+F5");
  await sleep(50);
  check(
    !(await page.evaluate(() => dev.running)),
    "import dialog cannot start a background run",
  );
  await page.click("#dlgOk");
  await page.waitForFunction(
    () => !App.projectImportBusy && App.tabs.length === 5,
  );
  const imported = await page.evaluate(() => App.projectDocuments());
  check(
    imported.length === 2 &&
      imported[0].name === "lib/util.cs" &&
      imported[1].text === saved.files[1].text,
    "actual imported snapshot restores source contents/order",
  );
  check(
    await page.evaluate(
      (state) =>
        state.ids.every((id) => App.tabs.some((t) => t.id === id)) &&
        App.project.members.every(
          (id) =>
            !state.ids.includes(id) &&
            App.tabs.find((t) => t.id === id).local &&
            App.tabs.find((t) => t.id === id).path === null &&
            App.tabs.find((t) => t.id === id).dirty,
        ),
      originalState,
    ),
    "new independent tab IDs/no device binding, originals retained",
  );
  await page.evaluate(() => {
    term.clear();
    ed.ta.focus();
  });
  await page.press("#ta", "Shift+F5");
  await page.waitForFunction(
    () =>
      /finished in/.test(document.getElementById("term").innerText) &&
      !App.projectBusy &&
      dev.pending === 0,
    null,
    { timeout: 15000 },
  );
  check(
    /\n42\n/.test(await page.innerText("#term")) &&
      !/UNRELATED/.test(await page.innerText("#term")),
    "actual host serial execution of imported project, not mocked success",
  );
  check(
    fs.readFileSync(ROOT + "/lib/util.cs", "utf8") === "// util\n" &&
      !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "restored run preserves original device file and cleans temporary bundle",
  );
  // Save unfinished work without making it executable or discarding its text.
  await page.fill("#ta", "class Draft { /* unfinished");
  await openExport();
  const draftDownload = page.waitForEvent("download");
  await page.click("#dlgOk");
  const draft = await draftDownload;
  await draft.saveAs(OUT + "/draft.microcsproj");
  check(
    JSON.parse(fs.readFileSync(OUT + "/draft.microcsproj", "utf8")).files[1]
      .text === "class Draft { /* unfinished",
    "unfinished code saved exactly, not lost to Run-only syntax validation",
  );
  await page.evaluate(() => {
    term.clear();
  });
  await page.press("#ta", "Shift+F5");
  await page.waitForFunction(() => !App.projectBusy);
  check(
    !/run project/.test(await page.innerText("#term")) &&
      !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "unfinished draft does not upload or run",
  );
  // A fresh browser context, with no prior tabs/cache/drafts, reopens the file.
  const reopened = await browser.newPage({
    viewport: { width: 667, height: 375 },
  });
  await reopened.route("https://**/*", (r) => r.abort());
  await reopened.goto(HTML);
  check(
    await reopened.evaluate(
      () => App.tabs.length === 0 && App.project === null,
    ),
    "fresh context has no session-only membership to rely on",
  );
  await reopened.setInputFiles("#openProject", projectFile);
  await reopened.waitForSelector("#modalBg.show");
  await reopened.click("#dlgOk");
  await reopened.waitForFunction(
    () => !App.projectImportBusy && App.project && App.tabs.length === 2,
  );
  check(
    await reopened.evaluate(
      () =>
        App.project.name === "portable project" &&
        App.projectDocuments()[1].text ===
          "Console.WriteLine(new Meter().Read());\n" &&
        App.active === App.project.entryId,
    ),
    "project file alone restores membership/order/entry after reopening",
  );
  await reopened.evaluate(() => {
    ed.ta.setSelectionRange(
      ed.ta.value.indexOf("Meter") + 1,
      ed.ta.value.indexOf("Meter") + 1,
    );
    ed.ta.focus();
  });
  await reopened.keyboard.press("F12");
  await sleep(100);
  check(
    await reopened.evaluate(
      () =>
        App.tab().name === "lib/util.cs" &&
        ed.ta.value.slice(ed.ta.selectionStart, ed.ta.selectionEnd) === "Meter",
    ),
    "cross-file navigation restored in fresh offline context",
  );
  await reopened.screenshot({ path: OUT + "/reopened-project-f12.png" });
  // The DOM textarea normalizes CRLF/CR; imported canonical source must not.
  const mixedSource =
    'var payload = @"one\r\nβ two\rthree";\r\nConsole.WriteLine(payload.Length);\n';
  const mixedProject = {
    format: "microcs-source-project",
    version: 1,
    name: "line endings",
    entry: "source-1",
    files: [{ id: "source-1", name: "mixed.cs", text: mixedSource }],
  };
  await reopened.setInputFiles("#openProject", {
    name: "mixed.microcsproj",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(mixedProject)),
  });
  await reopened.waitForSelector("#modalBg.show");
  await reopened.click("#dlgOk");
  await reopened.waitForFunction(() => !App.projectImportBusy);
  check(
    await reopened.evaluate(
      (raw) => App.projectDocuments()[0].text === raw,
      mixedSource,
    ),
    "import and activation preserve CRLF/CR inside verbatim payload exactly",
  );
  await reopened.click("#btnMore");
  await reopened.getByText("Download source project…", { exact: true }).click();
  const mixedDownloadJob = reopened.waitForEvent("download");
  await reopened.click("#dlgOk");
  const mixedDownload = await mixedDownloadJob;
  await mixedDownload.saveAs(OUT + "/mixed.microcsproj");
  check(
    JSON.parse(fs.readFileSync(OUT + "/mixed.microcsproj", "utf8")).files[0]
      .text === mixedSource,
    "untouched re-export preserves exact mixed source, not textarea LF conversion",
  );
  await reopened.evaluate(() => {
    ed.ta.setSelectionRange(0, 0);
    ed.ta.focus();
  });
  await reopened.keyboard.type(" ");
  check(
    await reopened.evaluate(
      (raw) => App.projectDocuments()[0].text === " " + raw,
      mixedSource,
    ),
    "native character typing preserves mixed literal separators",
  );
  await reopened.keyboard.press("Control+z");
  await sleep(50);
  check(
    await reopened.evaluate(
      (raw) => App.projectDocuments()[0].text === raw,
      mixedSource,
    ),
    "native undo restores canonical source, including mixed EOLs",
  );
  await reopened.keyboard.press("Control+Shift+z");
  await sleep(50);
  check(
    await reopened.evaluate(
      (raw) => App.projectDocuments()[0].text === " " + raw,
      mixedSource,
    ),
    "native redo restores canonical edited snapshot",
  );
  // Real toolbar formatting, replacement and paste preserve raw payload.
  const formatSource =
    'if (true)\r\n{\nvar payload = @"one\r\nβ two\rthree";\r\nConsole.WriteLine(payload);\r}\n';
  await reopened.evaluate((raw) => {
    App.openTab({ name: "format.cs", text: raw, saved: raw, local: true });
    window.rawBefore = raw;
    ed.ta.focus();
  }, formatSource);
  await reopened.keyboard.press("Alt+Shift+f");
  check(
    await reopened.evaluate(
      () =>
        App.tab().text.includes('@"one\r\nβ two\rthree"') &&
        App.tab()
          .text.match(/\r\n|\r|\n/g)
          .join("|") === rawBefore.match(/\r\n|\r|\n/g).join("|") &&
        App.tab().text.includes("    Console"),
    ),
    "browser formatter retains literal bytes and mixed source EOLs",
  );
  await reopened.keyboard.press("Control+z");
  await sleep(50);
  check(
    await reopened.evaluate((raw) => App.tab().text === raw, formatSource),
    "formatter remains native-undoable with exact raw source",
  );
  await reopened.evaluate(() => {
    ed.ta.focus();
    ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length);
    const data = new DataTransfer();
    data.setData("text/plain", "// pasted\r\n// δεύτερο\r");
    ed.ta.dispatchEvent(
      new ClipboardEvent("paste", {
        clipboardData: data,
        bubbles: true,
        cancelable: true,
      }),
    );
  });
  check(
    await reopened.evaluate(
      (raw) => App.tab().text === raw + "// pasted\r\n// δεύτερο\r",
      formatSource,
    ),
    "paste event retains its own exact CRLF/CR bytes",
  );
  await reopened.evaluate(() => {
    App.openTab({
      name: "replace.cs",
      text: "var x=1;\r\nvar x2=2;\rConsole.WriteLine(x);\n",
      saved: null,
      local: true,
    });
    ed.ta.focus();
  });
  await reopened.keyboard.press("Control+h");
  await reopened.fill("#fFind", "var");
  await reopened.fill("#fRep", "varX");
  await reopened.click("#fRepAll");
  check(
    await reopened.evaluate(
      () =>
        App.tab().text === "varX x=1;\r\nvarX x2=2;\rConsole.WriteLine(x);\n",
    ),
    "actual replace-all retains CRLF/CR/LF across multiple changed lines",
  );
  await reopened.evaluate(() => {
    App.openTab({
      name: "move.cs",
      text: "one\r\ntwo\rthree\n",
      saved: null,
      local: true,
    });
  });
  await reopened.evaluate(() => new Promise(requestAnimationFrame));
  await reopened.evaluate(() => {
    ed.ta.setSelectionRange(4, 4);
    ed.ta.focus();
  });
  console.log(
    "MOVE_PRECONDITION",
    await reopened.evaluate(() =>
      JSON.stringify({
        source: App.tab().text,
        selection: [ed.ta.selectionStart, ed.ta.selectionEnd],
      }),
    ),
  );
  await reopened.keyboard.press("Alt+ArrowUp");
  check(
    await reopened.evaluate(() => App.tab().text === "two\r\none\rthree\n"),
    "move line uses raw source segments rather than normalized payload",
  );
  await reopened.keyboard.press("Control+z");
  await sleep(50);
  check(
    await reopened.evaluate(() => App.tab().text === "one\r\ntwo\rthree\n"),
    "move line native undo restores original mixed endings",
  );
  await reopened.evaluate(() => {
    ed.ta.setSelectionRange(0, 7);
    ed.ta.focus();
  });
  await reopened.keyboard.press("Alt+Shift+ArrowDown");
  check(
    await reopened.evaluate(
      () => App.tab().text === "one\r\ntwo\r\none\r\ntwo\rthree\n",
    ),
    "duplicate lines keeps copied raw separators",
  );
  // Same mixed literal through the actual serial boundary and real compiler.
  const originalFile = OUT + "/mixed-original.cs";
  fs.writeFileSync(originalFile, mixedSource);
  const originalRun = require("child_process").spawnSync(MCS, [originalFile], {
    encoding: "utf8",
    timeout: 5000,
  });
  check(
    originalRun.status === 0,
    "original mixed-EOL source is valid for the real compiler",
  );
  await page.setInputFiles("#openProject", {
    name: "mixed.microcsproj",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(mixedProject)),
  });
  await page.waitForSelector("#modalBg.show");
  await page.click("#dlgOk");
  await page.waitForFunction(() => !App.projectImportBusy);
  await page.evaluate(() => {
    term.clear();
    ed.ta.focus();
  });
  await page.press("#ta", "Shift+F5");
  await page.waitForFunction(
    () =>
      /finished in/.test(document.getElementById("term").innerText) &&
      !App.projectBusy &&
      dev.pending === 0,
    null,
    { timeout: 15000 },
  );
  check(
    (await page.innerText("#term")).includes(
      "\n" + originalRun.stdout.trim() + "\n",
    ),
    "imported literal runtime output matches unchanged raw CLI source",
  );
  check(
    !fs.readdirSync(ROOT).some((x) => x.startsWith(".studio_project_")),
    "mixed-EOL project execution cleans its temporary source",
  );
  // Line-only diagnostic mapping is an isolated UI/model check, not a fake device success.
  check(
    await reopened.evaluate(() => {
      const raw = "var a=1;\rvar b=2;\r\nnope();\n";
      const t = App.openTab({
        name: "diagnostic.cs",
        text: raw,
        saved: raw,
        local: true,
      });
      App.lastProjectRun = {
        path: "/.test-map.cs",
        bundle: Lang.projectBundle(
          [{ id: t.id, name: t.name, text: raw }],
          t.id,
        ),
      };
      return App.showProjectLocation("/.test-map.cs", 2) && ed.errLine === 3;
    }),
    "LF compiler row maps past an earlier CR-only editor separator without guessing",
  );
  check(
    await reopened.evaluate(() => {
      ed.setError(0);
      return App.showProjectLocation("/.test-map.cs", 1) && ed.errLine === 0;
    }),
    "ambiguous compiler row spanning CR-only editor rows refuses a guessed highlight",
  );
  check(
    await reopened.evaluate(() => {
      const t = App.tab();
      t.text = t.text.replace("\r\n", "\n");
      ed.ta.value = SourceText.display(t.text);
      return App.showProjectLocation("/.test-map.cs", 2) && ed.errLine === 0;
    }),
    "EOL-only source change invalidates the exact raw run snapshot",
  );
  check(
    await reopened.evaluate(() => {
      ed.sourceHistory = [];
      ed.sourceHistoryBytes = 0;
      for (let i = 0; i < 140; i++) ed.rememberSource("snapshot " + i);
      return (
        ed.sourceHistory.length === 128 &&
        ed.sourceHistoryBytes <= 2 * 1024 * 1024
      );
    }),
    "canonical undo side cache enforces its entry count bound",
  );
  check(
    await reopened.evaluate(() => {
      ed.rememberSource("x".repeat(1024 * 1024 + 1));
      return ed.sourceHistoryBytes <= 2 * 1024 * 1024;
    }),
    "canonical undo side cache enforces its raw-plus-display size bound",
  );
  await reopened.screenshot({ path: OUT + "/editing-short-window.png" });
  await reopened.close();
  fs.writeFileSync(OUT + "/term.txt", await page.innerText("#term"));
  console.log(
    fails ? `${fails} FAILED` : "ALL PORTABLE PROJECT BROWSER TESTS PASSED",
  );
  await browser.close();
  proc.kill();
  process.exit(fails ? 1 : 0);
})().catch(async (e) => {
  console.error(e);
  hostProcess?.kill();
  if (activeBrowser) await activeBrowser.close().catch(() => {});
  process.exit(2);
});
