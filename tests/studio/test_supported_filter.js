let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const assert=require('assert'),path=require('path');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1400,height:900}}), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('file://'+path.resolve('tools/studio/index.html'));
  await page.evaluate(()=>{dev.port={};App.capabilities=new Map([['Compiler',true],['Wifi',false],['Ble',false],['Filesystem',true]]);App.info='MicroCS 1.17.0';App.gallery();});
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
  await page.evaluate(()=>{dev.port={};App.capabilities=legacyCapabilities('features=0xffff');App.galRender();});
  assert(await page.evaluate(()=>App.gal.list.every(t=>templateSupport(t,App.capabilities,true,App.info).state==='supported')),'legacy unknown hidden, not guessed');
  await page.uncheck('#gSupportedOnly');assert(await page.evaluate(()=>App.gal.list.length===TEMPLATES.length));
  assert.deepStrictEqual(errors,[]); console.log('PASS default-on supported filter, exact Wi-Fi capability, unknown/legacy, offline, empty search, persistence');
 } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});
