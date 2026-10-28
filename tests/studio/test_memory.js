// Production parser + workflow finalization: legacy reports must not invent totals.
const fs=require('fs'),path=require('path'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../../tools/studio/studio.js'),'utf8');
function methods(start,end,deps={}) {
 const a=source.indexOf('  '+start),b=source.indexOf('  '+end,a);assert(a>0&&b>a);
 return new Function(...Object.keys(deps),'return ({'+source.slice(a,b)+'});')(...Object.values(deps));
}
(async()=>{
 const node={textContent:'old stale value',title:'',tabIndex:-1,classList:{toggle(){},remove(){}}};let report={status:'OK',out:new TextEncoder().encode('heap 16384 bytes in use, peak 54272, 50 objects, 3 collections\nvm_limit 38912\nstack 0 used, 64 allocated, 43 peak, 1024 limit\nframes 0 used, 8 allocated, 6 peak, 64 limit\npool 40960 total, 18000 used, 39000 peak, 22960 free, 8192 largest\ndevice_heap 200000 total, 150000 used, 50000 free, 40000 minimum_free, 30000 largest\n')};
 const dev={connected:true,running:false,op:fn=>fn(),cmd:async()=>report};
 const fmtBytes=n=>{ if (n == null || isNaN(n)) return '—'; if (n < 1024) return n + ' B'; if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB'; if (n < 1024 * 1048576) return (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + ' MB'; return (n / 1073741824).toFixed(1) + ' GB'; };
 const app=methods('memoryText(text)','renderFiles()',{$:()=>node,dev,TextDecoder,fmtBytes});
 await app.refreshMemory();
 assert.strictEqual(node.textContent,'Device heap: 146.5 KiB / 195.3 KiB');
 assert(!node.textContent.includes('Memory snapshot'));
 assert(node.title.includes('Click to expand.'));
 app.toggleMemoryInfo();
 assert(node.textContent.includes('VM pool: 18,000 B / 40,960 B'));
 assert(node.textContent.includes('Pool peak: 39,000 B'));
 assert(node.textContent.includes('largest: 8,192 B'));
 assert(node.textContent.includes('VM tracked: 16,384 B · peak: 54,272 B'));
 assert(node.textContent.includes('Device heap: 150,000 B / 200,000 B'));
 assert(node.textContent.includes('not additive'));
 assert(node.textContent.includes('Stack slots: 0 used · 64 allocated · peak 43 · max 1024'));
 assert(node.textContent.includes('Call frames: 0 used · 8 allocated · peak 6 · max 64'));
 assert(node.title.includes('not static RAM'));
 app.toggleMemoryInfo();
 assert.strictEqual(node.textContent,'Device heap: 146.5 KiB / 195.3 KiB');
 const automatic=app.memoryText('heap 100 bytes in use, peak 200\npool 16000 total, 100 used, 200 peak, 15900 free, 15800 largest\npool_backing 16384 committed, 196608 limit, 1 regions\n');
 assert(automatic.includes('16,384 B committed · ceiling: 196,608 B · regions: 1'));
 const old=app.memoryText('heap 16384 bytes in use, peak 54272');
 assert(old.includes('capacity unavailable'));assert(old.includes('Device heap unavailable'));
 assert.strictEqual(app.memorySummary('heap 16384 bytes in use, peak 54272'),'Device heap unavailable');
 assert(!old.includes('/ 0'));assert(!old.includes('NaN'));
 const current='heap 13576 bytes in use, peak 13880\nvm_limit 96256\npool 32712 total, 15528 used, 16320 peak, 17184 free, 15320 largest\npool_backing 32768 committed, 98304 limit, 2 regions\ndevice_heap 130460 total, 104152 used, 26308 free, 21468 minimum_free, 25088 largest\n';
 assert.strictEqual(app.memorySummary(current),'Device heap: 101.7 KiB / 127.4 KiB');
 // Native growth must change the primary counter even if VM usage is unchanged.
 const nativeGrowth=current.replace('104152 used, 26308 free','124152 used, 6308 free');
 assert.strictEqual(app.memorySummary(nativeGrowth),'Device heap: 121.2 KiB / 127.4 KiB');
 assert(app.memoryText(nativeGrowth).includes('VM pool: 15,528 B / 32,712 B'));
 assert(app.memoryText(current).startsWith('Memory snapshot\nDevice heap: 104,152 B / 130,460 B'));
 assert.strictEqual(app.memorySummary('device_heap 0 total, 0 used, 0 free, 0 minimum_free, 0 largest'),'Device heap: 0.0 KiB / 0.0 KiB');
 assert(app.memoryText(current).includes('VM allocation limit: 96,256 B'));
 assert(app.memoryText(current).includes('Pool peak: 16,320 B'));
 assert.strictEqual(app.memorySummary('device_heap 200000 total, 150000 used, 50000 free, 40000 minimum_free, 30000 largest'),'Device heap: 146.5 KiB / 195.3 KiB');
 assert.strictEqual(app.memorySummary('pool 0 total, 0 used, 0 peak, 0 free, 0 largest'),'Device heap unavailable');
 assert.strictEqual(app.memorySummary('pool 40960 total, 18000 used, 39000 peak, 22960 free, 8192 largest'),'Device heap unavailable');
 assert.strictEqual(app.memorySummary('unrecognized legacy output'),'Memory report unavailable');
 report={status:'ERR unavailable',out:new Uint8Array()};await app.refreshMemory();assert.strictEqual(node.textContent,'Memory report unavailable');
 dev.cmd=async()=>{throw Error('disconnected');};await app.refreshMemory();assert(node.textContent.includes('refresh failed'));
 let refreshes=0;app.refreshMemory=async()=>{refreshes++;};app.updateButtons=()=>{};
 Object.assign(app,methods('async workflow(action)','async checkScripts(paths,',{dev,ed:{sigHide(){},hoverHide(){},hideComplete(){}},toast(){},term:{line(){}}}));
 assert.strictEqual(await app.workflow(async()=>{throw Error('Build failed: out of memory');}),false);
 assert.strictEqual(refreshes,1,'failed check/build must refresh before releasing workflow lock');
 assert.strictEqual(app.workflowBusy,false);
 await app.workflow(async()=>true);assert.strictEqual(refreshes,2);
 console.log('PASS Studio memory: physical totals, exact bytes, legacy/failed telemetry, post-OOM workflow refresh');
})().catch(e=>{console.error(e);process.exitCode=1;});
