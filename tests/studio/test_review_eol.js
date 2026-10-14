// Independent reviewer: offline key interaction, explicit byte-only scope.
// --baseline preserves the original native-CtrlZ gap characterization.
const assert = require('assert'), path = require('path');
const {chromium} = require('playwright');
(async () => {
 const browser = await chromium.launch({executablePath:process.env.CHROME,args:['--no-sandbox']});
 try {
  const page = await browser.newPage();
  let errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route(/^https?:/,r=>r.abort());
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  const raw='a\r\nb\rc\n', plain='a\nb\nc\n';
  const open=async()=>{
   await page.evaluate(raw=>App.openTab({name:'review-eol.cs',text:raw,saved:raw,local:true}),raw);
   await page.evaluate(()=>new Promise(requestAnimationFrame));
  };
  const replace=async text=>page.evaluate(text=>{ed.ta.focus();ed.insert(text,0,ed.value.length,true)},text);
  const state=()=>page.evaluate(()=>({raw:App.tab().text,dirty:App.tab().dirty,sel:[ed.ta.selectionStart,ed.ta.selectionEnd],err:ed.errLine}));
  await open();
  await replace(plain);
  assert.equal((await state()).raw,plain,'exact EOL replacement');
  if (process.argv.includes('--baseline')) {
   await page.keyboard.press('Control+z');
   assert.notEqual((await state()).raw,raw,'baseline native raw-only gap reproduced');
   console.log('PASS baseline known native EOL-only undo gap independently reproduced');
  } else {
   assert.equal((await state()).dirty,true);
   await page.evaluate(()=>ed.setError(2));
   await page.keyboard.press('Control+Alt+z');
   assert.deepEqual(await state(),{raw,dirty:false,sel:[0,plain.length],err:0},'explicit raw undo updates exact bytes/selection/dirty/diagnostic');
   await page.keyboard.press('Control+Alt+Shift+z');
   assert.deepEqual(await state(),{raw:plain,dirty:true,sel:[plain.length,plain.length],err:0},'explicit redo');
   await page.keyboard.press('Control+Alt+z');
   await replace(raw); // true byte no-op must retain redo
   await page.keyboard.press('Control+Alt+Shift+z');
   assert.equal((await state()).raw,plain,'no-op preserves redo');
   await page.keyboard.press('Control+Alt+z');
   await replace('a\rb\rc\r');
   await page.keyboard.press('Control+Alt+Shift+z');
   assert.equal((await state()).raw,'a\rb\rc\r','new raw edit truncates redo');
   // Native shortcut explicitly departs from raw-only history even if empty.
   await open();await replace(plain);
   await page.keyboard.press('Control+z');
   assert.equal((await state()).raw,plain,'native CtrlZ is not byte-only undo');
   await page.keyboard.press('Control+Alt+z');
   assert.equal((await state()).raw,plain,'native shortcut invalidates explicit history');
   // Independent normal-text native undo must remain functional.
   await open();
   await page.evaluate(()=>{ed.ta.focus();ed.insert('X',0,0,true)});
   assert.equal((await state()).raw,'X'+raw);
   await page.keyboard.press('Control+z');
   assert.equal((await state()).raw,raw,'normal native text undo retains mixed bytes');
   // A real text edit invalidates raw-only history (no hidden replay later).
   await open();await replace(plain);
   await page.evaluate(()=>ed.ta.setSelectionRange(ed.value.length,ed.value.length));
   await page.keyboard.type('q');
   await page.keyboard.press('Control+Alt+z');
   assert.equal((await state()).raw,plain+'q','text input invalidates explicit history');
   // Reopening the same tab clears per-open raw history.
   await open();await replace(plain);
   await page.evaluate(()=>ed.open(App.tab()));
   await page.evaluate(()=>new Promise(requestAnimationFrame));
   await page.keyboard.press('Control+Alt+z');
   assert.equal((await state()).raw,plain,'tab reopening clears history');
   // Small budgets exercise actual Editor warning/fallback integration.
   await open();
   await page.evaluate(()=>{ed.eolHistory=new SourceText.EolHistory(128,1)});
   await replace(plain);
   await page.keyboard.press('Control+Alt+z');
   assert.equal((await state()).raw,plain,'over-budget raw edit retained without false undo');
   assert(await page.evaluate(()=>ed.eolHistory.items.length===0&&ed.eolHistory.units===0));
   assert(await page.evaluate(()=>document.body.innerText.includes('exceeds the bounded raw history')),'oversized edit warns');
   console.log('PASS independent explicit EOL key/byte/selection/dirty/diagnostic/no-op/redo/native/text/tab/budget regressions');
  }
  assert.deepEqual(errors,[]);
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
