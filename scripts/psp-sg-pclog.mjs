// If input LANDS, the emulated CPU must execute code. Sample the PC repeatedly while
// pressing: if the PC is stuck in one tight loop that never varies, the game is waiting;
// if it varies widely, the game is running and simply ignoring the button.
const URL="ws://127.0.0.1:12345/debugger";
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},8000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"pclog",version:"1"});
const st=await req("cpu.status");
console.log("cpu.status:",JSON.stringify(st));
const pcs=[];
for (let i=0;i<20;i++){ const r=await req("cpu.status"); pcs.push(r.pc); await sleep(120); }
const u=[...new Set(pcs)];
console.log(`distinct PCs over 20 samples: ${u.length}`);
console.log("  sample:",u.slice(0,8).map(x=>"0x"+x.toString(16).toUpperCase()).join(" "));
console.log(u.length<=3 ? "  => CPU is spinning in a tiny loop (waiting / idle)" : "  => CPU is executing varied code (running)");
// press and watch ticks advance
const t1=await req("cpu.status"); await sleep(1000); const t2=await req("cpu.status");
console.log(`ticks advanced in 1s: ${t2.ticks-t1.ticks}`);
s.close();
