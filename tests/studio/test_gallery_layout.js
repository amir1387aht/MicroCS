// Geometry regression: capability status must not consume the template body row.
// CHROME=/path/to/chromium node tests/studio/test_gallery_layout.js
let chromium;
try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const assert = require('assert'), path = require('path'), fs = require('fs');
const OUT = path.resolve(process.env.OUT || 'build/studio-gallery');
fs.mkdirSync(OUT, { recursive: true });
(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.route('https://**', r => r.abort()); // offline, deterministic, no remote template fetch
    await page.goto('file://' + path.resolve(__dirname, '../../tools/studio/index.html'));
    await page.click('#btnTemplates');
    for (const [width, height] of [[1910, 900], [1400, 860], [800, 600], [390, 844], [667, 375]]) {
      await page.setViewportSize({ width, height });
      for (const state of ['offline', 'connected', 'legacy', 'empty']) {
        await page.evaluate(state => {
          dev.port = state === 'offline' ? null : {};
          App.capabilitiesKnown = state !== 'legacy';
          App.capabilities = new Map(state === 'empty' ? [] : [['Compiler', true], ['GPIO', true]]);
          App.gal.cat = 'All'; App.gal.q = ''; App.gal.sel = 0; App.galRender();
        }, state);
        const boxes = await page.evaluate(() => {
          const box = s => { const e = document.querySelector(s), r = e.getBoundingClientRect();
            return { x:r.x, y:r.y, right:r.right, bottom:r.bottom, width:r.width, height:r.height,
              visible:getComputedStyle(e).display !== 'none', scrollWidth:e.scrollWidth, clientWidth:e.clientWidth }; };
          return { dialog:box('.gallery'), head:box('.gh'), status:box('#gSupport'), body:box('.gb'),
            cats:box('#gCats'), items:box('#gItems'), preview:box('.prev'), info:box('#gInfo'), code:box('#gPrev'),
            foot:box('.gf'), actions:['#gName','#gInsert','#gAdd','#gCancel','#gSearch','#gClose','#gSupportedOnly'].map(box) };
        });
        const label = `${width}x${height} ${state}`;
        assert(boxes.actions[0].width > 160, `${label}: filename remains editable`);
        assert(boxes.body.height > height * .35, `${label}: usable template body (got ${boxes.body.height}px)`);
        assert(boxes.status.y >= boxes.head.bottom - 1 && boxes.body.y >= boxes.status.bottom - 1 && boxes.foot.y >= boxes.body.bottom - 1, `${label}: rows do not overlap`);
        assert(boxes.dialog.x >= 0 && boxes.dialog.y >= 0 && boxes.dialog.right <= width + 1 && boxes.dialog.bottom <= height + 1, `${label}: dialog fits viewport`);
        for (const b of [boxes.head, boxes.status, boxes.body, boxes.foot, ...boxes.actions]) {
          assert(b.x >= boxes.dialog.x - 1 && b.right <= boxes.dialog.right + 1 && b.y >= boxes.dialog.y - 1 && b.bottom <= boxes.dialog.bottom + 1, `${label}: controls fit dialog`);
        }
        for (let i = 0; i < boxes.actions.length; i++) for (let j = i + 1; j < boxes.actions.length; j++) {
          const a = boxes.actions[i], b = boxes.actions[j];
          assert(a.right <= b.x + 1 || b.right <= a.x + 1 || a.bottom <= b.y + 1 || b.bottom <= a.y + 1, `${label}: controls do not overlap`);
        }
        assert(boxes.cats.width > 70 && boxes.items.width > 140, `${label}: usable category and item columns`);
        if (boxes.preview.visible) {
          assert(boxes.code.height > 65, `${label}: code preview visible`);
          assert(boxes.code.y >= boxes.info.bottom - 1 && boxes.code.bottom <= boxes.body.bottom + 1, `${label}: preview contained`);
        }
        console.log('PASS ' + label);
        if (state === 'offline' || (width === 390 && state === 'legacy')) await page.screenshot({ path: `${OUT}/${width}-${state}.png` });
      }
    }
    await page.setViewportSize({width:1400,height:860});
    await page.evaluate(() => { dev.port = null; document.documentElement.classList.add('light'); App.galRender(); });
    await page.fill('#gSearch', 'zz-no-template-zz');
    await page.waitForSelector('#gItems .none');
    assert(await page.isDisabled('#gAdd'), 'empty search disables Add');
    await page.press('#gSearch', 'Escape');
    await page.waitForSelector('#gItems .item');
    await page.press('#gSearch', 'ArrowDown');
    assert(await page.inputValue('#gName'), 'keyboard selection updates name');
    await page.screenshot({path:OUT + '/1400-light.png'});
    await page.press('#gSearch', 'Escape');
    assert(!await page.isVisible('#galleryBg'), 'Escape closes gallery');
    assert.deepStrictEqual(errors, [], 'no browser exceptions');
    console.log('PASS empty search, keyboard selection, light theme and Escape');
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
