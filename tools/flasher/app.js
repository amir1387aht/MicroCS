import {
  assertChip,
  assertUnsecuredEsp,
  assetFor,
  verifyAsset,
  validateImage,
  writeUf2,
} from "./core.js";
import { Stm32Uart, Stm32Dfu } from "./stm32.js";
const $ = (id) => document.getElementById(id);
const methods = {
  esp: "USB / UART · ESP bootloader",
  uart: "UART · STM32 ROM bootloader",
  dfu: "USB · STM32 DFU",
  uf2: "USB · BOOTSEL drive",
};
let boards = [],
  catalog,
  firmware,
  currentAsset,
  busy = false,
  generation = 0,
  catalogGeneration = 0,
  refreshing = false;
const lines = [];
function log(message) {
  lines.push(`${new Date().toLocaleTimeString()}  ${message}`);
  if (lines.length > 300) lines.splice(0, lines.length - 300);
  $("log").textContent = lines.join("\n");
}
function progress(value, phase) {
  $("progress").value = value;
  $("progressText").textContent = `${phase} · ${Math.round(value)}%`;
}
function status(message, error = false) {
  $("status").textContent = message;
  $("status").classList.toggle("error", error);
}
function selectedBoard() {
  return boards.find((b) => b.id === $("board").value);
}
function release() {
  return catalog?.releases.find((r) => r.tag === $("release").value);
}
function capable() {
  if (!window.isSecureContext) return false;
  if (["esp", "uart"].includes($("method").value)) return !!navigator.serial;
  if ($("method").value === "dfu") return !!navigator.usb;
  return !!window.showDirectoryPicker;
}
function updateButton() {
  $("flash").disabled =
    busy || refreshing || !firmware || !$("consent").checked || !capable();
}
function updateInstructions() {
  const board = selectedBoard(),
    method = $("method").value;
  if (!board) return;
  $("instructions").textContent =
    method === "dfu"
      ? "USB DFU: connect the Black Pill’s USB port, hold BOOT0 high and reset. Windows may need WinUSB for the STM32 bootloader (0483:df11), not ST-LINK. USB DFU cannot uniquely identify the MCU: confirm this is an F411CE Black Pill. After verified flashing, set BOOT0 low and press RESET."
      : board.instructions.split(" USB DFU:")[0];
  $("consentText").textContent =
    "I’ve selected the exact board and backed up my files. Flashing can erase saved scripts, settings and the filesystem. " +
    (method === "uart" ? "STM32 UART erases the entire flash. " : "") +
    "Keep power connected.";
  $("verifyStep").textContent =
    method === "uf2" ? "Confirm the REPL in Studio" : "Verify installation";
}
function capability() {
  updateInstructions();
  const supported = capable();
  $("compat").classList.toggle("warning", !supported);
  $("compat").textContent = supported
    ? "Browser ready. You’ll choose a device or USB drive when you connect."
    : "This method needs desktop Chrome / Edge over HTTPS (or localhost). Use the firmware download and board instructions below instead.";
  $("flash").innerHTML =
    $("method").value === "uf2"
      ? 'Choose boot drive & install <span aria-hidden="true">→</span>'
      : 'Connect & install MicroCS <span aria-hidden="true">→</span>';
  updateButton();
}
function options(select, entries) {
  select.replaceChildren(
    ...entries.map(([value, label]) => new Option(label, value)),
  );
}
function chooseBoard() {
  const board = selectedBoard();
  options(
    $("method"),
    board.methods.map((id) => [id, methods[id]]),
  );
  $("consent").checked = false;
  capability();
  loadFirmware();
}
function chooseFamily() {
  options(
    $("board"),
    boards
      .filter((b) => b.family === $("family").value)
      .map((b) => [b.id, b.label]),
  );
  chooseBoard();
}
async function loadFirmware() {
  const run = ++generation;
  firmware = null;
  currentAsset = null;
  $("integrity").className = "integrity";
  updateButton();
  progress(0, "Preparing");
  const board = selectedBoard(),
    rel = release();
  if (!rel || !board) return;
  const asset = assetFor(rel, board);
  $("releaseLink").href = rel.url;
  $("integrity").textContent = "";
  $("filemeta").textContent = "";
  if (!asset) {
    $("filename").textContent = "No release image for this target";
    status("Firmware unavailable", true);
    $("integrity").classList.add("error");
    $("download").href = rel.url;
    $("integrity").textContent =
      "Choose another release. This target may not have been built in older releases.";
    return;
  }
  currentAsset = asset;
  $("filename").textContent = asset.name;
  $("filemeta").textContent = `${(asset.size / 1048576).toFixed(2)} MB`;
  $("download").href = asset.downloadUrl;
  status("Fetching firmware");
  $("integrity").textContent = "Downloading official release image…";
  try {
    const url = new URL(asset.path, location.href);
    if (
      url.origin !== location.origin ||
      !url.pathname.startsWith(new URL("./firmware/", location.href).pathname)
    )
      throw new Error("Unsafe firmware path in catalog.");
    const res = await fetch(url, { cache: "no-cache" });
    if (!res.ok)
      throw new Error(`Firmware download failed (HTTP ${res.status}).`);
    const bytes = await verifyAsset(await res.arrayBuffer(), asset);
    validateImage(bytes, board);
    if (run !== generation) return;
    firmware = bytes;
    $("integrity").className = "integrity verified";
    $("integrity").textContent = "✓ SHA-256 checked · official release image";
    status("Ready to install");
    $("progressText").textContent = "Firmware checked · no device connected";
    log(`Ready: ${asset.name}; SHA-256 ${asset.sha256}`);
  } catch (e) {
    if (run !== generation) return;
    status("Could not prepare firmware", true);
    $("integrity").className = "integrity error";
    $("integrity").textContent = e.message;
    log(e.message);
  }
  updateButton();
}
function syncControls() {
  for (const id of ["family", "board", "release", "method", "consent", "retry"])
    $(id).disabled = busy || refreshing;
  $("release").disabled ||= !catalog?.releases?.length;
  updateButton();
}
async function refresh() {
  if (busy) return;
  const epoch = ++catalogGeneration,
    old = $("release").value;
  refreshing = true;
  ++generation;
  catalog = null;
  firmware = null;
  currentAsset = null;
  syncControls();
  status("Fetching releases");
  $("integrity").className = "integrity";
  $("integrity").textContent = "Loading the official release catalog…";
  try {
    const res = await fetch("firmware/catalog.json", { cache: "no-store" });
    if (!res.ok)
      throw new Error(`Release catalog unavailable (HTTP ${res.status}).`);
    const next = await res.json();
    if (epoch !== catalogGeneration) return;
    if (next.schema !== 1 || !next.releases?.length)
      throw new Error("No published firmware catalog.");
    catalog = next;
    options(
      $("release"),
      catalog.releases.map((r, i) => [
        r.tag,
        `${r.tag}${i === 0 ? " · latest stable" : ""}`,
      ]),
    );
    if (catalog.releases.some((r) => r.tag === old)) $("release").value = old;
    await loadFirmware();
  } catch (e) {
    if (epoch !== catalogGeneration) return;
    catalog = null;
    status("Releases unavailable", true);
    $("integrity").className = "integrity error";
    $("progressText").textContent = "No verified firmware ready";
    $("integrity").textContent =
      `${e.message} Use the download fallback, or refresh after Pages has deployed.`;
    log(e.message);
  } finally {
    if (epoch === catalogGeneration) {
      refreshing = false;
      syncControls();
    }
  }
}
function lock(value) {
  busy = value;
  document.body.classList.toggle("busy", value);
  syncControls();
}
$("flash").addEventListener("click", async () => {
  if (busy || refreshing || !firmware || !$("consent").checked || !capable())
    return;
  const board = selectedBoard(),
    bytes = firmware,
    asset = currentAsset,
    method = $("method").value;
  let cleanup = async () => {};
  lock(true);
  progress(0, "Connecting");
  status("Choose your device");
  try {
    // Call permission APIs immediately in the user gesture, before imports/network work.
    if (method === "uf2") {
      const dir = await window.showDirectoryPicker({ mode: "readwrite" });
      status("Copying to BOOTSEL drive");
      await writeUf2(dir, bytes, board, asset.name);
      progress(100, "Copy complete");
      status("Firmware copied");
      log(
        "USB drive write completed. The board should reboot; confirm its MicroCS REPL in Studio. UF2 cannot provide browser readback verification.",
      );
    } else if (method === "esp") {
      const port = await navigator.serial.requestPort();
      const { ESPLoader, Transport, md5 } = await import("./vendor.js");
      const transport = new Transport(port, true);
      cleanup = () => transport.disconnect();
      const loader = new ESPLoader({
        transport,
        baudrate: 115200,
        terminal: { clean() {}, write: log, writeLine: log },
        debugLogging: false,
      });
      status("Detecting ESP chip");
      await loader.main();
      assertChip(loader.chip.CHIP_NAME, board.id);
      await assertUnsecuredEsp(loader, board);
      const flashSize = await loader.detectFlashSize();
      if (
        !flashSize ||
        loader.flashSizeBytes(flashSize) <
          Math.max(board.capacity, Math.ceil(bytes.length / 4) * 4)
      )
        throw new Error(
          "Physical flash capacity is smaller than this firmware requires. Nothing was erased.",
        );
      status("Installing firmware");
      await loader.writeFlash({
        fileArray: [{ data: bytes, address: 0 }],
        flashSize: "keep",
        flashMode: "keep",
        flashFreq: "keep",
        eraseAll: false,
        compress: true,
        calculateMD5Hash: md5,
        reportProgress: (_file, written, total) =>
          progress((90 * written) / total, "Writing"),
      });
      log("Flash MD5 verified by esptool. Resetting the board.");
      progress(100, "Verified");
      try {
        await loader.after("hard_reset");
        status("MicroCS installed");
      } catch (e) {
        log(
          `Firmware verified, but automatic reset failed: ${e.message}. Press RESET manually.`,
        );
        status("Verified · reset your board");
      }
    } else {
      const device =
        method === "uart"
          ? await navigator.serial.requestPort()
          : await navigator.usb.requestDevice({
              filters: [{ vendorId: 0x0483, productId: 0xdf11 }],
            });
      const adapter =
        method === "uart"
          ? new Stm32Uart(device, log)
          : new Stm32Dfu(device, log);
      cleanup = () => adapter.close();
      status("Checking STM32 bootloader");
      await adapter.connect(board);
      status("Installing firmware");
      await adapter.flash(bytes, progress);
      status("Verified · reset your board");
    }
  } catch (e) {
    const cancelled = e.name === "NotFoundError" || e.name === "AbortError";
    status(
      cancelled ? "Connection cancelled" : "Installation stopped",
      !cancelled,
    );
    log(`${e.name}: ${e.message}`);
    $("progressText").textContent = cancelled
      ? "No device selected. You can try again."
      : `${e.message} Fix the connection and retry. If writing started, re-enter the bootloader before retrying.`;
    $("log").closest("details").open = true;
  } finally {
    await cleanup().catch((e) => log(`Disconnect: ${e.message}`));
    lock(false);
  }
});
$("family").addEventListener("change", chooseFamily);
$("board").addEventListener("change", chooseBoard);
$("method").addEventListener("change", () => {
  $("consent").checked = false;
  capability();
});
$("release").addEventListener("change", () => {
  $("consent").checked = false;
  loadFirmware();
});
$("consent").addEventListener("change", updateButton);
$("retry").addEventListener("click", refresh);
$("saveLog").addEventListener("click", () => {
  const url = URL.createObjectURL(
    new Blob([lines.join("\n")], { type: "text/plain" }),
  );
  const a = document.createElement("a");
  a.href = url;
  a.download = "microcs-flasher.log";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});
window.addEventListener("beforeunload", (e) => {
  if (busy) {
    e.preventDefault();
    e.returnValue = "";
  }
});
try {
  const res = await fetch("boards.json");
  if (!res.ok) throw new Error("Board catalog could not be loaded.");
  boards = await res.json();
  chooseFamily();
  await refresh();
} catch (e) {
  status("Flasher unavailable", true);
  log(e.message);
  $("integrity").textContent = e.message;
}
