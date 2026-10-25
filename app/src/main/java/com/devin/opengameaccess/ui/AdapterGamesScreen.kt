package com.devin.opengameaccess.ui

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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
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
 * adapter registry live through poke_adapter_library_*; the Android layer
 * never links that registry (its readers are Lua), so it reads
 * AdapterLibraryData instead — which is GENERATED from the same registry
 * objects by tools/android-library-gen.cpp and checked for staleness by the
 * launcher contract test. A reader that ships without its library entry is
 * therefore impossible on either platform.
 *
 * ⛔ THE ONE DELIBERATE DIFFERENCE FROM iOS: THE LOAD BUTTON. iOS's game page
 * loads the game filtered to that console's files. The Android launcher's
 * Select Game flow starts Game Boy and PSP games from its own screen, and
 * there is no per-game filtered picker behind this list, so a per-game Load
 * button here would promise a flow that does not exist. Each page instead
 * ends with a plain line saying what THIS build does with that console
 * (see [platformLine]).
 */
@Composable
fun AdapterGamesScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var selected by remember { mutableStateOf<AdapterGameInfo?>(null) }
    var settingsFor by remember { mutableStateOf<AdapterGameInfo?>(null) }

    val settingsGame = settingsFor
    if (settingsGame != null) {
        PerGameSettingsScreen(
            game = settingsGame,
            onBack = { settingsFor = null },
            modifier = modifier,
        )
        return
    }

    val game = selected
    if (game != null) {
        AdapterGamePage(
            game = game,
            onBack = { selected = null },
            onOpenSettings = { settingsFor = game },
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
            text = "Each game opens notes on what its reader speaks and the " +
                "controls it adds.",
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

/** One game's page: what the reader does, and what this build does with it. */
@Composable
private fun AdapterGamePage(
    game: AdapterGameInfo,
    onBack: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
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
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    // Bumped after every store write so the rows re-read the
    // overrides — the store is SharedPreferences, not Compose state.
    var revision by remember { mutableStateOf(0) }
    val settings = remember(revision) { GameSettings.supportedSettings(game.id) }

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
 * this line only claims the reader is present. The DS and SNES readers
 * are not compiled into this build at all. A page that implied otherwise
 * would send a blind player hunting for speech that cannot happen.
 */
private fun platformLine(console: String): String = when (console) {
    "gb" -> "This Android build runs these games: pick one with Select Game " +
        "on the launcher."
    "nds" -> "This game's reader is not in the Android build yet; its speech " +
        "is in the iOS app today."
    "psp" -> "PlayStation Portable games run in this Android build, and this " +
        "game's reader is in the build too: pick one with Select Game on " +
        "the launcher."
    "snes" -> "Super Nintendo games cannot run in this Android build yet. " +
        "This reader is in the iOS app today."
    else -> "This console's games cannot run in this Android build yet."
}
