// Real runtime (not the legacy standalone CLI) over fake Web Serial.
let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const {spawn}=require('child_process');
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve('build/studio-app-replace-fs');
fs.rmSync(root,{recursive:true,force:true});fs.mkdirSync(root,{recursive:true});
const proc=spawn(path.resolve('./build/test_app_replace'),['--fs',root]);
let browser,page,chain=Promise.resolve();const errors=[];
(async()=>{
 browser=await chromium.launch({executablePath:process.env.CHROME||'/usr/bin/chromium',args:['--no-sandbox']});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await page.exposeFunction('__write',a=>{proc.stdin.write(Buffer.from(a));});
 proc.stdout.on('data',d=>{chain=chain.then(()=>page.evaluate(a=>window.__push?.(a),[...d]).catch(()=>{}));});
 await page.addInitScript(()=>{
  let ctl;const port={async open(){port.readable=new ReadableStream({start(c){ctl=c;}});port.writable=new WritableStream({write(a){return window.__write([...a]);}});},async close(){},async setSignals(){}};
  window.__push=a=>ctl?.enqueue(new Uint8Array(a));
  Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>port,getPorts:async()=>[port],addEventListener(){}}});
 });
 await page.goto('file://'+path.resolve('tools/studio/index.html'));await page.click('#btnConnect');
 await page.waitForFunction(()=>App.capabilities?.get('Runtime.Replace')===true);
 await page.evaluate(()=>{
  const cmd=dev.cmd;window.__commands=[];
  dev.cmd=function(c,...a){window.__commands.push(c);return cmd.call(this,c,...a);};
  App.openTab({path:'/app.cs',saved:null,text:'class Demo { public int Value = 42; } var app = new Demo(); var encoder = new RotaryEncoder(18,7,2,8); Timer.Start(0,60000000,()=>{}); File.AppendAllText("/runs","x");'});
 });
 for(let i=0;i<10;i++){
  const result=await page.evaluate(async()=>{window.__commands=[];return await App.runCurrent(false,true);});
  assert.strictEqual(result,true);
  const commands=await page.evaluate(()=>window.__commands);
  assert(commands.indexOf('newapp')>=0 && commands.indexOf('newapp')<commands.indexOf('check /app.cs'));
  assert.strictEqual(fs.readFileSync(root+'/runs','utf8'),'x'.repeat(i+1));
 }
 await page.evaluate(()=>{window.__commands=[];});
 assert.strictEqual(await page.evaluate(()=>App.save()),true);
 assert(!(await page.evaluate(()=>window.__commands)).includes('newapp'),'Save must not unload');
 const result=await page.evaluate(async()=>{
  const r=await dev.op(()=>dev.cmd('exec Console.WriteLine(app.Value);'));
  return {status:r.status,out:new TextDecoder().decode(r.out)};
 });
 assert.strictEqual(result.status,'OK');assert(result.out.includes('42'));
 await page.evaluate(async()=>{
  App.newScratch({file:'selection.cs',text:'Console.WriteLine(app.Value);'});
  ed.ta.setSelectionRange(0,ed.ta.value.length);window.__commands=[];
  await App.runCurrent(true);
 });
 const selection=await page.evaluate(()=>window.__commands);
 assert(!selection.includes('newapp'));
 assert(selection.includes('run /.studio_run.cs shared'));
 assert.deepStrictEqual(errors,[]);
 console.log('PASS real-runtime browser: 10 F5 reruns release encoder/timer, unload precedes check, files persist, Save preserves globals');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{proc.kill();if(browser)await browser.close();});
