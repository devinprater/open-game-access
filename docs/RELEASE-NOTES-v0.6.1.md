# Open Game Access v0.6.1 — the NES readers actually reach the phone

**v0.6.0-nes said the NES readers worked on the device. They could not.** This release
makes that true, and states plainly what went wrong.

## What was wrong

The shipped v0.6.0-nes IPA had the Mesen core genuinely linked in — 281 core objects in
the binary — and **zero reader files**. `Resources/nes-lua/` was in `.gitignore`, no
`Package.swift` entry copied it, and the one call that points the console at a reader
(`poke_set_script_dir`) was gated to Game Boy ROMs. So every `.nes` booted **silently**,
while the release notes promised Zelda 1 Access speaking "the overworld screen on arrival
and on every screen crossing".

Nothing in that build reported a problem: the core linked, the file type resolved
correctly, the console ran. It is the third time this project has shipped a feature that
was *linked* but not *reachable*, which is why two of the new gates below check
reachability rather than compilation.

## What works now

- **NES readers are in the app.** Zelda 1 Access and Dragon Warrior Access ship as bundled
  resources, and the console loads them.
- **The core picks the reader from the cartridge itself.** The ROM's CRC32 decides which
  reader a game gets — never its filename, so a renamed copy or a translation patch cannot
  get a reader written for a different game. A game nobody has written one for gets **no
  reader**, which is the honest outcome. Verified: Super Mario Bros. resolves to none and
  says so in the log.
- **Zelda (USA and Rev 1) and Dragon Warrior (USA and Rev 1)** each resolve to their set
  and speak. Measured on the host with real ROMs, through the same source lists the app
  compiles.
- **NES is now marked playable** in the console registry, and its reader assets are proven
  present in the built IPA.

## Two bugs that would have broken it on the phone anyway

Both were in the reader wrappers, and neither would have shown up on this machine:

- Each wrapper pointed at a **hard-coded path on my own machine** as the reader's location.
  On a phone that file does not exist, so the reader would have failed to load.
- Each used a **shell command** to find its own data files. iOS does not let an app do
  that, so the search would have quietly found nothing — and because Dragon Warrior
  declares its speech file inside one of those data files, **that reader would have been
  completely mute** while looking healthy.

The readers also write their own files (speech text, saved settings, progress). An app
bundle cannot be written to, so the app now stages a writable copy before handing the
console the path. Without that, narration would have worked and nothing would ever have
been saved.

## Not in this build

- **No phone has run any of this.** Everything above is host-side and in the IPA; the first
  run on the device is yours. The release notes are not claiming more than that this time.
- **Zelda's map and room narration has still not been heard** — only its menu lines
  ("Inventory Menu."). The reader is known to narrate rooms; this build has not observed it.
- **Dragon Warrior says "Warning: Critical health." on a fresh boot**, which is suspicious
  for a brand-new game. Uninvestigated, and flagged rather than hidden.
- **Neither mod's spatial audio works.** Both ship a separate Windows helper for footsteps
  and radar cues; iOS has no equivalent path. The readers skip those calls, so speech works
  and cues stay silent.
- NES audio is a deliberate refusal, and NES savestates refuse out loud (battery saves work).

## Fixed on the way

- A `.nes` file the picker accepted **vanished from the recent-games list** afterwards —
  the filter was missing the NES file types.
- The two third-party readers ship **without their Windows helper files**, like the Game Boy
  reader set does.

## New checks, each proven able to fail

- **The reader sets are really bundled** — the named sets exist, each wrapper derives its
  own path instead of hard-coding one, neither shells out, and the app is set up to copy
  them in. Runs in CI, no emulator needed, because this is the exact thing that was false.
- **The backend tables are not rotated.** Adding a capability to the backend interface and
  forgetting one console's table shifts every later entry, so the app calls the wrong
  function — with a clean build. The gate counts them.
- **The full reader path is exercised with real ROMs** and requires speech, on the build
  host, with the identity check proven by sabotaging every CRC row.

## Downloads

| Platform | File | Notes |
|---|---|---|
| **iOS device** | `OpenGameAccess-v0.6.1.ipa` | **Unsigned** — re-sign with SideStore as usual. arm64, iOS 17+. |
| **Android** | `app-gitHub-prod-debug.apk` | Built by CI on the release tag; attached here when it finishes (up to ~2 h after the release appears). |
