package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets

/**
 * The Game Boy / Game Boy Color / Game Boy Advance side of the Pokémon Access
 * integration: mGBA as the core, Pokémon Access v3.1.0 (`pokemon.lua` +
 * `gb.lua`/`gba.lua`) as the reader.
 *
 * This is a sibling of [AccessibilityScript], not a replacement. The NDS path
 * (melonDS + Ola's `main.lua`) is untouched and still loaded by
 * [AccessibilityScript]; this one handles `.gb`, `.gbc` and `.gba`.
 *
 * ⛔ THE SCRIPT'S DATA FILES MUST EXIST AS REAL FILES, NOT ASSETS.
 * `pokemon.lua` finds its per-game tables with `loadfile(scriptpath .. "game\\…")`
 * and its messages with `loadfile(scriptpath .. "\\message\\…")`. `loadfile` and
 * `require` cannot read an Android asset stream, so the 195-file asset tree is
 * copied to the app's files directory once per launch (and only when the tree
 * changes, keyed on the bundled `pokemon.lua` size).
 *
 * ⛔ THE BACKSLASHES ARE EXPECTED. The script joins its paths with `\` because
 * it was written for Windows. The native host normalises them, so nothing here
 * should try to "fix" the script — see MGBACore.cpp's normalizePath.
 */
object GbAccessibilityScript
{
    private const val TAG = "PokemonAccess"

    // Where the asset tree lives inside the APK.
    private const val GB_ASSET_ROOT = "lua/gb"
    // Where it is extracted to, relative to filesDir.
    private const val EXTRACT_DIR = "pokemon-access-gb"
    // The version stamp: any change to this forces a re-extract.
    private const val ASSET_VERSION = "3.1.0-1"

    /** The ROM extensions this path handles. */
    val ROM_EXTENSIONS = setOf("gb", "gbc", "gba")

    // Native side (MGBAScriptJNI.cpp).
    private external fun setGbSpeechBridge(bridge: Any?)
    private external fun loadGbRom(romPath: String, savePath: String?, scriptText: String, assetDir: String): Boolean
    private external fun runGbFrame()
    private external fun copyGbFrame(out: IntArray, sizeOut: IntArray): Int
    private external fun isGbScriptRunning(): Boolean
    private external fun getGbScriptFrameCount(): Long
    private external fun getGbPlatform(): String
    private external fun setGbHotkey(key: Char, down: Boolean)
    private external fun setGbKeys(keys: Int)
    private external fun stopGbRom()
    private external fun getGbExecHookStats(out: LongArray)

    val isRunning: Boolean get() = isGbScriptRunning()
    val frameCount: Long get() = getGbScriptFrameCount()
    /** "gba", "gb", or "" — what the loaded cart turned out to be. */
    val platform: String get() = getGbPlatform()

    /**
     * Picks the core for a ROM by extension. `.nds`/`.dsi`/`.ids` stay on the
     * melonDS path; anything else is not something this app can run.
     */
    fun handlesExtension(extension: String?): Boolean
    {
        return extension?.lowercase() in ROM_EXTENSIONS
    }

    fun handlesUri(uri: Uri): Boolean
    {
        val name = uri.lastPathSegment ?: uri.toString()
        return handlesExtension(name.substringAfterLast('.', ""))
    }

    /**
     * Reads `pokemon.lua` out of the assets, concatenated with nothing else: the
     * GB/GBA script is a single entry point, unlike the NDS one which needs a
     * compat shim. The host installs the equivalent of that shim in C.
     */
    private fun readScript(context: Context): String?
    {
        return try
        {
            context.assets.open("$GB_ASSET_ROOT/pokemon.lua").use { stream ->
                InputStreamReader(stream, StandardCharsets.UTF_8).use { it.readText() }
            }
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[pokemon-access-gb] could not read $GB_ASSET_ROOT/pokemon.lua", e)
            null
        }
    }

    /**
     * Copies the whole script tree into the app's files directory and returns
     * its absolute path (with a trailing '/'), or null on failure.
     *
     * AssetManager.list() only lists one directory level, so this walks
     * recursively. The tree is ~2.7 MB and 195 files; it happens once per
     * version stamp, not once per ROM.
     */
    fun extractAssets(context: Context): String?
    {
        val root = File(context.filesDir, EXTRACT_DIR)
        val stamp = File(root, ".version")

        if (stamp.isFile && stamp.readText().trim() == ASSET_VERSION && File(root, "pokemon.lua").isFile)
        {
            return root.absolutePath + "/"
        }

        return try
        {
            if (root.exists()) root.deleteRecursively()
            if (!root.mkdirs() && !root.isDirectory)
            {
                Log.e(TAG, "[pokemon-access-gb] could not create ${root.absolutePath}")
                return null
            }

            val assets = context.assets
            var copied = 0
            fun copy(assetPath: String, target: File)
            {
                val children = assets.list(assetPath) ?: emptyArray()
                if (children.isEmpty())
                {
                    // A file, not a directory.
                    target.parentFile?.mkdirs()
                    assets.open(assetPath).use { input ->
                        target.outputStream().use { output -> input.copyTo(output, 64 * 1024) }
                    }
                    copied++
                    return
                }
                target.mkdirs()
                for (child in children) copy("$assetPath/$child", File(target, child))
            }

            copy(GB_ASSET_ROOT, root)
            stamp.writeText(ASSET_VERSION)
            Log.i(TAG, "[pokemon-access-gb] extracted $copied script files to ${root.absolutePath}")
            root.absolutePath + "/"
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[pokemon-access-gb] asset extraction failed", e)
            null
        }
    }

    /**
     * Starts the GB/GBA reader against an already-loaded mGBA core.
     *
     * `romPath` and `savePath` are native paths: the native side reads the ROM
     * itself, so the caller must have resolved the SAF content URI to a real
     * file first (see [GbRomResolver]).
     */
    fun start(context: Context, romPath: String, savePath: String?): Boolean
    {
        val speech = AccessibilityScript.speechBridge(context)
        setGbSpeechBridge(speech)

        val assetDir = extractAssets(context) ?: return false
        val script = readScript(context) ?: return false

        Log.i(TAG, "[pokemon-access-gb] script ${script.length} chars, assets at $assetDir")

        val started = loadGbRom(romPath, savePath, script, assetDir)
        if (started)
        {
            Log.i(TAG, "[pokemon-access-gb] accessibility script loaded (pokemon.lua, platform $platform)")
        }
        else
        {
            Log.e(TAG, "[pokemon-access-gb] accessibility script failed to load")
        }
        return started
    }

    fun stop()
    {
        stopGbRom()
    }

    fun shutdown()
    {
        stopGbRom()
        setGbSpeechBridge(null)
    }

    fun runFrame()
    {
        runGbFrame()
    }

    /**
     * The current frame as RGBA8888 ints, plus its size. Returns (pixels, width,
     * height), or null when nothing is loaded.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>?
    {
        val pixels = IntArray(240 * 160)
        val size = IntArray(2)
        val count = copyGbFrame(pixels, size)
        if (count <= 0) return null
        return Triple(pixels, size[0], size[1])
    }

    /** registerexec hooks installed and how often they fired (diagnostics). */
    fun execHookStats(): Pair<Long, Long>
    {
        val stats = LongArray(2)
        getGbExecHookStats(stats)
        return stats[0] to stats[1]
    }

    /** Host key down/up, for the script's own hotkeys (letters). */
    fun onKeyEvent(key: Char, down: Boolean)
    {
        val upper = key.uppercaseChar()
        if (upper !in 'A'..'Z') return
        setGbHotkey(upper, down)
    }

    /** The emulated pad, as mGBA key bits. */
    fun setKeys(keys: Int)
    {
        setGbKeys(keys)
    }
}
