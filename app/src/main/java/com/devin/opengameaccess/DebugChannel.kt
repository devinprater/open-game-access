package com.devin.opengameaccess

import android.content.Context
import android.util.Log
import java.io.File

/**
 * Where the in-game debug channel's files live on Android
 * (docs/design/debug-channel.md): save a cmd.txt into this folder while a
 * game runs and the replies land in replies.log beside it.
 *
 * The app's external files folder, because it is the one place both the
 * app and a computer can reach without a permission prompt: over USB or
 * adb it is Android/data/<package>/files/oga-debug. When there is no
 * external storage the internal files folder is used instead, which adb
 * can still read with run-as.
 *
 * The DS, NES/SNES, Genesis and Game Boy sessions run on the shared core
 * and so share this folder. PSP still runs its own host on Android and
 * has no channel yet.
 */
object DebugChannel
{
    private const val TAG = "OgaDebugChannel"

    fun dir(context: Context): String
    {
        val root = context.getExternalFilesDir(null) ?: context.filesDir
        val dir = File(root, "oga-debug")
        if (!dir.isDirectory && !dir.mkdirs())
        {
            Log.w(TAG, "could not make ${dir.absolutePath}")
        }
        return dir.absolutePath
    }
}
