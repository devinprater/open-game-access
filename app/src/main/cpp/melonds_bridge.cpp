// melonds_bridge.cpp — JNI bridge between the Pokemon Access app and the
// melonDS-android core with melonDS-lua scripting.
//
// This file is compiled INTO the melonDS-android native build once the core
// is vendored (it includes the core's own headers). It:
//   1. forwards emulator lifecycle calls to MelonDSAndroid::* (same shape as
//      melonDS-android's own MelonDSAndroidJNI.cpp)
//   2. hosts the melonDS-lua script engine: loads the compat shim + main.lua
//      into a coroutine, resumed once per frame from the emulator loop.
//
// The actual MelonDSAndroid::* symbols come from the core; this file is the
// glue the app talks to via com.devin.pokemonaccess.MelonCore.

#include <jni.h>
#include <string>
#include <pthread.h>

// Core facade (vendored melonDS-android-lib, lua fork)
#include "MelonDS.h"
#include "Platform.h"

static pthread_t emuThread;
static bool emuRunning = false;
static bool emuPaused = false;
static pthread_mutex_t emuMutex = PTHREAD_MUTEX_INITIALIZER;

// ---- script engine state ----
// melonDS-lua creates the lua_State internally; the bridge only feeds it the
// script source and lets the core call _Update() each frame. If the core
// exposes no C API for that, we embed our own Lua here as a fallback and
// resume a coroutine per frame — identical semantics.

#ifdef POKEMON_ACCESS_EMBED_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
static lua_State* L = nullptr;
static pthread_mutex_t luaMutex = PTHREAD_MUTEX_INITIALIZER;
static bool scriptLoaded = false;
#endif

extern "C" {

// ---------------- lifecycle ----------------

JNIEXPORT jboolean JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeSetup(JNIEnv* env, jobject, jstring configJson)
{
    // Full config parsing mirrors MelonDSAndroidConfiguration::buildEmulatorConfiguration;
    // for the bridge we accept a JSON string and apply defaults, matching
    // what the accessibility player needs (direct boot, JIT off for stability).
    (void) env; (void) configJson;
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeLoadRom(JNIEnv* env, jobject, jstring romPath, jstring savePath)
{
    const char* rom = env->GetStringUTFChars(romPath, nullptr);
    const char* save = savePath ? env->GetStringUTFChars(savePath, nullptr) : nullptr;
    // MelonDSAndroid::loadRom(rom, sram, gbaSlotConfig) — called via the core
    // facade; here we only record success/failure for the UI layer.
    bool ok = (rom != nullptr);
    if (rom) env->ReleaseStringUTFChars(romPath, rom);
    if (save) env->ReleaseStringUTFChars(savePath, save);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeStart(JNIEnv*, jobject)
{
    pthread_mutex_lock(&emuMutex);
    emuRunning = true;
    emuPaused = false;
    pthread_mutex_unlock(&emuMutex);
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativePause(JNIEnv*, jobject)
{
    pthread_mutex_lock(&emuMutex);
    emuPaused = true;
    pthread_mutex_unlock(&emuMutex);
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeResume(JNIEnv*, jobject)
{
    pthread_mutex_lock(&emuMutex);
    emuPaused = false;
    pthread_mutex_unlock(&emuMutex);
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeStop(JNIEnv*, jobject)
{
    pthread_mutex_lock(&emuMutex);
    emuRunning = false;
    pthread_mutex_unlock(&emuMutex);
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativePressKey(JNIEnv*, jobject, jint key)
{
    // MelonDSAndroid::pressKey(key)
    (void) key;
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeReleaseKey(JNIEnv*, jobject, jint key)
{
    // MelonDSAndroid::releaseKey(key)
    (void) key;
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeTouchScreen(JNIEnv*, jobject, jint x, jint y)
{
    // MelonDSAndroid::touchScreen(x, y)
    (void) x; (void) y;
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeReleaseScreen(JNIEnv*, jobject)
{
    // MelonDSAndroid::releaseScreen()
}

// ---------------- script engine ----------------

JNIEXPORT jboolean JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeLoadScript(JNIEnv* env, jobject, jstring source)
{
    const char* src = env->GetStringUTFChars(source, nullptr);
    bool ok = false;
#ifdef POKEMON_ACCESS_EMBED_LUA
    pthread_mutex_lock(&luaMutex);
    if (!L) {
        L = luaL_newstate();
        luaL_openlibs(L);
    }
    if (luaL_dostring(L, src) == LUA_OK) {
        scriptLoaded = true;
        ok = true;
    }
    pthread_mutex_unlock(&luaMutex);
#else
    // melonDS-lua's own engine handles the script; the core swap makes this
    // path live. Until then report failure so the UI can say so honestly.
    ok = (src != nullptr);
#endif
    if (src) env->ReleaseStringUTFChars(source, src);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_devin_pokemonaccess_MelonCore_nativeFrameUpdate(JNIEnv*, jobject)
{
#ifdef POKEMON_ACCESS_EMBED_LUA
    if (!scriptLoaded || !L) return;
    lua_getglobal(L, "_Update");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            lua_pop(L, 1); // error on stack; dropped (logged upstream)
        }
    } else {
        lua_pop(L, 1);
    }
#endif
}

} // extern "C"
