package com.devin.opengameaccess

import android.content.Context
import android.content.Intent
import android.util.Log
import java.io.File

/**
 * ⛔ DEBUG HARNESS ONLY — the PPSSPP-on-Android stage 2 boot proof.
 *
 * Nothing in the shipping launcher calls this. [MainActivity.onCreate]
 * forwards its launch intent here; unless the intent carries [EXTRA_ROM]
 * this returns immediately and the launcher behaves exactly as before.
 * In practice the extra is only ever set by hand:
 *
 *   adb shell am start \
 *     -n me.magnum.melonds.dev/com.devin.opengameaccess.MainActivity \
 *     --es oga_psp_probe_rom /sdcard/Android/data/me.magnum.melonds.dev/files/game.cso \
 *     --ei oga_psp_probe_frames 900
 *
 * The proof itself is native and synchronous (PspSessionJNI.cpp): load a
 * disc image through the psp_* ABI, run N frames, and log the same PROOF
 * lines scripts/psp-host-proof-main.cpp prints for the host proof, under
 * the logcat tag "PspProof". This object only stages the two things the
 * core needs as real files — the bundled ppsspp-assets tree (extracted
 * below, because psp_core's DirectoryReader VFS cannot read APK assets)
 * and a save/memstick root — then gets out of the way. It is not the
 * stage-3 session: no screen, no input, no audio.
 */
object PspProbe {

    private const val TAG = "PspProof"

    const val EXTRA_ROM = "oga_psp_probe_rom"
    const val EXTRA_FRAMES = "oga_psp_probe_frames"

    // Where the asset tree lives inside the APK (staged into the frontend
    // assets by scripts/android-apply-overlay.sh section 8, out of
    // build-assets/ppsspp-assets — the audited boot set).
    private const val ASSET_ROOT = "ppsspp-assets"
    private const val EXTRACT_DIR = "ppsspp-assets"
    private const val ASSET_VERSION = "stage2-1"

    private external fun nativePspProof(
        romPath: String,
        saveDir: String,
        assetDir: String,
        frames: Int,
    ): Boolean

    /** Called from MainActivity.onCreate with the launch intent. */
    fun maybeStart(context: Context, intent: Intent?) {
        val rom = intent?.getStringExtra(EXTRA_ROM)?.takeIf { it.isNotBlank() } ?: return
        val frames = intent.getIntExtra(EXTRA_FRAMES, 900)
        val app = context.applicationContext
        val thread = Thread { runProof(app, rom, frames) }
        thread.name = "psp-probe"
        thread.start()
    }

    private fun runProof(context: Context, rom: String, frames: Int) {
        Log.i(TAG, "PROOF: kotlin-start rom=$rom frames=$frames")
        try {
            System.loadLibrary("melonDS-android-frontend")
        } catch (t: Throwable) {
            Log.e(TAG, "PROOF: FAILED library: ${t.message}")
            return
        }
        val assetDir = extractAssets(context)
        if (assetDir == null) {
            Log.e(TAG, "PROOF: FAILED assets: extraction failed")
            return
        }
        val saveDir = File(context.filesDir, "ppsspp-save")
        if (!saveDir.isDirectory && !saveDir.mkdirs()) {
            Log.e(TAG, "PROOF: FAILED save-dir: could not create ${saveDir.absolutePath}")
            return
        }
        // On armeabi-v7a the PSP core (and with it this native method) is
        // absent by design; turn that into a logged proof failure rather
        // than a dead thread.
        val ok = try {
            nativePspProof(rom, saveDir.absolutePath, assetDir, frames)
        } catch (t: Throwable) {
            Log.e(TAG, "PROOF: FAILED jni: ${t.message}")
            return
        }
        Log.i(TAG, "PROOF: kotlin-result ok=$ok")
    }

    /**
     * Copies the bundled ppsspp-assets tree into filesDir and returns its
     * absolute path, or null on failure. The version-stamped pattern is
     * GbAccessibilityScript.extractAssets': the copy happens once per
     * stamp, and the core itself fails loudly naming any missing manifest
     * file at load, so a partial extraction cannot slip through silently.
     */
    private fun extractAssets(context: Context): String? {
        val root = File(context.filesDir, EXTRACT_DIR)
        val stamp = File(root, ".version")

        if (stamp.isFile && stamp.readText().trim() == ASSET_VERSION &&
            File(root, "compat.ini").isFile
        ) {
            return root.absolutePath
        }

        return try {
            if (root.exists()) root.deleteRecursively()
            if (!root.mkdirs() && !root.isDirectory) {
                Log.e(TAG, "could not create ${root.absolutePath}")
                return null
            }

            val assets = context.assets
            var copied = 0
            fun copy(assetPath: String, target: File) {
                val children = assets.list(assetPath) ?: emptyArray()
                if (children.isEmpty()) {
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

            copy(ASSET_ROOT, root)
            stamp.writeText(ASSET_VERSION)
            Log.i(TAG, "extracted $copied PPSSPP asset files to ${root.absolutePath}")
            root.absolutePath
        } catch (e: Exception) {
            Log.e(TAG, "PPSSPP asset extraction failed", e)
            null
        }
    }
}
