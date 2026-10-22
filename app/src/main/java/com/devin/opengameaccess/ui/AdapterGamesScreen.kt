package com.devin.opengameaccess.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
// Material 2, as everywhere in this app (see LauncherScreen's note).
import androidx.compose.material.Button
import androidx.compose.material.MaterialTheme
import androidx.compose.material.OutlinedButton
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.devin.opengameaccess.AdapterGameInfo
import com.devin.opengameaccess.AdapterLibraryData

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
 * Select Game flow only starts Game Boy games today, and the other consoles'
 * readers are not in this build at all, so a per-game Load button here would
 * promise what the build cannot do. Each page instead ends with a plain line
 * saying what THIS build does with that console (see [platformLine]).
 */
@Composable
fun AdapterGamesScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var selected by remember { mutableStateOf<AdapterGameInfo?>(null) }

    val game = selected
    if (game != null) {
        AdapterGamePage(game = game, onBack = { selected = null }, modifier = modifier)
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
 * reader the project has; this app runs Game Boy games (its Select Game flow
 * and Lua reader set) inside a melonDS shell, and the native Core readers —
 * DS, PSP, SNES — are not compiled into it. PSP is additionally a standing
 * decision, not an oversight (TODO: "PPSSPP on Android (DEFERRED 2026-10-03)").
 * A page that implied otherwise would send a blind player hunting for a Load
 * button that cannot exist.
 */
private fun platformLine(console: String): String = when (console) {
    "gb" -> "This Android build runs these games: pick one with Select Game " +
        "on the launcher."
    "nds" -> "This game's reader is not in the Android build yet; its speech " +
        "is in the iOS app today."
    "psp" -> "PlayStation Portable games cannot run in this Android build: " +
        "there is no PSP core in it. This reader is in the iOS app today."
    "snes" -> "Super Nintendo games cannot run in this Android build yet. " +
        "This reader is in the iOS app today."
    else -> "This console's games cannot run in this Android build yet."
}
