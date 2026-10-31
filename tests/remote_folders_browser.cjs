// Optional browser regression test for the phone's folder browser. Requires playwright and Chromium.
// "Back" goes one folder up and only shows where there is a folder above; the × always leaves.
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
let roots;
const listings = {
  'C:\\Videos': {title:'Vídeos',parent:'',items:[{dir:true,name:'Séries',path:'C:\\Videos\\Series'},{dir:true,name:'Filmes',path:'C:\\Videos\\Filmes'}]},
  'C:\\Videos\\Series': {title:'Vídeos ▸ Séries',parent:'C:\\Videos',items:[{dir:true,name:'Twin Peaks',path:'C:\\Videos\\Series\\Twin Peaks'}]},
  'C:\\Videos\\Series\\Twin Peaks': {title:'Vídeos ▸ Séries ▸ Twin Peaks',parent:'C:\\Videos\\Series',items:[{dir:false,name:'S01E01.mkv',path:'C:\\Videos\\Series\\Twin Peaks\\S01E01.mkv'}]},
  'C:\\Downloads': {title:'Downloads',parent:'',items:[]},
};
const server=http.createServer((req,res)=> {
  if(req.url==='/font.ttf'){res.setHeader('Content-Type','font/ttf');res.end(fs.readFileSync('assets/fonts/CueOmni.ttf'));return;}
  if(req.url.startsWith('/list')){
    const p=decodeURIComponent(new URL(req.url,'http://x').searchParams.get('p')||'');
    const body=p===''?{path:'',parent:'',title:'',items:roots}:{path:p,...listings[p]};
    res.setHeader('Content-Type','application/json');res.end(JSON.stringify(body));return;
  }
  if(req.url.startsWith('/thumb')){res.statusCode=404;res.end();return;}
  res.setHeader('Content-Type','text/html');res.end(pageHTML);
});
(async()=>{
  await new Promise(r=>server.listen(8124,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
  const errors=[];
  for (const multi of [false,true]) {
    roots = multi ? [{dir:true,name:'Vídeos',path:'C:\\Videos'},{dir:true,name:'Downloads',path:'C:\\Downloads'}]
                  : [{dir:true,name:'Vídeos',path:'C:\\Videos'}];
    const context=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
    const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
    await page.addInitScript(()=>{
      localStorage.setItem('cue.token.v2','a'.repeat(64));
      window.WebSocket=class {
        constructor(){this.readyState=0;setTimeout(()=>{this.readyState=1;this.onopen?.();},5);}
        send(data){const m=JSON.parse(data);if(m.t==='auth'){window.__ws=this;setTimeout(()=>{this.onmessage?.({data:JSON.stringify({t:'hello',pc:'Cue PC'})});this.onmessage?.({data:JSON.stringify({t:'state',view:'home',file:'',pos:0,dur:0,speed:1,vol:100,paused:true,ended:false,audioOnly:false,sub:{has:false,on:false,delay:0,size:44,sizeMin:28,sizeMax:72,pos:93,posDefault:93,posMin:12,posMax:97},audio:{n:0,i:0},timer:{m:0,left:0},next:'',color:'#a0508f'})});},5);}}
        close(){this.readyState=3;}
      };
    });
    await page.goto('http://127.0.0.1:8124');await page.waitForTimeout(250);
    const state=()=>page.evaluate(()=>({open:!document.getElementById('browse').classList.contains('hide'),
      title:document.getElementById('crumb').textContent,arrow:getComputedStyle(document.getElementById('crumbArrow')).display!=='none',
      tiles:document.querySelectorAll('#grid .tile').length}));
    await page.locator('#bFolder').click();await page.waitForTimeout(150);
    let s=await state();
    if (!multi) {
      // one shared folder: it opens straight away and is the top, so there is no back arrow
      assert(s.open && s.title==='Vídeos' && !s.arrow, JSON.stringify(s));
      await page.screenshot({path:'build/remote-browser/folders-top.png'});
      await page.locator('#crumbBtn').click({force:true});await page.waitForTimeout(150);
      assert((await state()).open,'tapping the title at the top must not leave');
    } else {
      assert(s.open && s.tiles===2 && !s.arrow, JSON.stringify(s));
      await page.locator('#grid .tile').first().click();await page.waitForTimeout(150);
      s=await state(); assert(s.title==='Vídeos' && s.arrow, 'several shared folders: Vídeos can go back to the list '+JSON.stringify(s));
      await page.locator('#crumbBtn').click();await page.waitForTimeout(150);
      s=await state(); assert(s.open && s.tiles===2 && !s.arrow, 'back to the shared folders '+JSON.stringify(s));
      await page.locator('#grid .tile').first().click();await page.waitForTimeout(150);
    }
    await page.locator('#grid .tile').first().click();await page.waitForTimeout(150);
    await page.locator('#grid .tile').first().click();await page.waitForTimeout(150);
    s=await state(); assert(s.title.endsWith('Twin Peaks') && s.arrow, JSON.stringify(s));
    if (!multi) await page.screenshot({path:'build/remote-browser/folders-deep.png'});
    await page.locator('#crumbBtn').click();await page.waitForTimeout(150);
    s=await state(); assert(s.open && s.title==='Vídeos ▸ Séries', 'back goes one folder up '+JSON.stringify(s));
    await page.locator('#crumbBtn').click();await page.waitForTimeout(150);
    s=await state(); assert(s.open && s.title==='Vídeos', JSON.stringify(s));
    await page.locator('#grid .tile').first().click();await page.waitForTimeout(150);
    await page.locator('#browseClose').click();await page.waitForTimeout(150);
    s=await state(); assert(!s.open, '× leaves the folders from anywhere');
    assert.equal(await page.locator('#browseClose').getAttribute('aria-label'),'fechar pastas');
    await context.close();
  }
  await browser.close();server.close();
  assert.deepEqual(errors,[]);
  console.log('remote folders: passed');
})().catch(e=>{console.error(e);process.exit(1);});
