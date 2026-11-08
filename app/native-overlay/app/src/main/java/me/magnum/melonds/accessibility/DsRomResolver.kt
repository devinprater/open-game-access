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
 * The copy is keyed on the document's name and length, which is enough
 * to tell "the player opened this ROM again" from "the player replaced
 * the file with a patched hack of the same name" for every case that
 * matters here.
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

            if (target.isFile && (expectedLength <= 0 || target.length() == expectedLength))
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
     * created if needed. Keeping it beside the ROM's cached copy means
     * saves survive between sessions without going through SAF — the
     * same arrangement melonDS itself uses (the .sav next to the ROM).
     */
    fun savePathFor(context: Context, romPath: String): String?
    {
        val rom = File(romPath)
        val save = File(rom.parentFile, rom.nameWithoutExtension + ".sav")
        return try
        {
            if (!save.isFile) save.createNewFile()
            save.absolutePath
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[ds] no save file at ${save.absolutePath}", e)
            null
        }
    }
}
