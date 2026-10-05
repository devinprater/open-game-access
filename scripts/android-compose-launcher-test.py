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
JNI_CPP = "app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp"
LEGACY_HTML = "app/src/main/assets/index.html"

# ⛔ SANITY: a missing file must be an ERROR, not a pass. An early version of this
# class of check scanned a file that did not exist, found no bad strings in nothing,
# and reported success.
for rel in (SESSION_KT, SCREEN_KT, ACTIVITY_KT, GBSCRIPT_KT, JNI_CPP):
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
# an unattached view announces nothing.
if "SideEffect" in activity:
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

print()
if fails:
    print("FAIL: the Compose launcher contract is broken:")
    for f in fails:
        print("  - %s" % f)
    sys.exit(1)
print("PASS: the Compose launcher contract holds (%d checks)." % checks)
