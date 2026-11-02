// Offline scope/access regressions shared by CI and local development.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.join(__dirname,'../../tools/studio'),context={window:{},console};vm.createContext(context);
for(const file of ['api.js','docs.js','format.js','lang.js'])vm.runInContext(fs.readFileSync(path.join(root,file),'utf8'),context);
const L=context.window.Lang;
const source=`class Box {
 private int Secret;
 int DefaultSecret;
 public int Value;
 protected int ProtectedValue;
 private static int StaticSecret;
 private int Hidden() { return Secret; }
 public int Read() { /*OWN*/ return Secret; }
}
class Child : Box { public int ReadChild() { /*CHILD*/ return ProtectedValue; } }
class Stranger { public void Run() { Box b = new Box(); /*OUTSIDE*/ } }
`;
const own=source.indexOf('/*OWN*/'),child=source.indexOf('/*CHILD*/'),out=source.indexOf('/*OUTSIDE*/');
for(const name of ['Secret','DefaultSecret','Hidden']){
 assert(L.membersOf('Box',false,source,own)[name],name+' visible inside declaring class');
 assert(!L.membersOf('Box',false,source,out)[name],name+' hidden outside declaring class');
 assert(!L.membersOf('Child',false,source,child)[name],name+' hidden in derived class');
}
assert(L.membersOf('Box',true,source,own).StaticSecret);
assert(!L.membersOf('Box',true,source,out).StaticSecret);
assert(L.membersOf('Child',false,source,child).ProtectedValue);
assert(!L.membersOf('Box',false,source,child).ProtectedValue,'base-typed receiver is not a derived instance');
assert(L.membersOf('Box',false,source,child,true).ProtectedValue,'base keyword is allowed');
assert(!L.membersOf('Box',false,source,out).ProtectedValue);
assert(L.membersOf('Box',false,source,out).Value);
assert.strictEqual(L.typeOfExpr('b.Secret',source,out),null);
assert.strictEqual(L.typeOfExpr('b.Value',source,out),'int');
assert.strictEqual(L.signaturesFor({name:'Hidden',recv:'b',open:out},source).length,0);
assert.strictEqual(L.signaturesFor({name:'Read',recv:'b',open:out},source).length,1);
const constructors='class Factory { private Factory(){} public Factory(int x){} void Inside(){ /*HERE*/ } }';
assert.strictEqual(L.ctorsOf('Factory',constructors,0).length,1);
assert.strictEqual(L.ctorsOf('Factory',constructors,constructors.indexOf('/*HERE*/')).length,2);
const lib='namespace Library { class Widget { private int Secret; public int Value; public int Read()=>Value; } }';
let model='class Client { void Run(){ /*HERE*/ } }'+L.FILE_BOUNDARY+lib;
let p=model.indexOf('/*HERE*/');
assert.strictEqual(L.resolveUser('Widget',model,p),null,'unimported type is not a short-name suggestion');
assert(L.resolveUser('Library.Widget',model,p));
assert(L.classInfo('Library.Widget',model,p));
assert(!L.membersOf('Library.Widget',false,model,p).Secret);
assert(L.membersOf('Library.Widget',false,model,p).Value);
assert(L.membersOf('Library.Widget',false,model,p).Read,'expression-bodied method is indexed');
model='using Library; class Client { void Run(){ Widget w; /*HERE*/ } }'+L.FILE_BOUNDARY+lib;
p=model.indexOf('/*HERE*/');
assert.strictEqual(L.resolveUser('Widget',model,p).name,'Library.Widget');
assert.strictEqual(L.typeOfExpr('w.Value',model,p),'int');
assert.strictEqual(L.typeOfExpr('Library.Widget',model,p),'Library.Widget');
model='class Client { void Run(){ /*HERE*/ } }'+L.FILE_BOUNDARY+'using Library; class Other {}'+L.FILE_BOUNDARY+lib;
p=model.indexOf('/*HERE*/');
assert.strictEqual(L.resolveUser('Widget',model,p),null,'using in another tab must not leak');
model='class Client { void Run(){ /*HERE*/ } }'+L.FILE_BOUNDARY+'namespace FileScoped; class Hidden {}';
p=model.indexOf('/*HERE*/');
assert.strictEqual(L.resolveUser('Hidden',model,p),null);
assert(L.resolveUser('FileScoped.Hidden',model,p));
model='using A; using B; class Client { void Run(){ /*HERE*/ } } namespace A { class Item {} } namespace B { class Item {} }';
p=model.indexOf('/*HERE*/');
assert.strictEqual(L.resolveUser('Item',model,p),null,'ambiguous imports do not pick an arbitrary declaration');
assert(L.resolveUser('A.Item',model,p));assert(L.resolveUser('B.Item',model,p));
console.log('PASS language scope: private/default/protected/static access, constructors, signature help, namespaces, ambiguous imports and tab isolation');

const enumModel='class Client { void Run(){ /*HERE*/ } }'+L.FILE_BOUNDARY+'namespace Library { enum State { Idle, Busy } }';
assert.strictEqual(L.resolveUser('State',enumModel,enumModel.indexOf('/*HERE*/')),null);
assert(L.resolveUser('Library.State',enumModel,enumModel.indexOf('/*HERE*/')));
const parentModel='namespace A { class Parent {} } namespace A.B { class Child { /*HERE*/ } }';
assert(L.resolveUser('Parent',parentModel,parentModel.indexOf('/*HERE*/')));
