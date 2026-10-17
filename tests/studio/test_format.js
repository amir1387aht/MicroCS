const assert = require('assert'), F = require('../../tools/studio/format.js');
const samples = [
  'if(true){int x=1;Console.WriteLine(x);}',
  'var s="a,b=1"; // untouched, =',
  'var a=1; // CRLF comment  \r\nvar b=2;\r\n',
  'if(true) {x=1;} // closing comment',
  'var s = @"a\n  b  \n";\nif(true) {\nx=1;\n}',
  'var s = $"prefix {string.Join(",", a)} end";',
  'var s = @$"a {string.Join("\\n", a)}\n  b  ";',
  'var s = """\n  spaces  \n  { no code }  \n""";',
  'for(int i=0;i<3;i++){ /* a = b;   */ x+=1; }',
  '// comments keep trailing whitespace   \nvar n=1e-3; var x = -2;',
  'var incomplete = "keep trailing spaces   ',
  'int[] a = {1,2,3};\nvar f = (int x) => {return x;};',
];
for (const s of samples) {
  const out = F.format(s, { reflow: true });
  assert.strictEqual(F.format(out, { reflow: true }), out, 'idempotent: '+s);
  const parts = src => F.lex(src).ranges.map(r => src.slice(r.start, r.end));
  assert.deepStrictEqual(parts(out), parts(s), 'literal/comment preservation: '+s);
  assert.strictEqual(out.replace(/\s/g,''), s.replace(/\s/g,''), 'only whitespace changes');
}
assert.strictEqual(F.format('if(true){int x=1;}', { reflow:true }), 'if (true)\n{\n    int x = 1;\n}');
console.log('PASS safe C# formatting: literals, interpolation, raw/verbatim strings, comments, numbers, idempotence');

// Keep shipped examples stable too: no protected bytes/non-whitespace tokens may change.
const fs = require('fs'), path = require('path'); let templateCount = 0;
function walk(dir) {
  for (const ent of fs.readdirSync(dir, {withFileTypes:true})) {
    const file = path.join(dir, ent.name);
    if (ent.isDirectory()) { walk(file); continue; }
    if (!file.endsWith('.cs')) continue;
    const source = fs.readFileSync(file, 'utf8'), out = F.format(source, {reflow:true});
    const parts = text => F.lex(text).ranges.map(r => text.slice(r.start, r.end));
    assert.deepStrictEqual(parts(out), parts(source), file);
    assert.strictEqual(out.replace(/\s/g, ''), source.replace(/\s/g, ''), file);
    assert.strictEqual(F.format(out, {reflow:true}), out, file);
    templateCount++;
  }
}
walk(path.join(__dirname, '../../tools/studio/templates'));
console.log('PASS formatter preservation/idempotence across '+templateCount+' shipped C# templates');
