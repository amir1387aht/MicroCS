// Offline real Chromium regression; no serial, compiler, server or port needed.
// NODE_PATH=/vercel/sandbox/node_modules CHROME=$(command -v chromium) node tests/studio/test_source_eol_history_browser.js
// REPRO_BASELINE=1 injects the canonical preview editor to reproduce the gap.
const assert = require('assert');
const path = require('path');
const {execFileSync} = require('child_process');
let chromium;
try { ({chromium} = require('playwright')); }
catch { ({chromium} = require('playwright-core')); }
const raw = 'var text = @"first  \r\nفارسی 🚀  \rend";\nConsole.WriteLine(text);\r\n';
const lf = raw.replace(/\r\n|\r/g, '\n');
const cr = lf.replace(/\n/g, '\r');
let checks = 0, browser;
function equal(actual, expected, label) {
  assert.deepStrictEqual(actual, expected, label); console.log('PASS ' + label); checks++;
}
(async () => {
  browser = await chromium.launch({executablePath: process.env.CHROME, args: ['--no-sandbox']});
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.route('https://**/*', route => route.abort());
  if (process.env.REPRO_BASELINE) for (const name of ['source.js', 'studio.js']) {
    const body = execFileSync('git', ['show', 'd6cc1e8f2f6a992350494e4b00267a73919b545f:tools/studio/' + name], {encoding:'utf8'});
    await page.route('**/' + name, route => route.fulfill({body, contentType:'text/javascript'}));
  }
  await page.goto('file://' + path.resolve(__dirname, '../../tools/studio/index.html'));
  await page.waitForFunction(() => typeof App !== 'undefined' && typeof ed !== 'undefined');
  const open = async (text = raw, readOnly = false) => {
    await page.evaluate(({text, readOnly}) => {
      App.openTab({name:'eol-history.cs', text, saved:text, local:true, readOnly});
    }, {text, readOnly});
    await page.evaluate(() => new Promise(requestAnimationFrame));
    await page.focus('#ta');
  };
  const state = () => page.evaluate(() => ({raw:ed.tab.text, view:ed.ta.value, dirty:ed.tab.dirty, sel:[ed.ta.selectionStart,ed.ta.selectionEnd]}));
  const paste = async text => page.evaluate(text => {
    ed.ta.setSelectionRange(0, ed.ta.value.length);
    const data = new DataTransfer(); data.setData('text/plain', text);
    ed.ta.dispatchEvent(new ClipboardEvent('paste', {clipboardData:data, bubbles:true, cancelable:true}));
  }, text);
  const undo = () => page.press('#ta', 'Control+Alt+z');
  const redo = () => page.press('#ta', 'Control+Alt+Shift+z');
  if (!process.env.REPRO_BASELINE) {
    await open();
    await page.evaluate(() => ed.setError(1));
    await paste(raw);
    equal(await page.evaluate(() => [ed.errLine, ed.eolHistory.items.length, ed.tab.dirty]), [1, 0, false], 'identical raw no-op leaves diagnostics, history and clean state unchanged');
  }
  await open(); await paste(lf);
  equal((await state()).raw, lf, 'exact paste changes raw EOLs without display change');
  if (process.env.REPRO_BASELINE) {
    await page.press('#ta', 'Control+z');
    equal((await state()).raw === raw, false, 'baseline native undo fails to recover original raw source');
    console.log('REPRODUCED canonical preview same-display EOL undo gap');
    return;
  }
  equal((await state()).dirty, true, 'byte-only change marks source dirty');
  equal((await state()).view, lf, 'textarea remains normalized');
  await undo();
  equal((await state()).raw, raw, 'explicit keyboard undo restores mixed literal bytes');
  equal((await state()).sel, [0, lf.length], 'undo restores original selection');
  equal((await state()).dirty, false, 'undo returns saved source clean');
  await redo(); equal((await state()).raw, lf, 'keyboard redo restores exact LF raw');
  equal((await state()).sel, [lf.length, lf.length], 'redo restores paste caret');
  await paste(cr); await undo(); await undo();
  equal((await state()).raw, raw, 'repeated indistinguishable views undo in raw sequence');
  await redo(); await paste(lf); await redo();
  equal((await state()).raw, cr, 'no-op replacement retains redo');
  await undo(); await paste(raw); await redo();
  equal((await state()).raw, raw, 'new EOL edit after undo invalidates old redo');

  // Source/project diagnostics compare canonical raw, not normalized textarea.
  await open();
  await page.evaluate(() => {
    const t = ed.tab;
    App.setProjectMembers([t.id], t.id, 'EOL history');
    App.lastProjectRun = {path:'/.eol-history.cs', bundle:Lang.projectBundle([{id:t.id,name:t.name,text:t.text}], t.id)};
    ed.setError(1);
  });
  await paste(lf);
  equal(await page.evaluate(() => SourceProject.parse(SourceProject.stringify(App.project.name, App.projectDocuments(), App.project.entryId)).files[0].text), lf, 'project snapshot exports exact byte-only replacement');
  equal(await page.evaluate(() => ed.errLine), 0, 'EOL edit clears existing source diagnostic');
  await page.evaluate(() => App.showProjectLocation('/.eol-history.cs', 4));
  equal(await page.evaluate(() => ed.errLine), 0, 'project diagnostic refuses stale raw snapshot');
  equal(await page.evaluate(() => document.body.textContent.includes('old diagnostic position is stale')), true, 'stale project diagnostic explicitly reported');
  await undo();
  equal((await state()).raw, raw, 'project source/export input restored exactly');
  equal(await page.evaluate(() => SourceProject.parse(SourceProject.stringify(App.project.name, App.projectDocuments(), App.project.entryId)).files[0].text), raw, 'project snapshot exports exact restored mixed source');
  await page.evaluate(() => App.showProjectLocation('/.eol-history.cs', 4));
  equal(await page.evaluate(() => ed.errLine > 0), true, 'restored exact raw source can use its original project diagnostic');

  // Text-changing edits abandon raw-only history, leaving ordinary native undo.
  await open(); await paste(lf);
  await page.evaluate(() => ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length));
  await page.keyboard.press('!');
  equal(await page.evaluate(() => ed.eolHistory.items.length), 0, 'ordinary typing invalidates byte-only history');
  await page.press('#ta', 'Control+z');
  equal((await state()).raw, lf, 'native undo of typing retains current LF literal bytes');
  await page.press('#ta', 'Control+Shift+z');
  equal((await state()).raw, lf + '!', 'native redo remains browser owned');
  await undo(); equal((await state()).raw, lf + '!', 'explicit old EOL undo cannot cross a text edit');

  await open();
  await page.evaluate(() => ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length));
  await page.keyboard.press('!');
  await paste(lf + '!');
  await undo(); equal((await state()).raw, raw + '!', 'raw undo after text edit restores exact most recent source');
  await page.press('#ta', 'Control+z');
  equal((await state()).raw, raw, 'native undo of earlier text edit still restores original mixed literal');
  await page.press('#ta', 'Control+Shift+z');
  equal((await state()).raw, raw + '!', 'native redo of earlier text edit preserves mixed literal');

  await open(); await paste(lf);
  await page.press('#ta', 'Control+z');
  equal(await page.evaluate(() => ed.eolHistory.items.length), 0, 'native keyboard undo request clears raw-only history');
  // Native undo grouping is browser-owned, not one snapshot per character.
  await open(); await paste(lf);
  await page.evaluate(() => ed.ta.setSelectionRange(ed.ta.value.length, ed.ta.value.length));
  await page.keyboard.type('!?');
  await page.press('#ta', 'Control+z');
  equal([lf, lf + '!'].includes((await state()).raw), true, 'native coalescing never rewrites retained literal source');

  await open(); await paste(lf);
  await page.evaluate(() => {
    ed.ta.dispatchEvent(new InputEvent('beforeinput', {inputType:'historyUndo', bubbles:true, cancelable:true}));
  });
  equal(await page.evaluate(() => ed.eolHistory.items.length), 0, 'native beforeinput undo clears raw-only history even without text change');
  await undo(); equal((await state()).raw, lf, 'invalidated raw history cannot guess from view');

  await open(); await paste(lf);
  await page.evaluate(() => ed.tab.readOnly = ed.ta.readOnly = true);
  await undo(); equal((await state()).raw, lf, 'readOnly blocks raw undo');
  await page.evaluate(() => ed.tab.readOnly = ed.ta.readOnly = false);
  await page.evaluate(() => ed.ta.dispatchEvent(new KeyboardEvent('keydown', {key:'z',ctrlKey:true,altKey:true,isComposing:true,bubbles:true,cancelable:true})));
  equal((await state()).raw, lf, 'composition keyboard does not invoke history');
  await undo(); equal((await state()).raw, raw, 'composition guard does not consume undo');
  await paste(lf);
  await page.evaluate(() => ed.ta.dispatchEvent(new InputEvent('input', {inputType:'insertCompositionText',isComposing:true,bubbles:true})));
  equal(await page.evaluate(() => ed.eolHistory.items.length), 0, 'composition input invalidates separate raw history');

  await open(); await paste(lf);
  await page.evaluate(() => { const tab = ed.tab; App.openTab({name:'other.cs',text:'Console.WriteLine(1);',saved:null}); App.activate(tab.id); });
  await page.evaluate(() => new Promise(requestAnimationFrame));
  await undo(); equal((await state()).raw, lf, 'tab reopen resets bounded EOL-only history');

  await open();
  await page.evaluate(() => ed.eolHistory = new SourceText.EolHistory(128, 1));
  await paste(lf);
  equal((await state()).raw, lf, 'oversized edit still preserves pasted bytes');
  equal(await page.evaluate(() => document.body.textContent.includes('exceeds the bounded raw history')), true, 'oversized edit explicitly warns undo unavailable');
  await undo(); equal((await state()).raw, lf, 'oversized history never restores guessed bytes');
  equal(errors, [], 'no browser page errors');
  console.log(`PASS ${checks} real Chromium EOL-history assertions`);
})().catch(e => {console.error(e); process.exitCode = 1;}).finally(async () => {if(browser) await browser.close();});
