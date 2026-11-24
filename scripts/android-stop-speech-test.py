#!/usr/bin/env python3
"""android-stop-speech-test.py -- Stop speech silences speech and NEVER ends the game.

The bug this exists for: on Android, Game Boy's Stop speech (the launcher button and
the controller chord) called the session's native stop, which tore the core down. The
voice went quiet because the game was gone, and the screen still said it was running.

This gate holds EVERY Android game session to one contract, the one iOS's
GameSession.stopSpeech already keeps (stop the platform voice, then send the core
StopSpeech so its announcement queue is cleared and a queued line cannot start up again):

  1. Each *GameSession.kt has a stopSpeech, and its body tears nothing down: no call
     to the session's own stop(), no <Console>AccessibilityScript.stop(), no
     stopAudio(), no `running = false`, nothing that destroys, unloads or resets.
  2. Each stopSpeech sends the core StopSpeech: adapterCommand(<X>AdapterCommands.
     STOP_SPEECH), or Game Boy's own GbAccessibilityScript.stopSpeech().
  3. Every <X>AdapterCommands.STOP_SPEECH equals adapter.h's Command::StopSpeech.
  4. The native side of that command only reaches poke_command (or the PSP host's
     mirror of it), never a teardown, and the PSP host answers StopSpeech itself
     before any adapter dispatch.
  5. The controller's Stop speech chord calls only stopSpeech functions, and names
     every session the chord can reach (all but DS, whose script has its own stop key).

A NEW CONSOLE'S SESSION IS CAUGHT AUTOMATICALLY: the session list is the directory's
*GameSession.kt files, not a list kept here. Needs only python3.
"""
import glob
import os
import re
import sys

# OGA_STOP_SPEECH_ROOT lets android-stop-speech-test.sh point this at a mutated copy.
ROOT = os.environ.get("OGA_STOP_SPEECH_ROOT") or \
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KT = os.path.join(ROOT, "app/src/main/java/com/devin/opengameaccess")
CPP = os.path.join(ROOT, "app/native-overlay/app/src/main/cpp")
ADAPTER_H = os.path.join(ROOT, "Core/adapter.h")

fails = []
checks = 0


def ok(what):
    global checks
    checks += 1
    print("  ok   " + what)


def bad(what, why):
    global checks
    checks += 1
    print("  FAIL " + what + "\n       " + why)
    fails.append(what)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def body(src, head):
    """The brace-balanced body that follows `head` (comments removed), or None."""
    i = src.find(head)
    if i < 0:
        return None
    j = src.find("{", i)
    depth = 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return strip_comments(src[j:k + 1])
    return None


# ---- the enum value ---------------------------------------------------------------
enum_src = strip_comments(read(ADAPTER_H))
m = re.search(r"enum\s+class\s+Command\s*\{(.*?)\}", enum_src, re.S)
names = [n.strip().split("=")[0].strip() for n in m.group(1).split(",")] if m else []
names = [n for n in names if n]
stop_value = names.index("StopSpeech") if "StopSpeech" in names else None
if stop_value is not None and "=" not in m.group(1):
    ok("adapter.h's Command::StopSpeech is %d" % stop_value)
else:
    bad("adapter.h's Command::StopSpeech is readable",
        "could not read the Command enum (or it gained explicit values)")

# ---- 1-3: every session -------------------------------------------------------------
TEARDOWN = [
    (r"(?<![\w.])stop\s*\(\s*\)", "calls the session's own stop()"),
    (r"\w+AccessibilityScript\s*\.\s*stop\s*\(", "stops the console's native core"),
    (r"\bstopAudio\s*\(", "stops the game's audio"),
    (r"\brunning\s*=\s*false", "marks the game not running"),
    (r"(?i)\b(destroy|teardown|unload|release|reset)\w*\s*\(", "destroys, unloads or resets"),
]
sessions = sorted(glob.glob(os.path.join(KT, "*GameSession.kt")))
if len(sessions) >= 6:
    ok("found %d Android game sessions" % len(sessions))
else:
    bad("found the Android game sessions", "only %d *GameSession.kt files" % len(sessions))

all_src = {os.path.basename(p): read(p) for p in glob.glob(os.path.join(KT, "*.kt"))}
command_objects = {}
for name, src in all_src.items():
    for obj in re.finditer(r"object\s+(\w+AdapterCommands)\s*\{(.*?)\n\}", src, re.S):
        v = re.search(r"const\s+val\s+STOP_SPEECH\s*=\s*(\d+)", obj.group(2))
        if v:
            command_objects[obj.group(1)] = int(v.group(1))

session_names = []
for path in sessions:
    fname = os.path.basename(path)
    sname = fname[:-len(".kt")]
    session_names.append(sname)
    src = all_src[fname]
    stop = body(src, "fun stopSpeech(")
    if stop is None:
        bad("%s has a stopSpeech" % sname, "no `fun stopSpeech(` in %s" % fname)
        continue
    hits = [why for rx, why in TEARDOWN if re.search(rx, stop)]
    # The platform voice's own stop is speech, not the game.
    if hits == ["calls the session's own stop()"] and \
            not re.search(r"(?<![\w.])stop\s*\(\s*\)",
                          re.sub(r"speechBridge\([^)]*\)\s*\.\s*stop\s*\(\s*\)", "", stop)):
        hits = []
    if hits:
        bad("%s.stopSpeech leaves the game running" % sname, "; ".join(hits))
    else:
        ok("%s.stopSpeech leaves the game running" % sname)

    sent = re.findall(r"adapterCommand\s*\(\s*(\w+AdapterCommands)\s*\.\s*STOP_SPEECH\s*\)", stop)
    if sent or "GbAccessibilityScript.stopSpeech()" in stop:
        ok("%s.stopSpeech clears the core's queued speech" % sname)
    else:
        bad("%s.stopSpeech clears the core's queued speech" % sname,
            "send adapterCommand(<X>AdapterCommands.STOP_SPEECH), as iOS sends "
            "poke_command(StopSpeech), or a queued line starts again")
    for obj in sent:
        if command_objects.get(obj) == stop_value and stop_value is not None:
            ok("%s.STOP_SPEECH is adapter.h's StopSpeech (%d)" % (obj, stop_value))
        else:
            bad("%s.STOP_SPEECH is adapter.h's StopSpeech" % obj,
                "it is %s, adapter.h says %s" % (command_objects.get(obj), stop_value))

# ---- 4: the native side ---------------------------------------------------------------
NATIVE_TEARDOWN = r"\b(poke_stop|poke_destroy|DestroyCore\w*|\w+Stop\s*\(|delete\s+\w+|\.reset\s*\()"
for path in sorted(glob.glob(os.path.join(CPP, "*SessionJNI.cpp"))):
    fname = os.path.basename(path)
    src = read(path)
    heads = re.findall(r"(Java_\w+_native\w*(?:AdapterCommand|StopSpeech))\s*\(", src)
    for head in heads:
        b = body(src, head + "(")
        reaches = b is not None and ("poke_command(" in b or "PspAdapterHostCommand(" in b)
        tears = b is not None and re.search(NATIVE_TEARDOWN, b)
        if reaches and not tears:
            ok("%s: %s only sends the command" % (fname, head.split("_native")[-1]))
        else:
            bad("%s: %s only sends the command" % (fname, head.split("_native")[-1]),
                "it must reach poke_command (or PspAdapterHostCommand) and tear nothing down")

psp_host = read(os.path.join(CPP, "PspAdapterHost.cpp"))
cmd = body(psp_host, "bool PspAdapterHostCommand(int cmd)")
if cmd:
    s = cmd.find("Command::StopSpeech")
    d = cmd.find("g_adapter->command(command)")
    branch = cmd[s:cmd.find("}", s)] if s >= 0 else ""
    if s >= 0 and d > s and "return true" in branch and not re.search(NATIVE_TEARDOWN, branch):
        ok("the PSP host answers StopSpeech itself, before any adapter dispatch")
    else:
        bad("the PSP host answers StopSpeech itself, before any adapter dispatch",
            "PspAdapterHostCommand's StopSpeech branch moved or grew a teardown")
else:
    bad("the PSP host has PspAdapterHostCommand", "not found in PspAdapterHost.cpp")

# ---- 5: the controller chord -----------------------------------------------------------
ctrl = all_src.get("ControllerInput.kt", "")
i = ctrl.find("Action.StopSpeech ->")
chord = body(ctrl, "Action.StopSpeech ->") if i >= 0 else None
if chord is None:
    bad("the controller's Stop speech chord is wired", "no `Action.StopSpeech ->` branch")
else:
    calls = re.findall(r"(\w+GameSession)\s*\.\s*(\w+)\s*\(", chord)
    wrong = sorted({"%s.%s" % c for c in calls if c[1] not in ("stopSpeech",)})
    if calls and not wrong:
        ok("the controller's Stop speech chord calls only stopSpeech")
    else:
        bad("the controller's Stop speech chord calls only stopSpeech",
            "it also calls %s" % ", ".join(wrong) if wrong else "it calls nothing")
    reached = {c[0] for c in calls if c[1] == "stopSpeech"}
    missing = sorted(set(session_names) - reached - {"DsGameSession"})
    if not missing:
        ok("the chord reaches every session but DS's (%d)" % len(reached))
    else:
        bad("the chord reaches every session but DS's",
            "a running %s game would fall through to another console's stop: add it"
            % ", ".join(missing))

print()
if fails:
    print("FAIL: Stop speech can end a game on Android (%d of %d checks):" % (len(fails), checks))
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("PASS: Stop speech only stops speech, on every Android session (%d checks)." % checks)
