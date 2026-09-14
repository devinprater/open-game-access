package me.magnum.melonds.accessibility

import android.content.Context
import android.util.Log
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets

/**
 * Loads the Pokémon Access accessibility script out of the APK's assets and
 * hands it to the native Lua host.
 *
 * TWO FILES, ONE CHUNK. `main.lua` is Ola's accessibility script, and it is
 * loaded byte-for-byte as published — never edited, never regenerated.
 * `bizhawk_compat.lua` is the shim that recreates the BizHawk API surface
 * (mainmemory, joypad, input.get, emu.frameadvance, console.writeline,
 * gameinfo, speech) on top of this core's Lua library.
 *
 * ⛔ THE ORDER AND THE CONCATENATION ARE BOTH LOAD-BEARING:
 *   * the shim goes FIRST, because main.lua's top-level code calls into it
 *     immediately (detect_game() reads the ROM header through memory.read_u8);
 *   * they go into the SAME chunk, because that is what the shim was written
 *     for — it declares no top-level locals (everything lives in one `do ... end`
 *     block) precisely so it spends none of the 200-locals-per-chunk budget that
 *     main.lua needs. Loading them as two chunks would also "work" but would
 *     diverge from the iOS reference build, which is verified.
 *
 * The `\n` separator is required: main.lua is not guaranteed to end in a
 * newline, and a chunk whose first main.lua line begins with a Lua long comment
 * or a label would otherwise be swallowed by the shim's last line.
 */
object AccessibilityScript
{
    private const val TAG = "PokemonAccess"

    private const val SHIM_ASSET = "lua/bizhawk_compat.lua"
    private const val MAIN_ASSET = "lua/main.lua"

    // Native side (PokeScriptJNI.cpp).
    private external fun setSpeechBridge(bridge: Any?)
    private external fun startScript(scriptText: String): Boolean
    private external fun isScriptRunning(): Boolean
    private external fun getScriptFrameCount(): Long
    private external fun stopScript()
    private external fun setHotkey(key: Char, down: Boolean)
    private external fun speakNow(text: String, interrupt: Boolean)

    // The single speech bridge for the process. Held here so the native global
    // ref and this Kotlin object have the same lifetime.
    private var speech: AccessibilitySpeech? = null

    /** True once the script has compiled and reached its first frame yield. */
    val isRunning: Boolean get() = isScriptRunning()

    val frameCount: Long get() = getScriptFrameCount()

    /**
     * Called once, before any ROM is loaded: builds the TTS engine and points
     * the native speech callback at it.
     */
    fun initialize(context: Context)
    {
        if (speech == null)
        {
            val s = AccessibilitySpeech(context.applicationContext)
            s.init()
            speech = s
            setSpeechBridge(s)
            Log.i(TAG, "[pokemon-access] speech bridge ready")
        }
    }

    /**
     * Reads both assets, concatenates them and starts the script against the
     * running console. Returns true on success.
     *
     * The read is ~2.1 MB of UTF-8 and happens once per ROM launch. `InputStreamReader`
     * with the file's own charset (UTF-8) is what keeps the script's many literal
     * strings — accented Pokémon names, arrows, ♪ ★ — from being mangled; reading
     * bytes as ISO-8859-1 or letting the platform default win would corrupt them.
     */
    fun start(context: Context): Boolean
    {
        initialize(context)

        val shim = readAsset(context, SHIM_ASSET) ?: return false
        val main = readAsset(context, MAIN_ASSET) ?: return false

        Log.i(TAG, "[pokemon-access] assets: ${SHIM_ASSET} (${shim.length} chars), ${MAIN_ASSET} (${main.length} chars)")

        val script = StringBuilder(shim.length + main.length + 8)
            .append(shim)
            .append('\n')
            .append(main)
            .toString()

        val started = startScript(script)
        if (started)
        {
            Log.i(TAG, "[pokemon-access] accessibility script loaded (shim + main.lua, ${script.length} chars)")
        }
        else
        {
            Log.e(TAG, "[pokemon-access] accessibility script failed to load")
        }
        return started
    }

    fun stop()
    {
        stopScript()
    }

    /**
     * The shared TextToSpeech bridge, for the GB/GBA path to reuse.
     *
     * ONE APP, ONE VOICE. The Game Boy reader (see [GbAccessibilityScript])
     * routes its speech through the same AccessibilitySpeech instance, so the
     * player hears the same engine and the same voice whichever console the ROM
     * turns out to be for, and there is never a second TextToSpeech alive to
     * fight over the audio focus.
     */
    fun speechBridge(context: Context): AccessibilitySpeech
    {
        initialize(context)
        return speech!!
    }

    fun shutdown()
    {
        stopScript()
        setSpeechBridge(null)
        speech?.shutdown()
        speech = null
    }

    /** Speak a line from the app itself (not the script). */
    fun speak(text: String, interrupt: Boolean = true)
    {
        speakNow(text, interrupt)
    }

    /**
     * Host key down/up, forwarded to the script so main.lua's poll_keys() can
     * edge-detect the letters it listens for (R stop speech, U repeat, C the
     * overworld coordinates key, and the J/K/L/I/O navigation letters the
     * controller-mod layer maps pad buttons onto).
     *
     * `key` is the ASCII code of the UPPER-CASE letter: the script's letter
     * names are upper-case ("R", "U", ...) and its shim translates codes to
     * names through an A-Z table.
     */
    fun onKeyEvent(key: Char, down: Boolean)
    {
        val upper = key.uppercaseChar()
        if (upper !in 'A'..'Z') return
        setHotkey(upper, down)
    }

    private fun readAsset(context: Context, name: String): String?
    {
        return try
        {
            context.assets.open(name).use { stream ->
                InputStreamReader(stream, StandardCharsets.UTF_8).use { it.readText() }
            }
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[pokemon-access] could not read asset $name", e)
            null
        }
    }
}
