# Frontend accessibility review with Community-Access/accessibility-agents

Open Game Access is a screen-reader-first product, so its UI needs the same
scrutiny as its memory readers. [Community-Access/accessibility-agents][repo]
(MIT) is a pack of 80 accessibility-focused AI agents that encode WCAG 2.2 AA
rules for AI coding tools. This note records what is useful here, how to use it,
and — importantly — where it does **not** apply.

[repo]: https://github.com/Community-Access/accessibility-agents

## What it actually is

Agent/prompt definitions, not a library and not a runtime dependency. Nothing is
vendored into this repo and nothing is linked into the app. The useful parts are:

- `.claude/specialists/mobile-accessibility.md` — the one that matters here:
  iOS (SwiftUI/UIKit) and Android (Compose/Views) semantics, touch targets,
  screen-reader behaviour.
- `.claude/specialists/aria-specialist.md`, `contrast-master.md`,
  `keyboard-navigation`-class specialists — for the web/HTML surfaces.
- `.github/workflows/a11y-check.yml`, `a11y-pr-gate.yml` — CI shape for
  accessibility gates.

Install for an AI coding tool with the project-scoped, non-interactive form so
nothing global is touched:

```bash
bash install.sh --project --copilot --yes --no-auto-update --dry-run  # preview
bash install.sh --project --copilot --yes --no-auto-update           # apply
```

Windows: `.\install.ps1 -Project -Copilot -Yes -NoAutoUpdate -DryRun`.

## The rules worth enforcing here, and why

Taken from the mobile specialist and applied to this codebase.

### Touch targets

- iOS minimum **44 x 44 pt**; Android minimum **48 x 48 dp**.
- Measured in points/dp, **not pixels** — `48dp` at 3x density is 144 physical
  pixels, and a 48-*pixel* target is ~16dp and fails.

### Every control needs a name AND a reason to exist as a control

- `contentDescription` (Android) / `accessibilityLabel` (iOS) on anything
  interactive.
- An element that cannot be focused, named, and activated **does not exist** to
  a screen reader, no matter how it looks or how well it responds to touch.

### Do not announce what the UI already says

The Android `TYPE_ANNOUNCEMENT` event and `announceForAccessibility()` are for
"exceptional situations" — state that changes **without** the UI updating. The
platform docs are explicit that ordinary control state should be carried by the
control's own label/value, and that `TYPE_ANNOUNCEMENT` is deprecated in favour
of semantic alternatives. Announcing a button press that the user just performed
is noise.

⛔ **This is the single most relevant rule for this app, and it is also the rule
this app deliberately breaks.** Here the *game state* changes with no UI change
at all — the emulated screen is a bitmap and its content is not in the
accessibility tree. Game narration is therefore a legitimate use of
announcements, and the app is careful to route *UI* feedback (ROM loaded, errors)
differently from *game* narration. See `docs/design/announcements-vs-live-regions.md`.

### Live regions over announcements where the UI does update

For app-owned UI text that changes in place, the correct pattern is
`accessibilityLiveRegion` (Android) or an `updatesFrequently` trait / live-region
element (iOS) — not a posted announcement. Announcements get dropped when the
screen reader is mid-utterance; a live region on a focused element is re-read.

## Where it does NOT apply

- **Web-only specialists are irrelevant to the SwiftUI, Compose and XML-View
  surfaces.** The repo tells you to hand off the other way itself.
- **The mobile specialist targets app UI.** It has no concept of an emulated
  console's input sampling, an audio cue, or a game-state announcement channel.
  Applying "don't announce what the UI says" to a game reader would delete the
  product. The distinction is *whose* state changed: the app's UI, or the game's.
- **Touch-target minimums are a floor, not a design.** The reading buttons here
  are 56dp and full-width because they are used without sight, where a
  minimum-size target is a target you will miss.

## What was actually applied in this change

| Finding | Rule | Fix |
|---|---|---|
| D-pad is one unlabeled node to TalkBack | name + activate per control | four named `Move ...` buttons forwarding to the console d-pad; the pad's own split-cluster children are now `IMPORTANT_FOR_ACCESSIBILITY_YES` with a `contentDescription` so they are reachable at all |
| An activated pad slot never moved the player | — (correctness) | hold the press ~140 ms; a DOWN/UP inside one frame is never sampled |
| App overrode the player's speech rate/engine | honour platform preferences | read `TTS_DEFAULT_RATE` / `TTS_DEFAULT_PITCH`; leave engine to the system; manifest `<queries>` for `TTS_SERVICE` so a third-party engine is visible on Android 11+ |
| VoiceOver queue/interrupt collapsed to "always interrupt" | — (correctness) | carry `interrupt` across as `.accessibilitySpeechQueueAnnouncement` |

## Verification

`aapt2`-independent check of the shipped artifact, because a merged manifest in
`build/intermediates/` is only evidence if you read the *packaged* one:

```bash
sed -n '/<queries>/,/<\/queries>/p' \
  app/build/intermediates/packaged_manifests/gitHubProdDebug/*/AndroidManifest.xml
```

Confirm the string resources compiled too, rather than trusting `strings.xml`:

```bash
grep -o "a11y_move_[a-z]*" \
  app/build/intermediates/packaged_res/gitHubProdDebug/*/values/values.xml | sort -u
```
