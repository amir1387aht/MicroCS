// Shared lexical regressions. Node only; actual completion UI is tested separately.
const assert = require('assert'), fs = require('fs'), path = require('path');
const window = {};
new Function('window', fs.readFileSync(path.join(__dirname, '../../tools/studio/lang.js'), 'utf8'))(window);
const Lang = window.Lang;
const cases = [
    ['$"{Con', 'code'], ['$"{Console.', 'code'],
    ['$@"line\n{Console.', 'code'], ['@$"line\n{Console.', 'code'],
    ['$"text Con', 'string'], ['@"line\nCon', 'string'],
    ['$"{{Con', 'string'], ['$"{x} text Con', 'string'],
    ['$"{x:Con', 'format'], ['$"{x,10:000', 'format'],
    ['$"{x, wid', 'code'], ['$"{ok ? Con', 'code'],
    ['$"{ok ? x : Con', 'code'], ['$"{x ?? Con', 'code'],
    ['$"{x?.Con', 'code'], ['$"{x?[Con', 'code'],
    ['$"{(ok ? x : y):Con', 'format'], ['$"{x:HH:mm:ss', 'format'],
    ['$"{Call("Con', 'string'], ['$"{Call("}", Con', 'code'],
    ['$"{new[] {"}", "{"}[0] + Con', 'code'],
    ['$"{x /* } \' \\" Con', 'comment'], ['$"{x /* } */ + Con', 'code'],
    ['$@"{x // } Con', 'comment'], ['$@"{x // }\n + Con', 'code'],
    ['$"nested {$"inner {Con', 'code'], ['$"nested {$"inner Con', 'string'],
    ['$"{\'Con', 'char'], ['$"{\'}\' + Con', 'code'],
    ['// Con', 'comment'], ['/* Con', 'comment'], ['/* Con */ Con', 'code'],
    ['"escaped \\" Con', 'string'], ['"closed" Con', 'code'],
    ['@"doubled "" Con', 'string'], ['@"closed" Con', 'code'],
    ['"invalid\nCon', 'code'], ['Con', 'code'], ['', 'code'],
];
for (const [src, context] of cases) {
    assert.strictEqual(Lang.contextAt(src, src.length), context, JSON.stringify(src));
    const blank = Lang.blank(src);
    assert.strictEqual(blank.length, src.length, 'offsets retained');
    for (let i = 0; i < src.length; i++) if (/\r|\n/.test(src[i])) assert.strictEqual(blank[i], src[i], 'line endings retained');
}
const full = '$@"literal {Console.WriteLine(1, Math.Max(2, 3)):000} tail";';
assert.strictEqual(Lang.contextAt(full, full.indexOf('Console')), 'code');
assert.strictEqual(Lang.contextAt(full, full.indexOf('literal')), 'string');
assert.strictEqual(Lang.contextAt(full, full.indexOf('000')), 'format');
assert.strictEqual(Lang.contextAt(full, full.indexOf('tail')), 'string');
assert(Lang.blank(full).includes('Console.WriteLine(1, Math.Max(2, 3))'), 'expressions retained');
assert(!Lang.blank(full, true).includes('Console'), 'formatter mask ignores entire literal');
for (const src of ['$"{Console.WriteLine(1,', '$@"line\n{Console.WriteLine(1,', '$"{$"{Console.WriteLine(1,']) {
    const ctx = Lang.callContext(src, src.length);
    assert(ctx && ctx.name === 'WriteLine' && ctx.recv === 'Console' && ctx.arg === 1, 'signature context: ' + src);
}
assert.strictEqual(Lang.receiverBefore('$"{Console.', 10), 'Console');
const deep = '$"{'.repeat(1000) + 'Con';
assert.doesNotThrow(() => Lang.lexical(deep), 'nesting is bounded');
const model = Lang.lexical(full);
assert.strictEqual(model, Lang.lexical(full), 'unchanged document reuses model');
assert.notStrictEqual(model, Lang.lexical(full + ' '), 'edit invalidates model');
console.log('PASS shared lexer: contexts, offsets, nested interpolation, signatures, formats, cache and depth bound');
