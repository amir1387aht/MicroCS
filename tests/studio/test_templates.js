// Compile and run every C# template of MicroCS Studio (tools/studio/templates.js)
// on the simulated board:  node tests/studio/test_templates.js ./mcs
// A template passes when it compiles and either finishes, hits the time budget
// (main loops) or stops at a missing *device* (IOException / TimeoutException:
// the simulator has no OLED, BME280...). Unknown members, type errors etc. fail.
const fs = require('fs'), os = require('os'), path = require('path'), { spawn } = require('child_process');
const MCS = path.resolve(process.argv[2] || './mcs');
global.window = {};
const src = fs.readFileSync(path.join(__dirname, '../../tools/studio/templates.js'), 'utf8');
const { TEMPLATES, SNIPPETS } = new Function(src + '; return { TEMPLATES, SNIPPETS };')();
let fails = 0;
const cs = TEMPLATES.filter((t) => t.file.endsWith('.cs'));
function run(t) {
  return new Promise((done) => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mcs-tpl-'));
    fs.writeFileSync(path.join(dir, t.file), t.text);
    const p = spawn(MCS, ['--sim', '--fs', dir, '--time-limit', '3000', '--run-for', '2500', path.join(dir, t.file)]);
    let err = '';
    p.stderr.on('data', (d) => (err += d)); p.stdout.resume();
    p.stdin.on('error', () => {}); p.stdin.end('Ann\n7\n');
    const kill = setTimeout(() => p.kill(), 20000);
    p.on('close', (code, signal) => {
      clearTimeout(kill);
      const ok = code === 0 || /time limit|budget/i.test(err) || /Unhandled exception\. (IOException|TimeoutException)/.test(err) || signal === 'SIGTERM';
      if (!ok) { fails++; console.log(`FAIL ${t.cat} / ${t.name}\n${err.trim().split('\n').slice(0, 4).join('\n')}`); }
      fs.rmSync(dir, { recursive: true, force: true });
      done();
    });
  });
}
(async () => {
  const queue = [...cs];
  await Promise.all(Array.from({ length: 8 }, async () => { while (queue.length) await run(queue.shift()); }));
  const labels = new Set();
  for (const s of SNIPPETS) { if (labels.has(s.label)) { fails++; console.log('FAIL duplicate snippet ' + s.label); } labels.add(s.label); }
  console.log(`${cs.length} C# templates, ${TEMPLATES.length} total, ${SNIPPETS.length} snippets: ${fails ? fails + ' FAILED' : 'ALL PASSED'}`);
  process.exit(fails ? 1 : 0);
})();
