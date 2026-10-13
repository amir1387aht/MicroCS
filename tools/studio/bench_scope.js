// Reproducible checked Node scope-model latency. Not browser/MCU performance.
const fs = require('fs'), vm = require('vm'), path = require('path'), os = require('os');
const assert = require('assert'), { performance } = require('perf_hooks');
global.window = {};
for (const name of ['docs.js', 'lang.js'])
  vm.runInThisContext(fs.readFileSync(path.join(__dirname, name), 'utf8'));
const source = Array.from({ length: 400 }, (_, i) =>
  `void Fn${i}(int argument${i}) { string local${i}="a;b"; { var nested${i}=new List<int>(); } }`
).join('\n') + '\nvoid Final(int present) { var finalText="test"; ';
const cold = [], warm = [];
for (let i = 0; i < 7; i++) {
  const text = source + ' // sample' + i;
  let time = performance.now();
  const symbols = window.Lang.symbols(text);
  cold.push(performance.now() - time);
  assert.strictEqual(window.Lang.symbols(text), symbols, 'one-source cache is reused');
  assert.deepStrictEqual([...window.Lang.locals(text, text.length).keys()], ['present', 'finalText']);
  assert.strictEqual(window.Lang.varType('present', text, text.length), 'int');
  assert.strictEqual(window.Lang.varType('local0', text, text.length), null);
  time = performance.now();
  for (let j = 0; j < 100; j++) {
    window.Lang.locals(text, text.length);
    window.Lang.varType('present', text, text.length);
  }
  warm.push((performance.now() - time) / 100);
}
console.log(JSON.stringify({
  kind: 'checked Node host source-model latency; NOT browser or MCU timing',
  node: process.version, arch: process.arch, platform: process.platform,
  cpu: os.cpus()[0].model, functions: 401, source_bytes: Buffer.byteLength(source),
  fixture_sha256: require('crypto').createHash('sha256').update(source).digest('hex'),
  cold_parse_ms: cold, warm_locals_plus_type_ms: warm,
  workload: 'seven distinct edits; one cold symbol model + 100 cached locals/type lookups per edit',
  scope: 'single-document WIP lookup; no project parser or universal latency claim'
}, null, 2));
