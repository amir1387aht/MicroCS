// Pure completion / signature / gallery metadata regression, without a browser.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '../../tools/studio');
const context = { window: {} };
for (const file of ['docs.js', 'api.js', 'lang.js'])
  vm.runInNewContext(fs.readFileSync(path.join(root,file),'utf8'), context);
const lang = context.window.Lang;
assert(lang.isClassName('RotaryEncoder', ''));
const ctor = lang.ctorsOf('RotaryEncoder','')[0];
assert(ctor && ctor.params.length === 5);
assert.strictEqual(ctor.params[2].def, '4');
assert.strictEqual(ctor.params[3].def, '-1');
const source = 'var encoder = new RotaryEncoder(32, 33, 4, 25);\nencoder.';
assert.strictEqual(lang.varType('encoder',source,source.length), 'RotaryEncoder');
const members = lang.membersOf('RotaryEncoder',false,source);
for (const name of ['Backend','Position','Reset','ButtonPressed','Dispose']) assert(members[name], name);
assert.strictEqual(members.Position[0].ret, 'int');
assert.strictEqual(members.Backend[0].ret, 'string');
assert.strictEqual(members.ButtonPressed[0].ret, 'bool');
const templates = new Function(fs.readFileSync(path.join(root,'templates.js'),'utf8') + ';return TEMPLATES;')();
const template = templates.find(t => t.src === '22-input-devices/encoder.cs');
assert(template.requires.includes('RotaryEncoder'));
assert(template.text.includes('finally { encoder.Dispose(); }'));
assert(!template.text.includes('.OnChange('));
console.log('PASS Studio RotaryEncoder completion, constructor defaults and capability-gated template');
