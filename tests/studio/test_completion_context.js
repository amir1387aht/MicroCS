// Offline browser regression for the real editor (no fake language service).
let chromium;
try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const assert = require('assert'), path = require('path'), fs = require('fs');
const OUT = path.resolve(process.env.OUT || 'build/studio-context');
fs.mkdirSync(OUT, { recursive: true });
(async () => {
    const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
    try {
        const page = await browser.newPage({ viewport: { width: 1400, height: 860 } });
        const errors = [];
        page.on('pageerror', e => errors.push(e.message));
        await page.route('https://**', r => r.abort());
        await page.goto('file://' + path.resolve(__dirname, '../../tools/studio/index.html'));
        await page.evaluate(() => App.newScratch());
        const suggest = async src => page.evaluate(src => {
            ed.ta.value = src; ed.ta.setSelectionRange(src.length, src.length);
            ed.refresh(true); ed.hideComplete(); ed.suggest(true);
            return ed.box.style.display === 'block' ? ed.items.map(i => i.label) : [];
        }, src);
        for (const src of ['$"{Con', '$@"line\n{Con', '@$"line\n{Con', '$"{x /* } */ + Con', '$"{$"{Con']) {
            assert((await suggest(src)).includes('Console'), 'Console completion: ' + src);
        }
        for (const src of ['$"{Console.', '$@"line\n{Console.', '@$"line\n{Console.', '$"{Call("}", Console.']) {
            assert((await suggest(src)).includes('WriteLine'), 'member completion: ' + src);
        }
        for (const src of ['$"literal Con', '$"{{Con', '@"line\nCon', '$"{x:Con', '$"{x /* Con', '// Con', '$"{Call("Con']) {
            assert.deepStrictEqual(await suggest(src), [], 'no completion in text/comment/format: ' + src);
        }
        for (const src of ['$"{Console.WriteLine(', '$@"line\n{Console.WriteLine(']) {
            const sig = await page.evaluate(src => {
                ed.ta.value = src; ed.ta.setSelectionRange(src.length, src.length); ed.sigUpdate(true);
                return ed.sig?.name;
            }, src);
            assert.strictEqual(sig, 'WriteLine', 'parameter help in interpolation');
        }
        const cleared = await page.evaluate(() => {
            ed.ta.value = '$"literal Con'; ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length);
            ed.sigUpdate(true); return !ed.sig;
        });
        assert(cleared, 'explicit parameter help cannot leak a stale popup into literal text');
        assert(await page.evaluate(() => !!ed.describe('$"{Console.WriteLine}', { a: 11, word: 'WriteLine' })), 'hover documents interpolation member');
        // Exercise real keyboard input too: auto-paired quotes/braces must not
        // suppress assistance when the caret is still inside an expression.
        for (const [typed, label] of [['$"{Con', 'Console'], ['$"{Console.', 'WriteLine'], ['$@"{Con', 'Console'], ['@$"{Console.', 'WriteLine']]) {
            await page.fill('#ta', ''); await page.click('#ta');
            await page.keyboard.type(typed, { delay: 10 });
            await page.waitForFunction(label => ed.box.style.display === 'block' && ed.items.some(i => i.label === label), label);
        }
        await suggest('$@"status\n{Console.');
        await page.screenshot({ path: OUT + '/interpolation-completion.png' });
        assert.deepStrictEqual(errors, [], 'no page errors');
        console.log('PASS real Studio completion/signatures/hover in nested, multiline and incomplete interpolations; text suppression');
    } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
