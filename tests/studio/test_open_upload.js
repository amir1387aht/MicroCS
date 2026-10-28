// One in-flight open per normalized device path; no redundant File-mode writes.
let chromium;try{({chromium}=require('playwright'));}catch{({chromium}=require('playwright-core'));}
const assert=require('assert'),path=require('path');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(()=>{
   dev.port={};dev.epoch=1;dev.mode='machine';dev.op=async fn=>fn();
   window.__fetch=App.fetchFile;window.__calls=0;
   App.fetchFile=()=>{__calls++;return new Promise(resolve=>window.__finish=resolve);};
   App.treeInit();App.treeDirs.set('/',[{name:'app.cs',size:8}]);App.cwd='/';App.entries=App.treeDirs.get('/');App.renderFiles();
  });
  await page.dblclick('#fileList .file[data-path="/app.cs"] .name');
  await page.evaluate(()=>{window.__opens=[...App.openingFiles.values(),App.openPath('/lib/../app.cs'),App.openPath('\\app.cs')];});
  assert.strictEqual(await page.evaluate(()=>__calls),1,'click burst must issue one get');
  await page.evaluate(async()=>{__finish(enc.encode('original'));await Promise.all(__opens);});
  assert.strictEqual(await page.locator('#tablist .tab').count(),1);
  assert.strictEqual(await page.evaluate(()=>App.openingFiles.size),0);
  await page.evaluate(()=>{
   const t=App.tab();ed.ta.value='my draft';ed.refresh();ed.onChange();
   window.__first=t.id;const again=App.openTab({path:'/folder/../app.cs',text:'discard me',saved:'discard me'});
   if(again.id!==t.id)throw Error('duplicate direct open');
  });
  assert.strictEqual(await page.locator('#ta').inputValue(),'my draft','existing edits win');
  await page.evaluate(()=>App.openPath('/app.cs'));
  assert.strictEqual(await page.evaluate(()=>__calls),1,'dirty existing tab must not fetch');
  // Failed downloads must clear the lock and permit retry.
  await page.evaluate(async()=>{
   App.fetchFile=async()=>{__calls++;throw Error('read failed');};await Promise.all([App.openPath('/retry.cs'),App.openPath('/retry.cs')]);
   App.fetchFile=async()=>{__calls++;return enc.encode('retry');};await App.openPath('/retry.cs');
  });
  assert.strictEqual(await page.evaluate(()=>App.tabs.filter(t=>t.path==='/retry.cs').length),1);
  // A new connection gets a fresh request; its response cannot be replaced by the old one.
  await page.evaluate(()=>{
   window.__finishers=[];App.fetchFile=()=>new Promise(r=>__finishers.push(r));
   window.__old=App.openPath('/reconnect.cs');dev.epoch++;
   window.__new=App.openPath('/reconnect.cs');
  });
  await page.evaluate(async()=>{__finishers[1](enc.encode('new device'));await __new;__finishers[0](enc.encode('old device'));await __old;});
  assert.strictEqual(await page.evaluate(()=>App.tabs.find(t=>t.path==='/reconnect.cs').text),'new device');
  // If another route opens a draft while a download is pending, preserve it.
  await page.evaluate(()=>{
   App.fetchFile=()=>new Promise(r=>window.__finish=r);window.__pending=App.openPath('/race.cs');
   App.openTab({path:'/race.cs',text:'draft won',saved:null});
  });
  await page.evaluate(async()=>{__finish(enc.encode('remote'));await __pending;});
  assert.deepStrictEqual(await page.evaluate(()=>App.tabs.filter(t=>t.path==='/race.cs').map(t=>t.text)),['draft won']);
  // Large-file confirmations must also be coalesced, and cancellation unlocks retries.
  await page.evaluate(()=>{window.__large=[App.openPath('/large.cs',600000),App.openPath('/large.cs',600000)];});
  await page.waitForSelector('#modalBg.show');await page.click('#dlgNo');
  await page.evaluate(()=>Promise.all(__large));
  assert.strictEqual(await page.evaluate(()=>App.openingFiles.size),0);
  await page.evaluate(async()=>{dev.get=async()=>new Uint8Array([0,1,2]);App.fetchFile=async()=>new Uint8Array([0,1,2]);await App.openPath('/data.bin');await App.openPath('/data.bin');});
  assert.strictEqual(await page.evaluate(()=>App.tabs.filter(t=>t.path==='/data.bin').length),1);
  // Old duplicate drafts must not be lost or target the same upload path twice.
  await page.evaluate(()=>{
   App.tabs=[];ed.tab=null;store.set('drafts',[
    {path:'/app.cs',text:'first draft'}, {path:'/lib/../app.cs',text:'second draft'}, {path:'/app.cs',text:'first draft'}
   ]);App.restoreDrafts();
  });
  assert.deepStrictEqual(await page.evaluate(()=>App.tabs.map(t=>[t.path,t.text])),[['/app.cs','first draft'],[null,'second draft']]);
  // Exercise actual File-mode saveBuild/saveOne/putReliable with fake transport only.
  await page.evaluate(()=>{
   App.tabs=[];ed.tab=null;App.projectMode=false;store.set('saveAllCs',true);store.set('formatOnSave',false);
   window.__files={'/app.cs':'int value = 1;','/lib/helper.cs':'int helper = 2;'};
   window.__gets=[];window.__puts=[];window.__checks=[];
   dev.get=async p=>{__gets.push(p);if(!(p in __files))throw Error('not found');return enc.encode(__files[p]);};
   dev.put=async(p,data)=>{__puts.push(p);__files[p]=new TextDecoder().decode(data);};
   App.refresh=async()=>{};App.refreshMemory=async()=>{};App.checkScripts=async paths=>{__checks.push(paths);};
   App.openTab({path:'/lib/helper.cs',text:__files['/lib/helper.cs'],saved:__files['/lib/helper.cs']});
   App.openTab({path:'/app.cs',text:__files['/app.cs'],saved:__files['/app.cs']});
  });
  assert.strictEqual(await page.evaluate(()=>App.save()),true);
  assert.deepStrictEqual(await page.evaluate(()=>__puts),[],'identical File-mode files must not upload');
  assert.deepStrictEqual(await page.evaluate(()=>__checks[0].sort()),['/app.cs','/lib/helper.cs']);
  await page.evaluate(()=>{__gets=[];__puts=[];ed.ta.value='int value = 3;';ed.refresh();ed.onChange();});
  assert.strictEqual(await page.evaluate(()=>App.save()),true);
  assert.deepStrictEqual(await page.evaluate(()=>__puts),['/app.cs'],'only changed file uploads');
  assert.deepStrictEqual(await page.evaluate(()=>__gets),['/lib/helper.cs'],'dirty File-mode path avoids pre-upload read');
  await page.evaluate(()=>{__gets=[];__puts=[];__files['/app.cs']='int value = 9;';delete __files['/lib/helper.cs'];});
  assert.strictEqual(await page.evaluate(()=>App.save()),true);
  assert.deepStrictEqual(await page.evaluate(()=>__puts.sort()),['/app.cs','/lib/helper.cs'],'external edits and deletions still repaired on Save');
  assert.strictEqual(await page.evaluate(()=>__files['/app.cs']),'int value = 3;');
  // Ctrl/Cmd+S must upload, including physical S on non-Latin layouts.
  const shortcuts = [
   {key:'s',code:'KeyS',ctrlKey:true},
   {key:'س',code:'KeyS',ctrlKey:true},
   {key:'ы',code:'KeyS',ctrlKey:true},
   {key:'s',code:'KeyS',metaKey:true},
   {key:'S',code:'KeyS',ctrlKey:true,shiftKey:true},
  ];
  for (const [i,shortcut] of shortcuts.entries()) {
   await page.evaluate(i=>{__puts=[];ed.ta.value='int value = '+(20+i)+';';ed.refresh();ed.onChange();ed.ta.focus();},i);
   const prevented=await page.evaluate(shortcut=>{const e=new KeyboardEvent('keydown',{...shortcut,bubbles:true,cancelable:true});ed.ta.dispatchEvent(e);return e.defaultPrevented;},shortcut);
   assert(prevented,'save shortcut must suppress browser Save Page: '+JSON.stringify(shortcut));
   await page.waitForFunction(()=>!App.workflowBusy);
   assert.deepStrictEqual(await page.evaluate(()=>__puts),['/app.cs'],'shortcut uploads exactly once');
   assert.strictEqual(await page.evaluate(()=>__files['/app.cs']),'int value = '+(20+i)+';');
  }
  // Capture must win even when an editor child stops bubbling.
  await page.evaluate(()=>{ed.ta.addEventListener('keydown',e=>e.stopPropagation());__puts=[];ed.ta.value='int value = 30;';ed.refresh();ed.onChange();ed.ta.focus();});
  await page.keyboard.press('Control+s');await page.waitForFunction(()=>!App.workflowBusy);
  assert.deepStrictEqual(await page.evaluate(()=>__puts),['/app.cs']);
  // Dialogs reserve the browser shortcut but never save the file behind them.
  for(const overlay of ['commandBg','modalBg','galleryBg']) {
   const result=await page.evaluate(id=>{__puts=[];const bg=$(id);if(id==='commandBg')bg.hidden=false;else bg.classList.add('show');const e=new KeyboardEvent('keydown',{key:'s',code:'KeyS',ctrlKey:true,bubbles:true,cancelable:true});bg.dispatchEvent(e);if(id==='commandBg')bg.hidden=true;else bg.classList.remove('show');return e.defaultPrevented;},overlay);
   assert(result,'dialog must suppress browser Save Page');assert.deepStrictEqual(await page.evaluate(()=>__puts),[],'no background save');
  }
  const repeated=await page.evaluate(()=>{const e=new KeyboardEvent('keydown',{key:'s',code:'KeyS',ctrlKey:true,repeat:true,bubbles:true,cancelable:true});ed.ta.dispatchEvent(e);return e.defaultPrevented;});
  assert(repeated);assert.deepStrictEqual(await page.evaluate(()=>__puts),[],'held shortcut does not enqueue saves');
  await page.evaluate(()=>{__puts=[];dev.get=async()=>{throw Error('connection stalled');};});
  assert.strictEqual(await page.evaluate(()=>App.save()),false,'transport errors cannot silently mark verified');
  assert.deepStrictEqual(await page.evaluate(()=>__puts),[]);
  // Offline local tabs must not silently turn Ctrl+S into a computer download.
  let downloads=0;page.on('download',()=>downloads++);
  await page.evaluate(()=>{dev.port=null;App.tab().local=true;ed.ta.value='unsaved offline draft';ed.refresh();ed.onChange();ed.ta.focus();});
  await page.keyboard.press('Control+s');await page.waitForTimeout(100);
  assert.strictEqual(downloads,0,'offline save shortcut never downloads');
  assert.strictEqual(await page.evaluate(()=>App.tab().dirty),true,'offline draft stays dirty');
  assert.deepStrictEqual(errors,[]);
  console.log('PASS opens/uploads: click burst, normalized paths, dirty drafts, failed retry, reconnect, conflicting restored drafts, binary/large-file opens; unchanged File-mode uploads skipped, edits/deletions verified, read failures blocked; Ctrl/Cmd+S device uploads across keyboard layouts, capture, overlays, repeats and offline drafts');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
