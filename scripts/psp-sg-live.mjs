#!/usr/bin/env node
/**
 * psp-sg-live.mjs — LIVE Steins;Gate dialogue reader.
 *
 * This is the host-side adapter. It polls the game's message log over PPSSPP's debugger
 * and prints each new line as it appears, with its speaker. No RAM dumps, no screenshots:
 * three 4-byte reads per tick.
 *
 * ⭐ HOW IT WORKS — every address and formula came from the game's decompiled code
 * (FUN_00050fc8 / FUN_00051574) and was verified against a live dump:
 *
 *   write_head  = u32(0x089797E8)     how many lines have been logged
 *   log_base    = u32(0x08978F14)     the record array (0 until the log is allocated)
 *   entry(i)    = log_base + ((write_head - (i - 1)) - 1) * 0xAC        i = 1 is NEWEST
 *   entry+0x1c  = speaker name
 *   entry+0x48  = the spoken line
 *
 * ⛔ THE LOG IS ALLOCATED LAZILY. `log_base` is 0 and `write_head` is 0 until the game
 * emits its first message. So the trigger is "write_head increased", and the very first
 * line is the transition 0 -> 1. Do not treat a null log_base as an error.
 *
 * ⛔ INDEX 1 IS THE NEWEST LINE, not the oldest. The walk counts BACK from the write
 * head. Reading asc ends up printing the prologue backwards.
 *
 * ⛔ THE FIELDS ARE FIXED-WIDTH AND NUL-PADDED. Split at the NUL before decoding.
 *
 * Usage:
 *   node psp-sg-live.mjs                    follow new lines
 *   node psp-sg-live.mjs --backlog 10       print the last 10 LOGICAL lines and exit
 *
 * ⚠ --backlog takes LOGICAL LINES, not records. Wrapped lines are stored across several
 * 0xAC records, so it walks back until it has collected that many complete lines.
 *   node psp-sg-live.mjs --speak            also emit each line to stdout for a TTS hook
 *   node psp-sg-live.mjs --json             machine-readable events, one JSON per line
 *   node psp-sg-live.mjs --auto circle      advance the story AND read it (one socket)
 *   node psp-sg-live.mjs --auto circle --tts "my-speak.sh"    SPEAK each new line
 */
import process from "node:process";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";

// ---- the verified globals ---------------------------------------------------
const G_WRITE_HEAD = 0x089797E8;
const G_LOG_BASE = 0x08978F14;
const G_CONFIG = 0x089B5E34;

const ST = 0xAC;          // record stride
const OFF_NAME = 0x1C;    // speaker name
const OFF_TEXT = 0x48;    // the spoken line
const NAME_LEN = 0x28;
const TEXT_LEN = 0x60;
const POLL_MS = 120;

// The engine's control pairs. ⛔ Delimiters, NOT text — leaving them in makes a reader
// speak raw bytes, and mis-reading them as speaker codes is a documented trap.
const CTRL = [
  [/\x81\x43/g, ", "],   // in-line break
  [/\x81\x67/g, ""],     // start of spoken text
  [/\x81\x65/g, "'"],  // ⭐ emphasis/quotes around a word: 0x81e ... 0x81f
  [/\x81\x66/g, "'"],  //    (unmapped, these print as raw 'e'/'f' letters)
  [/\x81\x6b/g, ""],     // start of speaker name
  [/\x81\x6c/g, ""],     // end of speaker name
  [/\x81\x68/g, ""],     // end of box
  // ⭐ 0x81 0xF4 — an inline emote/expression marker, seen when the story advances
  // ("Ooh, fried chicken! It looks so good!" + 81F4 + box-end). Unmapped it printed as a
  // stray 'ô'. It carries no speakable content, so drop it rather than guess a name.
  [/\x81\xf4/g, ""],
  [/%K%P/g, " "],        // page break — a space, never spoken
  [/%P/g, " "],
  [/%K/g, " "],
];

const args = process.argv.slice(2);
const flag = (n, d = null) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : d; };
const has = (n) => args.includes(`--${n}`);

const BACKLOG = flag("backlog") ? Number(flag("backlog")) : 0;
// --auto <button>  advance the story itself (press + poll on ONE debugger connection)
const AUTO = flag("auto");
// --tts <command>  run this command once per new line, with the line on stdin.
// Spawned SEQUENTIALLY (one at a time, awaited) so a long line is never talked over —
// a visual novel is conversational, and overlapping speech is unusable with a screen
// reader. The command receives plain text, already stripped of control codes.
const TTS = flag("tts");
const SPEAK = has("speak");
const JSON_OUT = has("json");

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

/**
 * Decode one fixed-width field.
 * ⭐ THE TRAILING SPACE IS PART OF THE DATA. The engine wraps at a fixed byte width and
 * keeps the word-boundary space at the END of the earlier chunk:
 *
 *   chunk A ends 'oment '      chunk B starts 'of our'   -> 'moment of our'
 *   chunk A ends 'understandi' chunk B starts 'ng.'      -> 'understanding.'
 *
 * Trimming that trailing space glues words together ("momentof", "Itis"); blindly adding
 * a space breaks cut words ("understandi ng."). Preserve the trailing space exactly, then
 * concatenate with no separator.
 */
function clean(buf) {
  const trailSpace = buf.length > 0 && buf[buf.length - 1] === 0x20;  // a genuine ASCII space
  let s = buf.toString("latin1");
  for (const [re, rep] of CTRL) s = s.replace(re, rep);
  const text = s.split(/\s+/).filter(Boolean).join(" ").replace(/^[,\s]+|[,\s]+$/g, "");
  return { text: trailSpace && text ? `${text} ` : text, trailSpace };
}

class Debugger {
  constructor(url = URL, timeoutMs = 15000) { this.url = url; this.timeoutMs = timeoutMs; this.pending = new Map(); this.nextTicket = 1; }
  async connect() {
    const s = new WebSocket(this.url, "debugger.ppsspp.org"); this.socket = s;
    s.addEventListener("message", (e) => this.#on(String(e.data)));
    s.addEventListener("close", () => this.#fail(new Error("disconnected")));
    s.addEventListener("error", () => {});
    await new Promise((res, rej) => {
      const t = setTimeout(() => rej(new Error("connect timeout")), 8000);
      s.addEventListener("open", () => { clearTimeout(t); res(); }, { once: true });
      s.addEventListener("error", () => { clearTimeout(t); rej(new Error("connect failed")); }, { once: true });
    });
    await this.request("version", { name: "OGA SG live", version: "0.1.0" });
  }
  #on(d) { let m; try { m = JSON.parse(d); } catch { return; }
    if (m.ticket != null && this.pending.has(String(m.ticket))) {
      const e = this.pending.get(String(m.ticket)); this.pending.delete(String(m.ticket));
      clearTimeout(e.timer); m.event === "error" ? e.reject(new Error(m.message)) : e.resolve(m);
    } }
  #fail(err) { for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(err); } this.pending.clear(); }
  request(event, fields = {}) {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.reject(new Error("not connected"));
    const tk = String(this.nextTicket++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.pending.delete(tk); rej(new Error(`${event} timed out`)); }, this.timeoutMs);
      this.pending.set(tk, { resolve: res, reject: rej, timer });
      this.socket.send(JSON.stringify({ event, ticket: tk, ...fields }));
    });
  }
  async read(address, size) {
    const r = await this.request("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
  async u32(address) { const b = await this.read(address, 4); return b.length === 4 ? b.readUInt32LE(0) : 0; }
  close() { try { this.socket?.close(); } catch {} }
}

/// Read log entry `index` (1 = newest). Returns {speaker, text, nameOnly} or null.
async function entry(db, logBase, writeHead, index, capacity) {
  if (index === 0 || writeHead === 0 || logBase === 0) return null;
  const k = (writeHead - (index - 1)) - 1;
  if (k < 0 || k >= capacity) return null;
  const a = logBase + k * ST;
  const nmRaw = await db.read(a + OFF_NAME, NAME_LEN);
  const txRaw = await db.read(a + OFF_TEXT, TEXT_LEN);
  // ⛔ SPLIT AT THE NUL FIRST — the fields are NUL-padded fixed width.
  const cut = (b) => b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0));
  const nmF = clean(cut(nmRaw));
  const txF = clean(cut(txRaw));
  const nm = nmF.text, tx = txF.text;
  // ⛔ DROP records that decode to nothing, and records with no letters or digits. The log
  // contains blank spacer records and stray control/box-drawing bytes that decode to junk
  // like '¥§' which would otherwise be spoken aloud.
  if (!/[A-Za-z0-9]/.test(nm + tx)) return null;
  // ⭐ FIELD PRESENCE DECIDES, NOT LENGTH.
  // The old rule was `nm.length > tx.length`, which MISREADS a short line as narration:
  //   name='Kurisu'  text='...!'    -> became text "Kurisu", speaker none
  //   name='Rintaro' text='Nope.'   -> became narration "Rintaro"
  // Two consecutive short lines then merged into the nonsense line "RintaroKurisu".
  // ⭐ A record with a name and NO text is a long name plate stored on its own
  // ('Graceful...Girl?', 'Cat-Eared Girl') — it is not a line, and its speaker applies to
  // the NEXT record. Return it as nameOnly so the caller can carry it forward.
  if (nm && !tx) return { speaker: nm, text: "", nameOnly: true };
  return { speaker: nm || null, text: txF.text, nameOnly: false };
}

/**
 * ⭐ MERGE WRAPPED RECORDS INTO LOGICAL LINES.
 * A long line is stored as SEVERAL 0xAC records: the FIRST carries the speaker name plate,
 * and the following ones are bare continuations with no name. Printing them one per line
 * chops sentences mid-word:
 *
 *   Rintaro: Taking the known as known, and the unkn
 *   own as unknown -- this is true understandi
 *   ng.
 *
 * Rule: a record with NO name is a continuation of the previous line. Every logical line
 * starts with a record that has a name plate (narration uses '???' as a placeholder), so
 * this is unambiguous — observed across the whole prologue.
 */
function join(lines) {
  const out = [];
  let pendingSpeaker = null;
  for (const l of lines) {
    // ⭐ A LONG NAME PLATE ARRIVES AS ITS OWN RECORD. Remember it; it belongs to the next
    // record that actually carries text.
    if (l.nameOnly) { pendingSpeaker = l.speaker; continue; }
    const prev = out[out.length - 1];
    // ⭐ THE LINE IS CLOSED WHEN THE ACCUMULATED TEXT REACHES A SENTENCE END.
    // The engine wraps at a fixed byte width, so a long line arrives as several chunks,
    // and a wrapped chunk stops MID-SENTENCE — the full stop only appears in the last
    // chunk. So keep appending while the accumulated text has not ended a sentence.
    //   '...he can\'t ' + 'swim.'       -> ends '.' -> closed
    //   '...true understandi' + 'ng.'   -> ends '.' -> closed
    //   '...the moment ' + 'of our ...' -> no terminal yet -> keep appending
    const open = prev && !prev.closed;
    if (open) {
      // Concatenate with NO separator — the engine keeps the word-boundary space at the
      // END of the earlier chunk ('moment ' + 'of our' -> 'moment of our').
      // Do NOT trim inside the loop: a line wrapped 3x merges twice, and trimming after
      // the first merge deletes the boundary space the second merge needs ("can'tswim").
      prev.text = (prev.text + l.text).replace(/[ \t]{2,}/g, " ");
      if (!prev.speaker && (l.speaker || pendingSpeaker)) prev.speaker = l.speaker || pendingSpeaker;
    } else {
      out.push({
        speaker: l.speaker || pendingSpeaker || null,
        text: l.text,
        closed: false,
      });
    }
    pendingSpeaker = null;
    const cur = out[out.length - 1];
    if (isSentenceEnd(cur.text)) cur.closed = true;
  }
  // Trim once, now that the logical lines are complete.
  for (const l of out) l.text = l.text.trim();
  return out;
}

/**
 * Does this text finish a sentence (so the logical line is complete)?
 * Covers '.', '!', '?' including '...' and '?!', optionally followed by a closing quote
 * or bracket. A trailing comma/colon/dash means the sentence continues.
 */
function isSentenceEnd(s) {
  return /[.!?…][\"')\]]*$/.test(s.trim());
}

/**
 * Speak one line through the user's own TTS command.
 * ⭐ THE SINK IS PLUGGABLE ON PURPOSE: the project rule is to route speech to the user's
 * chosen engine and never override their voice/rate/pitch. Piping to an external command
 * keeps that promise — whatever they point it at is what speaks, with their settings.
 * The line arrives on stdin (UTF-8), so the command can be a script, `say`, SAPI via
 * PowerShell, a screen-reader CLI, anything.
 * Speaker and text are joined so a listener hears who is talking, which is the whole
 * point of a visual novel reader.
 */
// ⭐ A REAL SERIAL QUEUE. `emit()` is called synchronously in a loop, and the earlier
// version serialised by awaiting a single shared `speaking` promise. That only chains ONE
// level deep: calls 2..N all awaited the SAME promise, and when it resolved they resumed
// together and all spawned at once. Every line was spoken, but OUT OF ORDER — which with a
// screen reader is worse than not speaking, because the story is scrambled.
// Chaining each task onto a tail promise keeps strict order.
let ttsQueue = Promise.resolve();
function speak(speaker, text) {
  if (!TTS || !text) return;
  const payload = speaker && speaker !== "???" ? `${speaker}: ${text}` : text;
  ttsQueue = ttsQueue.then(() => new Promise((resolve) => {
    // shell:true so a user can pass a pipeline like "espeak -v en" or a path with args.
    import("node:child_process").then(({ spawn }) => {
      const ch = spawn(TTS, { shell: true, stdio: ["pipe", "inherit", "inherit"] });
      ch.on("error", (e) => { console.error(`!! tts failed: ${e.message}`); resolve(); });
      ch.on("close", () => resolve());
      ch.stdin.on("error", () => {});
      ch.stdin.end(payload + "\n", "utf8");
    }).catch(() => resolve());
  }));
  // Never let a failed sink poison the queue for the rest of the scene.
  ttsQueue = ttsQueue.catch(() => {});
  return ttsQueue;
}

function emit(speaker, text) {
  if (JSON_OUT) { console.log(JSON.stringify({ speaker, text })); }
  else console.log(speaker ? `${speaker}: ${text}` : text);
  // Fire and forget: emit() is called from the poll loop and must not block it, but
  // speak() serialises internally so lines stay in order.
  void speak(speaker, text);
}

async function main() {
  const db = new Debugger();
  await db.connect();

  // capacity lives in the loaded SYSTEM.CFG: config + 0x18
  let capacity = 512;
  const cfg = await db.u32(G_CONFIG);
  if (cfg) { const c = await db.read(cfg + 0x18, 2); if (c.length === 2) capacity = c.readUInt16LE(0); }

  try {
    if (BACKLOG) {
      const wh = await db.u32(G_WRITE_HEAD), lb = await db.u32(G_LOG_BASE);
      if (!wh || !lb) { console.error("no message log yet — the game has not emitted a line"); return 1; }
      // ⚠ --backlog is LOGICAL LINES, not records: one line may span several wrapped
      // records, so walk back further than BACKLOG records and trim afterwards.
      // ⛔ ORDER MATTERS FOR join(). The walk goes newest -> oldest, but join() assumes
      // CHRONOLOGICAL order (a record with no name is a continuation of the one BEFORE
      // it). Collecting newest-first and joining directly makes a continuation look like
      // a line start, which truncated lines mid-sentence. Collect, then REVERSE.
      // ⛔ INDEX 1 IS THE NEWEST RECORD and index increases going BACK in time
      // (k = wh - i). So walk i ASCENDING from 1 to collect newest-first, then reverse
      // into chronological order for join(). Walking i downward collects the OLDEST
      // records and scrambles the output.
      const newestFirst = [];
      // Ask for more records than lines wanted — wrapping means lines < records.
      const recordBudget = Math.min(wh, BACKLOG * 4 + 8);
      for (let i = 1; i <= recordBudget; i++) {
        const e = await entry(db, lb, wh, i, capacity);
        if (e) newestFirst.push(e);
      }
      const chrono = join(newestFirst.reverse());
      for (const l of chrono.slice(-BACKLOG)) emit(l.speaker, l.text);
      return 0;
    }

    if (!JSON_OUT) {
      console.error(`watching 0x${G_WRITE_HEAD.toString(16).toUpperCase()} every ${POLL_MS} ms`);
      console.error("(the log is allocated lazily: head 0 means no line has been logged yet)");
      console.error("");
    }

    let last = await db.u32(G_WRITE_HEAD);
    let announcedStart = false;
    for (;;) {
      // ⭐ --auto ADVANCES THE STORY ITSELF. This must run on the SAME connection as the
      // polling: PPSSPP's debugger is effectively one client, so a separate presser
      // process competes with the follower and the follower silently sees nothing.
      // In tests, presses from another process moved the write head while the follower
      // printed NOTHING, which reads as "the trigger is broken" rather than "the presser
      // was on another socket".
      if (AUTO) {
        try { await db.request("input.buttons.press", { button: AUTO, duration: 20 }); } catch {}
        if (POLL_MS < 400) await sleep(60);
      }
      await sleep(POLL_MS);
      const wh = await db.u32(G_WRITE_HEAD);
      if (wh === last) continue;

      const lb = await db.u32(G_LOG_BASE);
      if (!announcedStart && last === 0 && wh > 0) {
        if (!JSON_OUT) console.error(`[log allocated after ${wh} line(s)]`);
        announcedStart = true;
      }
      // Emit every line between last and wh, oldest first, so nothing is skipped when
      // several arrive within one poll interval. join() merges wrapped records.
      const fresh = [];
      for (let i = wh - last; i >= 1; i--) {
        const e = await entry(db, lb, wh, i, capacity);
        if (e && e.text) fresh.push(e);
      }
      for (const l of join(fresh)) emit(l.speaker, l.text);
      last = wh;
    }
  } finally { db.close(); }
}

main().catch(e => { console.error(`!! ${e.message}`); process.exit(1); });
