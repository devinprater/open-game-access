# TASK9 — Lock-on state and EX-core objects

Context: same game/build (read-only static analysis, no ROMs, no emulator). ANSWER8/8B
battle chain is VALIDATED live (doc s112 — preserve): BM=[0x08B955A0], P0=[BM+0x14],
P1=[P0+0x2F0], S=[P+0x51C], HPmax=u16[S+8], dmg=u16[S+2], BRV=s16[S+0x0E],
base=s16[S+0x10], EX=float[S+0x14] (full 10000), pos=floats[P+0x80/84/88].

Player-confirmed mechanics (design contract, do not re-derive): L1 cycles a lock ring
enemy -> ex-core -> off -> enemy. Each state plays a DISTINCT game sound (no speech
needed for state changes); optional newcomer announcements come later. While locked, the
target stays centered; the app plays a low beep whose rate rises as distance closes
(beacon contract: adapter must expose lock-target identity + distance on demand).

## Ask

1. Lock-on state: where is the current lock target stored (holder + offsets)? How is
each state represented (enemy P? / ex-core object? / none)? What distinguishes them?
2. EX-core objects: where do spawned cores live (list holder + entry layout)? Position
floats? Claimed/consumed flag?
3. Minimal validator reads for both, same style as ANSWER8.

Two independent static references per claim. No invented offsets. Write ANSWER9.md.
