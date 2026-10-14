// Independent real-key/raw-source contract. No server/device or source execution.
const assert=require('assert'),path=require('path');
let chromium;try{({chromium}=require('playwright'))}catch{({chromium}=require('playwright-core'))}
(async()=>{const b=await chromium.launch({executablePath:process.env.CHROME,args:['--no-sandbox']});try{
 const p=await b.newPage(),errors=[];p.on('pageerror',e=>errors.push(e.message));await p.route(/^https?:/,r=>r.abort());await p.goto('file://'+path.resolve('tools/studio/index.html'));
 const prefix='// preserved CRLF\r\n// preserved CR\r';
 await p.evaluate(text=>App.openTab({name:'review-fields.cs',text,saved:text,local:true}),prefix);await p.evaluate(()=>new Promise(requestAnimationFrame));await p.focus('#ta');
 await p.evaluate(()=>{const n=ed.ta.value.length;ed.ta.setSelectionRange(n,n);ed.items=[{kind:'s',label:'review',text:'${3:🚀}${1:a}${2:bb}$0'}];ed.sel=0;ed.ctx={start:n,end:n};ed.box.style.display='block';});
 await p.keyboard.press('Tab');assert.equal(await p.evaluate(()=>ed.ta.value.slice(ed.ta.selectionStart,ed.ta.selectionEnd)),'a');
 await p.keyboard.type('X');await p.keyboard.press('Tab');assert.equal(await p.evaluate(()=>ed.ta.value.slice(ed.ta.selectionStart,ed.ta.selectionEnd)),'bb');
 await p.keyboard.type('YZ');await p.keyboard.press('Shift+Tab');assert.equal(await p.evaluate(()=>ed.ta.value.slice(ed.ta.selectionStart,ed.ta.selectionEnd)),'X');
 await p.keyboard.press('Tab');await p.keyboard.press('Tab');assert.equal(await p.evaluate(()=>ed.ta.value.slice(ed.ta.selectionStart,ed.ta.selectionEnd)),'🚀');await p.keyboard.type('Q');await p.keyboard.press('Tab');
 assert.equal(await p.evaluate(()=>App.tab().text),prefix+'QXYZ');assert.equal(await p.evaluate(()=>!!ed.snippet),false);
 // Stale ordinary popup must not delete prefix-to-unrelated-caret source.
 await p.evaluate(()=>{ed.items=[{kind:'k',label:'return'}];ed.sel=0;ed.ctx={start:0,end:2};ed.box.style.display='block';ed.ta.setSelectionRange(ed.ta.value.length,ed.ta.value.length);});
 const before=await p.evaluate(()=>({raw:ed.tab.text,sel:[ed.ta.selectionStart,ed.ta.selectionEnd],dirty:ed.tab.dirty,error:ed.errLine,history:ed.sourceHistory.length}));await p.keyboard.press('Tab');
 const after=await p.evaluate(()=>({raw:ed.tab.text,sel:[ed.ta.selectionStart,ed.ta.selectionEnd],dirty:ed.tab.dirty,error:ed.errLine,history:ed.sourceHistory.length}));assert.deepStrictEqual(after,before);
 assert.deepStrictEqual(errors,[]);console.log('PASS independent numbered visitation/reverse navigation/UTF16/textual offsets/raw CRLF+CR/final exit/stale ordinary popup preservation');
 await p.close();
}finally{await b.close()}})().catch(e=>{console.error(e);process.exitCode=1});
