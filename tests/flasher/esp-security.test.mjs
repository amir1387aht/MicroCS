import test from "node:test";
import assert from "node:assert/strict";
import { ESPLoader } from "../../tools/flasher/node_modules/esptool-js/lib/esploader.js";
import { assertUnsecuredEsp } from "../../tools/flasher/core.js";

// Exercise the pinned library's actual getSecurityInfo/main/runStub cache path.
// Only hardware interactions and chip-description reads are mocked.
for (const [name, id, short] of [
  ["ESP32-C3", "esp32c3", false],
  ["ESP32-S2", "esp32s2", true],
]) {
  test(`${name} ROM security cache survives main/runStub at unchanged baud`, async () => {
    const loader = new ESPLoader({
      transport: {
        getInfo() {
          return "mock";
        },
      },
      baudrate: 115200,
      terminal: { clean() {}, write() {}, writeLine() {} },
    });
    let queries = 0;
    loader.checkCommand = async (...args) => {
      queries++;
      if (short && args[4] === 20) throw Error("12-byte S2 reply");
      const response = new Uint8Array(short ? 12 : 20);
      response[12] = 5; // C3 image-chip ID; absent in S2's short response.
      return response;
    };
    loader.chip = {
      CHIP_NAME: name,
      async getChipDescription() {
        return name;
      },
      async getChipFeatures() {
        return [];
      },
      async getCrystalFreq() {
        return 40;
      },
      async readMac() {
        return "mock";
      },
    };
    loader.detectChip = async () => {
      await loader.getSecurityInfo();
    };
    loader.syncStubDetected = true;
    loader.applyUsbFlashWriteSize = async () => {};
    loader.readFlashId = async () => 0x1640ef;
    await loader.main();
    const queryCount = queries;
    assert.equal(queryCount, short ? 2 : 1);
    assert.equal(loader.IS_STUB, true);
    // No new command may be sent to a stub that does not support security-info.
    loader.checkCommand = async () => {
      throw Error("uncached stub command");
    };
    await assertUnsecuredEsp(loader, { id });
    assert.equal(queries, queryCount);
  });
}
