// Test the actual Editor.format method without requiring a DOM.
const assert = require('assert'), fs = require('fs'), path = require('path');
const win = {};
new Function('window', fs.readFileSync(path.join(__dirname, '../../tools/studio/lang.js'), 'utf8'))(win);
const source = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const method = source.slice(source.indexOf('  static format(src)'), source.indexOf('  inString(p)'));
const Editor = new Function('Lang', 'return class Editor {' + method + '};')(win.Lang);
const payloads = src => win.Lang.lexical(src).strings.map(r => src.slice(r.start, r.end));
for (const newline of ['\n', '\r\n', '\r']) {
    for (const prefix of ['@"', '$@"', '@$"']) {
        const src = ['if (true)', '{', 'var text = ' + prefix + 'first  ', '   {literal or interpolation}  ', '', '\t Unicode: فارسی 🚀 ""quote""  ', '  last";', 'Console.WriteLine(text);', '}'].join(newline);
        const formatted = Editor.format(src);
        assert.deepStrictEqual(payloads(formatted), payloads(src), 'multiline payload retained ' + prefix);
        assert.deepStrictEqual(formatted.match(/\r\n|\n|\r/g), src.match(/\r\n|\n|\r/g), 'line endings retained');
        assert(formatted.includes(newline + '    Console.WriteLine(text);'), 'following code reindented correctly');
        assert.strictEqual(Editor.format(formatted), formatted, 'idempotent');
    }
}
const mixed = 'if (true)\r\n{\nConsole.WriteLine("x");\r}\n';
assert.deepStrictEqual(Editor.format(mixed).match(/\r\n|\n|\r/g), mixed.match(/\r\n|\n|\r/g));
const comment = '/* first  \n   * keep  \n  */';
assert(Editor.format('if (true)\n{\n' + comment + '\nConsole.WriteLine(1);\n}').includes(comment), 'multiline comment retained');
const incomplete = 'if (true)\n{\nvar text = @"unfinished  \n   tail  ';
assert.deepStrictEqual(payloads(Editor.format(incomplete)), payloads(incomplete), 'incomplete string payload retained');
const src = 'if (true)\n{\nConsole.WriteLine($"{new[] {1, 2}[0]:000}");\n}';
assert(Editor.format(src).includes('\n    Console.WriteLine('), 'interpolation braces do not affect indentation');
console.log('PASS formatter: multiline payloads/comments, incomplete literals, interpolation, Unicode, mixed LF/CRLF/CR, idempotence');
// Optional end-to-end compiler check: node tests/studio/test_format.js ./mcs
if (process.argv[2]) {
    const { spawnSync } = require('child_process'), os = require('os');
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'microcs-format-'));
    try {
        for (const prefix of ['@"', '$@"', '@$"']) {
            const src = ['if (true)', '{', 'var text = ' + prefix + 'first  ', '   value {42}  ', '\tفارسی 🚀  ', ' last";', 'Console.WriteLine(text);', '}'].join('\n');
            const run = (name, text) => {
                const file = path.join(dir, name); fs.writeFileSync(file, text);
                const result = spawnSync(path.resolve(process.argv[2]), [file], { encoding: 'utf8', timeout: 5000 });
                assert.strictEqual(result.status, 0, result.stderr || String(result.error));
                return result.stdout;
            };
            assert.strictEqual(run('formatted.cs', Editor.format(src)), run('original.cs', src), 'runtime output unchanged by formatting');
        }
        console.log('PASS formatter compiled/runtime outputs unchanged for all multiline string prefixes');
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}
