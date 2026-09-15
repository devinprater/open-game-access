/*
    MGBAScriptJNI.cpp — the JNI bridge for the Game Boy / GBC / GBA path.

    Same shape as PokeScriptJNI.cpp, and deliberately sharing the SAME Kotlin
    speech object (me.magnum.melonds.accessibility.AccessibilitySpeech), so one
    app speaks for both cores and there is exactly one TextToSpeech engine.

    Responsibilities:

      * extract the bundled Pokémon Access v3.1.0 asset tree out of the APK
        once per launch and hand the native host a real directory path. The
        script's `require`/`loadfile` calls need paths on the filesystem, and a
        Lua chunk cannot be loaded from an Android asset stream;
      * load a .gb/.gbc/.gba ROM into an MGBARunner owned here;
      * start the script with the asset directory;
      * resume it once per emulated frame and drive the mGBA frame loop;
      * hand the framebuffer to Kotlin as an RGBA byte array.

    ⛔ The speech callback fires on the emulator thread, inside the frame loop.
    It must not block: the Kotlin side hands the string straight to
    TextToSpeech and returns.
*/

#include <jni.h>
#include <android/log.h>

#include <memory>
#include <string>
#include <vector>

#include "MGBARunner.h"
#include "MGBACore.h"
#include "MelonDSAndroidInterface.h"

#define LOG_TAG "PokemonAccess"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace
{

std::unique_ptr<MelonDSAndroid::MGBARunner> gRunner;

// The Kotlin bridge object — the same AccessibilitySpeech instance the NDS path
// uses. Held as a global ref for the ROM session.
jobject gSpeechBridge = nullptr;
jmethodID gSpeakMethod = nullptr;   // (Ljava/lang/String;Z)V
jmethodID gStopMethod = nullptr;    // ()V
jmethodID gSoundMethod = nullptr;   // (Ljava/lang/String;II)V

JNIEnv* currentEnv()
{
    return jniEnvHandler ? jniEnvHandler->getCurrentThreadEnv() : nullptr;
}

void callSpeech(const char* text, bool interrupt)
{
    if (gSpeechBridge == nullptr) return;
    JNIEnv* env = currentEnv();
    if (env == nullptr) return;
    if (text == nullptr)
    {
        env->CallVoidMethod(gSpeechBridge, gStopMethod);
    }
    else
    {
        jstring jtext = env->NewStringUTF(text);
        env->CallVoidMethod(gSpeechBridge, gSpeakMethod, jtext, interrupt ? JNI_TRUE : JNI_FALSE);
        env->DeleteLocalRef(jtext);
    }
    // A Java exception must never unwind through the Lua C frames.
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

} // namespace

extern "C"
{

/// Installs the speech bridge. Called with the same AccessibilitySpeech the NDS
/// path uses.
JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_setGbSpeechBridge(JNIEnv* env, jobject thiz, jobject bridge)
{
    if (gSpeechBridge != nullptr)
    {
        env->DeleteGlobalRef(gSpeechBridge);
        gSpeechBridge = nullptr;
        gSpeakMethod = nullptr;
        gStopMethod = nullptr;
        gSoundMethod = nullptr;
    }
    if (bridge == nullptr) return;

    jclass cls = env->GetObjectClass(bridge);
    gSpeakMethod = env->GetMethodID(cls, "speak", "(Ljava/lang/String;Z)V");
    gStopMethod = env->GetMethodID(cls, "stop", "()V");
    // Optional: the cue sounds. A bridge without it still works, silently.
    gSoundMethod = env->GetMethodID(cls, "playSound", "(Ljava/lang/String;II)V");
    if (gSpeakMethod == nullptr || gStopMethod == nullptr)
    {
        LOGI("[pokemon-access-gb] speech bridge is missing speak(String,boolean)/stop()");
        env->DeleteLocalRef(cls);
        return;
    }
    gSpeechBridge = env->NewGlobalRef(bridge);
    env->DeleteLocalRef(cls);
    LOGI("[pokemon-access-gb] speech bridge installed");
}

/// Loads a GB/GBC/GBA ROM and starts the accessibility script against it.
/// `assetDir` is the absolute directory the assets were extracted to and must
/// end in '/'. Returns true when the script compiled and reached its first
/// frame yield.
JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_loadGbRom(
    JNIEnv* env, jobject thiz, jstring romPath, jstring savePath, jstring scriptText, jstring assetDir)
{
    if (romPath == nullptr || scriptText == nullptr || assetDir == nullptr)
    {
        LOGE("[pokemon-access-gb] loadGbRom called with a missing argument");
        return JNI_FALSE;
    }

    const char* rom = env->GetStringUTFChars(romPath, nullptr);
    const char* save = savePath == nullptr ? nullptr : env->GetStringUTFChars(savePath, nullptr);
    const char* text = env->GetStringUTFChars(scriptText, nullptr);
    const char* assets = env->GetStringUTFChars(assetDir, nullptr);

    std::string romString(rom ? rom : "");
    std::string saveString(save ? save : "");
    std::string textString(text ? text : "");
    std::string assetString(assets ? assets : "");

    env->ReleaseStringUTFChars(romPath, rom);
    if (save) env->ReleaseStringUTFChars(savePath, save);
    env->ReleaseStringUTFChars(scriptText, text);
    env->ReleaseStringUTFChars(assetDir, assets);

    gRunner = std::make_unique<MelonDSAndroid::MGBARunner>();

    if (!gRunner->loadRom(romString, saveString))
    {
        LOGE("[pokemon-access-gb] mGBA could not load %s", romString.c_str());
        gRunner.reset();
        return JNI_FALSE;
    }

    LOGI("[pokemon-access-gb] loaded %s as platform '%s'",
         romString.c_str(), gRunner->platformName().c_str());

    auto* script = gRunner->script();
    script->setSpeechCallback([](const char* speechText, bool interrupt, void*) {
        callSpeech(speechText, interrupt);
    }, nullptr);
    script->setLogCallback([](const char* logText, void*) {
        LOGI("%s", logText ? logText : "");
    }, nullptr);
    script->setSoundCallback([](const char* path, int pan, int volume, void*) {
        if (gSoundMethod == nullptr || gSpeechBridge == nullptr || path == nullptr) return;
        JNIEnv* jenv = currentEnv();
        if (jenv == nullptr) return;
        jstring jpath = jenv->NewStringUTF(path);
        jenv->CallVoidMethod(gSpeechBridge, gSoundMethod, jpath, pan, volume);
        jenv->DeleteLocalRef(jpath);
        if (jenv->ExceptionCheck())
        {
            jenv->ExceptionDescribe();
            jenv->ExceptionClear();
        }
    }, nullptr);

    script->setScriptDirectory(assetString);
    script->setScriptText(std::move(textString));

    if (!script->start())
    {
        LOGE("[pokemon-access-gb] script failed to start: %s", script->lastError().c_str());
        return JNI_FALSE;
    }

    LOGI("[pokemon-access-gb] script started (pokemon.lua, platform %s)",
         gRunner->platformName().c_str());
    return JNI_TRUE;
}

/// Runs one emulated frame plus one resume of the screen reader. Called from
/// the frame thread.
JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_runGbFrame(JNIEnv* env, jobject thiz)
{
    if (gRunner) gRunner->runFrame();
}

/// Copies the current RGBA8888 frame into `out` (240*160 or 160*144 pixels).
/// Returns the pixel count, or 0 if nothing is loaded.
JNIEXPORT jint JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_copyGbFrame(JNIEnv* env, jobject thiz, jintArray out, jintArray sizeOut)
{
    if (!gRunner) return 0;
    int width = 0, height = 0, stride = 0;
    const uint32_t* pixels = gRunner->framebuffer(&width, &height, &stride);
    if (!pixels || width <= 0 || height <= 0) return 0;

    int count = width * height;
    if (out == nullptr || env->GetArrayLength(out) < count) return 0;

    // ⛔ mGBA writes mColor, which is XBGR8 (0x00BBGGRR) — the TOP byte is
    // unused and is therefore ZERO. On little-endian the bytes in memory are
    // R, G, B, 0x00, and the renderer uploads with GL_RGBA + GL_UNSIGNED_BYTE,
    // which reads that fourth byte as ALPHA. Leaving it at 0 makes every pixel
    // fully transparent and the screen renders solid black even though the
    // colours are correct. Force the alpha byte opaque.
    //
    // The RGB order matches RGBA byte order as-is, so only alpha needs setting;
    // jint is 32-bit on Android so the words transfer without a copy.
    if (stride == width)
    {
        std::vector<jint> swizzled((size_t) count);
        for (int i = 0; i < count; i++)
            swizzled[(size_t) i] = (jint) (pixels[i] | 0xFF000000u);
        env->SetIntArrayRegion(out, 0, count, swizzled.data());
    }
    else
    {
        // Padded rows: copy row by row so the stride does not smear.
        std::vector<jint> swizzled((size_t) count);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                swizzled[(size_t) (y * width + x)] =
                    (jint) (pixels[(size_t) (y * stride + x)] | 0xFF000000u);
        env->SetIntArrayRegion(out, 0, count, swizzled.data());
    }
    if (sizeOut != nullptr && env->GetArrayLength(sizeOut) >= 2)
    {
        jint size[2] = { (jint) width, (jint) height };
        env->SetIntArrayRegion(sizeOut, 0, 2, size);
    }
    return count;
}

JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_isGbScriptRunning(JNIEnv* env, jobject thiz)
{
    if (!gRunner || !gRunner->script()) return JNI_FALSE;
    return gRunner->script()->isRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_getGbScriptFrameCount(JNIEnv* env, jobject thiz)
{
    if (!gRunner || !gRunner->script()) return 0;
    return (jlong) gRunner->script()->framesRun();
}

/// Which core the loaded ROM wanted: "gba", "gb", or "".
JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_getGbPlatform(JNIEnv* env, jobject thiz)
{
    if (!gRunner) return env->NewStringUTF("");
    return env->NewStringUTF(gRunner->platformName().c_str());
}

/// A host key went down/up. `key` is the ASCII code of an upper-case letter.
JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_setGbHotkey(JNIEnv* env, jobject thiz, jchar key, jboolean down)
{
    if (!gRunner || !gRunner->script()) return;
    gRunner->script()->setHotkey((char) key, down == JNI_TRUE);
}

/// The emulated pad's state, as mGBA key bits
/// (0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L).
JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_setGbKeys(JNIEnv* env, jobject thiz, jint keys)
{
    if (gRunner) gRunner->setKeys((uint32_t) keys);
}

/// Stops the script and tears the core down. Idempotent.
JNIEXPORT void JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_stopGbRom(JNIEnv* env, jobject thiz)
{
    if (!gRunner) return;
    auto* script = gRunner->script();
    if (script) script->setSpeechCallback(nullptr, nullptr);
    gRunner.reset();
    LOGI("[pokemon-access-gb] stopped");
}

/// registerexec diagnostics: how many hooks are live and how often they fired.
JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_accessibility_GbAccessibilityScript_getGbExecHookStats(JNIEnv* env, jobject thiz, jlongArray out)
{
    if (!gRunner || !gRunner->script()) return 0;
    auto* script = gRunner->script();
    if (out != nullptr && env->GetArrayLength(out) >= 2)
    {
        jlong stats[2] = { (jlong) script->execCallbackCount(), (jlong) script->execBreakpointCount() };
        env->SetLongArrayRegion(out, 0, 2, stats);
    }
    return 0;
}

} // extern "C"
