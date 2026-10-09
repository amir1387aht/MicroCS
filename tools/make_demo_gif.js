// Records assets/demo.gif: MicroCS Studio (tools/studio) wired to the host device shell
// (`mcs --repl --sim`) through a fake Web Serial port, typing a script and running it.
//   CHROME=$(command -v chromium) node tools/make_demo_gif.js [./mcs]      (needs playwright + ffmpeg)
let chromium;
try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const { spawn, execFileSync } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const MCS = path.resolve(process.argv[2] || './mcs');
const OUT = path.resolve(__dirname, '../assets/demo.gif');
const HTML = 'file://' + path.resolve(__dirname, 'studio/index.html');
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'mcs-demo-'));
const ROOT = TMP + '/fs', FR = TMP + '/frames';
fs.mkdirSync(ROOT); fs.mkdirSync(FR);
fs.writeFileSync(ROOT + '/main.cs', '');
fs.writeFileSync(ROOT + '/blink.cs', 'GPIO.Mode(25, GPIO.Output);\nScheduler.Every(500, () => GPIO.Toggle(25));\n');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
(async () => {
  const proc = spawn(MCS, ['--repl', '--echo', '--sim', '--fs', ROOT], { stdio: ['pipe', 'pipe', 'inherit'] });
  const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
  const page = await browser.newPage({ viewport: { width: 1200, height: 700 } });
  await page.exposeFunction('__serialWrite', (a) => { proc.stdin.write(Buffer.from(a)); });
  let chain = Promise.resolve();
  proc.stdout.on('data', (d) => { const a = [...d]; chain = chain.then(() => page.evaluate((b) => window.__serialPush(b), a).catch(() => {})); });
  await page.addInitScript(() => {
    let ctl;
    const port = {
      readable: null, writable: null,
      async open() { port.readable = new ReadableStream({ start(c) { ctl = c; } }); port.writable = new WritableStream({ write(ch) { return window.__serialWrite(Array.from(ch)); } }); },
      async close() { port.readable = port.writable = null; }, async setSignals() {},
    };
    window.__serialPush = (b) => ctl.enqueue(new Uint8Array(b));
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port, getPorts: async () => [port], addEventListener() {} } });
  });
  let n = 0, recording = true;
  const snap = async (times = 1) => { const f = FR + `/f${String(n).padStart(4, '0')}.png`; await page.screenshot({ path: f }); for (let i = 1; i < times; i++) fs.copyFileSync(f, FR + `/f${String(n + i).padStart(4, '0')}.png`); n += times; };
  const rec = (async () => { while (recording) { await snap(); await sleep(60); } })();
  await page.goto(HTML);
  await sleep(600);
  await page.click('#btnConnect');
  await page.waitForFunction(() => document.getElementById('connText').textContent.startsWith('MicroCS'), null, { timeout: 8000 });
  await page.waitForSelector('.file');
  await sleep(500);
  await page.click('#fileList .file:has-text("main.cs") .name');
  await page.waitForSelector('.tab.active');
  await page.click('#ta');
  const lines = [
    '// C# on the board: type, F5, done',
    'var rnd = new Random();',
    'for (int i = 1; i <= 3; i++) Console.WriteLine($"roll {i}: {rnd.Next(1, 7)}");',
    'var list = new List<int> { 3, 1, 4, 1, 5, 9 };',
    'Console.WriteLine($"max {list.Max()}, sum {list.Sum()} on {Hal.Board}");',
  ];
  for (const l of lines) {
    for (const ch of l) { await page.keyboard.type(ch); await sleep(ch === ' ' ? 25 : 35); }
    await page.keyboard.press('Escape');
    await page.keyboard.press('End');
    await page.keyboard.press('Enter');
    await page.keyboard.press('Home');
  }
  await sleep(400);
  await page.keyboard.press('F5');
  await page.waitForFunction(() => /finished in/.test(document.getElementById('term').innerText), null, { timeout: 8000 }).catch(() => {});
  await sleep(2500);
  recording = false; await rec;
  const text = await page.inputValue('#ta');
  console.log(text, '\n---\n', (await page.innerText('#term')).split('\n').slice(-8).join('\n'));
  await browser.close(); proc.kill();
  // 1 frame per screenshot at ~10 fps, 960 px wide, shared palette
  execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-framerate', '10', '-i', FR + '/f%04d.png', '-vf',
    'scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle', OUT]);
  console.log(OUT, fs.statSync(OUT).size, 'bytes,', n, 'frames');
  fs.rmSync(TMP, { recursive: true, force: true });
  process.exit(0);
})().catch((e) => { console.error(e); process.exit(1); });
