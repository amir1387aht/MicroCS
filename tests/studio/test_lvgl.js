// LVGL IntelliSense (tools/studio/lvgl.js + lvgl_api.js): offline, no browser. Needs tools/studio/lvgl_api.js
// (python3 tools/lvgl_gen/studio_data.py after `make lvgl-api`).
const fs = require('fs'), vm = require('vm'), assert = require('assert'), path = require('path');
const root = path.join(__dirname, '../../tools/studio');
const context = { window: {}, console };
// a document whose script tags run the file at once, like the browser does for lvgl_api.js
context.document = { head: { appendChild(s) { vm.runInContext(fs.readFileSync(path.join(root, s.src), 'utf8'), context); s.onload(); } },
                     createElement() { return {}; } };
vm.createContext(context);
for (const f of ['api.js', 'docs.js', 'format.js', 'lang.js', 'lvgl.js']) vm.runInContext(fs.readFileSync(path.join(root, f), 'utf8'), context);
const S = context.window.StudioLvgl, L = context.window.Lang;
assert(!S.ready(), 'the 350 KB table is not loaded before it is needed');
assert(S.mentions('var o = lv_obj_create(null);') && S.mentions('x = LV_ALIGN_CENTER;') && !S.mentions('Console.WriteLine("hi");'));
let loaded = 0; S.load(() => loaded++);
assert(S.ready() && loaded === 1);

const f = S.fn('lv_obj_set_size')[0];
assert.strictEqual(f.ret, 'void'); assert.strictEqual(f.params.map(p => p.name).join(), 'obj,w,h'); assert.strictEqual(f.params[0].type, 'lv_obj_t');
assert.strictEqual(S.fn('lv_label_set_text')[0].params[1].type, 'string');
assert.strictEqual(S.fn('lv_obj_create')[0].ret, 'lv_obj_t');
// v8 names and v9 names are both known, as are the hand-written callback wrappers and macro functions
for (const n of ['lv_scr_load', 'lv_screen_load', 'lv_btn_create', 'lv_button_create', 'lv_obj_add_event_cb', 'lv_timer_create', 'lv_anim_set_custom_exec_cb', 'LV_PCT', 'LV_GRID_FR', 'lvgl_host_run'])
  assert(S.fn(n), n);
assert.strictEqual(S.fn('lv_obj_add_event_cb')[0].params[1].type, 'Action<lv_event_t>');
assert.strictEqual(S.fn('no_such_function'), null);
assert.strictEqual(S.kindOf('LV_ALIGN_CENTER'), 'int constant');
assert.strictEqual(S.kindOf('LV_SYMBOL_OK'), 'string constant');
assert(/font/.test(S.kindOf('lv_font_montserrat_14')));
// completion items
const it = S.items('lv_obj_set_s');
assert(it.length && it[0].label.startsWith('lv_obj_set_s') && it.some(x => x.label === 'lv_obj_set_size' && x.mems));
assert(S.items('LV_ALIGN_').some(x => x.label === 'LV_ALIGN_CENTER' && x.kind === 'f'));
assert(S.items('lv_st', { afterNew: true }).some(x => x.label === 'lv_style_t'));
assert.strictEqual(S.items('l').length, 0, 'one letter does not flood the list');
// signature help through Lang.callContext / signaturesFor
const src = 'var o = lv_obj_create(null);\nlv_obj_set_size(o, 10, ';
const ctx = L.callContext(src, src.length);
assert.strictEqual(ctx.name, 'lv_obj_set_size'); assert.strictEqual(ctx.arg, 2);
const sigs = L.signaturesFor(ctx, src);
assert(sigs.length === 1 && sigs[0].params.length === 3);
console.log('PASS LVGL IntelliSense: lazy load, completion, constants, signature help (%d functions)', Object.keys(context.window.MCS_LVGL.f).length);
