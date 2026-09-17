// Wait for the intro movie to end, then press through. Long holds, watching head + screen.
const URL="ws://127.0.0.1:12345/debugger", HEAD=0x089797E8, LB=0x08978F14;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},10000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"skip",version:"1"});
const u32=async a=>Buffer.from((await req("memory.read",{address:a,size:4,replacements:false})).base64,"base64").readUInt32LE(0);
console.log("start: head =",await u32(HEAD),"logbase =","0x"+(await u32(LB)).toString(16).toUpperCase());
for (let round=1; round<=14; round++) {
  for (const b of ["start","cross"]) {
    await req("input.buttons.press",{button:b,duration:600});
    await sleep(900);
  }
  const h=await u32(HEAD), lb=await u32(LB);
  console.log(`  round ${round}: head=${h} logbase=0x${lb.toString(16).toUpperCase()}${h>0?"   <== IN GAME":""}`);
  if (h>0) { console.log("  *** REACHED THE GAME ***"); break; }
  await sleep(4000);
}
console.log("final head =",await u32(HEAD));
s.close();
