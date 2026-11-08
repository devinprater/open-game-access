package com.devin.opengameaccess.ui

import android.app.Activity
import android.net.Uri
import androidx.activity.compose.BackHandler
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
import androidx.compose.runtime.LaunchedEffect
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
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
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
import com.devin.opengameaccess.ControllerInput
import com.devin.opengameaccess.GbGameSession
import com.devin.opengameaccess.GbKeys
import com.devin.opengameaccess.PspAdapterCommands
import com.devin.opengameaccess.PspGameSession
import com.devin.opengameaccess.PspKeys
import com.devin.opengameaccess.rememberFrameClock
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
    showPicture: Boolean,
    announce: (String, Boolean) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenGames: () -> Unit,
    pendingRom: Uri?,
    onPendingRomHandled: () -> Unit,
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
    // ⛔ SEEDED FROM THE SESSIONS, NOT FROM FALSE. This screen leaves the
    // composition entirely when Settings or the games list opens, while
    // the session objects (singletons) keep the game running. Starting
    // these flags at false would resurrect the launcher as an idle
    // screen over a live game — control groups gone, and the Back
    // handlers below disabled, so the next Back would exit the Activity
    // instead of quitting the game. The sessions are the source of
    // truth; the flags below only track what THIS screen started.
    var running by remember { mutableStateOf(GbGameSession.isRunning) }
    var pspRunning by remember { mutableStateOf(PspGameSession.isRunning) }
    var starting by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var problem by remember { mutableStateOf("") }
    var liveMode by remember { mutableStateOf(LiveRegionMode.Polite) }

    // One running flag for the parts of the screen that only care THAT a
    // game is up (the header, the status line); the control groups below
    // still branch on which session it is.
    val anyRunning = running || pspRunning

    // The frame clocks: one emulated frame per Compose frame while a
    // game runs, one per session, both composed HERE at the screen's
    // top level — outside the game picture's switch gate, so turning
    // the picture off never stops the GB game advancing. (This branch
    // composed only the PSP twin at first, which left a running GB
    // game frozen on its first frame whenever it was started from
    // here: nothing pumped its session.)
    rememberFrameClock(running)
    rememberPspFrameClock(pspRunning)

    // ⛔ BACK IN A SESSION IS QUIT, NEVER AN EXIT (input parity, stage 1).
    // iOS leaves a game through an explicit Quit with a VoiceOver escape
    // action; the Android system Back is the same gesture wearing system
    // clothes, so it performs the same leave-game flow the Quit buttons
    // below perform — session stop, flag cleared, launcher state back —
    // instead of Android's default, which finished the whole Activity
    // mid-game. The two handlers are mutually exclusive (a GB and a PSP
    // session never run at once). MainActivity's handler covers the
    // Settings / games-list level; at the idle launcher nothing here is
    // enabled, so Back still leaves the app from the front door.
    BackHandler(enabled = running) {
        session.stop()
        running = false
    }
    BackHandler(enabled = pspRunning) {
        PspGameSession.stop()
        pspRunning = false
    }

    // ⛔ CONTROLLER CONNECT / DISCONNECT IS SPOKEN (input parity, stage 1).
    // iOS publishes the connected controller's name and the player hears
    // the change; here the name lives on ControllerInput (Compose state,
    // written by its InputManager listener) and this effect turns a
    // CHANGE into a status line — which is the launcher's voice, live
    // region and bridge fallback both. The remembered previous value
    // starts at whatever is already connected, so a pad paired before
    // the app opened is not "announced" at first composition; a pad
    // that arrives or leaves mid-session is.
    val controllerName = ControllerInput.connectedName
    val controllerConnected = controllerName != null
    var previousControllerName by remember { mutableStateOf(controllerName) }

    fun say(message: String, interrupt: Boolean = false) {
        status = message
        liveMode = if (interrupt) LiveRegionMode.Assertive else LiveRegionMode.Polite
        announce(message, interrupt)
    }

    // A picked file becomes the pending game: the name the screen
    // shows, and which session it belongs to. Shared by the Select
    // Game picker below and by a library page's Load button (the
    // pendingRom hand-off further down): one routing, one set of
    // spoken answers, whichever door the file came through.
    fun acceptPickedRom(uri: Uri) {
        pickedUri = uri
        // The provider's display name, not the URI's last segment:
        // an opaque SAF id (msf:42) carries no extension to route
        // by, so a PSP image behind one would fall through to the
        // GB session and be refused there.
        pickedName = displayName(context, uri)
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
            return
        }
        say("$pickedName selected. Press Start Game.")
    }

    // The speaking half of the controller watch above (kept below say's
    // declaration — a local fun is only visible from where it is
    // declared, and the compiler enforces it).
    LaunchedEffect(controllerName) {
        val previous = previousControllerName
        previousControllerName = controllerName
        if (previous == controllerName) return@LaunchedEffect
        if (controllerName != null) {
            say("Controller connected: $controllerName")
        } else {
            say("Controller disconnected.")
        }
    }

    // ⛔ THE MAGIC-TAP EQUIVALENT (input parity, stage 1). On iOS a
    // VoiceOver magic tap on the game screen asks the reader where the
    // player is (GameSession.performMagicTap, answered only by readers
    // that support it). TalkBack has no magic tap, so the game-screen
    // element below carries the same query as a custom action. It asks
    // exactly what the on-screen routes ask: on PSP the adapter's Where
    // am I, with the reader group's own refusal lines; on Game Boy the
    // script hotkey this screen's own Where am I button taps. A game
    // whose reader does not answer it says so, in the controller
    // layer's line, rather than the action going silent.
    fun whereAmI() {
        when {
            pspRunning -> {
                val commands = AdapterLibraryData.games
                    .firstOrNull { it.id == PspGameSession.adapterId }?.commands
                    ?: emptyList()
                if (PspAdapterCommands.WHERE_AM_I in commands) {
                    if (!PspGameSession.sendAdapterCommand(PspAdapterCommands.WHERE_AM_I)) {
                        say(
                            if (PspGameSession.adapterName.isNotEmpty()) {
                                "Not ready yet."
                            } else {
                                "No reader."
                            }
                        )
                    }
                } else {
                    say("No reader command there for this game.")
                }
            }
            running -> session.tapHotkey('E')
            else -> Unit
        }
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
        acceptPickedRom(uri)
    }

    // Start the pending game: the Start Game button's whole behaviour,
    // extracted so a game loaded from its own library page starts
    // exactly the way a game picked here starts — same routing, same
    // spoken lines. The availability guards below duplicate the Start
    // button's enabled state because the hand-off calls this directly;
    // on the button's own path they can never fire (the button is
    // disabled in those states), so nothing about it changes.
    fun startPickedGame() {
        val uri = pickedUri
        if (uri == null) {
            say("Select a game first.")
            return
        }
        if (starting) {
            // A load is already in flight (its button is disabled for
            // the same reason); the session's generation counter would
            // supersede it, but the in-flight load's own spoken result
            // is the answer the player is waiting for.
            return
        }
        val activity = context as? Activity
        if (activity == null) {
            say("That game could not be started.")
            return
        }
        if (pickedIsPsp) {
            if (!pspAvailable) {
                // acceptPickedRom already said this at pick time.
                return
            }
            // ⛔ ASYNCHRONOUS, UNLIKE THE GB START BELOW. A disc image
            // can take a while to copy and load (PspGameSession's doc
            // has the numbers), so Start says so and the session calls
            // back; `starting` keeps a second press from racing the
            // first load.
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
            return
        }
        if (!gbAvailable) {
            say("That is a Game Boy game, and this build has no Game Boy core.")
            return
        }
        val error = session.start(activity, uri)
        if (error != null) {
            problem = error
            say("That game could not be started. $error", interrupt = true)
            return
        }
        running = true
        problem = ""
        say(
            "Game started. The reader will speak one startup line, " +
                "then only game content."
        )
    }

    // ⛔ A LIBRARY PAGE'S LOAD LANDS HERE. The game page's Load button
    // opens its own console-filtered picker; a pick is handed up to
    // the Activity (pendingRom) and the games screen closes, which
    // composes this screen with the hand-off waiting. It is consumed
    // once, then run through the SAME accept + start path as a file
    // picked here — so the routing, the core checks and the spoken
    // answers are this screen's own, and a game started from its page
    // is indistinguishable from one started from this screen. While a
    // game is already running the pick only becomes the pending
    // selection (the player quits first, as on iOS, where the library
    // sits behind the running game too); idle, it starts at once —
    // that is the page's whole promise.
    LaunchedEffect(pendingRom) {
        val uri = pendingRom ?: return@LaunchedEffect
        onPendingRomHandled()
        acceptPickedRom(uri)
        // ⛔ THE START DECISION ASKS THE SESSIONS, NOT THIS SCREEN'S
        // FLAGS. The flags are seeded from the sessions at first
        // composition, but the hand-off is consumed in that same
        // first composition; the sessions are the source of truth, so
        // a pick handed up while a game runs can never start a second
        // session over the live one — it stays the pending selection
        // until the player quits, exactly as the comment above says.
        if (!GbGameSession.isRunning && !PspGameSession.isRunning) {
            startPickedGame()
        }
    }

    // The status line, said once when the screen comes up. iOS's StatusBar is read
    // first on focus and always reflects state. Returning from Settings
    // or the games list mid-game recomposes this screen; the ready line
    // is for the idle launcher only, never spoken over a running game.
    remember(available) {
        if (GbGameSession.isRunning || PspGameSession.isRunning) {
            // A game is already running (see the seeded flags above).
        } else if (available) {
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
                    // The magic-tap equivalent (see whereAmI above):
                    // offered only while a game runs, as on iOS, where
                    // the gesture exists only on the game screen.
                    if (anyRunning) {
                        customActions = listOf(
                            CustomAccessibilityAction(label = "Where am I") {
                                whereAmI()
                                true
                            },
                        )
                    }
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

        // The transient line: the last thing say() was given. The status
        // line above only ever shows the derived session state, so a
        // one-off message (a controller connecting at the idle launcher,
        // where MainActivity's bridge fallback stays silent because no
        // session is running) would otherwise be written to state and
        // never heard. This region observes that state, so the message
        // is announced when it lands.
        if (status.isNotEmpty()) {
            Text(
                text = status,
                fontSize = 14.sp,
                color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp, vertical = 4.dp)
                    .semantics {
                        liveRegion = liveMode
                        contentDescription = status
                    },
            )
        }

        // The game picture: iOS's FrameImage, sitting where its ScreenView
        // sits — under the screen's spoken description, above the controls.
        // ⛔ COMPOSED ONLY WHILE A GAME RUNS AND THE SWITCH IS ON. Off, the
        // composable does not exist, so no frame is ever copied: iOS keeps
        // the picture out of its frame path the same way when its switch is
        // off. The image itself is decorative (GamePicture says why); the
        // "Game screen" element above remains the one spoken stop.
        if (anyRunning && showPicture) {
            GamePicture(
                fetchFrame = {
                    if (PspGameSession.isRunning) {
                        PspGameSession.currentFrame()
                    } else {
                        GbGameSession.currentFrame()
                    }
                },
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 8.dp),
            )
        }

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
                    onClick = { startPickedGame() },
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
                // The on-screen pad hides while a controller is connected
                // (iOS does the same): the physical pad replaces it, and
                // the reader / speech / state / quit groups stay.
                if (!controllerConnected) {
                    PspDirectionalPad(PspGameSession)
                    PspActionButtons(PspGameSession)
                    PspSystemButtons(PspGameSession)
                }
                PspReaderGroup(PspGameSession) { message -> say(message) }
                PspSpeechGroup(PspGameSession)
                PspStateGroup(PspGameSession) { message -> say(message) }
                QuitPspGame(PspGameSession) { pspRunning = false }
            } else {
                // Running: the controls, grouped by what the player is doing.
                // The pad groups hide while a controller is connected, as
                // on iOS; the reader groups below are not the pad and stay.
                if (!controllerConnected) {
                    DirectionalPad(session)
                    ActionButtons(session, pickedName)
                    SystemButtons(session, pickedName)
                }
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
                        try {
                            tryAwaitRelease()
                        } finally {
                            // Finally, not sequence: if this composable
                            // is removed mid-press (the pad hides when a
                            // controller connects), the await is cancelled
                            // and without the finally the emulated button
                            // would stay down in the running game.
                            held = false
                            session.releaseKey(bit)
                        }
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
                        try {
                            tryAwaitRelease()
                        } finally {
                            // Finally, not sequence — see HoldButton: a
                            // mid-press removal cancels the await, and the
                            // button must still come up in the game.
                            held = false
                            session.releaseButton(button)
                        }
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
