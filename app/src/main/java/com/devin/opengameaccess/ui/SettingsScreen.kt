package com.devin.opengameaccess.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.Alignment
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Button
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Switch
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Settings, mirroring iOS's SettingsView as far as the Android path can honestly go.
 *
 * ⛔ WHAT IS DELIBERATELY ABSENT, AND WHY. A screen picker (iOS shows
 * it only for two-screen consoles; Android has none yet), the
 * show-game-picture switch (there is no picture in the Android UI to
 * hide — the picture is parity stage 2's work), and any Game Boy
 * save-state control (the GB native surface exports no state call, so
 * the button would be wired to nothing). Shipping buttons for those
 * would be worse than omitting them, for the reason iOS already
 * states about its own hidden reader groups -- a blind player cannot
 * tell a deliberately-absent control from a broken one. They go in
 * when the capability does.
 *
 * What parity stage 3 brought across from iOS's list: the game-sound
 * toggle (PSP; Game Boy has no audio path at all, which the switch's
 * own note says), Save state / Load state / Repeat last speech in the
 * Game state section, the reading log under Troubleshooting, and a
 * spoken announcement for every change made on this screen.
 *
 * The game-picture switch is the most recent to cross that bar: the
 * picture itself landed on the session screen (GamePicture, stage 2),
 * so the switch has something real to gate — and it defaults the way
 * iOS defaults it, off while a screen reader's touch exploration is
 * on (see DisplaySettings).
 *
 * The switches below are the controls that have crossed that bar: the PSP cue
 * has something real to gate — it is iOS's "Lock cue" switch under the name the
 * design docs use, off by default, exactly like iOS — and Birth by Sleep's
 * gauge-full mode has a real adapter seam behind it. Both are GLOBAL defaults:
 * a game can override either for itself in its Per Game Settings
 * (docs/design/per-game-settings.md), which is what the notes under each
 * switch say.
 *
 * The core line is here because it is the one fact this screen CAN report honestly,
 * and it is the same question the launcher asks rather than assuming.
 */
@Composable
fun SettingsScreen(
    coreAvailable: Boolean,
    battleCuesEnabled: Boolean,
    onBattleCuesChanged: (Boolean) -> Unit,
    hudGaugeCuesSound: Boolean,
    onHudGaugeCuesChanged: (Boolean) -> Unit,
    gameSoundEnabled: Boolean,
    onGameSoundChanged: (Boolean) -> Unit,
    showPicture: Boolean,
    onShowPictureChanged: (Boolean) -> Unit,
    pspRunning: Boolean,
    onSaveState: () -> Boolean,
    onLoadState: () -> Boolean,
    hasSavedState: () -> Boolean,
    onRepeatSpeech: () -> Boolean,
    onOpenReadingLog: () -> Unit,
    announce: (String) -> Unit = {},
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // ⛔ EVERY CHANGE ON THIS SCREEN IS SPOKEN (parity stage 3).
    // iOS's toggles announce their own change — with no screen, an
    // unspoken change is an invisible one — and its per-game panel
    // does the same. Here the line goes two ways, the launcher's
    // say() pattern exactly: [lastChange] feeds the live-region
    // status line below (which the screen reader speaks even before
    // any game has built the speech bridge), and [announce] carries
    // it to the bridge while a game runs.
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
            text = "Settings",
            fontSize = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back", fontSize = 18.sp)
        }

        // The change line: a live region, so the screen reader speaks
        // each change as it happens (see the note at the top of this
        // screen). Rendered only once there is something to say.
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

        Text(
            text = "Core",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Text(
            text = if (coreAvailable) {
                "Game Boy core: available."
            } else {
                "Game Boy core: NOT compiled into this build."
            },
            fontSize = 16.sp,
        )

        Text(
            text = "Sound",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(text = "Game sound", fontSize = 16.sp)
            // ⛔ THE SWITCH CARRIES ITS OWN NAME, like the picture
            // switch below: the label Text is a separate semantics
            // node and this Row does not merge, so without the
            // description TalkBack would focus an unnamed switch.
            Switch(
                checked = gameSoundEnabled,
                onCheckedChange = { enabled ->
                    onGameSoundChanged(enabled)
                    changed(if (enabled) "Game sound on." else "Game sound off.")
                },
                modifier = Modifier.semantics { contentDescription = "Game sound" },
            )
        }
        Text(
            text = "The game's own audio, for PSP games. On unless you turn it " +
                "off. Speech and the battle cues are never turned off by this: " +
                "a cue can still sound over a muted game. Game Boy games have " +
                "no sound in this app yet, so this switch has nothing to gate " +
                "for them.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "Performance",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(text = "Show game picture", fontSize = 16.sp)
            // ⛔ THE SWITCH CARRIES ITS OWN NAME. The label Text is a
            // separate semantics node and this Row does not merge, so
            // without the description TalkBack would focus an unnamed
            // on/off switch.
            Switch(
                checked = showPicture,
                onCheckedChange = onShowPictureChanged,
                modifier = Modifier.semantics { contentDescription = "Show game picture" },
            )
        }
        Text(
            text = "When on, the running game's picture is shown at the top " +
                "of the game screen, for someone looking at the phone. Off " +
                "keeps the picture out of the app entirely: the game still " +
                "runs, the screen shows nothing, and no time is spent " +
                "copying frames. Off by default while a screen reader's " +
                "touch exploration is on, as on iOS.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "Battle cues",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(text = "Battle cue sounds", fontSize = 16.sp)
            Switch(
                checked = battleCuesEnabled,
                onCheckedChange = { enabled ->
                    onBattleCuesChanged(enabled)
                    changed(
                        if (enabled) "Battle cue sounds on." else "Battle cue sounds off."
                    )
                },
            )
        }
        Text(
            text = "Off by default. When on, PSP games whose reader reports battle " +
                "cues play them over the game sound: the lock-on beacon, whose pulse " +
                "quickens as your target closes, and a beep on every hit that drops " +
                "a health bar \u2014 the foe on the right, you on the left, the pitch " +
                "falling as that health falls. It never changes what the game does.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )
        Text(
            text = "This is the app default. A game can override it for itself " +
                "from Per Game Settings on its page in the games list.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "Gauge-full cues",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(text = "Gauge-full cues as sounds", fontSize = 16.sp)
            Switch(
                checked = hudGaugeCuesSound,
                onCheckedChange = { useSound ->
                    onHudGaugeCuesChanged(useSound)
                    // iOS's own words for this change.
                    changed(
                        if (useSound) {
                            "Gauge-full cues: sounds."
                        } else {
                            "Gauge-full cues: spoken lines."
                        }
                    )
                },
            )
        }
        Text(
            text = "For Kingdom Hearts: Birth by Sleep. When on, a short sound " +
                "plays when the Focus or D-Link gauge fills; when off, a spoken " +
                "line says so instead. This is the app default; the game can " +
                "override it for itself from Per Game Settings on its page in " +
                "the games list.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "Game state",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Button(
            onClick = {
                changed(if (onSaveState()) "State saved." else "Could not save state.")
            },
            enabled = pspRunning,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Save state", fontSize = 16.sp)
        }
        Button(
            onClick = {
                changed(
                    when {
                        !hasSavedState() -> "No saved state yet."
                        onLoadState() -> "State loaded."
                        else -> "Could not load state."
                    }
                )
            },
            enabled = pspRunning,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Load state", fontSize = 16.sp)
        }
        Button(
            onClick = {
                if (!onRepeatSpeech()) changed("Nothing to repeat.")
            },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Repeat last speech", fontSize = 16.sp)
        }
        Text(
            text = "Save and load state act on the PSP game running now, in " +
                "the same per-game slot its own Save states group uses — " +
                "they are live while a PSP game runs and dimmed otherwise. " +
                "Game Boy games have no save-state control in this build " +
                "yet. Repeat last speech re-reads the newest line the " +
                "reader spoke.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "Troubleshooting",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Button(
            onClick = { onOpenReadingLog() },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Reading log", fontSize = 16.sp)
        }
        Text(
            text = "Shows the lines the reader has spoken, in order, and " +
                "lets you share them — for troubleshooting when a screen " +
                "is not announced as expected.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )

        Text(
            text = "About",
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )
        Text(
            text = "Open Game Access reads a running game's own memory and speaks what " +
                "is there \u2014 dialogue, menus, the map, characters and their health " +
                "\u2014 instead of guessing from the screen. Which game it can describe " +
                "depends on the reader script loaded for it.",
            fontSize = 14.sp,
            color = MaterialTheme.colors.onSurface.copy(alpha = 0.8f),
        )
    }
}
