// Offline browser regression: hiding is UI-only, never a device stop/reset.
let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const path=require('path'), assert=require('assert');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME||'/usr/bin/chromium',args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1400,height:900}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(()=>{App.newScratch({text:'int value = 1;'});term.line('retained output'); window.__commands=[]; dev.cmd=(c)=>{window.__commands.push(c);throw Error('Unexpected device command');};});
  const height=()=>page.locator('.editor-pane').evaluate(e=>e.getBoundingClientRect().height);
  const initial=await height();
  const split=await page.locator('#splitCons').boundingBox();
  await page.mouse.move(split.x+50,split.y+2);await page.mouse.down();await page.mouse.move(split.x+50,split.y-65);await page.mouse.up();
  const saved=await page.locator('#center').evaluate(e=>e.style.getPropertyValue('--cons'));
  await page.click('#btnCloseTerminal');
  assert(await page.locator('#consolePane').isHidden());assert(await page.locator('#splitCons').isHidden());
  assert((await height())>initial+200);assert.strictEqual(await page.getAttribute('#btnTerminal','aria-expanded'),'false');
  await page.click('#btnTerminal');assert(await page.locator('#consolePane').isVisible());
  assert.strictEqual(await page.locator('#center').evaluate(e=>e.style.getPropertyValue('--cons')),saved);
  assert((await page.locator('#term').innerText()).includes('retained output'));
  await page.locator('#ta').focus();const text=await page.locator('#ta').inputValue();
  await page.keyboard.press('Alt+Tab');assert(await page.locator('#consolePane').isHidden());
  assert.strictEqual(await page.locator('#ta').inputValue(),text,'Alt+Tab must not indent');
  // Holding the shortcut may not repeatedly flicker the panel.
  await page.evaluate(()=>dispatchEvent(new KeyboardEvent('keydown',{key:'Tab',altKey:true,repeat:true,bubbles:true,cancelable:true})));
  assert(await page.locator('#consolePane').isHidden());
  await page.keyboard.press('Control+`');assert(await page.locator('#consolePane').isVisible());
  assert.strictEqual(await page.evaluate(()=>document.activeElement.id),'consolePane','offline prompt is disabled: focus panel');
  await page.keyboard.press('Control+`');assert(await page.locator('#consolePane').isHidden(),'Ctrl+` must also hide');
  await page.locator('#ta').focus();await page.keyboard.press('Control+`');
  assert(await page.locator('#consolePane').isVisible(),'Ctrl+` opens from editor');
  await page.evaluate(()=>{$('cin').disabled=false;$('cin').focus();});
  await page.keyboard.press('Control+`');assert(await page.locator('#consolePane').isHidden(),'Ctrl+` closes from REPL');
  assert.strictEqual(await page.evaluate(()=>document.activeElement.id),'ta');
  await page.evaluate(()=>dispatchEvent(new KeyboardEvent('keydown',{key:'`',code:'Backquote',ctrlKey:true,repeat:true,bubbles:true,cancelable:true})));
  assert(await page.locator('#consolePane').isHidden(),'repeated Ctrl+` must not toggle');
  await page.keyboard.press('Control+`');assert(await page.locator('#consolePane').isVisible());
  assert.strictEqual(await page.evaluate(()=>document.activeElement.id),'cin');
  await page.keyboard.press('Alt+Tab');assert(await page.locator('#consolePane').isHidden());
  assert.strictEqual(await page.evaluate(()=>document.activeElement.id),'ta');
  await page.keyboard.press('Control+j');assert(await page.locator('#consolePane').isVisible());
  assert.strictEqual(await page.evaluate(()=>document.activeElement.id),'cin');
  await page.evaluate(()=>{App.setView('plot');App.setTerminalVisible(false);});
  assert.strictEqual(await page.evaluate(()=>plot.visible),false);
  await page.click('#btnTerminal');assert.strictEqual(await page.evaluate(()=>plot.visible),true);
  await page.click('#btnCloseTerminal');
  assert.deepStrictEqual(await page.evaluate(()=>window.__commands),[],'panel controls must not send device commands');
  await page.locator('#ta').focus();await page.keyboard.press('Tab');
  assert.notStrictEqual(await page.locator('#ta').inputValue(),text,'normal Tab still indents');
  // Avoid the unsaved-draft beforeunload prompt in a headless reload.
  await page.evaluate(()=>App.tabs.forEach(t=>t.dirty=false));await page.reload();
  assert(await page.locator('#consolePane').isHidden());
  await page.click('#btnMore');await page.getByText('Show terminal  Ctrl+` / Ctrl+J',{exact:true}).click();
  assert(await page.locator('#consolePane').isVisible());assert.strictEqual(await page.evaluate(()=>App.view),'plot');
  assert.strictEqual(await page.locator('#center').evaluate(e=>e.style.getPropertyValue('--cons')),saved);
  await page.evaluate(()=>App.setView('text'));
  await page.setViewportSize({width:800,height:600});await page.click('#btnCloseTerminal');await page.click('#btnTerminal');
  assert(await page.locator('#consolePane').isVisible());
  assert.deepStrictEqual(errors,[]);
  console.log('PASS terminal: close/reopen, resize/output retention, Alt+Tab capture, Ctrl+J, Ctrl+`, focus, plotter, persistence, menu, compact viewport');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
