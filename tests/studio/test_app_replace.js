// Execute the real Run preparation and saveBuild methods; old firmware must not
// receive a destructive command inferred from its version or feature bits.
const fs=require('fs'), path=require('path'), assert=require('assert');
const js=fs.readFileSync(path.join(__dirname,'../../tools/studio/studio.js'),'utf8');
const a=js.indexOf('  async prepareRun() {'), b=js.indexOf('  async save(t =',a);
assert(a>0 && b>a);
function setup(capability, status='OK') {
  const events=[], dev={connected:true, running:false,
    op: async f=>f(), cmd: async c=>{events.push(c);return {status};}};
  const app=new Function('dev','ed','store','term','enc',
    'return ({'+js.slice(a,b)+'});')(dev,{stash(){}},{get:(k,d)=>k==='formatOnSave'?false:d},
      {line:(text,kind)=>events.push(kind+':'+text)},new TextEncoder());
  Object.assign(app,{capabilities:new Map(capability===undefined?[]:[['Runtime.Replace',capability]]),
    tabs:[],tab:()=>null,stopForUpload:async()=>events.push('stop'),
    checkScripts:async()=>events.push('check'),refresh(){}});
  return {app,events};
}
(async()=>{
  let t=setup(true);
  await t.app.saveBuild(null,false,true);
  assert(t.events.indexOf('newapp')>=0 && t.events.indexOf('newapp')<t.events.indexOf('check'));
  t=setup(true);await t.app.saveBuild(null,false,false);
  assert(!t.events.includes('newapp') && t.events.includes('check'),'Save/Check must preserve app');
  t=setup(true,'ERR workers did not stop');
  await assert.rejects(t.app.saveBuild(null,false,true),/Cannot replace previous app/);
  assert(!t.events.includes('check'),'must not continue after replacement failure');
  for(const capability of [false,undefined]) {
    t=setup(capability);await t.app.saveBuild(null,false,true);
    assert(!t.events.includes('newapp') && t.events.includes('check'));
    assert(t.events.some(e=>e.includes('legacy firmware')));
  }
  assert(js.includes('saveBuild(project ? null : t, project, true)'));
  assert(js.includes('label === "selection" ? " shared" : ""'));
  console.log('PASS app replacement: Run unloads before Check; Save/selection preserve state; failure and legacy handling');
})().catch(e=>{console.error(e);process.exitCode=1;});
