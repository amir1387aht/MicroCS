// A real LVGL foreground loop occupies the shell queue. File operations must not
// wait forever behind it, and stopping it must require explicit consent.
let chromium; try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path'), assert = require('assert');
const root = path.resolve('build/studio-lvgl-files-fs');
fs.rmSync(root, { recursive: true, force: true }); fs.mkdirSync(root, { recursive: true });
const example = fs.readFileSync('tools/studio/templates/33-lvgl/hello_label.cs', 'utf8')
  .replace('while (true)', 'Console.WriteLine("LVGL_RUNNING");\nwhile (true)');
fs.writeFileSync(root + '/gui.cs', example);
fs.writeFileSync(root + '/delete.cs', 'Console.WriteLine("keep draft");\n');
fs.writeFileSync(root + '/other.cs', '// another file\n');
const proc = spawn(path.resolve(process.argv[2] || 'build/mcs_lvgl'), ['--lvgl=320x240', '--repl', '--echo', '--sim', '--no-boot', '--fs', root], { stdio: ['pipe', 'pipe', 'pipe'] });
let browser, page, closing = false, chain = Promise.resolve(); const errors = [];
proc.stderr.on('data', d => { if (!closing) process.stderr.write(d); });
proc.stdin.on('error', e => { if (!closing) errors.push('stdin: ' + e.message); });
proc.on('error', e => { if (!closing) errors.push('runtime: ' + e.message); });
proc.on('exit', (code, signal) => { if (!closing) errors.push('runtime exited: ' + code + '/' + signal); });
(async () => {
  browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
  page = await browser.newPage(); page.on('pageerror', e => errors.push(e.message));
  await page.exposeFunction('__write', data => new Promise((resolve, reject) => proc.stdin.write(Buffer.from(data), e => e ? reject(e) : resolve())));
  proc.stdout.on('data', data => { chain = chain.then(() => page.evaluate(a => window.__push?.(a), [...data]).catch(() => {})); });
  await page.addInitScript(() => {
    let ctl; const port = { async open() { port.readable = new ReadableStream({ start(c) { ctl = c; } }); port.writable = new WritableStream({ write(a) { return window.__write([...a]); } }); }, async close() {}, async setSignals() {} };
    window.__push = a => ctl?.enqueue(new Uint8Array(a));
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port, getPorts: async () => [port], addEventListener() {} } });
  });
  await page.goto('file://' + path.resolve('tools/studio/index.html'));
  await page.click('#btnConnect');
  await page.waitForFunction(() => dev.connected && App.capabilitiesKnown && App.capabilities.get('Lvgl') === true, null, { timeout: 15000 });
  await page.evaluate(() => { window.__run = App.workflow(() => App.runPath('/gui.cs')); });
  await page.waitForFunction(() => dev.running && $('term').innerText.includes('LVGL_RUNNING'), null, { timeout: 10000 });
  await page.evaluate(() => { window.__delete = App.remove('/delete.cs', false); });
  await page.waitForSelector('#modalBg.show');
  assert.match(await page.innerText('#modalBg'), /running script.*stop|stop.*running script/i, 'explain that deletion will stop the script');
  await page.click('#dlgNo'); await page.evaluate(() => window.__delete);
  assert(fs.existsSync(root + '/delete.cs'), 'cancel must not delete');
  assert(await page.evaluate(() => dev.running), 'cancel must not stop the script');
  await page.evaluate(() => { App.openTab({path:'/delete.cs', text:'unsaved draft', saved:'saved'}); window.__delete = App.remove('/delete.cs', false); });
  await page.waitForSelector('#modalBg.show'); await page.click('#dlgOk');
  await Promise.race([page.evaluate(() => window.__delete), new Promise((_, reject) => setTimeout(() => reject(Error('delete was queued behind the running LVGL script')), 8000))]);
  assert(!fs.existsSync(root + '/delete.cs'), 'confirmed delete must remove file');
  assert(!await page.evaluate(() => dev.running), 'foreground LVGL script must be stopped');
  assert.strictEqual(await page.evaluate(() => App.tabs.find(t => t.path === '/delete.cs')?.text), 'unsaved draft');
  assert(await page.evaluate(() => App.tabs.find(t => t.path === '/delete.cs')?.dirty), 'deleted open file remains recoverable');
  // A new foreground run (or a replaced connection) must never be stopped based
  // on confirmation for a different run/device.
  await page.evaluate(() => { window.__run = App.runPath('/gui.cs'); });
  await page.waitForFunction(() => dev.running && $('term').innerText.includes('LVGL_RUNNING'), null, { timeout: 10000 });
  await page.evaluate(() => { window.__delete = App.remove('/other.cs', false); });
  await page.waitForSelector('#modalBg.show');
  await page.evaluate(() => { window.__oldToken = App.activeRunToken; App.activeRunToken = {}; });
  await page.click('#dlgOk'); await page.evaluate(() => window.__delete);
  assert(fs.existsSync(root + '/other.cs') && await page.evaluate(() => dev.running), 'do not delete or interrupt a different run');
  await page.evaluate(() => { App.activeRunToken = window.__oldToken; window.__delete = App.remove('/other.cs', false); });
  await page.waitForSelector('#modalBg.show'); await page.click('#dlgOk');
  await Promise.race([page.evaluate(() => window.__delete), new Promise((_, reject) => setTimeout(() => reject(Error('direct run delete timed out')), 8000))]);
  assert(!fs.existsSync(root + '/other.cs') && !await page.evaluate(() => dev.running), 'direct run must be stopped before delete');
  assert.deepStrictEqual(errors, []);
  console.log('PASS LVGL foreground delete: workflow and direct runs, consent, cancellation, race protection and draft preservation');
})().catch(e => { console.error(e); process.exitCode = 1; }).finally(async () => {
  closing = true; try { if (browser) await browser.close(); } finally { proc.stdin.destroy(); proc.kill(); }
});
