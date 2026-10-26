// Real host shell/file operations behind a legacy protocol shim: no caps/check endpoints.
let chromium;try{({chromium}=require('playwright'));}catch{({chromium}=require('playwright-core'));}
const {spawn}=require('child_process'),fs=require('fs'),path=require('path'),assert=require('assert');
(async()=>{
 const root=path.resolve('build/studio-legacy-fs');fs.rmSync(root,{recursive:true,force:true});fs.mkdirSync(root,{recursive:true});
 const proc=spawn(path.resolve(process.argv[2]||'./mcs'),['--shell','--sim','--fs',root]);
 let browser,page,chain=Promise.resolve(),errors=[],commands=[];
 try{
  browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
  await page.exposeFunction('__write',async bytes=>{
   const b=Buffer.from(bytes),s=b.toString();
   if (/^(?:caps|info|check .*)\n$/.test(s)) {
    commands.push(s.trim());
    const text=s==='info\n'?'MicroCS 1.14.0 features=0xffff value=16 bytes\n\x04OK\n':'\x04ERR unknown command\n';
    await page.evaluate(a=>window.__push?.(a),[...Buffer.from(text)]);return;
   }
   if(/^(?:run|project) /.test(s))commands.push(s.trim());proc.stdin.write(b);
  });
  proc.stdout.on('data',b=>{chain=chain.then(()=>page.evaluate(a=>window.__push?.(a),[...b]).catch(()=>{}));});
  await page.addInitScript(()=>{
   let controller;const port={async open(){port.readable=new ReadableStream({start(c){controller=c;}});port.writable=new WritableStream({write(b){return window.__write([...b]);}});},async close(){},async setSignals(){}};
   window.__push=a=>controller?.enqueue(new Uint8Array(a));
   Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>port,getPorts:async()=>[port],addEventListener(){}}});
  });
  await page.goto('file://'+path.resolve('tools/studio/index.html'));await page.click('#btnConnect');
  await page.waitForFunction(()=>document.querySelector('#connText').textContent.startsWith('MicroCS 1.14.0'));
  await page.waitForFunction(()=>!dev.pending);
  await page.evaluate(()=>{
   App.openTab({path:'/helper.cs',text:'File.WriteAllText("/must-not-run","bad");',saved:null});
   App.openTab({path:'/app.cs',text:'File.AppendAllText("/ran","A");',saved:null});
  });
  assert.strictEqual(await page.evaluate(()=>App.save()),true,'legacy default save succeeds');
  assert(!fs.existsSync(root+'/ran')&&!fs.existsSync(root+'/must-not-run'),'saving never executes either file');
  assert(fs.existsSync(root+'/app.cs')&&fs.existsSync(root+'/helper.cs'));
  assert.strictEqual(commands.filter(x=>x.startsWith('check ')).length,1,'missing command is cached');
  await page.evaluate(()=>App.runCurrent(false,true));
  await page.waitForFunction(()=>!App.workflowBusy&&!dev.running);
  assert.strictEqual(fs.readFileSync(root+'/ran','utf8'),'A');assert(!fs.existsSync(root+'/must-not-run'));
  assert.deepStrictEqual(commands.filter(x=>x.startsWith('run ')),['run /app.cs']);
  assert((await page.innerText('#term')).includes('without compile-only validation'));
  await page.evaluate(()=>App.gallery());
  assert(await page.isChecked('#gSupportedOnly'), 'supported-only filter defaults on');
  assert(await page.evaluate(() => App.gal.list.every(t => templateSupport(t, App.capabilities, true, App.info).state === 'supported')), 'unknown is not assumed supported');
  await page.uncheck('#gSupportedOnly');
  const gallery = await page.evaluate(() => ({
   shown: App.gal.list.map(t => t.src), bundled: TEMPLATES.map(t => t.src)
  }));
  assert(gallery.bundled.length > 0, 'template bundle must not be empty');
  assert.deepStrictEqual(gallery.shown, gallery.bundled, 'legacy firmware must keep every bundled template');
  assert.strictEqual(await page.locator('#gItems .item').count(), gallery.bundled.length, 'every bundled template is rendered');
  assert((await page.innerText('#gSupport')).includes('unknown'));
  assert.deepStrictEqual(errors,[]);
  console.log('PASS legacy serial workflow: no caps/check, all templates, save-all without execution, run active file only, no repeated unsupported probes');
 }finally{proc.kill();if(browser)await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
