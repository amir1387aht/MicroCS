// Compile and run every C# template of MicroCS Studio (tools/studio/templates/, bundled in templates.js)
// on the simulated board:  node tests/studio/test_templates.js ./mcs
// A template passes when it compiles and either finishes, hits the time budget
// (main loops) or stops at a missing *device* (IOException / TimeoutException:
// the simulator has no OLED, BME280...). Unknown members, type errors etc. fail.
// U8g2 / U8x8 templates print a hint on firmware without u8g2; with a u8g2 build
// (make mcs-u8g2) they run against the simulated SSD1306 (--oled) and must really draw:
//   node tests/studio/test_templates.js build/mcs_u8g2 [--only=u8g2]
// Bluetooth LE templates need a BLE build (make mcs-ble; skipped elsewhere):
//   node tests/studio/test_templates.js build/mcs_ble --only=ble
const fs = require('fs'), os = require('os'), path = require('path'), { spawn, execFileSync } = require('child_process');
const MCS = path.resolve(process.argv[2] || './mcs');
const ONLY = (process.argv.find((a) => a.startsWith('--only=')) || '').slice(7);
const FEATURES = execFileSync(MCS, ['--features']).toString();
const HAS_U8G2 = / u8g2\b/.test(FEATURES), HAS_BLE = / ble\b/.test(FEATURES);
const isU8g2 = (t) => /\bU8(g2|x8)\b/.test(t.text);
const isBle = (t) => /\bBle(Service|Uart|Advertisement)?\b/.test(t.text);
global.window = {};
const studio = (f) => fs.readFileSync(path.join(__dirname, '../../tools/studio', f), 'utf8');
const { TEMPLATES, SNIPPETS } = new Function(studio('templates.js') + '\n' + studio('snippets.js') + '; return { TEMPLATES, SNIPPETS };')();
let fails = 0;
function failure(message) {
  fails++; console.log(message);
  if (process.env.GITHUB_ACTIONS) console.log('::error title=Studio template failure::' +
    message.replace(/%/g, '%25').replace(/\r/g, '%0D').replace(/\n/g, '%0A'));
}
const all = TEMPLATES.filter((t) => t.file.endsWith('.cs') && (ONLY !== 'u8g2' || isU8g2(t)) && (ONLY !== 'ble' || isBle(t)));
const cs = all; // BLE examples must return cleanly when the module / radio is missing
function run(t) {
  return new Promise((done) => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mcs-tpl-'));
    fs.writeFileSync(path.join(dir, t.file), t.text);
    const oled = HAS_U8G2 && isU8g2(t);
    const p = spawn(MCS, [...(oled ? ['--oled'] : []), '--sim', '--fs', dir, '--time-limit', '3000', '--run-for', '2500', path.join(dir, t.file)]);
    let err = '', out = '';
    p.stderr.on('data', (d) => (err += d)); p.stdout.on('data', (d) => (out += d));
    p.stdin.on('error', () => {}); p.stdin.end('Ann\n7\n');
    const kill = setTimeout(() => p.kill(), 20000);
    p.on('close', (code, signal) => {
      clearTimeout(kill);
      if (oled) err = err.split('\n').filter((l) => !/^[|+]/.test(l)).join('\n');   // the drawn frames
      if (oled && (/has no U8g2/.test(out) || !/\[oled 0x3C frame/.test(out + err))) { failure(`FAIL ${t.cat} / ${t.name}: drew nothing on the simulated OLED`); }
      const ok = code === 0 || /time limit|budget/i.test(err) || /Unhandled exception\. (IOException|TimeoutException)/.test(err) || signal === 'SIGTERM';
      if (!ok) failure(`FAIL ${t.cat} / ${t.name} (exit=${code}, signal=${signal})\n${err.trim().split('\n').slice(0, 8).join('\n')}\n${out.slice(-500)}`);
      fs.rmSync(dir, { recursive: true, force: true });
      done();
    });
  });
}
(async () => {
  // templates that bind a server port must not overlap; run them one at a time afterwards
  const binds = (t) => /new (HttpServer|DnsServer|TcpListener)\b/.test(t.text);
  const queue = cs.filter((t) => !binds(t));
  await Promise.all(Array.from({ length: 8 }, async () => { while (queue.length) await run(queue.shift()); }));
  for (const t of cs.filter(binds)) await run(t);
  for (const t of TEMPLATES) if (!t.src || !t.desc) { fails++; console.log(`FAIL ${t.cat} / ${t.name}: no source file or description (run tools/studio/gen_templates.py)`); }
  const labels = new Set();
  for (const s of SNIPPETS) { if (labels.has(s.label)) { fails++; console.log('FAIL duplicate snippet ' + s.label); } labels.add(s.label); }
  if (ONLY === 'ble' && (!HAS_BLE || !cs.length)) { fails++; console.log('FAIL no Bluetooth LE templates ran (build with make mcs-ble)'); }
  const skipped = all.length - cs.length;
  console.log(`${cs.length} C# templates${skipped ? ` (${skipped} Bluetooth LE skipped: no ble in this build)` : ''}${HAS_U8G2 ? ` (${cs.filter(isU8g2).length} on the simulated OLED)` : ''}, ${TEMPLATES.length} total, ${SNIPPETS.length} snippets: ${fails ? fails + ' FAILED' : 'ALL PASSED'}`);
  process.exit(fails ? 1 : 0);
})();
