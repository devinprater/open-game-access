package com.devin.opengameaccess.ui

import android.content.Intent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Button
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.magnum.melonds.accessibility.AccessibilityScript

/**
 * The reading log: what the reader has spoken, in order — the twin
 * of iOS's ReadingLogView, reached from Settings' Troubleshooting
 * section, with the same Share.
 *
 * ⛔ THE CONTENT IS THE SPOKEN HISTORY, AND THE DIFFERENCE FROM iOS
 * IS STATED, NOT HIDDEN. iOS's log holds the script's silent console
 * trail (GameSession.debugLines, also written to a file there). The
 * Android hosts surface no such feed to the app layer — their debug
 * lines go to logcat only — so what this screen can show honestly
 * is everything the shared speech bridge actually said: script
 * lines and app lines, in the order they were spoken, capped by the
 * bridge at 400. For the player's troubleshooting question — what
 * did it say, and where did the lines stop — that is the same
 * evidence in the shape this platform has.
 *
 * Read on entry, not observed: the bridge is a plain object, not
 * Compose state, and leaving and re-entering the screen re-reads
 * it. Nothing here edits the log; it is a record, not a document.
 */
@Composable
fun ReadingLogScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val lines = remember { AccessibilityScript.spokenHistory() }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            text = "Reading log",
            fontSize = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back", fontSize = 18.sp)
        }

        Button(
            onClick = {
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_TEXT, lines.joinToString("\n"))
                }
                context.startActivity(Intent.createChooser(intent, "Share the log"))
            },
            enabled = lines.isNotEmpty(),
            modifier = Modifier
                .fillMaxWidth()
                .semantics { contentDescription = "Share the log" },
        ) {
            Text("Share the log", fontSize = 18.sp)
        }

        if (lines.isEmpty()) {
            Text(
                text = "Nothing yet. Start a game and this fills with the " +
                    "lines the reader speaks.",
                fontSize = 16.sp,
            )
        } else {
            for (line in lines) {
                Text(text = line, fontSize = 14.sp)
            }
        }
    }
}
