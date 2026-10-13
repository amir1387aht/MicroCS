// Pure gallery filtering: no DOM or connected board needed.
const fs = require('fs'), assert = require('assert'), path = require('path');
const js = fs.readFileSync(path.join(__dirname, '../../tools/studio/studio.js'), 'utf8');
const a = js.indexOf('function parseCapabilities('), b = js.indexOf('const GH_TEMPLATES', a);
assert(a > 0 && b > a);
const { parseCapabilities, templateCompatible, legacyCapabilities } = new Function(js.slice(a,b) + '; return { parseCapabilities, templateCompatible, legacyCapabilities };')();
const caps = parseCapabilities('capabilities=1\r\nCompiler=1\r\nWifi=1\r\nBle=0\r\nGPIO=1\r\nWifi.AP=0\r\n');
assert(caps instanceof Map); assert(caps.get('Wifi')); assert.strictEqual(caps.get('Ble'), false);
for(const bad of ['', 'capabilities=2\nWifi=1', 'capabilities=1\nWifi=true', 'capabilities=1\nWifi=1\nWifi=0', 'capabilities=1']) assert.strictEqual(parseCapabilities(bad),null);
assert(templateCompatible({requires:['Compiler','Wifi']},caps,true));
assert(!templateCompatible({requires:['Wifi','Ble']},caps,true));
assert(!templateCompatible({requires:['Wifi','Wifi.AP']},caps,true));
assert(!templateCompatible({requires:['I2C']},caps,true));
assert(templateCompatible({requires:['Ble']},caps,false));
const old = legacyCapabilities('MicroCS 1.14.0 features=0xffff value=16 bytes');
assert(templateCompatible({requires:['Compiler']},old,true));
assert(!templateCompatible({requires:['GPIO']},old,true));
assert(!templateCompatible({requires:['Ble']},old,true));
const TEMPLATES = new Function(fs.readFileSync(path.join(__dirname,'../../tools/studio/templates.js'),'utf8') + ';return TEMPLATES;')();
assert(TEMPLATES.every(t => Array.isArray(t.requires)));
assert(TEMPLATES.filter(t=>t.src.startsWith('31-')).every(t=>t.requires.includes('Ble')));
assert(TEMPLATES.filter(t=>t.src.startsWith('30-')).every(t=>t.requires.includes('EspNow')));
assert(TEMPLATES.find(t=>t.src.endsWith('/wifi_ap.cs')).requires.includes('Wifi.AP'));
assert(!TEMPLATES.find(t=>t.src.endsWith('/tcp_echo.cs')).requires.includes('Wifi'));
assert(TEMPLATES.find(t=>t.src.endsWith('/https_download.cs')).requires.includes('Tls'));
console.log('PASS gallery capabilities: strict reports, offline/legacy behavior, multi-requirement filtering and explicit metadata');

// Firmware vocabulary must fit existing capability protocol v1 clients.
const manifest=fs.readFileSync(path.join(__dirname,'../../include/mcs_capabilities.def'),'utf8');
const names=[...manifest.matchAll(/^MCS_CAP\("([^"\n]+)"/gm)].map(m=>m[1]);
assert(names.length>80 && names.every(n=>/^[A-Za-z][A-Za-z0-9.]*$/.test(n)));
const full=parseCapabilities('capabilities=1\n'+names.map(n=>n+'=0').join('\n')+'\n');
assert(full && full.size===names.length,'every native capability must preserve protocol parsing');
assert(full.has('TFT') && !full.has('TFT_eSPI'));
console.log('PASS entire native vocabulary fits existing protocol v1 grammar');

// Earlier sprite-only firmware must not offer packed-format APIs it lacks.
const packed=TEMPLATES.find(t=>t.src.endsWith('/tft_packed.cs'));
const earlier=parseCapabilities('capabilities=1\nCompiler=1\nTFT=1\nTFT.Sprite=1\n');
assert(packed && !templateCompatible(packed,earlier,true));
const packedCaps=parseCapabilities('capabilities=1\nCompiler=1\nTFT=1\nTFT.Sprite=1\nTFT.Palette=1\nTFT.Bitmap=1\n');
assert(templateCompatible(packed,packedCaps,true));
assert(templateCompatible(packed,earlier,false),'offline browse keeps all examples');
assert(!templateCompatible(packed,new Map([...packedCaps].map(([n,v])=>[n,n==='TFT.Bitmap'?false:v])),true));
console.log('PASS packed sprite templates require new palette/bitmap APIs, not earlier sprite-only firmware');

const image=TEMPLATES.find(t=>t.src.endsWith('/tft_image.cs'));
assert(image && !templateCompatible(image,packedCaps,true));
assert(templateCompatible(image,new Map([...packedCaps,['TFT.Image',true]]),true));
assert(!templateCompatible(image,new Map([...packedCaps,['TFT.Image',false]]),true));
assert(templateCompatible(image,earlier,false));
console.log('PASS image template requires actual new TFT.Image API, preserving older firmware filtering');

const copy=TEMPLATES.find(t=>t.src.endsWith('/tft_copy.cs'));
const imageCaps=new Map([...packedCaps,['TFT.Image',true]]);
assert(copy && !templateCompatible(copy,imageCaps,true));
assert(templateCompatible(copy,new Map([...imageCaps,['TFT.SpriteCopy',true]]),true));
assert(!templateCompatible(copy,new Map([...imageCaps,['TFT.SpriteCopy',false]]),true));
assert(templateCompatible(copy,earlier,false));
console.log('PASS copy template requires new TFT.SpriteCopy API rather than class/image presence');

const fonts=TEMPLATES.find(t=>t.src.endsWith('/lvgl_fonts.cs'));
const priorGui=parseCapabilities('capabilities=1\nCompiler=1\nLVGL=1\n');
assert(fonts && !templateCompatible(fonts,priorGui,true));
assert(templateCompatible(fonts,new Map([...priorGui,['LVGL.Font',true]]),true));
assert(!templateCompatible(fonts,new Map([...priorGui,['LVGL.Font',false]]),true));
assert(templateCompatible(fonts,priorGui,false));
console.log('PASS font template requires actual new LVGL.Font API, preserving old firmware/offline behavior');
