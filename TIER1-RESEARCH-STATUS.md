# Tier-1 games — memory-mapping and research status

The four most tractable titles from `NDS-GAME-FEASIBILITY.md`, taken in order of
how easily a reader could be built. What follows distinguishes **verified** from
**claimed**, because a cheat-code list is a claim and internet claims about RAM
addresses are routinely stale, region-specific, or mistyped.

## Method

Action Replay codes name **absolute RAM addresses** — exactly what an
accessibility reader needs, and the only public memory documentation most of these
games have. So they were used as the starting point and then checked against a
live console:

- `scripts/verify-cheats.sh` — reads each claimed address every 60 frames for a
  full boot-and-play run and reports first/last value, how many distinct values it
  took, and how often it changed.
- `scripts/ramscan.cpp` / `scripts/region-scan.sh` — when a claimed address reads
  zero, scans the surrounding window to distinguish "the game had not allocated
  this yet" from "this address is wrong for this ROM".

A right address holds a plausible value for its meaning *and moves when the game
moves*; a wrong one holds 0 forever or holds noise. **Reading zero for a whole run
proves only that no code touched that byte during the window** — it is not proof
the address is wrong, and this distinction matters below.

## 1. Dragon Ball Z: Attack of the Saiyans (Monolith Soft, US `BRPE`)

**Guides — excellent.** GameFAQs has multiple complete walkthroughs
(DarkstarRipclaw's is marked *Final*, 2011; Herms98's is incomplete). The full
guide documents a **bestiary of all 150 monsters**, item lists, the scouting/capture
systems, and the mission structure — that is close to a machine-readable spec of
the game's state, which is unusual and very valuable. There is also a complete
ROM-hack project (ChronoCrash, v1.022).

**Memory — claimed, NOT yet verified.** Published Action Replay addresses:
money `020CC370`, bonus points `020CC84C`, AP `020CD4A4`, battle HP/AP pairs
around `020CD300`/`020CD308`, consumables at `020CC3FC`, capsules `020CC498`.

**Verified finding:** the region `020CC000–020CDFFF` is only **2.2% populated**
(182 nonzero bytes of 8192) after 6000 frames, and every specifically claimed
address read 0 for the whole run. The live bytes there are **pointers** into RAM
(`020CC774 = 44 DD 0C 02` → `020C0DD4`), not the flat globals the codes imply.

**Reading:** these codes are widely republished for the **Europe** release; this
file is the **US** release. A pointer table at the claimed global offsets is
consistent with a version/region mismatch, not with "the game has no state yet".
**Next step is a 20-minute job, not a research project:** scan for the money value
live (set it, find it, change it, confirm the address moves) — the standard
cheat-search loop. Do this before writing any reader.

## 2. Bleach: The 3rd Phantom (Tom Create, US `YBTE`)

**Guides — thin.** No complete GameFAQs walkthrough surfaced. What exists is a
translation wiki (`bl3rd.wikidot.com`) which usefully lists the game's *data files
by name* (`db_skill_name.bin`, etc.), a GBAtemp ROM-hack thread, and cheat lists.
Expect to work from the game's own data rather than from a guide.

**Memory — VERIFIED against a live console.** This is the strongest result of the
four. The character stat block was confirmed as a **struct array with stride
`0x13C`**, exactly as the codes claim:

| Offset in struct | Meaning | Live value observed |
|---|---|---|
| +0x00 | level | `01` |
| +0x16 | current HP | `0x0161` = **353**, changing |
| +0x18 | max HP | `0x0161` = **353** |
| +0x1C | SP | `0x18` = **24** |
| +0x2A | stat pair | `0x0B0A` = **2826** |

Character 0 at `021DB446`, character 1 at `021DB582` (= +0x13C) with **294** HP —
a different, plausible value. The struct base is `021DB430`, and
`021DB41C = 7F` / `021DB42E = FFFFFFFF` sit just before it, consistent with a
live actor array.

Two traps this exposed, both worth remembering: "current HP" and "max HP" read
**identical** at this point (353/353) — a reader must not treat equal values as
one being a duplicate of the other; and a stride that reproduces across indices is
what proves it is the real record layout rather than a lucky hit.

**This is a working memory map for the party stats**, i.e. family B is already
solved for the battle screens. And the precedent to copy is a close one:
**StanHash's GBA Fire Emblem for Screen Readers** is a Lua script suite doing
exactly this for a tactics game, including its own `LuaUtilities` tooling.

## 3. Dragon Ball: Origins 2 (Game Republic, US `BDBE`)

**Guides — good.** GameFAQs has a full FAQ/walkthrough (jellybob8), plus
chapter-specific board threads and chest listings.

**Memory — claimed, NOT verified, and currently the weakest of the four.**
Published addresses: zenny `0210A200`, skill points `0210A204`, episodes
`0210A31C` (these are for the **Europe** release, `BDBP`). The US run read **all
three as 0 for the whole 5000-frame run**. The surrounding window is 13.7%
populated — busier than the DBZ region, but the live bytes there are **pointers**
(`0210A008 = 14 A0 10 02` → `0210A014`), not the flat values the codes describe.
Same likely cause as #1: a Europe-specific list applied to a US ROM.

Because this game's *action* portion is family D (real-time combat), the value of
mapping it is lower than #1 and #2 — a reader here would cover menus, story and
shops, and go quiet during the fighting.

## 4. Dragon Ball Z: Harukanaru Densetsu (Bec, US `A5LE`)

**Guides — good for the card system specifically.** DSSingleCard.com publishes a
**complete list of action types and their effects** and a second list of the card
events; GameFAQs has a Capsule Guide and general walkthroughs. For a card game
that is the important half, because the reader has to name cards and their effects.

**Memory — no public addresses found.** No Action Replay list and no RAM map
surfaced for this title; only a "quick level up" code. This one needs the mapping
done from scratch — which is more work than #1–#3, but the game being turn-based
means the payoff is the same shape as #1.

## Summary

| Game | Guide quality | Public memory docs | Verified here |
|---|---|---|---|
| DBZ: Attack of the Saiyans | ★★★ complete, incl. 150-monster bestiary | AR codes (EU-tagged) | Region busy but **claimed addresses read 0** — needs a live value scan |
| Bleach: The 3rd Phantom | ★ thin (translation wiki w/ data file names) | AR codes (US) | **struct array verified**: stride 0x13C, HP/level/SP confirmed live |
| DB Origins 2 | ★★ full walkthrough | AR codes (EU) | **claimed addresses read 0** on US |
| DBZ: Harukanaru Densetsu | ★★★ card/action lists | **none found** | not attempted — nothing to verify |

**Recommendation unchanged, with one refinement.** DBZ: Attack of the Saiyans is
still the best first project because its guide is effectively a spec of the game's
state. But **Bleach: The 3rd Phantom is now the one with a proven memory map** —
its party stat block is verified, so a battle-screen reader has a real foundation
today. If the goal is "get something audible fastest", start there; if the goal is
"build the most useful mod", start with Attack of the Saiyans and spend the first
session on the live value scan.

**One correction to carry forward:** for both zero-reading titles, the honest
statement is *"the published addresses do not hold those values on this ROM"* —
not *"the addresses are wrong"*. The region-level evidence (pointers at the
claimed offsets, EU-tagged code lists, different region hash) points at a
region/version mismatch, and that is fixable by re-running the code search against
the right release.
