// Execute production putReliable: native failures require opt-in app cleanup.
const fs=require('fs'),path=require('path'),assert=require('assert');
const js=fs.readFileSync(path.join(__dirname,'../../tools/studio/studio.js'),'utf8');
const a=js.indexOf('  async putReliable('),b=js.indexOf('  async transfer(',a);
function setup(message,answer,capability=true){
 const events=[];let calls=0;
 const dev={op:async f=>f(),put:async()=>{events.push('put');if(!calls++)throw new Error(message);return 'OK';}};
 const method=new Function('dev','term','dialog','return ({'+js.slice(a,b)+'}).putReliable;')(
 dev,{line(){}},async()=>{events.push('confirm');return answer;});
 const app={capabilities:new Map([['Runtime.Replace',capability]]),stopForUpload:async()=>{},checkSpace(){},prepareRun:async()=>events.push('unload')};
 return{run:()=>method.call(app,'/generic.cs',new Uint8Array([1])),events};
}
(async()=>{
 let t=setup('out of memory',true);assert.equal(await t.run(),'OK');assert.deepEqual(t.events,['put','confirm','unload','put']);
 t=setup('I/O error',false);await assert.rejects(t.run(),/I\/O error/);assert.deepEqual(t.events,['put','confirm']);
 t=setup('out of memory',true,false);await assert.rejects(t.run(),/Native filesystem memory/);assert.deepEqual(t.events,['put']);
 t=setup('permission denied',true);await assert.rejects(t.run(),/permission denied/);assert.deepEqual(t.events,['put']);
 t=setup('upload timed out',true);assert.equal(await t.run(),'OK');assert.deepEqual(t.events,['put','put']);
 console.log('PASS upload recovery: consent, cancel, legacy firmware, unrelated error and timeout');
})().catch(e=>{console.error(e);process.exitCode=1;});
