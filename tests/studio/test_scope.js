// Real shared language service; scope/type lookup, not a simulated IDE result.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
global.window={};
for(const f of ['docs.js','lang.js']) vm.runInThisContext(fs.readFileSync(path.join(__dirname,'../../tools/studio',f),'utf8'));
const L=window.Lang;
let checks=0;
function test(src,expect,absent=[]) {
  const pos=src.indexOf('/*caret*/'); assert(pos>=0);
  const locals=L.locals(src,pos);
  for(const [name,type] of Object.entries(expect)) { assert(locals.has(name),`missing ${name}: ${src}`); assert.strictEqual(L.varType(name,src,pos),type,`type ${name}: ${src}`); checks++; }
  for(const name of absent) { assert(!locals.has(name),`leaked ${name}: ${src}`); assert.strictEqual(L.varType(name,src,pos),null,`leaked type ${name}: ${src}`); checks++; }
}
test('int outer=1; { string inner="x"; } /*caret*/',{outer:'int'},['inner']);
test('void A(int first) { string leaked="x"; } void B(int second) { /*caret*/ }',{second:'int'},['first','leaked']);
test('int value=1; { string value="x"; /*caret*/ }',{value:'string'});
test('int value=1; { string value="x"; } /*caret*/',{value:'int'});
test('void A(int count) { var text="x"; /*caret*/ }',{count:'int',text:'string'});
test('for(int i=0;i<2;i++) { string text="x"; /*caret*/ }',{i:'int',text:'string'});
test('for(int i=0;i<2;i++) { string text="x"; } /*caret*/',{},['i','text']);
test('for(int i=0;i<2;i++) Console.WriteLine(i); /*caret*/',{},['i']);
test('var values=new int[2]; foreach(var item in values) { /*caret*/ }',{values:'int[]',item:'int'});
test('var values=new int[2]; foreach(var item in values) Console.WriteLine(item); /*caret*/',{values:'int[]'},['item']);
test('try {} catch(Exception error) { /*caret*/ }',{error:'Exception'});
test('try {} catch(Exception error) {} /*caret*/',{},['error']);
test('var action=(int count) => { string local="x"; /*caret*/ };',{count:'int',local:'string',action:null});
test('var action=(int count) => { string local="x"; }; /*caret*/',{action:null},['count','local']);
test('var action=arg => { /*caret*/ };',{arg:null,action:null});
test('var action=arg => { }; /*caret*/',{action:null},['arg']);
test('class A { int field; void Run(int param) { string own="x"; /*caret*/ } } class B { bool other; }',{param:'int',own:'string'},['other']);
const fields='class A { int field; void Run() { /*caret*/ } }'; assert.strictEqual(L.varType('field',fields,fields.indexOf('/*caret*/')),'int'); checks++;
test('class A { public A(int number) { /*caret*/ } }',{number:'int'});
test('class A { public A(int number) { } void Run() { /*caret*/ } }',{},['number']);
test('int outer=1; void Run(int arg) { string value="x"; /*caret*/',{outer:'int',arg:'int',value:'string'});
test('int real=1; // string fake="x";\nvar text="int ghost=1;"; /*caret*/',{real:'int',text:'string'},['fake','ghost']);
test('void A(int leaked) {} void B(string actual) { var text=$"{actual.Length /*caret*/}"; }',{actual:'string',text:'string'},['leaked']);
test('void Run(ref int count, params string[] names) { /*caret*/ }',{count:'int',names:'string[]'});
test('try {} catch(Exception error) when(error!=null) {} /*caret*/',{},['error']);
test('for(int i=0;i<2;i++) if(i>0) Console.WriteLine(i); else Console.WriteLine(i); /*caret*/',{},['i']);
test('string value="x"; void Run() { int value=1; } value./*caret*/',{value:'string'});
test('record Point(int X, int Y); void Later(int arg) { int local=1; /*caret*/ }',{arg:'int',local:'int'});
test('void Run(string separator="a,b", int number=1) { /*caret*/ }',{separator:'string',number:'int'});
assert.strictEqual(L.classes.Tft.ctors.length,1); checks++;
assert.strictEqual(L.classes.TftSprite.ctors.length,1); checks++;
assert.strictEqual(L.classes.TftSprite.ctors[0].params.length,4); checks++;
assert.strictEqual(L.classes.TftSprite.ctors[0].params[3].def,'16'); checks++;
const documented=L.symbols('void Run(string separator="a,b", int number=1) {}').funcs.Run[0];
assert.strictEqual(documented.params.length,2); assert.strictEqual(documented.params[0].def,'"a,b"'); checks++;
for (const [source, present, absent] of [
  ['void Global() {} { void Local() {} /*caret*/ }', ['Global','Local'], []],
  ['void Global() {} { void Local() {} } /*caret*/', ['Global'], ['Local']],
  ['class A { void Run() { void Local(int p) {} /*caret*/ } }', ['Local'], []],
  ['class A { void Run() { void Local(int p) {} } void Other() { /*caret*/ } }', [], ['Local']]
]) {
  const pos=source.indexOf('/*caret*/'), funcs=L.functionsAt(source,pos);
  for(const n of present) { assert(funcs[n],`missing function ${n}`); checks++; }
  for(const n of absent) { assert(!funcs[n],`leaked function ${n}`); checks++; }
}
const literal='var text="first;second"; /*caret*/'; assert.strictEqual(L.varType('text',literal,literal.indexOf('/*caret*/')),'string'); checks++;
const cls=L.classInfo('A','class A { void Run(string separator="a,b", int count=1) {} }');
assert.strictEqual(cls.i.Run[0].params.length,2); assert.strictEqual(cls.i.Run[0].params[0].def,'"a,b"'); checks++;
const multiline='class A { void Run(string separator=@"[first],\nsecond", int count=1) {} }';
const multilineParam=L.classInfo('A',multiline).i.Run[0].params[0];
assert.strictEqual(multilineParam.def,'@"[first],\nsecond"'); checks++;
console.log(`PASS scoped declarations: ${checks} visibility/type assertions`);
