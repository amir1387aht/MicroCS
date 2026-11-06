// Acceptance test: an infinite LVGL script must not stop or block Studio
// file operations or a separate code command. Output must not corrupt transfers.
let chromium; try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path'), assert = require('assert');
const root = path.resolve('build/studio-lvgl-files-fs');
fs.rmSync(root, { recursive: true, force: true }); fs.mkdirSync(root, { recursive: true });
const example = fs.readFileSync('tools/studio/templates/33-lvgl/hello_label.cs', 'utf8')
  .replace('while (true)', 'Console.WriteLine("LVGL_RUNNING");\nvar timer = lv_timer_create(t => Console.WriteLine("GUI_TIMER"), 100, null);\nint beat = 0;\nwhile (true)')
  .replace('    lv_timer_handler();', '    if (++beat % 20 == 0) Console.WriteLine("GUI_HEARTBEAT");\n    lvgl_host_run(5);');
fs.writeFileSync(root + '/gui.cs', example);
fs.writeFileSync(root + '/delete.cs', 'Console.WriteLine("keep draft");\n');
fs.writeFileSync(root + '/other.cs', '// another file\n');
fs.writeFileSync(root + '/tight.cs', 'Console.WriteLine("TIGHT_STARTED"); lv_timer_create(t => Console.WriteLine("TIGHT_TIMER"), 100, null); Scheduler.Every(100, () => Console.WriteLine("SCHED_TIMER")); while (true) { }');
fs.writeFileSync(root + '/tight-delete.cs', '// delete during tight loop\n');
fs.writeFileSync(root + '/callback.cs', 'lv_timer_create(t => { Console.WriteLine("CALLBACK_BUSY"); while (true) {} }, 10, null); while (true) {}');
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
  // Unrelated deletion must not ask to stop or stop the running GUI.
  await page.evaluate(() => { window.__delete = App.remove('/delete.cs', false); });
  await page.waitForSelector('#modalBg.show');
  const prompt = await page.innerText('#modalBg');
  assert(!/stop.*running|running.*stop/i.test(prompt), 'delete must not prompt to stop the GUI');
  await page.click('#dlgOk');
  await Promise.race([page.evaluate(() => window.__delete), new Promise((_, reject) => setTimeout(() => reject(Error('delete blocked behind LVGL script')), 8000))]);
  assert(!fs.existsSync(root + '/delete.cs'), 'delete must complete while GUI runs');
  assert(await page.evaluate(() => dev.running), 'GUI must still be running after deletion');
  // Background text must not corrupt the binary file protocol.
  const code = await page.evaluate(() => dev.op(() => dev.cmd('exec Console.WriteLine("OTHER_CODE_OK");')));
  assert.strictEqual(code.status, 'OK', 'other code command must run without stopping GUI');
  assert.match(Buffer.from(code.out).toString('utf8'), /OTHER_CODE_OK/);
  const bytes = await page.evaluate(() => dev.op(async () => [...await dev.get('/other.cs')]));
  assert.strictEqual(Buffer.from(bytes).toString('utf8'), '// another file\n', 'download remains byte-exact while GUI outputs');
  const beatsBefore = await page.evaluate(() => ($('term').innerText.match(/GUI_TIMER/g) || []).length);
  await page.waitForFunction(n => ($('term').innerText.match(/GUI_TIMER/g) || []).length > n, beatsBefore, { timeout: 5000 });
  await page.evaluate(() => App.stop());
  await page.waitForFunction(() => !dev.running, null, { timeout: 10000 });
  await page.evaluate(() => App.workflow(() => App.runPath('/tight.cs')));
  await page.waitForFunction(() => dev.running && $('term').innerText.includes('TIGHT_STARTED'), null, { timeout: 10000 });
  await page.waitForFunction(() => $('term').innerText.includes('TIGHT_TIMER') && $('term').innerText.includes('SCHED_TIMER'), null, { timeout: 10000 });
  await page.evaluate(() => { window.__tightDelete = App.remove('/tight-delete.cs', false); });
  await page.waitForSelector('#modalBg.show');
  assert(!/stop.*running|running.*stop/i.test(await page.innerText('#modalBg')));
  await page.click('#dlgOk');
  await Promise.race([page.evaluate(() => window.__tightDelete), new Promise((_, reject) => setTimeout(() => reject(Error('delete blocked behind tight loop')), 8000))]);
  assert(!fs.existsSync(root + '/tight-delete.cs'));
  assert(await page.evaluate(() => dev.running));
  await page.evaluate(() => App.stop());
  await page.waitForFunction(() => !dev.running, null, { timeout: 10000 });
  await page.evaluate(() => App.workflow(() => App.runPath('/callback.cs')));
  await page.waitForFunction(() => dev.running && $('term').innerText.includes('CALLBACK_BUSY'), null, { timeout: 10000 });
  await page.evaluate(() => App.stop());
  await page.waitForFunction(() => !dev.running, null, { timeout: 10000 });
  assert.deepStrictEqual(errors, []);
  console.log('PASS concurrent LVGL, scheduler and deletion in sleeping and tight loops; Stop interrupts a busy callback');
})().catch(async e => { console.error(e); if (page) console.error('STATE', await page.evaluate(() => ({running: dev.running, background: dev.backgroundRun, caps: [...(App.capabilities || [])], term: $('term').innerText.slice(-3000)})).catch(() => null)); process.exitCode = 1; }).finally(async () => {
  closing = true; try { if (browser) await browser.close(); } finally { proc.stdin.destroy(); proc.kill(); }
});
