# Announcements vs live regions: two channels, chosen by whose state changed

The question this answers: **when the app has something to say, which platform
mechanism should say it?** Getting this wrong is not cosmetic — the wrong choice
is either silent or deafening, and both failure modes are invisible to a
sighted developer.

## The rule

Route by **whose state changed**, not by how important the message feels.

| What changed | Mechanism | Why |
|---|---|---|
| The **app's own UI** (ROM loaded, error, setting changed) | The control's own label/value, or a live region if the text changes in place | The screen reader already re-reads a focused element when it changes. A separate announcement duplicates it or interrupts it. |
| **Game state** with no UI change (dialogue on screen, player position, terrain, an enemy's HP) | An announcement, posted through the platform's accessibility channel | Nothing in the accessibility tree changed, because the game screen is a bitmap. There is no element to re-read. |

The second row is why this app exists and why it is the documented exception to
the general "prefer semantics over announcements" guidance. The emulated console
writes pixels; its meaning is reconstructed by the readers in Lua and has to be
spoken, because there is no view to attach it to.

## Platform mechanism

### Android

- **TalkBack running** → a **live region**, whose text is set per line. The
  queue/interrupt contract maps onto the region's politeness: a *polite* region
  queues behind whatever is being read, an *assertive* one interrupts. One view
  carries both cases by switching mode, exactly as the iOS side switches an
  attributed-string attribute.
- **TalkBack not running** → `TextToSpeech` directly, with `QUEUE_ADD` for
  background lines and `QUEUE_FLUSH` for requested ones. This is what makes the
  app work with no screen reader at all, and with the screen off, where an
  accessibility announcement would go nowhere.
- **App UI feedback** → the control's own `contentDescription`/state. Not an
  announcement.

The live-region view is wired through `AccessibilityScript.setAnnouncementView`,
because the speech engine is built when the ROM loads and the Activity's layout
is inflated afterwards — the reference is nullable for exactly that ordering, and
narration arriving before it exists falls back to TTS rather than being dropped.

⛔ **`TYPE_ANNOUNCEMENT` is deprecated** in favour of semantic alternatives, and
the platform docs say to use it only in exceptional situations. Game narration
is that exception — but a live region expresses it better here, because the
politeness setting *is* the queue/interrupt distinction, so no deprecated API is
needed at all.

### iOS

- **VoiceOver running** → `UIAccessibility.post(notification: .announcement)`.
  VoiceOver owns the audio session; a second synthesizer speaking over it gets
  cut off mid-word, so the game must not use `AVSpeechSynthesizer` here.
- **VoiceOver not running** → `AVSpeechSynthesizer`, which also works with the
  screen off.

## Carrying queue-vs-interrupt across the VoiceOver switch

The Lua reader's `say(text, interrupt)` contract means:

- `interrupt = false` — background narration; it should wait its turn.
- `interrupt = true` — the player asked for this; it should jump the queue.

⛔ **A plain `String` announcement always interrupts**, so posting one for every
line collapses both cases into "interrupt" and background narration ends up
cutting off the line the player just requested — the exact opposite of the
contract. The queue flag must be carried as an attributed string:

```swift
let announcement = NSAttributedString(
    string: text,
    attributes: [.accessibilitySpeechQueueAnnouncement: queue]
)
UIAccessibility.post(notification: .announcement, argument: announcement)
```

The key has no effect on a plain `String` argument. This is the whole reason the
iOS engine builds an `NSAttributedString` for the non-interrupting case.

## The known limitation, stated plainly

**VoiceOver silently discards an announcement posted while it is reading a
focused element's label.** It is not queued, it is dropped. This is documented
behaviour of the announcement notification, not a bug in this app.

Consequences:

1. There is no retry that makes announcements reliable, because VoiceOver gives
   no failure callback for a dropped announcement.
2. So announcements are **not the only channel**. The game screen element
   carries the same state in its `accessibilityValue` (`Running <game>`,
   position, status), so a dropped line can still be reached by re-focusing the
   screen instead of being lost outright.
3. The "Repeat" control re-speaks the last line on demand, which is the
   user-driven recovery path when an announcement is missed.

Anything that must be heard and must not be lost belongs in the element's value,
not in an announcement.

## Android TTS: honour the player's own settings

The app must not impose its own voice, rate or pitch. For a screen-reader user
those settings **are** the interface — a rate that is too fast is unusable rather
than merely annoying, and someone who has tuned their device has already made
this decision once.

- Read `Settings.Secure.TTS_DEFAULT_RATE` and `TTS_DEFAULT_PITCH` (stored as
  integer percent, so `100` = 1.0x) and apply them.
- Do **not** pass an explicit engine name to `TextToSpeech(context, listener)`;
  `null` means "the engine the player selected", and pinning a name freezes in
  whatever was default at that moment.
- The manifest must declare `<intent><action
  android:name="android.intent.action.TTS_SERVICE" /></intent>` inside
  `<queries>`. Without it, Android 11+ package-visibility filtering hides
  third-party TTS engines, so the system default is reachable but the player's
  own installed engine is not — the precise opposite of honouring the setting.

## Testing note

None of this is verifiable by building. Announcement routing differs by whether
a screen reader is running, and voice/rate come from device settings. Verify on
a device or emulator with TalkBack (Android) and VoiceOver (iOS) both on and
off, and confirm the engine/rate in logcat:

```
[pokemon-access] TTS ready, engine=<package>
[pokemon-access] speech rate=1.0 pitch=1.0 (from the player's device settings)
```
