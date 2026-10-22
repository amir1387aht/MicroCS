// Actual Editor.open: delayed tab restoration may not steal a newer focus.
const fs = require('fs'), path = require('path'), assert = require('assert');
const source = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const a = source.indexOf('  open(tab) {'), b = source.indexOf('  stash() {', a);
assert(a > 0 && b > a);
function setup() {
  const frames = [], document = { activeElement: null }, selection = [];
  const ta = { value: '', focus() { document.activeElement = this; },
    setSelectionRange(...args) { selection.push(args); } };
  const open = new Function('requestAnimationFrame', 'document', 'return ({' + source.slice(a,b) + '}).open;')(
    callback => frames.push(callback), document);
  const editor = { ta, open, tab: null, stash() {}, hideComplete() {}, sigHide() {}, hoverHide() {},
    setError() {}, refresh() {}, syncScroll() {}, cursorMoved() {} };
  return { editor, document, selection, frames, console: {} };
}
let t = setup(), tab = { text: 'one', sel: [2,2], scroll: [10,20] };
t.editor.open(tab);
t.frames.shift()(); assert.deepStrictEqual(t.selection, [[2,2]]);
assert.strictEqual(t.document.activeElement, t.editor.ta);
assert.strictEqual(t.editor.ta.scrollTop, 10);
t = setup(); t.editor.open(tab); t.document.activeElement = t.console;
t.frames.shift()();
assert.strictEqual(t.document.activeElement, t.console, 'delayed tab frame must not steal console focus');
assert.deepStrictEqual(t.selection, [], 'do not restore a caret after focus moved away');
t = setup(); const other = { text: 'two', sel: [1,1] };
t.editor.open(tab); t.editor.open(other);
t.frames.shift()(); assert.deepStrictEqual(t.selection, [], 'ignore obsolete tab restoration');
t.frames.shift()(); assert.deepStrictEqual(t.selection, [[1,1]]);
t = setup(); t.editor.open(tab); t.editor.tab = null; t.document.activeElement = t.console;
t.frames.shift()(); assert.strictEqual(t.document.activeElement, t.console);
t = setup(); t.editor.open(tab);
assert.strictEqual(t.document.activeElement, t.editor.ta, 'tab activation focuses synchronously');
console.log('PASS editor focus: synchronous activation, console handoff, stale tab and close callbacks');
