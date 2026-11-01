import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import {
  assertChip,
  assertUnsecuredEsp,
  assetFor,
  verifyAsset,
  validateImage,
  addressPacket,
  writePacket,
  checksum,
  parseDfuMemory,
  writeUf2,
} from "../../tools/flasher/core.js";
const boards = JSON.parse(
  await readFile(new URL("../../tools/flasher/boards.json", import.meta.url)),
);
test("registry covers every release target, without claiming generic Arduino/Zephyr images", () => {
  assert.equal(boards.length, 22);
  assert.equal(new Set(boards.map((b) => b.id)).size, 22);
  assert.equal(boards.filter((b) => b.family === "ESP32").length, 10);
  assert.equal(boards.filter((b) => b.family === "RP2").length, 5);
  assert.equal(boards.filter((b) => b.family === "STM32").length, 7);
});
test("ESP target matching is exact, including C6 vs C61", () => {
  assertChip("ESP32-C61", "esp32c61");
  assertChip("ESP32-S3", "esp32s3");
  assert.throws(() => assertChip("ESP32-C6", "esp32c61"), /Nothing was erased/);
  assert.throws(() => assertChip("ESP32-S3", "esp32"), /Wrong MCU/);
});
test("asset lookup has no fuzzy board matches", () => {
  assert.equal(
    assetFor({ assets: [{ board: "esp32c61" }] }, { id: "esp32c6" }),
    undefined,
  );
});
test("SHA-256 and size checked before install", async () => {
  const bytes = Uint8Array.of(1, 2, 3),
    sha256 = "039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81";
  assert.deepEqual(await verifyAsset(bytes.buffer, { size: 3, sha256 }), bytes);
  await assert.rejects(
    verifyAsset(bytes.buffer, { size: 4, sha256 }),
    /size mismatch/,
  );
  await assert.rejects(
    verifyAsset(bytes.buffer, { size: 3, sha256: "00" }),
    /checksum mismatch/,
  );
});
test("STM32 image fits target and has flash vectors", () => {
  const board = boards.find((b) => b.id === "blackpill_f411ce");
  const bytes = new Uint8Array(512),
    v = new DataView(bytes.buffer);
  v.setUint32(0, 0x20020000, true);
  v.setUint32(4, 0x08000101, true);
  validateImage(bytes, board);
  v.setUint32(4, 0x08000100, true);
  assert.throws(() => validateImage(bytes, board), /vector/);
  assert.throws(
    () => validateImage(new Uint8Array(board.capacity + 1), board),
    /fit/,
  );
});
test("ESP32 merged images validate the correct bootloader offset", () => {
  const image = new Uint8Array(12288);
  image[0x1000] = 0xe9;
  validateImage(
    image,
    boards.find((b) => b.id === "esp32"),
  );
  assert.throws(
    () =>
      validateImage(
        image,
        boards.find((b) => b.id === "esp32s3"),
      ),
    /Invalid/,
  );
});
function uf2(family = 0xe48bff56) {
  const b = new Uint8Array(512),
    v = new DataView(b.buffer);
  for (const [o, n] of [
    [0, 0x0a324655],
    [4, 0x9e5d5157],
    [8, 0x2000],
    [12, 0x10000000],
    [16, 256],
    [24, 1],
    [28, family],
    [508, 0x0ab16f30],
  ])
    v.setUint32(o, n, true);
  return b;
}
test("all ESP bootloader offsets and chip IDs are validated", () => {
  for (const b of boards.filter((b) => b.family === "ESP32")) {
    const bytes = new Uint8Array(b.bootOffset + 24);
    bytes[b.bootOffset] = 0xe9;
    new DataView(bytes.buffer).setUint16(
      b.bootOffset + 12,
      b.imageChipId,
      true,
    );
    validateImage(bytes, b);
    bytes[b.bootOffset + 12] ^= 1;
    assert.throws(() => validateImage(bytes, b), /header/);
  }
});
test("every UF2 block is checked and RP2 families cannot be confused", () => {
  const rp = boards.find((b) => b.id === "pico"),
    rp2 = boards.find((b) => b.id === "pico2");
  validateImage(uf2(), rp);
  validateImage(uf2(0xe48bff59), rp2);
  validateImage(uf2(0xe48bff57), rp2);
  assert.throws(() => validateImage(uf2(), rp2), /family/);
  assert.throws(() => validateImage(new Uint8Array(1), rp), /length/);
  const corrupt = new Uint8Array(1024);
  corrupt.set(uf2());
  new DataView(corrupt.buffer).setUint32(24, 2, true);
  assert.throws(() => validateImage(corrupt, rp), /signature/);
});
test("UART address XOR and final write padding", () => {
  assert.deepEqual(addressPacket(0x08000000), [8, 0, 0, 0, 8]);
  const packet = writePacket(Uint8Array.of(1, 2, 3));
  assert.equal(packet[0], 31);
  assert.equal(packet.length, 34);
  assert.equal(checksum(packet), 0);
  assert.equal(packet[4], 255);
});
test("DFU map is parsed strictly and unsafe maps rejected", () => {
  const sectors = parseDfuMemory(
    "@Internal Flash /0x08000000/04*016Kg,01*064Kg,03*128Kg",
  );
  assert.equal(sectors.length, 8);
  assert.equal(sectors.at(-1).address + sectors.at(-1).size, 0x08080000);
  for (const name of [
    "@Option Bytes /0x1ffff800/01*016 e",
    "@Internal Flash /0x00000000/01*512Kg",
    "@Internal Flash /0x08000000/01*512Ka",
  ])
    assert.throws(() => parseDfuMemory(name));
});
test("UF2 validates boot drive before creating any output", async () => {
  let created = false;
  const dir = {
    async getFileHandle(name, opts) {
      if (opts) {
        created = true;
        throw Error("unexpected write");
      }
      return {
        async getFile() {
          return {
            async text() {
              return "Board-ID: RP2350";
            },
          };
        },
      };
    },
  };
  await assert.rejects(
    writeUf2(
      dir,
      uf2(),
      boards.find((b) => b.id === "pico"),
      "x.uf2",
    ),
    /not a RP2040/,
  );
  assert.equal(created, false);
});
test("UF2 writes and closes verified boot drive", async () => {
  let data,
    closed = false;
  const dir = {
    async getFileHandle(name, opts) {
      return opts
        ? {
            async createWritable() {
              return {
                async write(b) {
                  data = b;
                },
                async close() {
                  closed = true;
                },
              };
            },
          }
        : {
            async getFile() {
              return {
                async text() {
                  return "UF2 Bootloader\nBoard-ID: RPI-RP2";
                },
              };
            },
          };
    },
  };
  const b = uf2();
  await writeUf2(
    dir,
    b,
    boards.find((b) => b.id === "pico"),
    "x.uf2",
  );
  assert.equal(data, b);
  assert.equal(closed, true);
});

test("STM32 reset vector must point inside the image, stack is aligned", () => {
  const board = boards.find((b) => b.id === "blackpill_f411ce");
  const bytes = new Uint8Array(32),
    v = new DataView(bytes.buffer);
  v.setUint32(0, 0x20020000, true);
  v.setUint32(4, 0x08000009, true);
  validateImage(bytes, board);
  v.setUint32(4, 0x08000101, true);
  assert.throws(() => validateImage(bytes, board), /vector/);
  v.setUint32(4, 0x08000009, true);
  v.setUint32(0, 0x20020001, true);
  assert.throws(() => validateImage(bytes, board), /vector/);
});
test("UART payload length is bounded before constructing a packet", () => {
  assert.throws(() => writePacket(new Uint8Array()), /1 to 256/);
  assert.throws(() => writePacket(new Uint8Array(257)), /1 to 256/);
  assert.equal(writePacket(new Uint8Array(256))[0], 255);
});
test("DfuSe permissions must include read, erase AND write; maps are bounded", () => {
  for (const permission of ["a", "b", "c", "d", "e", "f"])
    assert.throws(() =>
      parseDfuMemory(`@Internal Flash /0x08000000/01*512K${permission}`),
    );
  for (const layout of ["00*512Kg", "01*000Kg", "01*2Mg,01*1Mg"])
    assert.throws(
      () => parseDfuMemory(`@Internal Flash /0x08000000/${layout}`),
      /Invalid/,
    );
});
function uf2Blocks(count = 2, family = 0xe48bff56) {
  const bytes = new Uint8Array(512 * count),
    view = new DataView(bytes.buffer);
  for (let i = 0; i < count; i++) {
    bytes.set(uf2(family), i * 512);
    view.setUint32(i * 512 + 12, 0x10000000 + i * 256, true);
    view.setUint32(i * 512 + 20, i, true);
    view.setUint32(i * 512 + 24, count, true);
  }
  return bytes;
}
test("UF2 refuses unsafe payload, address, flags, missing and repeated blocks", () => {
  const board = boards.find((b) => b.id === "pico");
  for (const [offset, value] of [
    [8, 0x2001],
    [8, 0x3000],
    [8, 0x6000],
    [8, 0x12000],
    [12, 0x20000000],
    [12, 0x10000001],
    [12, 0x11000000],
    [16, 0],
    [16, 480],
    [16, 252],
    [24, 0],
    [20, 2],
    [512 + 20, 0],
    [512 + 24, 3],
    [512 + 12, 0x10000000],
  ]) {
    const bytes = uf2Blocks();
    new DataView(bytes.buffer).setUint32(offset, value, true);
    assert.throws(
      () => validateImage(bytes, board),
      /UF2/,
      `offset ${offset}, value ${value}`,
    );
  }
  assert.throws(
    () => validateImage(uf2Blocks().subarray(0, 512), board),
    /UF2/,
  );
  // Out-of-order physical blocks are valid as long as numbering is complete.
  const ordered = uf2Blocks(),
    shuffled = new Uint8Array(1024);
  shuffled.set(ordered.subarray(512));
  shuffled.set(ordered.subarray(0, 512), 512);
  validateImage(shuffled, board);
});
test("UF2 honors RP2350-E10 ignored absolute prefix without accepting truncation", () => {
  const board = boards.find((b) => b.id === "pico2");
  const prefix = uf2(0xe48bff57),
    v = new DataView(prefix.buffer);
  v.setUint32(8, 0xa000, true);
  v.setUint32(12, 0x10ffff00, true);
  v.setUint32(24, 2, true);
  prefix.fill(0xef, 32, 288);
  v.setUint32(288, 0x9957e304, true);
  const firmware = uf2Blocks(2, 0xe48bff59),
    bytes = new Uint8Array(1536);
  bytes.set(prefix);
  bytes.set(firmware, 512);
  validateImage(bytes, board);
  assert.throws(() => validateImage(prefix, board), /Incomplete/);
  assert.throws(() => validateImage(bytes.subarray(0, 1024), board), /UF2/);
  bytes[32] ^= 1;
  assert.throws(() => validateImage(bytes, board), /UF2/);
});
test("UF2 helper validates image before opening output or boot-drive files", async () => {
  const dir = {
    getFileHandle() {
      throw Error("must not access drive");
    },
  };
  await assert.rejects(
    writeUf2(
      dir,
      new Uint8Array(512),
      boards.find((b) => b.id === "pico"),
      "x.uf2",
    ),
    /UF2/,
  );
});
test("UF2 copy errors abort the stream and never report completion", async () => {
  let aborted = false,
    closed = false;
  const dir = {
    async getFileHandle(name, opts) {
      if (!opts)
        return {
          async getFile() {
            return {
              async text() {
                return "Board-ID: RPI-RP2";
              },
            };
          },
        };
      return {
        async createWritable() {
          return {
            async write() {
              throw Error("unplugged");
            },
            async close() {
              closed = true;
            },
            async abort() {
              aborted = true;
            },
          };
        },
      };
    },
  };
  await assert.rejects(
    writeUf2(
      dir,
      uf2(),
      boards.find((b) => b.id === "pico"),
      "x.uf2",
    ),
    /did not complete/,
  );
  assert.equal(aborted, true);
  assert.equal(closed, false);
});

function securityLoader(board, flags = 0, flashCryptCnt = 0) {
  return {
    chip: { CHIP_NAME: board.label },
    secureDownloadMode: false,
    async getSecurityInfo() {
      return { flags, flashCryptCnt };
    },
    async readReg(address) {
      if (address === 0x3ff5a000) return flashCryptCnt << 20;
      assert.equal(address, 0x3ff5a018);
      return flags & 1 ? 0x10 : 0;
    },
    writeReg() {
      throw Error("must never write a register or fuse");
    },
  };
}
test("ESP security preflight supports every released family without fuse writes", async () => {
  for (const board of boards.filter((b) => b.family === "ESP32")) {
    for (const count of [0, 3, 5, 6, 15])
      await assertUnsecuredEsp(securityLoader(board, 0, count), board);
    for (const count of [1, 2, 4, 7, 127])
      await assert.rejects(
        assertUnsecuredEsp(securityLoader(board, 0, count), board),
        /Nothing was erased/,
      );
    await assert.rejects(
      assertUnsecuredEsp(securityLoader(board, 1), board),
      /Secure Boot/,
    );
    const secure = securityLoader(board);
    secure.secureDownloadMode = true;
    await assert.rejects(assertUnsecuredEsp(secure, board), /Secure download/);
    if (board.id !== "esp32")
      await assert.rejects(
        assertUnsecuredEsp(securityLoader(board, 4), board),
        /secure download/,
      );
  }
});
test("ESP security checks fail closed on unreadable or malformed state", async () => {
  for (const board of boards.filter((b) => b.family === "ESP32")) {
    const loader = securityLoader(board);
    loader.readReg = loader.getSecurityInfo = async () => {
      throw Error("disconnected");
    };
    await assert.rejects(
      assertUnsecuredEsp(loader, board),
      /Cannot verify.*Nothing was erased/,
    );
    for (const bad of [null, undefined, NaN, -0x80000001, 0x100000000, "0"]) {
      loader.readReg = async () => bad;
      loader.getSecurityInfo = async () => ({ flags: bad, flashCryptCnt: 0 });
      await assert.rejects(assertUnsecuredEsp(loader, board), /Cannot verify/);
    }
    if (board.id !== "esp32") {
      for (const bad of [null, undefined, NaN, -1, 256, "0"]) {
        loader.getSecurityInfo = async () => ({ flags: 0, flashCryptCnt: bad });
        await assert.rejects(
          assertUnsecuredEsp(loader, board),
          /Cannot verify/,
        );
      }
    }
  }
});
test("ESP32 checks both legacy Secure Boot generations and detects chip mismatch", async () => {
  const board = boards.find((b) => b.id === "esp32"),
    loader = securityLoader(board);
  loader.readReg = async (address) => (address === 0x3ff5a018 ? 0x20 : 0);
  await assert.rejects(assertUnsecuredEsp(loader, board), /Secure Boot/);
  loader.chip.CHIP_NAME = "ESP32-C3";
  await assert.rejects(assertUnsecuredEsp(loader, board), /Wrong MCU/);
});

test("classic ESP32 accepts signed int32 register decoding without ignoring security bits", async () => {
  const board = boards.find((b) => b.id === "esp32");
  const loader = securityLoader(board);
  loader.readReg = async () => -0x80000000;
  await assertUnsecuredEsp(loader, board);
  loader.readReg = async (address) =>
    address === 0x3ff5a000 ? -0x7ff00000 : -0x80000000;
  await assert.rejects(assertUnsecuredEsp(loader, board), /Nothing was erased/);
});
