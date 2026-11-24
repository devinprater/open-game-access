// GbSessionJNI.cpp — the JNI surface over the shared Core host
// (Sources/CPokeCore/include/pokecore.h) for the Game Boy, Game Boy Color
// and Game Boy Advance, driven by GbAccessibilityScript
// (me.magnum.melonds.accessibility) under GbGameSession.
//
// THE HOST IS THE SHARED CORE, EXACTLY AS iOS RUNS IT. Until this file,
// Android ran Game Boy games through a host of its own: MGBARunner +
// MGBACore (an mGBA wrapper and a VisualBoyAdvance-flavoured Lua surface
// written for Android alone) and MGBAScriptJNI over them. iOS ran the same
// games through Core/gba_core.cpp under Core/pokecore.cpp. Two hosts for
// one console meant every reader fix, every audio fix and every adapter
// had to land twice, and Android got none of the shared-Core features
// (the native GBA adapter, the announcement queue's stop, the debug
// channel). This file retires the Android host: pokecore's ROM resolver
// routes .gb/.gbc/.gba to LoadGbaRom, which drives gba_core.cpp — the same
// translation unit iOS ships, compiled into the frontend by the overlay's
// section 9 — and the reader set boots through the same oga_bootstrap.lua.
//
// THE SHAPE IS MesenSessionJNI.cpp's: one core at a time, owned by this
// file; create/load/start/stop under a lifecycle lock; frames (the Compose
// frame clock), button latches and the audio drain then run against the
// live core concurrently. The audio thread only ever calls poke_read_audio,
// which reads gba_core's staged queue (Core/gb_audio_queue.h) under that
// queue's own mutex — never mGBA's ring.
//
// THE PAD. GbGameSession keeps the whole pad as one mask (GbKeys, mGBA's
// own bit order). POKE_BTN_A..POKE_BTN_L are that same order (pokecore.h
// and gba_core.h say so, and the launcher contract test checks GbKeys), so
// bit i of the mask is POKE_BTN i: the mask is applied per button here.
//
// THE CUE SOUNDS. The reader's audio.play cues reach pokecore's sound
// callback with the reader's own path separators (it was written for
// Windows); they are normalised to '/' here before the shared bridge's
// playSound, which loads them with SoundPool — the conversion MGBACore
// used to do on its side of the old path.
//
// ABI GATING. The overlay (section 9) compiles this surface with the rest of
// the shared-Core glue, on the 64-bit ABIs only; on armeabi-v7a the symbols
// are absent and the Kotlin probe (nativeGbAvailable) turns that absence
// into a plain "not in this build".

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

// oga::Command::StopSpeech (Core/adapter.h). Raw, the way the other
// sessions' command mirrors send it; never a player button.
const int kCommandStopSpeech = 37;

void CoreLog(const char *text, void * /*userdata*/) {
  __android_log_print(ANDROID_LOG_INFO, "GbCore", "%s", text ? text : "");
}

// ---- the speech bridge (mirrors MesenSessionJNI.cpp) -------------------

JavaVM *g_vm = nullptr;
jobject g_bridge = nullptr;
jmethodID g_speakMethod = nullptr;  // speak(Ljava/lang/String;Z)V
jmethodID g_stopMethod = nullptr;   // stop()V
jmethodID g_soundMethod = nullptr;  // playSound(Ljava/lang/String;II)V, optional

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

void ClearException(JNIEnv *env) {
  // A Java exception must never unwind through the Lua C frames the
  // callbacks below are called from.
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

// Speak one line on the shared bridge; a null text stops speech.
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
  ClearException(env);
}

void OnSpeech(const char *text, bool interrupt, void * /*userdata*/) {
  BridgeSpeak(text, interrupt);
}

// The reader joins its sound paths with '\' (it was written for Windows)
// and can leave doubled separators; SoundPool wants a real path.
std::string NormaliseSoundPath(const char *path) {
  std::string out;
  for (const char *p = path; *p; ++p) {
    const char c = (*p == '\\') ? '/' : *p;
    if (c == '/' && !out.empty() && out.back() == '/') continue;
    out.push_back(c);
  }
  return out;
}

void OnSound(const char *path, int pan, int volume, void * /*userdata*/) {
  if (!path || !g_bridge || !g_soundMethod) return;
  JNIEnv *env = BridgeEnv();
  if (!env) return;
  const std::string real = NormaliseSoundPath(path);
  jstring jpath = env->NewStringUTF(real.c_str());
  env->CallVoidMethod(g_bridge, g_soundMethod, jpath, (jint)pan,
                      (jint)volume);
  env->DeleteLocalRef(jpath);
  ClearException(env);
}

// ---- the one live core ------------------------------------------------

// Written only under g_lifecycle_lock (load, start, stop); read lock-free
// by the frame/audio/button calls, which GbGameSession confines to the
// lifetime between a successful start and stop() — and stop() joins the
// audio thread before it tears the core down.
PokeCore *g_core = nullptr;
std::string g_game_code;
std::string g_last_error;
// The in-game debug channel's folder (docs/design/debug-channel.md):
// kept across loads and handed to every core this file creates.
std::string g_debug_dir;
long long g_frames = 0;
int g_keys = 0;  // the mask last applied, so only changed buttons move
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
    // The cartridge's battery save: gba_core writes it through mGBA's
    // save file as the game does, and the flush is the same belt the DS
    // and Mesen surfaces wear (harmless where it is a no-op).
    poke_flush_save(g_core);
    poke_stop(g_core);
    poke_destroy(g_core);
    g_core = nullptr;
  }
  g_keys = 0;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbAvailable(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer (the Mesen probe's shape).
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_setGbSpeechBridge(
    JNIEnv *env, jobject /*self*/, jobject bridge) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (g_bridge) {
    env->DeleteGlobalRef(g_bridge);
    g_bridge = nullptr;
  }
  g_speakMethod = nullptr;
  g_stopMethod = nullptr;
  g_soundMethod = nullptr;
  if (!bridge) return;
  env->GetJavaVM(&g_vm);
  jclass bridgeClass = env->GetObjectClass(bridge);
  g_speakMethod =
      env->GetMethodID(bridgeClass, "speak", "(Ljava/lang/String;Z)V");
  g_stopMethod = env->GetMethodID(bridgeClass, "stop", "()V");
  // Optional: the reader's cue sounds. A bridge without it still speaks.
  g_soundMethod =
      env->GetMethodID(bridgeClass, "playSound", "(Ljava/lang/String;II)V");
  if (env->ExceptionCheck()) env->ExceptionClear();  // a missing playSound
  env->DeleteLocalRef(bridgeClass);
  if (!g_speakMethod || !g_stopMethod) {
    g_speakMethod = nullptr;
    g_stopMethod = nullptr;
    g_soundMethod = nullptr;
    return;
  }
  g_bridge = env->NewGlobalRef(bridge);
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbSetDebugDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  g_debug_dir = FromJString(env, dir);
  if (g_core) poke_debug_set_dir(g_core, g_debug_dir.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbDebugCommand(
    JNIEnv *env, jobject /*self*/, jstring text) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) return;
  const std::string t = FromJString(env, text);
  poke_debug_command(g_core, t.c_str());
}

// Load a .gb/.gbc/.gba file. `save_path` is the cartridge's battery save
// file (GbRomResolver.savePathFor), which gba_core hands to mGBA.
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbLoadRom(
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
  poke_set_sound_callback(core, &OnSound, nullptr);
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
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbGameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbLastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

// The running backend's registry id ("gba" for all three consoles), or ""
// before a load: proof in logcat that the game really is on the shared
// Core's Game Boy backend and not on some other route.
extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbBackendId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_backend_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

// Hand the core the extracted reader set (the directory holding
// oga_bootstrap.lua). After the load, before the start — iOS's order.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbSetScriptDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  PokeCore *core = g_core;
  if (core) poke_set_script_dir(core, FromJString(env, dir).c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbStart(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no Game Boy game is loaded";
    return JNI_FALSE;
  }
  if (!poke_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbStop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbRunning(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && poke_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbFrame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  if (!core || !poke_frame(core)) return JNI_FALSE;
  ++g_frames;
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbFramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (jlong)g_frames;
}

// The whole pad as GbKeys bits (mGBA's order == POKE_BTN_A..POKE_BTN_L).
// Only the buttons whose bit changed are pressed or released.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbSetKeys(
    JNIEnv * /*env*/, jobject /*self*/, jint keys) {
  PokeCore *core = g_core;
  if (!core) return;
  const int changed = g_keys ^ (int)keys;
  for (int b = POKE_BTN_A; b <= POKE_BTN_L; ++b) {
    if (changed & (1 << b)) poke_set_button(core, b, (keys & (1 << b)) != 0);
  }
  g_keys = (int)keys;
}

// A reader hotkey (an upper-case letter) down or up.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbSetHotkey(
    JNIEnv * /*env*/, jobject /*self*/, jchar key, jboolean down) {
  PokeCore *core = g_core;
  if (!core || key == 0 || key > 0x7F) return;
  const char k[2] = {(char)key, '\0'};
  poke_set_hotkey(core, k, down == JNI_TRUE);
}

// Stop talking: the announcement queue is cleared and the platform voice
// stopped (pokecore's StopSpeech command). The GAME KEEPS RUNNING — the old
// Android path stopped speech by tearing the whole core down while the
// screen still showed the game.
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbStopSpeech(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  return (core && poke_command(core, kCommandStopSpeech)) ? JNI_TRUE
                                                          : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbReadAudio(
    JNIEnv *env, jobject /*self*/, jshortArray out, jint max_frames) {
  PokeCore *core = g_core;
  if (!core || !out || max_frames <= 0) return 0;
  const jsize capacity_frames = env->GetArrayLength(out) / 2;
  const int want = (int)std::min<jint>(max_frames, capacity_frames);
  if (want <= 0) return 0;
  static thread_local std::vector<int16_t> scratch;
  scratch.resize((size_t)want * 2);
  const int frames = poke_read_audio(core, scratch.data(), want);
  if (frames > 0) env->SetShortArrayRegion(out, 0, frames * 2, scratch.data());
  return frames;
}

// The "Game sound" switch. Speech and the reader's cue sounds take other
// paths and are unaffected.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbSetAudioEnabled(
    JNIEnv * /*env*/, jobject /*self*/, jboolean enabled) {
  PokeCore *core = g_core;
  if (core) poke_set_audio_enabled(core, enabled == JNI_TRUE);
}

// The running game's frame as little-endian RGBA words plus its size (the
// Mesen/PSP nativeCopyFrame contract). 240x160 on a GBA, 160x144 on a GB.
extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_nativeGbCopyFrame(
    JNIEnv *env, jobject /*self*/, jintArray out, jintArray sizeOut) {
  PokeCore *core = g_core;
  if (!core || !out) return 0;
  int w = 0, h = 0;
  // The fill call must come first: poke_framebuffer stages the pixels,
  // and the pointer accessor only hands back that staging buffer.
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
