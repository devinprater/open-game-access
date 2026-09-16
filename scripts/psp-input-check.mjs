#!/usr/bin/env node
/**
 * psp-input-check.mjs — prove that injected input actually reaches the emulated pad.
 *
 * ⛔ THE POINT OF THIS SCRIPT. When the game "ignores" a button press there are two
 * very different explanations, and they look identical from a screenshot:
 *
 *   1. injection never reached `sceCtrl`  -> a tooling problem, fixable here
 *   2. injection reached it and the game chose not to act -> a game fact
 *
 * Subscribing to the `input.buttons` broadcast settles it, because PPSSPP emits that
 * event on every `sceCtrl` change. If the event fires with our button, injection
 * worked and any remaining "nothing happened" is the GAME's answer, not a bug.
 *
 * This is the check that stops the whole "the emulator is ignoring input" rabbit hole
 * (window focus, bindings, --debugger flags) before it starts.
 *
 * Usage: psp-input-check.mjs [button]      (default: start)
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const button = process.argv[2] || "start";
const HOLD_FRAMES = 25;

const socket = new WebSocket(URL, "debugger.ppsspp.org");
let ticket = 1;
const events = [];

socket.addEventListener("message", (e) => {
  let m; try { m = JSON.parse(String(e.data)); } catch { return; }
  if (m.event === "input.buttons") {
    events.push(m);
    process.stdout.write(`  [broadcast] input.buttons buttons=${m.buttons} pressed=${m.pressed}\n`);
  }
});

await new Promise((res, rej) => {
  const t = setTimeout(() => rej(new Error(`connect timeout ${URL}`)), 8000);
  socket.addEventListener("open", () => { clearTimeout(t); res(); }, { once: true });
  socket.addEventListener("error", () => { clearTimeout(t); rej(new Error(`connect failed ${URL}`)); }, { once: true });
});

function send(event, fields = {}) {
  const tk = String(ticket++);
  socket.send(JSON.stringify({ event, ticket: tk, ...fields }));
  return new Promise((res, rej) => {
    const timer = setTimeout(() => rej(new Error(`${event} timed out`)), 20000);
    const h = (e) => {
      let m; try { m = JSON.parse(String(e.data)); } catch { return; }
      if (String(m.ticket) === tk) { clearTimeout(timer); socket.removeEventListener("message", h); res(m); }
    };
    socket.addEventListener("message", h);
  });
}

await send("version", { name: "OGA input check", version: "0.1.0" });
await send("broadcast.config.set", { disallowed: { logger: true } });
// ask for the input.buttons broadcast explicitly
try { await send("broadcast.config.set", { listen: { "input.buttons": true } }); } catch {}

const g = await send("game.status");
console.log(`game: ${g.game?.id} ${g.game?.title}`);
console.log(`pressing ${button} for ${HOLD_FRAMES} frames (~${Math.round(HOLD_FRAMES / 60 * 1000)} ms)`);
console.log("note: the request answers only AFTER the hold completes");

await new Promise(r => setTimeout(r, 300));
await send("input.buttons.press", { button, duration: HOLD_FRAMES });
await new Promise(r => setTimeout(r, 800));

console.log(`\nbroadcast events seen: ${events.length}`);
if (events.length === 0) {
  console.log("  NO BROADCAST. Either the subscription did not take, or injection never");
  console.log("  reached sceCtrl. Do NOT conclude the game ignores input from this alone —");
  console.log("  the subscription is the likelier failure. Confirm the game moved by");
  console.log("  comparing two screenshots instead.");
} else {
  const withBtn = events.filter(e => String(e.buttons || "").toLowerCase().includes(button));
  console.log(`  events naming ${button}: ${withBtn.length}`);
  console.log("  => injection REACHED the pad. If the screen did not change, that is the");
  console.log("     game's answer, not a tooling problem.");
}

socket.close();
