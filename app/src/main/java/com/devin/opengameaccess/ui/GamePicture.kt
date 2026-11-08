package com.devin.opengameaccess.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The game picture: the running game's framebuffer on the session
 * screen — iOS's FrameImage with the FrameStore behind it
 * (RootView/GameSession), in Compose's shape.
 *
 * ⛔ NOT AN ACCESSIBILITY ELEMENT, DELIBERATELY, AS ON IOS. iOS makes
 * its screen a picture of a game being described to the player by the
 * reader, and making it focusable would add a stop to every swipe;
 * the launcher's own "Game screen" element already carries the spoken
 * description and the session's state. So the image is decorative
 * (null content description) — for the sighted person looking at the
 * phone, never a TalkBack stop.
 *
 * ⛔ COMPOSED ONLY WHEN THE PICTURE IS SWITCHED ON. The caller gates
 * composition on the Settings switch (DisplaySettings), so a player
 * who has it off — the default while a screen reader's touch
 * exploration is on, exactly iOS's default — never pays the copy at
 * all: no poll, no readback, nothing in the frame path.
 *
 * The poll runs at half the display cadence (~30 Hz). The picture is
 * there to be looked at, not to track play, and the PSP hand-off is a
 * GPU readback copy besides; half rate halves that cost. The Bitmap
 * is reused across copies (recreated only when the frame's size
 * changes, e.g. a Game Boy game in a Super Game Boy border size), and
 * the state carries a copy counter so a mutated-in-place Bitmap still
 * tells Compose a new frame landed.
 */
@Composable
fun GamePicture(
    fetchFrame: () -> Triple<IntArray, Int, Int>?,
    modifier: Modifier = Modifier,
) {
    // The caller's lambda re-reads which session is running on every
    // recomposition; the poll must call the LATEST one, not the one
    // captured when the effect started.
    val fetch by rememberUpdatedState(fetchFrame)
    var picture by remember { mutableStateOf<Pair<Bitmap, Long>?>(null) }

    LaunchedEffect(Unit) {
        var target: Bitmap? = null
        // ⛔ THE BYTE BUFFER IS CACHED ACROSS POLLS, like the Bitmap.
        // Allocating it per frame churned a fresh 522,240-byte buffer
        // for every PSP frame — ~15.7 MB/s of garbage at this poll
        // rate, on the main-thread coroutine. It is reallocated only
        // when a frame needs more room than it has, and rewound
        // before every refill.
        var byteBuffer: ByteBuffer? = null
        var ticks = 0L
        var copies = 0L
        while (true) {
            withFrameNanos { }
            if (++ticks % 2L != 0L) continue
            val frame = fetch() ?: continue
            val (pixels, width, height) = frame
            if (width <= 0 || height <= 0 || pixels.size < width * height) continue
            if (target == null || target.width != width || target.height != height) {
                target = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            }
            // ⛔ BYTE ORDER. Both sessions hand back RGBA byte-order
            // words (see copyGbFrame / nativePspCopyFrame): their
            // memory IS an RGBA byte stream, which is what an
            // ARGB_8888 Bitmap reads from a buffer. Bitmap.setPixels
            // would want the channels packed the other way and swap
            // red and blue, so the copy goes through a buffer instead.
            val needed = width * height * 4
            if (byteBuffer == null || byteBuffer!!.capacity() < needed) {
                byteBuffer = ByteBuffer.allocate(needed)
                    .order(ByteOrder.LITTLE_ENDIAN)
            }
            val bytes = byteBuffer!!
            bytes.position(0)
            bytes.asIntBuffer().put(pixels, 0, width * height)
            bytes.position(0)
            target.copyPixelsFromBuffer(bytes)
            picture = target to ++copies
        }
    }

    val current = picture?.first
    if (current != null) {
        Image(
            bitmap = current.asImageBitmap(),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            filterQuality = FilterQuality.None,
            modifier = modifier
                .fillMaxWidth()
                .aspectRatio(current.width.toFloat() / current.height.toFloat()),
        )
    }
}
