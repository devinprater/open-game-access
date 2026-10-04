#!/usr/bin/env python3
"""Verify the JS <-> Kotlin bridge contract, in both directions.

⛔ THIS IS THE CHECK THAT MATTERS FOR THIS PLATFORM. There is no Kotlin compiler
on this host, so CI's Gradle step is the only thing that can type-check the
Android side. But the failure mode that has actually bitten this feature is not a
type error — it is a NAME that does not exist: Kotlin posting to a JS function the
page never defines (silence, which reads as a crash to someone who cannot see the
screen), or the page calling a @JavascriptInterface method that was never
exported. Neither is a compile error in either language.

So: parse both files, extract the exported methods and the called names, and
require them to match. This runs in adapter-tests on every Core/** push.

⛔ COMMENTS ARE STRIPPED FIRST. Twice now a check of mine fired on its own
explanatory comment — "the stale text is still there" when the text was only
being described in a comment. A check for what the PLAYER hears must look at what
is said and displayed, not at prose about it.
"""
import io, os, re, sys

ROOT = os.environ.get("OGA_ROOT", os.path.expanduser("~/oga-work"))
if len(sys.argv) > 1:
    ROOT = sys.argv[1]
os.chdir(ROOT)

# ⛔ SANITY: a missing file must be an ERROR, not a pass. An early version of
# this class of check scanned a file that did not exist, found no bad strings in
# nothing, and reported success.
for rel in ("app/src/main/java/com/devin/opengameaccess/GbBridge.kt",
            "app/src/main/assets/index.html",
            "app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp"):
    if not os.path.exists(rel):
        print("!! %s is missing; refusing to report a pass" % rel)
        sys.exit(1)

KT = "app/src/main/java/com/devin/opengameaccess/GbBridge.kt"
HTML = "app/src/main/assets/index.html"

def strip_js_comments(src):
    """Remove // and /* */ comments, keeping string literals intact."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            out.append(c); i += 1
            while i < n and src[i] != q:
                if src[i] == "\\":
                    out.append(src[i]); i += 1
                    if i < n: out.append(src[i]); i += 1
                    continue
                out.append(src[i]); i += 1
            if i < n: out.append(src[i]); i += 1
            continue
        if c == "/" and i + 1 < n and src[i+1] == "/":
            while i < n and src[i] != "\n": i += 1
            continue
        if c == "/" and i + 1 < n and src[i+1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i+1] == "/"): i += 1
            i += 2
            continue
        out.append(c); i += 1
    return "".join(out)

def strip_kt_comments(src):
    """Remove // comments; keep /* */ block comments stripped too."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"(?m)//[^\n]*$", "", src)

kt_raw = io.open(KT, encoding="utf-8").read()
html_raw = io.open(HTML, encoding="utf-8").read()

kt_prose = strip_kt_comments(kt_raw)
html_prose = strip_js_comments(html_raw)

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

# ---- 1. every @JavascriptInterface method the page reaches -----------------
exported = set(re.findall(r'@JavascriptInterface\s+fun\s+(\w+)\s*\(', kt_raw))
called = set(re.findall(r'\boga_gb\.(\w+)\s*\(', html_prose))
missing_on_kt = called - exported
if not missing_on_kt:
    ok("every oga_gb.<method> the page calls is exported (%d methods)" % len(called))
else:
    bad("every oga_gb.<method> the page calls is exported",
        "missing: %s" % sorted(missing_on_kt))

# ---- 2. every Kotlin -> JS callback the page defines ----------------------
kt_calls = set(re.findall(r'window\.(\w+)\s*&&', kt_prose))
page_defines = set(re.findall(r'window\.(\w+)\s*=\s*function', html_prose))
missing_on_js = kt_calls - page_defines
if not missing_on_js:
    ok("every window.<fn> Kotlin calls is defined by the page (%d)" % len(kt_calls))
else:
    bad("every window.<fn> Kotlin calls is defined by the page",
        "missing: %s" % sorted(missing_on_js))

# ---- 3. no callback invoked through a name that does not exist ------------
phantom = re.findall(r'window\.(\w+)\s*&&\s*window\.\1\s*\(', kt_prose)
undefined = [n for n in phantom if n not in page_defines]
if not undefined:
    ok("no Kotlin callback is invoked through a name the page never defines")
else:
    bad("no Kotlin callback is invoked through a name the page never defines",
        "phantom: %s" % undefined)

# ---- 4. the stale claim is gone from CODE (comments excluded) -------------
STALE = "not compiled into this test build"
if STALE in html_prose:
    bad("the stale capability claim is gone from the page's CODE", "still present")
else:
    ok("the stale capability claim is gone from the page's CODE")
if STALE in kt_prose:
    bad("the stale capability claim is gone from Kotlin's CODE", "still present")
else:
    ok("the stale capability claim is gone from Kotlin's CODE")

# The page must ASK rather than assert capability.
if "oga_gb.isAvailable()" in html_prose:
    ok("the page asks the native side whether the core is present")
else:
    bad("the page asks the native side whether the core is present", "it assumes instead")

# ---- 5. the external funs the Kotlin declares must be in the JNI ---------
gb = io.open("app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt",
             encoding="utf-8").read()
jni = io.open("app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp", encoding="utf-8").read()
# ⛔ EXACT SYMBOL NAMES, not substrings. JNI resolves a native method by the
# mangled symbol Java_<package>_<Class>_<method>, so a check that merely greps
# for the method name passes on `setGbHotkeyRenamed` — which is exactly what the
# mutation test caught when this check was first written.
jni_symbols = set(re.findall(r'Java_[A-Za-z0-9_]+', jni))
externals = re.findall(r'external fun\s+(\w+)\s*\(', gb)
unbound = []
for name in externals:
    # A companion-object fun gets _00024Companion_ inserted ("$" -> "_00024");
    # a plain object fun is the simple form. Accept either exact symbol.
    simple = "Java_me_magnum_melonds_accessibility_GbAccessibilityScript_" + name
    companion = "Java_me_magnum_melonds_accessibility_GbAccessibilityScript_00024Companion_" + name
    if simple not in jni_symbols and companion not in jni_symbols:
        unbound.append(name)
if not unbound:
    ok("every external fun in GbAccessibilityScript has its exact JNI symbol (%d)" % len(externals))
else:
    bad("every external fun in GbAccessibilityScript has its exact JNI symbol",
        "unbound: %s" % unbound)

# ---- 6. the key mask the page builds must match mGBA's key order ----------
# The page OR's key bits; a wrong bit is a wrong button, silently.
keydefs = {k: int(v) for k, v in re.findall(r'(\w+)\s*:\s*(\d+)\b', re.search(
    r'var KEY = \{(.*?)\};', html_prose, re.S).group(1))}
expected = {'A':1,'B':2,'SELECT':4,'START':8,'RIGHT':16,'LEFT':32,'UP':64,'DOWN':128,'R':256,'L':512}
if keydefs == expected:
    ok("the page's key mask matches mGBA's key order exactly")
else:
    diff = {k: (keydefs.get(k), v) for k, v in expected.items() if keydefs.get(k) != v}
    bad("the page's key mask matches mGBA's key order exactly", "differs: %s" % diff)

print()
if fails:
    print("FAIL: the Android bridge contract is broken:")
    for f in fails:
        print("  - %s" % f)
    sys.exit(1)
print("PASS: the Android Game Boy bridge contract holds (%d checks)." % checks)
