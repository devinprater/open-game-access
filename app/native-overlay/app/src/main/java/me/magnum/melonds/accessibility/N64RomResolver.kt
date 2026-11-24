package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Turns a SAF content URI into a real filesystem path the Nintendo 64
 * host can open. A twin of [GenesisRomResolver], for the same reason:
 *
 * ⛔ THE N64 HOST READS THE ROM ITSELF, FROM A PATH. Mupen64Plus-Next
 * loads a cartridge by reading a real file, and the wrapper reads and
 * writes the battery save at the path it is handed — there is no way to
 * give it a content URI. So the URI is materialised into the app's cache
 * directory once and reused after that, with the Genesis twin's
 * freshness rule (name, length and the document's own timestamp).
 */
object N64RomResolver
{
    private const val TAG = "N64Access"

    private fun cacheDir(context: Context): File
    {
        val dir = File(context.cacheDir, "n64roms")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    /** A stable cache file name for a URI. */
    private fun cacheFileFor(context: Context, uri: Uri, document: DocumentFile?): File
    {
        val name = document?.name ?: uri.lastPathSegment?.substringAfterLast('/') ?: "rom.z64"
        val safe = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        // Prefix with the URI hash so two different ROMs that share a
        // filename do not overwrite each other.
        return File(cacheDir(context), "${uri.toString().hashCode().toUInt().toString(16)}-$safe")
    }

    /**
     * Returns an absolute path to the ROM's bytes, or null. A `file://`
     * URI is used in place; anything else is copied into the cache.
     */
    fun resolve(context: Context, uri: Uri): String?
    {
        if (uri.scheme == "file")
        {
            val path = uri.path
            if (path != null && File(path).isFile) return path
        }

        val document = DocumentFile.fromSingleUri(context, uri)

        if (uri.scheme == "content" || uri.scheme == "android.resource")
        {
            val target = cacheFileFor(context, uri, document)
            val expectedLength = document?.length() ?: -1L
            // The freshness witness: the document's own timestamp,
            // when the provider reports one. The copy's timestamp is
            // when it was made, so a document modified after that —
            // a patched hack dropped over the original — is newer
            // and the copy is redone below instead of reused.
            val documentModified = document?.lastModified() ?: 0L

            if (target.isFile &&
                (expectedLength <= 0 || target.length() == expectedLength) &&
                (documentModified <= 0L || documentModified <= target.lastModified()))
            {
                return target.absolutePath
            }

            return try
            {
                context.contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { output -> input.copyTo(output, 256 * 1024) }
                } ?: return null
                Log.i(TAG, "[n64] materialised $uri to ${target.absolutePath} (${target.length()} bytes)")
                target.absolutePath
            }
            catch (e: Exception)
            {
                Log.e(TAG, "[n64] could not materialise $uri", e)
                null
            }
        }

        // A plain path in the URI itself (the ROM list stores some of these).
        val path = uri.path
        return if (path != null && File(path).isFile) path else null
    }

    /**
     * The battery-save path the N64 host should use for this ROM. For
     * this backend the path IS the battery file (EEPROM, SRAM, FlashRAM
     * and the Controller Paks in the core's own layout): the wrapper
     * restores it at load when its size matches and writes it back on
     * stop when the game changed it. An empty file is created up front
     * so a bad folder fails here, in words, not silently at quit; the
     * wrapper treats a file of the wrong size (an empty one included) as
     * "no save yet".
     *
     * ⛔ UNDER filesDir, NOT BESIDE THE CACHED ROM — the Genesis twin's
     * rule and reason: the ROM copy lives in the cache, which Android may
     * evict, and a save kept beside it would vanish with it. A ROM used in
     * place keeps its save beside it, as emulator frontends traditionally
     * do. The extension is .srm, the name Mupen64Plus-Next's libretro
     * frontends use for this block.
     */
    fun savePathFor(context: Context, romPath: String): String?
    {
        val rom = File(romPath)
        // A ROM used in place (a real file the player pointed at) is
        // in no danger of eviction, and its save stays beside it —
        // the arrangement emulator frontends traditionally use. Only
        // a cache copy's save moves to internal storage.
        if (!rom.absolutePath.startsWith(context.cacheDir.absolutePath))
        {
            val adjacent = File(rom.parentFile, rom.nameWithoutExtension + ".srm")
            return try
            {
                if (!adjacent.isFile) adjacent.createNewFile()
                adjacent.absolutePath
            }
            catch (e: Exception)
            {
                Log.w(TAG, "[n64] no save file at ${adjacent.absolutePath}", e)
                null
            }
        }
        val dir = File(context.filesDir, "n64-saves")
        if (!dir.isDirectory) dir.mkdirs()
        val save = File(dir, rom.nameWithoutExtension + ".srm")
        return try
        {
            if (!save.isFile) save.createNewFile()
            save.absolutePath
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[n64] no save file at ${save.absolutePath}", e)
            null
        }
    }
}
