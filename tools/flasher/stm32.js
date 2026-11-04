import { addressPacket, writePacket, hex, parseDfuMemory } from "./core.js";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
export class Stm32Uart {
  constructor(port, log = () => {}) {
    this.port = port;
    this.log = log;
    this.queue = [];
    this.failure = null;
  }
  async open() {
    await this.port.open({
      baudRate: 115200,
      dataBits: 8,
      stopBits: 1,
      parity: "even",
      flowControl: "none",
    });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.pump = (async () => {
      try {
        while (true) {
          const { value, done } = await this.reader.read();
          if (done) break;
          this.queue.push(...value);
        }
      } catch (e) {
        this.failure = e;
      } finally {
        this.failure ||= new Error("Serial device disconnected.");
      }
    })();
  }
  async read(count, timeout = 5000) {
    const deadline = Date.now() + timeout;
    while (this.queue.length < count) {
      if (this.failure) throw this.failure;
      if (Date.now() > deadline)
        throw new Error(
          "STM32 timeout. Check BOOT0, ROM UART pins, 3.3 V wiring and reset; reconnect before retrying.",
        );
      await sleep(5);
    }
    return Uint8Array.from(this.queue.splice(0, count));
  }
  async send(data) {
    await this.writer.write(Uint8Array.from(data));
  }
  async ack(timeout) {
    const [value] = await this.read(1, timeout);
    if (value !== 0x79)
      throw new Error(
        `STM32 rejected command (${hex(value)}). Check boot mode or protection. No automatic unprotect is attempted.`,
      );
  }
  async command(value) {
    await this.send([value, value ^ 255]);
    await this.ack();
  }
  async packet(bytes, timeout) {
    await this.send(bytes);
    await this.ack(timeout);
  }
  async connect(board) {
    this.board = null;
    await this.open();
    await this.send([0x7f]);
    await this.ack();
    await this.command(0);
    const [n] = await this.read(1);
    const commands = await this.read(n + 1);
    await this.ack();
    await this.command(2);
    const [size] = await this.read(1);
    const id = await this.read(size + 1);
    await this.ack();
    if (id.length !== 2 || ((id[0] << 8) | id[1]) !== board.pid)
      throw new Error(
        "STM32 device ID does not match the selected MCU. Nothing was erased.",
      );
    this.commands = [...commands.slice(1)];
    for (const c of [0x11, 0x31])
      if (!this.commands.includes(c))
        throw new Error(
          "Bootloader does not support verified writes. Nothing was erased.",
        );
    if (!this.commands.includes(0x44) && !this.commands.includes(0x43))
      throw new Error("No supported erase command. Nothing was erased.");
    this.board = board;
    this.log(
      `Matched STM32 device ID ${hex(board.pid)}. Board variant must still match your selection.`,
    );
  }
  async flash(bytes, progress) {
    if (
      !this.board ||
      !bytes.length ||
      !Number.isSafeInteger(this.board.capacity) ||
      Math.ceil(bytes.length / 32) * 32 > this.board.capacity
    )
      throw new Error(
        "Firmware does not fit the connected MCU. Nothing was erased.",
      );
    this.log("Erasing flash. Do not unplug the board.");
    if (this.commands.includes(0x44)) {
      await this.command(0x44);
      await this.packet([255, 255, 0], 120000);
    } else {
      await this.command(0x43);
      await this.packet([255, 0], 120000);
    }
    for (let offset = 0; offset < bytes.length; offset += 256) {
      await this.command(0x31);
      await this.packet(addressPacket(0x08000000 + offset));
      await this.packet(
        writePacket(bytes.subarray(offset, offset + 256)),
        10000,
      );
      progress(Math.min(65, (65 * (offset + 256)) / bytes.length), "Writing");
    }
    this.log("Reading back flash for byte-for-byte verification.");
    for (let offset = 0; offset < bytes.length; offset += 256) {
      const count = Math.min(256, bytes.length - offset);
      await this.command(0x11);
      await this.packet(addressPacket(0x08000000 + offset));
      await this.packet([count - 1, (count - 1) ^ 255]);
      const actual = await this.read(count);
      if (actual.some((b, i) => b !== bytes[offset + i]))
        throw new Error(
          `Readback verification failed at ${hex(0x08000000 + offset)}. Do not boot; retry flashing.`,
        );
      progress(
        65 + (35 * Math.min(offset + count, bytes.length)) / bytes.length,
        "Verifying",
      );
    }
    this.log(
      "Verified. Restore normal BOOT configuration and press RESET, then open Studio.",
    );
  }
  async close() {
    if (this.reader) {
      await this.reader.cancel().catch(() => {});
      await this.pump;
      this.reader.releaseLock();
    }
    this.writer?.releaseLock();
    await this.port.close().catch(() => {});
  }
}
export class Stm32Dfu {
  constructor(device, log = () => {}) {
    this.device = device;
    this.log = log;
  }
  async out(request, value = 0, data = new Uint8Array()) {
    const r = await this.device.controlTransferOut(
      {
        requestType: "class",
        recipient: "interface",
        request,
        value,
        index: this.iface,
      },
      data,
    );
    if (r.status !== "ok" || r.bytesWritten !== data.byteLength)
      throw new Error(
        "USB DFU request stalled or was incomplete. Re-enter bootloader and retry.",
      );
  }
  async input(request, value, length) {
    const r = await this.device.controlTransferIn(
      {
        requestType: "class",
        recipient: "interface",
        request,
        value,
        index: this.iface,
      },
      length,
    );
    if (r.status !== "ok" || !r.data || r.data.byteLength !== length)
      throw new Error("Invalid USB DFU response.");
    return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
  }
  async idle() {
    let s = await this.input(3, 0, 6);
    if (s[4] === 10) {
      await this.out(4);
      s = await this.input(3, 0, 6);
    }
    if (s[0] || ![2, 5, 9].includes(s[4]))
      throw new Error(
        "DFU is not idle. Reset into the bootloader before retrying.",
      );
    if (s[4] !== 2) {
      await this.out(6);
      s = await this.input(3, 0, 6);
      if (s[0] || s[4] !== 2)
        throw new Error("DFU could not return to idle state.");
    }
  }
  async poll() {
    const deadline = Date.now() + 120000;
    while (Date.now() < deadline) {
      const s = await this.input(3, 0, 6);
      if (s[0])
        throw new Error(
          `DFU error ${s[0]}. Check flash protection and boot mode; no automatic unprotect is attempted.`,
        );
      if (s[4] === 5) return;
      if (![3, 4].includes(s[4]))
        throw new Error(`Unexpected DFU state ${s[4]}.`);
      // bwPollTimeout is a minimum delay, not a suggestion to cap at 5 s.
      const delay = Math.max(5, s[1] + (s[2] << 8) + (s[3] << 16));
      if (delay > deadline - Date.now())
        throw new Error("USB DFU erase/write timed out (device poll timeout).");
      await sleep(delay);
    }
    throw new Error("USB DFU erase/write timed out.");
  }
  async special(command, address) {
    const packet = new Uint8Array(5);
    packet[0] = command;
    new DataView(packet.buffer).setUint32(1, address, true);
    await this.out(1, 0, packet);
    await this.poll();
  }
  async connect(board) {
    this.ready = false;
    await this.device.open();
    if (!this.device.configuration)
      await this.device.selectConfiguration(
        this.device.configurations[0].configurationValue,
      );
    const configIndex = this.device.configurations.findIndex(
      (c) =>
        c.configurationValue === this.device.configuration.configurationValue,
    );
    if (configIndex < 0)
      throw new Error("Cannot identify active USB configuration.");
    const isFlash = (a) =>
      a.interfaceClass === 0xfe &&
      a.interfaceSubclass === 1 &&
      a.interfaceProtocol === 2 &&
      a.interfaceName?.startsWith("@Internal Flash");
    const iface = this.device.configuration.interfaces.find((i) =>
      i.alternates.some(isFlash),
    );
    if (!iface) throw new Error("No STM32 DfuSe internal flash interface.");
    const alt = iface.alternates.find(isFlash);
    this.iface = iface.interfaceNumber;
    this.sectors = parseDfuMemory(alt.interfaceName);
    // This route is restricted to the published Black Pill F411 image and its 512 KiB F4 layout.
    if (
      board.id !== "blackpill_f411ce" ||
      this.sectors.map((s) => s.size).join(",") !==
        [16384, 16384, 16384, 16384, 65536, 131072, 131072, 131072].join(",")
    )
      throw new Error(
        "DFU flash geometry does not match the selected Black Pill. Nothing was erased.",
      );
    await this.device.claimInterface(this.iface);
    this.claimed = true;
    await this.device.selectAlternateInterface(
      this.iface,
      alt.alternateSetting,
    );
    // Read functional descriptor; wTransferSize is at bytes 5..6 of descriptor type 0x21.
    const head = await this.device.controlTransferIn(
      {
        requestType: "standard",
        recipient: "device",
        request: 6,
        value: 0x0200 | configIndex,
        index: 0,
      },
      9,
    );
    if (
      head.status !== "ok" ||
      head.data?.byteLength !== 9 ||
      head.data.getUint8(0) !== 9 ||
      head.data.getUint8(1) !== 2 ||
      head.data.getUint8(5) !== this.device.configuration.configurationValue ||
      head.data.getUint16(2, true) < 9
    )
      throw new Error("Cannot read USB configuration.");
    const total = head.data.getUint16(2, true);
    const result = await this.device.controlTransferIn(
      {
        requestType: "standard",
        recipient: "device",
        request: 6,
        value: 0x0200 | configIndex,
        index: 0,
      },
      total,
    );
    if (result.status !== "ok" || result.data?.byteLength !== total)
      throw new Error("Cannot read DFU descriptors.");
    const view = result.data;
    let active = false;
    let transfer = 0;
    for (let i = 0; i < view.byteLength;) {
      if (i + 2 > view.byteLength)
        throw new Error("Malformed DFU descriptors.");
      const len = view.getUint8(i),
        type = view.getUint8(i + 1);
      if (len < 2 || i + len > view.byteLength)
        throw new Error("Malformed DFU descriptors.");
      if (type === 4 && len >= 9)
        active =
          view.getUint8(i + 2) === this.iface &&
          view.getUint8(i + 3) === alt.alternateSetting;
      if (type === 0x21 && active && len >= 9) {
        const flags = view.getUint8(i + 2);
        if ((flags & 3) !== 3)
          throw new Error("DFU device cannot write and read back.");
        if (view.getUint16(i + 7, true) !== 0x011a)
          throw new Error("Selected interface is not STM32 DfuSe.");
        transfer = view.getUint16(i + 5, true);
      }
      i += len;
    }
    if (transfer < 256 || transfer > 65535 || transfer % 4)
      throw new Error("Invalid DFU transfer size.");
    // AN3156 supports at most 2048 bytes for F4 internal flash operations.
    // Descriptor transfer size is a ceiling, not proof of ROM flash granularity.
    this.chunk = Math.min(transfer, 2048);
    await this.idle();
    this.ready = true;
    this.log(
      "DFU geometry checked. USB DFU does not uniquely identify the MCU: exact board selection is required.",
    );
  }
  async flash(bytes, progress) {
    const capacity = this.sectors?.reduce((n, sector) => n + sector.size, 0);
    if (
      !this.ready ||
      !bytes.length ||
      Math.ceil(bytes.length / 4) * 4 > capacity
    )
      throw new Error(
        "Firmware does not fit the connected DFU flash. Nothing was erased.",
      );
    this.log("Erasing covered internal flash sectors. Do not unplug.");
    for (const sector of this.sectors)
      if (sector.address < 0x08000000 + bytes.length)
        await this.special(0x41, sector.address);
    for (let offset = 0; offset < bytes.length; offset += this.chunk) {
      const part = bytes.subarray(offset, offset + this.chunk);
      const padded = new Uint8Array(Math.ceil(part.length / 4) * 4).fill(255);
      padded.set(part);
      // AN3156 §§4.1/5.1 derives block strides from the transfer length.
      // A short final block must not reuse an incremented block number. Set
      // the exact pointer and use block 2, safe with either ROM interpretation.
      await this.special(0x21, 0x08000000 + offset);
      await this.out(1, 2, padded);
      await this.poll();
      progress(
        (65 * Math.min(offset + part.length, bytes.length)) / bytes.length,
        "Writing",
      );
    }
    await this.idle();
    for (let offset = 0; offset < bytes.length; offset += this.chunk) {
      const count = Math.min(this.chunk, bytes.length - offset);
      await this.special(0x21, 0x08000000 + offset);
      await this.idle(); // DNLOAD-IDLE -> IDLE before UPLOAD.
      const data = await this.input(2, 2, Math.ceil(count / 4) * 4);
      if (data.some((b, i) => b !== (bytes[offset + i] ?? 255)))
        throw new Error(
          `DFU verification failed at ${hex(0x08000000 + offset)}.`,
        );
      await this.idle(); // UPLOAD-IDLE -> IDLE before the next address command.
      progress(65 + (35 * (offset + count)) / bytes.length, "Verifying");
    }
    // Keep bootloader connected: user restores BOOT0 then resets. This avoids masking disconnect errors.
    this.log(
      "Readback verified. Set BOOT0 low and press RESET to start MicroCS.",
    );
  }
  async close() {
    if (this.claimed)
      await this.device.releaseInterface(this.iface).catch(() => {});
    if (this.device.opened) await this.device.close().catch(() => {});
  }
}
