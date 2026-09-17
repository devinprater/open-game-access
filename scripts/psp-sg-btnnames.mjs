// brute-force PPSSPP debugger button NAMES, to find ones not in the short list.
const URL="ws://127.0.0.1:12345/debugger";
const s=new WebSocket(URL,"debugger.ppsspp.org");
let n=1; const pend=new Map();
s.addEventListener("message",e=>{let m;try{m=JSON.parse(e.data)}catch{return}
  if(m.ticket&&pend.has(m.ticket)){const p=pend.get(m.ticket);pend.delete(m.ticket);m.event==="error"?p.rej(new Error(m.message)):p.res(m)}});
await new Promise((res,rej)=>{s.addEventListener("open",res,{once:true});s.addEventListener("error",rej,{once:true})});
const req=(e,f={})=>new Promise((res,rej)=>{const t=String(n++);pend.set(t,{res,rej});
  setTimeout(()=>{pend.delete(t);rej(new Error("timeout"))},5000);s.send(JSON.stringify({event:e,ticket:t,...f}))});
await req("version",{name:"names",version:"1"});
const cands=["l","r","l1","r1","l2","r2","L","R","trigger_l","trigger_r","ltrig","rtrig",
 "leftshoulder","rightshoulder","shoulder_l","shoulder_r","shoulderL","shoulderR",
 "ltrigger","rtrigger","lb","rb","LB","RB","pad_l","pad_r","analog","stick","nub",
 "cross_pad","dpad_up","dpad_down","dpad_left","dpad_right","select_button","start_button",
 "home_button","hold_button","wlan_button","screen_button","note_button","volume_up",
 "volume_down","power_button","debug","pause","reset","fastforward","rewind","save_state","load_state"];
const ok=[];
for (const c of cands) {
  try { await req("input.buttons.press",{button:c,duration:5}); ok.push(c); }
  catch(e) { /* rejected */ }
}
console.log("ACCEPTED button names beyond the known set:");
console.log(" ", ok.join(", ") || "(none new)");
s.close();
