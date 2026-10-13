// Actual source-project confirmation, many long filenames, offline local browser.
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  ({ chromium } = require("playwright-core"));
}
const assert = require("assert"),
  fs = require("fs"),
  path = require("path");
const OUT = path.resolve("build/studio-project-layout");
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
    ])
      for (const theme of ["dark", "light"]) {
        const page = await browser.newPage({ viewport: { width, height } });
        await page.route("https://**/*", (r) => r.abort());
        await page.goto("file://" + path.resolve("tools/studio/index.html"));
        if (theme === "light") await page.click("#btnTheme");
        await page.evaluate(() => {
          App.tabs = [];
          App.active = null;
          ed.tab = null;
          for (let i = 0; i < 32; i++)
            App.openTab({ name: "source_member_".repeat(17) + i + ".cs", text: "", saved: null });
          App.createProject();
        });
        await page.waitForSelector("#modalBg.show");
        const g = await page.evaluate(() => {
          const m = document.getElementById("modal"),
            p = m.querySelector("p"),
            a = m.querySelector(".acts"),
            input = document.getElementById("dlgIn");
          const rect = (e) => {
            const r = e.getBoundingClientRect();
            return { left: r.left, right: r.right, top: r.top, bottom: r.bottom, height: r.height };
          };
          return {
            modal: rect(m),
            actions: rect(a),
            input: rect(input),
            text: rect(p),
            scrollWidth: p.scrollWidth,
            clientWidth: p.clientWidth,
          };
        });
        assert(
          g.modal.left >= 0 &&
            g.modal.right <= width &&
            g.modal.top >= 0 &&
            g.modal.bottom <= height,
          JSON.stringify({ width, height, theme, g }),
        );
        checks++;
        assert(
          g.actions.bottom <= height &&
            g.actions.top >= g.input.bottom &&
            g.input.top >= g.text.bottom,
          "visible input/actions do not overlap membership text",
        );
        checks++;
        assert(
          g.scrollWidth <= g.clientWidth + 1 && g.text.height >= 40,
          "long membership wraps with useful scrollable text",
        );
        checks++;
        await page.screenshot({ path: OUT + `/${width}x${height}-${theme}.png` });
        await page.press("#dlgIn", "Escape");
        assert(
          await page.evaluate(
            () =>
              App.project === null &&
              App.tabs.length === 32 &&
              !document.getElementById("modalBg").classList.contains("show"),
          ),
          "cancel keeps source tabs and creates no project",
        );
        checks++;
        console.log(`PASS source-project dialog ${width}x${height} ${theme}`);
        await page.close();
      }
    console.log(`PASS ${checks} project dialog geometry/cancellation assertions`);
  } finally {
    await browser.close();
  }
})()
  .then(() => process.exit(0))
  .catch((e) => {
    console.error(e);
    process.exit(1);
  });
