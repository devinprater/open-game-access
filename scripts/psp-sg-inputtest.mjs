// Minimal, unambiguous test: press HOME (must raise the PSP quit prompt if input lands).
// We read a broad RAM slab and also report cpu.status before/after.
const URL="ws://127.0.0.1:12345/debugger";
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("to"))},60000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"it",version:"1"});
const rd=async(a,z)=>Buffer.from((await req("memory.read",{address:a,size:z,replacements:false})).base64,"base64");
const u32=async a=>Buffer.from((await req("memory.read",{address:a,size:4,replacements:false})).base64,"base64").readUInt32LE(0);
async function slab(){ const p=[]; for(let i=0;i<3;i++) p.push(await rd(0x08800000+i*0x100000,0x100000)); return Buffer.concat(p); }
const cnt=(a,b)=>{let c=0;for(let i=0;i<a.length;i++) if(a[i]!==b[i])c++;return c};

console.log("cpu.status before:",JSON.stringify(await req("cpu.status")));
let a=await slab(); await sleep(3000); let b=await slab();
console.log("  idle 3s churn:",cnt(a,b));

console.log("pressing HOME ...");
await req("input.buttons.press",{button:"home",duration:60});
await sleep(2500);
console.log("cpu.status after :",JSON.stringify(await req("cpu.status")));
let c=await slab(); await sleep(3000); let d=await slab();
console.log("  post-HOME churn:",cnt(c,d));

console.log("\nreading the two phone-state bytes:");
console.log("  0x0897B1E7 =",await u32(0x0897B1E7));
console.log("  0x0897B1E8 =",await u32(0x0897B1E8));
s.close();
