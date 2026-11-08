// DsSessionJNI.cpp — the JNI surface over the DS host ABI
// (Sources/CPokeCore/include/pokecore.h), driven by DsAccessibilityScript
// (me.magnum.melonds.accessibility) under DsGameSession.
//
// THE HOST IS THE SHARED CORE, EXACTLY AS iOS RUNS IT. Core/pokecore.cpp
// is the Nintendo DS host the iOS app ships: it owns the melonDS::NDS
// instance (direct boot, software renderer), the embedded Lua engine the
// Pokémon Black/White reader script runs in, the native adapter attach
// (the shared Core registry's DS adapters — Fire Emblem: Shadow Dragon,
// Dragon Ball Z: Attack of the Saiyans, Dragon Quest IX), the
// announcement queue and the battle-cue snapshots. This file compiles
// that same translation unit into the Android frontend build (see
// scripts/android-apply-overlay.sh section 9) and exposes it through the
// same lifecycle shape PspSessionJNI established for PSP:
//
//   one core at a time, owned by this file; create/load under a
//   lifecycle lock; frames (the Compose frame clock), button latches and
//   the audio drain then run against the live core concurrently.
//
// WHY NOT THE FILES THAT WERE ALREADY HERE. Two earlier DS starts ship
// in this tree and neither can run a game:
//   - MelonCore.kt / melonds_bridge.cpp (app tree): a stub. Its load
//     checks a pointer, its button press discards the key, no emulation
//     thread ever runs — and the frontend's CMake never compiled the
//     bridge anyway. LuaScriptRunner.kt names a native library that
//     does not exist at all.
//   - PokeScript (overlay, compiled): a real Lua host, but a SCRIPT-ONLY
//     one — it reads a live melonDS::NDS owned by the upstream
//     frontend's own emulator activity, and its JNI counterpart
//     (PokeScriptJNI.cpp, named by AccessibilityScript.kt) was never
//     written. It also has no host for the NATIVE DS adapters, which are
//     half of what DS parity means. pokecore carries both halves, with
//     the iOS play history behind them, so it is the host; PokeScript
//     stays compiled and unused, as it is today, and AccessibilityScript
//     keeps its (guarded, fail-closed) natives unbound by design.
//
// SPEECH. pokecore's announcement queue paces itself by estimate when
// the host uses the PLAIN speech callback (poke_set_speech_callback) —
// the same choice the PSP adapter host makes on Android
// (host_reports_done = false in PspAdapterHost.cpp). The callback
// forwards to the shared AccessibilitySpeech bridge: speak(text,
// interrupt), with a null text meaning stop, the convention
// PspAdapterHost's BridgeSpeak established.
//
// THE ONE PLATFORM SYMBOL. pokecore calls
// melonDS::Platform::PokeSetLogForward (Core/poke_platform.cpp on iOS).
// That file is NOT compiled here: it implements melonDS::Platform file
// IO, which the frontend's own PlatformAndroid.cpp already provides in
// this library — compiling both would collide. pokecore needs only the
// log-forward setter from it, so it is defined below as a no-op (the
// forward only feeds poke_platform's own Log(), which is also not
// compiled here; core log lines reach logcat through the log callback
// instead).
//
// ABI GATING. The overlay (section 9) adds this surface only on the
// 64-bit ABIs, mirroring the PSP subset's stance; on armeabi-v7a these
// symbols are absent and the Kotlin probe (nativeDsAvailable) turns
// that absence into a plain "not in this build".

#include <jni.h>

#include <android/log.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "pokecore.h"
#include "poke_platform.h"

namespace melonDS {
namespace Platform {

// See the header note: the setter only, deliberately a no-op here.
void PokeSetLogForward(PokeLogForward forward) { (void)forward; }

}  // namespace Platform
}  // namespace melonDS

namespace {

// The core's own log lines (adapter/host messages on iOS's console) go
// to logcat under their own tag, the PspSessionJNI "PspCore" pattern.
void CoreLog(const char *text, void * /*userdata*/) {
  __android_log_print(ANDROID_LOG_INFO, "DsCore", "%s", text ? text : "");
}

// ---- the speech bridge (mirrors PspAdapterHost.cpp) ------------------

JavaVM *g_vm = nullptr;
jobject g_bridge = nullptr;
jmethodID g_speakMethod = nullptr;  // speak(Ljava/lang/String;Z)V
jmethodID g_stopMethod = nullptr;   // stop()V

JNIEnv *BridgeEnv(void) {
  if (!g_vm) return nullptr;
  JNIEnv *env = nullptr;
  if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) ==
      JNI_OK) {
    return env;
  }
  // UI-thread calls arrive attached already; the attach path exists for
  // any future caller on a native thread.
  if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
  return nullptr;
}

// Speak one line on the shared bridge; a null text stops speech. No
// bridge installed drops the line silently (the fail-closed shape every
// host in this build uses).
void BridgeSpeak(const char *text, bool interrupt) {
  if (!g_bridge || !g_speakMethod || !g_stopMethod) return;
  JNIEnv *env = BridgeEnv();
  if (!env) return;
  if (!text) {
    env->CallVoidMethod(g_bridge, g_stopMethod);
  } else {
    jstring jtext = env->NewStringUTF(text);
    env->CallVoidMethod(g_bridge, g_speakMethod, jtext,
                        interrupt ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(jtext);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

// pokecore's plain speech callback (see the header note on pacing).
void OnSpeech(const char *text, bool interrupt, void * /*userdata*/) {
  BridgeSpeak(text, interrupt);
}

// ---- the one live core ------------------------------------------------

// Written only under g_lifecycle_lock (load, start, stop); read
// lock-free by the frame/audio/button calls, which the Kotlin session
// confines to the lifetime between a successful load and stop() — the
// same contract PspSessionJNI's callers keep.
PokeCore *g_core = nullptr;
std::string g_game_code;
std::string g_last_error;
// The reader script (BizHawk compat shim + the game's main.lua,
// concatenated by the Kotlin side), applied to each fresh core before
// its ROM loads — pokecore's contract is set-once-before-load, the
// order GameSession.swift uses at init.
std::string g_script;
long long g_frames = 0;
std::mutex g_lifecycle_lock;

std::string FromJString(JNIEnv *env, jstring s) {
  if (!s) return "";
  const char *chars = env->GetStringUTFChars(s, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

void StoreLastError() {
  const char *e = g_core ? poke_last_error(g_core) : nullptr;
  g_last_error = e ? e : "";
}

void DestroyCoreLocked() {
  if (g_core) {
    // ⛔ FLUSH THE BATTERY SAVE BEFORE ANYTHING IS TORN DOWN.
    // pokecore's periodic flush runs once a second (its NdsTick),
    // and neither poke_stop nor poke_destroy flushes — so without
    // this, a game saved in the last second before quit (or before
    // a new load replaced this core) silently loses that save.
    poke_flush_save(g_core);
    poke_stop(g_core);
    poke_destroy(g_core);
    g_core = nullptr;
  }
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsAvailable(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer: where the DS glue is not
  // compiled (armeabi-v7a), the symbol is absent and the Kotlin call
  // throws UnsatisfiedLinkError instead of reaching here.
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_setDsSpeechBridge(
    JNIEnv *env, jobject /*self*/, jobject bridge) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (g_bridge) {
    env->DeleteGlobalRef(g_bridge);
    g_bridge = nullptr;
    g_speakMethod = nullptr;
    g_stopMethod = nullptr;
  }
  if (!bridge) return;
  env->GetJavaVM(&g_vm);
  jclass bridgeClass = env->GetObjectClass(bridge);
  g_speakMethod =
      env->GetMethodID(bridgeClass, "speak", "(Ljava/lang/String;Z)V");
  g_stopMethod = env->GetMethodID(bridgeClass, "stop", "()V");
  env->DeleteLocalRef(bridgeClass);
  if (!g_speakMethod || !g_stopMethod) {
    g_speakMethod = nullptr;
    g_stopMethod = nullptr;
    return;
  }
  g_bridge = env->NewGlobalRef(bridge);
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsSetScript(
    JNIEnv *env, jobject /*self*/, jstring script) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  g_script = FromJString(env, script);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsLoadRom(
    JNIEnv *env, jobject /*self*/, jstring rom_path, jstring save_path) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
  g_game_code.clear();
  g_last_error.clear();
  g_frames = 0;

  const std::string rom = FromJString(env, rom_path);
  const std::string save = FromJString(env, save_path);

  PokeCore *core = poke_create();
  if (!core) {
    g_last_error = "poke_create returned null";
    return JNI_FALSE;
  }
  poke_set_log_callback(core, &CoreLog, nullptr);
  poke_set_speech_callback(core, &OnSpeech, nullptr);
  // Always applied, even empty: the session sets the script on every
  // start, and a stale script must never leak into a later game.
  poke_set_script(core, g_script.c_str());
  if (!poke_load_rom(core, rom.c_str(), save.c_str())) {
    const char *e = poke_last_error(core);
    g_last_error = e ? e : "";
    poke_destroy(core);
    return JNI_FALSE;
  }
  g_core = core;
  const char *code = poke_game_code(core);
  g_game_code = code ? code : "";
  return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsGameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsLastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsStart(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no DS game is loaded";
    return JNI_FALSE;
  }
  if (!poke_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsStop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsRunning(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && poke_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsFrame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  if (!core || !poke_frame(core)) return JNI_FALSE;
  ++g_frames;
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsFramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (jlong)g_frames;
}

// Buttons are POKE_BTN_* ids (Sources/CPokeCore/include/pokecore.h) —
// DsGameSession's DsKeys mirror, checked by the launcher contract test.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsSetButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jboolean down) {
  PokeCore *core = g_core;
  if (core) poke_set_button(core, (int)button, down == JNI_TRUE);
}

// A tap counted in emulated frames (poke_tap_button): the core holds
// the press across frame boundaries itself, so the game samples it
// whatever the host's timing does — the fix for taps whose
// wall-clock release fired before any frame ran.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsTapButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jint frames) {
  PokeCore *core = g_core;
  if (core) poke_tap_button(core, (int)button, (int)frames);
}

// The DS touch screen, in the bottom screen's own 0..255 / 0..191
// coordinates (poke_touch). Coarse taps are what the session screen
// offers; see DsGameSession's touch group for the honest limits.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsTouch(
    JNIEnv * /*env*/, jobject /*self*/, jint x, jint y, jboolean down) {
  PokeCore *core = g_core;
  if (core) poke_touch(core, (int)x, (int)y, down == JNI_TRUE);
}

// A reader-script hotkey: one upper-case letter ("R", "C", ...), the
// set GameSystem.swift's DS script groups name.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsSetHotkey(
    JNIEnv * /*env*/, jobject /*self*/, jchar key, jboolean down) {
  PokeCore *core = g_core;
  if (!core) return;
  const char letter[2] = {(char)key, '\0'};
  poke_set_hotkey(core, letter, down == JNI_TRUE);
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsReadAudio(
    JNIEnv *env, jobject /*self*/, jshortArray out, jint max_frames) {
  PokeCore *core = g_core;
  if (!core || !out || max_frames <= 0) return 0;
  const jsize capacity_frames = env->GetArrayLength(out) / 2;
  const int want = (int)std::min<jint>(max_frames, capacity_frames);
  if (want <= 0) return 0;
  // Through a scratch buffer rather than the array's elements (the
  // PspSessionJNI pattern): only the frames actually drained are
  // committed to the caller's array.
  static thread_local std::vector<int16_t> scratch;
  scratch.resize((size_t)want * 2);
  const int frames = poke_read_audio(core, scratch.data(), want);
  if (frames > 0) env->SetShortArrayRegion(out, 0, frames * 2, scratch.data());
  return frames;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsSetAudioEnabled(
    JNIEnv * /*env*/, jobject /*self*/, jboolean enabled) {
  PokeCore *core = g_core;
  if (core) poke_set_audio_enabled(core, enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsSaveState(
    JNIEnv *env, jobject /*self*/, jstring path) {
  PokeCore *core = g_core;
  if (!core) return JNI_FALSE;
  if (!poke_save_state(core, FromJString(env, path).c_str())) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsLoadState(
    JNIEnv *env, jobject /*self*/, jstring path) {
  PokeCore *core = g_core;
  if (!core) return JNI_FALSE;
  if (!poke_load_state(core, FromJString(env, path).c_str())) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

// ---- the adapter surface ----------------------------------------------
// Thin forwards to pokecore's registry answers. The command integer is
// oga::Command's raw value (Core/adapter.h); DsGameSession's
// DsAdapterCommands mirror is what the UI sends, and the contract test
// checks it against adapter.h itself.

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsAdapterId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_adapter_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsAdapterName(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *name = core ? poke_adapter_name(core) : nullptr;
  return env->NewStringUTF(name ? name : "");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsAdapterReady(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  // The native side attaches lazily inside this call (the adapter's
  // memory anchors only exist once the game has booted), so the
  // session polls it once a frame — pokecore's own contract.
  return (core && poke_adapter_ready(core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsAdapterCommand(
    JNIEnv * /*env*/, jobject /*self*/, jint cmd) {
  PokeCore *core = g_core;
  return (core && poke_command(core, (int)cmd)) ? JNI_TRUE : JNI_FALSE;
}

// The cue poll: one frame of battle-cue data, packed into two scratch
// arrays the Kotlin side owns, IN THE PSP POLL'S EXACT PACKING (see
// PspSessionJNI's nativePspCuePoll) so the session-side cue logic is
// the same shape on both consoles. ints: [0]=beacon state, [1]=field
// active, [2]=field kind, [3]=heading live, [4]=one-shot health
// bitmask, [5]=guard window active, [6]=guard window beat. floats:
// [0]=lock distance, [1]=field distance, [2]=bearing degrees,
// [3]=foe hz, [4]=foe pan, [5]=own hz, [6]=own pan, [7]=guard window
// fraction. pokecore's four poke_cue_* calls share one per-frame
// snapshot (its CueSnap cache), so asking all four here answers from
// the same moment — the one-snapshot rule PspAdapterHostCuePoll states
// for the PSP side. The arrays are written even on a false return, so
// a refused poll reads as silence, never as last frame's data. False
// means no reader is selected for this game; the cue calls themselves
// never attach (the session's per-frame adapterReady poll does that).
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_DsAccessibilityScript_nativeDsCuePoll(
    JNIEnv *env, jobject /*self*/, jintArray ints, jfloatArray floats) {
  if (!ints || !floats) return JNI_FALSE;
  if (env->GetArrayLength(ints) < 7 || env->GetArrayLength(floats) < 8) {
    return JNI_FALSE;
  }
  int state = 0, field = 0, kind = 0, heading_live = 0, health_bits = 0;
  int guard_active = 0, guard_beat = 0;
  float dist = 0.0f, bearing = 0.0f, field_dist = 0.0f;
  float foe_hz = 0.0f, foe_pan = 0.0f, own_hz = 0.0f, own_pan = 0.0f;
  float guard_frac = 0.0f;

  PokeCore *core = g_core;
  const char *adapter_id = core ? poke_adapter_id(core) : nullptr;
  const bool selected = adapter_id && adapter_id[0] != '\0';
  if (selected) {
    state = poke_cue_snapshot(core, &dist);
    field = poke_cue_snapshot_field(core, &kind, &bearing, &field_dist,
                                    &heading_live);
    health_bits = poke_cue_health(core, &foe_hz, &foe_pan, &own_hz, &own_pan);
    guard_active = poke_cue_guard(core, &guard_frac, &guard_beat);
  }

  const jint iv[7] = {state, field, kind, heading_live, health_bits,
                      guard_active, guard_beat};
  const jfloat fv[8] = {dist, field_dist, bearing, foe_hz,
                        foe_pan, own_hz, own_pan, guard_frac};
  env->SetIntArrayRegion(ints, 0, 7, iv);
  env->SetFloatArrayRegion(floats, 0, 8, fv);
  return selected ? JNI_TRUE : JNI_FALSE;
}
