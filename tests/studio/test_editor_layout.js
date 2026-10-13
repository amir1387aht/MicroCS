// Real offline Studio layout/input state. No device or protocol success is mocked.
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  ({ chromium } = require("playwright-core"));
}
const assert = require("assert"),
  fs = require("fs"),
  path = require("path");
const OUT = path.resolve("build/studio-editor-layout");
fs.mkdirSync(OUT, { recursive: true });
(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.CHROME || undefined,
    args: ["--no-sandbox"],
  });
  let checks = 0;
  try {
    for (const [width, height] of [
      [1400, 860],
      [390, 844],
      [667, 375],
      [320, 568],
    ])
      for (const theme of ["dark", "light"]) {
        const page = await browser.newPage({ viewport: { width, height } });
        await page.route("https://**/*", (r) => r.abort());
        await page.goto("file://" + path.resolve("tools/studio/index.html"));
        if (theme === "light") await page.click("#btnTheme");
        await page.evaluate(() => {
          App.openTab({
            name: "layout.cs",
            text: 'Console.WriteLine(@"فارسی\r\n🚀");\r\n',
            saved: null,
            local: true,
          });
        });
        await page.evaluate(() => new Promise(requestAnimationFrame));
        let g = await page.evaluate(() => {
          const r = document.getElementById("code").getBoundingClientRect();
          return {
            width: r.width,
            height: r.height,
            left: r.left,
            right: r.right,
            document: document.documentElement.scrollWidth,
            source: App.tab().text,
          };
        });
        assert(
          g.width >= Math.min(280, width - 60) &&
            g.height >= 96 &&
            g.left >= 0 &&
            g.right <= width,
          JSON.stringify({ width, height, theme, g }),
        );
        checks++;
        assert(g.document <= width, "layout does not widen the page");
        checks++;
        if (width <= 600) {
          assert(
            (await page.getAttribute("#btnExplorer", "aria-expanded")) ===
              "false",
            "phone default leaves room for source",
          );
          checks++;
          await page.click("#btnExplorer");
          assert(
            (await page.getAttribute("#btnExplorer", "aria-expanded")) ===
              "true",
            "explorer can reopen",
          );
          checks++;
          assert(
            await page.isVisible("#deviceExplorer"),
            "real explorer visible",
          );
          checks++;
          await page.keyboard.press("Control+b");
          assert(
            (await page.getAttribute("#btnExplorer", "aria-expanded")) ===
              "false",
            "keyboard collapses explorer",
          );
          checks++;
        }
        assert(
          await page.evaluate((raw) => App.tab().text === raw, g.source),
          "layout toggling does not rewrite source",
        );
        checks++;
        await page.screenshot({
          path: OUT + `/${width}x${height}-${theme}.png`,
        });
        await page.close();
        console.log(`PASS editor layout ${width}x${height} ${theme}`);
      }
    const page = await browser.newPage({
      viewport: { width: 1400, height: 860 },
    });
    await page.route("https://**/*", (r) => r.abort());
    await page.goto("file://" + path.resolve("tools/studio/index.html"));
    await page.evaluate(() => {
      App.openTab({
        name: "persistent-layout.cs",
        text: 'Console.WriteLine(@"a\r\nβ");\r\n',
        saved: null,
        local: true,
      });
      window.beforeSource = App.tab().text;
    });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    // Persisted sizes clamp for geometry only; resizing must not overwrite desired desktop sizes.
    await page.evaluate(() => {
      store.set("side", 560);
      store.set("cons", 700);
      App.layout();
    });
    await page.setViewportSize({ width: 667, height: 375 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    assert(
      await page.evaluate(
        () =>
          Number(
            document.getElementById("splitSide").getAttribute("aria-valuenow"),
          ) <= 337 &&
          Number(
            document.getElementById("splitCons").getAttribute("aria-valuenow"),
          ) <= 149 &&
          store.get("side") === 560 &&
          store.get("cons") === 700,
      ),
      "rendered small-window bounds do not overwrite desktop preferences",
    );
    checks++;
    await page.setViewportSize({ width: 390, height: 844 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "false",
      "compact default independent of desktop widths",
    );
    checks++;
    await page.click("#btnExplorer");
    await page.setViewportSize({ width: 1400, height: 860 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "true",
      "desktop preference remains visible",
    );
    checks++;
    await page.keyboard.press("Control+b");
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "false",
      "desktop collapse persists separately",
    );
    checks++;
    await page.setViewportSize({ width: 390, height: 844 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "true",
      "explicit compact open preference restored",
    );
    checks++;
    await page.keyboard.press("Control+b");
    await page.setViewportSize({ width: 1400, height: 860 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "false",
      "compact action does not change desktop collapse",
    );
    checks++;
    await page.click("#btnExplorer");
    await page.locator("#splitSide").focus();
    await page.keyboard.press("Home");
    assert(
      (await page.getAttribute("#splitSide", "aria-valuenow")) === "200",
      "keyboard separator Home reaches minimum",
    );
    checks++;
    await page.keyboard.press("ArrowRight");
    assert(
      (await page.getAttribute("#splitSide", "aria-valuenow")) === "220",
      "keyboard separator resizes in bounded increments",
    );
    checks++;
    await page.keyboard.press("End");
    assert(
      (await page.getAttribute("#splitSide", "aria-valuenow")) === "560",
      "keyboard separator End reaches maximum",
    );
    checks++;
    await page.locator("#splitCons").focus();
    await page.keyboard.press("Home");
    assert(
      (await page.getAttribute("#splitCons", "aria-valuenow")) === "90",
      "console keyboard Home preserves output area",
    );
    checks++;
    await page.keyboard.press("ArrowUp");
    assert(
      (await page.getAttribute("#splitCons", "aria-valuenow")) === "110",
      "console keyboard Up enlarges output area",
    );
    checks++;
    await page.keyboard.press("End");
    assert(
      (await page.getAttribute("#splitCons", "aria-valuenow")) ===
        (await page.getAttribute("#splitCons", "aria-valuemax")),
      "console keyboard End respects current editor minimum",
    );
    checks++;
    const separator = await page.locator("#splitSide").boundingBox();
    await page.mouse.move(separator.x + 2, separator.y + 40);
    await page.mouse.down();
    await page.mouse.move(320, separator.y + 40, { steps: 5 });
    await page.mouse.up();
    assert(
      (await page.getAttribute("#splitSide", "aria-valuenow")) === "320" &&
        !(await page
          .locator("#splitSide")
          .evaluate((el) => el.classList.contains("drag"))),
      "real pointer drag stores size and releases capture/drag state",
    );
    checks++;
    await page.evaluate(() => {
      document.getElementById("btnRefresh").disabled = false;
      document.getElementById("btnRefresh").focus();
    });
    await page.keyboard.press("Control+b");
    assert(
      await page.evaluate(() => document.activeElement === ed.ta),
      "hiding focused explorer returns focus to source",
    );
    checks++;
    assert(
      await page.evaluate(() => App.tab().text === beforeSource),
      "resize/pointer/keyboard/focus changes leave canonical source untouched",
    );
    checks++;
    await page.evaluate(() => {
      store.set("side", {});
      store.set("cons", "bogus");
      App.layout();
    });
    assert(
      await page.evaluate(
        () =>
          Number.isFinite(
            Number(
              document
                .getElementById("splitSide")
                .getAttribute("aria-valuenow"),
            ),
          ) &&
          Number.isFinite(
            Number(
              document
                .getElementById("splitCons")
                .getAttribute("aria-valuenow"),
            ),
          ),
      ),
      "invalid persisted sizes use finite defaults",
    );
    checks++;
    await page.evaluate(() => {
      App.tab().dirty = false;
    });
    await page.reload();
    assert(
      (await page.getAttribute("#btnExplorer", "aria-expanded")) === "false",
      "desktop collapse survives actual page reload",
    );
    checks++;
    await page.close();
    const touch = await browser.newPage({
      viewport: { width: 667, height: 375 },
      hasTouch: true,
    });
    await touch.route("https://**/*", (r) => r.abort());
    await touch.goto("file://" + path.resolve("tools/studio/index.html"));
    const bar = await touch.locator("#splitSide").boundingBox(),
      cdp = await touch.context().newCDPSession(touch);
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ x: bar.x + 2, y: bar.y + 60, id: 1 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [{ x: 220, y: bar.y + 60, id: 1 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchEnd",
      touchPoints: [],
    });
    assert(
      (await touch.getAttribute("#splitSide", "aria-valuenow")) === "220" &&
        !(await touch
          .locator("#splitSide")
          .evaluate((el) => el.classList.contains("drag"))),
      "real Chromium touch pointer drag resizes and releases, no DOM event fiction",
    );
    checks++;
    const cancelBar = await touch.locator("#splitSide").boundingBox();
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ x: cancelBar.x + 2, y: cancelBar.y + 60, id: 2 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [{ x: 300, y: cancelBar.y + 60, id: 2 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchCancel",
      touchPoints: [],
    });
    assert(
      !(await touch
        .locator("#splitSide")
        .evaluate((el) => el.classList.contains("drag"))),
      "actual touch cancellation clears drag state",
    );
    checks++;
    await touch.close();
    console.log(
      `PASS ${checks} editor useful-area/overflow/toggle/source/pointer/touch/keyboard/persistence assertions`,
    );
  } finally {
    await browser.close();
  }
})()
  .then(() => process.exit(0))
  .catch((e) => {
    console.error(e);
    process.exit(1);
  });
