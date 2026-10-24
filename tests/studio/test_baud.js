// Validate custom rates without a browser or attached UART.
const fs = require('fs'), path = require('path'), assert = require('assert');
const source = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const a = source.indexOf('function parseBaud('), b = source.indexOf('const store = {', a);
assert(a >= 0 && b > a);
const parse = new Function(source.slice(a, b) + '; return parseBaud;')();
for (const [input, want] of [[1, 1], [' 74880 ', 74880], ['00125000', 125000],
  [125000, 125000], ['4294967295', 4294967295], [115200, 115200]]) {
  assert.strictEqual(parse(input), want, String(input));
}
for (const input of ['', ' ', 0, -1, '1.5', '1e5', '0x100', '+12', '12bps',
  '4294967296', Infinity, NaN, null, undefined, true, [], {}, '9'.repeat(500)]) {
  assert.strictEqual(parse(input), null, String(input));
}
console.log('PASS baud validation: custom/preset rates, uint32 bounds, invalid saved values');
