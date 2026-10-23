// PspAdapterHost.cpp — the Android PSP adapter host (stage 4). See the
// header for the shape; this file is the PSP slice of Core/pokecore.cpp
// (BuildHost, EndFrame's adapter pump, poke_adapter_ready, poke_command)
// rebuilt against the PSP core directly instead of the cross-system
// ops table, because on Android only the PSP core exists.
//
// THE REGISTRY: Core/adapters.cpp is deliberately NOT linked here. Its
// find_by_game_code table references all eleven adapters, and two of
// the non-PSP ones cannot exist on Android (the Fire Emblem adapter
// needs the DS core's memory functions; the GBA/DS ones belong to the
// melonDS/mGBA side this build does not carry) — linking the registry
// would fail on their symbols. Instead this file declares extern the
// four PSP adapter structs (defined by the same adapter TUs iOS links)
// and matches the loaded DISC_ID against them with the registry's own
// rule — FULL-string equality against game_code, in adapters.cpp: "a
// substring would let a similar id attach another game's tables". The
// launcher contract test (§10) asserts this table names exactly the
// registry's ULUS-coded adapters, so the two cannot drift silently.
//
// THE DEDUPLICATION HELPERS: the adapters call two process-wide
// helpers the registry TU owns — oga::AdapterNoteSpoken /
// oga::AdapterSpeechReset declared in adapter.h. They are copied below
// (flagged), because linking adapters.cpp just for them is exactly
// what cannot happen here. The repo already keeps such copies under a
// "copied, not shared" flag where platforms genuinely diverge; the
// contract test's registry check also covers this file keeping them.
//
// SPEECH: everything spoken goes to the one shared AccessibilitySpeech
// instance the app owns (the same object MGBAScriptJNI forwards to),
// reached through the JNI bridge PspGameSession installs at session
// start. Three mouths, one path, exactly as on iOS where every adapter
// line lands in the one VoiceOver queue:
//   - the announcement queue's sink (adapter automatic speech),
//   - oga::Host::speak (an adapter's direct wire, used when its queue
//     path declines),
//   - the core speech callback psp_set_speech_callback. NOTE: in the
//     pinned psp_core.cpp the callback is stored and never invoked —
//     the adapters speak through the queue/host, not the core. It is
//     set anyway: the stage-3 session left the slot deliberately
//     unset FOR this host, and if a later core rev starts invoking it
//     the lines should land on the same bridge.
//
// THREADS: selection runs on the session's load thread; attach, frames
// and commands run on the UI thread (PspGameSession confines its calls
// to the lifetime between a successful load and stop(), the session
// JNI's documented rule). The queue is created at selection and
// destroyed at stop, never concurrently with a frame or command.
#include "PspAdapterHost.h"

#include <android/log.h>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "adapter.h"
#include "announce.h"

// The PSP adapter structs, defined by their Core TUs (the same
// declarations Core/adapters.cpp makes for its registry table).
// Declared at namespace scope — NOT inside the anonymous namespace
// below, which would name different (unresolvable) entities.
namespace oga {
extern const Adapter kDissidiaFinalFantasy;
extern const Adapter kDragonBallZAnotherRoad;
extern const Adapter kMortalKombatUnchained;
extern const Adapter kKingdomHeartsBirthBySleep;
}  // namespace oga

namespace {

constexpr const char* kTag = "PspHost";

void HostLogLine(const char* text) {
  __android_log_print(ANDROID_LOG_INFO, kTag, "%s", text ? text : "");
}

// ---- the speech bridge ------------------------------------------------

JavaVM* g_vm = nullptr;
jobject g_bridge = nullptr;
jmethodID g_speakMethod = nullptr;  // speak(Ljava/lang/String;Z)V
jmethodID g_stopMethod = nullptr;   // stop()V

JNIEnv* BridgeEnv(void) {
  if (!g_vm) return nullptr;
  JNIEnv* env = nullptr;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) ==
      JNI_OK) {
    return env;
  }
  // UI-thread calls arrive attached already; the attach path exists
  // for any future caller on a native thread.
  if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
  return nullptr;
}

// Speak one line on the shared bridge; a null text stops speech, the
// same convention MGBAScriptJNI's callSpeech uses for the DS script
// engine's null-string stop. No bridge installed (the ARMv7 build,
// where the PSP natives do not exist and this code never runs — kept
// for symmetry) drops the line silently.
void BridgeSpeak(const char* text, bool interrupt) {
  if (!g_bridge || !g_speakMethod || !g_stopMethod) return;
  JNIEnv* env = BridgeEnv();
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

// ---- the host (pokecore's BuildHost, PSP slice) -----------------------

PspCore* g_core = nullptr;
const oga::Adapter* g_adapter = nullptr;
bool g_attached = false;
oga::Host g_host{};
oga::AnnounceQueue* g_queue = nullptr;

uint64_t NowMs(void) {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

// psp_debug_read is the one-function memory interface every PSP
// adapter ultimately reads through (it is what pokecore's PSP ops
// table wraps); width 1/2/4, and unmapped reads answer 0.
uint8_t HostRead8(void* ctx, uint32_t address) {
  return static_cast<uint8_t>(
      psp_debug_read(static_cast<PspCore*>(ctx), address, 1));
}
uint16_t HostRead16(void* ctx, uint32_t address) {
  return static_cast<uint16_t>(
      psp_debug_read(static_cast<PspCore*>(ctx), address, 2));
}
uint32_t HostRead32(void* ctx, uint32_t address) {
  return psp_debug_read(static_cast<PspCore*>(ctx), address, 4);
}

void HostSpeak(void* /*ctx*/, const char* utf8, bool interrupt) {
  BridgeSpeak(utf8, interrupt);
}
void HostLog(void* /*ctx*/, const char* utf8) { HostLogLine(utf8); }

// DS pad index -> PSP button. COPIED from Core/oga_core.cpp's
// kPadToPsp (the oga_core slice of the core glue is not part of this
// build); the adapters synthesize presses in DS pad indices because
// that is the one pad vocabulary adapter.h defines.
const int kPadToPsp[12] = {
    PSP_BTN_CROSS,   // A
    PSP_BTN_CIRCLE,  // B
    PSP_BTN_SELECT,  // SELECT
    PSP_BTN_START,   // START
    PSP_BTN_RIGHT,   // RIGHT
    PSP_BTN_LEFT,    // LEFT
    PSP_BTN_UP,      // UP
    PSP_BTN_DOWN,    // DOWN
    PSP_BTN_R,       // R
    PSP_BTN_L,       // L
    PSP_BTN_TRIANGLE,  // X
    PSP_BTN_SQUARE,    // Y
};
void HostSetButton(void* ctx, int pad_button, bool down) {
  if (pad_button < 0 || pad_button >= 12) return;
  psp_set_button(static_cast<PspCore*>(ctx), kPadToPsp[pad_button], down);
}

// The announcement queue's sink: released lines go to the same bridge
// as everything else; queue diagnostics go to the host log.
void QueueSpeak(void* /*ctx*/, const char* utf8, bool interrupt,
                uint32_t /*utterance_id*/) {
  BridgeSpeak(utf8, interrupt);
}
void QueueLog(void* /*ctx*/, const char* utf8) { HostLogLine(utf8); }

// The core speech callback (see the header note: stored, currently
// never invoked by the pinned core; wired so its lines would land on
// the same bridge if a later rev invokes it).
void CoreSpeech(const char* text, bool interrupt, void* /*user*/) {
  BridgeSpeak(text, interrupt);
}

// Selection: FULL-string DISC_ID equality, adapters.cpp's rule.
const oga::Adapter* FindPspAdapter(const char* game_code) {
  static const oga::Adapter* const kPspAdapters[] = {
      &oga::kDissidiaFinalFantasy,
      &oga::kDragonBallZAnotherRoad,
      &oga::kMortalKombatUnchained,
      &oga::kKingdomHeartsBirthBySleep,
  };
  if (!game_code || !*game_code) return nullptr;
  for (const oga::Adapter* adapter : kPspAdapters) {
    if (!adapter->game_code || !*adapter->game_code) continue;
    if (std::strcmp(game_code, adapter->game_code) == 0) return adapter;
  }
  return nullptr;
}

bool EnsureAttached(void) {
  if (!g_adapter) return false;
  if (g_attached) return true;
  if (!g_adapter->attach) return false;
  if (!g_adapter->attach(&g_host)) return false;
  g_attached = true;
  __android_log_print(ANDROID_LOG_INFO, kTag, "adapter attached: %s (%s)",
                      g_adapter->id, g_adapter->display_name);
  return true;
}

}  // namespace

namespace oga {

// COPIED FROM Core/adapters.cpp — AdapterNoteSpoken / AdapterSpeechReset
// and the one-utterance memory behind them. The adapters gate their
// automatic lines on this process-wide dedup; on iOS the definitions
// live in the registry TU, which this build cannot link (see the file
// header). Keep the two in step; adapter.h declares them.
static char g_lastSpoken[256] = {0};
static bool g_haveLast = false;

bool AdapterNoteSpoken(const char* s) {
  if (!s) return false;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", s);
  if (g_haveLast && std::strcmp(buf, g_lastSpoken) == 0) return false;
  std::memcpy(g_lastSpoken, buf, sizeof(g_lastSpoken));
  g_haveLast = true;
  return true;
}

void AdapterSpeechReset(void) { g_haveLast = false; }

}  // namespace oga

void PspAdapterHostSetSpeechBridge(JNIEnv* env, jobject bridge) {
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
    HostLogLine("PspAdapterHost: AccessibilitySpeech method lookup failed");
    g_speakMethod = nullptr;
    g_stopMethod = nullptr;
    return;
  }
  g_bridge = env->NewGlobalRef(bridge);
}

void PspAdapterHostOnLoad(PspCore* core, const char* game_code) {
  PspAdapterHostOnStop();  // defensive: a previous selection never lingers
  g_core = core;
  g_adapter = FindPspAdapter(game_code);
  if (!g_adapter) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "no adapter for %s; the game runs without a reader",
                        game_code ? game_code : "");
    return;
  }

  oga::AnnounceSink sink{};
  sink.speak = &QueueSpeak;
  sink.log = &QueueLog;
  oga::AnnounceConfig config{};
  // pokecore's poke_create values: utterance completion is estimated
  // by the queue (nothing on this side reports it), diagnostics quiet.
  config.host_reports_done = false;
  config.diag_verbose = false;
  g_queue = oga::announce_create(sink, config);

  g_host = oga::Host{};
  g_host.read8 = &HostRead8;
  g_host.read16 = &HostRead16;
  g_host.read32 = &HostRead32;
  g_host.speak = &HostSpeak;
  g_host.log = &HostLog;
  g_host.set_button = &HostSetButton;
  g_host.announce_q = g_queue;
  g_host.ctx = core;
  g_host.now_ms = NowMs();

  psp_set_speech_callback(core, &CoreSpeech, nullptr);
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "adapter selected: %s (%s) for %s", g_adapter->id,
                      g_adapter->display_name, game_code);
}

void PspAdapterHostOnStop(void) {
  if (g_attached && g_adapter && g_adapter->detach) g_adapter->detach();
  g_attached = false;
  g_adapter = nullptr;
  g_core = nullptr;
  if (g_queue) {
    oga::announce_destroy(g_queue);
    g_queue = nullptr;
  }
  g_host = oga::Host{};
}

void PspAdapterHostFrame(PspCore* core) {
  if (!g_core || core != g_core) return;
  // pokecore's EndFrame: stamp the clock, run the adapter's frame
  // hook, then tick the queue so finished utterances release the next.
  g_host.now_ms = NowMs();
  if (g_attached && g_adapter && g_adapter->on_frame) g_adapter->on_frame();
  if (g_queue) oga::announce_tick(g_queue, g_host.now_ms);
}

bool PspAdapterHostReady(void) {
  if (!g_adapter) return false;
  if (!EnsureAttached()) return false;
  return g_adapter->ready ? g_adapter->ready() : true;
}

bool PspAdapterHostCommand(int cmd) {
  // poke_command's mirror, same order: adapter present, range, the
  // core-internal intercepts, lazy attach, the ready gate, dispatch.
  if (!g_adapter) return false;
  if (cmd < 0 || cmd > static_cast<int>(oga::Command::Objective)) {
    return false;
  }
  const oga::Command command = static_cast<oga::Command>(cmd);
  // Stop speech and the spoken-history walk belong to the core, not
  // the game adapter — they work even before the adapter is ready.
  // StopSpeech also stops the platform voice itself (the bridge),
  // which is pokecore's EmitSpeech(nullptr) half.
  if (command == oga::Command::StopSpeech) {
    if (g_queue) oga::announce_stop(g_queue, NowMs());
    BridgeSpeak(nullptr, true);
    return true;
  }
  if (command == oga::Command::RepeatNewest) {
    return g_queue && oga::announce_repeat_newest(g_queue, NowMs());
  }
  if (command == oga::Command::RepeatOlder) {
    return g_queue && oga::announce_repeat_older(g_queue, NowMs());
  }
  if (!g_adapter->command) return false;
  if (!EnsureAttached()) return false;
  if (g_adapter->ready && !g_adapter->ready()) return false;
  g_host.now_ms = NowMs();
  g_adapter->command(command);
  return true;
}

const char* PspAdapterHostId(void) {
  return g_adapter ? g_adapter->id : "";
}

const char* PspAdapterHostName(void) {
  return g_adapter ? g_adapter->display_name : "";
}
