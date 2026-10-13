// Pure tokenizer tests; no DOM / browser / installed packages needed.
const fs = require('fs'), assert = require('assert'), path = require('path');
const source = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const code = source.slice(source.indexOf('const KEYWORDS ='), source.indexOf('/* ================================================================ editor */'));
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
const { highlight, stringEnd } = new Function('window', 'esc', code + '; return { highlight, stringEnd };')({}, esc);
const cases = [
  '$@"<h1>LED {(led.Read() ? "on" : "off")}</h1>\n<form method=post>Off</form>"',
  '@$"<h1>{(led.Read() ? "on" : "off")}</h1>\n<footer>tail</footer>"',
  '$"value {Get("}", \'{\')} tail"',
  '$@"doubled ""quote"", {{brace}} {new[] {"a", "b"}[0]}\n tail"',
  '$"nested {$"inner {x}"} tail"',
  '$@"comment {x /* } \" */ + 1} tail"',
  '@"C:\\folder\\name ""quoted""\n tail"',
  '"escaped \\" and \\\\ end"',
];
for (const literal of cases) {
  assert.strictEqual(stringEnd(literal + '; Console.WriteLine(42);', 0), literal.length, literal);
  const html = highlight(literal + '; Console.WriteLine(42);');
  assert(html.includes('t-num">42'), 'code after literal must not be swallowed: ' + literal);
  const plain = html.replace(/<[^>]+>/g, '');
  assert.strictEqual(plain, esc(literal + '; Console.WriteLine(42);'), 'source must round-trip');
}
const web = highlight(cases[0]);
assert(/<span class="t-str">[^]*&lt;form method=post&gt;Off&lt;\/form&gt;/.test(web), 'HTML after nested quotes remains a string');
assert(web.includes('class="t-str">&quot;on&quot;'), 'quoted interpolation expression is highlighted');
assert.strictEqual(stringEnd('$@"unfinished {x + "value"', 0), '$@"unfinished {x + "value"'.length);
assert.strictEqual(stringEnd('"unfinished\nnext', 0), '"unfinished'.length);
console.log('PASS Studio strings: interpolated/verbatim, nested holes, comments, escapes, incomplete input');
