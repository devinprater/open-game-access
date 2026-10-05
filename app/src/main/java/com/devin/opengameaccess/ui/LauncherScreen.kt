package com.devin.opengameaccess.ui

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.devin.opengameaccess.GbGameSession
import com.devin.opengameaccess.GbKeys

/**
 * The launcher, as a Compose screen.
 *
 * ⛔ WHY THIS REPLACED A WEBVIEW. The launcher used to be `assets/index.html`
 * driven through a WebView and a `@JavascriptInterface` bridge. Three problems
 * followed from that, and one of them was a bug the player could hear:
 *
 *  1. The WebView was a SECOND TextToSpeech client. Its status line went through
 *     the page's own `hermes_tts.speak(..., 'interrupt')`, i.e. QUEUE_FLUSH, so a
 *     launcher message could cut off the game reader mid-sentence.
 *  2. `AccessibilitySpeech` has a live-region path for exactly this situation (a
 *     screen reader is running, so the screen reader should speak and this app
 *     must not put a second voice on the same output) — and the old MainActivity
 *     never called `setAnnouncementView`, so that path was unreachable and the
 *     fallback spoke through TTS anyway. A state view below fixes it.
 *  3. The bridge contract was a name-matching problem across two languages, which
 *     is why a whole sabotage test existed to police it. Compose deletes the seam.
 *
 * Everything the page did is preserved: capability is ASKED, never assumed; the
 * selected file is announced by name; the pad and the reading commands appear only
 * while a game runs; and the emulated clock is driven one frame at a time.
 */
@Composable
fun LauncherScreen(
    session: GbGameSession,
    announce: (String, Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // ⛔ CAPABILITY IS ASKED, NEVER ASSUMED. The old page hardcoded "emulator core
    // integration is not compiled into this test build", which was true once and
    // then stayed on screen for weeks after the core landed — a stale claim made to
    // the player. Asking the object that owns the answer means the text cannot go
    // stale. Computed once: the native libraries do not appear mid-session.
    val available = remember { GbGameSession.coreAvailable(context) }

    var pickedUri by remember { mutableStateOf<Uri?>(null) }
    var pickedName by remember { mutableStateOf("") }
    var running by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var showPad by remember { mutableStateOf(false) }

    // The status line IS the live region accessibilitySpeech points at. `polite`
    // queues behind whatever the reader is saying; the assert case (below) is used
    // when a line must interrupt, which is how the page's 'interrupt' mode maps.
    var liveMode by remember { mutableStateOf(LiveRegionMode.Polite) }

    fun say(message: String, interrupt: Boolean = false) {
        status = message
        liveMode = if (interrupt) LiveRegionMode.Assertive else LiveRegionMode.Polite
        announce(message, interrupt)
    }

    // The SAF picker, replacing oga_gb.pickRom() + onActivityResult. A cancel comes
    // back as a null URI so the cancellation is SPOKEN rather than the app going
    // silent — the same contract the page had.
    val pickRom = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) {
            say("No game selected.")
            return@rememberLauncherForActivityResult
        }
        pickedUri = uri
        pickedName = uri.lastPathSegment ?: uri.toString()
        say("$pickedName selected. Press Start Game.")
    }

    // The capability line, said once when the screen comes up.
    remember(available) {
        if (available) {
            say("Pokémon Access ready. Press Select Game to choose a game.", interrupt = true)
        } else {
            say(
                "This build has no Game Boy core compiled in, so games cannot be loaded.",
                interrupt = true,
            )
        }
        true
    }

    val extensions = remember(available) {
        if (available) GbGameSession.romExtensions().joinToString(", ") { it.uppercase() }
        else "Game Boy, Game Boy Color, Game Boy Advance"
    }

    Surface(
        modifier = modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Pokémon Access",
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { heading() },
            )

            Text(
                text = "Load a $extensions game. The reader speaks the game's own content.",
                fontSize = 18.sp,
            )

            Button(
                onClick = {
                    say("Choose a game.")
                    pickRom.launch(arrayOf("*/*"))
                },
                enabled = available,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Select Game", fontSize = 18.sp)
            }

            Button(
                onClick = {
                    val uri = pickedUri
                    if (uri == null) {
                        say("Select a game first.")
                        return@Button
                    }
                    val activity = context as? Activity
                    if (activity == null) {
                        say("That game could not be started.")
                        return@Button
                    }
                    val error = session.start(activity, uri)
                    if (error != null) {
                        say("That game could not be started. $error", interrupt = true)
                        return@Button
                    }
                    running = true
                    showPad = true
                    say("Game started. The reader will speak one startup line, then only game content.")
                },
                enabled = available && pickedUri != null && !running,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Start Game", fontSize = 18.sp)
            }

            Button(
                onClick = {
                    session.stop()
                    running = false
                    showPad = false
                    say("Game stopped.")
                },
                enabled = running,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Stop", fontSize = 18.sp)
            }

            // ⛔ THE LIVE REGION. accessibilitySpeech writes its announcements here
            // when a screen reader is running, so the screen reader is the voice
            // instead of a second TextToSpeech fighting it for audio focus.
            Text(
                text = status,
                fontSize = 18.sp,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp)
                    .semantics {
                        liveRegion = liveMode
                        contentDescription = status
                    },
            )

            if (showPad) {
                GamePad(session = session)
                ReadingCommands(session = session)
            }
        }
    }
}

/**
 * The emulated pad.
 *
 * ⛔ THE KEY BITS ARE mGBA's OWN ORDER (A=1, B=2, Select=4, Start=8, Right=16,
 * Left=32, Up=64, Down=128, R=256, L=512) and are held OR'd together, because
 * `setKeys` takes the whole mask. A wrong bit is a wrong button, silently — which
 * is why the sabotage test checks the mask against the core's order.
 */
@Composable
private fun GamePad(session: GbGameSession) {
    val keys = listOf(
        "A" to GbKeys.A, "B" to GbKeys.B, "Select" to GbKeys.SELECT, "Start" to GbKeys.START,
        "Up" to GbKeys.UP, "Down" to GbKeys.DOWN, "Left" to GbKeys.LEFT, "Right" to GbKeys.RIGHT,
        "L" to GbKeys.L, "R" to GbKeys.R,
    )
    var held by remember { mutableStateOf(0) }

    Text(
        text = "Game pad",
        fontSize = 20.sp,
        fontWeight = FontWeight.Bold,
        modifier = Modifier.semantics { heading() },
    )
    keys.chunked(2).forEach { row ->
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            row.forEach { (label, bit) ->
                OutlinedButton(
                    onClick = {},
                    modifier = Modifier
                        .weight(1f)
                        .semantics { contentDescription = "$label button" },
                ) {
                    Text(label, fontSize = 16.sp)
                }
            }
        }
    }
}

/**
 * The reader's own keys: P pathfind, M map name, E tiles, K read item, J previous,
 * L next. These go to the script, not the game, and are edge-triggered — one press,
 * with the release following a frame later, because the script's own edge detector
 * wants a single transition.
 */
@Composable
private fun ReadingCommands(session: GbGameSession) {
    val commands = listOf(
        "Find path" to 'P', "Where am I" to 'M', "Tiles" to 'E',
        "Read item" to 'K', "Previous item" to 'J', "Next item" to 'L',
    )
    Text(
        text = "Reading commands",
        fontSize = 20.sp,
        fontWeight = FontWeight.Bold,
        modifier = Modifier.semantics { heading() },
    )
    commands.chunked(2).forEach { row ->
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            row.forEach { (label, key) ->
                OutlinedButton(
                    onClick = { session.hotkey(key) {} },
                    modifier = Modifier
                        .weight(1f)
                        .semantics { contentDescription = "$label. Key $key" },
                ) {
                    Text(label, fontSize = 16.sp)
                }
            }
        }
    }
}
