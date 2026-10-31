// Optional browser regression test: the back arrow beside menu titles, and quick taps that must stay taps
// (no double-tap zoom). Requires playwright and Chromium. Run from the source root.
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
assert(/<meta name="viewport"[^>]*maximum-scale=1[^>]*user-scalable=no/.test(html),'the page does not zoom');
const server=http.createServer((req,res)=> {
  if(req.url==='/font.ttf'){res.setHeader('Content-Type','font/ttf');res.end(fs.readFileSync('assets/fonts/CueOmni.ttf'));return;}
  if(req.url.startsWith('/thumb')||req.url.startsWith('/list')){res.statusCode=404;res.end();return;}
  res.setHeader('Content-Type','text/html');res.end(pageHTML);
});
(async()=>{
  await new Promise(r=>server.listen(8125,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
  const context=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
  const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
    localStorage.setItem('cue.token.v2','a'.repeat(64));
    window.__sent=[];
    window.WebSocket=class {
      constructor(){this.readyState=0;setTimeout(()=>{this.readyState=1;this.onopen?.();},5);}
      send(data){const m=JSON.parse(data);window.__sent.push(m);if(m.t==='auth'){window.__ws=this;setTimeout(()=>{this.onmessage?.({data:JSON.stringify({t:'hello',pc:'Cue PC'})});this.onmessage?.({data:JSON.stringify({t:'state',view:'play',file:'Twin Peaks - S01E06.mkv',pos:120,dur:3600,speed:1,vol:100,paused:false,ended:false,audioOnly:false,sub:{has:true,on:true,delay:0,size:44,sizeMin:28,sizeMax:72,pos:93,posDefault:93,posMin:12,posMax:97},audio:{n:1,i:0},timer:{m:0,left:0},next:'',color:'#a0508f'})});},5);}}
      close(){this.readyState=3;}
    };
  });
  await page.goto('http://127.0.0.1:8125');await page.waitForTimeout(250);
  const st=()=>page.evaluate(()=>({menu:!document.getElementById('menu').classList.contains('hide'),
    title:document.getElementById('menuTitleText').textContent,
    sub:!document.getElementById('subEdit').classList.contains('hide'),subTitle:document.getElementById('subEditTitle').textContent}));
  await page.locator('#bMenu').click();await page.waitForTimeout(150);
  let s=await st(); assert(s.menu && s.title==='menu',JSON.stringify(s));
  await page.locator('#rows button').first().click();await page.waitForTimeout(150);   // legendas ▸
  s=await st(); assert(s.title==='legendas',JSON.stringify(s));
  await page.locator('#rows button').nth(2).click();await page.waitForTimeout(150);    // tamanho
  s=await st(); assert(s.sub && s.subTitle==='tamanho da legenda',JSON.stringify(s));
  await page.screenshot({path:'build/remote-browser/menu-size-back.png'});
  // two quick taps on + are two steps, not a zoom and not one lost tap
  const before=await page.evaluate(()=>window.__sent.filter(m=>m.c==='subsize').length);
  await page.locator('#subPlus').tap();await page.waitForTimeout(60);await page.locator('#subPlus').tap();await page.waitForTimeout(150);
  const after=await page.evaluate(()=>window.__sent.filter(m=>m.c==='subsize').length);
  assert.equal(after-before,2,'quick taps on + must both count');
  assert.equal(await page.evaluate(()=>window.visualViewport.scale),1);
  await page.locator('#subEditBack').click();await page.waitForTimeout(150);
  s=await st(); assert(!s.sub && s.menu && s.title==='legendas','arrow: value back to the subtitles list '+JSON.stringify(s));
  await page.screenshot({path:'build/remote-browser/menu-subs-back.png'});
  await page.locator('#menuTitle').click();await page.waitForTimeout(150);
  s=await st(); assert(s.menu && s.title==='menu','arrow: subtitles back to the menu '+JSON.stringify(s));
  await page.locator('#menuTitle').click();await page.waitForTimeout(150);
  s=await st(); assert(!s.menu,'arrow: the menu back to the player');
  await browser.close();server.close();
  assert.deepEqual(errors,[]);
  console.log('remote menu: passed');
})().catch(e=>{console.error(e);process.exit(1);});
