# Dissidia Quick Battle flow (host-mapped, ULUS10437)

Main menu row 3 Quick Battle -> player character select (same 10-hero
carousel as Triangle: WoL, Firion, Onion, Cecil, Bartz, Terra, Cloud,
Squall, Zidane, Tidus, wrap both ways; prompts Triangle Customization,
START Random, Square All Random).

Cross on a fighter -> "Play as this character?" YES/NO (YES default).
YES -> opponent select: same 10-hero carousel, magenta "Computer"
banner names the CPU pick. Left/Right/R/L all do nothing here; only
Up/Down. (Villains unselectable at this save's progression: both
carousels wrap Tidus->Warrior of Light. mx-quick6: 11 downs land
Tidus, WoL, Firion. qk3's "Garland" read was a VLM misread of WoL's
horned helmet; 4 follow-up shots all show WoL.)

Cross on a fighter -> "Fight against this character?" YES/NO.
YES -> Battle Setup (mx-quick8):
  Player banner (PPSSPP111111, Lv1 Warrior of Light, blue, left) vs
  COMPUTER banner (Firion Lv1, red, right); center stage "?" icon.
  Rows: Enter Battle, STAGE Random 1:12, STRENGTH(CPU) Average 5:9,
  LEVEL(CPU) Auto 1:2, BEHAVIOR(CPU) Random 1:9. Left/Right change
  values (fractions are positions in each value list). Triangle
  Customization, Square Defaults. Footer "Enter battle.", MANUAL START.

Cross on Enter Battle -> Opponent Info (mx-quick9): Lv1 Firion stats
TOTAL (BASE + bonus): HP 1368 (1000+368), CP 330, BRV 143 (95+48),
ATK 12, DEF 5 (13-8), LUK 10; gear Cracked Shield / Leather Hat /
Leather Armor; BATTLE BGM track name; NEXT (Cross) continues.

NEXT -> stage intro -> live battle (mx-quick10 @4000: HUD, EX Core,
BREAK occurring; Random picked Interdimensional Rift stage).
Idle player loses to CPU by ~8000 frames -> defeat menu with Rematch
(mx-quick10 @8000): Return to Battle Setup / Rematch /
Return to Character Selection / Return to Start Menu.

Rematch gives a repeatable Lv1 fight loop: ideal for beacon testing.
CharToggle reader covers both carousels (identical 10 rows); Battle
Setup rows and Opponent Info stats are unmapped (no RAM gate found yet).
