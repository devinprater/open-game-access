#!/usr/bin/env node
// dis-read.mjs -- one-shot tiny RAM reads for closed-loop driving.
// Usage: node dis-read.mjs <hexaddr> <len>   (prints hex + ascii)
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const addr = parseInt(process.argv[2], 16);
const len = parseInt(process.argv[3] || "16", 10);
const ws = new WebSocket(URL);
let id = 0;
ws.onopen = () => ws.send(JSON.stringify({ event: "memory.read", requestId: ++id, address: addr, size: len }));
ws.onmessage = (m) => {
  const o = JSON.parse(String(m.data));
  if (o.event !== "memory.read" || o.base64 === undefined) return;
  const b = Buffer.from(o.base64, "base64");
  console.log("addr 0x" + addr.toString(16) + " len " + b.length);
  console.log("hex: " + b.toString("hex"));
  console.log("asc: " + b.toString("latin1").replace(/[^\x20-\x7e]/g, "."));
  let u = "";
  for (let i = 0; i + 1 < b.length; i += 2) {
    const c = b.readUInt16LE(i);
    u += c === 0 ? "|" : (c >= 32 && c < 127 ? String.fromCharCode(c) : ".");
  }
  console.log("u16: " + u);
  ws.close(); process.exit(0);
};
ws.onerror = () => { console.log("WS_ERR"); process.exit(1); };
setTimeout(() => { console.log("TIMEOUT"); process.exit(2); }, 15000);
