// PspSessionJNI.cpp — the JNI surface over the psp_* ABI (Core/psp_core.h).
//
// Two tenants, in the order they were built:
//
// STAGE 2 (kept intact; stage 4 will use it): the debug boot proof. One
// synchronous call that creates a core, points it at the extracted
// ppsspp-assets, loads a disc image, runs a fixed number of frames, and
// reports every assertion the host proof (scripts/psp-host-proof-main.cpp)
// makes, as logcat lines under the tag "PspProof":
//
//   PROOF: gameid=<DISC_ID>            (from PARAM.SFO, via psp_load_rom)
//   PROOF: frames=<ran>/<asked> completed=<core frame counter>
//   PROOF: fb=480x272                  (psp_framebuffer geometry)
//   PROOF: nonzero=<n>                 (RGB-nonzero pixels, final frame)
//   PROOF: shutdown-clean              (stop + destroy returned)
//
// A failure at any stage logs "PROOF: FAILED <stage>: <psp_last_error>"
// instead. Nothing in the launcher reaches it; the only caller is
// PspProbe (com.devin.opengameaccess), itself gated on a launch intent
// extra that in practice only `adb shell am start --es ...` sets.
//
// STAGE 3: the real session surface, driven by PspAccessibilityScript
// (me.magnum.melonds.accessibility) under PspGameSession. One core at a
// time, owned by this file the way MGBARunner owns the mGBA core:
// create/load happen under a lifecycle lock; frames (the Compose frame
// clock), button latches and the audio drain (the session's AudioTrack
// thread) then run against the live core concurrently — the same split
// iOS uses (CADisplayLink frames, audio render thread pulling
// psp_read_audio), with the glue's mixer ring as the seam between them.
//
// Compiled as part of the oga-ppsspp-glue OBJECT library
// (scripts/android-apply-overlay.sh section 8 copies it beside
// psp_core.cpp), so it exists on exactly the ABIs the core does -- on
// armeabi-v7a the subset is empty by design and this whole surface is
// absent with it (the Kotlin side's probe turns that absence into a
// plain "not in this build").

#include <jni.h>

#include <android/log.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "psp_core.h"

#include "PspAdapterHost.h"

namespace {

constexpr const char *kTag = "PspProof";

void Proof(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, ap);
  va_end(ap);
}

// The core's own log lines (adapter/host messages on iOS's console) go to
// a second tag, so the proof log shows what the core said while booting.
void CoreLog(const char *text, void * /*userdata*/) {
  __android_log_print(ANDROID_LOG_INFO, "PspCore", "%s", text ? text : "");
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_devin_opengameaccess_PspProbe_nativePspProof(
    JNIEnv *env, jobject /*self*/, jstring rom_path, jstring save_dir,
    jstring asset_dir, jint frames) {
  const char *rom = env->GetStringUTFChars(rom_path, nullptr);
  const char *save = env->GetStringUTFChars(save_dir, nullptr);
  const char *assets = env->GetStringUTFChars(asset_dir, nullptr);
  if (!rom || !save || !assets) {
    Proof("PROOF: FAILED args: could not read the JNI strings");
    if (rom) env->ReleaseStringUTFChars(rom_path, rom);
    if (save) env->ReleaseStringUTFChars(save_dir, save);
    if (assets) env->ReleaseStringUTFChars(asset_dir, assets);
    return JNI_FALSE;
  }
  // Same refusal as the host proof: a frame count outside (0, 5 hours].
  if (frames <= 0 || frames > 60 * 60 * 5) {
    Proof("PROOF: FAILED args: refusing frame count %d", (int)frames);
    env->ReleaseStringUTFChars(rom_path, rom);
    env->ReleaseStringUTFChars(save_dir, save);
    env->ReleaseStringUTFChars(asset_dir, assets);
    return JNI_FALSE;
  }

  Proof("PROOF: start rom=%s frames=%d", rom, (int)frames);

  bool ok = false;
  bool booted = false;
  PspCore *core = psp_create();
  if (!core) {
    Proof("PROOF: FAILED create: psp_create returned null");
  } else {
    psp_set_log_callback(core, &CoreLog, nullptr);
    psp_set_asset_dir(core, assets);
    char code[16] = {0};
    if (!psp_load_rom(core, rom, save, code)) {
      Proof("PROOF: FAILED load: %s", psp_last_error(core));
    } else {
      Proof("PROOF: gameid=%s", code);
      if (!psp_start(core)) {
        Proof("PROOF: FAILED start: %s", psp_last_error(core));
      } else {
        booted = true;
        int ran = 0;
        while (ran < frames && psp_frame(core)) ++ran;
        Proof("PROOF: frames=%d/%d completed=%llu", ran, (int)frames,
              (unsigned long long)psp_frames_completed(core));
        int w = 0, h = 0;
        long nonzero = 0;
        if (psp_framebuffer(core, &w, &h)) {
          Proof("PROOF: fb=%dx%d", w, h);
          const uint8_t *fb = psp_framebuffer_ptr(core);
          if (fb) {
            for (long i = 0; i < (long)w * h * 4; i += 4) {
              if (fb[i] | fb[i + 1] | fb[i + 2]) {
                if (++nonzero > 4096) break;
              }
            }
          }
        } else {
          Proof("PROOF: FAILED framebuffer: %s", psp_last_error(core));
        }
        Proof("PROOF: nonzero=%ld", nonzero);
        psp_stop(core);
        ok = (ran == frames && nonzero > 0);
      }
    }
    psp_destroy(core);
    // The host proof prints this once stop + destroy have returned,
    // whatever the frame/fb verdicts were (those ride the exit code).
    if (booted) Proof("PROOF: shutdown-clean");
  }

  env->ReleaseStringUTFChars(rom_path, rom);
  env->ReleaseStringUTFChars(save_dir, save);
  env->ReleaseStringUTFChars(asset_dir, assets);
  return ok ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// Stage 3: the session surface (PspAccessibilityScript's externals).
//
// The log callback IS set, to the same PspCore logcat tag the proof
// uses, so a session's boot reads exactly like the proof's did.
//
// Stage 4: the adapter host (PspAdapterHost.cpp) hangs off this
// surface's lifecycle — it is told about a successful load (it selects
// the adapter by DISC_ID, builds its host + announcement queue and
// sets the core's speech callback), about every completed frame (its
// pump), and about stop (it detaches before the core is destroyed).
// The adapter JNI at the bottom of this file is its UI path.

namespace {

// The one live core, or null. Written only under g_lifecycle_lock (load,
// start, stop); read lock-free by the frame/audio/button calls, which
// the Kotlin session confines to the lifetime between a successful load
// and stop() — the same contract MGBARunner's callers keep.
PspCore *g_core = nullptr;
std::string g_game_code;
std::string g_last_error;
std::mutex g_lifecycle_lock;

std::string FromJString(JNIEnv *env, jstring s) {
  if (!s) return "";
  const char *chars = env->GetStringUTFChars(s, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

void StoreLastError() {
  const char *e = g_core ? psp_last_error(g_core) : nullptr;
  g_last_error = e ? e : "";
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspAvailable(
    JNIEnv * /*env*/, jobject /*self*/) {
  // The function's existence IS the answer: where the subset is not
  // compiled (armeabi-v7a), the symbol is absent and the Kotlin call
  // throws UnsatisfiedLinkError instead of reaching here.
  return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspLoadRom(
    JNIEnv *env, jobject /*self*/, jstring rom_path, jstring save_dir,
    jstring asset_dir) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (g_core) {
    PspAdapterHostOnStop();
    psp_stop(g_core);
    psp_destroy(g_core);
    g_core = nullptr;
  }
  g_game_code.clear();
  g_last_error.clear();

  const std::string rom = FromJString(env, rom_path);
  const std::string save = FromJString(env, save_dir);
  const std::string assets = FromJString(env, asset_dir);

  PspCore *core = psp_create();
  if (!core) {
    g_last_error = "psp_create returned null";
    return JNI_FALSE;
  }
  psp_set_log_callback(core, &CoreLog, nullptr);
  psp_set_asset_dir(core, assets.c_str());
  char code[16] = {0};
  if (!psp_load_rom(core, rom.c_str(), save.c_str(), code)) {
    const char *e = psp_last_error(core);
    g_last_error = e ? e : "";
    psp_destroy(core);
    return JNI_FALSE;
  }
  g_core = core;
  g_game_code = code;
  PspAdapterHostOnLoad(core, code);
  return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspGameCode(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_game_code.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspLastError(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(g_last_error.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspStart(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (!g_core) {
    g_last_error = "no PSP game is loaded";
    return JNI_FALSE;
  }
  if (!psp_start(g_core)) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspStop(
    JNIEnv * /*env*/, jobject /*self*/) {
  std::lock_guard<std::mutex> lock(g_lifecycle_lock);
  if (g_core) {
    PspAdapterHostOnStop();
    psp_stop(g_core);
    psp_destroy(g_core);
    g_core = nullptr;
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspRunning(
    JNIEnv * /*env*/, jobject /*self*/) {
  return (g_core && psp_running(g_core)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspFrame(
    JNIEnv * /*env*/, jobject /*self*/) {
  PspCore *core = g_core;
  if (!core || !psp_frame(core)) return JNI_FALSE;
  // The adapter pump rides the completed frame (pokecore's EndFrame):
  // the adapter sees post-frame memory, its announcements tick.
  PspAdapterHostFrame(core);
  return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspFramesCompleted(
    JNIEnv * /*env*/, jobject /*self*/) {
  PspCore *core = g_core;
  return core ? (jlong)psp_frames_completed(core) : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspSetButton(
    JNIEnv * /*env*/, jobject /*self*/, jint button, jboolean down) {
  PspCore *core = g_core;
  if (core) psp_set_button(core, (int)button, down == JNI_TRUE);
}

extern "C" JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspReadAudio(
    JNIEnv *env, jobject /*self*/, jshortArray out, jint max_frames) {
  PspCore *core = g_core;
  if (!core || !out || max_frames <= 0) return 0;
  const jsize capacity_frames = env->GetArrayLength(out) / 2;
  const int want = (int)std::min<jint>(max_frames, capacity_frames);
  if (want <= 0) return 0;
  // Through a scratch buffer rather than the array's elements: the drain
  // hands back however many frames the ring actually had, and only those
  // are committed to the caller's array.
  static thread_local std::vector<int16_t> scratch;
  scratch.resize((size_t)want * 2);
  const int frames = psp_read_audio(core, scratch.data(), want);
  if (frames > 0) env->SetShortArrayRegion(out, 0, frames * 2, scratch.data());
  return frames;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspSaveState(
    JNIEnv *env, jobject /*self*/, jstring path) {
  PspCore *core = g_core;
  if (!core) return JNI_FALSE;
  if (!psp_save_state(core, FromJString(env, path).c_str())) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspLoadState(
    JNIEnv *env, jobject /*self*/, jstring path) {
  PspCore *core = g_core;
  if (!core) return JNI_FALSE;
  if (!psp_load_state(core, FromJString(env, path).c_str())) {
    StoreLastError();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

// ---------------------------------------------------------------------------
// Stage 4: the adapter surface — thin forwards to PspAdapterHost (see its
// header for the design). The command integer is oga::Command's raw
// value, the C ABI adapter.h defines; PspGameSession's PspAdapterCommands
// mirror is what the UI sends, and the contract test checks it against
// adapter.h itself.

extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_setPspSpeechBridge(
    JNIEnv *env, jobject /*self*/, jobject bridge) {
  PspAdapterHostSetSpeechBridge(env, bridge);
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspAdapterId(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(PspAdapterHostId());
}

extern "C" JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspAdapterName(
    JNIEnv *env, jobject /*self*/) {
  return env->NewStringUTF(PspAdapterHostName());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspAdapterReady(
    JNIEnv * /*env*/, jobject /*self*/) {
  return PspAdapterHostReady() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspAdapterCommand(
    JNIEnv * /*env*/, jobject /*self*/, jint cmd) {
  return PspAdapterHostCommand((int)cmd) ? JNI_TRUE : JNI_FALSE;
}

// The cue poll: one snapshot per frame, packed into two scratch arrays
// the Kotlin side owns. ints: [0]=beacon state, [1]=field active,
// [2]=field kind, [3]=heading live, [4]=one-shot health bitmask,
// [5]=guard window active, [6]=guard window beat. floats: [0]=lock
// distance, [1]=field distance, [2]=bearing degrees, [3]=foe hz,
// [4]=foe pan, [5]=own hz, [6]=own pan, [7]=guard window fraction.
// The arrays are written even on a false return
// (PspAdapterHostCuePoll zeroed them), so a refused poll reads as
// silence, never as last frame's data.
extern "C" JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspCuePoll(
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
  const bool ok = PspAdapterHostCuePoll(
      &state, &dist, &field, &kind, &bearing, &field_dist, &heading_live,
      &health_bits, &foe_hz, &foe_pan, &own_hz, &own_pan, &guard_active,
      &guard_frac, &guard_beat);
  const jint iv[7] = {state, field, kind, heading_live, health_bits,
                      guard_active, guard_beat};
  const jfloat fv[8] = {dist, field_dist, bearing, foe_hz,
                        foe_pan, own_hz, own_pan, guard_frac};
  env->SetIntArrayRegion(ints, 0, 7, iv);
  env->SetFloatArrayRegion(floats, 0, 8, fv);
  return ok ? JNI_TRUE : JNI_FALSE;
}

// The per-game-settings seam: the session's resolved HUD gauge-cue
// choice for Birth by Sleep, pushed into the adapter at session start
// (see PspAdapterHost.h). A forward, nothing more — the value was
// resolved in Kotlin (GameSettings) before it crossed.
extern "C" JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspSetHudGaugeCues(
    JNIEnv * /*env*/, jobject /*self*/, jboolean use_sound) {
  PspAdapterHostSetHudGaugeCues(use_sound == JNI_TRUE);
}
