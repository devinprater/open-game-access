# TASK8B — Battle-holder correction (follows TASK8/ANSWER8)

Live check during an ACTIVE battle (fighters on screen, effects firing): RAM
0x08B915A0 reads 0x00000000, and its whole +-0x40 neighborhood is zeros (real zero
bytes, not read errors). Either DAT_003915A0 is BSS that this battle mode never sets,
or the bias/region is wrong for that symbol. (Bias +0x08800000 itself is proven live:
file 0x00394940 == RAM 0x08B98940 board holder.)

Also searched live RAM for the Opponent-Info enemy signature (HP max 1000 with dmg 662
at -6, per the pre-battle menu "HP 338 (1000 - 662)"): zero hits. And bare u16==1000
appears 465x (asset noise).

## Ask

Re-derive the fighter-list holder: who WRITES the fighter list (constructor/inserter
callers), and from which battle-mode init path? Give the corrected holder + chain, or
name the battle modes that use different managers. Two independent static references per
claim. Append the correction to ANSWER8.md (new section, keep the old).
