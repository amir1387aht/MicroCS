// Real status component layout with long labels. States are geometry-only,
// not device connection/IO acknowledgement; serial behavior has separate tests.
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  ({ chromium } = require("playwright-core"));
}
const assert = require("assert"),
  fs = require("fs"),
  path = require("path");
const OUT = path.resolve("build/studio-status-layout");
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
          App.openTab({
            name: "source_member_".repeat(17) + "31.cs",
            text: "",
            saved: null,
            local: true,
          });
        });
        await page.evaluate(() => new Promise(requestAnimationFrame));
        for (const state of ["off", "on", "busy"]) {
          await page.evaluate((state) => {
            App.setConn(
              state,
              state === "busy"
                ? "Waiting for the device…"
                : "MicroCS 1.14.0 · " + "board-label-".repeat(20),
            );
            document.getElementById("sbBusy").textContent =
              state === "off" ? "Ready" : "● working…";
            document.getElementById("sbPos").textContent =
              "Ln 123456, Col 654321";
          }, state);
          const g = await page.evaluate(() => {
            const rect = (id) => {
              const e = document.getElementById(id),
                r = e.getBoundingClientRect();
              return {
                left: r.left,
                right: r.right,
                width: r.width,
                text: e.textContent,
                title: e.title,
              };
            };
            return {
              conn: rect("sbConn"),
              file: rect("sbFile"),
              busy: rect("sbBusy"),
              pos: rect("sbPos"),
            };
          });
          for (const key of ["conn", "busy", "pos"]) {
            assert(
              g[key].left >= 0 && g[key].right <= width && g[key].width > 0,
              JSON.stringify({ width, state, key, g }),
            );
            checks++;
          }
          assert(
            g.conn.right <= g.file.left &&
              g.file.right <= g.busy.left &&
              g.busy.right <= g.pos.left,
            "status fields do not overlap",
          );
          checks++;
          assert(
            g.file.width > 0 &&
              g.file.title === g.file.text &&
              g.conn.title === g.conn.text,
            "clipped labels retain exact tooltip identity",
          );
          checks++;
        }
        await page.screenshot({
          path: OUT + `/${width}x${height}-${theme}.png`,
        });
        await page.close();
        console.log(`PASS status layout ${width}x${height} ${theme}`);
      }
    console.log(`PASS ${checks} status geometry/identity assertions`);
  } finally {
    await browser.close();
  }
})()
  .then(() => process.exit(0))
  .catch((e) => {
    console.error(e);
    process.exit(1);
  });
