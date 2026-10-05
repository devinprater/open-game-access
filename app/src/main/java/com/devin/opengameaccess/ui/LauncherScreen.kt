package com.devin.opengameaccess.ui

import android.app.Activity
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
// ⛔ MATERIAL 2, NOT MATERIAL 3. The frontend's `libs.compose.material3` points at
// `androidx.compose.material3.adaptive:adaptive` (adaptive layouts), NOT the
// component library; the app's toolkit is `androidx.compose.material` (Material 2).
// Importing material3.* does not resolve and the module fails to compile.
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.MaterialTheme
import androidx.compose.material.OutlinedButton
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.devin.opengameaccess.GbGameSession
import com.devin.opengameaccess.GbKeys

/**
 * The launcher, mirroring the iOS RootView.
 *
 * ⛔ PARITY IS THE POINT, AND THE GROUPING IS THE USEFUL PART. iOS regroups the
 * controls by WHAT THE PLAYER IS DOING rather than by which keys exist, because a
 * flat grid of fifteen buttons makes every one equally far away when you navigate
 * by swipe. Each group is a labelled container so a screen reader announces the
 * group name on entry. That shape is reproduced here, with `heading()` on each
 * group label so the groups are reachable by heading navigation as well.
 *
 * ⛔ WHY THIS REPLACED A WEBVIEW. The launcher was `assets/index.html` in a WebView,
 * which was a SECOND TextToSpeech client (its status line used QUEUE_FLUSH and cut
 * the reader off mid-sentence), and whose `setAnnouncementView` hand-off the old
 * Activity never made -- so the screen reader and TTS fought over one output.
 */
@Composable
fun LauncherScreen(
    session: GbGameSession,
    announce: (String, Boolean) -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // ⛔ CAPABILITY IS ASKED, NEVER ASSUMED. The page once hardcoded "not compiled
    // into this test build" and kept saying it for weeks after the core landed.
    val available = remember { GbGameSession.coreAvailable(context) }

    var pickedUri by remember { mutableStateOf<Uri?>(null) }
    var pickedName by remember { mutableStateOf("") }
    var running by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var problem by remember { mutableStateOf("") }
    var liveMode by remember { mutableStateOf(LiveRegionMode.Polite) }

    fun say(message: String, interrupt: Boolean = false) {
        status = message
        liveMode = if (interrupt) LiveRegionMode.Assertive else LiveRegionMode.Polite
        announce(message, interrupt)
    }

    val pickRom = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) {
            // A cancel is SPOKEN rather than left silent, the same contract the
            // WebView page had.
            say("No game selected.")
            return@rememberLauncherForActivityResult
        }
        pickedUri = uri
        pickedName = uri.lastPathSegment ?: uri.toString()
        problem = ""
        say("$pickedName selected. Press Start Game.")
    }

    // The status line, said once when the screen comes up. iOS's StatusBar is read
    // first on focus and always reflects state.
    remember(available) {
        if (available) {
            say("Open Game Access ready. Press Select Game to choose a game.", interrupt = true)
        } else {
            say(
                "This build has no Game Boy core compiled in, so games cannot be loaded.",
                interrupt = true,
            )
        }
        true
    }

    val extensions = remember(available) {
        if (available) {
            GbGameSession.romExtensions().joinToString(", ") { it.uppercase() }
        } else {
            "Game Boy, Game Boy Color, Game Boy Advance"
        }
    }

    // What the status line says about the session, in iOS's shape: an honest
    // one-liner rather than a restatement of the last announcement.
    val statusLine = when {
        running && pickedName.isNotEmpty() -> "Playing $pickedName"
        pickedUri != null && pickedName.isNotEmpty() -> "Ready: $pickedName"
        problem.isNotEmpty() -> "Problem: $problem"
        else -> "No game loaded"
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState()),
    ) {
        // The game screen. ⛔ ONE LABELLED ELEMENT, NOT A STOP PER PIXEL -- iOS
        // deliberately makes its screen a single element with a value describing
        // state, because the picture is described by the reader, and making it
        // focusable per-detail just adds stops to every swipe.
        Text(
            text = if (running) "Game screen" else "Choose a game to begin",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 12.dp)
                .semantics {
                    contentDescription = "Game screen. $statusLine"
                },
        )

        // The status line: a live region, so the running screen reader announces it
        // (see the note in MainActivity about who owns the voice).
        Text(
            text = statusLine,
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 4.dp)
                .semantics {
                    liveRegion = liveMode
                    contentDescription = statusLine
                },
        )

        Column(
            modifier = Modifier.padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Open Game Access",
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { heading() },
            )

            Text(
                text = "Load a $extensions game. The reader speaks the game's own content.",
                fontSize = 16.sp,
                color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
            )

            Button(
                onClick = { onOpenSettings() },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Settings", fontSize = 18.sp)
            }

            if (!running) {
                // Idle: pick, then start -- in the order they are needed.
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
                            problem = error
                            say("That game could not be started. $error", interrupt = true)
                            return@Button
                        }
                        running = true
                        problem = ""
                        say(
                            "Game started. The reader will speak one startup line, " +
                            "then only game content."
                        )
                    },
                    enabled = available && pickedUri != null,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Start Game", fontSize = 18.sp)
                }

                if (problem.isNotEmpty()) {
                    Text(
                        text = problem,
                        fontSize = 14.sp,
                        color = Color(0xFFFF6B6B),
                        modifier = Modifier.semantics { contentDescription = "Error: $problem" },
                    )
                }
            } else {
                // Running: the controls, grouped by what the player is doing.
                DirectionalPad(session)
                ActionButtons(session, pickedName)
                SystemButtons(session, pickedName)
                PathFindingGroup(session)
                ReadingGroup(session)
                SpeechGroup(session)
                QuitGame(session) { running = false }
            }
        }
    }
}

// ---- groups ------------------------------------------------------------------

/** Move up / left / right / down. */
@Composable
private fun DirectionalPad(session: GbGameSession) {
    GroupLabel("Directional pad")
    HoldButton("Up", session, GbKeys.UP)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        HoldButton("Left", session, GbKeys.LEFT, Modifier.weight(1f))
        HoldButton("Right", session, GbKeys.RIGHT, Modifier.weight(1f))
    }
    HoldButton("Down", session, GbKeys.DOWN)
}

/** The face buttons. Game Boy answers A/B; GBA adds nothing here (L/R are shoulders). */
@Composable
private fun ActionButtons(session: GbGameSession, romName: String) {
    GroupLabel("Action buttons")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        HoldButton("A", session, GbKeys.A, Modifier.weight(1f))
        HoldButton("B", session, GbKeys.B, Modifier.weight(1f))
    }
}

/**
 * Start, Select, and the shoulders.
 *
 * ⛔ THE SHOULDERS ARE HIDDEN WHEN THE CONSOLE HAS NONE. The original Game Boy and
 * the Game Boy Color have no L/R, so showing them there would be controls that do
 * nothing -- iOS keys the same decision on `system.hasShoulders`. The extension is
 * the only signal the launcher has, so it is used and stated.
 */
@Composable
private fun SystemButtons(session: GbGameSession, romName: String) {
    GroupLabel("System buttons")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        HoldButton("Start", session, GbKeys.START, Modifier.weight(1f))
        HoldButton("Select", session, GbKeys.SELECT, Modifier.weight(1f))
        if (romName.lowercase().endsWith(".gba")) {
            HoldButton("L", session, GbKeys.L, Modifier.weight(1f))
            HoldButton("R", session, GbKeys.R, Modifier.weight(1f))
        }
    }
}

/** Choosing a destination and being guided to it. Game Boy answers P and E. */
@Composable
private fun PathFindingGroup(session: GbGameSession) {
    GroupLabel("Path finding and map")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        HotkeyButton("Find path", 'P', session, Modifier.weight(1f))
        HotkeyButton("Where am I", 'E', session, Modifier.weight(1f))
    }
}

/** Reading a list: items on the map, or options on a menu. Game Boy answers K/J/L. */
@Composable
private fun ReadingGroup(session: GbGameSession) {
    GroupLabel("Reading items and lists")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        HotkeyButton("Read item", 'K', session, Modifier.weight(1f))
        HotkeyButton("Previous", 'J', session, Modifier.weight(1f))
        HotkeyButton("Next", 'L', session, Modifier.weight(1f))
    }
}

/**
 * The controls that act on speech itself.
 *
 * ⛔ ON GAME BOY THERE IS NO SCRIPT KEY THAT STOPS SPEECH -- R moves the camera --
 * so this stops at the engine and drops background lines until the player asks for
 * something. iOS makes exactly this distinction (`system.hasDirectSpeechStop`),
 * because a two-finger tap only ends the current utterance while the script keeps
 * producing lines.
 */
@Composable
private fun SpeechGroup(session: GbGameSession) {
    GroupLabel("Speech controls")
    OutlinedButton(
        onClick = { session.stopSpeech() },
        modifier = Modifier
            .fillMaxWidth()
            .height(56.dp)
            .semantics {
                contentDescription = "Stop speech"
            },
    ) {
        Text("Stop speech", fontSize = 16.sp)
    }
}

@Composable
private fun QuitGame(session: GbGameSession, onQuit: () -> Unit) {
    OutlinedButton(
        onClick = {
            session.stop()
            onQuit()
        },
        modifier = Modifier
            .fillMaxWidth()
            .height(56.dp)
            .semantics { contentDescription = "Quit game" },
    ) {
        Text("Quit game", fontSize = 16.sp)
    }
}

// ---- primitives --------------------------------------------------------------

/**
 * A group's name, as a heading.
 *
 * ⛔ A HEADING, NOT DECORATION. iOS wraps each group in a labelled accessibility
 * container so the name is announced on entry; on Android a `heading()` gives the
 * same orientation plus heading navigation, so a player can jump group to group
 * instead of swiping through every button in order.
 */
@Composable
private fun GroupLabel(text: String) {
    Text(
        text = text,
        fontSize = 18.sp,
        fontWeight = FontWeight.Bold,
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp)
            .semantics {
                heading()
                isTraversalGroup = true
            },
    )
}

/**
 * A game button: held while the finger is down, and ACTIVATABLE BY A SCREEN READER.
 *
 * ⛔ THE TWO PATHS ARE NOT REDUNDANT, AND THE SECOND WAS MISSING BEFORE.
 *  - `pointerInput` + `detectTapGestures(onPress=...)` keeps the button down for as
 *    long as the finger is, because these games read held directions every frame --
 *    a tap-only button cannot walk.
 *  - `semantics { onClick { ... } }` is the CLICK action, which is the ONLY thing a
 *    screen reader can invoke. With an empty `onClick` and only the gesture, every
 *    pad button did NOTHING under TalkBack. A screen reader cannot express "hold",
 *    so the click path does a short press-then-release -- exactly what iOS's
 *    `.accessibilityAction { tap() }` does with its 0.12 s release.
 */
@Composable
private fun HoldButton(
    label: String,
    session: GbGameSession,
    bit: Int,
    modifier: Modifier = Modifier,
) {
    var held by remember { mutableStateOf(false) }

    OutlinedButton(
        onClick = { /* unused: the click action below is the screen-reader path */ },
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .pointerInput(bit) {
                detectTapGestures(
                    onPress = {
                        held = true
                        session.pressKey(bit)
                        tryAwaitRelease()
                        held = false
                        session.releaseKey(bit)
                    },
                )
            }
            .semantics {
                contentDescription = label
                onClick(label = "press") {
                    // Short press then release: the core sees the press on the next
                    // frame, and a screen reader has no way to hold.
                    session.pressKey(bit)
                    session.releaseKeySoon(bit)
                    true
                }
            },
    ) {
        Text(if (held) "$label (held)" else label, fontSize = 16.sp)
    }
}

/** A script hotkey. Edge-triggered: the script wants the frame the key went down. */
@Composable
private fun HotkeyButton(
    label: String,
    key: Char,
    session: GbGameSession,
    modifier: Modifier = Modifier,
) {
    Button(
        onClick = { session.tapHotkey(key) },
        colors = ButtonDefaults.outlinedButtonColors(),
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .semantics { contentDescription = label },
    ) {
        Text(label, fontSize = 14.sp)
    }
}
