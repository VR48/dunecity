import { build } from 'esbuild';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root = fileURLToPath(new URL('../../', import.meta.url));
const output = await mkdtemp(path.join(tmpdir(), 'dunecity-p2p-test-'));
await build({entryPoints:[path.join(root, 'platform/web/p2pkit/src/framing/index.ts'), path.join(root, 'platform/web/p2pkit/src/transports/rtc.ts')], outdir:output, outbase:path.join(root,'platform/web/p2pkit/src'), bundle:true, platform:'browser', format:'esm', outExtension:{'.js':'.mjs'}});
const {Chunker, CHUNK_LIMITS:L} = await import(pathToFileURL(path.join(output,'framing/index.mjs')));
const {RTCTransport, directIceServers, validateDirectCandidate, validateDirectDescription} = await import(pathToFileURL(path.join(output,'transports/rtc.mjs')));
await rm(output,{recursive:true,force:true});

test('largest game payload survives P2PKit fragmentation both directions', () => {
  const expected = `g:1:123:${'ab'.repeat(262128)}`;
  const packets = [...new Chunker().split('0123456789abcdef',JSON.stringify(expected))].map(JSON.parse);
  assert.ok(packets.length > 32);
  const receiver = new Chunker();
  let actual;
  for (const packet of packets.reverse()) actual = receiver.ingest(packet) ?? actual;
  assert.equal(JSON.parse(actual),expected);
});
test('preauthentication fragment shapes cannot allocate or escape bounds', () => {
  const valid={id:'ab',i:0,n:2,part:'x'};
  const invalid=[null,[],{},1,'x', {...valid,n:2**32-1}, {...valid,n:0}, {...valid,n:-1}, {...valid,n:1.5}, {...valid,i:NaN}, {...valid,i:-1}, {...valid,i:2}, {...valid,part:[]}, {...valid,part:'x'.repeat(16001)}, {...valid,id:'x'.repeat(65)}, {...valid,id:'__proto__\n'}, {...valid,n:1,i:1}];
  for(const packet of invalid) assert.throws(()=>new Chunker().ingest(packet));
});
test('duplicate conflicts, changing group sizes and partial expiry fail closed', () => {
  const c=new Chunker(); const first={id:'ab',i:0,n:2,part:'one'};
  assert.equal(c.ingest(first),undefined); assert.equal(c.ingest(first),undefined);
  assert.throws(()=>c.ingest({...first,part:'two'}));
  assert.throws(()=>c.ingest({...first,n:1}));
  assert.throws(()=>c.checkDeadline(performance.now()+L.lifetimeMs));
  c.reset(); assert.doesNotThrow(()=>c.checkDeadline(performance.now()+L.lifetimeMs));
});
test('partial groups and aggregate byte budgets are enforced', () => {
  const c=new Chunker();
  for(let i=0;i<L.pendingGroups;i++) c.ingest({id:String(i),i:0,n:2,part:'a'});
  assert.throws(()=>c.ingest({id:'extra',i:0,n:2,part:'a'}));
  c.reset();
  assert.throws(()=>{
    for(let group=0;group<8;group++) for(let i=0;i<60;i++) c.ingest({id:String(group),i,n:128,part:'a'.repeat(16000)});
  },/budget/);
});
test('no TURN config or injected relay candidate can enter RTC', () => {
  assert.deepEqual(directIceServers([{urls:'stun:stun.example.org:3478'}]),[{urls:['stun:stun.example.org:3478']}]);
  for(const server of [{urls:'turn:evil:3478'},{urls:['stun:good','turns:evil']},{urls:'https://evil'},{urls:'stun:good',credential:'secret'}]) assert.throws(()=>directIceServers([server]));
  validateDirectCandidate({candidate:'candidate:1 1 udp 1 127.0.0.1 5000 typ host',sdpMid:'0'});
  assert.throws(()=>validateDirectCandidate({candidate:'candidate:1 1 udp 1 1.2.3.4 5000 typ relay'}));
});
const sdp='v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=fingerprint:sha-256 '+Array(32).fill('AB').join(':')+'\r\n';
test('data-only SDP requires certificate fingerprint and rejects embedded relay candidates',()=>{
  validateDirectDescription({type:'offer',sdp});
  assert.throws(()=>validateDirectDescription({type:'offer',sdp:sdp+'a=candidate:1 1 udp 1 1.2.3.4 5000 typ relay\r\n'}));
  assert.throws(()=>validateDirectDescription({type:'offer',sdp:sdp+'m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n'}));
  assert.throws(()=>validateDirectDescription({type:'rollback',sdp}));
  assert.throws(()=>validateDirectDescription({type:'offer',sdp:sdp.replace(Array(32).fill('AB').join(':'), ':'.repeat(95))}));
  assert.throws(()=>validateDirectDescription({type:'offer',sdp:sdp+'a=fingerprint:sha-256 '+Array(32).fill('AB').join(':')+'\r\n'}));
});
class Channel {
  label='p2pkit'; ordered=true; maxRetransmits=null; maxPacketLifeTime=null;
  readyState='connecting'; bufferedAmount=0; sent=[];
  close(){this.readyState='closed';this.onclose?.()}
  send(packet){this.sent.push(packet)}
  open(){this.readyState='open';this.onopen?.()}
}
let pc;
class PC {
  connectionState='new'; channel=new Channel();
  constructor(){pc=this}
  createDataChannel(){return this.channel}
  async createOffer(){return {type:'offer',sdp}}
  async setLocalDescription(d){this.localDescription=d}
  async setRemoteDescription(){}
  async addIceCandidate(){}
  close(){this.connectionState='closed'}
}
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
function connection(){
  const handlers=new Set(); const sent=[];
  const t=new RTCTransport({self:'1',remote:'2',backend:{RTCPeerConnection:PC},initiator:true,iceServers:[],signalling:{ready:Promise.resolve(),onMessage(fn){handlers.add(fn);return()=>handlers.delete(fn)},send(m){sent.push(m)}}});
  const errors=[];let closed=0;t.on('error',e=>errors.push(e));t.on('disconnect',()=>closed++);
  return {t,pc,handlers,errors,sent,closed:()=>closed};
}
test('a malformed pre-game RTC frame closes the peer and detaches signal handlers',async()=>{
  const c=connection();c.pc.channel.open();
  c.pc.channel.onmessage({data:JSON.stringify({id:'x',i:0,n:2**32-1,part:'a'})});
  assert.equal(c.closed(),1);assert.equal(c.errors.length,1);assert.equal(c.handlers.size,0);
  c.t.disconnect();assert.equal(c.closed(),1);
  await tick();
});
test('RTC send preserves order and fragments a full-size game packet',async()=>{
  const c=connection();c.pc.channel.open();
  const messages=['a'.repeat(524256),'second','third'];
  await Promise.all(messages.map(m=>c.t.send(m)));
  const reassembly=new Chunker();const received=[];
  for(const packet of c.pc.channel.sent){const complete=reassembly.ingest(JSON.parse(packet));if(complete!==undefined)received.push(JSON.parse(complete))}
  assert.deepEqual(received,messages);assert.equal(c.t.bufferedAmount,0);c.t.disconnect();
});
test('overflowing concurrent sends fails closed instead of dropping commands',async()=>{
  const c=connection();c.pc.channel.open();c.pc.channel.bufferedAmount=2**22;
  const result=await Promise.allSettled(Array.from({length:8},()=>c.t.send('x'.repeat(600000))));
  assert.ok(result.every(r=>r.status==='rejected'));assert.equal(c.closed(),1);assert.ok(c.errors.length);c.t.disconnect();
});
test('invalid signaling and a flood before SDP cannot produce unhandled rejections',async()=>{
  const c=connection(); const handler=[...c.handlers][0];
  for(let i=0;i<140;i++)handler({from:'2',to:'1',iceCandidate:{candidate:'candidate:1 1 udp 1 127.0.0.1 4000 typ host'}});
  await tick();assert.equal(c.closed(),1);assert.equal(c.handlers.size,0);
});
test('well-formed tiny-message floods are bounded before parsing',async()=>{
  const c=connection();c.pc.channel.open();
  let received=0;c.t.on('message',()=>received++);
  const data=JSON.stringify({id:'a',i:0,n:1,part:'"ping"'});
  for(let i=0;i<5000;i++)c.pc.channel.onmessage({data});
  assert.equal(received,4096);assert.equal(c.closed(),1);assert.equal(c.errors.length,1);
  await tick();
});

test('ICE gathering completion does not publish an empty address or close a connection',async()=>{
  const c=connection();c.pc.channel.open();await tick();
  const before=c.sent.length;
  c.pc.onicecandidate({candidate:{toJSON(){return {candidate:'',sdpMid:'0',sdpMLineIndex:0}}}});
  c.pc.onicecandidate({candidate:null});
  assert.equal(c.sent.length,before);assert.equal(c.closed(),0);assert.equal(c.errors.length,0);
  await c.t.send('still connected');c.t.disconnect();
});
