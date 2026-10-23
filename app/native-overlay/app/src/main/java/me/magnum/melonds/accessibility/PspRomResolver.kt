package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Resolves a picked PSP disc image to a real filesystem path.
 *
 * ⛔ WHY A COPY AT ALL — THE PLAN'S R4, DECIDED FOR V1. `psp_load_rom`
 * opens the image through PPSSPP's filesystem VFS, which needs a real
 * path; a SAF content URI is not one. [GbRomResolver]'s answer is the
 * pragmatic one and it is copied here: materialise the document once,
 * keyed on the document's name and length (enough to tell "the player
 * opened this image again" from "the player replaced it with a
 * different pressing of the same name"), and reuse the copy after
 * that. A `file://` URI, or a URI that already carries a plain path,
 * is used in place with NO copy.
 *
 * ⛔ WHY NOT cacheDir (stage-4 proof find, 2026-10-23). The first
 * version materialised into `cacheDir/psproms`, and the system's
 * cache trimming evicted the 1.4–1.8 GB copies within about a minute
 * on the 6 GB proof device — killing boots mid-load, because cache
 * storage is exactly the storage Android is allowed to reclaim under
 * pressure, and a copy this size is the first thing to go. The copies
 * therefore live in app-specific NON-cache storage (`filesDir`,
 * beside the memstick root), which the system never evicts.
 *
 * ⛔ THE COST, STATED PLAINLY. Disc images are 0.5–2 GB, so the app
 * holds a SECOND FULL COPY of every PSP game the player opens — the
 * Dissidia CSO from the proofs is 1.37 GB — and because the copy is
 * no longer in the cache it now counts toward the app's storage size
 * and stays until the game is replaced, the app's data is cleared,
 * or the app is uninstalled. That is the v1 trade: storage permanence
 * for not reworking the core's VFS around content URIs. It is
 * recorded here and in the stage-3 notes so the number is a decision,
 * not a surprise.
 *
 * Saves do NOT live beside this copy (unlike GbRomResolver's .sav):
 * the memstick root is PspGameSession's, also under filesDir, and
 * savestates sit in their own filesDir directory — savedata and
 * disc images have different lifetimes and stay in different places.
 */
object PspRomResolver
{
    private const val TAG = "PspAccess"

    private fun storageDir(context: Context): File
    {
        // NOT context.cacheDir — see the header: cache copies this
        // size get evicted under the running session's feet.
        val dir = File(context.filesDir, "psproms")
        if (!dir.isDirectory) dir.mkdirs()
        return dir
    }

    /** A stable storage file name for a URI. */
    private fun storageFileFor(context: Context, uri: Uri, document: DocumentFile?): File
    {
        val name = document?.name ?: uri.lastPathSegment?.substringAfterLast('/') ?: "rom.bin"
        val safe = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        // Prefix with the URI hash so two different images that share a
        // filename do not overwrite each other (GbRomResolver's rule).
        return File(storageDir(context), "${uri.toString().hashCode().toUInt().toString(16)}-$safe")
    }

    /**
     * Returns an absolute path to the image's bytes, or null. A `file://`
     * URI is used in place; anything else is copied into app storage.
     * ⛔ THE COPY CAN TAKE A WHILE (gigabytes): call from a worker thread,
     * never the UI thread — PspGameSession.start does.
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
            val target = storageFileFor(context, uri, document)
            val expectedLength = document?.length() ?: -1L

            if (target.isFile && (expectedLength <= 0 || target.length() == expectedLength))
            {
                return target.absolutePath
            }

            return try
            {
                context.contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { output -> input.copyTo(output, 1024 * 1024) }
                } ?: return null
                Log.i(TAG, "materialised $uri to ${target.absolutePath} (${target.length()} bytes)")
                target.absolutePath
            }
            catch (e: Exception)
            {
                Log.e(TAG, "could not materialise $uri", e)
                null
            }
        }

        // A plain path in the URI itself.
        val path = uri.path
        return if (path != null && File(path).isFile) path else null
    }
}
