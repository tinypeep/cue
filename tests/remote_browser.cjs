// Optional browser regression test. Requires playwright and its Chromium browser. Run from the source root.
const {chromium} = require('playwright');
const fs = require('fs');
const http = require('http');
const assert = require('assert');
const path = require('path');
process.chdir(path.join(__dirname,'..'));
fs.mkdirSync('build/remote-browser',{recursive:true});
const html = fs.readFileSync('assets/remote/index.html','utf8');
const catalog = JSON.parse(fs.readFileSync('i18n/pt-BR.json','utf8'));
const pageHTML=html.replace('<script>','<script nonce="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa">').replace('/*CUE_BOOT*/','window.CUE='+JSON.stringify({lang:'pt-BR',strings:catalog})+';');
const server=http.createServer((req,res)=> {
 if(req.url==='/font.ttf'){res.setHeader('Content-Type','font/ttf');res.end(fs.readFileSync('assets/fonts/CueOmni.ttf'));return;}
 if(req.url.startsWith('/list')){assert(req.headers.authorization==='Bearer '+ 'a'.repeat(64));res.setHeader('Content-Type','application/json');res.end(JSON.stringify({path:'',parent:'',title:'Pastas',items:[{dir:true,name:'Vídeos',path:'C:\\Videos'},{dir:true,name:'Downloads',path:'C:\\Downloads'}]}));return;}
 res.setHeader('Content-Security-Policy',"default-src 'none'; script-src 'nonce-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'; style-src 'self' 'unsafe-inline'; img-src 'self' blob:; font-src 'self'; connect-src 'self' ws://127.0.0.1:8123; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");res.setHeader('Content-Type','text/html');res.end(pageHTML);
});
(async()=>{
 await new Promise(r=>server.listen(8123,'127.0.0.1',r));
 const browser=await chromium.launch({executablePath:process.env.CUE_CHROMIUM_PATH || undefined,headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
 const errors=[];
 for(const [width,height] of [[320,568],[375,667],[390,744],[390,844],[430,932],[844,390]]){
  const context=await browser.newContext({viewport:{width,height},isMobile:true,hasTouch:true,deviceScaleFactor:1});
  const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
   localStorage.setItem('cue.token.v2','a'.repeat(64));
   window.WebSocket=class {
    constructor(url){this.readyState=0;setTimeout(()=>{this.readyState=1;this.onopen?.();},5);}
    send(data){const m=JSON.parse(data);if(m.t==='auth'){window.__ws=this;setTimeout(()=>{this.onmessage?.({data:JSON.stringify({t:'hello',pc:'Cue PC'})});this.onmessage?.({data:JSON.stringify({t:'state',view:'home',file:'',pos:0,dur:0,speed:1,vol:100,paused:true,ended:false,audioOnly:false,sub:{has:false,on:false,delay:0,size:44,sizeMin:28,sizeMax:72,pos:93,posDefault:93,posMin:12,posMax:97},audio:{n:0,i:0},timer:{m:0,left:0},next:'',color:'#a0508f'})});},5);}}
    close(){this.readyState=3;}
   };
  });
  await page.goto('http://127.0.0.1:8123');await page.waitForTimeout(250);
  const bounds=await page.evaluate(()=>{
   const box=id=>{const r=document.getElementById(id).getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height};};
   const icon=document.getElementById('cIcon');const b=icon.getBBox();
   return {app:box('app'),folder:box('bFolder'),wheel:box('wheel'),back:box('bBack'),center:box('bCenter'),icon:{x:b.x,y:b.y,w:b.width,h:b.height},scrollW:document.documentElement.scrollWidth};
  });
  assert(bounds.app.x>=-1 && bounds.app.x+bounds.app.w<=width+1,JSON.stringify(bounds));
  assert(bounds.scrollW<=width+1, 'horizontal overflow');
  assert(bounds.icon.w===9 && bounds.icon.h===7,'folder sprite truncated');
  assert(bounds.back.h>=43.9 && bounds.back.w>=43.9,'tiny wheel target');
  await page.screenshot({path:`build/remote-browser/mobile-${width}x${height}.png`,fullPage:true});
  await page.locator('#bFolder').click();await page.waitForTimeout(100);
  assert(await page.locator('#grid .tile').count()===2);
  await page.screenshot({path:`build/remote-browser/folders-${width}x${height}.png`,fullPage:true});
  await page.locator('#bFolder').click();
  await page.evaluate(()=>window.__ws.onmessage({data:JSON.stringify({t:'state',view:'play',file:'A long movie title that should truncate cleanly.mkv',pos:120,dur:3600,speed:1,vol:100,paused:false,ended:false,audioOnly:false,sub:{has:true,on:true,delay:0,size:44,sizeMin:28,sizeMax:72,pos:93,posDefault:93,posMin:12,posMax:97},audio:{n:2,i:0,m:0},timer:{m:0,left:0},next:'',color:'#a0508f'})}));
  await page.waitForTimeout(100);
  await page.screenshot({path:`build/remote-browser/playing-${width}x${height}.png`,fullPage:true});
  const gesture=await page.evaluate(()=>{
    const bar=document.getElementById('bar'),r=bar.getBoundingClientRect();
    const event=(type,f)=>bar.dispatchEvent(new PointerEvent(type,{pointerId:77,bubbles:true,clientX:r.left+r.width*f,clientY:r.top+r.height/2}));
    event('pointerdown',.2);event('pointermove',.8);
    const fill=parseFloat(document.getElementById('barFill').style.width),knob=parseFloat(document.getElementById('knob').style.left);
    event('pointerup',.8);return {fill,knob};
  });
  assert(Math.abs(gesture.fill-80)<.1 && Math.abs(gesture.knob-80)<.1,JSON.stringify(gesture));
  const circular=await page.evaluate(()=>{
    const el=document.getElementById('wheel'),r=el.getBoundingClientRect();
    function at(type,angle) {const a=angle*Math.PI/180;el.dispatchEvent(new PointerEvent(type,{pointerId:88,bubbles:true,clientX:r.left+r.width/2+Math.sin(a)*r.width*164/344,clientY:r.top+r.height/2-Math.cos(a)*r.width*164/344}));}
    at('pointerdown',288);at('pointermove',324);
    const value=parseFloat(document.getElementById('knob').style.left);at('pointerup',324);return value;
  });
  assert(Math.abs(circular-90)<1,'active wheel dot must seek continuously: '+circular);


  await page.locator('#bMenu').click();await page.waitForTimeout(100);
  const menu=await page.locator('#menu').boundingBox();
  const menuEnd=await page.locator('#rows').boundingBox();
  assert(menuEnd.y+menuEnd.height <= bounds.wheel.y, 'menu overlaps controls');
  await page.screenshot({path:`build/remote-browser/menu-${width}x${height}.png`,fullPage:true});
  await page.locator('#rows [data-row="0"]').click();await page.waitForTimeout(100);
  const subRows=await page.locator('#rows').boundingBox();
  assert(subRows.y+subRows.height <= bounds.wheel.y,'subtitle menu overlaps controls');
  await page.screenshot({path:`build/remote-browser/subtitles-${width}x${height}.png`,fullPage:true});
  const nextLabels=await page.evaluate(()=>{
    const state={t:'state',view:'home',file:'',pos:0,dur:3600,speed:1,vol:100,paused:true,ended:false,audioOnly:false,sub:{has:false,on:false,delay:0,size:44,sizeMin:28,sizeMax:72,pos:93,posDefault:93,posMin:12,posMax:97},audio:{n:0,i:0},timer:{m:0,left:0},next:'Other Show.S01E01.mkv',nextAuto:false,color:'#a0508f'};
    const send=()=>window.__ws.onmessage({data:JSON.stringify(state)});
    send();state.view='play';state.file='Current.S01E09.mkv';state.pos=3595;send();
    const manual=document.getElementById('nextTop').textContent;
    const hint=document.getElementById('nextBottom').textContent;
    state.nextAuto=true;send();
    return {manual,hint,episode:document.getElementById('nextTop').textContent};
  });
  assert(nextLabels.manual.startsWith('a seguir') && nextLabels.episode.startsWith('próximo'),JSON.stringify(nextLabels));
  assert(nextLabels.hint===catalog['remote.tap_continue'],JSON.stringify(nextLabels));
  await page.evaluate(()=>{localStorage.clear();window.WebSocket=undefined;});
  await page.reload();await page.waitForTimeout(150);
  // Remove the remembered credential and trigger the real invalid-token close route.
  await page.evaluate(()=>{localStorage.clear();window.__ws.onclose({code:4001});});
  assert(await page.locator('#digits .digit').count()===6);
  const digits=await page.locator('#digits').boundingBox();
  assert(digits.x>=0 && digits.x+digits.width<=width+1);
  await page.screenshot({path:`build/remote-browser/pairing-${width}x${height}.png`,fullPage:true});
  console.log('passed',width,height,JSON.stringify(bounds));await context.close();
 }
 assert(errors.length===0,errors.join('\n'));await browser.close();server.close();
})().catch(e=>{console.error(e);process.exit(1)});
