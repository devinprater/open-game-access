#!/usr/bin/env python3
"""Verify the Compose launcher's contract with the native reader.

⛔ THIS REPLACES A JS-BRIDGE CHECK, AND THE REPLACEMENT MUST NOT BE WEAKER.
The old android-gb-bridge-test.py policed a WebView seam: it matched the names
Kotlin called in JS against the names the page defined, because a typo across that
seam was silence — which reads as a crash to someone who cannot see the screen.

The launcher is now Compose, so there is no seam to match across. What is still
worth guarding is the part a compiler cannot check, and the part that has actually
been wrong here before:

  1. The mGBA KEY BITS. A wrong bit is a wrong button, silently: the player presses
     Up and the game reads Right. GbKeys must match the core's order exactly.
  2. The JNI SYMBOLS GbAccessibilityScript declares. `external fun` resolves by
     mangled name, so a method with no matching Java_ symbol is an
     UnsatisfiedLinkError at the first ROM, not at compile time.
  3. THE ANNOUNCEMENT VIEW IS WIRED. This is the bug the Compose port fixes, and
     it has no compile-time symptom: without the call, AccessibilitySpeech's
     screen-reader path is unreachable and the app speaks through TTS on top of the
     screen reader — two voices on one output. If a later edit drops that call, this
     check fails.
  4. CAPABILITY IS ASKED, NOT ASSERTED. The launcher once hardcoded "not compiled
     into this test build" and kept saying it for weeks after the core landed.
  5. THE GAME LIBRARY DATA MATCHES THE REGISTRY. AdapterLibraryData.kt is
     generated from Core's adapter registry (tools/android-library-gen.cpp),
     but it is CHECKED IN, so it can go stale the day someone registers an
     adapter and forgets to regenerate. The count identity below — registry
     entries minus the two console-only placeholders the library filter
     excludes — is the part that catches exactly that, without the test
     having to parse C++ initializers.

Comments are stripped before scanning, because a check that fires on prose ABOUT a
defect is a false alarm — and that has happened here twice.
"""
import io
import os
import re
import sys

ROOT = os.environ.get("OGA_ROOT", os.path.expanduser("~/oga-work"))
if len(sys.argv) > 1:
    ROOT = sys.argv[1]
os.chdir(ROOT)

SESSION_KT = "app/src/main/java/com/devin/opengameaccess/GbGameSession.kt"
SCREEN_KT = "app/src/main/java/com/devin/opengameaccess/ui/LauncherScreen.kt"
ACTIVITY_KT = "app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
GBSCRIPT_KT = ("app/native-overlay/app/src/main/java/me/magnum/melonds/"
               "accessibility/GbAccessibilityScript.kt")
DSSCRIPT_KT = ("app/native-overlay/app/src/main/java/me/magnum/melonds/"
               "accessibility/AccessibilityScript.kt")
JNI_CPP = "app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp"
LEGACY_HTML = "app/src/main/assets/index.html"
LIBRARY_KT = "app/src/main/java/com/devin/opengameaccess/AdapterLibraryData.kt"
GAMES_SCREEN_KT = "app/src/main/java/com/devin/opengameaccess/ui/AdapterGamesScreen.kt"
REGISTRY_CPP = "Core/adapters.cpp"
PSP_SESSION_KT = "app/src/main/java/com/devin/opengameaccess/PspGameSession.kt"
PSP_SCRIPT_KT = ("app/native-overlay/app/src/main/java/me/magnum/melonds/"
                 "accessibility/PspAccessibilityScript.kt")
PSP_JNI_CPP = "app/native-overlay/app/src/main/cpp/PspSessionJNI.cpp"
PSP_HEADER = "Core/psp_core.h"

# ⛔ SANITY: a missing file must be an ERROR, not a pass. An early version of this
# class of check scanned a file that did not exist, found no bad strings in nothing,
# and reported success.
for rel in (SESSION_KT, SCREEN_KT, ACTIVITY_KT, GBSCRIPT_KT, DSSCRIPT_KT, JNI_CPP):
    if not os.path.exists(rel):
        print("!! %s is missing; refusing to report a pass" % rel)
        sys.exit(1)

fails = []
checks = 0


def ok(what):
    global checks
    checks += 1
    print("  ok   %s" % what)


def bad(what, why):
    global checks
    checks += 1
    fails.append(what)
    print("  FAIL %s  (%s)" % (what, why))


def read(rel):
    return io.open(rel, encoding="utf-8").read()


def strip_kt(src):
    """Remove block and line comments. Strings are left alone deliberately: the
    key-bit check below reads literal numbers, and stripping strings would hide
    them."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"(?m)//[^\n]*$", "", src)


session = strip_kt(read(SESSION_KT))
screen = strip_kt(read(SCREEN_KT))
activity = strip_kt(read(ACTIVITY_KT))

# ---- 1. the key bits must match mGBA's own order ----------------------------
# A wrong bit is a wrong button with no error anywhere.
want = {"A": 1, "B": 2, "SELECT": 4, "START": 8, "RIGHT": 16,
        "LEFT": 32, "UP": 64, "DOWN": 128, "R": 256, "L": 512}
block = re.search(r"object\s+GbKeys\s*\{(.*?)\n\}", session, re.S)
if not block:
    bad("GbKeys is defined", "no `object GbKeys {` found")
else:
    got = {k: int(v) for k, v in
           re.findall(r"const\s+val\s+(\w+)\s*=\s*(\d+)", block.group(1))}
    if got == want:
        ok("the key bits match mGBA's order exactly (%d keys)" % len(want))
    else:
        diff = {k: (got.get(k), v) for k, v in want.items() if got.get(k) != v}
        bad("the key bits match mGBA's order exactly", "differs: %s" % diff)

# ---- 2. every external fun in GbAccessibilityScript has its JNI symbol -----
# ⛔ EXACT SYMBOL NAMES, not substrings: JNI resolves by the mangled symbol
# Java_<package>_<Class>_<method>, so a name-only grep passes on a renamed method.
gb = read(GBSCRIPT_KT)
jni = read(JNI_CPP)
jni_symbols = set(re.findall(r"Java_[A-Za-z0-9_]+", jni))
externals = re.findall(r"external\s+fun\s+(\w+)\s*\(", gb)
unbound = []
for name in externals:
    simple = ("Java_me_magnum_melonds_accessibility_GbAccessibilityScript_" + name)
    companion = ("Java_me_magnum_melonds_accessibility_GbAccessibilityScript_"
                 "_00024Companion_" + name)
    if simple not in jni_symbols and companion not in jni_symbols:
        unbound.append(name)
if not unbound:
    ok("every external fun has its exact JNI symbol (%d)" % len(externals))
else:
    bad("every external fun has its exact JNI symbol", "unbound: %s" % unbound)

# ---- 3. the announcement view is WIRED --------------------------------------
# ⛔ THE BUG THIS PORT EXISTS TO FIX, and it has no compile-time symptom. Without
# setAnnouncementView, AccessibilitySpeech cannot announce through the screen
# reader's live region, falls through to its "no view yet" branch, and speaks via
# TTS over the top of the screen reader.
# ⛔ THE ACTIVITY MAY GO THROUGH THE SESSION, so accept either route -- but require
# the whole chain, or this stops checking anything. Since the launch crash, the
# Activity calls GbGameSession.attachAnnouncementView (which stores the view and
# forwards it) and the session re-forwards on start; a check for the literal method
# name in the Activity would fail on the correct design.
session_for_view = strip_kt(read(SESSION_KT))
attaches = ("setAnnouncementView(" in activity
            or ("attachAnnouncementView(" in activity and "setAnnouncementView(" in session_for_view))
if attaches:
    ok("the announcement view is wired (the screen-reader speech path)")
else:
    bad("the announcement view is wired",
        "setAnnouncementView is never reached, so announcements fall back to TTS "
        "and the screen reader and TTS interrupt each other")

# It must be wired from a side effect / attached view, not during composition:
# an unattached view announces nothing. ⛔ MATCH THE CALL, NOT THE WORD: the
# import line alone contains "SideEffect", so a bare substring check passes
# even with the call deleted (the mutation suite proved exactly that).
if "SideEffect {" in activity:
    ok("the announcement view is wired after attachment (SideEffect)")
else:
    bad("the announcement view is wired after attachment",
        "no SideEffect, so the view may not be in the window yet")

# ---- 4. capability is ASKED, never asserted --------------------------------
if re.search(r"coreAvailable\s*\(", screen) or "coreAvailable(" in session:
    ok("the launcher asks whether the core is present")
else:
    bad("the launcher asks whether the core is present", "it assumes instead")

STALE = "not compiled into this test build"
prose_free = strip_kt(read(ACTIVITY_KT)) + strip_kt(read(SCREEN_KT))
if STALE in prose_free:
    bad("the stale capability claim is gone from CODE", "still present")
else:
    ok("the stale capability claim is gone from CODE")

# ---- 5. the WebView seam is actually gone ----------------------------------
# If the html is still loaded anywhere, the port is half-done and there are two
# launchers -- the exact situation where a fix reaches one path and not the other.
if "loadUrl(" in activity or "WebView(" in activity:
    bad("the Activity no longer hosts a WebView", "loadUrl/WebView still present")
else:
    ok("the Activity no longer hosts a WebView")

if os.path.exists(LEGACY_HTML):
    bad("the WebView's index.html is gone",
        "%s still exists; two launchers can drift apart" % LEGACY_HTML)
else:
    ok("the WebView's index.html is gone")


# ---- 6. NOTHING TOUCHES THE NATIVE SPEECH BRIDGE BEFORE A GAME RUNS ---------
# ⛔ THIS RULE EXISTS BECAUSE THE APP CRASHED ON LAUNCH WITH EVERY OTHER CHECK
# GREEN. `AccessibilityScript.initialize()` reaches the JNI method
# `setSpeechBridge`, and the native library is not loaded until a ROM loads, so
# calling it in onCreate dies with UnsatisfiedLinkError. It compiled, the key bits
# were right, and the announcement view was wired -- the defect was ORDERING, which
# only a rule about ordering can catch.
#
# The one legitimate pre-game use is the announcement VIEW: AccessibilitySpeech
# stores it and forwards it to its bridge when the bridge is built. Every other call
# into that class must be guarded by a running game.
activity_src = strip_kt(read(ACTIVITY_KT))
session_src = strip_kt(read(SESSION_KT))

# initialize() must not appear in the Activity at all: the bridge is built on demand.
if "initialize(" in activity_src:
    bad("the launcher does not build the speech bridge before a game",
        "AccessibilityScript.initialize() is called in the Activity, which reaches a "
        "JNI method before the native library is loaded (UnsatisfiedLinkError at launch)")
else:
    ok("the launcher does not build the speech bridge before a game")

# Any other call into AccessibilityScript from the Activity must be guarded by a
# running game. The view attach is exempt (it forwards, and is re-forwarded on start).
unguarded = []
for line in activity_src.splitlines():
    s = line.strip()
    if "AccessibilityScript." not in s:
        continue
    if "setAnnouncementView" in s:
        continue                     # the exempt, forwarding call
    if "isRunning" in s:
        continue                     # guarded on the same line
    if s.startswith("import") or s.startswith("*") or s.startswith("//"):
        continue
    unguarded.append(s)
if unguarded:
    bad("every native speech call in the launcher is guarded by a running game",
        "unguarded: %s" % unguarded)
else:
    ok("every native speech call in the launcher is guarded by a running game")

# And the view really is re-forwarded once the bridge exists, or the screen-reader
# path silently stays dead for the whole session (the bug this port fixes).
if "storedAnnouncementView" in session_src and "setAnnouncementView" in session_src:
    ok("the announcement view is re-forwarded when the bridge is built")
else:
    bad("the announcement view is re-forwarded when the bridge is built",
        "no stored view to re-forward, so it stays null and speech falls back to TTS")

# ---- 7. THE GAME LIBRARY DATA MATCHES THE REGISTRY -------------------------
# AdapterLibraryData.kt is generated from the registry but checked in, so it
# can go stale. What is asserted here is the identity a regeneration cannot
# fake: one library entry per registered adapter, minus the two console-only
# placeholders (the NES and N64 rows) that Core's LibraryAdapter filter
# excludes — if a tenth adapter is registered and the data is not
# regenerated, the counts diverge and this fails. The screen must also exist
# and be wired from the Activity, or the data is a list nobody can reach.
if not os.path.exists(LIBRARY_KT):
    bad("the game library data is present", "%s is missing" % LIBRARY_KT)
elif not os.path.exists(REGISTRY_CPP):
    bad("the game library data matches the registry",
        "%s is missing; refusing to report a pass" % REGISTRY_CPP)
else:
    registry = read(REGISTRY_CPP)
    block = re.search(r"kAdapters\[\]\s*=\s*\{(.*?)\};", registry, re.S)
    entries = re.findall(r"&(k\w+)", block.group(1)) if block else []
    placeholders = [e for e in entries
                    if e in ("kNintendoEntertainmentSystem", "kNintendo64")]
    want_count = len(entries) - len(placeholders)
    data = read(LIBRARY_KT)
    # One match is the data class declaration; the rest are list entries.
    got_count = data.count("AdapterGameInfo(") - 1
    ids = re.findall(r'id\s*=\s*"([^"]+)"', data)
    if got_count == want_count and len(ids) == want_count:
        ok("the game library lists every registered adapter's game (%d)"
           % want_count)
    else:
        bad("the game library lists every registered adapter's game",
            "registry says %d library games, data has %d entries / %d ids; "
            "regenerate with scripts/gen-android-library.sh"
            % (want_count, got_count, len(ids)))
    if len(ids) == len(set(ids)) and all(ids):
        ok("the game library ids are unique and non-empty")
    else:
        bad("the game library ids are unique and non-empty", "ids: %s" % ids)
    if os.path.exists(GAMES_SCREEN_KT) and "AdapterGamesScreen" in activity:
        ok("the game library screen exists and is wired from the Activity")
    else:
        bad("the game library screen exists and is wired from the Activity",
            "AdapterGamesScreen missing or not referenced by MainActivity")

# ---- 8. EVERY DS NATIVE CALL IS GUARDED ------------------------------------
# ⛔ THE SAME CRASH CLASS, ONE FILE DEEPER. AccessibilityScript's `external fun`s
# belong to the DS native host (PokeScriptJNI.cpp), which the GB build does not
# link — the only native library in the APK carries the GB host. Section 6 keeps
# the Activity from building the bridge early, but the calls INSIDE this file
# were reachable too: on device, Start died with UnsatisfiedLinkError from
# setSpeechBridge before the GB host could register anything. The fix guarded
# the bridge registration and speak(); this rule holds EVERY native call in the
# file to the same pattern — an UnsatisfiedLinkError catch in the calling
# member, with a fail-closed fallback — so the next unguarded call site fails
# here instead of on a device.
ds_src = strip_kt(read(DSSCRIPT_KT))
ds_natives = re.findall(r"external\s+fun\s+(\w+)", ds_src)
if not ds_natives:
    bad("every DS native call is guarded by an UnsatisfiedLinkError catch",
        "no `external fun` declarations found in %s; the file changed shape"
        % DSSCRIPT_KT)
else:
    ds_lines = ds_src.splitlines()
    # Member spans: a member of the object starts at exactly four spaces with
    # `fun`/`val`/`var` (optionally `private`). The `external fun` declarations
    # never match — `external` intervenes — and they sit before the first
    # member, so declaration lines are never mistaken for call sites.
    starts = [i for i, l in enumerate(ds_lines)
              if re.match(r"^    (private )?(fun|val|var) ", l)]
    unguarded = []
    for name in ds_natives:
        call = re.compile(r"(?<![\w.])" + re.escape(name) + r"\s*\(")
        for j, s0 in enumerate(starts):
            e0 = starts[j + 1] if j + 1 < len(starts) else len(ds_lines)
            body = "\n".join(ds_lines[s0:e0])
            if not call.search(body):
                continue
            if "catch" not in body or "UnsatisfiedLinkError" not in body:
                unguarded.append("%s (member at %s:%d)"
                                 % (name, DSSCRIPT_KT, s0 + 1))
    if unguarded:
        bad("every DS native call is guarded by an UnsatisfiedLinkError catch",
            "unguarded: %s" % unguarded)
    else:
        ok("every DS native call is guarded by an UnsatisfiedLinkError catch "
           "(%d natives)" % len(ds_natives))

# ---- 9. THE PSP SESSION: BUTTON IDS, JNI, ROUTING, THE HONEST PAGE ---------
# Stage 3 adds a second session beside the GB one, and the same three defect
# classes apply, with PSP twists:
#  * The button ids are indices into the core's own table, so they are
#    checked against Core/psp_core.h's PSP_BTN_* defines — not against a
#    Kotlin convention, and not against GbKeys (a different core's order).
#  * The PSP core shares the frontend .so with the GB host, so presence is
#    a JNI question: every external the Kotlin declares must resolve to an
#    exact symbol, and availability must be the guarded native probe — the
#    GB zip probe cannot tell the cores apart, so a PSP coreAvailable built
#    on it would say "present" on a build where the subset was left out.
#  * The library page must stop claiming there is no PSP core. A stale
#    platform line is section 4's defect wearing a PSP badge: measured
#    against the build, and the build runs PSP games now.
for rel in (PSP_SESSION_KT, PSP_SCRIPT_KT, PSP_JNI_CPP, PSP_HEADER,
            GAMES_SCREEN_KT):
    if not os.path.exists(rel):
        print("!! %s is missing; refusing to report a pass" % rel)
        sys.exit(1)

psp_session = strip_kt(read(PSP_SESSION_KT))
psp_header = read(PSP_HEADER)
want_psp = {k: int(v) for k, v in
            re.findall(r"#define\s+PSP_BTN_(\w+)\s+(\d+)", psp_header)}
want_psp.pop("COUNT", None)  # a count, not a button; PspKeys holds buttons
block = re.search(r"object\s+PspKeys\s*\{(.*?)\n\}", psp_session, re.S)
if not block:
    bad("PspKeys is defined", "no `object PspKeys {` found")
else:
    got_psp = {k: int(v) for k, v in
               re.findall(r"const\s+val\s+(\w+)\s*=\s*(\d+)", block.group(1))}
    if got_psp == want_psp and len(want_psp) == 12:
        ok("the PSP button ids match psp_core.h exactly (%d buttons)"
           % len(want_psp))
    else:
        diff = {k: (got_psp.get(k), v) for k, v in want_psp.items()
                if got_psp.get(k) != v}
        bad("the PSP button ids match psp_core.h exactly",
            "differs: %s" % diff)

psp_kt = read(PSP_SCRIPT_KT)
psp_jni = read(PSP_JNI_CPP)
psp_jni_symbols = set(re.findall(r"Java_[A-Za-z0-9_]+", psp_jni))
psp_externals = re.findall(r"external\s+fun\s+(\w+)\s*\(", psp_kt)
unbound = []
for name in psp_externals:
    simple = ("Java_me_magnum_melonds_accessibility_"
              "PspAccessibilityScript_" + name)
    if simple not in psp_jni_symbols:
        unbound.append(name)
if psp_externals and not unbound:
    ok("every PSP external fun has its exact JNI symbol (%d)"
       % len(psp_externals))
else:
    bad("every PSP external fun has its exact JNI symbol",
        "unbound: %s" % unbound)

if ("coreAvailable" in psp_session and "isCoreLinked" in psp_session
        and "UnsatisfiedLinkError" in psp_session):
    ok("PSP availability is the guarded native probe "
       "(the cores share one .so)")
else:
    bad("PSP availability is the guarded native probe",
        "the probe must reach the JNI existence check behind an "
        "UnsatisfiedLinkError catch; the GB zip probe cannot tell the "
        "cores apart")

# ⛔ THIS ONE READS THE RAW SCREEN, NOT THE STRIPPED ONE. The picker's MIME
# string ("*/*") contains a "/*", which the comment stripper above reads as
# a block-comment opener; everything from the Select Game button to the next
# real "*/" — the whole Start block, this call included — is stripped as if
# it were prose. (The older checks never noticed: nothing they look for
# lives in that span.) The call is asserted with its parenthesis, and the
# raw file's only occurrence is the call itself.
if ("PspGameSession.start(" in read(SCREEN_KT) and '"iso"' in psp_kt
        and '"cso"' in psp_kt):
    ok("the launcher routes PSP disc images (.iso/.cso) to the PSP session")
else:
    bad("the launcher routes PSP disc images (.iso/.cso) to the PSP session",
        "no PspGameSession.start in the screen, or the PSP extension set "
        "lost iso/cso")

games_screen = strip_kt(read(GAMES_SCREEN_KT))
if "no PSP core" not in games_screen and "run in this Android build" in games_screen:
    ok("the game library's PSP page says PSP games run in this build")
else:
    bad("the game library's PSP page says PSP games run in this build",
        "the platform line is stale: the PSP core is compiled in and the "
        "launcher starts PSP games (stage 3)")

# The launcher's own lines during a PSP session ("Game started.", "State
# saved.") ride the SHARED speech bridge, exactly as the GB session's do:
# the PSP session builds the bridge at start and re-forwards its stored
# announcement view (section 6's two-step, second session), and the
# Activity's announce guard covers a PSP game. Drop any leg and those lines
# are typed into a status field nobody hears — the bridge is null (no
# voice) or the guard is closed (no call).
if ("AccessibilityScript.speechBridge(app)" in psp_session
        and "storedAnnouncementView" in psp_session
        and "PspGameSession.attachAnnouncementView" in activity_src
        and "PspGameSession.isRunning" in activity_src):
    ok("the PSP session builds the shared speech bridge and the launcher guard covers it")
else:
    bad("the PSP session builds the shared speech bridge and the launcher guard covers it",
        "without the bridge built at PSP start, the view re-forward and the "
        "widened announce guard, launcher lines during PSP play reach no voice")

# ---- 10. THE PSP ADAPTER HOST: REGISTRY, COMMANDS, THE SWIFT SETS ----------
# Stage 4 wires the native adapters into the PSP session through
# PspAdapterHost.cpp. The defect classes are the same, one level up:
#  * The host dispatches by DISC_ID against its own PSP table because the
#    registry TU (Core/adapters.cpp) cannot link on Android (two of its
#    adapters need cores this build does not carry). A hand-kept table is
#    a drift risk by construction, so it is checked against the registry:
#    the host must name exactly the adapters whose TUs carry a ULUS code.
#  * The command integers are the C ABI (adapter.h's append-only enum).
#    PspAdapterCommands mirrors the ones the PSP screen sends; a swapped
#    value is a wrong query with no error anywhere — the key-bit defect
#    wearing a reader badge — so each is checked against adapter.h.
#  * The buttons on the PSP screen come from the generated library data,
#    whose command lists are COPIED from AdapterCommand.swift's
#    supported(). The copy is checked against the Swift source itself:
#    inventing a button an adapter does not support, or dropping one it
#    does, both fail here.
#  * The library page must stop saying the PSP reader "arrives in the
#    next stage": it is in the build now, and a stale line is section 9's
#    defect one stage later.
PSP_HOST_CPP = "app/native-overlay/app/src/main/cpp/PspAdapterHost.cpp"
OVERLAY_SH = "scripts/android-apply-overlay.sh"
ADAPTER_H = "Core/adapter.h"
SWIFT_CMD = "Sources/OpenGameAccess/AdapterCommand.swift"
for rel in (PSP_HOST_CPP, OVERLAY_SH, ADAPTER_H, SWIFT_CMD):
    if not os.path.exists(rel):
        print("!! %s is missing; refusing to report a pass" % rel)
        sys.exit(1)

psp_host = read(PSP_HOST_CPP)
overlay_sh = read(OVERLAY_SH)

# The glue wiring: the overlay must copy the host and the adapter layer
# into the frontend's glue dir AND name them in the generated glue list,
# or the stage builds a session with no host behind its JNI forwards.
glue_names = ("PspAdapterHost.cpp", "announce.cpp", "osk_echo.cpp",
              "dissidia_adapter.cpp", "dbzar_adapter.cpp",
              "mku_adapter.cpp", "khbbs_adapter.cpp")
missing_glue = [g for g in glue_names if "glue/" + g not in overlay_sh]
if (not missing_glue and "PspAdapterHost.cpp" in overlay_sh
        and "Core/story" in overlay_sh):
    ok("the overlay wires the adapter host and the four PSP adapter TUs "
       "into the PSP glue")
else:
    bad("the overlay wires the adapter host and the four PSP adapter TUs "
        "into the PSP glue",
        "missing from the generated glue list: %s" % missing_glue)

# The ABI itself: adapter.h's Command enum, values by position.
adapter_h = read(ADAPTER_H)
enum_span = re.search(r"enum class Command \{(.*?)\n\};", adapter_h, re.S)
enum_names = []
if enum_span:
    enum_body = re.sub(r"//[^\n]*", "", enum_span.group(1))
    enum_names = re.findall(r"([A-Za-z_]\w*)\s*,", enum_body)
enum_value = {name: i for i, name in enumerate(enum_names)}
if len(enum_names) == 43 and enum_names[0] == "WhereAmI" \
        and enum_names[-1] == "Objective":
    ok("adapter.h's Command enum parsed (%d commands, append-only order)"
       % len(enum_names))
else:
    bad("adapter.h's Command enum parsed",
        "got %d names, first/last %s/%s"
        % (len(enum_names), enum_names[:1], enum_names[-1:]))

# The registry's PSP set: every adapter struct adapters.cpp registers,
# resolved to the TU that defines it; PSP is a ULUS game code in that TU.
registry = read(REGISTRY_CPP)
registered = sorted(set(re.findall(r"&(k\w+)", registry)))
core_tus = {}
for fname in os.listdir("Core"):
    if fname.endswith(".cpp"):
        core_tus[fname] = read(os.path.join("Core", fname))
expected_psp = set()
for struct in registered:
    for fname, text in core_tus.items():
        block = re.search(r"const Adapter %s = \{(.*?)\n\};" % struct,
                          text, re.S)
        if block:
            # The struct's fields are positional: id, display name,
            # game code — the third string literal is the code the
            # registry itself matches on.
            literals = re.findall(r'"([^"]*)"', block.group(1))
            if len(literals) >= 3 and literals[2].startswith("ULUS"):
                expected_psp.add(struct)
            break
host_set = set(re.findall(r"&oga::(k\w+)", psp_host))
if host_set == expected_psp and len(expected_psp) == 4:
    ok("the host's PSP dispatch table names exactly the registry's "
       "ULUS adapters (%s)" % ", ".join(sorted(expected_psp)))
else:
    bad("the host's PSP dispatch table names exactly the registry's "
        "ULUS adapters",
        "host has %s, the registry's PSP set is %s"
        % (sorted(host_set), sorted(expected_psp)))
if "AdapterNoteSpoken" in psp_host and "AdapterSpeechReset" in psp_host:
    ok("the host carries the adapters' spoken-line dedup helpers")
else:
    bad("the host carries the adapters' spoken-line dedup helpers",
        "the adapters call oga::AdapterNoteSpoken/AdapterSpeechReset; "
        "without definitions in the host TU the glue cannot link")

# The Kotlin command mirror, checked value by value against the enum.
def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])",
                  "_", name).upper()

cmd_block = re.search(r"object\s+PspAdapterCommands\s*\{(.*?)\n\}",
                      psp_session, re.S)
if not cmd_block:
    bad("PspAdapterCommands is defined", "no `object PspAdapterCommands {` found")
else:
    got_cmds = {k: int(v) for k, v in
                re.findall(r"const\s+val\s+(\w+)\s*=\s*(\d+)",
                           cmd_block.group(1))}
    wrong = {}
    for k, v in got_cmds.items():
        match = [n for n in enum_names if snake(n) == k]
        if not match or enum_value[match[0]] != v:
            wrong[k] = (v, enum_value.get(match[0]) if match else None)
    if got_cmds and not wrong and got_cmds.get("STOP_SPEECH") == 37:
        ok("every PspAdapterCommands value matches adapter.h's enum "
           "(%d commands)" % len(got_cmds))
    else:
        bad("every PspAdapterCommands value matches adapter.h's enum",
            "wrong: %s" % wrong)

# The generated command sets vs AdapterCommand.swift's supported().
swift = read(SWIFT_CMD)
sup_start = swift.find("static func supported")
sup_span = swift[sup_start:swift.find("\n}\n", sup_start)] \
    if sup_start >= 0 else ""
swift_sets = {}
for m in re.finditer(
        r'case "([^"]+)":(?:\s|//[^\n]*)*?return \[([^\]]*)\]',
        sup_span):
    names = re.findall(r"\.(\w+)", m.group(2))
    swift_sets[m.group(1)] = [enum_value[n[0].upper() + n[1:]]
                              for n in names]
library_raw = read(LIBRARY_KT)
generated_sets = {}
for m in re.finditer(
        r'AdapterGameInfo\(\s*id = "([^"]+)".*?commands = listOf\(([^)]*)\)',
        library_raw, re.S):
    generated_sets[m.group(1)] = [int(x) for x in
                                  re.findall(r"\d+", m.group(2))]
set_diffs = {}
for gid, gset in generated_sets.items():
    if gset != swift_sets.get(gid, []):
        set_diffs[gid] = (gset, swift_sets.get(gid, []))
for sid in swift_sets:
    if sid not in generated_sets:
        set_diffs[sid] = (None, swift_sets[sid])
if swift_sets and not set_diffs:
    ok("the generated command sets match AdapterCommand.supported() "
       "for every game (%d with buttons)"
       % sum(1 for v in swift_sets.values() if v))
else:
    bad("the generated command sets match AdapterCommand.supported() "
        "for every game", "differs: %s" % set_diffs)

# The generated titles vs the Swift title switch, for every command a
# generated set names (a button with the wrong label is a wrong button).
swift_titles = {}
title_at = swift.find("var title")
title_span = ""
if title_at >= 0:
    title_end = swift.find("\n    var ", title_at + 10)
    title_span = swift[title_at:title_end if title_end >= 0
                       else title_at + 6000]
swift_titles = {n[0].upper() + n[1:]: t for n, t in
                re.findall(r'case \.(\w+):\s*return "([^"]*)"', title_span)}
generated_titles = {int(v): t for v, t in
                    re.findall(r'(\d+) to "([^"]*)"', library_raw)}
title_diffs = {}
for used in {c for s in generated_sets.values() for c in s}:
    name = enum_names[used] if used < len(enum_names) else None
    if generated_titles.get(used) != swift_titles.get(name):
        title_diffs[used] = (generated_titles.get(used),
                             swift_titles.get(name))
if not title_diffs:
    ok("every generated command title matches AdapterCommand.title")
else:
    bad("every generated command title matches AdapterCommand.title",
        "differs: %s" % title_diffs)

# The PSP platform line: the reader is in the build NOW. (Sliced from
# platformLine itself: the screen's other when()s have "psp" rows too.)
games_raw = read(GAMES_SCREEN_KT)
pline_at = games_raw.find("private fun platformLine")
psp_line_span = re.search(r'"psp"\s*->\s*(.*?)\n\s*"snes"',
                          games_raw[pline_at:] if pline_at >= 0 else "",
                          re.S)
psp_line = "".join(re.findall(r'"([^"]*)"', psp_line_span.group(1))) \
    if psp_line_span else ""
if "reader is in the build" in psp_line and "not in the" not in psp_line \
        and "next stage" not in psp_line:
    ok("the game library's PSP page says the reader is in this build")
else:
    bad("the game library's PSP page says the reader is in this build",
        "the platform line is stale: %r" % psp_line)

# The session-side wiring: bridge registered at start and cleared at
# stop, the command entry point, the pad echoes, and the screen groups.
wiring = {
    "the PSP session registers the speech bridge with the adapter host":
        "PspAccessibilityScript.setSpeechBridge(" in psp_session,
    "the PSP session clears the host's speech bridge at stop":
        "setSpeechBridge(null)" in psp_session,
    "the PSP session exposes sendAdapterCommand":
        "fun sendAdapterCommand" in psp_session,
    "the PSP session forwards pad presses as adapter echoes":
        "forwardPadEcho(button)" in psp_session,
    "the PSP screen shows the reader and speech groups":
        # The RAW screen, not the stripped one: the PSP branch lives in
        # the span section 9's note describes (the picker's MIME string
        # blinds the comment stripper to everything after it).
        "PspReaderGroup(PspGameSession)" in read(SCREEN_KT)
        and "PspSpeechGroup(PspGameSession)" in read(SCREEN_KT),
}
for what, present in wiring.items():
    if present:
        ok(what)
    else:
        bad(what, "the stage-4 surface was edited away")

print()
if fails:
    print("FAIL: the Compose launcher contract is broken:")
    for f in fails:
        print("  - %s" % f)
    sys.exit(1)
print("PASS: the Compose launcher contract holds (%d checks)." % checks)
