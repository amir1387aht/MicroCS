// Uploads cannot be queued behind an unbounded foreground run. Fake only time
// and the device; test the actual App method without DOM or a physical board.
const fs = require('fs'), path = require('path'), assert = require('assert');
const js = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const a = js.indexOf('  async stopForUpload() {'), b = js.indexOf('  /* uploads go through', a);
assert(a > 0 && b > a);
function setup(running, onSleep = () => {}) {
  let now = 0, stops = 0, waits = 0, notes = 0;
  const dev = { running, connected: true, async stop() { stops++; } };
  const stop = new Function('dev', 'term', 'sleep', 'Date',
    'return ({' + js.slice(a, b) + '}).stopForUpload;')(
      dev, { line() { notes++; } },
      async ms => { now += ms; onSleep(dev, ++waits); }, { now: () => now });
  return { dev, stop, state: () => ({ now, stops, waits, notes }) };
}
(async () => {
  let t = setup(false);
  await t.stop();
  assert.deepStrictEqual(t.state(), { now: 0, stops: 0, waits: 0, notes: 0 });
  t = setup(true, (dev, waits) => { if (waits === 3) dev.running = false; });
  await t.stop();
  assert.deepStrictEqual(t.state(), { now: 60, stops: 1, waits: 3, notes: 1 });
  t = setup(true);
  await assert.rejects(t.stop(), /did not stop.*Stop or reset/);
  assert.strictEqual(t.state().stops, 1);
  assert.strictEqual(t.state().now, 4000);
  t = setup(true, dev => { dev.connected = false; });
  await assert.rejects(t.stop(), /Disconnected/);
  assert.strictEqual(t.state().waits, 1);
  console.log('PASS upload stop: idle, successful abort, bounded unresponsive script, disconnect');
})().catch(e => { console.error(e); process.exitCode = 1; });
