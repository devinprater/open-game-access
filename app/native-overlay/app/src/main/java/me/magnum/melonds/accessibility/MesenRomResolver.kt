package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

/**
 * Resolves launcher-picked ROM URIs to real files the Mesen host can
 * open.
 *
 * A content:// URI is not a path, and the host takes paths, so picked
 * ROMs are copied into the app cache first. The copy's name carries a
 * hash of the URI, so the same URI resolves to the same file and
 * re-picking it is free (the file is rewritten only when the picked
 * source's size or mtime moved) — the DsRomResolver scheme, kept
 * whole: libretro-style ".zip" containers are left alone (Mesen opens
 * archives itself), and a content:// name with no cartridge suffix
 * gets ".nes" appended only when the caller's diagnosis says the file
 * really is one.
 *
 * Two differences from the DS resolver, both because of what Mesen
 * wants:
 *  - [homePathFor] returns a DIRECTORY, not a save file: pokecore's
 *    NES/SNES branches hand the load's second path to Mesen as its
 *    home folder (FolderUtilities::SetHomeFolder), inside which the
 *    battery save lives. One folder per game, under the app's files
 *    dir, named after the cache copy the way the DS .sav is named
 *    after its copy — so a re-pick keeps its save, and two different
 *    ROMs never share one.
 *  - The cache directory is its own ("mesenroms"), so a ROM picked
 *    for the Mesen session and later for another console's session
 *    never collide on a shared cache name.
 */
object MesenRomResolver
{
    /**
     * Copies [uri] into the app cache (or returns the cached copy) and
     * returns the local file. [diagnosedExtension], when non-null, is
     * appended to a copy name that carries no cartridge-matching
     * suffix of its own — the launcher passes it only when the picked
     * bytes were actually identified (a picker content:// name can be
     * anything, including a hash with no extension at all).
     *
     * Throws on IO failure or when the picked name cannot be
     * identified as a cartridge image at all; the caller reports the
     * failure in its own voice.
     */
    fun resolveRom(context: Context, uri: Uri, diagnosedExtension: String? = null): File
    {
        if (uri.scheme == "file")
        {
            val path = uri.path
            if (!path.isNullOrBlank()) return File(path)
        }

        val pickedName = displayName(context, uri)
            ?: uri.lastPathSegment?.substringAfterLast('/')
            ?: "game"
        val cacheDir = File(context.cacheDir, "mesenroms").apply { mkdirs() }
        val copyName = copyNameFor(uri, pickedName, diagnosedExtension)
        val target = File(cacheDir, copyName)
        if (isFreshEnough(context, uri, target)) return target

        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "Could not open the picked game file." }
            target.outputStream().use { output -> input.copyTo(output) }
        }
        return target
    }

    /**
     * The Mesen home folder for a ROM copy: a per-game directory under
     * the app's internal files, created on demand. Kept off the cache
     * (the cache is evictable; a battery save is not), beside the DS
     * session's saves rather than with the ROM.
     */
    fun homePathFor(context: Context, romCopy: File): String
    {
        val dir = File(File(context.filesDir, "mesen-homes"), romCopy.name)
        dir.mkdirs()
        return dir.absolutePath
    }

    private fun displayName(context: Context, uri: Uri): String?
    {
        if (uri.scheme != "content") return null
        return try
        {
            context.contentResolver.query(
                uri,
                arrayOf(OpenableColumns.DISPLAY_NAME),
                null,
                null,
                null,
            )?.use { cursor ->
                if (cursor.moveToFirst())
                {
                    val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (index >= 0) cursor.getString(index) else null
                }
                else null
            }
        }
        catch (t: Throwable)
        {
            null
        }
    }

    private fun copyNameFor(uri: Uri, pickedName: String, diagnosedExtension: String?): String
    {
        val base = pickedName.substringBeforeLast('.', pickedName)
            .replace(Regex("[^A-Za-z0-9._-]+"), "_")
            .ifBlank { "game" }
        val suffix = pickedName.substringAfterLast('.', "")
        val lower = suffix.lowercase()
        val extension = when
        {
            lower in MesenAccessibilityScript.ROM_EXTENSIONS -> lower
            lower == "zip" -> "zip"
            diagnosedExtension != null -> diagnosedExtension.lowercase()
            else -> throw IllegalArgumentException(
                "That file is not a NES or Super Nintendo game ($pickedName)."
            )
        }
        val key = Integer.toHexString(uri.toString().hashCode())
        return "${base}_$key.$extension"
    }

    private fun isFreshEnough(context: Context, uri: Uri, target: File): Boolean
    {
        if (!target.exists() || target.length() == 0L) return false
        val pickedSize = pickedSize(context, uri) ?: return false
        if (pickedSize != target.length()) return false
        val pickedModified = lastModified(context, uri)
        return pickedModified == null || target.lastModified() >= pickedModified
    }

    private fun pickedSize(context: Context, uri: Uri): Long?
    {
        if (uri.scheme != "content") return null
        return try
        {
            context.contentResolver.query(
                uri,
                arrayOf(OpenableColumns.SIZE),
                null,
                null,
                null,
            )?.use { cursor ->
                if (cursor.moveToFirst())
                {
                    val index = cursor.getColumnIndex(OpenableColumns.SIZE)
                    if (index >= 0 && !cursor.isNull(index)) cursor.getLong(index) else null
                }
                else null
            }
        }
        catch (t: Throwable)
        {
            null
        }
    }

    private fun lastModified(context: Context, uri: Uri): Long?
    {
        if (uri.scheme != "content") return null
        return try
        {
            context.contentResolver.query(
                uri,
                arrayOf("last_modified"),
                null,
                null,
                null,
            )?.use { cursor ->
                if (cursor.moveToFirst())
                {
                    val index = cursor.getColumnIndex("last_modified")
                    if (index >= 0 && !cursor.isNull(index)) cursor.getLong(index) else null
                }
                else null
            }
        }
        catch (t: Throwable)
        {
            null
        }
    }
}
