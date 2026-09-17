// press each button repeatedly and report whether the WRITE HEAD moves.
const URL="ws://127.0.0.1:12345/debugger", HEAD=0x089797E8, CFG=0x089B5E34;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},8000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"btnsweep",version:"1"});
const u32=async a=>Buffer.from((await req("memory.read",{address:a,size:4,replacements:false})).base64,"base64").readUInt32LE(0);
const u16=async a=>Buffer.from((await req("memory.read",{address:a,size:2,replacements:false})).base64,"base64").readUInt16LE(0);
const cfg=await u32(CFG);
console.log(`config ptr 0x${cfg.toString(16).toUpperCase()}`);
for (const o of [0x18,0x1a,0x1c,0x1e,0x20]) console.log(`  config+0x${o.toString(16)} (u16) = ${await u16(cfg+o)}`);
console.log();
for (const b of ["cross","circle","square","triangle","start","select","up","down","right","left","home","wlan","note","screen","hold"]) {
  const before=await u32(HEAD);
  for (let i=0;i<3;i++){ await req("input.buttons.press",{button:b,duration:20}); await sleep(900); }
  await sleep(700);
  const after=await u32(HEAD);
  console.log(`  ${b.padEnd(9)} head ${before} -> ${after}${after>before?"   <== MOVES":""}`);
}
s.close();
