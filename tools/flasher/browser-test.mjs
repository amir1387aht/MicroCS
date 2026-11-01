// No hardware or network firmware required. Generated fixtures are NOT bootable images.
import { chromium } from "playwright";
import { createServer } from "node:http";
import { readFile, mkdir } from "node:fs/promises";
import { createHash } from "node:crypto";
import assert from "node:assert/strict";
const root = new URL("./", import.meta.url);
const boards = JSON.parse(await readFile(new URL("boards.json", root)));
const images = new Map();
for (const board of boards) {
  const bytes = new Uint8Array(
      board.family === "ESP32" ? board.bootOffset + 32 : 512,
    ),
    v = new DataView(bytes.buffer);
  if (board.family === "ESP32") {
    bytes[board.bootOffset] = 0xe9;
    v.setUint16(board.bootOffset + 12, board.imageChipId, true);
  } else if (board.family === "STM32") {
    v.setUint32(0, 0x20010000, true);
    v.setUint32(4, 0x08000101, true);
  } else
    for (const [o, n] of [
      [0, 0x0a324655],
      [4, 0x9e5d5157],
      [8, 0x2000],
      [12, 0x10000000],
      [16, 256],
      [20, 0],
      [24, 1],
      [28, board.chip === "RP2040" ? 0xe48bff56 : 0xe48bff59],
      [508, 0x0ab16f30],
    ])
      v.setUint32(o, n, true);
  images.set(
    `firmware/v1.0.0/microcs-1.0.0-${board.id}.${board.extension}`,
    bytes,
  );
}
const assets = [...images].map(([path, bytes]) => ({
  board: boards.find((b) => path.endsWith(`-${b.id}.${b.extension}`)).id,
  path,
  name: path.split("/").pop(),
  size: bytes.length,
  sha256: createHash("sha256").update(bytes).digest("hex"),
  downloadUrl: "https://github.com/amir1387aht/MicroCS/releases/latest",
}));
const catalog = {
  schema: 1,
  releases: [
    {
      tag: "v1.0.0",
      url: "https://github.com/amir1387aht/MicroCS/releases",
      assets,
    },
    {
      tag: "v0.9.0",
      url: "https://github.com/amir1387aht/MicroCS/releases",
      assets: [],
    },
  ],
};
const server = createServer(async (req, res) => {
  try {
    const path =
      new URL(req.url, "http://localhost").pathname.replace(/^\//, "") ||
      "index.html";
    if (path === "firmware/catalog.json") {
      res.setHeader("Content-Type", "application/json");
      return res.end(JSON.stringify(catalog));
    }
    if (images.has(path)) {
      res.setHeader("Content-Type", "application/octet-stream");
      return res.end(images.get(path));
    }
    if (!/^[a-zA-Z0-9-]+\.(html|js|css|json)$/.test(path)) {
      res.writeHead(404);
      return res.end();
    }
    res.setHeader(
      "Content-Type",
      path.endsWith(".js")
        ? "text/javascript"
        : path.endsWith(".css")
          ? "text/css"
          : path.endsWith(".json")
            ? "application/json"
            : "text/html",
    );
    res.end(await readFile(new URL(path, root)));
  } catch {
    res.writeHead(404);
    res.end();
  }
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const browser = await chromium.launch({
  headless: true,
  ...(process.env.BROWSER_EXECUTABLE_PATH
    ? { executablePath: process.env.BROWSER_EXECUTABLE_PATH }
    : {}),
  args: ["--no-sandbox"],
});
try {
  const context = await browser.newContext({
    viewport: { width: 1440, height: 1200 },
    colorScheme: "light",
  });
  const page = await context.newPage(),
    errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  const shots = process.env.SCREENSHOT_DIR;
  if (shots) await mkdir(shots, { recursive: true });
  const capture = async (name) => {
    if (shots)
      await page.screenshot({ path: `${shots}/${name}.png`, fullPage: true });
  };
  const ready = () =>
    page.getByText("Ready to install", { exact: true }).waitFor();
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await ready();
  const bodyBackground = () =>
    page.evaluate(() => getComputedStyle(document.body).backgroundColor);
  const assertPalette = async () => {
    const pairs = await page.evaluate(() => {
      const color = (sel, property) =>
        getComputedStyle(document.querySelector(sel))[property];
      return [
        [
          "body text",
          color("body", "color"),
          color("body", "backgroundColor"),
          4.5,
        ],
        [
          "secondary text",
          color(".muted", "color"),
          color("body", "backgroundColor"),
          4.5,
        ],
        [
          "links",
          color("nav a", "color"),
          color("body", "backgroundColor"),
          4.5,
        ],
        [
          "install button",
          color(".primary", "color"),
          color(".primary", "backgroundColor"),
          4.5,
        ],
        [
          "success message",
          color("#integrity", "color"),
          color(".panel", "backgroundColor"),
          4.5,
        ],
        [
          "input boundary",
          color("#board", "borderTopColor"),
          color("#board", "backgroundColor"),
          3,
        ],
      ];
    });
    const luminance = (color) => {
      const values = color
        .match(/[0-9.]+/g)
        .slice(0, 3)
        .map(Number)
        .map((n) => {
          const c = n / 255;
          return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
        });
      return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722;
    };
    for (const [name, fg, bg, min] of pairs) {
      const a = luminance(fg),
        b = luminance(bg),
        ratio = (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
      assert.ok(ratio >= min, `${name}: contrast ${ratio.toFixed(2)} < ${min}`);
    }
  };
  assert.equal(await page.getByLabel("Appearance").inputValue(), "system");
  assert.equal(await bodyBackground(), "rgb(255, 255, 255)");
  await assertPalette();
  await page.emulateMedia({ colorScheme: "dark" });
  assert.equal(await bodyBackground(), "rgb(25, 25, 25)");
  await assertPalette();
  await page.emulateMedia({ colorScheme: "light" });
  await page.getByLabel("Appearance").selectOption("dark");
  assert.equal(await bodyBackground(), "rgb(25, 25, 25)");
  await assertPalette();
  await capture("manual-dark-desktop");
  await page.reload();
  await ready();
  assert.equal(await page.getByLabel("Appearance").inputValue(), "dark");
  assert.equal(await bodyBackground(), "rgb(25, 25, 25)");
  await page.emulateMedia({ colorScheme: "dark" });
  await page.getByLabel("Appearance").selectOption("light");
  assert.equal(await bodyBackground(), "rgb(255, 255, 255)");
  await assertPalette();
  await page.reload();
  await ready();
  assert.equal(await page.getByLabel("Appearance").inputValue(), "light");
  assert.equal(await bodyBackground(), "rgb(255, 255, 255)");
  await page.getByLabel("Appearance").selectOption("system");
  assert.equal(await bodyBackground(), "rgb(25, 25, 25)");
  await page.emulateMedia({ colorScheme: "light" });
  assert.equal(await bodyBackground(), "rgb(255, 255, 255)");
  await page.evaluate(() =>
    localStorage.setItem("microcs-flasher-theme", "invalid-value"),
  );
  await page.reload();
  await ready();
  assert.equal(await page.getByLabel("Appearance").inputValue(), "system");
  const storageBlocked = await browser.newPage({ colorScheme: "light" });
  await storageBlocked.addInitScript(() =>
    Object.defineProperty(window, "localStorage", {
      get() {
        throw new DOMException("Storage blocked", "SecurityError");
      },
    }),
  );
  const blockedErrors = [];
  storageBlocked.on("pageerror", (e) => blockedErrors.push(e.message));
  await storageBlocked.goto(page.url());
  await storageBlocked.getByText("Ready to install", { exact: true }).waitFor();
  await storageBlocked.getByLabel("Appearance").selectOption("dark");
  assert.equal(
    await storageBlocked.evaluate(
      () => getComputedStyle(document.body).backgroundColor,
    ),
    "rgb(25, 25, 25)",
  );
  assert.deepEqual(blockedErrors, []);
  await storageBlocked.close();
  const startupFailure = await browser.newPage({ colorScheme: "light" });
  const startupErrors = [];
  startupFailure.on("pageerror", (e) => startupErrors.push(e.message));
  await startupFailure.route("**/boards.json", (route) =>
    route.fulfill({ status: 503, body: "Offline" }),
  );
  await startupFailure.goto(page.url());
  await startupFailure
    .getByText("Releases unavailable", { exact: true })
    .waitFor();
  assert.equal(await startupFailure.locator("#family").isEnabled(), false);
  assert.equal(await startupFailure.locator("#flash").isEnabled(), false);
  await startupFailure.locator("#family").dispatchEvent("change");
  await startupFailure.unroute("**/boards.json");
  await startupFailure.route("**/boards.json", (route) =>
    route.fulfill({ contentType: "application/json", body: "[null]" }),
  );
  await startupFailure.locator("#retry").click();
  await startupFailure.waitForFunction(() =>
    document
      .getElementById("integrity")
      .textContent.includes("Invalid board catalog"),
  );
  await startupFailure.unroute("**/boards.json");
  await startupFailure.locator("#retry").click();
  await startupFailure.getByText("Ready to install", { exact: true }).waitFor();
  assert.equal(await startupFailure.locator("#family").isEnabled(), true);
  assert.deepEqual(startupErrors, []);
  await startupFailure.route("**/firmware/catalog.json", (route) =>
    route.fulfill({
      contentType: "application/json",
      body: JSON.stringify({
        schema: 1,
        releases: [
          {
            tag: "v1.0.0",
            url: "https://github.com/amir1387aht/MicroCS/releases",
            assets: null,
          },
        ],
      }),
    }),
  );
  await startupFailure.locator("#retry").click();
  await startupFailure.waitForFunction(() =>
    document
      .getElementById("integrity")
      .textContent.includes("Invalid or empty published firmware catalog"),
  );
  assert.equal(await startupFailure.locator("#flash").isEnabled(), false);
  await startupFailure.unroute("**/firmware/catalog.json");
  await startupFailure.locator("#retry").click();
  await startupFailure.getByText("Ready to install", { exact: true }).waitFor();
  assert.deepEqual(startupErrors, []);
  await startupFailure.close();
  const sibling = await page.context().newPage();
  await sibling.goto(page.url());
  await sibling.getByText("Ready to install", { exact: true }).waitFor();
  await page.getByLabel("Appearance").selectOption("dark");
  await sibling.waitForFunction(
    () => document.documentElement.dataset.theme === "dark",
  );
  await sibling.getByLabel("Appearance").selectOption("light");
  await page.waitForFunction(
    () => document.documentElement.dataset.theme === "light",
  );
  await page.getByLabel("Appearance").selectOption("system");
  await sibling.close();

  await capture("desktop");
  for (const family of ["ESP32", "RP2", "STM32"]) {
    await page.selectOption("#family", family);
    const ids = await page
      .locator("#board option")
      .evaluateAll((a) => a.map((n) => n.value));
    for (const id of ids) {
      await page.selectOption("#board", id);
      await ready();
      assert.equal(
        await page.locator("#flash").isEnabled(),
        false,
        "consent must be required",
      );
      await page.locator("#consent").check();
      assert.equal(
        await page.locator("#flash").isEnabled(),
        true,
        `missing capability: ${id}`,
      );
    }
  }
  await page.selectOption("#board", "blackpill_f411ce");
  await ready();
  await page.selectOption("#method", "dfu");
  await capture("stm32-dfu");
  await page.selectOption("#family", "RP2");
  await ready();
  await capture("pico");
  await page.evaluate(
    () =>
      (window.showDirectoryPicker = async () => ({
        async getFileHandle(name, opts) {
          if (opts)
            return {
              async createWritable() {
                return {
                  async write() {
                    window.fixtureWritten = true;
                  },
                  async close() {},
                  async abort() {},
                };
              },
            };
          return {
            async getFile() {
              return {
                async text() {
                  return "Board-ID: RPI-RP2";
                },
              };
            },
          };
        },
      })),
  );
  await page.locator("#consent").check();
  await page.locator("#flash").click();
  await page.getByText("Firmware copied", { exact: true }).waitFor();
  assert.equal(await page.evaluate(() => window.fixtureWritten), true);
  await page.selectOption("#family", "ESP32");
  await ready();
  await page.locator("#consent").check();
  await page.evaluate(() =>
    Object.defineProperty(navigator, "serial", {
      value: {
        requestPort: async () => {
          await new Promise((resolve) => (window.cancelPermission = resolve));
          throw new DOMException("No port selected", "NotFoundError");
        },
      },
      configurable: true,
    }),
  );
  await page.locator("#flash").click();
  await page.waitForFunction(
    () => typeof window.cancelPermission === "function",
  );
  assert.equal(await page.locator("#retry").isEnabled(), false);
  await page.locator("#retry").dispatchEvent("click");
  assert.equal(
    await page.locator("#release").isEnabled(),
    false,
    "busy install must stay locked",
  );
  await page.evaluate(() => window.cancelPermission());
  await page.getByText("Connection cancelled", { exact: true }).waitFor();
  assert.equal(await page.locator("#family").isEnabled(), true);
  // Verify the integration keeps a successful flash when automatic reset fails.
  await page.route("**/vendor.js", (route) =>
    route.fulfill({
      contentType: "text/javascript",
      body: `
    export class Transport { constructor(port){} async disconnect(){window.fixtureDisconnected=true;} }
    export const md5=()=>"fixture-md5";
    export class ESPLoader {
      constructor(){this.chip={CHIP_NAME:"ESP32"};}
      async main(){} async detectFlashSize(){return window.fixtureSmallFlash?"2MB":"4MB";} flashSizeBytes(size){return size==="2MB"?2097152:4194304;}
      async readReg(address){return window.fixtureSecured && address===0x3ff5a018?0x10:0;}
      async getSecurityInfo(){return {flags:0,flashCryptCnt:0,parsedFlags:{SECURE_BOOT_EN:false,SECURE_DOWNLOAD_ENABLE:false}};}
      async writeFlash(options){if(options.fileArray[0].address!==0)throw Error("Wrong flash offset");if(!options.calculateMD5Hash)throw Error("No verification");options.reportProgress(0,10,10);window.fixtureEspWritten=true;window.fixtureEspWriteCount=(window.fixtureEspWriteCount||0)+1;}
      async after(){throw Error("USB reset disconnected");}
    }
  `,
    }),
  );
  await page.evaluate(() =>
    Object.defineProperty(navigator, "serial", {
      value: { requestPort: async () => ({}) },
      configurable: true,
    }),
  );
  await page.locator("#flash").click();
  await page
    .getByText("Verified · reset your board", { exact: true })
    .waitFor();
  assert.equal(
    await page.evaluate(
      () => window.fixtureEspWritten && window.fixtureDisconnected,
    ),
    true,
  );
  await page.evaluate(() => (window.fixtureSecured = true));
  await page.locator("#flash").click();
  await page.getByText("Installation stopped", { exact: true }).waitFor();
  assert.equal(
    await page.evaluate(() => window.fixtureEspWriteCount),
    1,
    "secured ESP must not reach writeFlash",
  );
  assert.match(
    await page.locator("#progressText").textContent(),
    /secure|encryption/i,
  );
  await page.evaluate(() => (window.fixtureSecured = false));
  await page.evaluate(() => (window.fixtureSmallFlash = true));
  await page.locator("#flash").click();
  await page.waitForFunction(() =>
    document
      .getElementById("progressText")
      .textContent.includes("Physical flash capacity"),
  );
  assert.equal(
    await page.evaluate(() => window.fixtureEspWriteCount),
    1,
    "a2MiB chip cannot receive a4MiB firmware layout",
  );
  await page.evaluate(() => (window.fixtureSmallFlash = false));

  await page.unroute("**/vendor.js");
  await page.setViewportSize({ width: 390, height: 844 });
  await capture("mobile");
  assert.equal(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
    false,
    "horizontal overflow",
  );
  await page.emulateMedia({ colorScheme: "dark" });
  await capture("dark");
  await page.selectOption("#release", "v0.9.0");
  await page.getByText("Firmware unavailable", { exact: true }).waitFor();
  assert.equal(await page.locator("#flash").isEnabled(), false);
  await capture("missing-target");
  await page.selectOption("#release", "v1.0.0");
  await ready();
  let heldCatalog, catalogRequested;
  const requestSeen = new Promise((resolve) => (catalogRequested = resolve));
  await page.route("**/firmware/catalog.json", (route) => {
    heldCatalog = route;
    catalogRequested();
  });
  await page.locator("#retry").click();
  await requestSeen;
  for (const id of [
    "family",
    "board",
    "release",
    "method",
    "consent",
    "retry",
    "flash",
  ])
    assert.equal(
      await page.locator(`#${id}`).isEnabled(),
      false,
      `refresh must lock ${id}`,
    );
  // Even a programmatic change cannot prepare an image from the old catalog.
  await page.locator("#board").dispatchEvent("change");
  assert.equal(await page.locator("#flash").isEnabled(), false);
  await heldCatalog.fulfill({ status: 503, body: "Unavailable" });
  await page.getByText("Releases unavailable", { exact: true }).waitFor();
  await capture("error");
  assert.equal(await page.locator("#flash").isEnabled(), false);
  await page.locator("#board").dispatchEvent("change");
  assert.equal(
    await page.locator("#flash").isEnabled(),
    false,
    "failed catalog must not revive an old image",
  );
  assert.equal(await page.locator("#release").isEnabled(), false);
  await page.unroute("**/firmware/catalog.json");
  await page.locator("#retry").click();
  await ready();
  await page.route("**/firmware/v1.0.0/*esp32.bin", (route) =>
    route.fulfill({
      body: Buffer.alloc(assets.find((a) => a.board === "esp32").size),
    }),
  );
  await page.locator("#retry").click();
  await page.getByText("Could not prepare firmware", { exact: true }).waitFor();
  assert.match(
    await page.locator("#integrity").textContent(),
    /checksum mismatch/,
  );
  assert.equal(await page.locator("#flash").isEnabled(), false);
  await page.unroute("**/firmware/v1.0.0/*esp32.bin");
  await page.locator("#retry").click();
  await ready();
  await page.evaluate(() =>
    Object.defineProperty(navigator, "serial", {
      value: undefined,
      configurable: true,
    }),
  );
  await page.selectOption("#board", "esp32s3");
  await ready();
  await page.locator("#consent").check();
  assert.equal(await page.locator("#flash").isEnabled(), false);
  await capture("unsupported");
  assert.deepEqual(errors, []);
  console.log(
    "Browser UI passed: 22 targets, consent, UF2 copy, cancel/unlock, mobile, dark, missing target, catalog refresh races, catalog recovery, checksum mismatch, reset fallback, unsupported browser, persisted/cross-tab theme overrides, blocked storage, board-catalog startup recovery, and AA palette contrast.",
  );
} finally {
  await browser.close();
  await new Promise((r) => server.close(r));
}
