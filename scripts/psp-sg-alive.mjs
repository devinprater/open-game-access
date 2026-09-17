// Is the emulator RUNNING? Read values that should change every frame if it is.
const URL="ws://127.0.0.1:12345/debugger";
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},8000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"alive",version:"1"});
const u32=async a=>Buffer.from((await req("memory.read",{address:a,size:4,replacements:false})).base64,"base64").readUInt32LE(0);
const u16=async a=>Buffer.from((await req("memory.read",{address:a,size:2,replacements:false})).base64,"base64").readUInt16LE(0);
// 0x089AA990 toggled 16<->17 during waits (animation) earlier in this investigation.
const samples=[];
for (let i=0;i<12;i++){ samples.push(await u16(0x089AA990)); await sleep(250); }
console.log("anim counter 0x089AA990:", samples.join(" "));
const uniq=[...new Set(samples)];
console.log(uniq.length>1 ? "  => EMULATOR IS RUNNING (value changes)" : "  => !!! FROZEN / PAUSED (value static)");
// also try the CPU: read the current PC if the debugger offers it
try { const st = await req("cpu.status"); console.log("\ncpu.status:", JSON.stringify(st).slice(0,200)); } catch(e) { console.log("\ncpu.status: unavailable"); }
try { const g = await req("gpu.status"); console.log("gpu.status:", JSON.stringify(g).slice(0,200)); } catch(e) { console.log("gpu.status: unavailable"); }
s.close();
