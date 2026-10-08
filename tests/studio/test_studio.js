// Browser test for MicroCS Studio (tools/studio/index.html): a fake Web Serial port is wired to
// `mcs --repl --echo --sim --fs <dir>` (the device shell on the host).
//   npm i playwright-core   (or playwright)
//   CHROME=/usr/bin/google-chrome node tests/studio/test_studio.js [./mcs]
let chromium;
try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path');
const OUT = path.resolve(process.env.OUT || 'build/studio');
const MCS = path.resolve(process.argv[2] || './mcs');
const HTML = 'file://' + path.resolve(__dirname, '../../tools/studio/index.html');
const ROOT = OUT + '/fs';
fs.mkdirSync(OUT, { recursive: true });
fs.rmSync(ROOT, { recursive: true, force: true }); fs.mkdirSync(ROOT + '/lib', { recursive: true });
fs.writeFileSync(ROOT + '/hello.cs', 'Console.WriteLine("hi from file");\n');
fs.writeFileSync(ROOT + '/lib/util.cs', '// util\n');
fs.writeFileSync(ROOT + '/data.bin', Buffer.from([0, 1, 2, 3, 255, 4]));
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
let fails = 0; const check = (c, w) => { console.log((c ? 'PASS ' : 'FAIL ') + w); if (!c) fails++; };
(async () => {
  const proc = spawn(MCS, [process.env.MODE === 'shell' ? '--shell' : '--repl', ...(process.env.ECHO === '0' ? [] : ['--echo']), '--sim', '--fs', ROOT], { stdio: ['pipe', 'pipe', 'inherit'] });
  const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
  const page = await browser.newPage({ viewport: { width: 1400, height: 860 } });
  page.on('pageerror', e => { console.log('PAGEERROR', e.message); fails++; });
  page.on('console', m => { if (m.type() === 'error') console.log('CONSOLE', m.text()); });
  await page.exposeFunction('__serialWrite', (arr) => { proc.stdin.write(Buffer.from(arr)); });
  let chain = Promise.resolve();
  proc.stdout.on('data', (d) => { const a = [...d]; chain = chain.then(() => page.evaluate((b) => window.__serialPush(b), a).catch(() => {})); });
  await page.addInitScript(() => {
    let ctl;
    const port = {
      readable: null, writable: null,
      async open() {     // fresh streams on every open, like a real port
        port.readable = new ReadableStream({ start(c) { ctl = c; } });
        // window.__rom: the "board" sits in the ESP32 ROM bootloader until RTS resets it
        port.writable = new WritableStream({ write(chunk) {
          if (window.__rom) { if (!window.__romSaid) { window.__romSaid = 1; ctl.enqueue(new TextEncoder().encode('rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))\r\nwaiting for download\r\n')); } return; }
          return window.__serialWrite(Array.from(chunk)); } });
      },
      async close() { port.readable = port.writable = null; },
      async setSignals(s) { window.__signals = (window.__signals || []).concat([s]); if (s.requestToSend) window.__rom = 0; },
    };
    window.__serialPush = (b) => ctl.enqueue(new Uint8Array(b));
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port, addEventListener() {} } });
  });
  const tpl = async (name) => {
    if (!(await page.isVisible('#galleryBg'))) await page.click('#btnTemplates');
    await page.click(`#gCats div[data-c="All"]`);
    await page.click(`#gItems .item b:text-is("${name}")`);
    await page.click('#gAdd');
  };
  await page.goto(HTML);
  await page.screenshot({ path: OUT + '/0_start.png' });
  await page.click('#btnConnect');
  await page.waitForFunction(() => document.getElementById('connText').textContent.startsWith('MicroCS'), null, { timeout: 8000 }).catch(() => {});
  check((await page.textContent('#connText')).startsWith('MicroCS 1.'), 'connected, info read: ' + await page.textContent('#connText'));
  await page.waitForSelector('.file');
  const names = await page.$$eval('#fileList .name', els => els.map(e => e.textContent));
  check(JSON.stringify(names) === JSON.stringify(['lib', 'data.bin', 'hello.cs']), 'file list ' + JSON.stringify(names));
  check(/free of/.test(await page.textContent('#stText')), 'storage: ' + await page.textContent('#stText'));
  check(/VM heap/.test(await page.textContent('#memInfo')), 'mem: ' + await page.textContent('#memInfo'));

  // REPL
  await page.fill('#cin', 'var x = 21;'); await page.press('#cin', 'Enter');
  await page.fill('#cin', 'x * 2'); await page.press('#cin', 'Enter');
  await page.waitForFunction(() => /\n42\n/.test(document.getElementById('term').innerText), null, { timeout: 5000 }).catch(() => {});
  let tt = await page.innerText('#term');
  check(/> var x = 21;\n> x \* 2\n42/.test(tt), 'REPL keeps state, echo filtered');
  await page.fill('#cin', 'for (int i = 0; i < 2; i++) {\n  Console.WriteLine("n" + i);\n}'); await page.press('#cin', 'Enter');
  await page.waitForFunction(() => /n1\n/.test(document.getElementById('term').innerText), null, { timeout: 5000 }).catch(() => {});
  tt = await page.innerText('#term');
  check(/\.\.\.   Console.WriteLine\("n" \+ i\);\n\.\.\. }\nn0\nn1/.test(tt), 'multi-line REPL input');

  // open a file, edit, save, run
  await page.click('#fileList .file:has-text("hello.cs") .name');
  await page.waitForSelector('.tab.active');
  check((await page.inputValue('#ta')).includes('hi from file'), 'opened hello.cs in editor');
  await page.click('#ta'); await page.keyboard.press('Control+End');
  await page.keyboard.type('Console.WriteLine(6 * 7');
  check((await page.inputValue('#ta')).endsWith('Console.WriteLine(6 * 7)'), 'auto-close paren');
  await page.keyboard.type(');');
  check((await page.inputValue('#ta')).endsWith('Console.WriteLine(6 * 7);'), 'overtype closing paren');
  check(await page.$('.tab.dirty') !== null, 'tab marked dirty');
  await page.keyboard.press('F5');
  await page.waitForFunction(() => /finished in/.test(document.getElementById('term').innerText), null, { timeout: 8000 }).catch(() => {});
  tt = await page.innerText('#term');
  check(/▶ run \/hello.cs\nhi from file\n42\n✓ finished/.test(tt), 'save + run streams output');
  check(fs.readFileSync(ROOT + '/hello.cs', 'utf8').endsWith('Console.WriteLine(6 * 7);'), 'saved to device fs');
  check(await page.$('.tab.dirty') === null, 'tab clean after save');

  // REPL again after machine ops
  await page.fill('#cin', 'x + 1'); await page.press('#cin', 'Enter');
  await page.waitForFunction(() => /\n22\n/.test(document.getElementById('term').innerText), null, { timeout: 5000 }).catch(() => {});
  check(/> x \+ 1\n22/.test(await page.innerText('#term')), 'REPL state survives file ops');

  // completion
  await page.click('#ta'); await page.keyboard.press('Control+End'); await page.keyboard.press('Enter');
  await page.keyboard.type('Dri');
  await sleep(100);
  check(await page.isVisible('#complete'), 'completion popup');
  await page.keyboard.press('Enter');
  await page.keyboard.type('.');
  await page.keyboard.press('Control+Space');
  const items = await page.$$eval('#complete div', els => els.map(e => e.textContent));
  check(items.some(t => t.startsWith('MGetDrives')), 'member completion: ' + items.slice(0, 3));
  await page.keyboard.press('Escape');
  await page.keyboard.press('Control+z'); 

  // template -> save as -> run
  // template gallery
  await page.click('#btnTemplates');
  await page.waitForSelector('#galleryBg.show');
  const nTpl = await page.$$eval('#gItems .item', els => els.length);
  check(nTpl >= 80, nTpl + ' templates in the gallery');
  check(await page.$('#gCats div[data-c="Boot and startup"]') !== null && await page.$('#gCats div[data-c="Scheduler"]') !== null, 'boot and scheduler categories');
  await page.fill('#gSearch', 'jobs.cfg');
  await page.click('#gItems .item b:text-is("jobs.cfg - scheduled scripts")');
  check(/every\s+500ms\s+\/blink\.cs/.test(await page.textContent('#gPrev')) && (await page.inputValue('#gName')) === 'jobs.cfg', 'search finds jobs.cfg, preview + file name');
  await page.fill('#gSearch', '');
  await page.click('#gCats div[data-c="Scheduler"]'); await page.click('#gItems .item b:text-is("State machine with jobs")');
  await page.screenshot({ path: OUT + '/4_templates.png' });
  await tpl('Files and free space');
  await page.click('#btnRun');
  await page.waitForSelector('#dlgIn');
  check((await page.inputValue('#dlgIn')) === '/files.cs', 'save dialog suggests ' + await page.inputValue('#dlgIn'));
  await page.press('#dlgIn', 'Enter');
  await page.waitForFunction(() => (document.getElementById('term').innerText.match(/finished in/g) || []).length >= 2, null, { timeout: 8000 }).catch(() => {});
  tt = await page.innerText('#term');
  check(/KB free of .* KB \(posix\)/.test(tt), 'template ran (DriveInfo)');
  check(fs.existsSync(ROOT + '/files.cs') && fs.existsSync(ROOT + '/log.txt'), 'files.cs saved, log.txt written by script');

  // compile error -> error line
  await tpl('Hello world');
  await page.click('#ta'); await page.keyboard.press('Control+End'); await page.keyboard.type('int y = ;');
  await page.keyboard.press('Control+s');
  await page.waitForSelector('#dlgIn'); await page.fill('#dlgIn', '/bad.cs'); await page.press('#dlgIn', 'Enter');
  await sleep(500);
  await page.keyboard.press('F5');
  await page.waitForFunction(() => /✗/.test(document.getElementById('term').innerText), null, { timeout: 8000 }).catch(() => {});
  tt = await page.innerText('#term');
  check(/bad\.cs\(4,9\): error/.test(tt) && /✗ failed/.test(tt), 'compile error reported: ' + (tt.match(/✗.*/) || [''])[0]);
  check(await page.isVisible('#errline'), 'error line highlighted');

  // stop a running loop
  await tpl('Button interrupt');
  await page.keyboard.press('F5');
  await page.waitForSelector('#dlgIn'); await page.press('#dlgIn', 'Enter');
  await page.waitForFunction(() => /Press the button/.test(document.getElementById('term').innerText), null, { timeout: 8000 }).catch(() => {});
  check(!(await page.isDisabled('#btnStop')), 'stop enabled while running');
  await page.click('#btnStop');
  await page.waitForFunction(() => /■ stopped/.test(document.getElementById('term').innerText), null, { timeout: 5000 }).catch(() => {});
  check(/■ stopped/.test(await page.innerText('#term')), 'script stopped');
  // close button on the document tab
  const nTabs = await page.$$eval('.tab', e => e.length);
  await page.hover('.tab.active'); await page.click('.tab.active .x'); await sleep(200);
  check((await page.$$eval('.tab', e => e.length)) === nTabs - 1, 'tab close button closes the tab');

  // upload
  fs.writeFileSync(OUT + '/up.txt', 'uploaded text\n'.repeat(500));
  await page.setInputFiles('#fileInput', OUT + '/up.txt');
  await page.waitForFunction(() => [...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'up.txt'), null, { timeout: 8000 }).catch(() => {});
  check(fs.existsSync(ROOT + '/up.txt') && fs.readFileSync(ROOT + '/up.txt', 'utf8') === 'uploaded text\n'.repeat(500), 'upload 7 KB');
  // binary open
  await page.click('#fileList .file:has-text("data.bin") .name');
  await page.waitForSelector('#hex', { state: 'visible' });
  check(/00 01 02 03 ff 04/.test(await page.innerText('#hex')), 'binary file shown as hex');
  // folder navigation
  await page.click('#fileList .file:has-text("lib") .name');
  await page.waitForFunction(() => [...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'util.cs'));
  check(true, 'navigated into /lib');
  await page.click('#crumbs a[data-p="/"]');
  await page.waitForFunction(() => [...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'hello.cs'));
  // rename
  await page.hover('#fileList .file:has-text("up.txt")');
  await page.click('#fileList .file:has-text("up.txt") button[data-a="mv"]');
  await page.waitForSelector('#dlgIn'); await page.fill('#dlgIn', 'renamed.txt'); await page.press('#dlgIn', 'Enter');
  await page.waitForFunction(() => [...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'renamed.txt'), null, { timeout: 5000 }).catch(() => {});
  check(fs.existsSync(ROOT + '/renamed.txt') && !fs.existsSync(ROOT + '/up.txt'), 'rename');
  // delete folder recursively
  await page.hover('#fileList .file:has-text("lib")');
  await page.click('#fileList .file:has-text("lib") button[data-a="rm"]');
  await page.click('#dlgOk');
  await page.waitForFunction(() => ![...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'lib'), null, { timeout: 5000 }).catch(() => {});
  check(!fs.existsSync(ROOT + '/lib'), 'recursive delete');
  // shell mode
  await page.click('#consMode button[data-m="shell"]');
  await page.fill('#cin', 'cat /renamed.txt'); await page.press('#cin', 'Enter');
  await sleep(500);
  check(/\$ cat \/renamed.txt\nuploaded text/.test(await page.innerText('#term')), 'shell command');
  await page.fill('#cin', 'bogus'); await page.press('#cin', 'Enter'); await sleep(400);
  check(/unknown command/.test(await page.innerText('#term')), 'shell error shown');
  await page.click('#consMode button[data-m="repl"]');
  // new folder + new file
  await page.click('#btnNewDir'); await page.waitForSelector('#dlgIn'); await page.fill('#dlgIn', 'apps'); await page.press('#dlgIn', 'Enter');
  await page.waitForFunction(() => [...document.querySelectorAll('#fileList .name')].some(e => e.textContent === 'apps'), null, { timeout: 5000 }).catch(() => {});
  check(fs.existsSync(ROOT + '/apps'), 'mkdir');
  await page.click('.tab:has-text("files.cs")');
  await page.click('#ta'); await page.keyboard.press('Control+End'); await page.keyboard.press('Enter');
  await page.keyboard.type('File.Re');
  await sleep(150);
  await page.screenshot({ path: OUT + '/3_editor.png' });
  await page.keyboard.press('Escape');
  await page.screenshot({ path: OUT + '/1_main.png' });
  await page.click('#btnReset'); await sleep(2500);
  check(await page.evaluate(() => (window.__signals || []).some(s => s.requestToSend === true)), 'reset pulses RTS');
  check(/MicroCS/.test(await page.textContent('#connText')), 'still connected after reset');
  // board stuck in download mode on connect: Studio resets it and connects
  await page.click('#btnConnect'); await sleep(300);
  await page.evaluate(() => { window.__rom = 1; window.__romSaid = 0; window.__signals = []; dev.lastAutoReset = 0; });
  await page.click('#btnConnect');
  await page.waitForFunction(() => /bootloader[\s\S]*— ready/.test(document.getElementById('term').innerText.split('— connected at').pop()), null, { timeout: 15000 }).catch(() => {});
  tt = await page.innerText('#term');
  check(/bootloader \(download mode\): resetting it[\s\S]*— ready/.test(tt), 'download mode detected, board reset, connected');
  await page.click('#btnTheme'); await sleep(200);
  await page.screenshot({ path: OUT + '/2_light.png' });
  console.log(fails ? `${fails} FAILED` : 'ALL PASSED');
  fs.writeFileSync(OUT + '/term.txt', await page.innerText('#term'));
  await browser.close(); proc.kill(); process.exit(fails ? 1 : 0);
})().catch(e => { console.log('ERROR', e); process.exit(2); });
