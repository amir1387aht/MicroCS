// Pure, offline language regressions: no browser or network required.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.join(__dirname,'../../tools/studio');const context={window:{},console};vm.createContext(context);
for(const file of ['api.js','docs.js','format.js','lang.js'])vm.runInContext(fs.readFileSync(path.join(root,file),'utf8'),context);
const L=context.window.Lang;
const source=`class Parent { public string Label; }
class App : Parent {
 public Dictionary<string, List<int>> Values;
 public string Name;
 public string GetName() { return Name; }
 void First(string arg) { string secret = "hidden"; }
 void Second(int arg) { int local = 1; { string nested = "gone"; } /*HERE*/ }
}`;
const p=source.indexOf('/*HERE*/');
assert.strictEqual(L.varType('arg',source,p),'int');
assert.strictEqual(L.varType('secret',source,p),null);
assert.strictEqual(L.varType('nested',source,p),null);
assert.strictEqual(L.locals(source,p).has('secret'),false);
assert.strictEqual(L.typeOfExpr('this.Name',source,p),'string');
assert.strictEqual(L.typeOfExpr('base.Label',source,p),'string');
assert.strictEqual(L.typeOfExpr('GetName()',source,p),'string');
assert.strictEqual(L.typeOfExpr('Values["key"]',source,p),'List<int>');
assert.strictEqual(L.typeOfExpr('Values["key"][0]',source,p),'int');
const literal='var text = """\nclass Fake { }\n""";\nclass Real {}';
assert(!L.symbols(literal).user.Fake);assert(L.symbols(literal).user.Real);
const call='Console.WriteLine(new Dictionary<string, List<int>>(), ';
assert.strictEqual(L.callContext(call,call.length).arg,1);
const signature='void First(string arg) { }\nvoid Second(int arg) { /*HERE*/ }';
assert.strictEqual(L.varType('arg',signature,signature.indexOf('/*HERE*/')),'int');
assert.strictEqual(L.typeOfExpr('"x".Split(")")[0]',source,p),'string');
assert.strictEqual(L.splitChain('value + unrelated'),null);
const loop='void Run() { for (int i=0;i<3;i++) Console.WriteLine(i); /*AFTER*/ }';
assert.strictEqual(L.varType('i',loop,loop.indexOf('WriteLine(i)')+10),'int');
assert.strictEqual(L.varType('i',loop,loop.indexOf('/*AFTER*/')),null);
console.log('PASS language: lexical scopes, parameters, this/base, method returns, dictionary values, raw strings, generic call arguments and literal-safe chains');
