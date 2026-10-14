// Independent reviewer: fully typed snippet labels must remain available.
const assert=require('assert'),path=require('path');
const {chromium}=require('playwright');
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROME,args:['--no-sandbox']});try{
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));await page.route(/^https?:/,r=>r.abort());
 await page.goto('file://'+path.resolve('tools/studio/index.html'));
 for(const label of ['for','while']){
  await page.evaluate(()=>App.openTab({name:'review-snippet.cs',text:'',saved:'',local:true}));await page.evaluate(()=>new Promise(requestAnimationFrame));await page.focus('#ta');await page.keyboard.type(label);
  const visible=await page.evaluate(label=>ed.box.style.display==='block'&&ed.items.some(it=>it.label===label&&it.kind==='s'),label);
  console.log(JSON.stringify({label,exactSnippetVisible:visible}));
  if(process.argv.includes('--baseline'))assert.equal(visible,false,'baseline exact-label exclusion characterized');
  else{assert.equal(visible,true,'fully typed snippet offered');await page.keyboard.press('Tab');const raw=await page.evaluate(()=>App.tab().text);assert.notEqual(raw,label,'actual Tab accepts offered snippet');assert(!/\$\{\d|\$0/.test(raw),'no snippet markers emitted');}
 }
 assert.deepEqual(errors,[]);console.log(process.argv.includes('--baseline')?'PASS independent baseline full-label gap characterization':'PASS independent fully typed snippet keyboard regression');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1});
