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
const mixed='class Demo{void Run(){var text=@"line one\n  line two  ";if(true){Console.WriteLine(text);}}}';
const mixedOut=F.format(mixed,{reflow:true});
assert(mixedOut.includes('class Demo\n{'),'reflow code around multiline literals');
assert(mixedOut.includes('if (true)\n'),'format later blocks, not just indentation');
assert.deepStrictEqual(F.lex(mixedOut).ranges.map(r=>mixedOut.slice(r.start,r.end)),F.lex(mixed).ranges.map(r=>mixed.slice(r.start,r.end)));
assert.strictEqual(F.format(mixedOut,{reflow:true}),mixedOut);
const crlf=F.format('if(true){\r\nint x=1;\r\n}',{reflow:true});
assert(!/(?<!\r)\n/.test(crlf),'new line breaks follow CRLF documents');
assert(F.format('if(true){x=1;}',{reflow:true,indentSize:2}).includes('\n  x = 1;'));
const literal='if(true){var s="a   b";}',formatted=F.format(literal,{reflow:true});
const caret=literal.indexOf('   ')+1;
assert.strictEqual(F.mapOffset(literal,formatted,caret),formatted.indexOf('"a   b"')+3,'caret inside protected whitespace stays exact');
console.log('PASS formatter 1.19: multiline-aware reflow, CRLF, indentation size and literal caret mapping');
