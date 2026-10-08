# Open Game Access v0.6.7 — a button press now lasts for emulated frames, not for seconds

**The short version:** On iOS, a button tapped through VoiceOver was released on a timer that
measured real seconds. On a warm or low-power phone that timer could expire between two emulated
frames, so the game never saw the press at all. That is why START did nothing in Pokémon Crystal.
The press is now held for a count of emulated frames, so a slow frame cannot swallow it.

## What this fixes

### Buttons tapped through VoiceOver could be invisible to the game

Tapping a button with VoiceOver released it a fixed 0.12 seconds later. The app draws its frames
from a display link, and iOS slows that display link down when the phone is warm or in Low Power
Mode. If one emulated frame took longer than 0.12 seconds, the press and the release both
happened between two frames, and the game sampled its controller with the button already up.

A press the console never samples is a press that never happened. This is why START did nothing in
Pokémon Crystal, and it is why no test on this machine could reproduce it: a computer runs frames
back to back, so it always sees the press.

The fix is in the core rather than the interface, because only the core knows when a frame
actually ran. A tap is now latched for four emulated frames and released inside the frame loop. It
does not matter how long each frame takes.

Measured on the real emulator with the real cartridge, by checking whether the screen actually
changed:

    frame-counted tap, normal speed               -> screen changed at frame 2
    press and release with no frame between them  -> screen NEVER changed (the bug, reproduced)
    frame-counted tap, every frame padded 60 ms   -> screen changed at frame 2

The third line is a throttled phone, and it now behaves exactly like full speed. The second line is
what the old timer did on a slow frame, and the game never saw the button.

A press you hold with your finger is unchanged. That path already worked, because you decide when
to let go.

## What is still not proven

Measured on the host, not on your phone. The throttling that caused this cannot be reproduced on a
computer, so the fix is proven by simulating the slow frame rather than by experiencing it.

## Carried over from v0.6.6

The Game Boy sound fix and the Game Boy Color cartridge fix are both included, and were unchanged
by this release.
