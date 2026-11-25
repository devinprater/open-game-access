package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Turns a SAF content URI into a real filesystem path the PlayStation 2
 * host can open. A sibling of [Ps1RomResolver], with the PlayStation 2
 * differences:
 *
 * ⛔ SINGLE-FILE DISCS ONLY. A PS2 game in this build is one .iso or
 * one .chd; nothing a PS2 disc names lives outside its own file, so
 * there are no index files to follow and no folder to grant (the
 * PlayStation resolver's whole second half has no counterpart here).
 * Each picked document is copied into its own folder under the cache
 * (keyed by the URI's hash, so two discs that share a name never
 * collide) under its original name.
 *
 * ⛔ THE BIOS IS IMPORTED ONCE AND STAGED BESIDE EVERY DISC. A PS2 BIOS
 * is the player's own dump (4 MiB .bin files, the PCSX2 collection's
 * shape). [importBios] keeps a picked BIOS in internal storage, and
 * [resolve] copies every imported BIOS into the disc's folder under
 * pcsx2/bios, where the host looks for it. With a BIOS staged, the
 * wrapper prefers ARMSX2; with none, Play! runs its HLE BIOS.
 *
 * ⛔ THE SAVE PATH IS A FOLDER. The cores keep their own memory-card
 * files; [savePathFor] prepares one folder per disc under filesDir.
 */
object Ps2RomResolver
{
    private const val TAG = "Ps2Access"

    /** The reason the last [resolve] refused, in words, or "". */
    @Volatile
    var lastProblem: String = ""
        private set

    /**
     * BIOS file names this import recognises: the ps2-*.bin spelling of
     * the PCSX2 BIOS collection (ps2-0230a-20080220.bin and its kin).
     * Deliberately NOT the scph*.bin spelling: that name is shared with
     * the PlayStation's BIOS files, and the launcher asks the
     * PlayStation first — a PlayStation BIOS must never land here.
     * (A PS2 BIOS named scph*.bin can be renamed to ps2-*.bin and
     * imported the same way.)
     */
    private val BIOS_NAME = Regex("(?i)^ps2[0-9a-z_-]*\\.bin$")

    /** The size of a PS2 BIOS image: 4 MiB. */
    private const val BIOS_BYTES = 4L * 1024L * 1024L

    /** True when a picked file name looks like a PlayStation 2 BIOS. */
    fun isBiosName(name: String): Boolean = BIOS_NAME.matches(name)

    private fun cacheRoot(context: Context): File
    {
        val dir = File(context.cacheDir, "ps2roms")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    private fun biosDir(context: Context): File
    {
        val dir = File(context.filesDir, "ps2-bios")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    private fun safeName(name: String): String = name.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_")

    /**
     * Keep a picked PlayStation 2 BIOS in internal storage, so every
     * later disc can boot ARMSX2 with it. Returns the stored file's
     * name, or null when the file could not be read or is not
     * BIOS-sized (4 MiB, the size the cores accept).
     */
    fun importBios(context: Context, uri: Uri, name: String): String?
    {
        val target = File(biosDir(context), safeName(name).lowercase())
        return try
        {
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output, 256 * 1024) }
            } ?: return null
            if (target.length() != BIOS_BYTES)
            {
                target.delete()
                return null
            }
            Log.i(TAG, "[ps2] imported BIOS ${target.name}")
            target.name
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[ps2] could not import BIOS from $uri", e)
            target.delete()
            null
        }
    }

    /** True when a BIOS was imported earlier. */
    fun hasBios(context: Context): Boolean =
        biosDir(context).listFiles()?.any { it.isFile } == true

    /**
     * Returns an absolute path to the disc image, or null with
     * [lastProblem] set. A `file://` URI is used in place; anything
     * else is copied into the cache. The imported BIOS files are then
     * staged beside the disc (under pcsx2/bios), where the host looks.
     */
    fun resolve(context: Context, uri: Uri): String?
    {
        lastProblem = ""
        val path = resolveDisc(context, uri)
        if (path == null)
        {
            if (lastProblem.isEmpty()) lastProblem = "Could not read that game file."
            return null
        }
        stageBios(context, File(path).parentFile)
        return path
    }

    private fun resolveDisc(context: Context, uri: Uri): String?
    {
        if (uri.scheme == "file")
        {
            val p = uri.path
            if (p != null && File(p).isFile) return p
        }
        if (uri.scheme == "content" || uri.scheme == "android.resource")
        {
            val document = DocumentFile.fromSingleUri(context, uri)
            val name = safeName(document?.name ?: uri.lastPathSegment?.substringAfterLast('/') ?: "disc.iso")
            val folder = File(cacheRoot(context), uri.toString().hashCode().toUInt().toString(16))
            if (!folder.isDirectory) folder.mkdirs()
            val target = File(folder, name)
            val expectedLength = document?.length() ?: -1L
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
                Log.i(TAG, "[ps2] materialised $uri to ${target.absolutePath} (${target.length()} bytes)")
                target.absolutePath
            }
            catch (e: Exception)
            {
                Log.e(TAG, "[ps2] could not materialise $uri", e)
                null
            }
        }
        val p = uri.path
        return if (p != null && File(p).isFile) p else null
    }

    /** Copy the imported BIOS files into [dir]/pcsx2/bios, where the host looks. */
    private fun stageBios(context: Context, dir: File?)
    {
        if (dir == null) return
        val imported = biosDir(context).listFiles()?.filter { it.isFile } ?: return
        if (imported.isEmpty()) return
        val target = File(File(dir, "pcsx2"), "bios")
        if (!target.isDirectory) target.mkdirs()
        imported.forEach { bios ->
            val staged = File(target, bios.name)
            if (staged.isFile && staged.length() == bios.length()) return@forEach
            try
            {
                bios.copyTo(staged, overwrite = true)
            }
            catch (e: Exception)
            {
                Log.w(TAG, "[ps2] could not stage BIOS ${bios.name}", e)
            }
        }
    }

    /**
     * The memory-card folder for this disc, under filesDir (never the
     * evictable cache), named for the disc's cached folder and name so
     * two discs never share cards. The cores create and keep their own
     * card files inside it.
     */
    fun savePathFor(context: Context, discPath: String): String?
    {
        val disc = File(discPath)
        val dir = File(context.filesDir, "ps2-saves")
        if (!dir.isDirectory) dir.mkdirs()
        val folder = File(dir, (disc.parentFile?.name ?: "disc") + "-" + disc.nameWithoutExtension)
        return try
        {
            if (!folder.isDirectory) folder.mkdirs()
            folder.absolutePath
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[ps2] no save folder at ${folder.absolutePath}", e)
            null
        }
    }
}
