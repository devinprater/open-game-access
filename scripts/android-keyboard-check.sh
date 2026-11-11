#!/usr/bin/env bash
# android-keyboard-check.sh — assert a physical keyboard can drive a session.
#
# ControllerInput.kt routes a pad's keys; a physical keyboard (the docked
# player's controller) joins the same funnel through its own device test
# and mapping. No host test compiles that file, and a dropped mapping row
# fails SILENTLY: the key is simply dead in-game and nothing anywhere
# reports it. So this script pins the behaviour instead — the acceptance
# gate and every mapping row, anchored on the emitted code lines.
#
# ⛔ Assert on the ACTUAL CODE LINES, not on prose or bare keycode names.
# A bare grep for "KEYCODE_Z" also matches the class doc's table and the
# keyboardKeyCodes set, so deleting the routing row would still pass —
# the same "gate that cannot fail" bug android-jit-check.sh documents.
# Every row assertion below is scoped to the body of the function that
# must carry it and anchors on its full emitted line, and the script is
# run once against mutated copies to prove a missing row is detected.
#
# The mapping (Devin's, 2026-11-11): arrows move; Z/X/A/S are the face
# diamond (south/east/west/north); Q/W are L1/R1; Enter is Start; J/K/L
# are the reader's previous/current/next item (Shift+J/L change
# category), P is Find path; Space is Where Am I and Tab is Menu
# Status; Shift tapped alone is Select. There are no keyboard modifier
# keys (no E/R): the reader keys perform the pad's chord commands
# directly, through the same routeChord/chord path the pad uses.
#
# Usage: scripts/android-keyboard-check.sh [repo-root]
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
fail=0
ok()  { printf 'ok: %s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*" >&2; fail=1; }

CI="$ROOT/app/src/main/java/com/devin/opengameaccess/ControllerInput.kt"

# Function bodies, extracted so row greps are scoped: the pad's when
# carries some identical lines (the D-pad rows), and only a scoped grep
# proves the KEYBOARD row exists. A body runs from its signature line
# to the first closing brace at the object's indent.
body_of() { awk -v pat="$2" 'index($0, pat) {f=1} f {print} f && /^    }$/ {exit}' "$1"; }
kb_set()  { awk '/private val keyboardKeyCodes = setOf\(/,/^    \)$/' "$1"; }

row() { # row <body> <line> <label>
  if printf '%s' "$1" | grep -qF "$2"; then ok "$3"; else bad "$3 — row missing or changed: $2"; fi
}
norow() { # norow <body> <regex> <label>
  if printf '%s' "$1" | grep -qE "$2"; then bad "$3 — found: $2"; else ok "$3"; fi
}

if [ ! -f "$CI" ]; then
  bad "app/src/main/java/com/devin/opengameaccess/ControllerInput.kt is missing"
  echo; exit 1
fi

KB="$(body_of "$CI" 'private fun handleKeyboardKeyEvent')"
RK="$(body_of "$CI" 'private fun readerKey')"
IK="$(body_of "$CI" 'private fun itemKey')"
SK="$(body_of "$CI" 'private fun shiftKey')"
SET="$(kb_set "$CI")"
[ -n "$KB" ]  || bad "handleKeyboardKeyEvent is missing from ControllerInput.kt"
[ -n "$RK" ]  || bad "readerKey is missing from ControllerInput.kt"
[ -n "$IK" ]  || bad "itemKey is missing from ControllerInput.kt"
[ -n "$SK" ]  || bad "shiftKey is missing from ControllerInput.kt"
[ -n "$SET" ] || bad "keyboardKeyCodes is missing from ControllerInput.kt"

echo "== a keyboard is accepted on its own device test, in-session only"
grep -qF 'private fun InputDevice.isKeyboard(): Boolean =' "$CI" \
  && ok "the keyboard has its own device test (it is not filed as a controller)" \
  || bad "InputDevice.isKeyboard is missing: keyboards have no acceptance test of their own"
grep -qF '(sources and InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD' "$CI" \
  && ok "the keyboard test matches the complete source class (SOURCE_KEYBOARD shares bit 0 with the gamepad class)" \
  || bad "the keyboard test is not a complete-mask match: a nonzero test misfiles pads and keyboards in both directions"
grep -qF 'device != null && device.isKeyboard()' "$CI" \
  && ok "acceptance requires a real device (no device-less keyboard path for stray key events)" \
  || bad "isKeyboardEvent does not require a device: stray events claiming the keyboard source would be swallowed"
grep -qF 'if (isKeyboardEvent(event.device)) return handleKeyboardKeyEvent(event)' "$CI" \
  && ok "non-pad key events route to the keyboard handler" \
  || bad "handleKeyEvent never routes to handleKeyboardKeyEvent: keyboard support is present but unreachable"
grep -qF 'if (!sessionActive) return false' "$CI" \
  && ok "the funnel refuses all input outside a session (keyboards included: the gate precedes routing)" \
  || bad "the session gate is missing from handleKeyEvent"
grep -qF '.filter { it.isGameController() }' "$CI" \
  && ok "connectedName still scans game controllers only: a keyboard never hides the on-screen pad" \
  || bad "refresh() no longer filters to game controllers: a keyboard could be named the connected controller"

echo
echo "== directions: the arrow keys"
row "$KB" 'KeyEvent.KEYCODE_DPAD_UP -> { direction(PadDir.UP, source, pressed); true }' "arrow up moves up"
row "$KB" 'KeyEvent.KEYCODE_DPAD_DOWN -> { direction(PadDir.DOWN, source, pressed); true }' "arrow down moves down"
row "$KB" 'KeyEvent.KEYCODE_DPAD_LEFT -> { direction(PadDir.LEFT, source, pressed); true }' "arrow left moves left"
row "$KB" 'KeyEvent.KEYCODE_DPAD_RIGHT -> { direction(PadDir.RIGHT, source, pressed); true }' "arrow right moves right"

echo
echo "== the face diamond, by position: Z south, X east, A west, S north"
row "$KB" 'KeyEvent.KEYCODE_Z -> { faceButton(Face.SOUTH, source, pressed); true }' "Z is the south face button"
row "$KB" 'KeyEvent.KEYCODE_X -> { faceButton(Face.EAST, source, pressed); true }' "X is the east face button"
row "$KB" 'KeyEvent.KEYCODE_A -> { faceButton(Face.WEST, source, pressed); true }' "A is the west face button"
row "$KB" 'KeyEvent.KEYCODE_S -> { faceButton(Face.NORTH, source, pressed); true }' "S is the north face button"

echo
echo "== shoulders and Start"
row "$KB" 'KeyEvent.KEYCODE_Q -> { set(source, emuButton(GbKeys.L, PspKeys.L, MesenKeys.L, GenesisKeys.X), pressed); true }' "Q is L1 (the pad's L1 targets)"
row "$KB" 'KeyEvent.KEYCODE_W -> { set(source, emuButton(GbKeys.R, PspKeys.R, MesenKeys.R, GenesisKeys.Z), pressed); true }' "W is R1 (the pad's R1 targets)"
row "$KB" 'KeyEvent.KEYCODE_ENTER -> { set(source, emuButton(GbKeys.START, PspKeys.START, MesenKeys.START, GenesisKeys.START), pressed); true }' "Enter is Start"

echo
echo "== Shift: Select on an unused tap, category modifier with J/L"
row "$KB" 'KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> { shiftKey(source, pressed); true }' "Shift routes to its own two-job handler"
row "$SK" 'val button = emuButton(GbKeys.SELECT, PspKeys.SELECT, MesenKeys.SELECT, GenesisKeys.MODE)' "the Shift tap means Select (the pad's Select targets)"
row "$SK" 'if (shiftHeld.isEmpty() && shiftTapArmed) {' "Select fires only when the last Shift comes up with its tap still armed"
row "$SK" 'set(source, button, true)' "the tap presses Select"
row "$SK" 'set(source, button, false)' "the tap releases Select (pressed and released at once)"
row "$IK" 'shiftTapArmed = false' "using Shift with J/L spends the tap: no Select fires afterwards"

echo
echo "== the reader's keys perform the pad's chord commands directly"
row "$RK" 'val held = if (ask) askHeld else browseHeld' "readerKey picks the ask or browse held set, like a held trigger"
row "$RK" 'held.add(source)' "readerKey registers the hold for the routing call"
row "$RK" 'val consumed = routeChord(input, source, pressed)' "readerKey routes through the pad's own chord path (the command is the chord's, by construction)"
row "$RK" 'held.remove(source)' "readerKey drops the hold when the call returns (nothing strands)"
row "$KB" 'KeyEvent.KEYCODE_J -> { itemKey(previous = true, event, source, pressed); true }' "J is previous item (previous category with Shift)"
row "$KB" 'KeyEvent.KEYCODE_L -> { itemKey(previous = false, event, source, pressed); true }' "L is next item (next category with Shift)"
row "$IK" 'readerKey(Input.Pad(if (previous) PadDir.LEFT else PadDir.RIGHT), ask = false, source, pressed)' "plain J/L are the browse chords' item commands (browse + D-pad left/right)"
row "$IK" 'readerKey(Input.Pad(if (previous) PadDir.UP else PadDir.DOWN), ask = false, source, pressed)' "Shift+J/L are the browse chords' group commands (browse + D-pad up/down)"
row "$KB" 'KeyEvent.KEYCODE_K -> { readerKey(Input.FaceBtn(Face.SOUTH), ask = false, source, pressed); true }' "K re-speaks the current item (the browse chord's south command)"
row "$KB" 'KeyEvent.KEYCODE_P -> { readerKey(Input.FaceBtn(Face.WEST), ask = true, source, pressed); true }' "P finds the path to the item (the ask chord's west command)"
row "$KB" 'KeyEvent.KEYCODE_SPACE -> { if (pressed) perform(Slot.WHERE_AM_I); true }' "Space asks Where Am I (press-only, like L3)"
row "$KB" 'KeyEvent.KEYCODE_TAB -> { if (pressed) perform(Slot.MENU_STATUS); true }' "Tab asks Menu Status (press-only, like R3) and is consumed, so focus stays put"

echo
echo "== dropped keys stay dropped: no E/R modifiers, no M, no WASD movement"
norow "$KB" 'KeyEvent\.KEYCODE_[ER] ->' "no E or R row routes anything (the keyboard has no modifier keys)"
norow "$SET" 'KeyEvent\.KEYCODE_[ER],' "neither E nor R is in the handled set"
norow "$KB" 'KeyEvent\.KEYCODE_M ->' "no M row (Menu Status is Tab)"
norow "$SET" 'KeyEvent\.KEYCODE_M,' "M is not in the handled set"

echo
echo "== key repeats: mapped keys are consumed silently, the rest fall through"
if printf '%s' "$KB" | grep -qF 'return event.keyCode in keyboardKeyCodes'; then
  ok "the keyboard handler consumes repeats of mapped keys only"
else
  bad "the keyboard repeat guard is missing: held keys would re-fire chords, or unmapped repeats get swallowed"
fi
for code in KEYCODE_Z KEYCODE_X KEYCODE_A KEYCODE_S KEYCODE_Q KEYCODE_W KEYCODE_J KEYCODE_K KEYCODE_L KEYCODE_P KEYCODE_SPACE KEYCODE_TAB KEYCODE_ENTER KEYCODE_SHIFT_LEFT KEYCODE_SHIFT_RIGHT; do
  if printf '%s' "$SET" | grep -qF "KeyEvent.$code"; then ok "$code is in the handled set"
  else bad "$code is not in keyboardKeyCodes: its repeats would fall through unconsumed"; fi
done

echo
echo "== negative control: removing a row or the route must FAIL this check"
# Inline, like android-jit-check.sh's control: mutate throwaway copies and
# show the same scoped greps come up empty. Do NOT re-run this script
# against the copies (that re-runs the control and never terminates).
TMP="$(mktemp -d)"; mkdir -p "$TMP/app/src/main/java/com/devin/opengameaccess"
MUT="$TMP/app/src/main/java/com/devin/opengameaccess/ControllerInput.kt"
sed '/KEYCODE_SPACE ->/d' "$CI" > "$MUT"
if body_of "$MUT" 'private fun handleKeyboardKeyEvent' | grep -qF 'KeyEvent.KEYCODE_SPACE -> { if (pressed) perform(Slot.WHERE_AM_I); true }'; then
  bad "the control mutation did not remove the Space row"
else
  ok "deleting the Space row is detectable (the row assertion anchors on the emitted line)"
fi
sed 's|return handleKeyboardKeyEvent(event)|return false|' "$CI" > "$MUT"
if grep -qF 'if (isKeyboardEvent(event.device)) return handleKeyboardKeyEvent(event)' "$MUT"; then
  bad "the control mutation did not remove the keyboard route"
else
  ok "deleting the keyboard route is detectable"
fi
sed 's|KeyEvent.KEYCODE_TAB -> { if (pressed) perform(Slot.MENU_STATUS); true }|KeyEvent.KEYCODE_E -> { setModifier(askHeld, source, pressed); true }|' "$CI" > "$MUT"
if body_of "$MUT" 'private fun handleKeyboardKeyEvent' | grep -qE 'KeyEvent\.KEYCODE_[ER] ->'; then
  ok "re-adding an E/R modifier row is detectable (the dropped-keys assertion can fail)"
else
  bad "the control mutation did not re-add an E row where Tab was"
fi
rm -rf "$TMP"

echo
[ "$fail" -eq 0 ] || exit 1
echo "PASS: a physical keyboard is accepted in-session and every mapped key routes where the table says"
