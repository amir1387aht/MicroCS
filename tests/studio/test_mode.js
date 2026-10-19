// Exercise production action methods: storage failures and asynchronous handoffs.
const fs=require('fs'),path=require('path'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../../tools/studio/studio.js'),'utf8');
function method(start,end,deps={}) {
  const a=source.indexOf('  '+start),b=source.indexOf('  '+end,a);
  assert(a>0&&b>a);
  return new Function(...Object.keys(deps),'return ({'+source.slice(a,b)+'});')(...Object.values(deps));
}
(async()=>{
  const mode={value:'file'},store={get(){throw Error('live mode must not read storage');},set(){/* browser store swallows failures */}};
  const app={updateButtons(){},...method('setProjectMode(project)','projectRoot()',{$:()=>mode,store})};
  app.setProjectMode(true);assert.strictEqual(app.projectMode,true);assert.strictEqual(mode.value,'project');
  app.setProjectMode(false);assert.strictEqual(app.projectMode,false);assert.strictEqual(mode.value,'file');
  const calls=[],dev={connected:true},ed={stash(){}};
  Object.assign(app,method('async save(t = this.tab())','async runFilePath(path)',{store}),
    method('async runCurrent(selectionOnly','async runPath(path', {dev,ed}));
  app.tab=()=>({path:'/active.cs'});app.saveBuild=async(t,p)=>{calls.push(p?'project-build':'file-build');return false;};
  let action;app.workflow=fn=>{action=fn;return 'pending';};
  app.setProjectMode(true);assert.strictEqual(await app.save(),'pending');app.setProjectMode(false);await action();
  assert.deepStrictEqual(calls.splice(0),['project-build']);
  app.setProjectMode(false);await app.runCurrent(false);app.setProjectMode(true);await action();
  assert.deepStrictEqual(calls.splice(0),['file-build']);
  app.setProjectMode(true);await app.runCurrent(false);app.setProjectMode(false);await action();
  assert.deepStrictEqual(calls.splice(0),['project-build']);
  app.setProjectMode(true);await app.runCurrent(false,true);await action();assert.deepStrictEqual(calls,['file-build']);
  console.log('PASS mode: in-memory authority, storage-independent switching, Save/F5 snapshots, explicit Run file');
})().catch(e=>{console.error(e);process.exitCode=1;});
