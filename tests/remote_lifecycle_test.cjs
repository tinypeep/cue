// Runs the production connection/command code with a deterministic clock and socket.
// No browser/DOM library is required. Layout is covered by remote_*_browser.cjs.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path'),crypto=require('crypto');
const root=process.env.CUE_SOURCE_ROOT||path.join(__dirname,'..');
const html=fs.readFileSync(path.join(root,'assets/remote/index.html'),'utf8');
const code=html.slice(html.indexOf('// ── state '),html.indexOf('// ── pairing by code'));
const credential='a'.repeat(64);
const kid=crypto.createHash('sha256').update(credential).digest('hex').substr(0,16);
const nonce='0'.repeat(32);
const proof=crypto.createHmac('sha256',Buffer.from(credential,'utf8')).update(nonce).digest('hex');
function fixture(){
 let now=1000,id=0,tasks=new Map(),sockets=[],nodes=new Map();
 const node=k=>{if(!nodes.has(k)){const set=new Set(['hide']);nodes.set(k,{textContent:'',classList:{contains:x=>set.has(x),toggle:(x,on)=>on?set.add(x):set.delete(x),add:x=>set.add(x),remove:x=>set.delete(x)}});}return nodes.get(k);};
 class Socket {
  constructor(){this.readyState=0;this.bufferedAmount=0;this.sent=[];sockets.push(this);}
  open(){this.readyState=1;this.onopen?.();}
  send(s){this.sent.push({at:now,...JSON.parse(s)});}
  receive(m){this.onmessage?.({data:JSON.stringify(m)});}
  close(){this.readyState=3;}
 }
 const ctx={console,Map,Set,Math,JSON,WebSocket:Socket,performance:{now:()=>now},Date:{now:()=>now},navigator:{onLine:true},document:{hidden:false},window:{},location:{hash:'',protocol:'http:',host:'127.0.0.1:7878'},
  localStorage:{getItem:()=> 'a'.repeat(64),setItem(){},removeItem(){}},history:{replaceState(){}},
  setTimeout:(fn,ms)=>{tasks.set(++id,{fn,at:now+ms});return id;},clearTimeout:k=>tasks.delete(k),
  $:node,t:k=>k,hidePair(){},showPair(){},toast(){},celebrate(){},linked(){},render(){},resumeRemoteWork(){},suspendRemoteWork(){},
  releaseAwake(){},keepAwake(){},clearPeek(){},cancelGestures(){},relocalize:()=>false,queueOpen:false,queueLoaded:0,loadQueue(){},coverRetry:0};
 const handlers={};
 ctx.document.addEventListener=(k,fn)=>(handlers[k]||=[]).push(fn);
 ctx.window.addEventListener=ctx.document.addEventListener;
 vm.createContext(ctx);vm.runInContext(code,ctx);
 const tail=html.slice(html.lastIndexOf("document.addEventListener('visibilitychange', function () {"),html.lastIndexOf('})();'));
 vm.runInContext(tail,ctx);
 function advance(ms){const until=now+ms;let count=0;while(true){let next;for(const [k,v]of tasks)if(v.at<=until&&(!next||v.at<next[1].at))next=[k,v];if(!next)break;assert(++count<10000,'timer loop');now=next[1].at;tasks.delete(next[0]);next[1].fn();}now=until;}
 // open a socket and complete the challenge-and-hello handshake
 function link(s){s.open();s.receive({t:'chal',n:nonce});const answer=s.sent.find(x=>x.t==='auth');assert(answer && answer.k===kid && answer.r===proof, 'the challenge must be answered with the credential proof');s.receive({t:'hello',pc:'test',sid:'01'.repeat(8),sk:'b'.repeat(64)});}
 function auth(){ctx.connect(null);const s=sockets.at(-1);link(s);return s;}
 function fire(k){for(const fn of handlers[k]||[])fn({persisted:true});}
 return {ctx,sockets,tasks,advance,auth,link,node,fire,jump:ms=>{now+=ms;}};
}
{
 const f=fixture(),s=f.auth();
 f.ctx.send({c:'vol',v:10},true);f.ctx.send({c:'vol',v:20},true);f.ctx.send({c:'vol',v:30});f.advance(200);
 assert.deepStrictEqual(s.sent.filter(x=>x.c==='vol').map(x=>x.v),[10,30],'a delayed intermediate value must not overwrite the final value');
}
{
 const f=fixture(),s=f.auth();
 for(let i=0;i<1000;i++)f.ctx.send({c:'vol',v:i%126},true);
 f.advance(1000);const writes=s.sent.filter(x=>x.c==='vol');
 assert(writes.length<=2 && writes.at(-1).v===999%126);
 console.log('1000 volume inputs =>',writes.length,'messages; latest value retained');
}
{
 const f=fixture(),s=f.auth();
 f.ctx.send({c:'vol',v:1},true);f.ctx.send({c:'vol',v:2},true);
 f.ctx.retireLink();const newer=f.auth();f.advance(200);
 assert.strictEqual(newer.sent.filter(x=>x.c).length,0,'no command replay on reconnect');
 const oldClose=s.onclose; // detached handlers cannot mutate a replacement connection
 assert.strictEqual(oldClose,null);assert.strictEqual(f.ctx.ws,newer);
}
{
 const f=fixture(),s=f.auth();f.advance(15000);
 assert(s.sent.some(x=>x.t==='ping'),'heartbeat must probe an apparently open socket');
 f.advance(10000);assert.strictEqual(s.readyState,3);f.advance(1000);assert.strictEqual(f.sockets.length,2,'a silent socket must be replaced');
}
{
 const f=fixture(),s=f.auth();f.advance(15000);s.receive({t:'pong'});f.advance(10000);assert.strictEqual(s.readyState,1);
 s.bufferedAmount=65537;f.ctx.send({c:'toggle'});assert.strictEqual(s.readyState,3,'backpressure must retire the stale socket');
}
{
 const f=fixture();f.ctx.connect(null);const s=f.sockets[0];f.advance(10000);assert.strictEqual(s.readyState,3,'connecting/authentication timeout');
}
{
 const f=fixture(),s=f.auth();f.ctx.S.media=11;f.ctx.send({c:'seek',v:5});assert.strictEqual(s.sent.at(-1).media,11);
 f.ctx.S.view='play';f.ctx.S.file='same.mkv';f.ctx.S.pos=99;f.ctx.holdOn('pos');
 const m=JSON.parse(JSON.stringify(f.ctx.S));m.media=12;m.pos=0;
 f.ctx.reconcile(m,f.ctx.S,99);assert.strictEqual(m.pos,0,'identical basenames in another media generation must not inherit optimistic position');
}
{
 const f=fixture(),s=f.auth();s.receive({t:'bye'});f.advance(60000);assert.strictEqual(f.sockets.length,1,'PC off must stop retries');assert.strictEqual(f.ctx.linkStopped,true);
}
console.log('remote lifecycle: final command ordering, bounded coalescing, no reconnect replay, socket identity, heartbeat/deadline, backpressure and media generations passed');

{
 const f=fixture(),s=f.auth();f.ctx.document.hidden=true;f.fire('visibilitychange');
 assert.strictEqual(s.readyState,3,'background must release its socket immediately');
 assert.strictEqual(f.tasks.size,0,'no connection timers while hidden');
 f.advance(3600000);f.ctx.document.hidden=false;f.fire('visibilitychange');f.fire('focus');f.fire('pageshow');
 assert.strictEqual(f.sockets.length,2,'resume event cascade must keep one attempt');
 const newer=f.sockets.at(-1);f.link(newer);
 assert.strictEqual(newer.sent[0].i,s.sent[0].i,'page identity must survive socket replacement');
}
{
 const f=fixture();f.auth();f.ctx.linkFrozen=true;f.fire('freeze');
 f.advance(120000);assert.strictEqual(f.tasks.size,0);assert.strictEqual(f.ctx.ws,null);
 f.fire('resume');f.fire('pageshow');assert.strictEqual(f.sockets.length,2);
}
{
 const f=fixture();f.auth();f.ctx.navigator.onLine=false;f.fire('offline');
 f.advance(3600000);assert.strictEqual(f.tasks.size,0,'offline must not spin retry timers');
 f.ctx.navigator.onLine=true;f.fire('online');f.fire('focus');assert.strictEqual(f.sockets.length,2);
}
{
 const f=fixture();f.ctx.cred=null;f.ctx.connect('123456');const first=f.sockets[0];first.open();
 first.receive({t:'chal',n:nonce});first.onerror();f.advance(1000);const second=f.sockets.at(-1);second.open();
 second.receive({t:'chal',n:nonce});
 assert.strictEqual(second.sent[0].c,'123456','transient failure must retain the unconfirmed pairing code');
 second.receive({t:'hello',pc:'test',key:'b'.repeat(64)});
 assert.strictEqual(f.ctx.pendingCode,null);assert.strictEqual(f.ctx.cred,'b'.repeat(64));
}
{
 const f=fixture();f.ctx.cred=null;f.ctx.connect('123456');const first=f.sockets[0];
 first.onclose({code:1006});f.advance(1000);assert.strictEqual(f.sockets.length,2,'network close must not report a wrong code');
 const second=f.sockets[1];second.onclose({code:4002});f.advance(180000);
 assert.strictEqual(f.sockets.length,2,'expired pairing code must stop automatic attempts');
}
{
 const f=fixture(),s=f.auth();f.advance(15000);const late=[...f.tasks.values()].find(x=>x.at===26000).fn;
 f.ctx.retireLink();const newer=f.auth();late();assert.strictEqual(f.ctx.ws,newer,'a late deadline cannot retire the replacement');
}
{
 const f=fixture();let s=f.auth();
 for(let i=0;i<12*60*4;i++){f.advance(15000);s.receive({t:'pong'});}
 assert.strictEqual(f.sockets.length,1,'12 simulated hours of idle heartbeat must stay connected');
 for(let i=0;i<200;i++){
  f.ctx.document.hidden=true;f.fire('visibilitychange');assert.strictEqual(f.tasks.size,0);
  f.ctx.document.hidden=false;f.fire('visibilitychange');s=f.sockets.at(-1);f.link(s);
 }
 assert.strictEqual(f.sockets.filter(x=>x.readyState===1).length,1,'repeated resumes must keep one live socket');
 assert.strictEqual(f.tasks.size,1,'repeated resumes must keep one heartbeat timer');
}
console.log('remote recovery: 12 simulated hours, 200 sleep/resume cycles, freeze, offline, transient pairing, event deduplication and stale deadlines passed');

{
 const f=fixture();let tick, renders=0, covers=0;
 Object.assign(f.ctx,{setInterval:fn=>{tick=fn;return 1;},clearInterval(){},
  drawFace(){},drawGlow(){},render(){++renders;},syncCover(){++covers;},pausedAt:0,
  faceTimers:new Set(),idleTimer:0,drag:null,anims:[]});
 vm.runInContext(html.slice(html.indexOf('var faceTimers ='),html.indexOf('// The end-of-episode card')),f.ctx);
 f.auth();f.ctx.startFaceTimers();
 for(let i=0;i<120;i++)tick();
 assert.strictEqual(renders,0,'idle timer must not rebuild paused UI');assert.strictEqual(covers,120,'failed covers remain retryable while paused');
 f.ctx.S.view='play';f.ctx.S.paused=false;tick();assert.strictEqual(renders,0,'fresh PC snapshot already rendered');
 f.advance(800);tick();assert.strictEqual(renders,1,'local timeline continues when PC snapshots lag');
 f.ctx.stopFaceTimers();
}
console.log('remote idle: paused UI rebuilds eliminated; cover retry and delayed timeline refresh preserved');

{
 const f=fixture(),s=f.auth(),lateMessage=s.onmessage;
 f.jump(90000);f.fire('focus');const newer=f.sockets.at(-1);
 assert.strictEqual(f.sockets.length,2,'returning interaction must replace a stale OPEN socket even without close/visibility events');
 lateMessage({data:'{"t":"bye"}'});assert.strictEqual(f.ctx.ws,newer);assert.strictEqual(f.ctx.linkStopped,false,'retired socket messages cannot stop a new attempt');
}
{
 const f=fixture();f.ctx.cred=null;f.ctx.connect('123456');
 f.ctx.document.hidden=true;f.fire('visibilitychange');f.advance(121000);
 f.ctx.document.hidden=false;f.fire('visibilitychange');
 assert.strictEqual(f.sockets.length,1,'expired unconfirmed code must not be retried');assert.strictEqual(f.tasks.size,0);
}

{
 const f=fixture();f.ctx.navigator.onLine=false;
 const s=f.auth();f.ctx.send({c:'toggle'});
 assert(s.sent.some(x=>x.c==='toggle'),'an offline browser hint must not disable a working local LAN');
}

// Production playback gestures: a pause changes the clock's rate only after sampling it.
function playback(f){
 Object.assign(f.ctx,{anim(){},drawFace(){}});
 vm.runInContext(html.slice(html.indexOf('function togglePlay()'),html.indexOf('function center()')),f.ctx);
 f.auth();Object.assign(f.ctx.S,{view:'play',file:'test.mkv',media:7,pos:10,dur:100,paused:false,speed:1,ended:false});
 f.ctx.stamp=1000;
}
{
 const f=fixture();playback(f);f.advance(600);f.ctx.togglePlay();
 assert(Math.abs(f.ctx.S.pos-10.6)<1e-8,'pause must freeze the displayed live position, not the preceding snapshot');
 f.advance(3000);f.ctx.togglePlay();assert(Math.abs(f.ctx.S.pos-10.6)<1e-8,'resume must not count time spent paused');
 f.advance(500);assert(Math.abs(f.ctx.livePos()-11.1)<1e-8);
}
{
 const f=fixture();playback(f);f.ctx.S.speed=2;f.advance(400);f.ctx.togglePlay();assert(Math.abs(f.ctx.S.pos-10.8)<1e-8);
 const stale=JSON.parse(JSON.stringify(f.ctx.S));stale.paused=false;stale.pos=10;
 f.ctx.reconcile(stale,f.ctx.S,f.ctx.livePos());assert.strictEqual(stale.paused,true);assert(Math.abs(stale.pos-10.8)<1e-8,'stale pause state must not move the anchored clock');
 const other=JSON.parse(JSON.stringify(f.ctx.S));other.media=8;other.pos=0;other.paused=false;
 f.ctx.reconcile(other,f.ctx.S,10.8);assert.strictEqual(other.pos,0,'another media generation stays authoritative');
}
{
 const f=fixture();playback(f);f.ctx.S.pos=100;f.ctx.S.ended=true;f.ctx.S.paused=true;f.advance(1000);f.ctx.togglePlay();
 assert.strictEqual(f.ctx.S.pos,0,'replay starts from zero');assert.strictEqual(f.ctx.S.ended,false);
 const old=JSON.parse(JSON.stringify(f.ctx.S));old.pos=100;old.paused=true;old.ended=true;
 f.ctx.reconcile(old,f.ctx.S,0);assert.strictEqual(old.pos,0);assert.strictEqual(old.ended,false,'stale end state cannot freeze a replay while awaiting confirmation');
}
{
 const f=fixture();playback(f);f.ctx.retireLink();const before=JSON.stringify(f.ctx.S);
 f.ctx.togglePlay();f.ctx.skip(15);f.ctx.what();assert.strictEqual(JSON.stringify(f.ctx.S),before,'unavailable commands cannot mutate optimistic playback state');
}
console.log('phone clock: pause/resume anchoring, 2x playback, stale pause snapshots, media replacement, replay and disconnected gestures passed');
