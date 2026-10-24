// JSON completion, signatures, aliases and capability-gated examples.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '../../tools/studio'), context = {window:{}};
for (const file of ['docs.js','api.js','lang.js']) vm.runInNewContext(fs.readFileSync(path.join(root,file),'utf8'),context);
const lang = context.window.Lang;
for (const cls of ['Json','JsonSerializer']) {
  const members=lang.membersOf(cls,true,'');
  for (const name of ['Parse','Deserialize','TryParse','Serialize','Stringify','Escape']) assert(members[name], cls+'.'+name);
  assert.strictEqual(members.Serialize[0].params.length,3);
  assert.strictEqual(members.Serialize[0].params[2].def,'false');
  assert.strictEqual(members.TryParse[0].ret,'bool');
}
for (const name of ['GetJson','PostJson','PutJson','PatchJson']) assert(lang.membersOf('Http',true,'')[name],name);
assert.strictEqual(lang.membersOf('HttpResponse',false,'').Json[0].ret,'object');
const templates = new Function(fs.readFileSync(path.join(root,'templates.js'),'utf8')+';return TEMPLATES;')();
assert(templates.find(t=>t.src.endsWith('/json_roundtrip.cs')).requires.includes('Json.ComputedProperties'));
assert(templates.find(t=>t.src.endsWith('/http_json.cs')).requires.includes('Http.Json'));
console.log('PASS Studio JSON aliases, signatures, HTTP helpers and template capabilities');
