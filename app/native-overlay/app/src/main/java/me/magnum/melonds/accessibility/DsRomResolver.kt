package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Turns a SAF content URI into a real filesystem path the DS host can
 * open. A twin of [GbRomResolver], for the same reason:
 *
 * ⛔ THE DS HOST READS THE ROM ITSELF, FROM A PATH. melonDS loads a
 * cartridge by seeking in a real file, and writes the battery save to
 * the path it is handed — there is no way to give it a content URI. So
 * the URI is materialised into the app's cache directory once and
 * reused after that. DS ROMs run large (up to 512 MB), so the copy
 * streams with a wide buffer and is only redone when the document's
 * length says the file changed.
 *
 * The copy is keyed on the document's name and length, and validated
 * against the document's last-modified time as well: a patched ROM
 * hack usually keeps the original's name AND its exact length (the
 * patch rewrites bytes in place), so length alone would hand the
 * player the stale cached bytes forever. When the document is newer
 * than the copy, the copy is redone; a provider that reports no
 * timestamp (0) leaves the length check as the only witness, the
 * same behaviour as before.
 */
object DsRomResolver
{
    private const val TAG = "DsAccess"

    private fun cacheDir(context: Context): File
    {
        val dir = File(context.cacheDir, "dsroms")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    /** A stable cache file name for a URI. */
    private fun cacheFileFor(context: Context, uri: Uri, document: DocumentFile?): File
    {
        val name = document?.name ?: uri.lastPathSegment?.substringAfterLast('/') ?: "rom.nds"
        val safe = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        // Prefix with the URI hash so two different ROMs that share a
        // filename (very common with "Pokemon White.nds" across regions)
        // do not overwrite each other.
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
                Log.i(TAG, "[ds] materialised $uri to ${target.absolutePath} (${target.length()} bytes)")
                target.absolutePath
            }
            catch (e: Exception)
            {
                Log.e(TAG, "[ds] could not materialise $uri", e)
                null
            }
        }

        // A plain path in the URI itself (the ROM list stores some of these).
        val path = uri.path
        return if (path != null && File(path).isFile) path else null
    }

    /**
     * The battery-save path the DS host should use for this ROM,
     * created if needed.
     *
     * ⛔ UNDER filesDir, NOT BESIDE THE CACHED ROM. The ROM copy lives
     * in the cache directory, which Android may evict under storage
     * pressure — a save kept beside it would vanish with the copy,
     * and the player would lose the game they were playing to a
     * cleanup they never saw. The save's name is the ROM copy's own
     * (already keyed by the URI hash in [cacheFileFor], so two ROMs
     * sharing a display name keep separate saves), kept in a
     * ds-saves folder in internal storage, which is never evicted.
     * A save left beside the ROM by an earlier version is migrated
     * across on first sight, so the upgrade does not orphan it.
     */
    fun savePathFor(context: Context, romPath: String): String?
    {
        val rom = File(romPath)
        // A ROM used in place (a real file the player pointed at) is
        // in no danger of eviction, and its save stays beside it —
        // the arrangement melonDS itself uses. Only a cache copy's
        // save moves to internal storage.
        if (!rom.absolutePath.startsWith(context.cacheDir.absolutePath))
        {
            val adjacent = File(rom.parentFile, rom.nameWithoutExtension + ".sav")
            return try
            {
                if (!adjacent.isFile) adjacent.createNewFile()
                adjacent.absolutePath
            }
            catch (e: Exception)
            {
                Log.w(TAG, "[ds] no save file at ${adjacent.absolutePath}", e)
                null
            }
        }
        val dir = File(context.filesDir, "ds-saves")
        if (!dir.isDirectory) dir.mkdirs()
        val save = File(dir, rom.nameWithoutExtension + ".sav")
        return try
        {
            if (!save.isFile)
            {
                val legacy = File(rom.parentFile, rom.nameWithoutExtension + ".sav")
                if (legacy.isFile)
                {
                    legacy.copyTo(save)
                    Log.i(TAG, "[ds] migrated battery save to ${save.absolutePath}")
                }
                else
                {
                    save.createNewFile()
                }
            }
            save.absolutePath
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[ds] no save file at ${save.absolutePath}", e)
            null
        }
    }
}
