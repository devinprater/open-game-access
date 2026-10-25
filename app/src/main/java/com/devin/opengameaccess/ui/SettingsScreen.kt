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
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Settings, mirroring iOS's SettingsView as far as the Android path can honestly go.
 *
 * ⛔ WHAT IS DELIBERATELY ABSENT, AND WHY. iOS offers a screen picker, a game-sound
 * toggle, save/load state, repeat-last-speech and a reading log. The Android path
 * exposes none of those yet: there is no game-sound control, no save-state call and
 * no debug-line feed in the session's surface. Shipping buttons for them would be
 * worse than omitting them, for the reason iOS already states about its own hidden
 * reader groups -- a blind player cannot tell a deliberately-absent control from a
 * broken one. They go in when the capability does.
 *
 * The two switches below are the controls that have crossed that bar: the PSP cue
 * synth (beacon + health beeps + earcons) exists now, so the battle-cues switch
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
            text = "Settings",
            fontSize = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.semantics { heading() },
        )

        Button(onClick = { onBack() }, modifier = Modifier.fillMaxWidth()) {
            Text("Back", fontSize = 18.sp)
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
            Switch(checked = battleCuesEnabled, onCheckedChange = onBattleCuesChanged)
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
            Switch(checked = hudGaugeCuesSound, onCheckedChange = onHudGaugeCuesChanged)
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
