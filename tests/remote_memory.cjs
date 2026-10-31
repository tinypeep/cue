// Browser lifecycle stress test; Playwright + Chromium required. Mock player, real DOM/fetch/audio.
const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
process.chdir(path.join(__dirname,'..'));
const strings=JSON.parse(fs.readFileSync('i18n/pt-BR.json','utf8'));
const html=fs.readFileSync('assets/remote/index.html','utf8').replace('/*CUE_BOOT*/','window.CUE='+JSON.stringify({lang:'pt-BR',strings})+';');
const bmp=Buffer.alloc(54+84*48*3);bmp.write('BM');bmp.writeUInt32LE(bmp.length,2);bmp.writeUInt32LE(54,10);bmp.writeUInt32LE(40,14);bmp.writeInt32LE(84,18);bmp.writeInt32LE(48,22);bmp.writeUInt16LE(1,26);bmp.writeUInt16LE(24,28);bmp.fill(96,54);
let request=0;
const server=http.createServer((req,res)=>{
  if(req.url==='/font.ttf'){res.end(fs.readFileSync('assets/fonts/CueOmni.ttf'));return;}
  if(req.url.startsWith('/list')){
    const n=++request;res.setHeader('Content-Type','application/json');
    const items=Array.from({length:24},(_,i)=>({name:`Movie ${n}-${i}.mp4`,path:`C:\\Videos\\${n}-${i}.mp4`}));
    res.end(JSON.stringify({path:'',parent:'',title:'Videos',items}));return;
  }
  if(req.url.startsWith('/thumb')){res.setHeader('Content-Type','image/bmp');setTimeout(()=>res.end(bmp),10);return;}
  res.setHeader('Content-Type','text/html');res.end(html);
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
 const browser=await chromium.launch({headless:true,executablePath:process.env.CUE_CHROMIUM_PATH||undefined,args:['--no-sandbox','--disable-dev-shm-usage']});
 const page=await browser.newPage({viewport:{width:390,height:844},isMobile:true,hasTouch:true});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{
  localStorage.setItem('cue.token.v2','a'.repeat(64));window.__liveURLs=new Set();window.__audioEdges=0;
  const create=URL.createObjectURL.bind(URL),revoke=URL.revokeObjectURL.bind(URL);
  URL.createObjectURL=blob=>{const u=create(blob);window.__liveURLs.add(u);return u;};
  URL.revokeObjectURL=url=>{window.__liveURLs.delete(url);return revoke(url);};
  const C=window.AudioContext||window.webkitAudioContext;
  if(C){window.AudioContext=class extends C {
   mark(node){let edges=0;const c=node.connect.bind(node),d=node.disconnect.bind(node);node.connect=(...args)=>{edges++;window.__audioEdges++;return c(...args);};node.disconnect=(...args)=>{window.__audioEdges-=edges;edges=0;return d(...args);};return node;}
   createOscillator(){return this.mark(super.createOscillator());}
   createGain(){return this.mark(super.createGain());}
   createBiquadFilter(){return this.mark(super.createBiquadFilter());}
  };}
  window.WebSocket=class {
   constructor(){this.readyState=0;setTimeout(()=>{this.readyState=1;this.onopen?.();},5);}
   send(data){if(JSON.parse(data).t==='auth')setTimeout(()=>this.onmessage?.({data:JSON.stringify({t:'hello',pc:'test'})}),1);}
   close(){this.readyState=3;}
  };
 });
 await page.goto(`http://127.0.0.1:${port}`);await page.waitForTimeout(100);
 const cdp=await page.context().newCDPSession(page);
 async function cycle(){
  await page.locator('#bFolder').click();await page.waitForFunction(()=>document.querySelector('#grid img.ok'));
  await page.locator('#bFolder').click();
  assert.strictEqual(await page.locator('#grid img').count(),0,'closed browser retained images');
  assert.strictEqual(await page.evaluate(()=>window.__liveURLs.size),0,'closed browser retained blob URLs');
 }
 async function heap(){await page.waitForTimeout(800);await cdp.send('HeapProfiler.collectGarbage');return (await cdp.send('Runtime.getHeapUsage')).usedSize;}
 for(let i=0;i<10;i++)await cycle();const before=await heap();
 for(let i=0;i<100;i++)await cycle();const after=await heap();
 for(let i=0;i<20;i++){await page.locator('#bFolder').click();await page.locator('#bFolder').click();}
 await page.waitForTimeout(800);await cdp.send('HeapProfiler.collectGarbage');
 const final=await page.evaluate(()=>({urls:window.__liveURLs.size,images:document.querySelectorAll('#grid img').length,edges:window.__audioEdges}));
 assert.deepStrictEqual(final,{urls:0,images:0,edges:0});
 assert(after-before<3*1024*1024,`unexpected retained JS heap growth: ${after-before}`);
 assert.deepStrictEqual(errors,[]);
 console.log(JSON.stringify({warmup:10,cycles:100,rapidCloseCycles:20,beforeBytes:before,afterBytes:after,deltaBytes:after-before,final}));
 await browser.close();await new Promise(r=>server.close(r));
})().catch(e=>{console.error(e);server.close();process.exit(1)});
