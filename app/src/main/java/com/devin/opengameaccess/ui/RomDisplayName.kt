package com.devin.opengameaccess.ui

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns

/**
 * The name a document provider gives a picked file — for display, and
 * for the extension checks that route and validate a pick.
 *
 * ⛔ NOT uri.lastPathSegment. A Storage Access Framework URI can end
 * in an opaque id (the msf:42 shape) that carries no extension at
 * all, while the provider's display name is the real file name
 * ("Pokemon Red.gb"). The ROM resolvers have always asked the
 * provider — DocumentFile.name makes this same query — so the
 * pickers ask here too: a supported ROM behind an opaque URI is not
 * refused for its URI's shape, and a PSP image is routed by its real
 * extension rather than by an id that has none.
 */
internal fun displayName(context: Context, uri: Uri): String {
    val queried = runCatching {
        context.contentResolver
            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
    }.getOrNull()
    return queried?.takeIf { it.isNotBlank() }
        ?: uri.lastPathSegment
        ?: uri.toString()
}
