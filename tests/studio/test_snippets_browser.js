// Offline real Chromium keyboard regressions. No device, network or automatic execution.
// CHROME=$(command -v chromium) node tests/studio/test_snippets_browser.js [./mcs]
// BENCH_ONLY=1 [BENCH_BASELINE=1] saves comparable legacy acceptance timing;
// Baseline timing injects exact b4f670a editor/catalog; normal invocation runs
// the functional regressions and rejects any baseline-mode flag.
const assert = require("assert"),
  fs = require("fs"),
  path = require("path"),
  os = require("os");
const { execFileSync, spawnSync } = require("child_process");
const { createHash } = require("crypto");
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  ({ chromium } = require("playwright-core"));
}
const BASE = "b4f670a2a778b92322bf868cba300f9b767df893";
const HTML =
  "file://" + path.resolve(__dirname, "../../tools/studio/index.html");
const OUT = path.resolve(process.env.OUT || "build/studio-snippets-browser");
fs.mkdirSync(OUT, { recursive: true });
let browser,
  checks = 0;
const equal = (actual, expected, why) => {
  assert.deepStrictEqual(actual, expected, why);
  checks++;
  console.log("PASS " + why);
};
const hash = (value) => createHash("sha256").update(value).digest("hex");
async function prepare(page, baseline = false) {
  await page.route("https://**/*", (route) => route.abort());
  if (baseline)
    for (const name of ["studio.js", "snippets.js"]) {
      const body = execFileSync(
        "git",
        ["show", BASE + ":tools/studio/" + name],
        { encoding: "utf8" },
      );
      await page.route("**/" + name, (route) =>
        route.fulfill({ body, contentType: "text/javascript" }),
      );
    }
  await page.goto(HTML);
  await page.waitForFunction(
    () => typeof App !== "undefined" && typeof ed !== "undefined",
  );
}
async function open(page, text = "", readOnly = false) {
  await page.evaluate(
    ({ text, readOnly }) =>
      App.openTab({
        name: "snippet.cs",
        text,
        saved: text,
        local: true,
        readOnly,
      }),
    { text, readOnly },
  );
  await page.evaluate(() => new Promise(requestAnimationFrame));
  await page.focus("#ta");
  await page.evaluate(() =>
    ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length),
  );
}
const state = (page) =>
  page.evaluate(() => ({
    raw: ed.tab.text,
    view: ed.value,
    sel: [ed.ta.selectionStart, ed.ta.selectionEnd],
    selected: ed.value.slice(ed.ta.selectionStart, ed.ta.selectionEnd),
    active: !!ed.snippet,
    field: ed.snippet?.field().id,
    dirty: ed.tab.dirty,
    error: ed.errLine,
    history: ed.sourceHistory.length,
  }));
async function choose(page, label, prefix, explicit = true, accept = true) {
  await page.keyboard.type(prefix);
  if (explicit) await page.press("#ta", "Control+Space");
  await page.waitForFunction(
    (label) =>
      ed.box.style.display === "block" &&
      ed.items.some((it) => it.kind === "s" && it.label === label),
    label,
  );
  const count = await page.evaluate((label) => {
    const index = ed.items.findIndex(
      (it) => it.kind === "s" && it.label === label,
    );
    return (index - ed.sel + ed.items.length) % ed.items.length;
  }, label);
  for (let i = 0; i < count; i++) await page.press("#ta", "ArrowDown");
  if (accept) await page.press("#ta", "Tab");
}
async function completion(page, label, kind, accept = true) {
  await page.press("#ta", "Control+Space");
  await page.waitForFunction(
    ({ label, kind }) =>
      ed.box.style.display === "block" &&
      ed.items.some((it) => it.label === label && it.kind === kind),
    { label, kind },
  );
  const count = await page.evaluate(
    ({ label, kind }) => {
      const index = ed.items.findIndex(
        (it) => it.label === label && it.kind === kind,
      );
      return (index - ed.sel + ed.items.length) % ed.items.length;
    },
    { label, kind },
  );
  for (let i = 0; i < count; i++) await page.press("#ta", "ArrowDown");
  if (accept) await page.press("#ta", "Tab");
}
async function synthetic(page, text) {
  // Authoring-validation tests use real keyboard acceptance of injected trusted catalog metadata.
  await page.evaluate((text) => {
    ed.items = [{ kind: "s", label: "testsnippet", text }];
    ed.sel = 0;
    ed.ctx = { start: ed.ta.selectionStart, end: ed.ta.selectionStart };
    ed.box.style.display = "block";
  }, text);
  await page.press("#ta", "Tab");
}
async function benchmark(baseline) {
  const hostContext = () => ({
    loadAverage: os.loadavg(),
    activeBuildProcessNames: execFileSync("ps", ["-eo", "comm"], {
      encoding: "utf8",
    })
      .split("\n")
      .map((s) => s.trim())
      .filter((s) =>
        /^(make|gcc|g\+\+|cc|cc1|cc1plus|clang|cmake|ninja|ctest|mcs)$/.test(s),
      ),
  });
  const hostBefore = hostContext();
  const fixture =
    "// fixed large-file fixture β🚀\n".repeat(2000) +
    'var payload = @"first  \r\nβ\r🚀\nend";\r\n';
  const insertion = "if (true)\n{\n    Console.WriteLine(1);\n}\n$0";
  const expectedRaw = fixture + insertion.replace("$0", "");
  const caret = expectedRaw.replace(/\r\n|\r/g, "\n").length;
  fs.writeFileSync(path.join(OUT, "latency-fixture.cs"), fixture);
  const samples = {
    cold: [],
    warm: [],
    navigation_new: [],
    field_edit_new: [],
  };
  async function sample(page) {
    await open(page, fixture);
    return page.evaluate((text) => {
      const start = ed.value.length;
      ed.items = [{ kind: "s", label: "benchmark", text }];
      ed.sel = 0;
      ed.ctx = { start, end: start };
      const t = performance.now();
      ed.accept();
      const elapsed = performance.now() - t;
      return {
        elapsed,
        raw: ed.tab.text,
        selection: [ed.ta.selectionStart, ed.ta.selectionEnd],
      };
    }, insertion);
  }
  for (let i = 0; i < 5; i++) {
    const page = await browser.newPage();
    await prepare(page, baseline);
    const result = await sample(page);
    equal(
      result.raw,
      expectedRaw,
      "cold benchmark exact legacy output matches",
    );
    equal(
      result.selection,
      [caret, caret],
      "cold benchmark exact legacy selection matches",
    );
    equal(
      result.raw.includes("$0"),
      false,
      "cold benchmark emits no legacy marker",
    );
    samples.cold.push(result.elapsed);
    await page.close();
  }
  const page = await browser.newPage();
  await prepare(page, baseline);
  for (let i = 0; i < 20; i++) {
    const result = await sample(page);
    equal(
      result.raw,
      expectedRaw,
      "warm benchmark exact legacy output matches " + i,
    );
    equal(
      result.selection,
      [caret, caret],
      "warm benchmark exact legacy selection matches " + i,
    );
    samples.warm.push(result.elapsed);
  }
  if (!baseline) {
    await open(page, fixture);
    await choose(page, "for", "fo");
    await page.evaluate(() => {
      window.__navigationSamples = [];
      ed.ta.addEventListener(
        "keydown",
        (e) => {
          if (e.key === "Tab") window.__navigationStart = performance.now();
        },
        true,
      );
      ed.ta.addEventListener("keydown", (e) => {
        if (e.key === "Tab")
          window.__navigationSamples.push(
            performance.now() - window.__navigationStart,
          );
      });
    });
    for (let i = 0; i < 20; i++) {
      await page.press("#ta", "Tab");
      await page.press("#ta", "Shift+Tab");
    }
    samples.navigation_new = await page.evaluate(
      () => window.__navigationSamples,
    );
    equal(
      samples.navigation_new.length,
      40,
      "NEW actual keyboard handler navigation timing samples",
    );
    equal(
      (await state(page)).active,
      true,
      "NEW navigation timing leaves a valid field session",
    );
    await page.keyboard.type("30");
    await page.evaluate(() => {
      window.__fieldEditSamples = [];
      ed.ta.addEventListener(
        "input",
        () => {
          window.__fieldEditStart = performance.now();
        },
        true,
      );
      ed.ta.addEventListener("input", () => {
        window.__fieldEditSamples.push(
          performance.now() - window.__fieldEditStart,
        );
      });
    });
    for (let i = 0; i < 20; i++) {
      await page.press("#ta", "Backspace");
      await page.keyboard.press("0");
    }
    samples.field_edit_new = await page.evaluate(
      () => window.__fieldEditSamples,
    );
    equal(
      samples.field_edit_new.length,
      40,
      "NEW actual active-field input handler timing samples",
    );
    equal(
      (await state(page)).active,
      true,
      "NEW active-field edit samples retain a valid session",
    );
  }
  await page.close();
  const metadata = {
    base: BASE,
    head: execFileSync("git", ["rev-parse", "HEAD"], {
      encoding: "utf8",
    }).trim(),
    baseline,
    measuredSourceCommit: baseline
      ? BASE
      : execFileSync("git", ["rev-parse", "HEAD"], { encoding: "utf8" }).trim(),
    benchmarkScriptSha256: hash(fs.readFileSync(__filename)),
    measuredWorkingFiles: Object.fromEntries(
      ["studio.js", "snippets.js"].map((name) => [
        name,
        hash(
          baseline
            ? execFileSync("git", ["show", BASE + ":tools/studio/" + name])
            : fs.readFileSync(
                path.resolve(__dirname, "../../tools/studio", name),
              ),
        ),
      ]),
    ),
    fixtureSha256: hash(fixture),
    fixtureUtf16: fixture.length,
    insertionSha256: hash(insertion),
    expectedRawOutputSha256: hash(expectedRaw),
    expectedSelection: [caret, caret],
    fixtureLines: fixture.split(/\r\n|\r|\n/).length,
    browser: browser.version(),
    browserExecutable: process.env.CHROME,
    playwright: (() => {
      try {
        return require("playwright/package.json").version;
      } catch {
        return require("playwright-core/package.json").version;
      }
    })(),
    node: process.version,
    os: os.platform() + " " + os.release(),
    cpu: os.cpus()[0]?.model,
    timestamp: new Date().toISOString(),
    units:
      "milliseconds, performance.now around synchronous accept; NEW keyboard navigation and active-field input handler durations (not asynchronous paint completion)",
    coldDefinition:
      "first editor acceptance after fresh page initialization, five pages",
    warmDefinition:
      "twenty acceptances on one initialized page, fixed fixture reset before each acceptance",
    hostBefore,
    hostAfter: hostContext(),
    limitation:
      "Shared host, no CPU affinity or isolated competing build load; larger-file responsiveness beyond this exact fixture is unmeasured. No unsupported numbered-feature baseline or speedup claim.",
    samples,
  };
  fs.writeFileSync(
    path.join(
      OUT,
      baseline ? "latency-baseline.json" : "latency-candidate.json",
    ),
    JSON.stringify(metadata, null, 2) + "\n",
  );
  console.log(
    "PASS raw latency samples/provenance saved; no unsupported navigation baseline or speedup claim",
  );
}
(async () => {
  browser = await chromium.launch({
    executablePath: process.env.CHROME,
    args: ["--no-sandbox"],
  });
  if (process.env.BENCH_ONLY) {
    await benchmark(!!process.env.BENCH_BASELINE);
    return;
  }
  assert(
    !process.env.BENCH_BASELINE,
    "Baseline mode is only a separately labeled benchmark, never a functional bypass",
  );
  const page = await browser.newPage({
      viewport: { width: 1400, height: 860 },
    }),
    errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  let downloads = 0;
  page.on("download", () => downloads++);
  await prepare(page);
  for (const [label, selected] of [
    ["for", "3"],
    ["while", "false"],
  ]) {
    await open(page);
    await choose(page, label, label, false);
    equal(
      (await state(page)).selected,
      selected,
      "fully typed snippet label remains available without explicit completion: " +
        label,
    );
    equal(
      (await state(page)).active,
      true,
      "exact-label keyboard acceptance creates a real editable session: " +
        label,
    );
    await page.press("#ta", "Escape");
  }
  for (const movement of ["Control+End", "Control+Home", "Shift+ArrowLeft"]) {
    await open(page, ";\nConsole.WriteLine(1);\n");
    await page.evaluate(() => ed.ta.setSelectionRange(0, 0));
    await choose(page, "for", "fo", true, false);
    const before = await state(page);
    await page.press("#ta", movement);
    equal(
      await page.evaluate(() => ed.box.style.display),
      "block",
      "stale completion characterization keeps popup visible: " + movement,
    );
    await page.press("#ta", "Tab");
    equal(
      (await state(page)).raw,
      before.raw,
      "stale snippet acceptance never replaces unrelated source: " + movement,
    );
    equal(
      (await state(page)).active,
      false,
      "stale caret/selection never starts field navigation: " + movement,
    );
  }
  const ordinary = [
    { label: "Console", kind: "c", prefix: "Con", header: "" },
    { label: "WriteLine", kind: "m", prefix: "Console.Wri", header: "" },
    { label: "return", kind: "k", prefix: "ret", header: "" },
    { label: "count", kind: "l", prefix: "co", header: "int count = 1;\n" },
  ];
  for (const item of ordinary) {
    await open(page, item.header);
    await page.keyboard.type(item.prefix);
    await completion(page, item.label, item.kind);
    const expected =
      item.kind === "m" ? "Console.WriteLine" : item.header + item.label;
    equal(
      (await state(page)).view,
      expected,
      "valid collapsed acceptance unchanged: " + item.kind,
    );
    await page.press("#ta", "Control+Space");
    equal(
      await page.evaluate(
        ({ label, kind }) =>
          ed.box.style.display === "block" &&
          ed.items.some((it) => it.label === label && it.kind === kind),
        item,
      ),
      false,
      "ordinary exact-label exclusion remains unchanged: " + item.kind,
    );
    for (const movement of ["Control+End", "Control+Home", "Shift+ArrowLeft"]) {
      await open(page, item.header + ";\nConsole.WriteLine(1);\n");
      await page.evaluate(
        (p) => ed.ta.setSelectionRange(p, p),
        item.header.length,
      );
      await page.keyboard.type(item.prefix);
      await completion(page, item.label, item.kind, false);
      await page.evaluate(() => {
        ed.tab.saved = ed.tab.text;
        ed.tab.dirty = false;
        ed.setError(1);
      });
      await page.press("#ta", movement);
      const before = await state(page);
      equal(
        await page.evaluate(() => ed.box.style.display),
        "block",
        "ordinary stale popup really visible: " + item.kind + " " + movement,
      );
      await page.press("#ta", "Tab");
      const after = await state(page);
      equal(
        after.raw,
        before.raw,
        "stale ordinary acceptance preserves unrelated trailing line: " +
          item.kind +
          " " +
          movement,
      );
      equal(
        after.sel,
        before.sel,
        "stale rejection preserves selection: " + item.kind + " " + movement,
      );
      equal(
        after.dirty,
        before.dirty,
        "stale rejection preserves dirty state: " + item.kind + " " + movement,
      );
      equal(
        after.error,
        before.error,
        "stale rejection preserves source diagnostic: " +
          item.kind +
          " " +
          movement,
      );
      equal(
        after.history,
        before.history,
        "stale rejection adds no source-history snapshot: " +
          item.kind +
          " " +
          movement,
      );
    }
  }
  await open(page);
  await choose(page, "for", "fo");
  await page.keyboard.type("Co");
  await completion(page, "Console", "c", false);
  const fieldSource = (await state(page)).raw;
  await page.press("#ta", "Control+End");
  equal(
    (await state(page)).active,
    false,
    "ordinary popup inside field cancels with field session",
  );
  equal(
    await page.evaluate(() => ed.box.style.display),
    "none",
    "field cancellation closes associated ordinary popup",
  );
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).raw.startsWith(fieldSource),
    true,
    "ordinary Tab after field cancellation preserves existing source",
  );
  for (const range of [
    [-1, 3],
    [0.5, 3],
    [0, 3.5],
    [4, 3],
    [0, 1000],
    ["0", 3],
    [0, NaN],
    [0, Infinity],
  ]) {
    await open(page, "Con");
    await page.evaluate((range) => {
      ed.items = [{ kind: "c", label: "Console" }];
      ed.sel = 0;
      ed.ctx = { start: range[0], end: range[1] };
      ed.box.style.display = "block";
      ed.tab.dirty = false;
      ed.setError(1);
    }, range);
    const before = await state(page);
    await page.press("#ta", "Tab");
    const after = await state(page);
    equal(
      after.raw,
      before.raw,
      "integer/range guard rejects invalid context without mutation: " +
        String(range),
    );
    equal(
      after.sel,
      before.sel,
      "invalid context rejection leaves selection unchanged",
    );
    equal(
      [after.dirty, after.error, after.history],
      [before.dirty, before.error, before.history],
      "invalid context rejection leaves dirty/diagnostic/history unchanged",
    );
  }
  const literal = 'var payload = @"first  \r\nβ\r🚀\nend";\r\n    ';
  await open(page, literal);
  await choose(page, "for", "fo");
  equal(
    (await state(page)).selected,
    "3",
    "completion keyboard accepts snippet and selects first default",
  );
  equal(
    (await state(page)).raw.startsWith(literal),
    true,
    "insertion preserves mixed-EOL multiline literal bytes",
  );
  equal(
    /\$\{|\$0/.test((await state(page)).raw),
    false,
    "numbered/final markers never enter source",
  );
  equal(
    await page.evaluate(() => ed.box.style.display),
    "none",
    "accepted completion popup closes",
  );
  await page.keyboard.type("5");
  equal(
    (await state(page)).field,
    1,
    "native typing edits selected field and retains session",
  );
  await page.press("#ta", "Control+Space");
  equal(
    await page.evaluate(() => ed.box.style.display),
    "block",
    "explicit completion popup really opens during active-field editing",
  );
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).selected,
    "Console.WriteLine(i);",
    "active field Tab wins over completion/indentation",
  );
  await page.press("#ta", "Shift+Tab");
  equal(
    (await state(page)).selected,
    "5",
    "backward traversal sees edited default",
  );
  await page.press("#ta", "Shift+Tab");
  equal(
    (await state(page)).selected,
    "5",
    "first ShiftTab stays first without indentation",
  );
  await page.press("#ta", "Tab");
  await page.keyboard.type("Console.WriteLine(i + 10);");
  equal(
    (await state(page)).active,
    true,
    "native typing plus automatic parentheses keep field tracking coherent",
  );
  await page.press("#ta", "Tab");
  let filled = await state(page);
  equal(filled.active, false, "last field Tab exits at final marker");
  equal(
    filled.sel,
    [filled.view.length, filled.view.length],
    "final caret points at actual expanded end",
  );
  equal(
    filled.raw.startsWith(literal),
    true,
    "field edits retain all original raw literal separators",
  );
  equal(filled.dirty, true, "filled snippet marks document dirty");
  if (process.argv[2]) {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), "mcs-browser-snippet-"));
    try {
      const source = path.join(temp, "filled.cs"),
        image = path.join(temp, "filled.mcsb");
      fs.writeFileSync(source, filled.raw);
      const mcs = path.resolve(process.argv[2]);
      for (const args of [
        [source],
        ["-c", source, "-o", image],
        [image],
        ["--xip", image],
      ]) {
        const result = spawnSync(mcs, args, {
          encoding: "utf8",
          timeout: 10000,
        });
        equal(
          result.status,
          0,
          "actual keyboard-filled source/copy/XIP compiles and executes " +
            args[0],
        );
        if (args[0] !== "-c")
          equal(
            result.stdout.trim(),
            "10\n11\n12\n13\n14",
            "actual keyboard-filled output",
          );
      }
    } finally {
      fs.rmSync(temp, { recursive: true, force: true });
    }
  }
  await page.screenshot({ path: path.join(OUT, "filled-snippet.png") });
  for (const newline of ["\n", "\r\n", "\r"]) {
    const prefix = 'var text = @"β🚀' + newline + 'literal  ";' + newline;
    await open(page, prefix);
    await choose(page, "ife", "if");
    await page.keyboard.type("false");
    await page.press("#ta", "Tab");
    await page.keyboard.type('Console.WriteLine("A");');
    await page.press("#ta", "Tab");
    await page.keyboard.type('Console.WriteLine("B");');
    await page.press("#ta", "Tab");
    equal(
      (await state(page)).raw.startsWith(prefix),
      true,
      "original literal bytes remain " + JSON.stringify(newline),
    );
    equal(
      (
        (await state(page)).raw.slice(prefix.length).match(/\r\n|\r|\n/g) || []
      ).every((e) => e === newline),
      true,
      "new snippet structural lines use original EOL " +
        JSON.stringify(newline),
    );
  }
  await open(page);
  await choose(page, "for", "fo");
  await page.keyboard.type("10");
  await page.press("#ta", "ArrowLeft");
  await page.press("#ta", "Backspace");
  equal(
    (await state(page)).active,
    true,
    "backspace inside field preserves navigation",
  );
  await page.press("#ta", "Delete");
  equal((await state(page)).active, true, "delete leaves valid empty field");
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).selected,
    "Console.WriteLine(i);",
    "later offsets follow field deletion",
  );
  await page.evaluate(() => {
    const data = new DataTransfer();
    data.setData(
      "text/plain",
      'Console.WriteLine(@"β🚀\r\nline  ");\rConsole.WriteLine(1);',
    );
    ed.ta.dispatchEvent(
      new ClipboardEvent("paste", {
        clipboardData: data,
        bubbles: true,
        cancelable: true,
      }),
    );
  });
  equal(
    (await state(page)).active,
    true,
    "exact multiline paste into selected field retains navigation",
  );
  equal(
    (await state(page)).raw.includes('β🚀\r\nline  ");\r'),
    true,
    "paste raw literal/EOL bytes retained",
  );
  await page.press("#ta", "Shift+Tab");
  equal(
    (await state(page)).selected,
    "",
    "backward navigation after paste keeps earlier empty field",
  );
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).selected.includes("β🚀\nline"),
    true,
    "display selection uses normalized UTF16 not raw offsets",
  );
  let prior = await state(page);
  await page.press("#ta", "Escape");
  equal((await state(page)).active, false, "Escape commits without rollback");
  equal((await state(page)).raw, prior.raw, "Escape preserves edited bytes");
  await open(page);
  await choose(page, "for", "fo");
  await page.press("#ta", "Tab");
  await page.evaluate(() => {
    const data = new DataTransfer();
    data.setData("text/plain", 'Console.WriteLine("${1:literal} $0");');
    ed.ta.dispatchEvent(
      new ClipboardEvent("paste", {
        clipboardData: data,
        bubbles: true,
        cancelable: true,
      }),
    );
  });
  equal(
    (await state(page)).raw.includes('"${1:literal} $0"'),
    true,
    "ordinary pasted source resembling markers is never reinterpreted",
  );
  equal(
    (await state(page)).active,
    true,
    "ordinary marker-looking source edits retain valid field offsets",
  );
  await open(page);
  await choose(page, "if", "i");
  prior = await state(page);
  await page.press("#ta", "Enter");
  equal((await state(page)).active, false, "Enter exits session");
  equal(
    (await state(page)).raw,
    prior.raw,
    "Enter exit does not insert newline",
  );
  await page.press("#ta", "Enter");
  equal(
    (await state(page)).view !== prior.view,
    true,
    "ordinary Enter works after exit",
  );

  for (const shortcut of [
    "Control+z",
    "Control+Shift+z",
    "Control+y",
    "Control+Alt+z",
    "Control+Alt+Shift+z",
  ]) {
    await open(page);
    await choose(page, "for", "fo");
    await page.press("#ta", shortcut);
    equal(
      (await state(page)).active,
      false,
      "undo/redo request clears session before native/raw history " + shortcut,
    );
    equal(
      /\$\{|\$0/.test((await state(page)).raw),
      false,
      "history request never resurrects template markers",
    );
  }
  await open(page);
  await choose(page, "for", "fo");
  await page.press("#ta", "Control+z");
  await page.press("#ta", "Control+Shift+z");
  equal(
    (await state(page)).active,
    false,
    "native redo never resurrects stale field offsets",
  );
  equal(
    (await state(page)).view.includes("for (int i"),
    true,
    "native snippet insertion redo works",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() =>
    ed.ta.dispatchEvent(
      new InputEvent("beforeinput", {
        inputType: "historyUndo",
        bubbles: true,
        cancelable: true,
      }),
    ),
  );
  equal(
    (await state(page)).active,
    false,
    "menu-style beforeinput history cancels session",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() => {
    ed.ta.readOnly = true;
  });
  await page.press("#ta", "Tab");
  equal((await state(page)).active, false, "readOnly cancels navigation");
  await page.evaluate(() => (ed.ta.readOnly = false));
  await open(page);
  await choose(page, "for", "fo");
  prior = await state(page);
  await page.evaluate(() =>
    ed.ta.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "Enter",
        isComposing: true,
        bubbles: true,
        cancelable: true,
      }),
    ),
  );
  equal((await state(page)).active, false, "composing key cancels session");
  equal(
    (await state(page)).raw,
    prior.raw,
    "composing Enter is not stolen for newline/exit",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() =>
    ed.ta.dispatchEvent(
      new CompositionEvent("compositionstart", { bubbles: true }),
    ),
  );
  equal(
    (await state(page)).active,
    false,
    "IME compositionstart cancels session",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() => {
    window.__snippetTab = ed.tab;
    App.openTab({
      name: "other.cs",
      text: "Console.WriteLine(1);",
      saved: null,
    });
    App.activate(__snippetTab.id);
  });
  await page.evaluate(() => new Promise(requestAnimationFrame));
  equal(
    (await state(page)).active,
    false,
    "tab reopening does not restore old navigation offsets",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.press("#ta", "Home");
  equal(
    (await state(page)).active,
    false,
    "arrow/Home outside active field cancels session",
  );
  prior = await state(page);
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).view !== prior.view,
    true,
    "Tab returns to ordinary indentation after cancellation",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() => {
    const f = ed.snippet.field();
    ed.ta.setSelectionRange(f.a - 1, f.b);
    ed.ta.dispatchEvent(new Event("select"));
  });
  equal(
    (await state(page)).active,
    false,
    "selection straddling field boundary cancels before editing",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.evaluate(() => {
    ed.ta.setSelectionRange(0, 0);
    ed.ta.dispatchEvent(new MouseEvent("click", { bubbles: true }));
  });
  equal(
    (await state(page)).active,
    false,
    "mouse caret outside field cancels without stale navigation",
  );
  await open(page);
  await choose(page, "for", "fo");
  await page.click("#btnMore");
  equal(
    (await state(page)).active,
    false,
    "focus leaving editor cancels snippet safely",
  );
  await page.press("body", "Escape");

  for (const bad of [
    "${1:x}${1:y}$0",
    "${1:x}$0$0",
    "${1:${2:x}}$0",
    "${1:missing",
    "${33:x}$0",
  ]) {
    await open(page, "Console.WriteLine(1);\n");
    prior = await state(page);
    await synthetic(page, bad);
    equal(
      (await state(page)).raw,
      prior.raw,
      "malformed snippet rejected before mutation " + bad,
    );
    equal(
      (await state(page)).active,
      false,
      "malformed snippet creates no session",
    );
  }
  await open(page);
  await synthetic(page, "${3:🚀}${1:}${2:}$0");
  equal(
    (await state(page)).field,
    1,
    "nontextual numeric order begins at field1",
  );
  await page.keyboard.type("A");
  await page.press("#ta", "Tab");
  await page.keyboard.type("B");
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).selected,
    "🚀",
    "earlier textual UTF16 field unchanged by later empty-field edits",
  );
  await page.keyboard.type("β");
  await page.press("#ta", "Tab");
  equal(
    (await state(page)).view,
    "βAB",
    "adjacent/default field edits preserve textual placement",
  );
  equal(
    (await state(page)).sel,
    [3, 3],
    "final caret follows all numeric-order edits",
  );
  await open(page);
  await choose(page, "cw", "c");
  equal(
    (await state(page)).active,
    false,
    "legacy $0-only snippet has no field session",
  );
  equal(
    (await state(page)).view,
    "Console.WriteLine();",
    "legacy insertion unchanged",
  );
  equal((await state(page)).sel, [18, 18], "legacy final caret unchanged");
  await open(page);
  await synthetic(page, "${1:value}$0");
  equal((await state(page)).active, true, "last-tab close starts with live fields");
  await page.evaluate(async () => {
    const tab = ed.tab;
    tab.saved = tab.text;
    tab.dirty = false;
    App.tabs = [tab];
    await App.closeTab(tab.id);
  });
  equal(
    await page.evaluate(() => !!ed.snippet),
    false,
    "closing the last tab releases field session immediately",
  );
  await open(page, "// " + "x".repeat(1024 * 1024) + "\n");
  const prefix = (await state(page)).raw;
  await choose(page, "if", "i");
  equal(
    (await state(page)).raw.startsWith(prefix),
    true,
    "oversized document still receives exact defaults",
  );
  equal(
    (await state(page)).active,
    false,
    "oversized document declines navigation without stale offsets",
  );
  equal(
    await page.evaluate(() =>
      document.body.textContent.includes(
        "field navigation is unavailable beyond the bounded document size",
      ),
    ),
    true,
    "oversized document explains bounded fallback",
  );
  await open(page, 'var text = @"literal ');
  await page.keyboard.type("fo");
  await page.press("#ta", "Control+Space");
  equal(
    await page.evaluate(() => ed.box.style.display === "block"),
    false,
    "snippets never offered in multiline literal context",
  );
  equal(downloads, 0, "snippet operations never implicitly save/download");
  equal(errors, [], "no browser errors");
  await page.close();
  console.log(`PASS ${checks} real Chromium snippet assertions`);
})()
  .catch((e) => {
    console.error(e);
    process.exitCode = 1;
  })
  .finally(async () => {
    if (browser) await browser.close();
  });
