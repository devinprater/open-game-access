package com.devin.opengameaccess.ui

import android.net.Uri
import android.webkit.MimeTypeMap
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
// Material 2, as everywhere in this app (see LauncherScreen's note).
import androidx.compose.material.Button
import androidx.compose.material.MaterialTheme
import androidx.compose.material.OutlinedButton
import androidx.compose.material.Switch
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
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
import com.devin.opengameaccess.AdapterGameInfo
import com.devin.opengameaccess.AdapterLibraryData
import com.devin.opengameaccess.GameSettings

/**
 * The game library: every game that has a reader, mirroring iOS's
 * AdapterLibraryView (Sources/OpenGameAccess/AdapterLibrary.swift).
 *
 * ⛔ THE LIST IS THE REGISTRY'S, READ FROM GENERATED DATA. iOS reads the Core
 * adapter registry live through poke_adapter_library_*; this screen reads
 * AdapterLibraryData instead — GENERATED from the same registry objects by
 * tools/android-library-gen.cpp and checked for staleness by the launcher
 * contract test. (The DS host itself links the registry — that is how its
 * adapters select by cartridge code — but a Compose screen still reads the
 * generated data, not a native query.) A reader that ships without its
 * library entry is therefore impossible on either platform.
 *
 * ⛔ THE LOAD BUTTONS, AND HOW THEIR FILTERS ARE KEPT HONEST. Each page
 * carries a Load button in iOS's place (after the notes) and under iOS's
 * label. Game Boy and PSP pages open the file picker filtered to the
 * console's files and hand the pick to the launcher (the screen's
 * onLoadRom callback), which starts it through its own accept + start
 * path — the same routing and spoken answers as its Select Game flow.
 * Nintendo DS pages carry their own button ("Load this game"): the DS
 * half of the flow landed with the DS session (parity stage 4), and it
 * hands the GAME to the launcher (onLoadGame / MainActivity's
 * dsLoadRequest), which opens its picker and routes the pick into the
 * DS session by extension, like any other pick.
 *
 * Android's picker filters by MIME TYPE, NOT BY EXTENSION, and has
 * no equivalent of iOS's UTType-by-extension filter
 * (AdapterGame.contentTypes). So a page's picker launches with the
 * console's MIME types plus the generic binary type (which is what
 * the system tags most ROM files as, their extensions being unknown
 * to it), and the picked file's extension is checked against the
 * console's own list on return: a wrong file is refused in words,
 * on this page, rather than routed into a session that cannot use
 * it. The guarantee iOS gets at the picker's front door is kept at
 * this end instead (see consoleExtensions / pickerMimeTypes).
 *
 * For a console this build cannot run yet, the button still exists
 * — every page has it, as on iOS — and its press speaks the plain
 * line saying what THIS build does with that console (see
 * [platformLine]) instead of opening a picker onto a dead end.
 */
@Composable
fun AdapterGamesScreen(
    onBack: () -> Unit,
    onLoadRom: (Uri) -> Unit,
    onLoadGame: (AdapterGameInfo) -> Unit,
    announce: (String) -> Unit = {},
    modifier: Modifier = Modifier,
) {
    var selected by remember { mutableStateOf<AdapterGameInfo?>(null) }
    var settingsFor by remember { mutableStateOf<AdapterGameInfo?>(null) }

    // The system Back walks this screen's own levels before it may leave
    // the screen (input parity, stage 1): each early-return level below
    // registers the handler for its own onBack lambda — the same calls the
    // levels' Back buttons make — and only that level is composed at a
    // time, so exactly one of these stands. At the list itself neither
    // stands, and MainActivity's handler takes the press back to the
    // launcher. Before this, system Back from a game page or the per-game
    // settings left the whole screen (or the app) instead of stepping
    // back one level.
    val settingsGame = settingsFor
    if (settingsGame != null) {
        BackHandler { settingsFor = null }
        PerGameSettingsScreen(
            game = settingsGame,
            onBack = { settingsFor = null },
            announce = announce,
            modifier = modifier,
        )
        return
    }

    val game = selected
    if (game != null) {
        BackHandler { selected = null }
        AdapterGamePage(
            game = game,
            onBack = { selected = null },
            onOpenSettings = { settingsFor = game },
            onLoadGame = onLoadGame,
            onLoadRom = onLoadRom,
            modifier = modifier,
        )
        return
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            text = "Games with readers",
            fontSize = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back", fontSize = 18.sp)
        }

        Text(
            text = "Each game opens notes on what its reader speaks, the " +
                "controls it adds, and a Load Game button.",
            fontSize = 16.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        for (entry in AdapterLibraryData.games) {
            OutlinedButton(
                onClick = { selected = entry },
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics {
                        // One stop per game, spoken as iOS speaks its rows:
                        // the name, then the coverage line.
                        contentDescription = if (entry.coverage.isEmpty()) {
                            entry.name
                        } else {
                            "${entry.name}. ${entry.coverage}."
                        }
                    },
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 6.dp),
                    verticalArrangement = Arrangement.spacedBy(3.dp),
                ) {
                    Text(entry.name, fontSize = 18.sp)
                    if (entry.coverage.isNotEmpty()) {
                        Text(
                            entry.coverage,
                            fontSize = 14.sp,
                            color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
                        )
                    }
                }
            }
        }

        Text(
            text = "A game that is not listed has no reader: no speech and no cues.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )
    }
}

/** One game's page: what the reader does, its Load button, and what this build does with it. */
@Composable
private fun AdapterGamePage(
    game: AdapterGameInfo,
    onBack: () -> Unit,
    onOpenSettings: () -> Unit,
    onLoadRom: (Uri) -> Unit,
    onLoadGame: (AdapterGameInfo) -> Unit,
    modifier: Modifier = Modifier,
) {
    // Whether THIS build can start the game's console today. The Load
    // button exists either way (see the file header); this decides
    // whether its press opens the picker or speaks the honest line.
    val canRun = game.console == "gb" || game.console == "psp"
    val context = LocalContext.current

    // The page's spoken answers (a cancelled picker, a wrong file, an
    // un-runnable console): rendered under the Load button as a live
    // region, the launcher status line's pattern, so a screen reader
    // announces them rather than leaving the press unanswered.
    var loadNote by remember { mutableStateOf("") }
    // ⛔ EVERY ATTEMPT ANNOUNCES, EVEN A REPEATED ONE. Assigning the
    // same note twice (Load pressed again for an un-runnable console,
    // a second cancelled picker) changes no state, so the live region
    // would stay silent on the repeat. The nonce counts attempts; the
    // note's Text is keyed on it below, so each attempt re-enters
    // composition and is announced, while the text itself stays the
    // display copy.
    var loadNoteNonce by remember { mutableStateOf(0) }
    fun note(text: String) {
        loadNote = text
        loadNoteNonce++
    }

    val pickForGame = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) {
            // A cancel is SPOKEN rather than left silent, the same
            // contract the launcher's own picker keeps.
            note("No game selected.")
            return@rememberLauncherForActivityResult
        }
        // The provider's display name, not the URI's last segment: an
        // opaque SAF id (msf:42) carries no extension to check.
        val name = displayName(context, uri)
        val extension = name.substringAfterLast('.', "").lowercase()
        if (extension in consoleExtensions(game.console)) {
            // The hand-off: the Activity closes this screen and the
            // launcher runs the pick through its own accept + start
            // path, so everything it says and checks is the
            // launcher's own.
            onLoadRom(uri)
        } else {
            note(
                "That file is not a ${consoleShortName(game.console)} game. " +
                    formatLine(game.console)
            )
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text(
            text = game.name,
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        // ⛔ AT THE TOP OF THE PAGE, ALWAYS THERE (the player's own
        // design, 2026-10-24): iOS puts this control at the top right
        // of the game page; this page is a scrolling column, so the
        // top slot under the game's name is the same place by the only
        // measure that matters to a TalkBack user — it is the first
        // control on the page, in the same place for every game,
        // whether or not that game has settings yet. The panel itself
        // says in words when a game has none. Nothing opens on its own
        // and nothing interrupts starting the game.
        Button(
            onClick = { onOpenSettings() },
            modifier = Modifier
                .fillMaxWidth()
                .semantics { contentDescription = "Per Game Settings for ${game.name}" },
        ) {
            Text("Per Game Settings", fontSize = 18.sp)
        }

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back to games", fontSize = 18.sp)
        }

        // The DS half of the parity program's load flow (see the file
        // doc): a DS page can start its game from here. The button
        // hands the game to the launcher, which opens its picker — the
        // player still chooses the ROM file, and the launcher routes
        // it into the DS session by extension, as it does for a pick
        // made on its own screen. Other consoles' pages have no such
        // button until their halves of the flow land.
        if (game.console == "nds") {
            Button(
                onClick = { onLoadGame(game) },
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics { contentDescription = "Load ${game.name}" },
            ) {
                Text("Load this game", fontSize = 18.sp)
            }
        }

        Text(
            text = consoleName(game.console),
            fontSize = 16.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        if (game.coverage.isNotEmpty()) {
            Text(text = game.coverage, fontSize = 17.sp, fontWeight = FontWeight.Bold)
        }

        versionLine(game.code)?.let { line ->
            Text(text = line, fontSize = 14.sp)
        }

        Text(
            text = game.notes.ifEmpty { "No notes have been written for this game yet." },
            fontSize = 16.sp,
        )

        // The page's one action, in iOS's place (after the notes) and
        // under iOS's label, spoken as "Load <game>" the way iOS
        // speaks it. For a console this build runs it opens the
        // picker filtered to the console's file types; for one it
        // cannot run yet, the press answers with the platform line
        // as the note below — the button never promises a flow that
        // does not exist.
        //
        // ⛔ NOT ON DS PAGES: a DS page already carries its own Load
        // button above ("Load this game", the DS stage's flow through
        // the launcher). Two Load buttons on one page would be two
        // doors to the same room; each console keeps the flow its own
        // stage built.
        if (game.console != "nds") {
            Button(
                onClick = {
                    if (canRun) {
                        pickForGame.launch(pickerMimeTypes(game.console))
                    } else {
                        note(platformLine(game.console))
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics { contentDescription = "Load ${game.name}" },
            ) {
                Text("Load game", fontSize = 18.sp)
            }
        }

        if (loadNote.isNotEmpty()) {
            // Keyed on the attempt nonce (see note() above): a repeated
            // identical answer is a fresh element, so the live region
            // announces it again instead of treating it as no change.
            key(loadNoteNonce) {
                Text(
                    text = loadNote,
                    fontSize = 16.sp,
                    modifier = Modifier.semantics {
                        liveRegion = LiveRegionMode.Polite
                        contentDescription = loadNote
                    },
                )
            }
        }

        Text(
            text = platformLine(game.console),
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold,
        )

        Text(
            text = formatLine(game.console),
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )
    }
}

/**
 * One game's Per Game Settings panel: its overrides, reached from the
 * "Per Game Settings" control at the top of its page
 * (docs/design/per-game-settings.md). The twin of iOS's
 * PerGameSettingsView — same model, same words where the platforms
 * allow.
 *
 * WHAT THE PLAYER SEES. One row per setting this game's reader can
 * actually use — a game never lists a setting it cannot use. Each
 * switch shows the value the game will use (its override when one
 * exists, else the app default from Settings), the line under it says
 * WHICH of those it is and what the app default is, and a game
 * carrying an override gets a "Use app default" button that clears
 * it. Every control's TalkBack description states its provenance, so
 * the fact does not depend on reading the line under the switch.
 *
 * A game whose reader uses no per-game setting still gets this panel
 * (the control sits on every game page, always in the same place) and
 * the panel says so in words rather than opening empty.
 */
@Composable
private fun PerGameSettingsScreen(
    game: AdapterGameInfo,
    onBack: () -> Unit,
    announce: (String) -> Unit = {},
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    // Bumped after every store write so the rows re-read the
    // overrides — the store is SharedPreferences, not Compose state.
    var revision by remember { mutableStateOf(0) }
    val settings = remember(revision) { GameSettings.supportedSettings(game.id) }

    // ⛔ EVERY CHANGE HERE IS SPOKEN, AS ON iOS (parity stage 3).
    // iOS's panel announces each change itself ("Battle cue sounds:
    // on, for Birth by Sleep." / "...now uses the app default: on."),
    // because with no screen an unspoken change is an invisible one.
    // The line goes two ways, the launcher's pattern: [lastChange]
    // feeds the live-region line below — the games list is usually
    // browsed with no game running, when the speech bridge does not
    // exist — and [announce] carries it to the bridge while one does.
    var lastChange by remember { mutableStateOf("") }
    fun changed(line: String) {
        lastChange = line
        announce(line)
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text(
            text = "Per Game Settings",
            fontSize = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back to ${game.name}", fontSize = 18.sp)
        }

        Text(
            text = "These settings apply to ${game.name} only. A setting with no " +
                "override uses the app default from Settings; changing it here " +
                "overrides it for this game. Settings apply when the game starts.",
            fontSize = 16.sp,
        )

        // The change line: a live region, spoken as changes happen
        // (see the note at the top of this screen).
        if (lastChange.isNotEmpty()) {
            Text(
                text = lastChange,
                fontSize = 14.sp,
                color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
                modifier = Modifier.semantics {
                    liveRegion = LiveRegionMode.Polite
                    contentDescription = lastChange
                },
            )
        }

        if (settings.isEmpty()) {
            Text(
                text = "This game has no per-game settings yet. The settings that " +
                    "apply to every game live in Settings.",
                fontSize = 16.sp,
            )
        }

        for (setting in settings) {
            val global = GameSettings.globalDefault(context, setting)
            val overrideValue = GameSettings.override(context, game.id, setting)
            val effective = overrideValue ?: global
            val provenance = if (overrideValue != null) {
                "Set for this game. The app default is ${onOff(global)}."
            } else {
                "Using the app default: ${onOff(global)}. Change the app default in Settings."
            }

            Text(
                text = setting.title,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { heading() },
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics {
                        contentDescription =
                            "${setting.title} for ${game.name}: ${onOff(effective)}. $provenance"
                    },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(text = setting.title, fontSize = 16.sp)
                Switch(
                    checked = effective,
                    onCheckedChange = { newValue ->
                        // Writing always writes an OVERRIDE — even
                        // when the new value equals the default, the
                        // player made a choice for this game, and the
                        // provenance line will say so until they clear
                        // it with "Use app default".
                        GameSettings.setOverride(context, game.id, setting, newValue)
                        revision++
                        // iOS's own words for this change.
                        changed("${setting.title}: ${onOff(newValue)}, for ${game.name}.")
                    },
                )
            }
            Text(
                text = provenance,
                fontSize = 14.sp,
                color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
            )
            if (overrideValue != null) {
                OutlinedButton(
                    onClick = {
                        GameSettings.setOverride(context, game.id, setting, null)
                        revision++
                        // iOS's own words for this change.
                        changed(
                            "${setting.title} for ${game.name} now uses the app " +
                                "default: ${onOff(global)}."
                        )
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .semantics {
                            contentDescription = "Use app default for ${setting.title}. " +
                                "Removes this game's override, so it follows the app " +
                                "default again: ${onOff(global)}."
                        },
                ) {
                    Text("Use app default", fontSize = 16.sp)
                }
            }
        }
    }
}

/** "on" / "off" for a switch value, in the words the panels speak. */
private fun onOff(value: Boolean): String = if (value) "on" else "off"

// ---- presentation lines, mirrored from AdapterLibrary.swift -----------------

/** "Nintendo DS", "PlayStation Portable", and so on. */
private fun consoleName(console: String): String = when (console) {
    "nds" -> "Nintendo DS"
    "gb" -> "Game Boy, Game Boy Color and Game Boy Advance"
    "psp" -> "PlayStation Portable"
    "snes" -> "Super Nintendo"
    "nes" -> "Nintendo Entertainment System"
    "n64" -> "Nintendo 64"
    else -> "this console"
}

/** The one line naming the file types the console uses. */
private fun formatLine(console: String): String = when (console) {
    "nds" -> "Game files for this console are Nintendo DS .nds files."
    "gb" -> "Game files for this console are .gba, .gbc and .gb files."
    "psp" -> "Game files for this console are PSP .iso, .cso and .pbp files."
    "snes" -> "Game files for this console are .sfc, .smc, .fig and .swc files."
    "nes" -> "Game files for this console are .nes, .fds and .unf files."
    else -> "Game files for this console use the file types this console uses."
}

/**
 * The supported-version line, when the registry names a code worth showing.
 * A CRC is a checksum, not a player-facing code, so it is worded as one.
 */
private fun versionLine(code: String): String? = when {
    code.startsWith("crc:") -> "Verified against the ROM with checksum ${code.drop(4)}."
    code.isEmpty() -> null
    else -> "Supported game code: $code."
}

/**
 * What THIS Android build does with the game's console, stated plainly.
 *
 * ⛔ MEASURED AGAINST THE BUILD, NOT THE REGISTRY. The registry lists every
 * reader the project has. This build runs Game Boy games (its Select Game
 * flow and Lua reader set) inside a melonDS shell, and PSP games too: the
 * PPSSPP core is compiled in, the launcher starts them, and — since
 * stage 4 — their readers are in the build with it, attached by the same
 * DISC_IDs iOS dispatches on. What a PSP game's speech covers is whatever
 * its adapter reports (the coverage line above, from the adapter itself);
 * this line only claims the reader is present. Nintendo DS joined them
 * with the parity program's DS stage: the shared Core DS host is compiled
 * in, the launcher starts DS games, and its readers — the native adapters
 * and the Pokémon script — are in the build with it. The SNES reader is
 * not compiled into this build at all. A page that implied otherwise
 * would send a blind player hunting for speech that cannot happen.
 */
private fun platformLine(console: String): String = when (console) {
    "gb" -> "This Android build runs these games: use Load game on this "
        "page, or pick one with Select Game on the launcher."
    "nds" -> "Nintendo DS games run in this Android build, and this "
        "game's reader is in the build too: use Load this game above, or "
        "pick the game with Select Game on the launcher."
    "psp" -> "PlayStation Portable games run in this Android build, and this " +
        "game's reader is in the build too: use Load game on this page, " +
        "or pick one with Select Game on the launcher."
    "snes" -> "Super Nintendo games cannot run in this Android build yet. " +
        "This reader is in the iOS app today."
    else -> "This console's games cannot run in this Android build yet."
}

// ---- the Load button's filter -----------------------------------------------

/**
 * The file extensions a console's games use: the extension lists of
 * iOS's AdapterGame.contentTypes, verbatim. A page's pick is checked
 * against this list on return — Android's picker filters by MIME
 * type, not by extension, so the console's guarantee is kept at this
 * end of the picker (see the file header).
 */
private fun consoleExtensions(console: String): Set<String> = when (console) {
    "nds" -> setOf("nds")
    "gb" -> setOf("gba", "gbc", "gb")
    "psp" -> setOf("iso", "cso", "pbp")
    "snes" -> setOf("sfc", "smc", "fig", "swc")
    "nes" -> setOf("nes", "fds", "unf")
    "n64" -> setOf("z64", "n64", "v64")
    else -> emptySet()
}

/**
 * The picker's filter for a console: the MIME types its extensions
 * resolve to, plus the generic binary type.
 *
 * ⛔ THE GENERIC TYPE RIDES ALONG ON PURPOSE. Most of these
 * extensions are unknown to the system's MIME table, so most real
 * ROM files are tagged application/octet-stream by their providers;
 * filtering on the registered types alone would hide the very files
 * the picker exists to offer. With the generic type included, the
 * picker hides everything carrying a known non-game type, and the
 * extension check on return (consoleExtensions) keeps the console's
 * promise exactly.
 */
private fun pickerMimeTypes(console: String): Array<String> {
    val mimeMap = MimeTypeMap.getSingleton()
    val types = consoleExtensions(console)
        .mapNotNull { mimeMap.getMimeTypeFromExtension(it) }
        .toMutableSet()
    types += "application/octet-stream"
    return types.toTypedArray()
}

/** "Game Boy" / "PlayStation Portable" — for the wrong-file line. */
private fun consoleShortName(console: String): String = when (console) {
    "gb" -> "Game Boy"
    "psp" -> "PlayStation Portable"
    else -> consoleName(console)
}
