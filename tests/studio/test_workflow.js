// Exercise the actual workflow lock with an intentionally delayed run -> Save handoff.
const fs = require('fs'), path = require('path'), assert = require('assert');
const source = fs.readFileSync(path.join(__dirname,'../../tools/studio/studio.js'),'utf8');
const a = source.indexOf('  async workflow(action) {'), b = source.indexOf('  async checkScripts(paths)',a);
assert(a>0 && b>a);
const deferred = () => { let resolve; const promise=new Promise(r=>resolve=r); return {promise,resolve}; };
function setup() {
  const dev={running:false}, states=[], notices=[];
  const workflow = new Function('dev','ed','toast','term','return ({'+source.slice(a,b)+'}).workflow;')(
    dev,{sigHide(){},hoverHide(){},hideComplete(){}},msg=>notices.push(msg),{line(){}});
  const app={workflow,updateButtons(){states.push(!!this.workflowBusy);}};
  return {app,dev,states,notices};
}
(async()=>{
  const {app,dev,states}=setup(), run=deferred(), stopped=deferred(), saved=deferred();
  const first=app.workflow(async()=>{dev.running=true;await run.promise;dev.running=false;return true;});
  app.stopForUpload=async()=>{run.resolve();await stopped.promise;};
  let saves=0, duplicates=0;
  const next=app.workflow(async()=>{saves++;await saved.promise;return true;});
  // The run can complete well before the pending stop/save continuation resumes.
  await first;
  assert.strictEqual(app.workflowBusy,true,'must not expose idle during handoff');
  assert.strictEqual(await app.workflow(()=>{duplicates++;}),false);
  stopped.resolve(); await Promise.resolve(); await Promise.resolve();
  assert.strictEqual(app.workflowBusy,true);
  saved.resolve(); assert.strictEqual(await next,true);
  assert.strictEqual(saves,1); assert.strictEqual(duplicates,0);
  assert.strictEqual(app.workflowBusy,false); assert.strictEqual(app.workflowDone,null);
  assert(states.slice(0,-1).every(Boolean),'buttons must remain busy until Save finishes');
  const failure=setup(), active=deferred();
  const job=failure.app.workflow(async()=>{failure.dev.running=true;await active.promise;failure.dev.running=false;});
  failure.app.stopForUpload=async()=>{throw new Error('cannot stop');};
  assert.strictEqual(await failure.app.workflow(()=>{throw new Error('must not run');}),false);
  assert.strictEqual(failure.app.workflowBusy,true); assert.strictEqual(failure.app.workflowHandoff,false);
  active.resolve();await job;assert.strictEqual(failure.app.workflowBusy,false);
  console.log('PASS workflow: delayed run/Save handoff, continuous busy state, duplicate rejection, stop failure recovery');
})().catch(e=>{console.error(e);process.exitCode=1;});
