package com.devin.opengameaccess.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.Button
import androidx.compose.material.MaterialTheme
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
 * exposes none of those yet: there is no audio control, no save-state call and no
 * debug-line feed in the session's surface. Shipping buttons for them would be worse
 * than omitting them, for the reason iOS already states about its own hidden reader
 * groups -- a blind player cannot tell a deliberately-absent control from a broken
 * one. They go in when the capability does.
 *
 * The core line is here because it is the one fact this screen CAN report honestly,
 * and it is the same question the launcher asks rather than assuming.
 */
@Composable
fun SettingsScreen(
    coreAvailable: Boolean,
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
