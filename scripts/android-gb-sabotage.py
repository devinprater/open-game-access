#!/usr/bin/env python3
"""Apply ONE sabotage to a staged copy of the Android bridge files.

Used by scripts/android-gb-bridge-test.sh. Each sabotage removes exactly one rule
the bridge test claims to guard; if the test still passes, the rule was never
covered.

Usage: android-gb-sabotage.py <staged-root> <name>
"""
import io
import sys

root, what = sys.argv[1], sys.argv[2]

KT = "app/src/main/java/com/devin/opengameaccess/GbBridge.kt"
HTML = "app/src/main/assets/index.html"
JNI = "app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp"


def edit(rel, old, new):
    p = root + "/" + rel
    s = io.open(p, encoding="utf-8").read()
    if old not in s:
        sys.exit("sabotage %s: anchor not found in %s" % (what, rel))
    io.open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))


if what == "phantom_callback":
    # Kotlin posts a result to a JS name the page will never define, so the
    # player gets silence on a cancel or an error.
    edit(KT,
         "window.ogaRomPicked && window.ogaRomPicked(null,",
         "window.ogaNope && window.ogaNope(null,")

elif what == "unexported_method":
    # The page calls a bridge method that is no longer @JavascriptInterface, so
    # the call throws at the first tap.
    p = root + "/" + KT
    s = io.open(p, encoding="utf-8").read()
    needle = "@JavascriptInterface"
    idx = s.index("fun stopGame()")
    start = s.rindex(needle, 0, idx)
    s = s[:start] + s[start + len(needle):]
    io.open(p, "w", encoding="utf-8").write(s)

elif what == "wrong_key_mask":
    # A wrong key bit is a wrong button, and silently so.
    edit(HTML, "UP:64", "UP:32")

elif what == "missing_jni_export":
    # An external fun with no JNI body throws UnsatisfiedLinkError at runtime.
    edit(JNI, "setGbHotkey", "setGbHotkeyRenamed")

elif what == "stale_claim":
    # Put the old capability claim back into the page's CODE (not a comment).
    edit(HTML,
         "say('Pok\u00e9mon Access ready. Press Select Game to choose a game.');",
         "say('emulator core integration is not compiled into this test build');")

else:
    sys.exit("unknown sabotage: %s" % what)

print("sabotage applied: %s" % what)
