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
        for (const [src, member] of [
            ['var display = new Tft(); display.', 'AttachLVGL'],
            ['var gui = new LvGui(160,80); gui.', 'Pump'],
            ['Tft.', 'Version'],
            ['new Tf', 'Tft'],
            ['var sprite = new TftSprite(display,12,10); sprite.', 'ReadPixel'],
            ['var sprite = new TftSprite(display,12,10,8); sprite.', 'PushTransparent'],
            ['new TftS', 'TftSprite'],
            ['var palette=new TftSprite(display,3,5,4); palette.', 'SetPaletteColor'],
            ['var bitmap=new TftSprite(display,9,3,1); bitmap.', 'BitmapColors'],
            ['var bitmap=new TftSprite(display,9,3,1); bitmap.', 'ReadValue']
        ]) {
            assert((await suggest(src)).includes(member), 'optional graphics help: ' + src);
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
        const suggestAt = async source => page.evaluate(source => {
            const p = source.indexOf('/*caret*/');
            ed.ta.value = source.replace('/*caret*/', ''); ed.ta.setSelectionRange(p, p);
            ed.refresh(true); ed.hideComplete(); ed.suggest(true);
            return ed.box.style.display === 'block' ? ed.items.map(i => i.label) : [];
        }, source);
        let visible = await suggestAt('void A(int first) { string leaked="x"; } void B(int second) { /*caret*/ }');
        assert(visible.includes('second') && !visible.includes('first') && !visible.includes('leaked'), 'sibling method locals/parameters must not leak');
        visible = await suggestAt('var item = new List<int>(); { string item="x"; } item./*caret*/');
        assert(visible.includes('Add') && !visible.includes('Substring'), 'closed shadow resolves outer list');
        visible = await suggestAt('var item = new List<int>(); { string item="x"; item./*caret*/ }');
        assert(visible.includes('Substring') && !visible.includes('Add'), 'inner shadow resolves string');
        visible = await suggestAt('void Outer() { void Local() {} } void Other() { /*caret*/ }');
        assert(!visible.includes('Local'), 'nested function must not leak into sibling');
        const leakedSignature = await page.evaluate(() => {
            const source='void Outer() { void Local(int n) {} } void Other() { Local(/*caret*/ }';
            const p=source.indexOf('/*caret*/'); ed.ta.value=source.replace('/*caret*/','');
            ed.ta.setSelectionRange(p,p); ed.sigUpdate(true); return ed.sig?.name || null;
        });
        assert.strictEqual(leakedSignature,null,'out-of-scope function parameter help must not leak');
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
        console.log('PASS real Studio scoped locals/shadows/functions and interpolation completion/signatures/hover; text suppression');
    } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
