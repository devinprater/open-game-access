// Ps2SessionJNI.cpp — the JNI surface over the shared Core host ABI
// (Sources/CPokeCore/include/pokecore.h), driven by
// Ps2AccessibilityScript (me.magnum.melonds.accessibility) under
// Ps2GameSession.
//
// A SIBLING OF Ps2SessionJNI.cpp, NOT A VARIANT. Core/pokecore.cpp
// routes a PlayStation 2 disc to the PS2 backend (Core/ps2_core.cpp —
// the wrapper that dlopen()s a libretro core, Play! or ARMSX2) by its
// header for the shared .iso name, exactly as it routes a PlayStation
// disc to PCSX ReARMed. This file exposes that host with the lifecycle
// the Genesis, DS, PSP and PlayStation surfaces share:
//
//   one core at a time, owned by this file; create/load under a
//   lifecycle lock; frames (the Compose frame clock), button latches
//   and the audio drain then run against the live core concurrently.
//
// NO READER YET. The registry's PlayStation 2 row
// (oga::kSonyPlayStation2) is a console row that REFUSES to attach: no
// PlayStation 2 reader exists. The adapter forwards below let the
// session ask and be told that truth; the RAM a reader will need is
// already exposed to Core adapters through ps2_read.
//
// THE CORE AND BIOS FOLDERS. The wrapper dlopen()s its core, so the
// session hands it two folders before every load (the setters below):
// the app's native library directory, where the overlay stages the
// pinned core libraries, and the folder the player's BIOS is staged
// under (as pcsx2/bios). With a BIOS the wrapper prefers ARMSX2; with
// none, Play! runs its HLE BIOS.
//
// THE MEMORY CARDS. pokecore's save path for a PlayStation 2 load is a
// FOLDER, not a file (the one deliberate difference from the
// PlayStation): the cores keep their own memory-card files in it, so
// there is nothing of ours to flush at stop. DestroyCoreLocked runs
// the shared order (flush, stop, destroy) anyway, so every host's
// teardown reads the same.
//
// SPEECH. pokecore's plain speech callback, forwarded to the shared
// AccessibilitySpeech bridge, exactly as the Genesis surface does.
//
// THE PLATFORM SYMBOL IS NOT REDEFINED HERE. DsSessionJNI.cpp defines
// melonDS::Platform::PokeSetLogForward in this same library.
//
// ABI GATING. The overlay (section 10d) compiles the PlayStation 2
// wrapper and this file only on arm64-v8a (the pinned cores are arm64
// builds); on every other ABI these symbols are absent and the Kotlin
// probe (nativePs2Available) turns that absence into a plain
// "not in this build".

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
  __android_log_print(ANDROID_LOG_INFO, "Ps2Core", "%s",
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
// The in-game debug channel's folder (docs/design/debug-channel.md):
// cmd.txt in, replies.log out. Kept across loads and handed to every core
// this file creates, the same as the Genesis surface.
std::string g_debug_dir;
// The PS2 core-library folder and BIOS staging folder (see the header
// note): kept across loads and handed to every core this file creates.
std::string g_core_dir;
std::string g_system_dir;
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
    // The shared order: flush, stop, destroy. For this backend there
    // is nothing of ours to flush (the memory cards are the core's own
    // files in the save folder); poke_flush_save is the DS backend's
    // flush and does nothing here — it stays in the sequence so every
    // host's teardown reads the same.
    poke_flush_save(g_core);
    poke_stop(g_core);
    poke_destroy(g_core);
    g_core = nullptr;
  }
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2Available(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer: where the PlayStation glue is
  // not compiled (armeabi-v7a), the symbol is absent and the Kotlin
  // call throws UnsatisfiedLinkError instead of reaching here.
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_setPs2SpeechBridge(
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
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2LoadRom(
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
  poke_debug_set_dir(core, g_debug_dir.c_str());
  poke_set_speech_callback(core, &OnSpeech, nullptr);
  // The core and BIOS folders go in before the load: the wrapper
  // picks its core (ARMSX2 when a BIOS is staged, else Play!) from
  // them, and the core reads its system files from the BIOS folder.
  poke_set_ps2_core_dir(core, g_core_dir.c_str());
  poke_set_ps2_system_dir(core, g_system_dir.c_str());
  // No script to set: the PlayStation 2 backend has no reader set (its
  // ops table's reader_set is null), so there is nothing to hand over —
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
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2GameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2LastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2Start(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no PlayStation 2 game is loaded";
    return JNI_FALSE;
  }
  if (!poke_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2Stop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2Running(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && poke_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2Frame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  if (!core || !poke_frame(core)) return JNI_FALSE;
  ++g_frames;
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2FramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (jlong)g_frames;
}

// Buttons are POKE_BTN_* ids (Sources/CPokeCore/include/pokecore.h) —
// Ps2GameSession's Ps2Keys mirror (the PlayStation names ride the
// shared slots: Cross on A, Circle on B, Triangle on X, Square on Y,
// L1 on L, R1 on R), checked by the launcher contract test.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jboolean down) {
  PokeCore *core = g_core;
  if (core) poke_set_button(core, (int)button, down == JNI_TRUE);
}

// A tap counted in emulated frames (poke_tap_button): the core holds
// the press across frame boundaries itself, so the game samples it
// whatever the host's timing does.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2TapButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jint frames) {
  PokeCore *core = g_core;
  if (core) poke_tap_button(core, (int)button, (int)frames);
}

// The DualShock 2's sticks: stick 0 the left, 1 the right; each axis
// -1..1, +x right, +y down (pokecore.h).
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetAnalog(
    JNIEnv * /*env*/, jobject /*self*/, jint stick, jfloat x, jfloat y) {
  PokeCore *core = g_core;
  if (!core) return;
  if (stick == 0) poke_set_analog(core, (float)x, (float)y);
  else if (stick == 1) poke_set_analog_right(core, (float)x, (float)y);
}

// The loaded disc's own frame rate (50 for a PAL disc, about 59.94
// for NTSC), or 0 before a load. The frame clock paces itself by this,
// so a PAL game runs at its real speed instead of a fifth too fast.
extern "C" JNIEXPORT jdouble JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2FrameRate(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  return core ? (jdouble)poke_frame_rate(core) : 0.0;
}

// Whether the first bytes of a picked file are a PlayStation 2 disc
// (Core/systems.cpp's sniff, shared with the iPhone and the core's own
// resolver). The launcher reads a .iso's header through its document
// stream and asks here before it decides which console runs it.
extern "C" bool oga_disc_header_is_ps2(const uint8_t *data, size_t len);

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2HeaderIsPs2(
    JNIEnv *env, jobject /*self*/, jbyteArray header, jint length) {
  if (!header || length <= 0) return JNI_FALSE;
  const jsize have = env->GetArrayLength(header);
  const jsize n = std::min<jsize>(have, (jsize)length);
  std::vector<uint8_t> bytes((size_t)n);
  env->GetByteArrayRegion(header, 0, n,
                          reinterpret_cast<jbyte *>(bytes.data()));
  return oga_disc_header_is_ps2(bytes.data(), bytes.size()) ? JNI_TRUE
                                                            : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2ReadAudio(
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
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetAudioEnabled(
    JNIEnv * /*env*/, jobject /*self*/, jboolean enabled) {
  PokeCore *core = g_core;
  if (core) poke_set_audio_enabled(core, enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SaveState(
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
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2LoadState(
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
// pixels in the backend (the PlayStation 2 wrapper reads the core's
// rendered frame back to RGBA8888 as the frame runs; the fill call is
// still made first, the contract every backend observes), and the
// pointer accessor hands back that staging buffer — never a second
// render. Screen index 0: the PlayStation 2 has one screen. The staged
// pixels are RGBA8888 bytes; copied word-wise they are the same jints
// the GB/PSP copies return, so the one GamePicture consumer serves
// all three.
extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2CopyFrame(
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

// ---- the cue poll -----------------------------------------------------
//
// One snapshot per frame, packed into two scratch arrays the Kotlin side owns.
// ints: [0] beacon state (poke_cue_snapshot: 0 silent, 1 battle no lock, 2 locked),
// [1] one-shot health bitmask (poke_cue_health: 1 foe beep, 2 own beep).
// floats: [0] lock distance, [1] foe hz, [2] foe pan, [3] own hz, [4] own pan.
// Written even on a false return, so a refused poll reads as silence, never as
// last frame's data. pokecore shares ONE per-frame snapshot between the two
// calls, so asking both here loses no event.
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2CuePoll(
    JNIEnv *env, jobject /*self*/, jintArray ints, jfloatArray floats) {
  if (!ints || !floats) return JNI_FALSE;
  if (env->GetArrayLength(ints) < 2 || env->GetArrayLength(floats) < 5) return JNI_FALSE;
  PokeCore *core = g_core;
  int state = 0, bits = 0;
  float dist = 0.0f, foe_hz = 0.0f, foe_pan = 0.0f, own_hz = 0.0f, own_pan = 0.0f;
  if (core) {
    state = poke_cue_snapshot(core, &dist);
    bits = poke_cue_health(core, &foe_hz, &foe_pan, &own_hz, &own_pan);
  }
  const jint iv[2] = {state, bits};
  const jfloat fv[5] = {dist, foe_hz, foe_pan, own_hz, own_pan};
  env->SetIntArrayRegion(ints, 0, 2, iv);
  env->SetFloatArrayRegion(floats, 0, 5, fv);
  return core ? JNI_TRUE : JNI_FALSE;
}

// ---- the debug channel ------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetDebugDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  g_debug_dir = FromJString(env, dir);
  if (g_core) poke_debug_set_dir(g_core, g_debug_dir.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetCoreDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  g_core_dir = FromJString(env, dir);
  if (g_core) poke_set_ps2_core_dir(g_core, g_core_dir.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2SetSystemDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  g_system_dir = FromJString(env, dir);
  if (g_core) poke_set_ps2_system_dir(g_core, g_system_dir.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2DebugCommand(
    JNIEnv *env, jobject /*self*/, jstring text) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) return;
  const std::string t = FromJString(env, text);
  poke_debug_command(g_core, t.c_str());
}

// ---- the adapter surface ----------------------------------------------
// Thin forwards to pokecore's registry answers, present so the
// session can ask and be told the truth: the PlayStation 2 row's
// attach refuses by design (there is no PlayStation 2 reader), so
// ready never answers true and commands are refused. The session
// draws no reader controls from these answers; they exist for the log
// and for the day a PlayStation 2 reader exists to be asked about.

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2AdapterId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_adapter_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2AdapterName(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *name = core ? poke_adapter_name(core) : nullptr;
  return env->NewStringUTF(name ? name : "");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2AdapterReady(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  return (core && poke_adapter_ready(core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_Ps2AccessibilityScript_nativePs2AdapterCommand(
    JNIEnv * /*env*/, jobject /*self*/, jint cmd) {
  PokeCore *core = g_core;
  return (core && poke_command(core, (int)cmd)) ? JNI_TRUE : JNI_FALSE;
}
