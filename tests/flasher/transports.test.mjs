import test from "node:test";
import assert from "node:assert/strict";
import { Stm32Uart, Stm32Dfu } from "../../tools/flasher/stm32.js";
import { writePacket, addressPacket } from "../../tools/flasher/core.js";
const ACK = 0x79;
class ScriptUart extends Stm32Uart {
  constructor(script) {
    super({});
    this.script = script;
    this.sent = [];
  }
  async open() {}
  async send(bytes) {
    this.sent.push([...bytes]);
    const [expected, reply] = this.script.shift() || [];
    assert.deepEqual([...bytes], expected);
    this.queue.push(...reply);
  }
}
const handshake = (pid = 0x431, erase = 0x44) => [
  [[0x7f], [ACK]],
  [
    [0, 255],
    [ACK, 5, 0x31, 0, 2, 0x11, 0x31, erase, ACK],
  ],
  [
    [2, 253],
    [ACK, 1, pid >> 8, pid & 255, ACK],
  ],
];
test("UART handshake, mass erase, padded write and readback follow AN3155", async () => {
  const bytes = Uint8Array.of(1, 2, 3, 4, 5, 6, 7, 8),
    addr = addressPacket(0x08000000);
  const script = [
    ...handshake(),
    [[0x44, 0xbb], [ACK]],
    [[255, 255, 0], [ACK]],
    [[0x31, 0xce], [ACK]],
    [addr, [ACK]],
    [writePacket(bytes), [ACK]],
    [[0x11, 0xee], [ACK]],
    [addr, [ACK]],
    [
      [7, 248],
      [ACK, ...bytes],
    ],
  ];
  const uart = new ScriptUart(script);
  await uart.connect({ pid: 0x431, capacity: 524288 });
  let value = 0;
  await uart.flash(bytes, (v) => (value = v));
  assert.equal(value, 100);
  assert.equal(script.length, 0);
});
test("UART wrong chip refuses before any erase/write command", async () => {
  const uart = new ScriptUart(handshake(0x433));
  await assert.rejects(
    uart.connect({ pid: 0x431, capacity: 524288 }),
    /does not match/,
  );
  assert.equal(uart.sent.length, 3);
});
test("UART NACK does not attempt automatic read-unprotect", async () => {
  const uart = new ScriptUart([[[0x7f], [0x1f]]]);
  await assert.rejects(
    uart.connect({ pid: 0x431, capacity: 524288 }),
    /No automatic unprotect/,
  );
  assert.deepEqual(uart.sent, [[0x7f]]);
});
test("UART rejects corrupt readback", async () => {
  const bytes = Uint8Array.of(1, 2, 3, 4),
    addr = addressPacket(0x08000000);
  const script = [
    ...handshake(),
    [[0x44, 0xbb], [ACK]],
    [[255, 255, 0], [ACK]],
    [[0x31, 0xce], [ACK]],
    [addr, [ACK]],
    [writePacket(bytes), [ACK]],
    [[0x11, 0xee], [ACK]],
    [addr, [ACK]],
    [
      [3, 252],
      [ACK, 1, 2, 3, 99],
    ],
  ];
  const uart = new ScriptUart(script);
  await uart.connect({ pid: 0x431, capacity: 524288 });
  await assert.rejects(
    uart.flash(bytes, () => {}),
    /verification failed/,
  );
});
test("UART read has a bounded timeout", async () => {
  await assert.rejects(new ScriptUart([]).read(1, 0), /timeout/);
});
class FakeDfu {
  constructor(map = "04*016Kg,01*064Kg,03*128Kg", transfer = 1024) {
    this.transfer = transfer;
    this.configuration = {
      configurationValue: 1,
      interfaces: [
        {
          interfaceNumber: 0,
          alternates: [
            {
              interfaceClass: 0xfe,
              interfaceSubclass: 1,
              interfaceProtocol: 2,
              alternateSetting: 0,
              interfaceName: `@Internal Flash /0x08000000/${map}`,
            },
          ],
        },
      ],
    };
    this.configurations = [this.configuration];
    this.memory = new Uint8Array(524288).fill(255);
    this.downloads = [];
    this.uploads = [];
    this.state = 2;
    this.address = 0x08000000;
  }
  async open() {
    this.opened = true;
  }
  async close() {
    this.opened = false;
  }
  async claimInterface() {}
  async releaseInterface() {}
  async selectAlternateInterface() {}
  async controlTransferOut({ request, value }, data) {
    if (request === 6) {
      assert.ok(
        [2, 3, 5, 9].includes(this.state),
        `ABORT from state ${this.state}`,
      );
      this.state = 2;
    } else if (request === 4) {
      assert.equal(this.state, 10);
      this.state = 2;
      this.error = 0;
    } else if (request === 1) {
      assert.ok([2, 5].includes(this.state), `DNLOAD from state ${this.state}`);
      this.downloads.push({ value, data: [...data] });
      this.pending = () => {
        if (value === 0) {
          const addr = new DataView(
            data.buffer,
            data.byteOffset,
            data.byteLength,
          ).getUint32(1, true);
          if (data[0] === 0x21) this.address = addr;
          else if (data[0] !== 0x41) throw Error("Unsafe command");
        } else {
          assert.ok(data.byteLength <= Math.min(this.transfer, 2048));
          assert.equal(data.byteLength % 4, 0);
          // AN3156 §5.1: stride is length of the sent buffer, NOT the descriptor.
          this.memory.set(
            data,
            this.address - 0x08000000 + (value - 2) * data.byteLength,
          );
        }
      };
      this.state = 3;
    } else throw Error("Unexpected request");
    return { status: "ok", bytesWritten: data.byteLength };
  }
  async controlTransferIn({ request, requestType, value }, length) {
    let bytes;
    if (requestType === "standard") {
      assert.equal(
        value,
        0x0200 + this.configurations.indexOf(this.configuration),
      );
      const descriptor =
        this.descriptor ||
        Uint8Array.of(
          9,
          2,
          27,
          0,
          1,
          this.configuration.configurationValue,
          0,
          0x80,
          50,
          9,
          4,
          0,
          0,
          0,
          0xfe,
          1,
          2,
          0,
          9,
          0x21,
          0x0b,
          0,
          0,
          this.transfer & 255,
          this.transfer >> 8,
          0x1a,
          1,
        );
      bytes = descriptor.slice(0, length);
    } else if (request === 3) {
      let state = this.state;
      if (state === 3) {
        this.pending();
        this.pending = null;
        state = 4;
        this.state = 5;
      }
      bytes = Uint8Array.of(this.error || 0, 0, 0, 0, state, 0);
    } else if (request === 2) {
      assert.ok([2, 9].includes(this.state), `UPLOAD from state ${this.state}`);
      assert.ok(length >= 2 && length <= Math.min(this.transfer, 2048));
      // AN3156 §4.1: stride is length of the requested buffer.
      const offset = this.address - 0x08000000 + (value - 2) * length;
      this.uploads.push({ value, address: this.address, length });
      bytes = this.memory.slice(offset, offset + length);
      if (this.corrupt) bytes[0] ^= 1;
      this.state = 9;
    } else throw Error("Unexpected request");
    return { status: "ok", data: new DataView(bytes.buffer) };
  }
}

test("DfuSe descriptor, erase, address, block write and readback sequence", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  assert.equal(dfu.chunk, 1024);
  const bytes = new Uint8Array(1500).map((_, i) => i % 251);
  let percent = 0;
  await dfu.flash(bytes, (v) => (percent = v));
  assert.equal(percent, 100);
  assert.deepEqual(device.memory.slice(0, 1500), bytes);
  assert.equal(device.downloads[0].data[0], 0x41);
  await dfu.close();
  assert.equal(device.opened, false);
});
test("DFU wrong geometry aborts before erase", async () => {
  const d = new FakeDfu("08*128Kg");
  await assert.rejects(
    new Stm32Dfu(d).connect({ id: "blackpill_f411ce" }),
    /geometry/,
  );
  assert.equal(d.downloads.length, 0);
});
test("DFU rejects readback mismatch and protection errors", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  device.corrupt = true;
  await assert.rejects(
    dfu.flash(Uint8Array.of(1, 2, 3, 4), () => {}),
    /verification failed/,
  );
  device.error = 3;
  await assert.rejects(dfu.poll(), /no automatic unprotect/);
});

test("DFU caps ROM transfers at 2048 and explicitly addresses a short final block", async () => {
  const device = new FakeDfu(undefined, 4096),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  assert.equal(dfu.chunk, 2048);
  const bytes = new Uint8Array(5001).map((_, i) => i % 253);
  await dfu.flash(bytes, () => {});
  assert.deepEqual(device.memory.slice(0, bytes.length), bytes);
  assert.ok(device.memory.slice(bytes.length, 5004).every((b) => b === 255));
  const writes = device.downloads.filter((d) => d.value !== 0);
  assert.deepEqual(
    writes.map((d) => d.value),
    [2, 2, 2],
  );
  assert.deepEqual(
    writes.map((d) => d.data.length),
    [2048, 2048, 908],
  );
  assert.deepEqual(device.uploads, [
    { value: 2, address: 0x08000000, length: 2048 },
    { value: 2, address: 0x08000800, length: 2048 },
    { value: 2, address: 0x08001000, length: 908 },
  ]);
});

test("UART rejects empty, oversized or unconnected writes before erase", async () => {
  const uart = new ScriptUart(handshake());
  await uart.connect({ pid: 0x431, capacity: 524288 });
  for (const bytes of [new Uint8Array(), new Uint8Array(524289)])
    await assert.rejects(
      uart.flash(bytes, () => {}),
      /Nothing was erased/,
    );
  assert.equal(uart.sent.length, 3);
  const unconnected = new ScriptUart([]);
  await assert.rejects(
    unconnected.flash(Uint8Array.of(1), () => {}),
    /Nothing was erased/,
  );
  assert.equal(unconnected.sent.length, 0);
});
test("UART legacy erase uses AN3155 global-erase checksum", async () => {
  const bytes = Uint8Array.of(10, 20, 30, 40),
    address = addressPacket(0x08000000);
  const uart = new ScriptUart([
    ...handshake(0x431, 0x43),
    [[0x43, 0xbc], [ACK]],
    [[255, 0], [ACK]],
    [[0x31, 0xce], [ACK]],
    [address, [ACK]],
    [writePacket(bytes), [ACK]],
    [[0x11, 0xee], [ACK]],
    [address, [ACK]],
    [
      [3, 252],
      [ACK, ...bytes],
    ],
  ]);
  await uart.connect({ pid: 0x431, capacity: 524288 });
  await uart.flash(bytes, () => {});
  assert.equal(uart.script.length, 0);
});
test("UART refuses bootloaders without readback or erase commands", async () => {
  for (const missing of [0x11, 0x31, 0x44]) {
    const script = handshake();
    const reply = script[1][1];
    reply.splice(reply.lastIndexOf(missing), 1);
    reply[1]--;
    const uart = new ScriptUart(script);
    await assert.rejects(
      uart.connect({ pid: 0x431, capacity: 524288 }),
      /Nothing was erased/,
    );
    assert.equal(uart.sent.length, 3);
  }
});
test("UART disconnect failure is raised instead of waiting until timeout", async () => {
  const uart = new ScriptUart([]);
  uart.failure = Error("device disconnected");
  await assert.rejects(uart.read(1), /disconnected/);
});
test("DFU rejects empty, oversized or unconnected writes before erase", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  for (const bytes of [new Uint8Array(), new Uint8Array(524289)])
    await assert.rejects(
      dfu.flash(bytes, () => {}),
      /Nothing was erased/,
    );
  assert.equal(device.downloads.length, 0);
  await assert.rejects(
    new Stm32Dfu(new FakeDfu()).flash(Uint8Array.of(1), () => {}),
    /Nothing was erased/,
  );
});
test("DFU checks all USB OUT bytes were transferred", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  device.controlTransferOut = async () => ({ status: "ok", bytesWritten: 4 });
  await assert.rejects(dfu.special(0x41, 0x08000000), /incomplete/);
});
test("DFU rejects truncated USB IN responses", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  device.controlTransferIn = async () => ({
    status: "ok",
    data: new DataView(new ArrayBuffer(5)),
  });
  await assert.rejects(dfu.input(3, 0, 6), /Invalid USB DFU/);
});
function dfuDescriptor() {
  return Uint8Array.of(
    9,
    2,
    27,
    0,
    1,
    1,
    0,
    0x80,
    50,
    9,
    4,
    0,
    0,
    0,
    0xfe,
    1,
    2,
    0,
    9,
    0x21,
    0x0b,
    0,
    0,
    0,
    4,
    0x1a,
    1,
  );
}
test("DFU rejects unsafe capabilities, non-DfuSe versions and malformed descriptors", async () => {
  for (const [offset, value] of [
    [20, 1], // can download, cannot upload
    [20, 2], // can upload, cannot download
    [25, 0x10], // plain DFU 1.1, not ST's DfuSe 1.1a
    [23, 1],
    [24, 0], // invalid non-aligned transfer size
    [18, 1], // malformed functional descriptor
    [9, 40], // truncated interface descriptor
    [12, 1], // descriptor for a different alternate
  ]) {
    const device = new FakeDfu();
    device.descriptor = dfuDescriptor();
    device.descriptor[offset] = value;
    const dfu = new Stm32Dfu(device);
    await assert.rejects(dfu.connect({ id: "blackpill_f411ce" }));
    assert.equal(device.downloads.length, 0);
    await assert.rejects(
      dfu.flash(Uint8Array.of(1), () => {}),
      /Nothing was erased/,
    );
  }
});
test("DFU refuses a truncated configuration instead of trusting a partial map", async () => {
  const device = new FakeDfu();
  device.descriptor = dfuDescriptor().slice(0, 26);
  await assert.rejects(
    new Stm32Dfu(device).connect({ id: "blackpill_f411ce" }),
    /descriptors/,
  );
  assert.equal(device.downloads.length, 0);
});
test("DFU uses the descriptor index for the active configuration", async () => {
  const device = new FakeDfu();
  device.configuration.configurationValue = 3;
  device.configurations.unshift({ configurationValue: 1, interfaces: [] });
  const dfu = new Stm32Dfu(device);
  await dfu.connect({ id: "blackpill_f411ce" });
  await dfu.flash(Uint8Array.of(1, 2, 3), () => {});
  assert.deepEqual(device.memory.slice(0, 4), Uint8Array.of(1, 2, 3, 255));
});
test("DFU only recovers defined idle states, never aborts an active erase", async () => {
  const device = new FakeDfu(),
    dfu = new Stm32Dfu(device);
  device.state = 4;
  await assert.rejects(dfu.connect({ id: "blackpill_f411ce" }), /not idle/);
  assert.equal(device.state, 4);
  assert.equal(device.downloads.length, 0);
  const errorDevice = new FakeDfu();
  errorDevice.state = 10;
  errorDevice.error = 3;
  await new Stm32Dfu(errorDevice).connect({ id: "blackpill_f411ce" });
  assert.equal(errorDevice.state, 2);
  assert.equal(errorDevice.downloads.length, 0);
});
test("DFU does not truncate the advertised busy poll delay", async () => {
  const dfu = new Stm32Dfu({});
  // 0x020000 = 131072 ms, exceeding the entire operation deadline.
  // Polling sooner would violate bwPollTimeout; fail safely without waiting.
  dfu.input = async () => Uint8Array.of(0, 0, 0, 2, 4, 0);
  await assert.rejects(dfu.poll(), /device poll timeout/);
});
