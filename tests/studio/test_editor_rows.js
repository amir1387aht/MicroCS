// Actual rendered gutter/code baselines, including error rows and deep scrolling.
const {chromium}=require('playwright'),assert=require('assert'),path=require('path');
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});
try{const page=await browser.newPage({viewport:{width:1200,height:800}});
await page.goto('file://'+path.resolve('tools/studio/index.html'));
for(const theme of ['dark','light'])for(const size of [11,13,17,22]){
const rows=await page.evaluate(({theme,size})=>{
 document.documentElement.classList.toggle('light',theme==='light');
 document.documentElement.style.setProperty('--fs',size+'px');
 document.documentElement.style.setProperty('--lh',Math.round(size*1.45)+'px');
 App.openTab({path:'/rows.cs',text:Array.from({length:600},(_,i)=>'// row '+(i+1)).join('\n'),saved:''});
 ed.measure();ed.refresh(true);ed.setError(244);
 const row=243;ed.ta.scrollTop=row*ed.lh-90;ed.syncScroll();
 const glyph=e=>{let range=document.createRange();range.selectNodeContents(e);return range.getBoundingClientRect().top;};
 const numbers=ed.gutter.children;
 return [0,242,243,244,599].map(i=>({row:i,height:numbers[i].getBoundingClientRect().height,expected:ed.lh,
  top:numbers[i].getBoundingClientRect().top-ed.gutter.getBoundingClientRect().top,
  baselineDelta:glyph(numbers[i])-glyph(ed.hl.querySelectorAll('.t-com')[i])}));
},{theme,size});
for(const r of rows){assert(Math.abs(r.height-r.expected)<0.1,JSON.stringify({theme,size,r}));assert(Math.abs(r.top-(6+r.row*r.expected))<0.1);assert(Math.abs(r.baselineDelta)<1,JSON.stringify({theme,size,r}));}
}
console.log('PASS editor rows: 600 lines, deep scroll, bold error row, four sizes, both themes');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
