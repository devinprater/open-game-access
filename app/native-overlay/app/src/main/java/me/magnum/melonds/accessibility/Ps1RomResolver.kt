package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Turns a SAF content URI into a real filesystem path the PlayStation
 * host can open. A sibling of [GenesisRomResolver], with three
 * PlayStation differences:
 *
 * ⛔ THE DISC KEEPS ITS OWN NAME. A .cue names its .bin tracks by file
 * name, and the core finds them (and a BIOS) in the disc's own folder.
 * So each picked document is copied into its own folder under the
 * cache (keyed by the URI's hash, so two discs that share a name never
 * collide) under its original name, never a prefixed one.
 *
 * ⛔ A .cue, .m3u OR .ccd NEEDS FILES THIS APP CANNOT REACH. The system
 * picker grants access to the ONE document picked, not its folder, so
 * the tracks a .cue lists are out of reach unless the URI is a plain
 * file path. [resolve] checks every listed file and, when one is
 * missing, refuses in words ([lastProblem]) — the player is told to use
 * a .chd image, which is one file per disc — rather than letting the
 * core fail on a half-present disc.
 *
 * ⛔ THE BIOS IS IMPORTED ONCE AND STAGED BESIDE EVERY DISC. A real
 * PlayStation BIOS (scph5501.bin and the like) is optional: without
 * one the core's HLE BIOS runs. [importBios] keeps a picked BIOS in
 * internal storage, and [resolve] copies it into the disc's folder,
 * where the host looks for it.
 */
object Ps1RomResolver
{
    private const val TAG = "Ps1Access"

    /** The reason the last [resolve] refused, in words, or "". */
    @Volatile
    var lastProblem: String = ""
        private set

    /** BIOS file names the core recognises: scph followed by digits, .bin. */
    private val BIOS_NAME = Regex("(?i)^(scph|psxonpsp|ps1_rom|openbios)[0-9a-z_-]*\\.bin$")

    /** True when a picked file name looks like a PlayStation BIOS. */
    fun isBiosName(name: String): Boolean = BIOS_NAME.matches(name)

    private fun cacheRoot(context: Context): File
    {
        val dir = File(context.cacheDir, "ps1roms")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    private fun biosDir(context: Context): File
    {
        val dir = File(context.filesDir, "ps1-bios")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    private fun safeName(name: String): String = name.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_")

    /**
     * Keep a picked PlayStation BIOS in internal storage, so every
     * later disc boots with it. Returns the stored file's name, or null
     * when the file could not be read or is not BIOS-sized (512 KiB, the
     * size the core accepts).
     */
    fun importBios(context: Context, uri: Uri, name: String): String?
    {
        val target = File(biosDir(context), safeName(name).lowercase())
        return try
        {
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } ?: return null
            if (target.length() != 512L * 1024L)
            {
                target.delete()
                return null
            }
            Log.i(TAG, "[ps1] imported BIOS ${target.name}")
            target.name
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[ps1] could not import BIOS from $uri", e)
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
     * else is copied into the cache, then checked for the files it
     * lists, then given the imported BIOS beside it.
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
        val missing = missingListedFiles(File(path))
        if (missing.isNotEmpty())
        {
            lastProblem = "That file lists other files (${missing.first()}" +
                (if (missing.size > 1) " and ${missing.size - 1} more" else "") +
                ") that this app cannot open from a single pick. Use a .chd image of the disc instead."
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
            val name = safeName(document?.name ?: uri.lastPathSegment?.substringAfterLast('/') ?: "disc.chd")
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
                Log.i(TAG, "[ps1] materialised $uri to ${target.absolutePath} (${target.length()} bytes)")
                target.absolutePath
            }
            catch (e: Exception)
            {
                Log.e(TAG, "[ps1] could not materialise $uri", e)
                null
            }
        }
        val p = uri.path
        return if (p != null && File(p).isFile) p else null
    }

    /**
     * The files an index image names that are not beside it: a .cue's
     * FILE lines, an .m3u's entries, a .ccd's .img twin. Empty for a
     * .chd and for anything complete.
     */
    fun missingListedFiles(disc: File): List<String>
    {
        val dir = disc.parentFile ?: return emptyList()
        val listed: List<String> = when (disc.extension.lowercase())
        {
            "cue" -> disc.readLines().mapNotNull { line ->
                val t = line.trim()
                if (!t.uppercase().startsWith("FILE")) return@mapNotNull null
                val quoted = Regex("\"([^\"]+)\"").find(t)?.groupValues?.get(1)
                quoted ?: t.split(Regex("\\s+")).getOrNull(1)
            }
            "m3u" -> disc.readLines().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }
            "ccd" -> listOf(disc.nameWithoutExtension + ".img")
            else -> emptyList()
        }
        return listed.map { it.substringAfterLast('/').substringAfterLast('\\') }
            .filter { !File(dir, it).isFile }
    }

    /** Copy the imported BIOS files into [dir], where the host looks. */
    private fun stageBios(context: Context, dir: File?)
    {
        if (dir == null) return
        biosDir(context).listFiles()?.filter { it.isFile }?.forEach { bios ->
            val staged = File(dir, bios.name)
            if (staged.isFile && staged.length() == bios.length()) return@forEach
            try
            {
                bios.copyTo(staged, overwrite = true)
            }
            catch (e: Exception)
            {
                Log.w(TAG, "[ps1] could not stage BIOS ${bios.name}", e)
            }
        }
    }

    /**
     * The memory card path for this disc, under filesDir (never the
     * evictable cache), named for the disc's cached folder and name so
     * two discs never share a card. A disc used in place keeps its card
     * beside it, the arrangement emulator frontends traditionally use.
     */
    fun savePathFor(context: Context, discPath: String): String?
    {
        val disc = File(discPath)
        val card = if (!disc.absolutePath.startsWith(context.cacheDir.absolutePath))
        {
            File(disc.parentFile, disc.nameWithoutExtension + ".mcd")
        }
        else
        {
            val dir = File(context.filesDir, "ps1-saves")
            if (!dir.isDirectory) dir.mkdirs()
            File(dir, (disc.parentFile?.name ?: "disc") + "-" + disc.nameWithoutExtension + ".mcd")
        }
        return try
        {
            if (!card.isFile) card.createNewFile()
            card.absolutePath
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[ps1] no memory card file at ${card.absolutePath}", e)
            null
        }
    }
}
