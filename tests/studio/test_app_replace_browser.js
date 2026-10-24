// Real runtime (not the legacy standalone CLI) over fake Web Serial.
let chromium; try { ({chromium}=require('playwright')); } catch { ({chromium}=require('playwright-core')); }
const {spawn}=require('child_process');
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve('build/studio-app-replace-fs');
fs.rmSync(root,{recursive:true,force:true});fs.mkdirSync(root,{recursive:true});
const proc=spawn(path.resolve('./build/test_app_replace'),['--fs',root]);
let browser,page,closing=false,chain=Promise.resolve();const errors=[];
// Only shutdown may close the pipe; transport failures during the test still fail it.
proc.stdin.on('error',e=>{if(!closing)errors.push('runtime stdin: '+e.message);});
proc.on('error',e=>{if(!closing)errors.push('runtime process: '+e.message);});
proc.on('exit',(code,signal)=>{if(!closing)errors.push('runtime exited: '+code+'/'+signal);});
(async()=>{
 browser=await chromium.launch({executablePath:process.env.CHROME||'/usr/bin/chromium',args:['--no-sandbox']});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await page.exposeFunction('__write',a=>{
  if(closing)return;
  return new Promise((resolve,reject)=>{
   proc.stdin.write(Buffer.from(a),e=>{if(e&&!closing)reject(e);else resolve();});
  });
 });
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
 // A script writes an already-open settings file without changing its size.
 await page.evaluate(async()=>{
  const scratch=App.tab();scratch.dirty=false;await App.closeTab(scratch.id);
  await dev.op(()=>dev.put('/settings.json',enc.encode('{"value":1}')));
  await App.openPath('/settings.json');
  const app=App.tabs.find(t=>t.path==='/app.cs');App.activate(app.id);
  ed.ta.value='File.WriteAllText("/settings.json", "{\\\"value\\\":2}");';ed.refresh();ed.onChange();
 });
 assert.strictEqual(await page.evaluate(()=>App.runCurrent(false,true)),true);
 assert.strictEqual(fs.readFileSync(root+'/settings.json','utf8'),'{"value":2}');
 assert.strictEqual(await page.evaluate(()=>App.tabs.find(t=>t.path==='/settings.json').text),'{"value":2}');
 // Writes outside the toolbar Run path also update the visible file. The idle
 // check uses get only; it must not unload the app or cancel callbacks.
 await page.evaluate(async()=>{
  const settings=App.tabs.find(t=>t.path==='/settings.json');App.activate(settings.id);
  await dev.op(()=>dev.cmd('exec File.WriteAllText("/settings.json", "{\\\"value\\\":3}");'));
  store.set('liveFileRefresh',true);window.__commands=[];
 });
 await page.waitForFunction(()=>$('ta').value==='{"value":3}');
 assert(!(await page.evaluate(()=>window.__commands)).some(c=>/^(newapp|cancel|run|exec)\b/.test(c)),'idle refresh is read-only');
 assert.deepStrictEqual(errors,[]);
 console.log('PASS real-runtime browser: 10 F5 reruns release encoder/timer, unload precedes check, files persist, Save preserves globals, same-size script writes and idle open-file refresh');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{
 // Closing Chromium first stops pending Web Serial writes before the process exits.
 closing=true;
 try {if(browser)await browser.close();}
 finally {proc.stdin.destroy();proc.kill();}
});
