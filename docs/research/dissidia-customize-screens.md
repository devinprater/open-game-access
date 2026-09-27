# Dissidia per-character Customize screens (host-mapped, ULUS10437)

Entry: character select Cross on a fighter -> "Check abilities and
equipment?" YES/NO (YES default) -> YES opens this flow for that
character. All below verified live on Warrior of Light Lv1 (mx-charcust*,
mx-abil1-12). No RAM signatures found for any of it (glyph UI); a future
reader would be input-echo like CustToggle/CharToggle.

## Tab ring (L/R shoulders, 4-cycle, cursor resets to list top on switch)

R-direction: Abilities -> Equipment -> Accessories -> Summons -> Abilities.
L goes the reverse. Tab hint text names both neighbors.

## Abilities tab

Left pane is a scrolling tree; right pane shows the selected node's list.

Parents (Down/Up 1:1, wrap assumed, Offensive->Basic verified):
  Offensive Abilities, Basic Abilities, Default, Remove All, List.
Cross on an expandable parent opens its children with cursor on first
child. Circle on a child collapses back to the parent.

Children cycle with wrap (NOT toggle):
  Offensive: Bravery Attacks <-> HP Attacks (2-cycle, 6-run proof).
  Basic: Actions -> Support -> Extra -> Actions (3-cycle, proven).
  Default: children unknown (not yet opened).

Right-pane lists seen:
  Bravery: LAND Dayflash 30 / Red Fang 20 (+1 locked), AIR Crossover 30.
  HP: LAND Shield of Light (ground) 40 / Shining Wave 40, AIR Shield of
  Light (midair) 40. CP 300/330, SET A/B/C (SELECT swaps, untested).
  Basic/Actions: Ground/Midair Evasion/Block 10 each, Aerial Recovery 10,
  Free Air Dash 30. Support: Always Target Indicator 10, EX Core Lock On
  10. Extra: empty at Lv1.

Left/Right dpad do nothing in the tree. Remove All / List behavior
unmapped. Per-slot focus in the right pane unmapped (how Cross enters it).

## Summons tab

Left list (cursor Equip): Equip, Unequip, Set reserves, Clear reserves.
Right: 10 empty summon slots, REMAINING USES column.

## Equipment tab

CURRENT EQUIPMENT: 4 empty slots Weapon / Hand / Head / Body.
Commands: Equip, Unequip, Remove All, Optimize (cursor Equip).
Stats panel TOTAL (BASE + bonus): HP 1000, CP 330, BRV 95, ATK 11,
DEF 14, LUK 10 (Lv1 naked WoL; numeric glyphs, no RAM gate yet).

## Accessories tab

3 empty CATEGORY/RANK slots. Commands: Equip, Unequip, Remove All
(cursor Equip). Same stat panel shape as Equipment.

## Open mapping questions

1. Default's children (Cross on Default).
2. Remove All / List: confirm dialog or instant action?
3. Right-pane slot focus: which button moves focus right, slot order?
4. Square command chart: what does it show?
5. SELECT set swap: does cursor reset?
6. Stat values per character/level: any RAM source, or textures only?
7. Does tab-switch always reset cursor to list top (seen twice)?
