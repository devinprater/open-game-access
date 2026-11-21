// GenesisSessionJNI.cpp — the JNI surface over the shared Core host ABI
// (Sources/CPokeCore/include/pokecore.h), driven by
// GenesisAccessibilityScript (me.magnum.melonds.accessibility) under
// GenesisGameSession.
//
// THE HOST IS THE SHARED CORE, THE DS SESSION'S SHAPE EXACTLY.
// Core/pokecore.cpp routes a Genesis cartridge to the Genesis Plus GX
// backend (Core/genesis_core.cpp, the wrapper the iOS build compiles
// from the same sources) by extension, exactly as it routes a DS
// cartridge to melonDS. This file exposes that host through the same
// lifecycle PspSessionJNI established and DsSessionJNI repeated:
//
//   one core at a time, owned by this file; create/load under a
//   lifecycle lock; frames (the Compose frame clock), button latches
//   and the audio drain then run against the live core concurrently.
//
// WHAT IS NOT HERE, AND WHY. No reader script (Genesis has no Lua
// reader and pokecore's Genesis ops table names no reader set), no
// touch, no hotkeys, no cue poll: the registry's Genesis row
// (oga::kSegaGenesis) is a console row that REFUSES to attach, on
// purpose — there is no Genesis reader, so there is nothing to poll
// and no command a reader could take. The adapter forwards below
// exist so the session can ask and be told the truth (the id answers
// "genesis"; ready never answers true; commands are refused), not
// because a reader is hiding behind them.
//
// THE BATTERY SAVE. pokecore's save path for a Genesis load IS the
// cartridge's .sav file: the wrapper restores it into the core's SRAM
// at load and flushes it back on genesis_stop — which poke_stop
// reaches — when the bytes changed. DestroyCoreLocked therefore runs
// the DS order (flush, stop, destroy) and the stop is the step that
// lands the save; poke_flush_save itself is the DS backend's own
// flush and a no-op here. EEPROM/flash-save cartridges keep their
// saves inside the chip emulation, not in that .sav; they persist
// through save states only (stated in genesis_core.h and the PR).
//
// SPEECH. As on the DS side: pokecore's announcement queue paces
// itself by estimate under the PLAIN speech callback, forwarded to
// the shared AccessibilitySpeech bridge (speak(text, interrupt); a
// null text stops). A Genesis game raises no announcements of its
// own — no reader — but the session's lifecycle runs over the same
// bridge the launcher's lines use, so the plumbing is identical.
//
// THE PLATFORM SYMBOL IS NOT REDEFINED HERE. pokecore needs
// melonDS::Platform::PokeSetLogForward; DsSessionJNI.cpp defines it
// in this same library. One definition, theirs.
//
// ABI GATING. The overlay (section 10) compiles the Genesis glue only
// on the 64-bit ABIs, mirroring sections 8/9; on armeabi-v7a these
// symbols are absent and the Kotlin probe (nativeGenesisAvailable)
// turns that absence into a plain "not in this build".

#include <jni.h>

#include <android/log.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "pokecore.h"

namespace {

// The core's own log lines go to logcat under their own tag, the
// PspSessionJNI "PspCore" / DsSessionJNI "DsCore" pattern.
void CoreLog(const char *text, void * /*userdata*/) {
  __android_log_print(ANDROID_LOG_INFO, "GenesisCore", "%s",
                      text ? text : "");
}

// ---- the speech bridge (mirrors DsSessionJNI's) ----------------------

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
  if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
  return nullptr;
}

// Speak one line on the shared bridge; a null text stops speech. No
// bridge installed drops the line silently (the fail-closed shape
// every host in this build uses).
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
// same contract DsSessionJNI's callers keep.
PokeCore *g_core = nullptr;
std::string g_game_code;
std::string g_last_error;
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
    // The DS order: flush, stop, destroy. For this backend the flush
    // that lands the battery save is inside poke_stop (genesis_stop
    // writes the .sav back when it changed); poke_flush_save is the
    // DS backend's flush and does nothing here — it stays in the
    // sequence so the two hosts' teardown reads the same.
    poke_flush_save(g_core);
    poke_stop(g_core);
    poke_destroy(g_core);
    g_core = nullptr;
  }
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisAvailable(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer: where the Genesis glue is
  // not compiled (armeabi-v7a), the symbol is absent and the Kotlin
  // call throws UnsatisfiedLinkError instead of reaching here.
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_setGenesisSpeechBridge(
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

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisLoadRom(
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
  // No script to set: the Genesis backend has no reader set (its ops
  // table's reader_set is null), so there is nothing to hand over —
  // the DS surface's set-on-every-load step has no counterpart here.
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
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisGameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisLastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisStart(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no Genesis game is loaded";
    return JNI_FALSE;
  }
  if (!poke_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisStop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisRunning(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && poke_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisFrame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  if (!core || !poke_frame(core)) return JNI_FALSE;
  ++g_frames;
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisFramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (jlong)g_frames;
}

// Buttons are POKE_BTN_* ids (Sources/CPokeCore/include/pokecore.h) —
// GenesisGameSession's GenesisKeys mirror (the Genesis names ride the
// shared slots: Mode on Select, X on L, Z on R, C on X), checked by
// the launcher contract test.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisSetButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jboolean down) {
  PokeCore *core = g_core;
  if (core) poke_set_button(core, (int)button, down == JNI_TRUE);
}

// A tap counted in emulated frames (poke_tap_button): the core holds
// the press across frame boundaries itself, so the game samples it
// whatever the host's timing does.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisTapButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jint frames) {
  PokeCore *core = g_core;
  if (core) poke_tap_button(core, (int)button, (int)frames);
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisReadAudio(
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
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisSetAudioEnabled(
    JNIEnv * /*env*/, jobject /*self*/, jboolean enabled) {
  PokeCore *core = g_core;
  if (core) poke_set_audio_enabled(core, enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisSaveState(
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
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisLoadState(
    JNIEnv *env, jobject /*self*/, jstring path) {
  PokeCore *core = g_core;
  if (!core) return JNI_FALSE;
  if (!poke_load_state(core, FromJString(env, path).c_str())) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

// The game picture: the PSP copy's exact shape (nativePspCopyFrame),
// over pokecore's framebuffer pair. poke_framebuffer STAGES the
// pixels in the backend (Genesis converts its 32bpp output to
// RGBA8888 as the frame runs; the fill call is still made first, the
// contract every backend observes), and the pointer accessor hands
// back that staging buffer — never a second render. Screen index 0:
// the Genesis has one screen. The staged pixels are RGBA8888 bytes;
// copied word-wise they are the same jints the GB/PSP copies return,
// so the one GamePicture consumer serves all three.
extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisCopyFrame(
    JNIEnv *env, jobject /*self*/, jintArray out, jintArray sizeOut) {
  PokeCore *core = g_core;
  if (!core || !out) return 0;
  int w = 0, h = 0;
  if (!poke_framebuffer(core, 0, &w, &h) || w <= 0 || h <= 0) return 0;
  const uint8_t *fb = poke_framebuffer_ptr(core, 0);
  if (!fb) return 0;
  const int count = w * h;
  if (env->GetArrayLength(out) < count) return 0;
  std::vector<jint> words((size_t)count);
  std::memcpy(words.data(), fb, (size_t)count * 4);
  env->SetIntArrayRegion(out, 0, count, words.data());
  if (sizeOut && env->GetArrayLength(sizeOut) >= 2) {
    jint size[2] = {(jint)w, (jint)h};
    env->SetIntArrayRegion(sizeOut, 0, 2, size);
  }
  return count;
}

// ---- the adapter surface ----------------------------------------------
// Thin forwards to pokecore's registry answers, present so the
// session can ask and be told the truth: the Genesis row's attach
// refuses by design (there is no Genesis reader), so ready never
// answers true and commands are refused. The session draws no reader
// controls from these answers; they exist for the log and for the day
// a Genesis reader exists to be asked about.

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisAdapterId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_adapter_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisAdapterName(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *name = core ? poke_adapter_name(core) : nullptr;
  return env->NewStringUTF(name ? name : "");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisAdapterReady(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  return (core && poke_adapter_ready(core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GenesisAccessibilityScript_nativeGenesisAdapterCommand(
    JNIEnv * /*env*/, jobject /*self*/, jint cmd) {
  PokeCore *core = g_core;
  return (core && poke_command(core, (int)cmd)) ? JNI_TRUE : JNI_FALSE;
}
