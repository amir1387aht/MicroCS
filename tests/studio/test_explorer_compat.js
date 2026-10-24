// Browser regressions for lazy tree navigation, legacy capability UI and safe checks.
let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const assert=require('assert'),path=require('path'),fs=require('fs');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME || undefined,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1400,height:900}}), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(async()=>{
   dev.port={}; dev.epoch=1; dev.op=fn=>fn();
   window.__listCalls=[];
   window.__fs={
    '/':[{name:'apps',dir:true},{name:'lib',dir:true},{name:'root.cs',size:3},{name:'.studio-check.cs',size:4},{name:'..',dir:true},{name:'bad/name',dir:true}],
    '/apps':[{name:'nested',dir:true},{name:'app.cs',size:6}],
    '/apps/nested':[{name:'leaf.cs',size:2}], '/lib':[{name:'display.cs',size:8}]
   };
   dev.ls=async p=>{__listCalls.push(p);if(!__fs[p])throw Error('not found');return __fs[p];};
   App.refreshStorage=()=>{}; App.syncOpenTabs=()=>{};
   await App.refresh();
  });
  const at=p=>page.locator('#fileList .file[data-path="'+p+'"]');
  assert.deepStrictEqual(await page.evaluate(()=>__listCalls),['/'],'collapsed folders are not listed');
  assert.strictEqual(await page.locator('#fileList .file').count(),3,'hidden and unsafe names ignored');
  await at('/apps').click();await at('/apps/app.cs').waitFor();
  assert(await at('/root.cs').isVisible(),'root siblings remain visible');
  await at('/apps/nested').click();await at('/apps/nested/leaf.cs').waitFor();
  assert.strictEqual(await at('/apps/nested/leaf.cs').getAttribute('aria-level'),'3');
  await at('/lib').click();await at('/lib/display.cs').waitFor();
  assert(await at('/apps/nested/leaf.cs').isVisible(),'multiple branches stay expanded');
  await page.evaluate(()=>App.openTab({path:'/apps/app.cs',text:'class Display {\n  public int Count = 0;\n  public void Draw(int Value) {\n    Count = this.Count + Value;\n    Console.WriteLine($"{Count}");\n  }\n}\nvar display = new Display();',saved:null}));
  fs.mkdirSync('build/studio-review',{recursive:true});
  await page.screenshot({path:'build/studio-review/tree-colors.png'});

  await at('/apps').focus();await page.keyboard.press('ArrowLeft');
  assert.strictEqual(await at('/apps').getAttribute('aria-expanded'),'false');
  assert.strictEqual(await at('/apps/nested/leaf.cs').count(),0,'collapse removes descendants only');
  assert(await at('/lib/display.cs').isVisible());
  await page.keyboard.press('ArrowRight');await at('/apps/nested/leaf.cs').waitFor();
  await at('/apps').focus();await page.keyboard.press('ArrowRight');
  assert.strictEqual(await page.evaluate(()=>document.activeElement.dataset.path),'/apps/nested','Right moves to first child');
  await page.keyboard.press('ArrowLeft'); // collapse nested
  await page.keyboard.press('ArrowLeft'); // parent
  assert.strictEqual(await page.evaluate(()=>document.activeElement.dataset.path),'/apps');
  await page.evaluate(async()=>{__fs['/']=__fs['/'].filter(x=>x.name!=='lib');App.cwd='/lib';await App.refresh();});
  assert.strictEqual(await at('/lib').count(),0,'removed directory disappears');
  assert.strictEqual(await page.evaluate(()=>App.cwd),'/','stale upload target reset');
  // A failed folder is visible, does not erase siblings and can be refreshed.
  await page.evaluate(async()=>{__fs['/'].push({name:'broken',dir:true});await App.refresh();});
  await at('/broken').click();await page.waitForSelector('.tree-error');
  assert(await at('/root.cs').isVisible());
  await page.evaluate(async()=>{__fs['/broken']=[{name:'fixed.cs',size:2}];await App.refresh();});
  await at('/broken/fixed.cs').waitFor();assert.strictEqual(await page.locator('.tree-error').count(),0);
  // Folder rename must update descendants, expansion state and upload target.
  await page.evaluate(()=>{
   dev.check=async command=>{
    if(command!=='mv /apps /renamed')throw Error('unexpected mutation: '+command);
    __fs['/'].find(it=>it.name==='apps').name='renamed';
    for(const key of Object.keys(__fs))if(key==='/apps'||key.startsWith('/apps/')){__fs['/renamed'+key.slice(5)]=__fs[key];delete __fs[key];}
    return {status:'OK'};
   };
   App.cwd='/apps/nested';App.treeExpanded.add('/apps/nested');window.__rename=App.rename('/apps');
  });
  await page.waitForSelector('#dlgIn',{state:'visible'});await page.fill('#dlgIn','/renamed');await page.click('#dlgOk');
  await page.evaluate(()=>window.__rename);await at('/renamed/app.cs').waitFor();
  assert.strictEqual(await page.evaluate(()=>App.tabs.find(t=>t.path==='/renamed/app.cs')?.path),'/renamed/app.cs');
  assert.strictEqual(await page.evaluate(()=>App.cwd),'/renamed/nested');
  assert.strictEqual(await at('/apps').count(),0);
  // A delayed result must not repopulate a disconnected/replaced device.
  await page.evaluate(()=>{
   const ls=dev.ls; window.__restoreLs=ls;
   dev.ls=p=>new Promise(resolve=>window.__release=()=>resolve([{name:'stale.cs',size:1}]));
   window.__pendingRefresh=App.refresh();
  });
  await page.waitForFunction(()=>!!window.__release);
  await page.evaluate(async()=>{dev.port=null;App.enable(false);__release();await __pendingRefresh;dev.ls=__restoreLs;});
  assert.strictEqual(await at('/stale.cs').count(),0);
  console.log('PASS explorer: nested files, independent branches, keyboard, lazy loads, deletion, retry, stale-result protection');
  await page.evaluate(()=>{
   dev.port={};App.info='MicroCS 1.14.0';App.capabilities=legacyCapabilities('MicroCS 1.14.0 features=0xffff');App.capabilitiesKnown=false;App.gallery();
  });
  const gallery = await page.evaluate(() => ({
   shown: App.gal.list.map(t => t.src), bundled: TEMPLATES.map(t => t.src)
  }));
  assert(gallery.bundled.length > 0, 'template bundle must not be empty');
  assert.deepStrictEqual(gallery.shown, gallery.bundled, 'legacy firmware must keep every bundled template');
  assert.strictEqual(await page.locator('#gItems .item').count(), gallery.bundled.length, 'every bundled template is rendered');
  assert((await page.innerText('#gSupport')).includes('unknown'));
  await page.evaluate(()=>{App.gal.sel=App.gal.list.findIndex(t=>t.file==='encoder.cs');App.galPreview();});
  assert((await page.innerText('#gInfo')).includes('firmware >= 1.17.0'));
  assert((await page.innerText('#gInfo')).includes('Not supported'));
  await page.screenshot({path:'build/studio-review/legacy-gallery.png'});
  assert(!await page.isDisabled('#gAdd'),'unsupported templates can be opened for adaptation');
  await page.click('#gAdd');assert((await page.inputValue('#ta')).includes('RotaryEncoder'));
  // Detect a missing check command without ever executing a source as a probe.
  const legacy=await page.evaluate(async()=>{
   const calls=[];dev.cmd=async c=>{calls.push(c);return {status:'ERR unknown command',out:new Uint8Array()};};
   App.compileCheck=null;
   await App.checkScripts(['/one.cs','/two.cs']);await App.checkScripts(['/three.cs']);
   let projectError='';try{await App.checkScripts(['/one.cs'],true);}catch(e){projectError=e.message;}
   App.compileCheck=null;dev.cmd=async c=>({status:'ERR compilation failed',out:new TextEncoder().encode('bad syntax')});
   let buildError='';try{await App.checkScripts(['/one.cs']);}catch(e){buildError=e.message;}
   return {calls,projectError,buildError};
  });
  assert.deepStrictEqual(legacy.calls,['check /one.cs']);
  assert(legacy.projectError.includes('Project mode requires'));
  assert(legacy.buildError.includes('Build failed'),'real compilation errors must not downgrade');
  assert.deepStrictEqual(errors,[]);
  console.log('PASS legacy gallery/version badges and safe upload-only fallback; project/real build errors remain blocked');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
