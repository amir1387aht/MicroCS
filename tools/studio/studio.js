"use strict";
/* ================================================================ utilities */
const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const icon = (name) => `<svg class="i"><use href="#i-${name}"/></svg>`;
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
function fmtBytes(n) {
  if (n == null || isNaN(n)) return "—";
  if (n < 1024) return n + " B";
  if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + " KB";
  if (n < 1024 * 1048576) return (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + " MB";
  return (n / 1073741824).toFixed(1) + " GB";
}
function concat(a, b) { if (!a.length) return b; const r = new Uint8Array(a.length + b.length); r.set(a); r.set(b, a.length); return r; }
function findSeq(buf, seq, from = 0, last = false) {
  const n = buf.length - seq.length;
  if (last) { for (let i = n; i >= from; i--) { let k = 0; while (k < seq.length && buf[i + k] === seq[k]) k++; if (k === seq.length) return i; } return -1; }
  for (let i = from; i <= n; i++) { let k = 0; while (k < seq.length && buf[i + k] === seq[k]) k++; if (k === seq.length) return i; }
  return -1;
}
const joinPath = (dir, name) => (dir === "/" ? "" : dir) + "/" + name;
const baseName = (p) => p.slice(p.lastIndexOf("/") + 1);
const dirName = (p) => { const i = p.lastIndexOf("/"); return i <= 0 ? "/" : p.slice(0, i); };
const extOf = (p) => { const b = baseName(p), i = b.lastIndexOf("."); return i > 0 ? b.slice(i + 1).toLowerCase() : ""; };
function normPath(p) {
  const out = [];
  for (const s of String(p).trim().replace(/\\/g, "/").split("/")) { if (!s || s === ".") continue; if (s === "..") out.pop(); else out.push(s); }
  return "/" + out.join("/");
}
function isText(bytes) {
  if (bytes.length === 0) return true;
  const n = Math.min(bytes.length, 8192);
  for (let i = 0; i < n; i++) { const b = bytes[i]; if (b === 0 || (b < 8) || (b > 13 && b < 27 && b !== 4)) return false; }
  try { new TextDecoder("utf-8", { fatal: true }).decode(bytes); return true; } catch { return false; }
}
function download(name, bytes) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bytes]));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
}

/* toasts, menus, dialogs */
function toast(msg, kind = "info", ms = 3200) {
  const t = document.createElement("div");
  t.className = "toast " + kind;
  t.innerHTML = icon(kind === "err" ? "x" : kind === "ok" ? "check" : "chip") + `<span>${esc(msg)}</span>`;
  $("toasts").appendChild(t);
  setTimeout(() => { t.style.transition = "opacity .3s"; t.style.opacity = "0"; setTimeout(() => t.remove(), 300); }, ms);
}
const Menu = {
  show(x, y, items) {
    const m = $("menu");
    m.innerHTML = "";
    for (const it of items) {
      if (it === "-") { m.appendChild(document.createElement("hr")); continue; }
      if (it.header) { const h = document.createElement("div"); h.className = "mh"; h.textContent = it.header; m.appendChild(h); continue; }
      const b = document.createElement("button");
      if (it.danger) b.className = "danger";
      b.innerHTML = (it.icon ? icon(it.icon) : "") + `<span>${esc(it.label)}</span>`;
      b.disabled = !!it.disabled;
      b.onclick = () => { Menu.hide(); it.run(); };
      m.appendChild(b);
    }
    m.style.display = "block";
    const r = m.getBoundingClientRect();
    m.style.left = Math.max(4, Math.min(x, innerWidth - r.width - 8)) + "px";
    m.style.top = Math.max(4, Math.min(y, innerHeight - r.height - 8)) + "px";
  },
  hide() { $("menu").style.display = "none"; },
  at(el, items) { const r = el.getBoundingClientRect(); Menu.show(r.left, r.bottom + 4, items); },
};
document.addEventListener("mousedown", (e) => { if (!$("menu").contains(e.target)) Menu.hide(); });
document.addEventListener("keydown", (e) => { if (e.key === "Escape") Menu.hide(); }, true);

function dialog({ title, text = "", value = null, ok = "OK", danger = false, select = null }) {
  return new Promise((resolve) => {
    const bg = $("modalBg"), m = $("modal");
    m.innerHTML = `<h3>${esc(title)}</h3>${text ? `<p>${esc(text)}</p>` : ""}` +
      (value !== null ? `<input id="dlgIn" spellcheck="false" autocomplete="off">` : "") +
      `<div class="acts"><button class="btn" id="dlgNo">Cancel</button><button class="btn ${danger ? "danger" : "primary"}" id="dlgOk">${esc(ok)}</button></div>`;
    bg.classList.add("show");
    const inp = $("dlgIn");
    const done = (v) => { bg.classList.remove("show"); document.removeEventListener("keydown", key, true); resolve(v); };
    const key = (e) => {
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); done(null); }
      else if (e.key === "Enter") { e.preventDefault(); e.stopPropagation(); done(inp ? inp.value : true); }
    };
    document.addEventListener("keydown", key, true);
    $("dlgNo").onclick = () => done(null);
    $("dlgOk").onclick = () => done(inp ? inp.value : true);
    bg.onmousedown = (e) => { if (e.target === bg) done(null); };
    if (inp) {
      inp.value = value; inp.focus();
      if (select) inp.setSelectionRange(select[0], select[1]); else inp.select();
    } else $("dlgOk").focus();
  });
}

/* ================================================================ device protocol
 * The MicroCS shell (modules/shell/mcs_shell.c, docs/STANDALONE.md):
 *   machine mode: one command per line; output, then a status line "\x04OK" / "\x04ERR msg"
 *   put <f> <len> -> "\x04READY", raw bytes, status    get <f> -> "\x04DATA <len>", bytes, status
 *   Ctrl-A: machine mode from any state ("\x04OK")      Ctrl-C: stop a running script
 *   repl: the interactive C# prompt ("> ", "... ")     */
class TimeoutError extends Error {}
class Device {
  constructor() {
    this.port = null; this.writer = null; this.reader = null;
    this.buf = new Uint8Array(0);
    this.wake = null;
    this.epoch = 0; this.opEpoch = 0;
    this.mode = "unknown";            // "machine" | "repl" | "unknown"
    this.inOp = false;
    this.chain = Promise.resolve();
    this.pending = 0;
    this.running = false;             // a script started by `run` is executing
    this.dec = new TextDecoder();
    this.onText = () => {};           // device output that no command is waiting for
    this.onState = () => {};
    this.onLost = () => {};
    this.lastAutoReset = 0;
  }
  get connected() { return !!this.port; }

  async open(port, baud) {
    await port.open({ baudRate: baud, bufferSize: 65536 });
    this.port = port; this.closing = false; this.mode = "unknown"; this.buf = new Uint8Array(0);
    /* release DTR/RTS: on ESP32 boards they drive EN/IO0 through the auto-reset circuit */
    try { await port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch {}
    this.writer = port.writable.getWriter();
    this.loop();
  }
  async loop() {
    const port = this.port, gen = this.gen = (this.gen || 0) + 1;
    let ended = false;
    while (port.readable && !this.closing && this.gen === gen && !ended) {
      this.reader = port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) { ended = true; break; }
          if (value && value.length) this.data(value);
        }
      } catch (e) {
        if (this.closing) break;     // a framing/overrun error recreates the stream: keep reading
      } finally { try { this.reader.releaseLock(); } catch {} }
    }
    if (!this.closing && this.gen === gen) this.lost();
  }
  lost() {
    const was = this.port;
    this.port = null; this.writer = null; this.mode = "unknown"; this.epoch++; this.kick();
    if (was) { try { was.close(); } catch {} this.onLost(); }
  }
  async close() {
    if (!this.port) return;
    this.closing = true;
    const port = this.port;
    this.port = null; this.epoch++; this.kick();
    try { await this.reader?.cancel(); } catch {}
    try { this.writer?.releaseLock(); } catch {}
    try { await port.close(); } catch {}
    this.writer = null; this.mode = "unknown";
  }
  async resetBoard() {
    /* esptool's hard reset: RTS low pulls EN low on boards with the usual
     * two-transistor circuit (and on the native USB-Serial-JTAG) */
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await sleep(120);
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    this.mode = "unknown";
  }
  data(v) {
    if (this.inOp) { this.buf = concat(this.buf, v); this.kick(); }
    else this.emit(v);
  }
  emit(bytes) {
    const text = this.dec.decode(bytes, { stream: true });
    if (/C# REPL\./.test(text)) this.mode = "repl";   // the board was reset / started in the REPL
    if (text) this.onText(text.replace(/\x04/g, ""));
  }
  /* the link went away (or was reopened) since the running operation started */
  dead() { return !this.port || this.opEpoch !== this.epoch; }
  kick() { const w = this.wake; this.wake = null; if (w) w(); }
  waitData(ms) {
    return new Promise((res) => {
      let t = 0;
      this.wake = () => { clearTimeout(t); res(true); };
      if (ms > 0) t = setTimeout(() => { this.wake = null; res(false); }, ms);
    });
  }
  async write(data) {
    if (!this.writer) throw new Error("Not connected");
    await this.writer.write(typeof data === "string" ? enc.encode(data) : data);
  }
  /* run fn with the line to ourselves; calls are queued */
  op(fn) {
    this.pending++; this.onState();
    const run = async () => {
      if (!this.port) throw new Error("Not connected");
      this.inOp = true; this.opEpoch = this.epoch;
      try { return await fn(); }
      finally {
        this.inOp = false;
        if (this.buf.length) { const b = this.buf; this.buf = new Uint8Array(0); this.emit(b); }
      }
    };
    const p = this.chain.then(run, run).finally(() => { this.pending--; this.onState(); });
    this.chain = p.catch(() => {});
    return p;
  }
  /* output until the next status line. onOut streams output (bytes); idle = ms without data (0 = wait forever) */
  async status({ onOut = null, idle = 6000 } = {}) {
    let out = new Uint8Array(0);
    for (;;) {
      const i = this.buf.indexOf(4);
      if (i >= 0) {
        const j = this.buf.indexOf(10, i);
        if (j >= 0) {
          const pre = this.buf.slice(0, i);
          const st = new TextDecoder().decode(this.buf.slice(i + 1, j)).replace(/\r$/, "");
          this.buf = this.buf.slice(j + 1);
          if (onOut) { if (pre.length) onOut(pre); } else out = concat(out, pre);
          return { status: st, out };
        }
      }
      if (onOut) { const k = i >= 0 ? i : this.buf.length; if (k) { onOut(this.buf.slice(0, k)); this.buf = this.buf.slice(k); } }
      if (this.dead()) throw new Error("Disconnected");
      if (!(await this.waitData(idle))) { this.mode = "unknown"; throw new TimeoutError("The device did not answer"); }
    }
  }
  /* Ctrl-C (stop a script / clear the line) + Ctrl-A (machine protocol).
   * If the board stays silent or sits in the ROM bootloader (opening the port
   * can toggle DTR/RTS, which drive EN/IO0 on most ESP32 boards), reset it
   * once into the normal firmware and try again. */
  async machine() {
    if (this.mode === "machine") return;
    let heard = new Uint8Array(0), didReset = false;
    for (let attempt = 0; attempt < 6; attempt++) {
      if (this.dead()) break;
      await this.write(Uint8Array.of(3, 1));
      const end = Date.now() + 1200 + Math.min(attempt, 2) * 800;
      for (;;) {
        const k = findSeq(this.buf, [4, 79, 75, 10], 0, true);
        if (k >= 0) {
          await sleep(30);                                 // a second OK may follow (Ctrl-C at the shell prompt is silent)
          const k2 = findSeq(this.buf, [4, 79, 75, 10], 0, true);
          const pre = new TextDecoder().decode(this.buf.slice(0, k2)).replace(/\x04[^\n]*\n/g, "");
          this.buf = this.buf.slice(k2 + 4);
          if (pre.replace(/\^C|> |\.\.\. |[\r\n ]/g, "").length) this.onText(pre);
          this.mode = "machine";
          return;
        }
        const left = end - Date.now();
        if (left <= 0 || this.dead()) break;
        await this.waitData(left);
      }
      heard = concat(heard, this.buf); this.buf = new Uint8Array(0);
      const text = new TextDecoder().decode(heard);
      const boot = /waiting for download|\(DOWNLOAD|download mode/i.test(text);
      if (!didReset && this.port && (boot || attempt === 1) && Date.now() - this.lastAutoReset > 30000) {
        didReset = true; this.lastAutoReset = Date.now();
        this.onText(boot ? "\n— the board is in its bootloader (download mode): resetting it —\n"
                         : "\n— no answer yet: resetting the board —\n");
        if (text.trim()) this.onText(text.replace(/\x04/g, "") + "\n");
        heard = new Uint8Array(0);
        await this.resetBoard();
        await sleep(1500);                                 // ROM + bootloader + MicroCS start
        if (this.dead()) throw new Error("The board restarted and its USB port went away - it will reconnect by itself.");
      }
    }
    const said = new TextDecoder().decode(heard).replace(/\x04/g, "").trim();
    if (said) this.onText("\n— the device sent: —\n" + said.slice(-600) + "\n");
    throw new Error(said
      ? "The device answers, but not like MicroCS (see the console). Is the MicroCS firmware running, at this baud rate?"
      : "No answer from the device (nothing was received). Check the port and baud rate, close other serial monitors, or press the board's RESET button and Connect again.");
  }
  async cmd(line, opts = {}) {
    await this.machine();
    await this.write(line + "\n");
    return this.status(opts);
  }
  async check(line, opts) {
    const r = await this.cmd(line, opts);
    if (r.status !== "OK") throw new Error(r.status.replace(/^ERR /, ""));
    return r;
  }
  async put(path, data, onProgress, chunk = 1024, gap = 0) {
    await this.machine();
    await this.write(`put ${path} ${data.length}\n`);
    const r = await this.status();
    if (r.status !== "READY") throw new Error(r.status.replace(/^ERR /, ""));
    for (let i = 0; i < data.length; i += chunk) {
      await this.write(data.subarray(i, Math.min(data.length, i + chunk)));
      if (onProgress) onProgress(Math.min(data.length, i + chunk), data.length);
      if (gap) await sleep(gap);
    }
    const s = await this.status({ idle: 20000 });
    if (s.status !== "OK") throw new Error(s.status.replace(/^ERR /, ""));
  }
  async get(path, onProgress) {
    await this.machine();
    await this.write(`get ${path}\n`);
    const r = await this.status();
    if (!r.status.startsWith("DATA ")) throw new Error(r.status.replace(/^ERR /, ""));
    const n = parseInt(r.status.slice(5), 10);
    while (this.buf.length < n) {
      if (onProgress) onProgress(this.buf.length, n);
      if (this.dead()) throw new Error("Disconnected");
      if (!(await this.waitData(8000))) { this.mode = "unknown"; throw new TimeoutError("Download stalled"); }
    }
    const data = this.buf.slice(0, n);
    this.buf = this.buf.slice(n);
    const s = await this.status();
    if (s.status !== "OK") throw new Error(s.status.replace(/^ERR /, ""));
    return data;
  }
  async ls(dir) {
    const r = await this.check("ls " + dir);
    const items = [];
    for (const line of new TextDecoder().decode(r.out).split("\n")) {
      const m = /^([fd])\s+(\S+)\s(.+?)\r?$/.exec(line);
      if (m) items.push({ name: m[3], dir: m[1] === "d", size: m[1] === "d" ? null : +m[2] });
    }
    return items;
  }
  async repl() {
    if (this.mode === "repl") return;
    await this.machine();
    await this.write("repl\n");
    const r = await this.status();
    if (r.status !== "OK") throw new Error(r.status.replace(/^ERR /, ""));
    /* swallow the banner and the first prompt - the console shows its own */
    const end = Date.now() + 1500;
    for (;;) {
      const k = findSeq(this.buf, [62, 32]);
      if (k >= 0) { this.buf = this.buf.slice(k + 2); break; }
      const left = end - Date.now();
      if (left <= 0) { this.buf = new Uint8Array(0); break; }
      await this.waitData(left);
    }
    this.mode = "repl";
  }
  stop() { if (this.writer) return this.write(Uint8Array.of(3)).catch(() => {}); }
}

/* ================================================================ console renderer
 * A small terminal: \r, \n, \b, tabs and ANSI colours (ESP-IDF logs) */
class Term {
  constructor(el) {
    this.el = el; this.max = 5000; this.cells = []; this.col = 0; this.sgr = "";
    this.escBuf = null; this.lineEl = null; this.dirty = false; this.onLink = null;
    el.addEventListener("click", (e) => {
      const a = e.target.closest(".link");
      if (a && this.onLink) this.onLink(a.dataset.path, +a.dataset.line);
    });
  }
  get lineText() { return this.cells.map((c) => (c ? c.c : " ")).join(""); }
  write(text, cls = "") {
    for (const ch of text) {
      if (this.escBuf !== null) {
        this.escBuf += ch;
        if (this.escBuf.length === 1 && ch !== "[") { this.escBuf = null; continue; }
        if (/[@-~]/.test(ch) && this.escBuf.length > 1) { this.csi(this.escBuf.slice(1)); this.escBuf = null; }
        else if (this.escBuf.length > 24) this.escBuf = null;
        continue;
      }
      if (ch === "\x1b") { this.escBuf = ""; continue; }
      if (ch === "\n") { this.newline(); continue; }
      if (ch === "\r") { this.col = 0; continue; }
      if (ch === "\b") { if (this.col > 0) this.col--; continue; }
      if (ch === "\t") { do this.put(" ", cls); while (this.col % 8); continue; }
      if (ch < " " || ch === "\x7f") continue;
      this.put(ch, cls);
    }
    this.schedule();
  }
  put(c, cls) { this.cells[this.col++] = { c, k: cls || this.sgr }; }
  csi(s) {
    const cmd = s.slice(-1), args = s.slice(0, -1).split(";").map((x) => parseInt(x, 10) || 0);
    if (cmd === "m") {
      for (const a of args) {
        if (a === 0) this.sgr = "";
        else if (a === 1) this.sgr = (this.sgr + " bold").trim();
        else if ((a >= 30 && a <= 37) || (a >= 90 && a <= 97)) this.sgr = this.sgr.replace(/c\d\d/, "").trim() + " c" + (a >= 90 ? a - 60 : a);
      }
    } else if (cmd === "K") this.cells.length = this.col;
    else if (cmd === "D") this.col = Math.max(0, this.col - (args[0] || 1));
    else if (cmd === "C") this.col += args[0] || 1;
    else if (cmd === "J" && args[0] === 2) this.clear();
  }
  render(el) {
    let html = "", run = "", k = null;
    const flush = () => { if (run) html += k ? `<span class="${k}">${esc(run)}</span>` : esc(run); run = ""; };
    for (const c of this.cells) { const ck = c ? c.k : ""; if (ck !== k) { flush(); k = ck; } run += c ? c.c : " "; }
    flush();
    el.innerHTML = html;
  }
  newline() {
    if (!this.lineEl) this.lineEl = this.addLine();
    this.render(this.lineEl);
    this.linkify(this.lineEl);
    this.lineEl = null; this.cells = []; this.col = 0;
    while (this.el.childElementCount > this.max) this.el.firstElementChild.remove();
    this.dirty = true;
  }
  addLine() { const d = document.createElement("div"); d.className = "l"; this.el.appendChild(d); return d; }
  schedule() {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => {
      this.raf = 0;
      const stick = this.el.scrollHeight - this.el.scrollTop - this.el.clientHeight < 40;
      if (this.cells.length || this.col) { if (!this.lineEl) this.lineEl = this.addLine(); this.render(this.lineEl); }
      if (stick || this.dirty) this.el.scrollTop = this.el.scrollHeight;
      this.dirty = false;
    });
  }
  /* "file.cs(3,5): error ..." and "... in /file.cs:line 3" open the file at that line */
  linkify(el) {
    const t = el.textContent;
    const m = /(\/[^\s():]+?\.cs)\((\d+),\d+\)/.exec(t) || /in (\/[^\s:]+?\.cs):line (\d+)/.exec(t);
    if (m && !m[1].startsWith("/.studio")) { el.classList.add("link"); el.dataset.path = m[1]; el.dataset.line = m[2]; el.title = "Open " + m[1] + " at line " + m[2]; }
    else if (m) { el.classList.add("link"); el.dataset.path = ""; el.dataset.line = m[2]; }
  }
  line(text, cls) { if (this.cells.length) this.newline(); this.write(text + "\n", cls); }
  clear() { this.el.innerHTML = ""; this.cells = []; this.col = 0; this.lineEl = null; }
}

/* ================================================================ C# highlighting (Visual Studio classification) */
const KEYWORDS = new Set(("abstract as base bool break byte case catch char checked class const continue decimal default delegate do double else enum " +
  "event explicit extern false finally fixed float for foreach goto if implicit in int interface internal is lock long namespace new null object " +
  "operator out override params private protected public readonly ref return sbyte sealed short sizeof stackalloc static string struct switch this " +
  "throw true try typeof uint ulong unchecked unsafe ushort using virtual void volatile while var dynamic async await get set value yield record " +
  "init when where nameof and or not with partial global").split(" "));
/* Visual Studio paints flow-control keywords in a separate colour */
const CONTROL = new Set("if else switch case default for foreach while do break continue return throw try catch finally goto yield await".split(" "));
const KNOWN_TYPES = new Set(("Exception IOException TimeoutException NotSupportedException ArgumentException ArgumentOutOfRangeException " +
  "ArgumentNullException InvalidOperationException IndexOutOfRangeException KeyNotFoundException FormatException DivideByZeroException " +
  "NullReferenceException NotImplementedException OverflowException StackOverflowException MissingMemberException IDisposable IEnumerable " +
  "IComparable Object String Int32 Double Boolean Char Byte System Flags").split(" "));
const TOKEN_SRC = /(\/\/[^\n]*)|(\/\*[\s\S]*?(?:\*\/|$))|(\$@"(?:[^"]|"")*"?|@\$"(?:[^"]|"")*"?|@"(?:[^"]|"")*"?|\$"(?:[^"\\\n{]|\\.|\{\{|\{(?:[^{}"\n]|"(?:[^"\\\n]|\\.)*")*\}?)*"?|"(?:[^"\\\n]|\\.)*"?)|('(?:[^'\\\n]|\\.){0,8}'?)|(\b0[xX][0-9a-fA-F_]+[uUlL]*|\b0[bB][01_]+[uUlL]*|\b\d[\d_]*(?:\.\d+)?(?:[eE][+-]?\d+)?[fFdDmMuUlL]*|\.\d+(?:[eE][+-]?\d+)?[fFdDmM]?)|(^[ \t]*#[a-z]+[^\n]*)|(@?[A-Za-z_][A-Za-z0-9_]*)|([{}()[\]])|([-+*/%=<>!&|^~?:]+)/gm;
let API = window.MCS_API || {};
const isType = (t) => !!API[t] || KNOWN_TYPES.has(t);
const span = (cls, t) => `<span class="t-${cls}">${esc(t)}</span>`;
/* string literal: escapes and interpolation holes */
function hlString(t) {
  const verbatim = /^(\$@|@\$|@)/.test(t), interp = t[0] === "$" || t[1] === "$";
  let out = "", buf = "", i = 0;
  const flush = () => { if (buf) { out += span("str", buf); buf = ""; } };
  while (i < t.length) {
    const c = t[i];
    if (!verbatim && c === "\\" && i + 1 < t.length) {
      const m = /^\\(u[0-9a-fA-F]{4}|x[0-9a-fA-F]{1,4}|.)/.exec(t.slice(i));
      flush(); out += span("esc", m[0]); i += m[0].length; continue;
    }
    if (interp && c === "{") {
      if (t[i + 1] === "{") { flush(); out += span("esc", "{{"); i += 2; continue; }
      let j = i + 1, depth = 1, q = false;
      for (; j < t.length && depth; j++) { const d = t[j]; if (d === '"' && t[j - 1] !== "\\") q = !q; else if (!q && d === "{") depth++; else if (!q && d === "}") depth--; }
      flush();
      const inner = t.slice(i + 1, depth ? j : j - 1);
      const fm = /(,\s*-?\d+)?(:(?:[A-Za-z]{1,2}\d*|[#0.,%]+))?$/.exec(inner);     // {value,alignment:format}
      const cut = fm && fm[0] && fm.index > 0 ? fm.index : inner.length;
      out += span("br", "{") + highlight(inner.slice(0, cut)) + (fm && fm[1] && cut < inner.length ? highlight(fm[1]) : "") +
        (fm && fm[2] && cut < inner.length ? span("str", fm[2]) : "") + (depth ? "" : span("br", "}"));
      i = j; continue;
    }
    if (interp && c === "}" && t[i + 1] === "}") { flush(); out += span("esc", "}}"); i += 2; continue; }
    buf += c; i++;
  }
  flush();
  return out;
}
function highlight(src) {
  const re = new RegExp(TOKEN_SRC.source, "gm");
  let html = "", last = 0, m, prevSig = "";
  while ((m = re.exec(src))) {
    if (m.index > last) { const gap = src.slice(last, m.index); html += esc(gap); if (gap.trim()) prevSig = gap.trim().slice(-1); }
    const t = m[0];
    let cls = null;
    if (m[1] || m[2]) cls = "com";
    else if (m[3]) { html += hlString(t); last = re.lastIndex; prevSig = '"'; continue; }
    else if (m[4]) { html += t.length > 3 && t[1] === "\\" ? span("str", "'") + span("esc", t.slice(1, -1)) + span("str", "'") : span("str", t); last = re.lastIndex; prevSig = "'"; continue; }
    else if (m[5]) cls = "num";
    else if (m[6]) cls = "pp";
    else if (m[7]) {
      const id = t[0] === "@" ? t.slice(1) : t;
      const after = src.slice(re.lastIndex, re.lastIndex + 40);
      const dot = prevSig === ".";
      if (KEYWORDS.has(id) && t[0] !== "@") cls = CONTROL.has(id) ? "ctl" : "kw";
      else if (/^\s*\(/.test(after)) cls = prevSig === "new" || (isType(id) && !dot) ? "ty" : "fn";
      else if (/^\s*<[\w\s,<>\[\]]*>\s*\(/.test(after) && /^[A-Z]/.test(id) && !isType(id)) cls = prevSig === "new" ? "ty" : "fn";      // generic call
      else if (dot) cls = isType(id) ? "ty" : null;                                                        // member: property / field / constant
      else if (isType(id)) cls = "ty";
      else if (/^[A-Z]/.test(id) && (prevSig === "new" || /^(class|struct|interface|enum|:)$/.test(prevSig) || /^\s*(<|\[\]|\.[A-Z]|[A-Za-z_]\w*\s*[=;,)\]{]|[A-Za-z_]\w*\s*$|\?\s+[A-Za-z_])/.test(after))) cls = "ty";
      else if (/^[A-Z]/.test(id) && /^[<,]$/.test(prevSig) && /^\s*(>|,|\[\])/.test(after)) cls = "ty";              // generic argument
      else if (/^[a-z_]/.test(id)) cls = "loc";
      if (cls === "ty" && /^I[A-Z][a-z]/.test(id)) cls = "if";                                             // interface
      prevSig = t;
    } else if (m[8]) cls = "br";
    else if (m[9]) cls = "op";
    if (!m[7]) prevSig = m[1] || m[2] ? prevSig : t;
    html += cls ? span(cls, t) : esc(t);
    last = re.lastIndex;
  }
  html += esc(src.slice(last));
  return html;
}
/* plain text files (.cfg, .txt, .csv...): # and // comments only */
function highlightPlain(src) {
  return src.split("\n").map((l) => /^\s*(#|\/\/)/.test(l) ? span("com", l) : esc(l)).join("\n");
}

/* ================================================================ editor */
class Editor {
  constructor() {
    this.ta = $("ta"); this.hl = $("hl"); this.gutter = $("gutterIn"); this.cur = $("curline"); this.errEl = $("errline");
    this.box = $("complete"); this.tab = null; this.lines = 0; this.errLine = 0;
    this.items = []; this.sel = 0; this.ctx = null;
    this.onChange = () => {}; this.onCursor = () => {};
    const ta = this.ta;
    ta.addEventListener("input", (e) => { this.refresh(); this.onChange(); if (e.inputType === "insertText" && /[\w.]/.test(e.data || "")) this.suggest(false); else if (e.inputType !== "insertText") this.hideComplete(); });
    ta.addEventListener("scroll", () => this.syncScroll());
    ta.addEventListener("keydown", (e) => this.key(e));
    ta.addEventListener("blur", () => setTimeout(() => { if (document.activeElement !== ta) this.hideComplete(); }, 150));
    for (const ev of ["click", "keyup", "select"]) ta.addEventListener(ev, () => this.cursorMoved());
    this.box.addEventListener("mousedown", (e) => { const d = e.target.closest("div[data-i]"); if (d) { e.preventDefault(); this.sel = +d.dataset.i; this.accept(); } });
    new ResizeObserver(() => this.syncScroll()).observe(ta);
    this.measure();
  }
  measure() {
    const s = document.createElement("span");
    s.style.cssText = "position:absolute;visibility:hidden;white-space:pre;font:var(--fs)/var(--lh) var(--mono)";
    s.textContent = "x".repeat(100);
    document.body.appendChild(s);
    this.cw = s.getBoundingClientRect().width / 100;
    this.lh = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--lh")) || 19;
    s.remove();
  }
  open(tab) {
    if (this.tab) this.stash();
    this.tab = tab;
    this.hideComplete();
    this.setError(0);
    this.ta.value = tab.text;
    this.ta.readOnly = !!tab.readOnly;
    this.refresh(true);
    requestAnimationFrame(() => {
      this.ta.setSelectionRange(tab.sel?.[0] || 0, tab.sel?.[1] || 0);
      this.ta.scrollTop = tab.scroll?.[0] || 0; this.ta.scrollLeft = tab.scroll?.[1] || 0;
      this.syncScroll(); this.cursorMoved();
      this.ta.focus({ preventScroll: true });
    });
  }
  stash() {
    if (!this.tab) return;
    this.tab.text = this.ta.value;
    this.tab.sel = [this.ta.selectionStart, this.ta.selectionEnd];
    this.tab.scroll = [this.ta.scrollTop, this.ta.scrollLeft];
  }
  get value() { return this.ta.value; }
  refresh(force) {
    const v = this.ta.value;
    if (this.tab) this.tab.text = v;
    if (this.raf && !force) return;
    const draw = () => {
      this.raf = 0;
      const val = this.ta.value;
      const nm = this.tab ? (this.tab.path || this.tab.name || "") : "";
      this.hl.innerHTML = (!nm || /\.cs$/i.test(nm) ? highlight(val) : highlightPlain(val)) + "\n";
      const n = val.split("\n").length;
      if (n !== this.lines || force) { this.lines = n; this.drawGutter(); }
      this.cursorMoved();
    };
    if (force) draw(); else this.raf = requestAnimationFrame(draw);
  }
  drawGutter() {
    let h = "";
    for (let i = 1; i <= this.lines; i++) h += `<div${i === this.errLine ? ' class="errl"' : ""}>${i}</div>`;
    this.gutter.innerHTML = h;
    this.curGut = null;
  }
  syncScroll() {
    const t = this.ta.scrollTop, l = this.ta.scrollLeft;
    this.hl.style.transform = `translate(${-l}px,${-t}px)`;
    this.gutter.style.transform = `translateY(${-t}px)`;
    this.placeLine(this.cur, this.curRow || 0);
    if (this.errLine) this.placeLine(this.errEl, this.errLine - 1);
    if (this.box.style.display === "block") this.placeBox();
  }
  placeLine(el, row) { el.style.transform = `translateY(${6 + row * this.lh - this.ta.scrollTop}px)`; }
  caret() {
    const p = this.ta.selectionStart, v = this.ta.value;
    const before = v.slice(0, p), row = before.split("\n").length - 1, col = p - before.lastIndexOf("\n") - 1;
    return { row, col, p };
  }
  cursorMoved() {
    const { row, col } = this.caret();
    this.curRow = row;
    this.placeLine(this.cur, row);
    const g = this.gutter.children[row];
    if (this.curGut !== g) { this.curGut?.classList.remove("cur"); g?.classList.add("cur"); this.curGut = g; }
    this.onCursor(row + 1, col + 1);
  }
  setError(line) {
    this.errLine = line;
    this.errEl.style.display = line ? "block" : "none";
    this.drawGutter();
    if (line) this.placeLine(this.errEl, line - 1);
  }
  gotoLine(line) {
    const v = this.ta.value; let p = 0;
    for (let i = 1; i < line && p >= 0; i++) p = v.indexOf("\n", p) + 1;
    if (p < 0) p = v.length;
    const e = v.indexOf("\n", p);
    this.ta.focus();
    this.ta.setSelectionRange(p, e < 0 ? v.length : e);
    const top = (line - 1) * this.lh - this.ta.clientHeight / 3;
    this.ta.scrollTop = Math.max(0, top);
    this.cursorMoved();
  }
  /* insert keeping the browser's undo history */
  insert(text, selStart, selEnd) {
    const ta = this.ta;
    if (selStart != null) ta.setSelectionRange(selStart, selEnd);
    if (!document.execCommand("insertText", false, text)) ta.setRangeText(text, ta.selectionStart, ta.selectionEnd, "end");
    this.refresh();
  }
  lineRange() {
    const v = this.ta.value, s = this.ta.selectionStart, e = this.ta.selectionEnd;
    const a = v.lastIndexOf("\n", s - 1) + 1;
    let b = v.indexOf("\n", e > s && v[e - 1] === "\n" ? e - 1 : e); if (b < 0) b = v.length;
    return [a, b];
  }
  key(e) {
    const ta = this.ta, v = ta.value, s = ta.selectionStart, en = ta.selectionEnd;
    const mod = e.ctrlKey || e.metaKey;
    if (this.box.style.display === "block") {
      if (e.key === "ArrowDown") { e.preventDefault(); this.sel = (this.sel + 1) % this.items.length; this.drawBox(); return; }
      if (e.key === "ArrowUp") { e.preventDefault(); this.sel = (this.sel - 1 + this.items.length) % this.items.length; this.drawBox(); return; }
      if (e.key === "Enter" || e.key === "Tab") { e.preventDefault(); this.accept(); return; }
      if (e.key === "Escape") { e.preventDefault(); this.hideComplete(); return; }
    }
    if (ta.readOnly) return;
    if (mod && e.key === " ") { e.preventDefault(); this.suggest(true); return; }
    if (mod && e.key === "/") { e.preventDefault(); this.toggleComment(); return; }
    if (mod && (e.key === "d" || e.key === "D") && !e.shiftKey) {
      e.preventDefault(); const [a, b] = this.lineRange(); const t = v.slice(a, b);
      this.insert("\n" + t, b, b); ta.setSelectionRange(s + t.length + 1, en + t.length + 1); return;
    }
    if (e.key === "Tab") {
      e.preventDefault();
      if (s !== en && v.slice(s, en).includes("\n") || e.shiftKey) {
        const [a, b] = this.lineRange();
        const lines = v.slice(a, b).split("\n");
        const out = lines.map((l) => (e.shiftKey ? l.replace(/^( {1,4}|\t)/, "") : "    " + l)).join("\n");
        this.insert(out, a, b);
        ta.setSelectionRange(a, a + out.length);
      } else {
        const col = s - v.lastIndexOf("\n", s - 1) - 1;
        this.insert(" ".repeat(4 - (col % 4)));
      }
      return;
    }
    if (e.key === "Enter" && !mod && !e.altKey) {
      e.preventDefault();
      const ls = v.lastIndexOf("\n", s - 1) + 1;
      const ind = /^[ \t]*/.exec(v.slice(ls, s))[0];
      const prev = v.slice(ls, s).trimEnd(), next = v[en];
      const open = /[{([]$/.test(prev);
      if (open && "})]".includes(next || "x")) {
        this.insert("\n" + ind + "    " + "\n" + ind);
        const c = s + 1 + ind.length + 4; ta.setSelectionRange(c, c);
      } else this.insert("\n" + ind + (open ? "    " : ""));
      return;
    }
    if (e.key === "}" && s === en) {
      const ls = v.lastIndexOf("\n", s - 1) + 1;
      if (/^[ \t]+$/.test(v.slice(ls, s)) && v[s] !== "}") {
        e.preventDefault();
        const ind = v.slice(ls, s);
        this.insert(ind.slice(0, Math.max(0, ind.length - 4)) + "}", ls, s); return;
      }
    }
    const pairs = { "(": ")", "[": "]", "{": "}", '"': '"', "'": "'" };
    if (!mod && pairs[e.key]) {
      if (s !== en && e.key !== "'") { e.preventDefault(); const sel = v.slice(s, en); this.insert(e.key + sel + pairs[e.key]); ta.setSelectionRange(s + 1, s + 1 + sel.length); return; }
      const nx = v[s] || "", pv = v[s - 1] || "";
      if ((e.key === '"' || e.key === "'") && nx === e.key) { e.preventDefault(); ta.setSelectionRange(s + 1, s + 1); return; }
      if (e.key === "'" ) return;
      if (e.key === '"' && (/\w/.test(pv) || this.inString(s))) return;
      if (/^[\s)\]};,.]?$/.test(nx)) { e.preventDefault(); this.insert(e.key + pairs[e.key]); ta.setSelectionRange(s + 1, s + 1); return; }
    }
    if (!mod && ")]}".includes(e.key) && e.key.length === 1 && s === en && v[s] === e.key) { e.preventDefault(); ta.setSelectionRange(s + 1, s + 1); return; }
    if (e.key === "Backspace" && s === en && s > 0) {
      const a = v[s - 1], b = v[s];
      if (pairs[a] && pairs[a] === b) { e.preventDefault(); this.insert("", s - 1, s + 1); return; }
      const ls = v.lastIndexOf("\n", s - 1) + 1, before = v.slice(ls, s);
      if (before.length >= 4 && /^ +$/.test(before)) { e.preventDefault(); this.insert("", s - (before.length % 4 || 4), s); return; }
    }
  }
  inString(p) {
    const line = this.ta.value.slice(this.ta.value.lastIndexOf("\n", p - 1) + 1, p);
    return (line.replace(/\\./g, "").split('"').length - 1) % 2 === 1;
  }
  toggleComment() {
    const ta = this.ta, v = ta.value, [a, b] = this.lineRange();
    const lines = v.slice(a, b).split("\n");
    const all = lines.filter((l) => l.trim()).every((l) => /^\s*\/\//.test(l));
    const ind = Math.min(...lines.filter((l) => l.trim()).map((l) => /^\s*/.exec(l)[0].length), 1e9);
    const out = lines.map((l) => (!l.trim() ? l : all ? l.replace(/^(\s*)\/\/ ?/, "$1") : l.slice(0, ind) + "// " + l.slice(ind))).join("\n");
    this.insert(out, a, b);
    ta.setSelectionRange(a, a + out.length);
  }

  /* ---- completion */
  suggest(explicit) {
    const ta = this.ta, v = ta.value, p = ta.selectionStart;
    if (p !== ta.selectionEnd) return this.hideComplete();
    const line = v.slice(v.lastIndexOf("\n", p - 1) + 1, p);
    if (this.inString(p) || /\/\//.test(line.replace(/"(?:[^"\\]|\\.)*"/g, ""))) return this.hideComplete();
    const mm = /([A-Za-z_]\w*)\s*\.\s*(\w*)$/.exec(line);
    let prefix, items = [];
    if (mm) {
      prefix = mm[2];
      const obj = mm[1];
      let names = [];
      if (API[obj]) names = API[obj].s.map((n) => ({ label: n, kind: "m", detail: obj }));
      else {
        const t = this.typeOf(obj, v);
        if (t && API[t]) names = API[t].i.map((n) => ({ label: n, kind: "m", detail: t }));
        else names = [...new Set([...(API["*"]?.i || []), ...Object.values(API).flatMap((c) => c.i)])].sort().map((n) => ({ label: n, kind: "m", detail: "" }));
      }
      items = names;
    } else {
      const w = /([A-Za-z_]\w*)$/.exec(line);
      prefix = w ? w[1] : "";
      if (!explicit && prefix.length < 2) return this.hideComplete();
      if (/^\d/.test(prefix)) return this.hideComplete();
      const seen = new Set();
      const add = (label, kind, detail = "", text = null) => { if (!seen.has(label + kind)) { seen.add(label + kind); items.push({ label, kind, detail, text }); } };
      for (const s of SNIPPETS) add(s.label, "s", s.detail, s.text);
      for (const c of Object.keys(API)) if (c !== "*") add(c, "c", "class");
      for (const k of KEYWORDS) add(k, "k", "keyword");
      for (const id of new Set(v.match(/[A-Za-z_]\w{2,}/g) || [])) if (id !== prefix && !KEYWORDS.has(id) && !API[id]) add(id, "w", "");
    }
    const lp = prefix.toLowerCase();
    let list = items.filter((it) => it.label.toLowerCase().startsWith(lp) && it.label !== prefix);
    if (lp.length > 1) list = list.concat(items.filter((it) => !it.label.toLowerCase().startsWith(lp) && it.label.toLowerCase().includes(lp)));
    const order = { s: 0, m: 1, c: 2, k: 3, w: 4 };
    list.sort((a, b) => (a.label.toLowerCase().startsWith(lp) ? 0 : 1) - (b.label.toLowerCase().startsWith(lp) ? 0 : 1) || (a.label.startsWith(prefix) ? 0 : 1) - (b.label.startsWith(prefix) ? 0 : 1) || order[a.kind] - order[b.kind] || a.label.length - b.label.length);
    list = list.slice(0, 60);
    if (!list.length || (!explicit && !mm && list.length === 1 && list[0].label.toLowerCase() === lp)) return this.hideComplete();
    this.items = list; this.sel = 0; this.ctx = { start: p - prefix.length, end: p };
    this.drawBox(); this.placeBox();
  }
  typeOf(name, src) {
    const re1 = new RegExp(`\\b(?:var\\s+)?${name}\\s*=\\s*new\\s+([A-Z]\\w*)`), re2 = new RegExp(`\\b([A-Z]\\w*)(?:<[^>]*>)?\\s+${name}\\b`);
    const m = re1.exec(src) || re2.exec(src);
    return m ? m[1] : null;
  }
  drawBox() {
    const tag = { k: "K", c: "C", m: "M", w: "ab", s: "⇥" };
    this.box.innerHTML = this.items.map((it, i) => `<div data-i="${i}" class="${i === this.sel ? "on" : ""}"><i class="${it.kind}">${tag[it.kind]}</i>${esc(it.label)}<em>${esc(it.detail || "")}</em></div>`).join("");
    this.box.style.display = "block";
    this.box.children[this.sel]?.scrollIntoView({ block: "nearest" });
  }
  placeBox() {
    const v = this.ta.value, p = this.ctx.start;
    const before = v.slice(0, p), row = before.split("\n").length - 1, col = p - before.lastIndexOf("\n") - 1;
    const x = 12 + col * this.cw - this.ta.scrollLeft, y = 6 + (row + 1) * this.lh - this.ta.scrollTop + 2;
    const w = this.ta.clientWidth, h = this.ta.clientHeight;
    this.box.style.left = Math.max(4, Math.min(x, w - 240)) + "px";
    this.box.style.top = (y + 240 > h && y > 260 ? y - this.lh - 244 : y) + "px";
  }
  hideComplete() { this.box.style.display = "none"; this.ctx = null; }
  accept() {
    const it = this.items[this.sel], c = this.ctx;
    this.hideComplete();
    if (!it || !c) return;
    if (it.text) {
      const v = this.ta.value, ls = v.lastIndexOf("\n", c.start - 1) + 1, ind = /^[ \t]*/.exec(v.slice(ls, c.start))[0];
      const text = it.text.replace(/\n/g, "\n" + ind), k = text.indexOf("$0");
      this.insert(text.replace("$0", ""), c.start, this.ta.selectionStart);
      const pos = c.start + (k < 0 ? text.length : k);
      this.ta.setSelectionRange(pos, pos);
    } else this.insert(it.label, c.start, this.ta.selectionStart);
    this.cursorMoved();
  }
}

/* ================================================================ app */
const dev = new Device();
const term = new Term($("term"));
const ed = new Editor();
const store = {
  get(k, d) { try { const v = localStorage.getItem("mcs-studio." + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem("mcs-studio." + k, JSON.stringify(v)); } catch {} },
};
const App = {
  cwd: "/", entries: [], tabs: [], active: null, consMode: store.get("consMode", "repl"),
  history: store.get("history", []), hpos: -1, echoQ: "", untitled: 1, info: "",

  /* ---------- connection */
  async connect() {
    if (dev.connected) return this.disconnect();
    if (this.autoReconnect && this.lastPort && $("btnConnect").querySelector("span").textContent === "Cancel") { this.autoReconnect = false; this.setConn("off", "Not connected"); return; }
    if (!("serial" in navigator)) { toast("Web Serial is not available - use Chrome or Edge on a desktop.", "err", 6000); return; }
    let port;
    try { port = await navigator.serial.requestPort(); } catch { return; }
    await this.openPort(port);
  },
  async openPort(port, again) {
    const baud = +$("baud").value;
    try {
      this.setConn("busy", again ? "Reconnecting…" : "Connecting…");
      await dev.open(port, baud);
    } catch (e) { this.setConn("off", "Not connected"); if (!again) toast("Could not open the port: " + e.message, "err", 6000); return; }
    this.lastPort = port; this.autoReconnect = true;
    store.set("baud", baud);
    term.line(`— ${again ? "reconnected" : "connected"} at ${baud} baud —`, "sys");
    if (again) await sleep(800);       // let the board finish booting
    await this.afterConnect();
  },
  async afterConnect() {
    try {
      await dev.op(() => dev.machine());
      const r = await dev.op(() => dev.cmd("info"));
      this.info = (new TextDecoder().decode(r.out).trim().split("\n")[0] || "MicroCS").replace(/ features=.*/, "");
      this.setConn("on", this.info);
      term.line(`${this.info} — ready`, "ok");
    } catch (e) {
      this.setConn("on", "Connected (no answer)");
      term.line(e.message, "err");
      toast(e.message, "err", 7000);
    }
    this.enable(true);
    await this.refresh();
    $("cin").focus();
  },
  async disconnect() {
    this.autoReconnect = false;
    await dev.close();
    term.line("— disconnected —", "sys");
    this.setConn("off", "Not connected");
    this.enable(false);
  },
  setConn(state, text) {
    $("connDot").className = "dot" + (state === "on" ? " on" : state === "busy" ? " busy" : "");
    $("connText").textContent = text;
    $("sbConn").textContent = state === "on" ? "Connected · " + text : state === "busy" ? text : "Offline";
    document.querySelector("footer").classList.toggle("off", state === "off");
    const b = $("btnConnect");
    b.classList.toggle("primary", state !== "on");
    b.querySelector("span").textContent = state === "on" ? "Disconnect" : "Connect";
  },
  enable(on) {
    for (const id of ["btnNewFile", "btnNewDir", "btnUpload", "btnRefresh", "btnReset", "btnCtrlC", "cin"]) $(id).disabled = !on;
    this.updateButtons();
    if (!on) { this.entries = []; this.renderFiles(); $("stText").textContent = "—"; $("stBar").style.width = "0"; $("memInfo").textContent = ""; }
  },
  updateButtons() {
    const t = this.tab(), c = dev.connected;
    $("btnSave").disabled = !t || t.readOnly || (!t.dirty && t.path && !t.local) || (!c && !t.local);
    $("btnRun").disabled = !c || !t || t.binary && extOf(t.path || "") !== "mcsb";
    $("btnStop").disabled = !c || !dev.running;
    $("sbBusy").textContent = dev.running ? "▶ running" : dev.pending ? "● working…" : "Ready";
    document.querySelector("footer").classList.toggle("run", !!(c && dev.running));     // orange while a script runs, like debugging in VS
  },
  async reset() {
    if (!dev.connected) return;
    try {
      term.line("— resetting the board —", "sys");
      await dev.resetBoard();
      await sleep(1800);
      if (dev.connected) await this.afterConnect();     // native USB ports vanish on reset: auto-reconnect takes over
    } catch (e) { toast("Reset failed: " + e.message, "err"); }
  },

  /* ---------- files */
  async refresh(quiet) {
    if (!dev.connected) return;
    try {
      this.entries = await dev.op(() => dev.ls(this.cwd));
    } catch (e) {
      if (this.cwd !== "/" && /no such|not found/i.test(e.message)) { this.cwd = "/"; return this.refresh(quiet); }
      if (!quiet) toast("ls: " + e.message, "err");
      this.entries = [];
    }
    this.renderFiles();
    this.refreshStorage();
  },
  async refreshStorage() {
    try {
      const r = await dev.op(() => dev.cmd("df " + this.cwd));
      const t = new TextDecoder().decode(r.out);
      const m = /^(\S+) (\S+) (\d+) KB total, (\d+) KB used, (\d+) KB free/m.exec(t);
      if (r.status === "OK" && m) {
        const total = +m[3] * 1024, used = +m[4] * 1024, pct = total ? (used / total) * 100 : 0;
        $("stLabel").textContent = `Storage · ${m[2]}`;
        $("stText").textContent = `${fmtBytes(total - used)} free of ${fmtBytes(total)}`;
        $("stBar").style.width = Math.max(1, pct).toFixed(1) + "%";
        $("stBarWrap").classList.toggle("full", pct > 90);
      } else { $("stLabel").textContent = "Storage"; $("stText").textContent = "size unknown"; }
      const mm = await dev.op(() => dev.cmd("mem"));
      const ms = /heap (\d+) bytes in use, peak (\d+)/.exec(new TextDecoder().decode(mm.out));
      $("memInfo").textContent = ms ? `VM heap: ${fmtBytes(+ms[1])} in use · peak ${fmtBytes(+ms[2])}` : "";
    } catch {}
  },
  renderFiles() {
    const crumbs = $("crumbs");
    const parts = this.cwd.split("/").filter(Boolean);
    let html = `<a data-p="/">${icon("chip")}</a>`, acc = "";
    for (const p of parts) { acc += "/" + p; html += `<span>/</span><a data-p="${esc(acc)}">${esc(p)}</a>`; }
    crumbs.innerHTML = html;
    crumbs.querySelectorAll("a").forEach((a) => (a.onclick = () => this.cd(a.dataset.p)));
    const list = $("fileList");
    const items = [...this.entries].filter((e) => !e.name.endsWith(".part") && !e.name.startsWith(".studio")).sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
    if (!dev.connected) { list.innerHTML = `<div class="empty"><b>No device connected</b><br>Connect a board running MicroCS to browse its files.</div>`; return; }
    if (!items.length) { list.innerHTML = `<div class="empty"><b>This folder is empty</b><br>Drop files here, or create one with the buttons above.</div>`; return; }
    list.innerHTML = "";
    if (this.cwd !== "/") items.unshift({ name: "..", dir: true, up: true });
    for (const it of items) {
      const path = it.up ? dirName(this.cwd) : joinPath(this.cwd, it.name);
      const ext = extOf(it.name);
      const kind = it.dir ? "dir" : ext === "cs" ? "cs" : ext === "mcsb" ? "img" : "txt";
      const row = document.createElement("div");
      row.className = "file" + (this.tab()?.path === path ? " active" : "");
      row.title = path;
      row.innerHTML = `<span class="ficon ${kind}">${icon(it.dir ? "folder" : kind === "cs" ? "code" : kind === "img" ? "chip" : "file")}</span>` +
        `<span class="name">${esc(it.name)}</span>` + (it.dir ? "" : `<span class="size">${fmtBytes(it.size)}</span>`) +
        (it.up ? "" : `<span class="acts">${kind === "cs" || kind === "img" ? `<button data-a="run" title="Run">${icon("play")}</button>` : ""}` +
          (it.dir ? "" : `<button data-a="dl" title="Download">${icon("download")}</button>`) +
          `<button data-a="mv" title="Rename / move">${icon("edit")}</button><button data-a="rm" class="del" title="Delete">${icon("trash")}</button></span>`);
      row.onclick = (e) => {
        const a = e.target.closest("button")?.dataset.a;
        if (a === "run") this.runPath(path);
        else if (a === "dl") this.downloadPath(path);
        else if (a === "mv") this.rename(path);
        else if (a === "rm") this.remove(path, it.dir);
        else if (it.dir) this.cd(path);
        else this.openPath(path, it.size);
      };
      row.oncontextmenu = (e) => { if (it.up) return; e.preventDefault(); this.fileMenu(e.clientX, e.clientY, path, it, kind); };
      list.appendChild(row);
    }
  },
  fileMenu(x, y, path, it, kind) {
    const items = [{ header: baseName(path) }];
    if (it.dir) items.push({ label: "Open folder", icon: "folder", run: () => this.cd(path) });
    else {
      items.push({ label: "Open in editor", icon: "edit", run: () => this.openPath(path, it.size) });
      if (kind === "cs" || kind === "img") items.push({ label: "Run", icon: "play", run: () => this.runPath(path) });
      if (kind === "cs" && path !== "/main.cs") items.push({ label: "Run at boot (copy to /main.cs)", icon: "boot", run: () => this.copyTo(path, "/main.cs") });
      items.push({ label: "Download", icon: "download", run: () => this.downloadPath(path) });
      items.push({ label: "Duplicate", icon: "copy", run: () => this.duplicate(path) });
    }
    items.push({ label: "Rename / move", icon: "edit", run: () => this.rename(path) });
    items.push({ label: "Copy path", icon: "copy", run: () => navigator.clipboard?.writeText(path) });
    items.push("-", { label: "Delete", icon: "trash", danger: true, run: () => this.remove(path, it.dir) });
    Menu.show(x, y, items);
  },
  cd(path) { this.cwd = normPath(path); this.refresh(); },
  checkName(path) {
    if (/\s/.test(path)) { toast("Names cannot contain spaces (the shell protocol separates arguments with spaces).", "err", 5000); return false; }
    if (path.length > 100) { toast("Path too long.", "err"); return false; }
    return true;
  },
  async newFile() {
    const name = await dialog({ title: "New file", text: `Create a file in ${this.cwd}`, value: "app.cs", select: [0, 3], ok: "Create" });
    if (!name) return;
    const path = normPath(name.startsWith("/") ? name : joinPath(this.cwd, name));
    if (!this.checkName(path)) return;
    if (this.entries.some((e) => joinPath(this.cwd, e.name) === path)) return this.openPath(path);
    const text = extOf(path) === "cs" ? `// ${baseName(path)}\nConsole.WriteLine("Hello from ${baseName(path)}");\n` : "";
    try {
      await dev.op(() => dev.put(path, enc.encode(text)));
      await this.refresh(true);
      this.openTab({ path, text, saved: text });
    } catch (e) { toast("Create failed: " + e.message, "err"); }
  },
  async newDir() {
    const name = await dialog({ title: "New folder", text: `Create a folder in ${this.cwd}`, value: "scripts", ok: "Create" });
    if (!name) return;
    const path = normPath(name.startsWith("/") ? name : joinPath(this.cwd, name));
    if (!this.checkName(path)) return;
    try { await dev.op(() => dev.check("mkdir " + path)); this.refresh(); } catch (e) { toast("mkdir: " + e.message, "err"); }
  },
  async rename(path) {
    const to = await dialog({ title: "Rename / move", text: "New name or full path", value: path, select: [path.lastIndexOf("/") + 1, path.length - (extOf(path) ? extOf(path).length + 1 : 0)], ok: "Rename" });
    if (!to) return;
    const dst = normPath(to.startsWith("/") ? to : joinPath(dirName(path), to));
    if (dst === path || !this.checkName(dst)) return;
    try {
      await dev.op(() => dev.check(`mv ${path} ${dst}`));
      for (const t of this.tabs) if (t.path === path) { t.path = dst; }
      this.renderTabs(); this.refresh();
      toast(`Renamed to ${dst}`, "ok");
    } catch (e) { toast("Rename failed: " + e.message, "err"); }
  },
  async remove(path, isDir) {
    const ok = await dialog({ title: `Delete ${baseName(path)}?`, text: isDir ? `The folder ${path} and everything in it will be deleted from the device.` : `${path} will be deleted from the device.`, ok: "Delete", danger: true });
    if (!ok) return;
    try {
      await dev.op(async () => {
        const rmTree = async (p, dir) => {
          if (dir) for (const c of await dev.ls(p)) await rmTree(joinPath(p, c.name), c.dir);
          await dev.check("rm " + p);
        };
        await rmTree(path, isDir);
      });
      for (const t of this.tabs) if (t.path === path || t.path?.startsWith(path + "/")) { t.dirty = true; t.saved = null; }
      this.renderTabs(); this.refresh();
      toast(`Deleted ${path}`, "ok");
    } catch (e) { toast("Delete failed: " + e.message, "err"); this.refresh(true); }
  },
  async upload(files) {
    if (!dev.connected) return toast("Connect a device first.", "err");
    for (const f of files) {
      const name = f.name.replace(/\s+/g, "_");
      const path = joinPath(this.cwd, name);
      const data = new Uint8Array(await f.arrayBuffer());
      try {
        await this.transfer(`↑ ${name}`, (prog) => this.putReliable(path, data, prog));
        term.line(`uploaded ${path} (${fmtBytes(data.length)})`, "sys");
        const t = this.tabs.find((t) => t.path === path);
        if (t && !t.dirty && isText(data)) { t.text = t.saved = new TextDecoder().decode(data); if (t === this.tab()) ed.open(t); }
      } catch (e) { toast(`Upload of ${name} failed: ${e.message}`, "err", 6000); break; }
    }
    this.refresh(true);
  },
  /* uploads go through the device's UART buffer: retry slower if bytes were lost */
  async putReliable(path, data, prog) {
    try { return await dev.op(() => dev.put(path, data, prog)); }
    catch (e) {
      if (!/timed out|did not answer|stalled/i.test(e.message)) throw e;
      term.line("upload stalled - retrying slower", "warn");
      return dev.op(() => dev.put(path, data, prog, 128, 12));
    }
  },
  async transfer(label, fn) {
    const x = $("xfer");
    $("xferName").textContent = label; $("xferPct").textContent = "0%"; $("xferBar").style.width = "0";
    x.classList.add("show");
    const prog = (a, b) => { const p = b ? Math.round((a / b) * 100) : 100; $("xferPct").textContent = `${fmtBytes(a)} · ${p}%`; $("xferBar").style.width = p + "%"; };
    try { return await fn(prog); } finally { setTimeout(() => x.classList.remove("show"), 500); }
  },
  async fetchFile(path) { return this.transfer(`↓ ${baseName(path)}`, (prog) => dev.op(() => dev.get(path, prog))); },
  async downloadPath(path) {
    try { download(baseName(path), await this.fetchFile(path)); } catch (e) { toast("Download failed: " + e.message, "err"); }
  },
  async copyTo(src, dst) {
    try {
      const t = this.tabs.find((t) => t.path === src);
      const data = t && !t.binary ? enc.encode(t.text) : await this.fetchFile(src);
      await dev.op(() => dev.put(dst, data));
      toast(`Copied to ${dst}`, "ok"); this.refresh(true);
    } catch (e) { toast("Copy failed: " + e.message, "err"); }
  },
  async duplicate(path) {
    const e = extOf(path), stem = e ? path.slice(0, -(e.length + 1)) : path;
    const to = await dialog({ title: "Duplicate", text: "Copy to", value: `${stem}_copy${e ? "." + e : ""}`, ok: "Copy" });
    if (to) { const dst = normPath(to); if (this.checkName(dst)) this.copyTo(path, dst); }
  },

  /* ---------- tabs */
  tab() { return this.tabs.find((t) => t.id === this.active) || null; },
  openTab({ path = null, text = "", saved = null, binary = null, local = false, name = null }) {
    const t = { id: Math.random().toString(36).slice(2), path, name, text, saved, binary, local, readOnly: !!binary, dirty: saved === null || saved !== text };
    if (binary) t.dirty = false;
    this.tabs.push(t);
    this.activate(t.id);
    return t;
  },
  activate(id) {
    if (this.tab()) ed.stash();
    this.active = id;
    const t = this.tab();
    $("welcome").style.display = t ? "none" : "grid";
    $("hex").style.display = t?.binary ? "block" : "none";
    if (t?.binary) $("hex").innerHTML = this.hexDump(t.binary, t.path);
    if (t) ed.open(t);
    this.renderTabs(); this.renderFiles(); this.updateButtons();
    $("sbFile").textContent = t ? (t.path || t.name || "untitled") + (t.local ? " (this computer)" : "") : "";
  },
  async closeTab(id) {
    const t = this.tabs.find((x) => x.id === id);
    if (!t) return;
    if (t.dirty && t.text.trim()) {
      const ok = await dialog({ title: "Discard changes?", text: `${t.path || t.name || "This file"} has unsaved changes.`, ok: "Discard", danger: true });
      if (!ok) return;
    }
    const i = this.tabs.indexOf(t);
    this.tabs.splice(i, 1);
    if (this.active === id) { ed.tab = null; this.activate(this.tabs[Math.min(i, this.tabs.length - 1)]?.id || null); }
    else this.renderTabs();
  },
  renderTabs() {
    const tl = $("tablist");
    tl.innerHTML = "";
    for (const t of this.tabs) {
      t.dirty = !t.binary && (t.saved === null || t.saved !== t.text);
      const d = document.createElement("div");
      d.className = "tab" + (t.id === this.active ? " active" : "") + (t.dirty ? " dirty" : "");
      const nm = t.path ? baseName(t.path) : t.name || "untitled";
      d.title = (t.path || nm) + (t.local ? " (on this computer)" : "");
      d.innerHTML = `<span class="ficon ${t.binary ? "img" : "cs"}">${icon(t.binary ? "chip" : "code")}</span><span class="tname">${esc(nm)}</span><button class="x" title="Close">${icon("x")}</button>`;
      d.onclick = (e) => { if (e.target.closest(".x")) this.closeTab(t.id); else this.activate(t.id); };
      d.onauxclick = (e) => { if (e.button === 1) this.closeTab(t.id); };
      tl.appendChild(d);
    }
    this.updateButtons();
  },
  async openPath(path, size) {
    const have = this.tabs.find((t) => t.path === path && !t.local);
    if (have) return this.activate(have.id);
    if (size > 512 * 1024 && !(await dialog({ title: "Large file", text: `${baseName(path)} is ${fmtBytes(size)}. Downloading it over serial may take a while.`, ok: "Open anyway" }))) return;
    try {
      const data = await this.fetchFile(path);
      if (isText(data)) { const text = new TextDecoder().decode(data); this.openTab({ path, text, saved: text }); }
      else this.openTab({ path, binary: data, text: "", saved: "" });
    } catch (e) { toast(`Cannot open ${path}: ${e.message}`, "err"); }
  },
  hexDump(b, path) {
    const n = Math.min(b.length, 64 * 1024);
    let s = `<b>${esc(path || "")} · ${fmtBytes(b.length)} · ${extOf(path || "") === "mcsb" ? "MicroCS bytecode image - run it, or download it" : "binary file"}${n < b.length ? " · first 64 KB" : ""}</b>\n\n`;
    for (let i = 0; i < n; i += 16) {
      const row = b.subarray(i, Math.min(i + 16, n));
      const hex = [...row].map((x) => x.toString(16).padStart(2, "0")).join(" ").padEnd(48);
      const asc = [...row].map((x) => (x >= 32 && x < 127 ? String.fromCharCode(x) : ".")).join("");
      s += `<b>${i.toString(16).padStart(8, "0")}</b>  ${hex}  ${esc(asc)}\n`;
    }
    return s;
  },
  newScratch(tpl) {
    const name = tpl ? tpl.file : `untitled-${this.untitled++}.cs`;
    this.openTab({ name, text: tpl ? tpl.text : "", saved: null });
  },
  async save(t = this.tab()) {
    if (!t || t.binary) return false;
    ed.stash();
    if (t.local && !dev.connected) { download(t.name || "file.cs", enc.encode(t.text)); t.saved = t.text; this.renderTabs(); return true; }
    if (!dev.connected) { toast("Connect a device to save there (or use ⋯ → Download).", "err"); return false; }
    if (!t.path || t.local) {
      const sug = joinPath(this.cwd, (t.name || "app.cs").replace(/\s+/g, "_"));
      const p = await dialog({ title: "Save to device", text: "Path on the device", value: sug, select: [sug.lastIndexOf("/") + 1, sug.length - 3], ok: "Save" });
      if (!p) return false;
      const path = normPath(p);
      if (!this.checkName(path)) return false;
      t.path = path; t.local = false; t.name = null;
    }
    const text = t.text;
    try {
      await this.transfer(`↑ ${baseName(t.path)}`, (prog) => this.putReliable(t.path, enc.encode(text), prog));
      t.saved = text;
      this.renderTabs();
      $("sbFile").textContent = t.path;
      if (dirName(t.path) === this.cwd) this.refresh(true);
      return true;
    } catch (e) { toast("Save failed: " + e.message, "err", 6000); return false; }
  },

  /* ---------- running */
  async runCurrent(selectionOnly) {
    const t = this.tab();
    if (!t || !dev.connected) return;
    ed.stash();
    if (selectionOnly) {
      const s = ed.ta.selectionStart, e = ed.ta.selectionEnd;
      const code = s !== e ? ed.ta.value.slice(s, e) : ed.ta.value.slice(ed.lineRange()[0], ed.lineRange()[1]);
      const tmp = "/.studio_run.cs";
      try {
        await dev.op(() => dev.put(tmp, enc.encode(code)));
        await this.runPath(tmp, "selection", async () => { try { await dev.check("rm " + tmp); } catch {} });
      } catch (e) { toast(e.message, "err"); }
      return;
    }
    if (!t.binary && (t.dirty || !t.path || t.local)) { if (!(await this.save(t))) return; }
    await this.runPath(t.path);
  },
  async runPath(path, label, after) {
    if (dev.running) { toast("A script is still running - Stop it first.", "err"); return; }
    ed.setError(0);
    term.line(`▶ run ${label || path}`, "info");
    const t0 = performance.now();
    const dec = new TextDecoder();
    dev.running = true; this.updateButtons();
    $("btnStop").disabled = false;
    let st, tail = "";
    try {
      st = await dev.op(async () => {
        const r = await dev.cmd("run " + path, { onOut: (b) => { const s = dec.decode(b, { stream: true }); tail = (tail + s).slice(-600); term.write(s); }, idle: 0 });
        if (after) await after();
        return r;
      });
    } catch (e) { st = { status: "ERR " + e.message }; }
    dev.running = false; this.updateButtons();
    const ms = Math.round(performance.now() - t0);
    if (st.status === "OK") term.line(`✓ finished in ${ms} ms`, "ok");
    else {
      const msg = st.status.replace(/^ERR /, "");
      const shown = msg.length > 8 && tail.includes(msg.slice(0, 60));    // the error text was already printed
      term.line(/aborted/i.test(msg) ? "■ stopped" : shown ? `✗ failed after ${ms} ms` : "✗ " + msg, /aborted/i.test(msg) ? "warn" : "err");
      const m = /\((\d+),\d+\)/.exec(msg) || /:line (\d+)/.exec(msg);
      const tab = this.tab();
      if (m && tab && (tab.path === path || label === "selection")) ed.setError(label === "selection" ? 0 : +m[1]);
    }
    this.refresh(true);
  },
  stop() { dev.stop(); term.line("^C", "warn"); },

  /* ---------- console */
  setConsMode(m) {
    this.consMode = m; store.set("consMode", m);
    $("consMode").querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.m === m));
    $("ps").textContent = m === "repl" ? ">" : "$";
    $("cin").placeholder = m === "repl" ? "C# statement or expression — Enter runs, Shift+Enter new line, ↑ ↓ history" : "shell command: ls, cat <f>, run <f>, jobs, mem, df, help …";
  },
  async consoleSend() {
    const cin = $("cin"), text = cin.value.replace(/\s+$/, "");
    if (!dev.connected) return;
    cin.value = ""; this.autosize();
    if (text) { this.history = this.history.filter((h) => h !== text); this.history.push(text); if (this.history.length > 200) this.history.shift(); store.set("history", this.history); }
    this.hpos = -1;
    if (this.consMode === "shell") {
      if (!text) return;
      term.line("", "");
      term.write("$ ", "in"); term.write(text + "\n", "in");
      const dec = new TextDecoder();
      try {
        const r = await dev.op(() => dev.cmd(text, { onOut: (b) => term.write(dec.decode(b, { stream: true })), idle: /^(run|exec)\b/.test(text) ? 0 : 8000 }));
        if (r.status.startsWith("ERR")) term.line(r.status.slice(4), "err");
        if (/^(rm|mv|mkdir|put)\b/.test(text)) this.refresh(true);
      } catch (e) { term.line(e.message, "err"); }
      return;
    }
    const lines = text.split("\n");
    const wasRepl = dev.mode === "repl";
    const cur = term.lineText;
    const atPrompt = wasRepl && /^(> |\.\.\. )$/.test(cur);
    if (term.cells.length && !atPrompt) term.newline();
    lines.forEach((l, i) => { if (!(i === 0 && atPrompt)) term.write(i ? "... " : "> ", "in"); term.write(l + "\n", "in"); });
    this.echoQ = lines.map((l) => l + "\r\n").join("");
    try {
      await dev.op(async () => {
        await dev.repl();
        await dev.write(lines.map((l) => l + "\r").join(""));
      });
    } catch (e) { this.echoQ = ""; term.line(e.message, "err"); }
  },
  /* device text while nobody waits for a status: REPL output, script/job output, boot log */
  onDeviceText(text) {
    if (this.echoQ) {
      let i = 0, out = "";
      while (i < text.length) {
        if (!this.echoQ) { out += text.slice(i); break; }
        const c = text[i];
        if (c === this.echoQ[0]) { this.echoQ = this.echoQ.slice(1); i++; continue; }
        if (c === "\r" && this.echoQ[0] === "\n") { i++; continue; }
        if (c === "\n" && this.echoQ[0] === "\r") { this.echoQ = this.echoQ.slice(1); continue; }
        if (text.startsWith("... ", i)) { i += 4; continue; }
        this.echoQ = "";
      }
      text = out;
    }
    if (text) term.write(text);
  },
  autosize() { const c = $("cin"); c.style.height = "24px"; c.style.height = Math.min(160, c.scrollHeight) + "px"; },
  consoleKey(e) {
    const c = $("cin");
    if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); this.consoleSend(); return; }
    if (e.key === "c" && (e.ctrlKey || e.metaKey) && c.selectionStart === c.selectionEnd) { e.preventDefault(); this.stop(); c.value = ""; return; }
    if (e.key === "l" && e.ctrlKey) { e.preventDefault(); term.clear(); return; }
    const first = !c.value.slice(0, c.selectionStart).includes("\n"), last = !c.value.slice(c.selectionEnd).includes("\n");
    if (e.key === "ArrowUp" && first && this.history.length) {
      e.preventDefault();
      if (this.hpos < 0) { this.draft = c.value; this.hpos = this.history.length; }
      this.hpos = Math.max(0, this.hpos - 1); c.value = this.history[this.hpos]; this.autosize();
    } else if (e.key === "ArrowDown" && last && this.hpos >= 0) {
      e.preventDefault();
      this.hpos++;
      if (this.hpos >= this.history.length) { this.hpos = -1; c.value = this.draft || ""; } else c.value = this.history[this.hpos];
      this.autosize();
    }
  },

  /* ---------- misc */
  /* ---------- template gallery ("Add New Item") */
  gallery(insertMode) {
    const g = this.gal || (this.gal = { cat: "All", sel: 0, list: [] });
    $("galleryBg").classList.add("show");
    $("gSearch").value = "";
    $("gInsert").disabled = !this.tab() || !!this.tab().binary;
    this.galRender();
    setTimeout(() => (insertMode ? $("gInsert") : $("gSearch")).focus(), 0);
  },
  galClose() { $("galleryBg").classList.remove("show"); if (this.tab()) ed.ta.focus(); },
  galRender() {
    const g = this.gal, q = $("gSearch").value.trim().toLowerCase();
    const cats = ["All", ...new Set(TEMPLATES.map((t) => t.cat))];
    const match = (t) => !q || (t.name + " " + t.desc + " " + t.file + " " + t.cat + " " + t.text).toLowerCase().includes(q);
    $("gCats").innerHTML = cats.map((c) => {
      const n = TEMPLATES.filter((t) => (c === "All" || t.cat === c) && match(t)).length;
      return `<div data-c="${esc(c)}" class="${c === g.cat ? "on" : ""}"><span>${esc(c)}</span><small>${n}</small></div>`;
    }).join("");
    g.list = TEMPLATES.filter((t) => (g.cat === "All" || t.cat === g.cat) && match(t));
    if (g.sel >= g.list.length) g.sel = 0;
    $("gItems").innerHTML = g.list.length ? g.list.map((t, i) =>
      `<div class="item${i === g.sel ? " on" : ""}${/\.cs$/.test(t.file) ? "" : " cfg"}" data-i="${i}">${icon(/\.cs$/.test(t.file) ? "code" : "file")}<div><b>${esc(t.name)}</b><span>${esc(t.desc)}</span><em>${esc(t.file)}</em></div></div>`).join("")
      : `<div class="none">No template matches “${esc(q)}”.</div>`;
    this.galPreview();
  },
  galPreview() {
    const t = this.gal.list[this.gal.sel];
    $("gInfo").innerHTML = t ? `<b>${esc(t.name)}</b><span>${esc(t.cat)} · ${esc(t.desc)}</span>` : "";
    $("gPrev").innerHTML = t ? (/\.cs$/.test(t.file) ? highlight(t.text) : highlightPlain(t.text)) : "";
    $("gName").value = t ? t.file : "";
    $("gAdd").disabled = $("gInsert").disabled && !t;
    $("gItems").querySelector(".item.on")?.scrollIntoView({ block: "nearest" });
  },
  galAdd(insert) {
    const t = this.gal.list[this.gal.sel];
    if (!t) return;
    this.galClose();
    if (insert && this.tab() && !this.tab().binary) { ed.ta.focus(); ed.insert(t.text); return; }
    const name = ($("gName").value.trim() || t.file).replace(/^\/+/, "");
    this.newScratch({ ...t, file: name });
  },
  galWire() {
    $("gCats").onclick = (e) => { const d = e.target.closest("[data-c]"); if (d) { this.gal.cat = d.dataset.c; this.gal.sel = 0; this.galRender(); } };
    $("gItems").onclick = (e) => { const d = e.target.closest("[data-i]"); if (d) { this.gal.sel = +d.dataset.i; this.galRender(); } };
    $("gItems").ondblclick = (e) => { if (e.target.closest("[data-i]")) this.galAdd(false); };
    $("gSearch").oninput = () => { this.gal.sel = 0; this.galRender(); };
    $("gClose").onclick = $("gCancel").onclick = () => this.galClose();
    $("gAdd").onclick = () => this.galAdd(false);
    $("gInsert").onclick = () => this.galAdd(true);
    $("galleryBg").onmousedown = (e) => { if (e.target === $("galleryBg")) this.galClose(); };
    $("galleryBg").addEventListener("keydown", (e) => {
      const g = this.gal;
      if (e.key === "Escape") { e.preventDefault(); this.galClose(); }
      else if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); g.sel = Math.max(0, Math.min(g.list.length - 1, g.sel + (e.key === "ArrowDown" ? 1 : -1))); this.galRender(); }
      else if (e.key === "Enter" && e.target.id !== "gInsert" && e.target.id !== "gCancel" && e.target.id !== "gClose") { e.preventDefault(); this.galAdd(false); }
      else if (e.key.toLowerCase() === "e" && e.ctrlKey) { e.preventDefault(); $("gSearch").focus(); }
    });
  },
  moreMenu(anchor) {
    const t = this.tab();
    Menu.at(anchor, [
      { label: "New untitled file", icon: "file-plus", run: () => this.newScratch() },
      { label: "Templates and examples…", icon: "template", run: () => this.gallery() },
      { label: "Insert template at cursor…", icon: "code", disabled: !t || !!t.binary, run: () => this.gallery(true) },
      { label: "Open from this computer…", icon: "open", run: () => $("openLocal").click() },
      { label: "Download to this computer", icon: "download", disabled: !t, run: () => download(t.path ? baseName(t.path) : t.name || "file.cs", t.binary || enc.encode(t.text)) },
      { label: "Save as… (device)", icon: "save", disabled: !t || !!t.binary || !dev.connected, run: async () => { const old = t.path; t.path = null; if (!(await this.save(t))) t.path = old; } },
      "-",
      { label: "Run selection / line", icon: "play", disabled: !t || !dev.connected, run: () => this.runCurrent(true) },
      { label: "Show running jobs", icon: "term", disabled: !dev.connected, run: () => { this.setConsMode("shell"); $("cin").value = "jobs"; this.consoleSend(); } },
      "-",
      { label: "Editor font larger", icon: "edit", run: () => this.font(1) },
      { label: "Editor font smaller", icon: "edit", run: () => this.font(-1) },
    ]);
  },
  font(d) {
    const fs = Math.max(10, Math.min(24, (store.get("fs", 13)) + d));
    store.set("fs", fs);
    document.documentElement.style.setProperty("--fs", fs + "px");
    document.documentElement.style.setProperty("--lh", Math.round(fs * 1.45) + "px");
    ed.measure(); ed.refresh(true); ed.syncScroll();
  },
  theme(toggle) {
    let light = store.get("light", false);          // Visual Studio Dark by default
    if (toggle) { light = !light; store.set("light", light); }
    document.documentElement.classList.toggle("light", light);
    $("btnTheme").innerHTML = icon(light ? "moon" : "sun");
  },
  splitters() {
    const drag = (el, onMove) => el.addEventListener("mousedown", (e) => {
      e.preventDefault(); el.classList.add("drag");
      const mv = (ev) => onMove(ev), up = () => { el.classList.remove("drag"); removeEventListener("mousemove", mv); removeEventListener("mouseup", up); ed.syncScroll(); };
      addEventListener("mousemove", mv); addEventListener("mouseup", up);
    });
    const side = store.get("side", 280), cons = store.get("cons", 250);
    $("main").style.setProperty("--side", side + "px");
    $("center").style.setProperty("--cons", cons + "px");
    drag($("splitSide"), (e) => { const w = Math.max(200, Math.min(560, e.clientX)); $("main").style.setProperty("--side", w + "px"); store.set("side", w); });
    drag($("splitCons"), (e) => { const r = $("center").getBoundingClientRect(); const h = Math.max(90, Math.min(r.height - 120, r.bottom - e.clientY)); $("center").style.setProperty("--cons", h + "px"); store.set("cons", h); });
  },

  init() {
    this.theme(false);
    if (store.get("fs", 13) !== 13) this.font(0);
    this.splitters();
    const b = store.get("baud", 115200); $("baud").value = String(b);
    this.setConsMode(this.consMode);
    if (!("serial" in navigator)) $("noSerial").innerHTML = `<div class="warnbox">This browser has no Web Serial. Open this file in <b>Chrome</b> or <b>Edge</b> on a desktop to talk to a device — the editor works here too.</div>`;
    dev.onText = (t) => this.onDeviceText(t);
    dev.onState = () => this.updateButtons();
    dev.onLost = () => {
      term.line("— device disconnected" + (this.autoReconnect ? " - waiting for it to come back —" : " —"), "warn");
      this.setConn(this.autoReconnect ? "busy" : "off", this.autoReconnect ? "Waiting for the device…" : "Not connected");
      this.enable(false);
      if (this.autoReconnect) { const b = $("btnConnect"); b.querySelector("span").textContent = "Cancel"; }
    };
    /* native USB (ESP32-S3/C3/C6 USB-Serial-JTAG) disappears on every chip reset: reopen it when it comes back */
    if ("serial" in navigator) navigator.serial.addEventListener?.("connect", (e) => {
      const p = e.target;
      if (dev.connected || !this.autoReconnect || !this.lastPort) return;
      const a = p.getInfo?.() || {}, b = this.lastPort.getInfo?.() || {};
      if (p === this.lastPort || (a.usbVendorId && a.usbVendorId === b.usbVendorId && a.usbProductId === b.usbProductId)) this.openPort(p, true);
    });
    term.onLink = (path, line) => {
      if (path) { const t = this.tabs.find((x) => x.path === path); (t ? Promise.resolve(this.activate(t.id)) : this.openPath(path)).then(() => { ed.setError(line); ed.gotoLine(line); }); }
      else if (this.tab()) { ed.gotoLine(line); }
    };
    ed.onChange = () => { const t = this.tab(); if (!t) return; const was = t.dirty; t.dirty = t.saved === null || t.saved !== t.text; if (was !== t.dirty) this.renderTabs(); if (ed.errLine) ed.setError(0); };
    ed.onCursor = (l, c) => ($("sbPos").textContent = `Ln ${l}, Col ${c}`);

    $("btnConnect").onclick = () => this.connect();
    $("wConnect").onclick = () => this.connect();
    $("wNew").onclick = () => this.newScratch();
    $("wTemplates").onclick = () => this.gallery();
    $("wOpen").onclick = () => $("openLocal").click();
    $("btnTemplates").onclick = () => this.gallery();
    this.galWire();
    $("btnMore").onclick = (e) => this.moreMenu(e.currentTarget);
    $("btnReset").onclick = () => this.reset();
    $("btnTheme").onclick = () => this.theme(true);
    $("btnRefresh").onclick = () => this.refresh();
    $("btnNewFile").onclick = () => this.newFile();
    $("btnNewDir").onclick = () => this.newDir();
    $("btnUpload").onclick = () => $("fileInput").click();
    $("fileInput").onchange = (e) => { this.upload([...e.target.files]); e.target.value = ""; };
    $("btnOpenLocal").onclick = () => $("openLocal").click();
    $("openLocal").onchange = async (e) => {
      const f = e.target.files[0]; e.target.value = "";
      if (!f) return;
      const data = new Uint8Array(await f.arrayBuffer());
      if (isText(data)) { const text = new TextDecoder().decode(data); this.openTab({ name: f.name, text, saved: text, local: true }); }
      else this.openTab({ name: f.name, binary: data, local: true });
    };
    $("btnSave").onclick = () => this.save();
    $("btnRun").onclick = () => this.runCurrent(false);
    $("btnStop").onclick = () => this.stop();
    $("btnCtrlC").onclick = () => this.stop();
    $("btnClear").onclick = () => term.clear();
    $("consMode").onclick = (e) => { const m = e.target.closest("button")?.dataset.m; if (m) { this.setConsMode(m); $("cin").focus(); } };
    $("cin").addEventListener("keydown", (e) => this.consoleKey(e));
    $("cin").addEventListener("input", () => this.autosize());
    $("term").addEventListener("mouseup", () => { if (!getSelection().toString() && !$("cin").disabled) $("cin").focus(); });

    const files = $("files");
    files.addEventListener("dragover", (e) => { e.preventDefault(); files.classList.add("drag"); });
    files.addEventListener("dragleave", (e) => { if (!files.contains(e.relatedTarget)) files.classList.remove("drag"); });
    files.addEventListener("drop", (e) => { e.preventDefault(); files.classList.remove("drag"); if (e.dataTransfer.files.length) this.upload([...e.dataTransfer.files]); });
    addEventListener("dragover", (e) => e.preventDefault());
    addEventListener("drop", (e) => e.preventDefault());

    addEventListener("keydown", (e) => {
      const mod = e.ctrlKey || e.metaKey;
      if (mod && e.key.toLowerCase() === "s") { e.preventDefault(); this.save(); }
      else if (e.key === "F5" || (mod && e.key === "Enter")) { e.preventDefault(); this.runCurrent(e.shiftKey && mod); }
      else if (mod && e.key === "`") { e.preventDefault(); $("cin").focus(); }
      else if (e.altKey && e.key.toLowerCase() === "n") { e.preventDefault(); this.newScratch(); }
      else if (e.altKey && e.key.toLowerCase() === "t") { e.preventDefault(); this.gallery(); }
      else if (mod && e.key.toLowerCase() === "o") { e.preventDefault(); $("openLocal").click(); }
      else if (e.altKey && e.key.toLowerCase() === "w" && this.active) { e.preventDefault(); this.closeTab(this.active); }
    });
    addEventListener("beforeunload", (e) => { if (this.tabs.some((t) => t.dirty && t.text.trim())) { e.preventDefault(); e.returnValue = ""; } });
    this.renderFiles(); this.renderTabs(); this.activate(null);
    term.line("MicroCS Studio — connect a board running MicroCS (USB). Output of scripts, jobs and the REPL appears here.", "sys");
  },
};
App.init();
