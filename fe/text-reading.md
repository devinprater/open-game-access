# FE11 text reading (boot → first map), V1

Goal: know what's on screen before reaching the map. Blind-playable
pre-map flow: title → main menu → difficulty → (hard submenu) →
prologue narration → prologue maps → chapter 1.

## Method: screenshot + OCR + fuzzy-match to exact RAM strings

Heap addresses shift between boots, so hardcoded RAM addresses are out.
Instead:

1. `fe/text-db.txt` — 2000+ exact UI strings extracted from RAM snapshots
   across the boot (menus at mm2, narration at snap-1400/2200, map text
   at mapA). Rebuild: `fe-textdb-all.sh` (needs fresh RAM snaps).
2. Capture both DS screens (`fedump` SHOT lines; `-other` files = bottom).
3. OCR with tesseract (host tool only): upscale 4x, crop the text box,
   threshold bright-on-dark (`fe-ocr.sh`); dark-on-parchment narration
   pages need `FE_INVERT=1`.
4. `scripts/fe-read.py` — F1-scored sliding-window match of OCR words
   against DB lines → prints the EXACT string (score ≥ ~0.4 = reliable).

## Proven coverage (host, 2026-09-20)

- Difficulty descriptions (Normal/Hard): exact, score ~0.65.
- Hard submenu header ("Choose an enemy level..."): exact, ~0.44.
- Prologue narration, both styles (blue box + parchment): exact, up to 0.80.
- Tutorial help ("Waiting"/Waiting…): exact, ~0.57.
- Title ("TOUCH TO START") + main menu ("New Game" only, no save):
  fixed strings, no RE needed.

## Not yet

- Selected-option tracking (heap structs shift; OCR the description box
  instead — the shown description IS the selection for difficulty).
- Difficulty card labels (stylized font, OCR-weak; fixed list
  Normal/Hard + Hard 1–5 submenu known).
- Button-icon glyphs (B/A) extract as blanks; substitute in reader later.
- Prologue map dialogs: same pipeline applies (dialog lines are in the
  DB); needs a full playthrough run to verify each box.
- Phone path: iOS Vision framework OCR on-device + bundled text-db
  (no tesseract on device). Not started; phone testing paused.
