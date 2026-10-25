# Per-game settings — the model, the keys, and how to add one

Status: implemented on both platforms 2026-10-24 (the player's own
design, settled the same day). Local batch, unpushed; see the
verification note at the end for exactly what is and is not proven.

## The design (the player's decisions, 2026-10-24 — do not redesign)

- The app already shows a per-game page (the adapter notes) when a
  game is tapped, and the game does NOT auto-start from there. A
  **"Per Game Settings" control sits at the top of that page** — top
  right in the iOS toolbar; first control under the game's name on
  Android, whose page is a scrolling column. It is there for EVERY
  game, always in the same place, so it can be found by position and
  by habit. Nothing opens at load time; nothing interrupts starting a
  game. A game with no per-game settings still opens the panel, and
  the panel says so in words rather than opening empty.
- The app's **Settings hold the GLOBAL DEFAULT** for each setting,
  applying to every game that can use it. The per-game panel holds
  **overrides for that game only**.
- **Settings apply when the game is started**: the session reads the
  effective value — the game's override if present, else the global
  default.

## The model

```
effective(setting, game) = override(game, setting)   if one exists
                         = global(setting)           otherwise
```

A game shows only the settings its reader can use. A panel row for a
setting the game cannot use would be a dead control, and in this app
a dead control is indistinguishable from a broken one — that is why
the support table below exists and why it is keyed by what each
adapter REALLY does today (the same rule, and the same hand-kept
app-layer shape, as `AdapterCommand.supported`).

## The settings that exist

| Setting | What it is | Global default | Games |
|---|---|---|---|
| Battle cue sounds (`battle_cues`) | The whole cue family: lock-on/field beacon, health hit beeps, gauge-full earcons. Gates the app-side synth only; the adapter computes the same either way. | OFF | dissidia, dbzar, mku, khbbs — exactly the adapters with a non-null `cue_snapshot` in Core |
| Gauge-full cues (`hud_gauge_cues_sound`) | Birth by Sleep's HUD gauge-full moments: sound (earcon) vs the adapter's spoken lines ("Focus full." / "D-Link ready."). The decision-C split from the sound-first pass. | sounds (ON) | khbbs only — the seam is that adapter's own setter, `oga::KhbbsHudGaugeCues(bool)` |

No other per-game-meaningful toggle existed in the codebase when this
was built; none was invented. If a future setting appears, it joins
through the checklist at the end, not by growing a parallel system.

## Store keys

**iOS** (UserDefaults):
- Global battle cues: `cueBeacon` (owned by `GameSession.cueBeaconEnabled`) — unchanged from before this panel existed.
- Global gauge cues: `hudGaugeCuesSound` (owned by `GameSession.hudGaugeCuesSound`), default true.
- Override: `gameSetting.<setting raw value>.<adapter id>`, e.g. `gameSetting.battle_cues.khbbs`. Presence of the key IS the override; clearing removes the key.

**Android** (SharedPreferences file `oga_settings`):
- Global battle cues: `battle_cues` (owned by `CueSettings`, including its silence-the-synth-on-off behaviour) — unchanged.
- Global gauge cues: `hud_gauge_cues_sound` (owned by `GameSettings`), default true.
- Override: `game.<adapter id>.<setting key>`, e.g. `game.khbbs.battle_cues`. Same presence-is-the-override rule.

The game id on both platforms is the **adapter registry id**
(`khbbs`, `dissidia`, …) — the id the library page is keyed by on iOS
and that `AdapterLibraryData` carries on Android, and the id the
native hosts report for the loaded game. Not the DISC_ID: the
settings belong to the reader, and one reader can serve several disc
revisions.

## When the value is read (the one deliberate asymmetry)

- **Battle cue sounds are resolved PER FRAME** by both sessions
  (iOS `GameSession.refreshCue`, Android `PspGameSession.pollCues`).
  The gate is app state read from the store each frame — the same
  shape both platforms already used for the global switch — so
  flipping the global in Settings, or an override in the panel, takes
  hold on the next frame with no restart, and there is no cached
  copy to go stale. Before the session knows the game's id, the
  resolver answers with the global, which is also what the game
  resolves to once named if it has no override, so the gate never
  flips spuriously at start.
- **The gauge-cue mode is applied ONCE AT SESSION START.** It is
  adapter state, not app state: the apps push the resolved value into
  the adapter through its seam — iOS `poke_set_hud_gauge_cues`
  (pokecore → `oga::KhbbsHudGaugeCues`), Android
  `PspAccessibilityScript.setHudGaugeCues` → JNI →
  `PspAdapterHostSetHudGaugeCues` → the same setter — at the moment
  the loaded game's adapter id is known (iOS in `start()`, Android in
  `start()`'s completion). The adapter's flag is deliberately not
  reset by attach/detach: a setting is not session state. Changing it
  mid-session therefore takes effect at the next start, which is what
  "settings apply when the game is started" means for an
  adapter-owned value; the panel text says settings apply at start so
  this is the stated behaviour, not a surprise.

## Panel behaviour (both platforms, same words where allowed)

- Header line: these settings apply to this game only; a setting
  with no override uses the app default from Settings; settings apply
  when the game starts.
- One row per supported setting. The switch shows the EFFECTIVE
  value. Under it, the provenance line: "Set for this game. The app
  default is on/off." — or "Using the app default: on/off. Change the
  app default in Settings."
- Changing a switch always writes an override (even when the new
  value equals the default — the player made a choice for this game,
  and the provenance line says so until they clear it).
- "Use app default" appears only while an override exists and clears
  it (removes the key).
- Accessibility is part of the feature, not a pass over it: every
  control carries a real label and hint/description that includes
  its provenance (Android's switch row description states the value
  AND which source it came from). iOS announces each change through
  the speech engine, as its Settings switches do — with no screen,
  an unspoken change is an invisible one.

## Where the code lives

- iOS: store + resolver + support table in
  `Sources/OpenGameAccess/GameSettings.swift` (Foundation only, so
  the Linux type-check gate covers it); panel in
  `PerGameSettingsView.swift`; entry control in `AdapterLibrary.swift`
  (`AdapterGamePage` toolbar); globals in `GameSession.swift`
  (`cueBeaconEnabled`, `hudGaugeCuesSound`) and `SettingsView.swift`.
- Android: store + resolver + support table in
  `GameSettings.kt` (the battle-cues global stays in
  `CueSettings.kt`); panel in `ui/AdapterGamesScreen.kt`
  (`PerGameSettingsScreen`); entry button in the same file's
  `AdapterGamePage`; globals wired through `MainActivity.kt` into
  `ui/SettingsScreen.kt`; consumption in `PspGameSession.kt`; the
  seam in `PspAccessibilityScript.kt`, `PspSessionJNI.cpp`,
  `PspAdapterHost.{h,cpp}`.
- Core: `oga::KhbbsHudGaugeCues` declared in `Core/adapter.h`
  (defined in `Core/khbbs_adapter.cpp`); `poke_set_hud_gauge_cues`
  declared in `Sources/CPokeCore/include/pokecore.h`, defined in
  `Core/pokecore.cpp`.

## How a future adapter author exposes a new per-game setting

1. **The capability comes first.** The setting must change something
   the adapter or the cue path really does today — an adapter seam
   (a setter like `KhbbsHudGaugeCues`) or an app-side gate over real
   behaviour. No setting for a hypothetical.
2. **Add the setting** to `GameSettings.Setting` (Swift) and
   `GameSettings.Setting` (Kotlin): a stable key string (it is part
   of the on-disk format — never rename one), a title, and on iOS a
   detail line.
3. **Add the supporting game ids** to `supportedSettings` on BOTH
   platforms, from the adapter's evidence (e.g. a non-null
   `cue_snapshot`), not from hope.
4. **Give it a global default** in Settings on both platforms: a
   persisted global on the session (iOS) / in the settings store
   (Android), a labelled switch, and `globalDefault` reading it.
   Choose the default to match the adapter's own default, so a
   player who never opens either screen gets today's behaviour.
5. **Wire consumption in the session**: app-state settings resolve
   per frame through `effective(...)`; adapter-state settings get a
   seam call applied once at session start, gated on the adapter id.
6. **Mind the gates a new seam trips**: a new `poke_*` call from
   Swift must be declared in `pokecore.h` (swift-check's poke-symbols
   gate); a new Kotlin `external fun` must have its exact JNI symbol
   in `PspSessionJNI.cpp` (launcher contract test §9). Run
   `scripts/adapter-tests.sh` and `scripts/swift-check.sh`.
7. **Document it here**: the settings table, the keys, the default.

## Verification (2026-10-24, this machine)

- `scripts/adapter-tests.sh`: the full suite — host adapter tests
  (including the khbbs host test, which drives the
  `KhbbsHudGaugeCues` seam both ways) and the Android launcher
  contract test, which now pairs 20 JNI functions (the new
  `nativePspSetHudGaugeCues` included) and still finds the generated
  library data unchanged. PASSED (see the batch notes for the run).
- The changed native TUs compile: `PspAdapterHost.cpp` and
  `PspSessionJNI.cpp` under g++ against JDK JNI headers with a stub
  `android/log.h` (clean but for the one pre-existing JDK-vs-Android
  `AttachCurrentThread` signature quirk in untouched bridge code, the
  same one the cue work recorded); `Core/pokecore.cpp` under g++
  against the ~/src dependency trees, with the local tree's two
  pre-existing drifted core headers (`oga_core.h` stale, `snes_core.h`
  absent — neither touched by this work) supplied from GitHub main's
  copies for the compile only, the same workaround the cue work used.
  The seam's link chain is proven by the khbbs host test: it compiles
  the adapter TU beside a test TU that includes `adapter.h` and also
  declares the setter locally, so declaration and definition agreeing
  is what its 425 passing checks ran on.
- **Not proven here**: the Swift and Kotlin sources have not been
  compiled — this machine has no swiftc, no Kotlin compiler, no
  Xcode, no Android SDK. `GameSettings.swift` is classified CHECKABLE
  in swift-check.py, so CI's swiftc will type-check it on the next
  build; the SwiftUI panel, the GameSession/SettingsView edits, and
  all the Kotlin await the same real builds the cue work awaits (iOS
  via CI on push, Android on the Legion), plus a hands-on pass: open
  a game page, set an override, start the game, hear it honoured.
