// Pure validation helpers shared by UI and regression tests.
export const hex = (n) => `0x${n.toString(16).padStart(8, "0")}`;
export const chipKey = (name) => name.toLowerCase().replace(/[^a-z0-9]/g, "");
export function assertChip(actual, expected) {
  if (chipKey(actual) !== expected)
    throw new Error(
      `Wrong MCU: detected ${actual}; selected ${expected}. Nothing was erased.`,
    );
}
// Public release images are plaintext and unsigned. Never overwrite a device
// whose security configuration needs encrypted/signed installation workflows.
// This helper only reads state; it never burns fuses or disables protection.
export async function assertUnsecuredEsp(loader, board) {
  assertChip(loader.chip.CHIP_NAME, board.id);
  if (loader.secureDownloadMode)
    throw new Error("Secure download mode is unsupported. Nothing was erased.");
  let secureBoot, secureDownload, cryptCount;
  try {
    const unsigned = (value) =>
      Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
    if (board.id === "esp32") {
      // ESP32 has no GET_SECURITY_INFO. Read its BLOCK0 read-only registers:
      // FLASH_CRYPT_CNT bits 20..26, ABS_DONE_0/ABS_DONE_1 bits 4/5.
      // https://github.com/espressif/esptool/blob/master/esptool/targets/esp32.py
      const word0 = await loader.readReg(0x3ff5a000);
      const word6 = await loader.readReg(0x3ff5a018);
      // readReg uses JS bitwise decoding and can return signed int32 values.
      const register32 = (value) =>
        Number.isInteger(value) && value >= -0x80000000 && value <= 0xffffffff;
      if (!register32(word0) || !register32(word6))
        throw new Error("Invalid eFuse response");
      cryptCount = (word0 >>> 20) & 0x7f;
      secureBoot = !!(word6 & 0x30);
      secureDownload = false;
    } else {
      // esptool-js 0.7.0 caches the ROM response during identifyChip(),
      // including S2's 12-byte fallback. main()/runStub() preserve that cache
      // at this app's unchanged 115200 baud. Do not force a stub-only query.
      const info = await loader.getSecurityInfo();
      if (
        !info ||
        !unsigned(info.flags) ||
        !Number.isInteger(info.flashCryptCnt) ||
        info.flashCryptCnt < 0 ||
        info.flashCryptCnt > 255
      )
        throw new Error("Invalid security-info response");
      secureBoot = !!(info.flags & 1);
      secureDownload = !!(info.flags & 4);
      cryptCount = info.flashCryptCnt;
    }
  } catch (error) {
    throw new Error(
      `Cannot verify ESP security configuration. Nothing was erased. ${error.message}`,
    );
  }
  // FLASH_CRYPT_CNT enables encryption when an odd number of bits is burned.
  let parity = 0;
  for (let n = cryptCount; n; n >>>= 1) parity ^= n & 1;
  if (secureBoot || secureDownload || parity)
    throw new Error(
      "Secure Boot, flash encryption or secure download mode is enabled. Public firmware cannot be installed safely. Nothing was erased.",
    );
}
export function assetFor(release, board) {
  return release.assets.find((a) => a.board === board.id);
}
export async function verifyAsset(buffer, asset) {
  if (buffer.byteLength !== asset.size || !asset.size)
    throw new Error("Firmware size mismatch. Reload the release catalog.");
  const hash = [
    ...new Uint8Array(await crypto.subtle.digest("SHA-256", buffer)),
  ]
    .map((x) => x.toString(16).padStart(2, "0"))
    .join("");
  if (hash !== asset.sha256)
    throw new Error("Firmware checksum mismatch. Nothing was erased.");
  return new Uint8Array(buffer);
}
export function validateImage(bytes, board) {
  if (board.family === "ESP32") {
    // Offsets from esptool target definitions; merged release images still flash at 0.
    const offset = board.bootOffset;
    if (bytes.length < offset + 24 || bytes[offset] !== 0xe9)
      throw new Error("Invalid merged ESP firmware image.");
    if (
      new DataView(bytes.buffer, bytes.byteOffset).getUint16(
        offset + 12,
        true,
      ) !== board.imageChipId
    )
      throw new Error("ESP firmware header does not match the selected chip.");
  } else if (board.family === "STM32") {
    if (bytes.length < 8 || bytes.length > board.capacity)
      throw new Error("Firmware does not fit the selected MCU.");
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const sp = v.getUint32(0, true),
      pc = v.getUint32(4, true);
    if (
      sp <= 0x20000000 ||
      sp % 8 !== 0 ||
      sp > 0x30080000 ||
      !(pc & 1) ||
      pc < 0x08000000 ||
      pc >= 0x08000000 + bytes.length
    )
      throw new Error("Invalid STM32 vector table.");
  } else {
    if (!bytes.length || bytes.length % 512)
      throw new Error("Invalid UF2 length.");
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const family =
      board.chip === "RP2040"
        ? [0xe48bff56]
        : [0xe48bff57, 0xe48bff59, 0xe48bff5a, 0xe48bff5b, 0xe48bff5c];
    const groups = new Map();
    let ignoredAbsolute = false;
    for (let i = 0; i < bytes.length; i += 512) {
      if (
        v.getUint32(i, true) !== 0x0a324655 ||
        v.getUint32(i + 4, true) !== 0x9e5d5157 ||
        v.getUint32(i + 508, true) !== 0x0ab16f30 ||
        !(v.getUint32(i + 8, true) & 0x2000) ||
        !family.includes(v.getUint32(i + 28, true))
      )
        throw new Error(
          "UF2 family or block signature does not match the selected MCU.",
        );
      const flags = v.getUint32(i + 8, true),
        address = v.getUint32(i + 12, true),
        payload = v.getUint32(i + 16, true),
        number = v.getUint32(i + 20, true),
        count = v.getUint32(i + 24, true),
        id = v.getUint32(i + 28, true);
      // RP2 ROM flash blocks use 256-byte pages. Refuse file containers,
      // non-flash blocks and unsupported flags rather than silently skipping them.
      if (
        (flags & ~0xa000) !== 0 ||
        payload !== 256 ||
        address % 256 ||
        address < 0x10000000 ||
        address + payload > 0x11000000 ||
        !count ||
        number >= count
      )
        throw new Error("Invalid UF2 flash block layout.");
      // picotool prefixes RP2350 flash images with an intentionally incomplete
      // absolute-family stream for erratum RP2350-E10. It is NOT a two-block
      // firmware image. Only accept the precise ignored-block form it emits.
      // https://github.com/raspberrypi/picotool/blob/master/elf2uf2/elf2uf2.cpp
      if (
        board.chip === "RP2350" &&
        id === 0xe48bff57 &&
        flags === 0xa000 &&
        number === 0 &&
        count === 2 &&
        bytes.subarray(i + 32, i + 288).every((b) => b === 0xef) &&
        v.getUint32(i + 288, true) === 0x9957e304
      ) {
        if (ignoredAbsolute) throw new Error("Duplicate UF2 RP2350-E10 block.");
        ignoredAbsolute = true;
        continue;
      }
      let group = groups.get(id);
      if (!group) {
        group = { count, numbers: new Set(), addresses: new Set() };
        groups.set(id, group);
      }
      if (
        count > bytes.length / 512 ||
        count !== group.count ||
        group.numbers.has(number) ||
        group.addresses.has(address)
      )
        throw new Error(
          "Inconsistent or duplicate UF2 block numbering/address.",
        );
      group.numbers.add(number);
      group.addresses.add(address);
    }
    if (
      !groups.size ||
      [...groups.values()].some((g) => g.numbers.size !== g.count)
    )
      throw new Error("Incomplete UF2 firmware image.");
  }
}
export const checksum = (bytes) => bytes.reduce((a, b) => a ^ b, 0);
export function addressPacket(address) {
  const bytes = [
    (address >>> 24) & 255,
    (address >>> 16) & 255,
    (address >>> 8) & 255,
    address & 255,
  ];
  return [...bytes, checksum(bytes)];
}
export function writePacket(bytes) {
  if (!bytes.length || bytes.length > 256)
    throw new Error("STM32 write payload must contain 1 to 256 bytes.");
  const padded = new Uint8Array(Math.ceil(bytes.length / 32) * 32).fill(255);
  padded.set(bytes);
  const packet = [padded.length - 1, ...padded];
  return [...packet, checksum(packet)];
}
export function parseDfuMemory(name) {
  const match = /^@Internal Flash\s*\/0x08000000\/([^/]+)$/i.exec(name || "");
  if (!match) throw new Error("Select the STM32 internal flash DFU interface.");
  const sectors = [];
  let address = 0x08000000;
  for (const part of match[1].split(",")) {
    const m = /^\s*(\d+)\*(\d+)([KMB])([a-g])\s*$/i.exec(part);
    if (!m || m[4].toLowerCase() !== "g")
      throw new Error("DfuSe flash must be readable, writable and erasable.");
    const size =
      Number(m[2]) * { K: 1024, M: 1048576, B: 1 }[m[3].toUpperCase()];
    const count = Number(m[1]);
    if (
      !size ||
      !count ||
      count > 4096 ||
      address - 0x08000000 + size * count > 2097152
    )
      throw new Error("Invalid DFU memory map.");
    for (let i = 0; i < count; i++) {
      sectors.push({ address, size });
      address += size;
    }
  }
  return sectors;
}
export async function writeUf2(directory, bytes, board, filename) {
  validateImage(bytes, board);
  const info = await (await directory.getFileHandle("INFO_UF2.TXT")).getFile();
  const text = await info.text();
  const expected =
    board.chip === "RP2040"
      ? /Board-ID:\s*RPI-RP2\b/i
      : /Board-ID:\s*RP2350\b/i;
  if (!expected.test(text))
    throw new Error(
      `This is not a ${board.chip} BOOTSEL drive. Nothing was written.`,
    );
  const handle = await directory.getFileHandle(filename, { create: true });
  const stream = await handle.createWritable();
  try {
    await stream.write(bytes);
    await stream.close();
  } catch (e) {
    await stream.abort().catch(() => {});
    throw new Error(
      `USB drive copy did not complete: ${e.message}. Re-enter BOOTSEL and retry, or use the download fallback.`,
    );
  }
}
