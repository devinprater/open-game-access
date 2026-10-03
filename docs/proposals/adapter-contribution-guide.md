# OGA Adapter Contributor Guide

How to add a per-game adapter to Open Game Access (OGA).

An adapter reads a game's own RAM from outside the guest through
host-gated reads, speaks semantic state, and drives the real console's
buttons when the player asks. It never writes RAM and never touches
game data files.

## Read first

- README.md — no-ROMs policy, adapter seam, per-game adapters.
- Core/adapter.h (92 lines, whole file) — the Host / Adapter / Command
  contract you implement against.
- Core/dbz_adapter.cpp header comment — the verified-map convention,
  the READ-ONLY rule, and the published-codes-were-wrong caution.
- Core/fe_adapter.cpp (150 lines) — the smallest honest adapter:
  attach, route sinks, forward commands, stay silent until ready.
- docs/fire-emblem-shadow-dragon-memory.md — what verified vs
  unverified looks like for one game.

Policy sources this guide distils (adapted, not copied):

- pd-access AGENTS.md — semantic-over-pixels, small hooks with policy
  in modules, never block the game loop, poll-vs-hook, dev-off /
  handoff-on defaults, handoff evidence.
- CFC2-access CLAUDE.md — two-independent-sources, dwell before
  concluding absence, count syscalls before loops, rate-limit logs by
  time, decouple play/diag switches.

## 1. Core principles

- Model the game, not the screen.
  - Read the value the engine already knows (cursor struct, unit record,
    name string) instead of inferring it from pixels or OCR.
  - Pixels and OCR are fallbacks, never the primary abstraction.
  - Speak the game's own identifiers (for example its PID/JID strings)
    rather than a hardcoded name table where the game stores one.
- Read-only. Always.
  - Inspect memory through the host's read8/16/32; out-of-range reads
    return 0, they never fault, and that is not a value.
  - Drive the REAL game with set_button when the player asks. Never
    teleport units, patch stats, or mutate gameplay state.
  - Never write guest RAM for any reason, including "just a test".
- Two independent sources per claim.
  - Every address, stride, offset, and decoding in your map needs two
    independent confirmations before it ships. Examples of independence:
    - A pointer lands exactly on a record boundary AND the name bytes at
      that record are readable text.
    - A cursor struct value AND pixel-position-divided-by-camera-tile-size
      reproducing the same tile across several snapshots.
    - A party list AND the game's own Status screen showing the same
      members and numbers.
  - One plausible-looking value is not verification. Memory is full of
    constants that pass relational tests (CFC2's 0x0222 run matched every
    "looks like HP" relationship at every alignment) — add a CONTENT test
    (printable text? sane range? exact alignment?) that a constant
    cannot pass.
  - A published cheat-code list is a hypothesis, not a source. DBZ's
    published base was one whole stride off, so every record decoded to
    a plausible NEIGHBOUR. Trust the shape, re-prove the address.
- No hardcoded address without a live cross-check.
  - Every constant in the map must name the live check that would catch
    it being wrong (alignment test, terminator test, text test, count
    test, on-screen confirmation).
  - If the check fails at runtime, the adapter refuses the data (invalid
    slot, stays not-ready, says "not available") instead of speaking
    garbage.
  - Record what is verified, what is not, and the method for each, in
    the per-game memory doc. Unverified fields stay out of speech.
- Prove it before you speak it.
  - A blind player cannot glance at the screen to filter your mistake.
    ready() stays false until map-level state exists; controls stay
    silent rather than narrating whatever happens to be in RAM.
  - Saying "not available yet" is an honest answer. Silence looks like
    a broken control.

## 2. Poll vs hook, adapted for OGA

OGA adapters cannot hook the engine — there is no code inside the guest
to hook. All observation is host-gated reads from outside. So the
pd-access rule becomes:

- Poll stable state once per frame; detect events by change, not by hook.
  - on_frame is your tick. Read the small stable value there (a
    session/mode word, a cursor struct, a count), not the whole world.
  - Command handlers (WhereAmI, NextAlly, ...) do the deeper read on
    demand, when the player asks.
- Prefer polling the stable word when that avoids speculative decoding.
  One cheap read per frame beats re-walking a structure you do not need
  yet.
- Prefer change-detection when polling would lose an event, its source,
  or its ordering.
  - Keep last-value state in the adapter, compare each frame, and speak
    only on change.
  - Give every announcement a dedup key (what changed plus which object),
    so a steady state speaks once and a repeated frame does not repeat it.
  - Give announcements an expiry: a change nobody can act on stops being
    announced.
- Dwell before concluding absence.
  - Some state arrives on its own schedule (engine text, captions,
    asynchronously allocated structures). A value missing on one tick is
    "not yet", not "does not exist".
  - CFC2 needed about 1 s of dwell per row before captions arrived; a
    300 ms walk concluded "no captions" and was wrong. Wait, re-check
    across frames, then conclude.
- Never block the game loop on speech, logging, or diagnostics.
  - on_frame returns fast. Speech goes through the host sink;
    diagnostics go to the log sink. No adapter code waits on either.

## 3. Performance rules

Host reads cross a boundary. Treat each one as a cost and budget them.

- Before any loop over memory, COUNT THE SYSCALLS it will make.
  - If the count scales with items/pages rather than regions, it is
    already too slow — no profiling needed, just multiplication.
  - CFC2's full-arena sweep called one syscall per 4 KB page (about
    65,000 syscalls) and stalled speech for 114 seconds. One query per
    region plus block reads did 256 MB in 125 ms.
- Hoist immutable checks out of the loop.
  - Anything that cannot change between ticks (code signatures, struct
    strides, verified base addresses, capability flags) is checked once
    at attach, not once per frame.
- Keep the inactive path to one cheap read.
  - When the game is not in the state you serve (no map loaded, wrong
    mode, adapter detached), on_frame does a single small read of the
    mode/session word and returns. Leaving the state must silence output
    as fast as entering it enables it.
  - The fault guard (host bounds-check returning 0) is a net for races,
    never a replacement for validating addresses before you read them.
- Bound every scan.
  - Fixed iteration caps, terminator checks, and count fields checked
    BEFORE dereferencing (DBZ checks the party count's low u16 before
    walking the pointer array).
  - A sweep that truncates must report the truncation as truncation, not
    as the answer. Count encounters separately from reported hits.
- Do not let liveness counters lie.
  - A heartbeat derived from the loop it monitors cannot detect that
    loop stalling. If you add a watchdog, drive it from wall-clock time,
    not from loop iterations.

## 4. Speech and log discipline

- Never call a platform speech API from adapter logic.
  - Speak only through the host's speak sink; diagnose only through the
    host's log sink. Both are set on every attach (the host pointer is
    per-core; a fresh core is built per ROM) and cleared on detach.
- Give announcements priorities, dedup keys, replacement groups, and
  expiry — in the adapter, before they reach the sink.
  - Priority: player-asked answers interrupt; ambient change notices
    queue behind what is speaking.
  - Dedup key: same state, same key, no repeat speech.
  - Replacement group: a newer value for the same slot (next ally, new
    cursor tile) REPLACES the queued older one instead of stacking
    behind it.
  - Expiry: stale queued speech is dropped, not read late.
- Rate-limit logs by wall-clock time, not by event count.
  - A change-guard on a counter that always changes is no guard at all:
    CFC2 logged 2741 lines in 5 minutes (one per about 25 ms) from a
    guard that was true on every tick. Time-based throttling (at most one
    line per N seconds per key) survives counters you did not expect
    to move.
- Decouple play switches from diag switches.
  - One flag enables player-facing speech; a SEPARATE flag enables
    diagnostic logging. Turning off diagnostics must never silence the
    player, and turning off speech must never blind debugging.
  - Development detail (pointers, raw addresses, full dumps) goes to the
    log sink, never to speech.
- Reuse verified wording; do not fork it.
  - The reader's spoken sentences were reviewed and their numbers
    verified against live RAM. Quote them through the sink rather than
    rewriting them in the adapter layer, so there is exactly one copy
    that can drift.
- Keep logs linear and screen-reader friendly.
  - One fact per line, plain lists, no ASCII tables. The owner reviews
    logs with NVDA.

## 5. Defaults and handoff

- Default new adapter features to OFF during development, while behaviour
  and failure modes are still being understood.
- Before handing a build to the project owner for blind-user acceptance
  testing, turn every implemented feature ON and verify the effective
  configuration plus the session-start log. Never require the acceptance
  tester to discover or manually enable something new.
- Compilation proves integration, not accessibility. A feature is not
  validated until a blind or screen-reader-dependent tester completes
  its stated task, and the report records barriers as well as successes.
- Every handoff states:
  - Files changed and the user-visible behaviour.
  - Game-code routing touched (registry entries, game codes, revs) and
    why each was necessary.
  - Assumptions and unresolved questions (what is verified, what is
    not, which revs were tested).
  - Build configuration and executable tested.
  - Runtime, speech, interaction, and independent-user evidence
    collected (which commands were run, what was spoken, session IDs).
  - The per-game memory doc section or log fields used to reach the
    conclusion.
  - Rollback steps and known regressions.
- Do not describe a feature as validated without recorded blind or
  screen-reader-dependent acceptance evidence.

## 6. File conventions

- One adapter is two files plus one test:
  - Core/<game>_adapter.cpp — the adapter (attach, commands, ready,
    detach, verified map).
  - Per-game reader logic lives in its own file (for example
    Core/fe_access.cpp) when it predates the adapter; the adapter
    file routes sinks and forwards commands, it does not reimplement
    the reader.
  - Core/<game>_adapter_test.cpp — host-stub test. Every adapter ships
    with one; the Host struct is function pointers precisely so the
    adapter is testable on the host with a stub.
- Verified-map header comment format (follow dbz_adapter.cpp):
  - Open with WHY THIS EXISTS: what investigation established the map
    and what it was confirmed against (the game's own screens, named).
  - Then a THE VERIFIED MAP (do not adjust without re-confirming ...)
    block listing each base address, stride, count, and field offset,
    one per line, with the traps annotated inline ("NOT 0x... — an
    earlier reading was one stride off, decoding every record to its
    neighbour").
  - Then a WHY-OTHER-SOURCES-WERE-WRONG note where one exists
    (published codes, plausible formulas) so the next contributor does
    not re-trust them.
  - Close with the READ-ONLY rule restated: inspects memory, drives
    real buttons on player request, never writes RAM.
- Adapter registration:
  - Define the Adapter struct with external linkage (namespace-scope
    extern const Adapter k...) so the registry in adapters.cpp can
    see it.
  - Key on the ROM game code from the header (bytes 0x0C-0x0F); note
    which region/rev the addresses were verified against in a comment
    at the registration site.
  - attach returns false when the ROM is not the expected game so the
    registry falls through; ready() gates speech on real state.
- No ROMs, saves, BIOS, firmware, or emulator objects in the repo, ever.
  Testers supply their own legally obtained game files.
  scripts/git-hooks/pre-commit refuses staged game data and build
  output; .gitignore is the second line of defence, not the first.
