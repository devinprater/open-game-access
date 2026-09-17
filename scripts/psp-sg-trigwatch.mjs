// press the trigger-family buttons and report whether the write head moves or the screen flips.
const URL="ws://127.0.0.1:12345/debugger", HEAD=0x089797E8;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},8000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"trig",version:"1"});
const u32=async a=>Buffer.from((await req("memory.read",{address:a,size:4,replacements:false})).base64,"base64").readUInt32LE(0);
for (const b of ["ltrigger","rtrigger","l2","r2","select","note","wlan"]) {
  const before=await u32(HEAD);
  for (let i=0;i<3;i++){ await req("input.buttons.press",{button:b,duration:25}); await sleep(1100); }
  await sleep(800);
  const after=await u32(HEAD);
  console.log(`  ${b.padEnd(10)} head ${before} -> ${after}${after>before?"   <== MOVES":""}`);
}
s.close();
