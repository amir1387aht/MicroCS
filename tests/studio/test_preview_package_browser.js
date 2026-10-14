// Actual extracted offline Studio package, not the development checkout's assets.
let chromium;try{({chromium}=require('playwright'));}catch{({chromium}=require('playwright-core'));}
const fs=require('fs'),path=require('path'),assert=require('assert');
(async()=>{const root=path.resolve('build/preview-package-smoke');const name=fs.readdirSync(root).find(n=>n.endsWith('-studio'));assert(name,'run extracted candidate smoke first');
const browser=await chromium.launch({executablePath:process.env.CHROME||undefined,args:['--no-sandbox']});try{
 const page=await browser.newPage({viewport:{width:390,height:844}});let errors=[];page.on('pageerror',e=>errors.push(e.message));let network=0;await page.route('https://**/*',r=>{network++;return r.abort();});await page.goto('file://'+path.join(root,name,'index.html'));
 assert(await page.evaluate(()=>typeof SourceText.edits==='function'&&typeof SourceProject.parse==='function'&&TEMPLATES.length===218),'packaged required modules and template registry load');
 await page.setInputFiles('#openProject',{name:'preview.microcsproj',mimeType:'application/json',buffer:Buffer.from(JSON.stringify({format:'microcs-source-project',version:1,name:'preview smoke',entry:'main',files:[{id:'main',name:'main.cs',text:'Console.WriteLine(@"one\r\nβ");\r\n'}]}))});
 await page.waitForSelector('#modalBg.show');await page.click('#dlgOk');await page.waitForFunction(()=>App.project&&!App.projectImportBusy);
 assert(await page.evaluate(()=>App.projectDocuments()[0].text==='Console.WriteLine(@"one\r\nβ");\r\n'&&document.getElementById('btnExplorer').getAttribute('aria-expanded')==='false'),'actual imported payload/compact layout in extracted package');
 await page.click('#btnMore');await page.getByText('Download source project…',{exact:true}).click();const job=page.waitForEvent('download');await page.click('#dlgOk');const downloaded=await job;const file=path.join(root,'packaged-roundtrip.microcsproj');await downloaded.saveAs(file);
 assert(JSON.parse(fs.readFileSync(file,'utf8')).files[0].text==='Console.WriteLine(@"one\r\nβ");\r\n');assert(errors.length===0,errors.join('; '));assert(network===0,'offline package must not fetch dependencies');
 await page.screenshot({path:path.join(root,'packaged-studio-phone.png')});console.log('PASS extracted offline Studio: required modules/templates, real project import/download, exact CRLF payload, compact layout, no page errors/network requests');
 }finally{await browser.close();}})().then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1);});
