// Test the real console methods with an earlier serial operation still queued.
// A delayed prompt must not consume the next command's echo filter or appear
// between its local input and result. No browser, timing sleeps or hardware.
const fs = require('fs'), path = require('path'), assert = require('assert');
const source = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const a = source.indexOf('  async consoleSend() {'), b = source.indexOf('  autosize()', a);
assert(a > 0 && b > a);
function setup(value, mode = 'machine') {
  let text = '', queued, release, reject;
  const input = { value }, sent = [];
  const term = {
    cells: [], lineText: '',
    write(s) { text += s.replace(/\r/g, ''); this.lineText = text.split('\n').pop(); this.cells = [1]; },
    newline() { this.write('\n'); }, line(s) { this.write(s + '\n'); }
  };
  const dev = {
    connected: true, running: false, mode,
    op(fn) { queued = fn; return new Promise((res, rej) => { release = res; reject = rej; }); },
    async repl() { this.mode = 'repl'; },
    async write(s) { sent.push(s); }
  };
  const app = new Function('dev', 'term', '$', 'store', 'plot', 'TextDecoder',
    'return ({' + source.slice(a, b) + '});')(
      dev, term, () => input, { set() {} }, { feed() {} }, TextDecoder);
  Object.assign(app, { history: [], autosize() {}, consMode: 'repl', echoQ: '' });
  return { app, dev, term, sent, text: () => text,
    async execute() { try { await queued(); release(); } catch (e) { reject(e); } } };
}
(async () => {
  let t = setup('x + 1');
  const submitted = t.app.consoleSend();
  assert.strictEqual(t.app.echoQ, '', 'no echo ownership while another operation owns the line');
  assert.strictEqual(t.text(), '', 'do not render input ahead of earlier device output');
  t.app.onDeviceText('previous output\r\n> ');
  await t.execute(); await submitted;
  assert.strictEqual(t.text(), 'previous output\n> x + 1\n');
  assert.deepStrictEqual(t.sent, ['x + 1\r']);
  for (const chunk of ['x +', ' 1\r', '\n22\r\n> ']) t.app.onDeviceText(chunk);
  assert.strictEqual(t.text(), 'previous output\n> x + 1\n22\n> ');
  assert.strictEqual(t.app.echoQ, '');
  // REPL transition output occurs before the new command claims echo ownership.
  t = setup('x + 1'); const second = t.app.consoleSend();
  t.dev.repl = async () => { assert.strictEqual(t.app.echoQ, ''); t.app.onDeviceText('old output\r\n'); t.dev.mode = 'repl'; };
  await t.execute(); await second;
  t.app.onDeviceText('x + 1\r\n22\r\n> ');
  assert(t.text().endsWith('> x + 1\n22\n> '));
  // Existing continuation prompts and multiline echo still have one owner.
  t = setup('one\ntwo', 'repl'); t.app.onDeviceText('... ');
  const multi = t.app.consoleSend(); await t.execute(); await multi;
  t.app.onDeviceText('one\r\n... two\r\nresult\r\n> ');
  assert.strictEqual(t.text(), '... one\n... two\nresult\n> ');
  assert.deepStrictEqual(t.sent, ['one\rtwo\r']);
  // Protocol failure releases the filter; later application output is visible.
  t = setup('x + 1'); const failed = t.app.consoleSend();
  t.dev.write = async () => { throw Error('disconnected'); };
  await t.execute(); await failed;
  assert.strictEqual(t.app.echoQ, ''); t.app.onDeviceText('later output\n');
  assert(t.text().endsWith('disconnected\nlater output\n'));
  console.log('PASS console queue: delayed prompts, transition output, split/multiline echo, error cleanup');
})().catch(e => { console.error(e); process.exitCode = 1; });
