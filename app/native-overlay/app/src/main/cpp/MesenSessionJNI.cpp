// MesenSessionJNI.cpp — the JNI surface over the shared Core host
// (Sources/CPokeCore/include/pokecore.h) for the NES and SNES backends,
// driven by MesenAccessibilityScript (me.magnum.melonds.accessibility)
// under MesenGameSession.
//
// THE HOST IS THE SHARED CORE, EXACTLY AS iOS RUNS IT — AND AS THE DS
// SESSION HERE RUNS IT. Core/pokecore.cpp is a multi-console host: its
// ROM resolver routes a file to the DS, GBA, NES, SNES or PSP backend by
// extension, and the NES/SNES branches (LoadNesRom / LoadSnesRom) drive
// Core/mesen_core.cpp and Core/snes_core.cpp, the MesenCE glue iOS
// ships. Until this stage the Android build carried those branches with
// their C ABIs refused (DsBackendStubs.cpp), because nothing compiled
// the MesenCE tree; the overlay's section 10 now builds it (the
// oga-mesen subproject) and drops the stubs, so this surface is the DS
// one's twin over the same ABI: one core at a time, owned by this file;
// create/load under a lifecycle lock; frames, button latches and the
// audio drain then run against the live core concurrently.
//
// WHY A SECOND JNI FILE OVER THE SAME HOST, NOT A WIDER DsSessionJNI.
// The PSP session set the shape: one JNI file per session, each owning
// its own PokeCore, because the sessions are peers and only one ever
// runs at a time. This file owns its core the same way. The one symbol
// the two pokecore surfaces share — melonDS::Platform::
// PokeSetLogForward — stays defined in DsSessionJNI.cpp alone; defining
// it here too would be a duplicate symbol in the one frontend .so.
//
// THE LOAD'S SECOND PATH IS A FOLDER, NOT A SAVE FILE. pokecore's NES
// and SNES branches hand their save_path to Mesen as its HOME FOLDER
// (FolderUtilities::SetHomeFolder): Mesen throws "Home folder not
// specified" from inside LoadRom without one, and keeps the game's
// battery save inside it. MesenGameSession therefore passes a per-game
// directory under the app's internal storage (MesenRomResolver), where
// the DS session passes a .sav path. Same parameter, different meaning
// — the core's branches decide, and both are documented at their loads.
//
// THE READER SETS. After a NES load, the core names the bundled reader
// set the ROM needs (poke_reader_set, by the ROM's own CRC32 — never by
// filename; "" is the normal answer for a game no reader covers). The
// session stages a writable copy of that set (the readers write into
// their own tree) and hands the directory back with
// nativeMesenSetScriptDir before the start, the order iOS's
// GameSession uses. The reader itself — the Dragon Warrior and Zelda 1
// Lua mods — runs inside mesen_core's Lua host, and its speech reaches
// this file's bridge through pokecore's speech callback, which
// LoadNesRom registers (the NesSayForward there). SNES games have no
// reader set: their reader is the native A Link to the Past adapter,
// selected by the registry when the core attaches it.
//
// ABI GATING. The overlay (section 10) adds this surface only on the
// 64-bit ABIs, mirroring the DS and PSP stance; on armeabi-v7a these
// symbols are absent and the Kotlin probe (nativeMesenAvailable) turns
// that absence into a plain "not in this build".

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
  __android_log_print(ANDROID_LOG_INFO, "MesenCore", "%s",
                      text ? text : "");
}

// ---- the speech bridge (mirrors DsSessionJNI.cpp) --------------------

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

// pokecore's plain speech callback (see DsSessionJNI's header note on
// pacing — the same choice, for the same reason).
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
    // Flush before teardown, the DS surface's rule: pokecore's
    // periodic flush is a DS cadence, and neither poke_stop nor
    // poke_destroy flushes — for the Mesen backends the battery save
    // is flushed by the core's own stop path, and this call is the
    // same belt the DS surface wears, harmless where it is a no-op.
    poke_flush_save(g_core);
    poke_stop(g_core);
    poke_destroy(g_core);
    g_core = nullptr;
  }
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenAvailable(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer: where the Mesen glue is
  // not compiled (armeabi-v7a), the symbol is absent and the Kotlin
  // call throws UnsatisfiedLinkError instead of reaching here.
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_setMesenSpeechBridge(
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
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenLoadRom(
    JNIEnv *env, jobject /*self*/, jstring rom_path, jstring home_path) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
  g_game_code.clear();
  g_last_error.clear();
  g_frames = 0;

  const std::string rom = FromJString(env, rom_path);
  const std::string home = FromJString(env, home_path);

  PokeCore *core = poke_create();
  if (!core) {
    g_last_error = "poke_create returned null";
    return JNI_FALSE;
  }
  poke_set_log_callback(core, &CoreLog, nullptr);
  poke_set_speech_callback(core, &OnSpeech, nullptr);
  if (!poke_load_rom(core, rom.c_str(), home.c_str())) {
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
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenGameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenLastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

// The running backend's registry id ("nes" / "snes"), or "" before a
// load: the session shapes its pad (a NES pad has no X/Y or shoulders)
// and its state group (the NES core refuses save states out loud) from
// this, never from the file's extension.
extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenBackendId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_backend_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

// The bundled reader set the loaded NES ROM needs, by its own CRC32
// (poke_reader_set). "" is the normal answer for a game no reader
// covers — the console then boots with no reader rather than someone
// else's.
extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenReaderSet(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *set = core ? poke_reader_set(core) : nullptr;
  return env->NewStringUTF(set ? set : "");
}

// Hand the core the staged reader-set directory. Call AFTER the load
// (the set is a property of the loaded cartridge) and BEFORE the
// start, which is what loads the reader — iOS's GameSession order.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenSetScriptDir(
    JNIEnv *env, jobject /*self*/, jstring dir) {
  PokeCore *core = g_core;
  if (core) poke_set_script_dir(core, FromJString(env, dir).c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenStart(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no NES or SNES game is loaded";
    return JNI_FALSE;
  }
  if (!poke_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenStop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  DestroyCoreLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenRunning(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && poke_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenFrame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  if (!core || !poke_frame(core)) return JNI_FALSE;
  ++g_frames;
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenFramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (jlong)g_frames;
}

// Buttons are POKE_BTN_* ids (Sources/CPokeCore/include/pokecore.h) —
// MesenGameSession's MesenKeys mirror, checked by the launcher contract
// test. pokecore translates the shared pad indices to each console's
// own button order (the NES drops what it does not have).
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenSetButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jboolean down) {
  PokeCore *core = g_core;
  if (core) poke_set_button(core, (int)button, down == JNI_TRUE);
}

// A tap counted in emulated frames (poke_tap_button): the core holds
// the press across frame boundaries itself, so the game samples it
// whatever the host's timing does.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenTapButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jint frames) {
  PokeCore *core = g_core;
  if (core) poke_tap_button(core, (int)button, (int)frames);
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenReadAudio(
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
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenSetAudioEnabled(
    JNIEnv * /*env*/, jobject /*self*/, jboolean enabled) {
  PokeCore *core = g_core;
  if (core) poke_set_audio_enabled(core, enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenSaveState(
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
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenLoadState(
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
// Thin forwards to pokecore's registry answers, the DS surface's
// shape. The command integer is oga::Command's raw value
// (Core/adapter.h); MesenGameSession's MesenAdapterCommands mirror is
// what the UI sends, and the contract test checks it against adapter.h
// itself.

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenAdapterId(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *id = core ? poke_adapter_id(core) : nullptr;
  return env->NewStringUTF(id ? id : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenAdapterName(
    JNIEnv *env, jobject /*self*/) {
  PokeCore *core = g_core;
  const char *name = core ? poke_adapter_name(core) : nullptr;
  return env->NewStringUTF(name ? name : "");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenAdapterReady(
    JNIEnv * /*env*/, jobject /*self*/) {
  PokeCore *core = g_core;
  // The native side attaches lazily inside this call (the adapter's
  // memory anchors only exist once the game has booted), so the
  // session polls it once a frame — pokecore's own contract.
  return (core && poke_adapter_ready(core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenAdapterCommand(
    JNIEnv * /*env*/, jobject /*self*/, jint cmd) {
  PokeCore *core = g_core;
  return (core && poke_command(core, (int)cmd)) ? JNI_TRUE : JNI_FALSE;
}

// The cue poll: one frame of battle-cue data, packed exactly as the DS
// and PSP polls pack it, so the session-side cue logic is the same
// shape on every console. No NES or SNES reader raises these cues
// today (the A Link to the Past adapter speaks; the NES readers speak)
// — the poll then answers "nothing selected" and the session silences
// its synth, the fail-closed shape. It is wired anyway so the day a
// Mesen-console adapter grows cues, they land on the synth that
// already mixes into this session's drain.
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenCuePoll(
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

// ---- the game picture ---------------------------------------------------
// The running game's frame, copied into the caller's IntArray as
// little-endian RGBA words plus a size — PspSessionJNI's
// nativePspCopyFrame contract, so the one GamePicture consumer serves
// this session too. Screen 0 is the console's only screen on both
// Mesen backends. Returns the pixel count, or 0 when there is no frame
// to copy (nothing loaded, or the backend has not produced one yet).
extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_MesenAccessibilityScript_nativeMesenCopyFrame(
    JNIEnv *env, jobject /*self*/, jintArray out, jintArray sizeOut) {
  PokeCore *core = g_core;
  if (!core || !out) return 0;
  int w = 0, h = 0;
  // The fill call must come first (the PSP note): poke_framebuffer
  // stages the pixels in the core, and the pointer accessor only
  // hands back that staging buffer.
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
