package com.devin.opengameaccess.ui

import android.app.Activity
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
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
import com.devin.opengameaccess.AdapterCommandTitles
import com.devin.opengameaccess.AdapterLibraryData
import com.devin.opengameaccess.GbGameSession
import com.devin.opengameaccess.GbKeys
import com.devin.opengameaccess.PspGameSession
import com.devin.opengameaccess.PspKeys
import com.devin.opengameaccess.rememberPspFrameClock

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
    onOpenGames: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // ⛔ CAPABILITY IS ASKED, NEVER ASSUMED. The page once hardcoded "not compiled
    // into this test build" and kept saying it for weeks after the core landed.
    // Asked PER CORE since stage 3: the two cores share one build but not one
    // fate (a build can carry the GB host without the PSP subset).
    val gbAvailable = remember { GbGameSession.coreAvailable(context) }
    val pspAvailable = remember { PspGameSession.coreAvailable(context) }
    val available = gbAvailable || pspAvailable

    var pickedUri by remember { mutableStateOf<Uri?>(null) }
    var pickedName by remember { mutableStateOf("") }
    var pickedIsPsp by remember { mutableStateOf(false) }
    var running by remember { mutableStateOf(false) }
    var pspRunning by remember { mutableStateOf(false) }
    var starting by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var problem by remember { mutableStateOf("") }
    var liveMode by remember { mutableStateOf(LiveRegionMode.Polite) }

    // One running flag for the parts of the screen that only care THAT a
    // game is up (the header, the status line); the control groups below
    // still branch on which session it is.
    val anyRunning = running || pspRunning

    // The PSP frame clock: one emulated frame per Compose frame while a
    // PSP game runs. (GB's twin, rememberFrameClock, belongs to the GB
    // session's own wiring.)
    rememberPspFrameClock(pspRunning)

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
        // Routing is by extension, the Kotlin equivalent of the core's
        // own backend resolution: a PSP disc image goes to the PSP
        // session, everything else the launcher has ever handled goes
        // to the GB session (which still refuses what it cannot run).
        val extension = pickedName.substringAfterLast('.', "").lowercase()
        pickedIsPsp = PspGameSession.handlesExtension(extension)
        if (pickedIsPsp && !pspAvailable) {
            // Said at pick time, not left for a disabled Start button to
            // imply: on a build without the PSP core (armeabi-v7a) this
            // is the honest answer.
            say(
                "$pickedName selected. That is a PSP game, and this build has no PSP core.",
            )
            return@rememberLauncherForActivityResult
        }
        say("$pickedName selected. Press Start Game.")
    }

    // The status line, said once when the screen comes up. iOS's StatusBar is read
    // first on focus and always reflects state.
    remember(available) {
        if (available) {
            say("Open Game Access ready. Press Select Game to choose a game.", interrupt = true)
        } else {
            say(
                "This build has no game core compiled in, so games cannot be loaded.",
                interrupt = true,
            )
        }
        true
    }

    val extensions = remember(available) {
        val parts = mutableListOf<String>()
        if (gbAvailable) parts += GbGameSession.romExtensions()
        if (pspAvailable) parts += PspGameSession.romExtensions()
        if (parts.isEmpty()) {
            "Game Boy, Game Boy Color, Game Boy Advance, PSP"
        } else {
            parts.joinToString(", ") { it.uppercase() }
        }
    }

    // What the status line says about the session, in iOS's shape: an honest
    // one-liner rather than a restatement of the last announcement.
    val statusLine = when {
        anyRunning && pickedName.isNotEmpty() -> "Playing $pickedName"
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
            text = if (anyRunning) "Game screen" else "Choose a game to begin",
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

            // The library: every game that has a reader, with its notes.
            // Mirrors iOS, whose home screen IS this list (AdapterLibraryView);
            // here it sits one tap off the launcher because the launcher also
            // carries the running game's controls.
            Button(
                onClick = { onOpenGames() },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Games with readers", fontSize = 18.sp)
            }

            if (!running && !pspRunning) {
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
                        if (pickedIsPsp) {
                            // ⛔ ASYNCHRONOUS, UNLIKE THE GB START BELOW.
                            // A disc image can take a while to copy and
                            // load (PspGameSession's doc has the numbers),
                            // so Start says so and the session calls back;
                            // `starting` keeps a second press from racing
                            // the first load.
                            starting = true
                            say(
                                "Loading $pickedName. PSP games are large, " +
                                "so this can take a while."
                            )
                            PspGameSession.start(activity, uri) { error ->
                                starting = false
                                if (error != null) {
                                    problem = error
                                    say(
                                        "That game could not be started. $error",
                                        interrupt = true,
                                    )
                                } else {
                                    pspRunning = true
                                    problem = ""
                                    say("Game started.")
                                }
                            }
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
                    enabled = available && pickedUri != null && !starting &&
                        (if (pickedIsPsp) pspAvailable else gbAvailable),
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
            } else if (pspRunning) {
                // Running a PSP game: the pad the console actually has,
                // the reader commands the running game's adapter
                // supports (the group renders nothing for a game with
                // no adapter, or an adapter with an empty command set),
                // speech controls, and the whole-core savestates the
                // psp_* ABI exposes directly.
                PspDirectionalPad(PspGameSession)
                PspActionButtons(PspGameSession)
                PspSystemButtons(PspGameSession)
                PspReaderGroup(PspGameSession) { message -> say(message) }
                PspSpeechGroup(PspGameSession)
                PspStateGroup(PspGameSession) { message -> say(message) }
                QuitPspGame(PspGameSession) { pspRunning = false }
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

// ---- the PSP pad -------------------------------------------------------------
//
// The same grouping-by-activity as the GB pad above, with the buttons the
// PSP actually has. The reader group below renders the running game's
// adapter commands; a game without an adapter gets the pad, the speech
// controls and the savestates only.

/** Move up / left / right / down. */
@Composable
private fun PspDirectionalPad(session: PspGameSession) {
    GroupLabel("Directional pad")
    PspHoldButton("Up", session, PspKeys.UP)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        PspHoldButton("Left", session, PspKeys.LEFT, Modifier.weight(1f))
        PspHoldButton("Right", session, PspKeys.RIGHT, Modifier.weight(1f))
    }
    PspHoldButton("Down", session, PspKeys.DOWN)
}

/** The face buttons, named as the console names them. */
@Composable
private fun PspActionButtons(session: PspGameSession) {
    GroupLabel("Action buttons")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        PspHoldButton("Cross", session, PspKeys.CROSS, Modifier.weight(1f))
        PspHoldButton("Circle", session, PspKeys.CIRCLE, Modifier.weight(1f))
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        PspHoldButton("Triangle", session, PspKeys.TRIANGLE, Modifier.weight(1f))
        PspHoldButton("Square", session, PspKeys.SQUARE, Modifier.weight(1f))
    }
}

/** Start, Select, and the shoulders — the PSP always has shoulders. */
@Composable
private fun PspSystemButtons(session: PspGameSession) {
    GroupLabel("System buttons")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        PspHoldButton("Start", session, PspKeys.START, Modifier.weight(1f))
        PspHoldButton("Select", session, PspKeys.SELECT, Modifier.weight(1f))
        PspHoldButton("L", session, PspKeys.L, Modifier.weight(1f))
        PspHoldButton("R", session, PspKeys.R, Modifier.weight(1f))
    }
}

/**
 * Whole-core savestates, one slot per game. PSP-only on this screen: the
 * psp_* ABI exposes states directly, where the GB path's ride its reader
 * script instead. The spoken result says which of the two things happened
 * rather than leaving the press unanswered.
 */
@Composable
private fun PspStateGroup(session: PspGameSession, say: (String) -> Unit) {
    val context = LocalContext.current
    GroupLabel("Save states")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(
            onClick = {
                say(
                    if (session.saveState(context)) {
                        "State saved."
                    } else {
                        "That state could not be saved."
                    }
                )
            },
            colors = ButtonDefaults.outlinedButtonColors(),
            modifier = Modifier
                .weight(1f)
                .height(56.dp)
                .semantics { contentDescription = "Save state" },
        ) {
            Text("Save state", fontSize = 14.sp)
        }
        Button(
            onClick = {
                say(
                    if (session.loadState(context)) {
                        "State loaded."
                    } else {
                        "There is no saved state for this game yet."
                    }
                )
            },
            colors = ButtonDefaults.outlinedButtonColors(),
            modifier = Modifier
                .weight(1f)
                .height(56.dp)
                .semantics { contentDescription = "Load state" },
        ) {
            Text("Load state", fontSize = 14.sp)
        }
    }
}

/**
 * The reader's command buttons for the running PSP game: exactly the
 * commands its adapter supports on iOS (AdapterCommand.supported,
 * carried into AdapterLibraryData by the library generator), titled as
 * iOS titles them, and nothing else — a command an adapter does not
 * support is not invented here, and one it does is not dropped. A game
 * with no adapter, or an adapter whose supported set is empty (DBZ:
 * Another Road speaks on its own and answers the pad echoes), renders
 * no group at all.
 *
 * A refused command speaks the refusal — iOS's two lines, chosen the
 * way iOS chooses them (an adapter exists but is not ready, versus
 * there is no reader for this game). The adapter's own answer to a
 * taken command arrives through the speech bridge, not through [say].
 */
@Composable
private fun PspReaderGroup(session: PspGameSession, say: (String) -> Unit) {
    val game = AdapterLibraryData.games.firstOrNull { it.id == session.adapterId }
    val commands = game?.commands ?: emptyList()
    if (commands.isEmpty()) return
    GroupLabel("Reader")
    commands.chunked(2).forEach { rowCommands ->
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (command in rowCommands) {
                val title = AdapterCommandTitles.byId[command] ?: continue
                Button(
                    onClick = {
                        if (!session.sendAdapterCommand(command)) {
                            say(
                                if (session.adapterName.isNotEmpty()) {
                                    "Not ready yet."
                                } else {
                                    "No reader."
                                }
                            )
                        }
                    },
                    colors = ButtonDefaults.outlinedButtonColors(),
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp)
                        .semantics { contentDescription = title },
                ) {
                    Text(title, fontSize = 14.sp)
                }
            }
            if (rowCommands.size == 1) {
                Spacer(Modifier.weight(1f))
            }
        }
    }
}

/**
 * The controls that act on speech itself. PSP stops directly — the
 * bridge's stop plus the adapter queue's own stop (the session performs
 * both halves) — where the GB group above can only stop at its engine.
 */
@Composable
private fun PspSpeechGroup(session: PspGameSession) {
    val context = LocalContext.current
    GroupLabel("Speech controls")
    OutlinedButton(
        onClick = { session.stopSpeech(context) },
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
private fun QuitPspGame(session: PspGameSession, onQuit: () -> Unit) {
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
 *
 * ⛔ NOT A REAL OutlinedButton (stage 4 proof find, fixed here with the
 * stages 1–4 integration, 2026-10-23). This used to wrap the gesture in
 * an OutlinedButton whose own internal clickable consumed every touch
 * before the outer pointerInput detector could see an unconsumed press,
 * so the touch path NEVER fired: physical and injected taps did nothing
 * at all, and only the semantics click below (the screen-reader path)
 * ever reached the core. The defect was found on the PSP twin
 * ([PspHoldButton], same shape) during the stage-4 proof; the button is
 * therefore drawn by hand (a bordered Box) so the gesture detector owns
 * the touch, exactly as there. The semantics block is byte-for-byte the
 * one the OutlinedButton version carried — screen-reader activation
 * behaviour is unchanged; this only gives the touch path its presses.
 */
@Composable
private fun HoldButton(
    label: String,
    session: GbGameSession,
    bit: Int,
    modifier: Modifier = Modifier,
) {
    var held by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .border(
                1.dp,
                MaterialTheme.colors.onSurface.copy(alpha = 0.12f),
                RoundedCornerShape(4.dp),
            )
            .clip(RoundedCornerShape(4.dp))
            .background(
                if (held) MaterialTheme.colors.primary.copy(alpha = 0.12f)
                else MaterialTheme.colors.surface
            )
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
        contentAlignment = Alignment.Center,
    ) {
        Text(if (held) "$label (held)" else label, fontSize = 16.sp)
    }
}

/**
 * A PSP game button: [HoldButton]'s twin, typed to the PSP session.
 *
 * The two are separate functions rather than one generic one for the
 * reason the sessions are separate objects: the GB pad pushes a whole
 * key mask, the PSP pad latches one button per call, and merging the
 * primitives would blur exactly the difference that matters. The
 * interaction contract is identical — hold under a finger, short press
 * on the screen-reader click (see HoldButton's note for why both paths
 * exist).
 *
 * ⛔ NOT A REAL OutlinedButton (stage 4 proof find, 2026-10-23). This
 * used to wrap the gesture in an OutlinedButton whose own internal
 * clickable consumed every touch before the outer pointerInput
 * detector could see an unconsumed press, so the touch path NEVER
 * fired: physical and injected taps did nothing at all, and only the
 * semantics click below (the screen-reader path) ever reached the
 * core. Two games sat at their title screens through a whole proof
 * session before a held-press UI dump — the label never flipped to
 * "(held)" — isolated it. The button is therefore drawn by hand (a
 * bordered Box) so the gesture detector owns the touch. The GB
 * [HoldButton] had the same latent defect; it was given this same
 * shape with the stages 1–4 integration (2026-10-23) — see its own
 * note. The twins are the same shape again, deliberately.
 */
@Composable
private fun PspHoldButton(
    label: String,
    session: PspGameSession,
    button: Int,
    modifier: Modifier = Modifier,
) {
    var held by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .border(
                1.dp,
                MaterialTheme.colors.onSurface.copy(alpha = 0.12f),
                RoundedCornerShape(4.dp),
            )
            .clip(RoundedCornerShape(4.dp))
            .background(
                if (held) MaterialTheme.colors.primary.copy(alpha = 0.12f)
                else MaterialTheme.colors.surface
            )
            .pointerInput(button) {
                detectTapGestures(
                    onPress = {
                        held = true
                        session.pressButton(button)
                        tryAwaitRelease()
                        held = false
                        session.releaseButton(button)
                    },
                )
            }
            .semantics {
                contentDescription = label
                onClick(label = "press") {
                    session.pressButton(button)
                    session.releaseButtonSoon(button)
                    true
                }
            },
        contentAlignment = Alignment.Center,
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
