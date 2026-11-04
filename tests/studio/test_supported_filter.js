let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const assert=require('assert'),path=require('path');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1400,height:900}}), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(()=>{dev.port={};App.capabilitiesKnown=true;App.capabilities=new Map([['Compiler',true],['Wifi',false],['Ble',false],['Filesystem',true]]);App.info='MicroCS 1.17.0';App.gallery();});
  assert(await page.isChecked('#gSupportedOnly'));
  assert(await page.evaluate(()=>App.gal.list.length>0 && App.gal.list.every(t=>templateSupport(t,App.capabilities,true,App.info).state==='supported')));
  assert.strictEqual(await page.locator('#gCats [data-c="Wi-Fi and networking"]').count(),0);
  await page.uncheck('#gSupportedOnly');
  assert(await page.evaluate(()=>App.gal.list.length===TEMPLATES.length));
  await page.evaluate(()=>App.galClose());await page.click('#btnTemplates');
  assert(!await page.isChecked('#gSupportedOnly'),'remember toggle on reopen');
  await page.check('#gSupportedOnly');
  await page.fill('#gSearch','Wifi');await page.waitForTimeout(90);
  assert.strictEqual(await page.locator('#gItems .item').count(),0);
  assert(await page.isDisabled('#gAdd'));
  await page.uncheck('#gSupportedOnly');
  assert(await page.locator('#gItems .item').count()>0);
  await page.reload();await page.click('#btnTemplates');
  assert(!await page.isChecked('#gSupportedOnly'),'persist across page reload');
  await page.check('#gSupportedOnly');
  assert(await page.evaluate(()=>!dev.connected && App.gal.list.length===TEMPLATES.length),'offline browsing remains available');
  await page.evaluate(()=>{dev.port={};App.capabilitiesKnown=false;App.capabilities=legacyCapabilities('features=0xffff');App.galRender();});
  assert(await page.evaluate(()=>App.gal.list.length===TEMPLATES.length),'legacy requirements remain browsable');
  assert(await page.isDisabled('#gSupportedOnly'),'unsupported filter unavailable without a report');
  assert(/capability report unavailable/.test(await page.textContent('#gSupport')));
  assert(await page.evaluate(()=>App.gal.list.filter(t=>t.requires.includes('Ble')).every(t=>templateSupport(t,App.capabilities,true,App.info).state==='unknown')),'unknown is not incompatible');
  // Reconnecting to current firmware restores the saved filter, without guessing.
  await page.evaluate(()=>{App.capabilitiesKnown=true;App.capabilities=new Map([['Compiler',true],['Ble',false]]);App.galRender();});
  assert(await page.isChecked('#gSupportedOnly'));assert(!await page.isDisabled('#gSupportedOnly'));
  assert(await page.evaluate(()=>App.gal.list.every(t=>templateSupport(t,App.capabilities,true,App.info).state==='supported')));
  // No report at all (or a rejected report): never label missing capabilities false.
  await page.evaluate(()=>{App.capabilitiesKnown=false;App.capabilities=null;App.galRender();});
  assert(await page.evaluate(()=>App.gal.list.length===TEMPLATES.length && App.gal.list.every(t=>templateSupport(t,null,true,App.info).state==='unknown')));
  assert(await page.isDisabled('#gSupportedOnly'));assert(await page.evaluate(()=>App.gal.list.length===TEMPLATES.length));
  // Exercise the actual connection handler, not just manually assigned states.
  const handshakes=await page.evaluate(async()=>{
   const saved={op:dev.op,machine:dev.machine,cmd:dev.cmd,refresh:App.refresh};
   const result=[];let report,calls;
   dev.op=async fn=>fn();dev.machine=async()=>{calls.push('machine');};App.refresh=async()=>{};
   dev.cmd=async line=>{calls.push(line);if(line==='info')return{status:'OK',out:new TextEncoder().encode('MicroCS 1.19.0 features=0xffff value=16 bytes\n')};if(report==='timeout')throw new Error('The device did not answer');return report;};
   try {
    for(report of [
     {status:'ERR unknown command',out:new Uint8Array()},
     {status:'OK',out:new TextEncoder().encode('capabilities=2\nBle=1\n')},
     {status:'OK',out:new TextEncoder().encode('capabilities=1\nBle=1\nBle=0\n')},
     'timeout',
     {status:'OK',out:new TextEncoder().encode('capabilities=1\nCompiler=1\nGPIO=1\nBle=0\n')}
    ]) {
     calls=[];await App.afterConnect();
     result.push({known:App.capabilitiesKnown,all:App.gal.list.length===TEMPLATES.length,
      compiler:App.capabilities.get('Compiler'),ble:templateSupport({requires:['Ble']},App.capabilities,true).state,
      disabled:document.getElementById('gSupportedOnly').disabled,calls});
    }
   } finally {dev.op=saved.op;dev.machine=saved.machine;dev.cmd=saved.cmd;App.refresh=saved.refresh;}
   return result;
  });
  for(const h of handshakes.slice(0,4))assert.deepStrictEqual(h,{known:false,all:true,compiler:true,ble:'unknown',disabled:true,calls:['machine','info','caps']});
  assert.deepStrictEqual(handshakes[4],{known:true,all:false,compiler:true,ble:'unsupported',disabled:false,calls:['machine','info','caps']});
  assert.deepStrictEqual(errors,[]); console.log('PASS default-on supported filter, exact Wi-Fi capability, unknown/legacy fallback and reconnect, offline, empty search, persistence');
 } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});
