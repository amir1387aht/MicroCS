// Device-side writes must update clean tabs without overwriting local edits.
let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const path=require('path'),assert=require('assert');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});
 try {
  const page=await browser.newPage(),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(()=>{
   dev.port={};dev.epoch=1;dev.mode='machine';dev.op=fn=>Promise.resolve().then(fn);
   window.__files={'/settings.json':'{"value":1}','/lib/data.txt':'old','/app.cs':'old'};
   window.__gets=[];dev.get=async p=>{__gets.push(p);if(!(p in __files))throw Error('not found');return enc.encode(__files[p]);};
   dev.ls=async()=>[];App.refreshStorage=()=>{};App.refreshMemory=async()=>{};
   window.__settings=App.openTab({path:'/settings.json',text:__files['/settings.json'],saved:__files['/settings.json']});
   window.__nested=App.openTab({path:'/lib/data.txt',text:'old',saved:'old'});
   window.__local=App.openTab({path:'/app.cs',text:'local',saved:'local',local:true});
   App.activate(__settings.id);App.cwd='/lib';
   __files['/settings.json']='{"value":2}';__files['/lib/data.txt']='new';
  });
  await page.evaluate(async()=>{await App.refresh(true);});
  assert.deepStrictEqual(await page.evaluate(()=>[__settings.text,__nested.text,__local.text]),['{"value":2}','new','local']);
  assert.strictEqual(await page.locator('#ta').inputValue(),'{"value":2}');
  // Background update preserves focus, caret and the undo history on no-change reads.
  await page.evaluate(()=>{$('cin').disabled=false;ed.ta.setSelectionRange(2,5);$('cin').focus();__files['/settings.json']='{"value":3}';});
  await page.evaluate(()=>App.pollOpenFile());
  assert.strictEqual(await page.locator('#ta').inputValue(),'{"value":3}');
  assert.deepStrictEqual(await page.evaluate(()=>[document.activeElement.id,ed.ta.selectionStart,ed.ta.selectionEnd]),['cin',2,5]);
  // Defaults generate NO recurring filesystem/serial reads, even after a write.
  await page.evaluate(()=>{__gets=[];__files['/settings.json']='{"value":4}';});
  await page.waitForTimeout(3000);
  assert.deepStrictEqual(await page.evaluate(()=>__gets),[],'default must not poll the MCU');
  assert.strictEqual(await page.locator('#ta').inputValue(),'{"value":3}');
  await page.click('#btnMore');
  await page.getByText('Live device file refresh (uses serial bandwidth)',{exact:true}).click();
  // Opt-in polling uses the actual timer, not just a direct call.

  await page.waitForFunction(()=>__settings.text==='{"value":4}');
  // Turning it off stops future recurring reads immediately.
  await page.click('#btnMore');
  await page.getByText('✓ Live device file refresh (uses serial bandwidth)',{exact:true}).click();
  await page.evaluate(()=>{__gets=[];});await page.waitForTimeout(3000);
  assert.deepStrictEqual(await page.evaluate(()=>__gets),[],'disabled polling must stay silent');
  // All unsafe states must make polling a no-op.
  for(const state of ['run','pending','workflow','handoff','repl','unknown','dirty']){
   const got=await page.evaluate(async state=>{
    __gets=[];dev.running=state==='run';dev.pending=state==='pending'?1:0;
    App.workflowBusy=state==='workflow';App.workflowHandoff=state==='handoff';
    dev.mode=['repl','unknown'].includes(state)?state:'machine';__settings.dirty=state==='dirty';
    await App.pollOpenFile();return __gets.length;
   },state);assert.strictEqual(got,0,state+' must not poll');
  }
  await page.evaluate(()=>{dev.running=false;dev.pending=0;dev.mode='machine';App.workflowBusy=false;App.workflowHandoff=false;__settings.dirty=false;});
  // Editing during a slow transfer must win, including an empty draft.
  await page.evaluate(async()=>{
   const get=dev.get;let done;dev.get=()=>new Promise(r=>done=r);
   const task=App.reloadTab(__settings);await Promise.resolve();await Promise.resolve();
   ed.ta.value='';ed.ta.dispatchEvent(new Event('input',{bubbles:true}));
   done(enc.encode('remote'));await task;dev.get=get;
  });
  assert.strictEqual(await page.locator('#ta').inputValue(),'');assert(await page.evaluate(()=>__settings.dirty));
  // A renamed tab, reconnect, closed tab or save during get invalidates the response.
  for(const change of ['rename','reconnect','close','save']){
   await page.evaluate(async change=>{
    App.tabs=[];ed.tab=null;window.__race=App.openTab({path:'/race.txt',text:'before',saved:'before'});
    const get=dev.get;let done;dev.get=()=>new Promise(r=>done=r);
    const task=App.reloadTab(__race);await Promise.resolve();await Promise.resolve();
    if(change==='rename')__race.path='/renamed.txt';
    if(change==='reconnect')dev.epoch++;
    if(change==='close')App.tabs=[];
    if(change==='save')__race.saved=__race.text='saved';
    done(enc.encode('STALE'));await task;dev.get=get;
   },change);
   assert.notStrictEqual(await page.evaluate(()=>__race.text),'STALE',change);
  }
  // Serial queue race: a new run/REPL starts before an idle read owns the line.
  await page.evaluate(async()=>{
   App.tabs=[];ed.tab=null;App.openTab({path:'/app.cs',text:'old',saved:'old'});__gets=[];
   const op=dev.op;let queued;dev.op=fn=>new Promise(resolve=>queued=()=>Promise.resolve(fn()).then(resolve));
   const task=App.pollOpenFile();dev.mode='repl';await queued();await task;dev.op=op;
  });
  assert.deepStrictEqual(await page.evaluate(()=>__gets),[]);
  // Failure retains the visible buffer; switching tabs notices same-size changes.
  await page.evaluate(async()=>{dev.mode='machine';delete __files['/app.cs'];await App.reloadTab(App.tab());});
  assert.strictEqual(await page.locator('#ta').inputValue(),'old');
  await page.evaluate(()=>{__files['/app.cs']='new';App.openTab({path:'/other.txt',text:'other',saved:'other'});});
  await page.locator('#tablist .tab').first().click();await page.waitForFunction(()=>$('ta').value==='new');
  // Run completion always refreshes clean open files, including failure/abort.
  for(const status of ['OK','ERR aborted','ERR test failure']){
   await page.evaluate(async status=>{
    __files['/app.cs']='old';await App.reloadTab(App.tabs[0]);
    dev.cmd=async c=>{if(c.startsWith('run '))__files['/app.cs']='run';return {status:c.startsWith('run ')?status:'OK',out:enc.encode('')};};
    await App.runPath('/script.cs');
   },status);
   await page.waitForFunction(()=>$('ta').value==='run');
  }
  // A delayed post-save refresh belongs to the same workflow, not the next
  // Save's editor/device snapshot. No extra transfers are needed to enforce it.
  await page.evaluate(()=>{
   App.tabs=[];ed.tab=null;App.openTab({path:'/app.cs',text:'int value = 1;',saved:'int value = 1;'});
   App.saveOne=async t=>{t.saved=t.text;return true;};App.checkScripts=async()=>{};
   App.refresh=()=>new Promise(resolve=>{window.__finishRefresh=resolve;});
   window.__saveFinished=false;window.__saveTask=App.save().then(result=>{__saveFinished=true;return result;});
  });
  await page.waitForFunction(()=>!!window.__finishRefresh);
  assert.strictEqual(await page.evaluate(()=>App.workflowBusy),true,'save stays busy through refresh');
  assert.strictEqual(await page.evaluate(()=>__saveFinished),false,'save cannot return before refresh');
  assert.strictEqual(await page.evaluate(()=>App.save()),false,'overlapping save must stay blocked');
  await page.evaluate(()=>__finishRefresh());
  assert.strictEqual(await page.evaluate(()=>__saveTask),true);
  assert.deepStrictEqual(errors,[]);
  console.log('PASS file refresh: same-size/nested changes, zero default polling, opt-in/off timer, tab selection, run/error/stop, drafts, focus/caret, serial/reconnect/save/rename/close races and missing files, delayed post-save refresh');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
