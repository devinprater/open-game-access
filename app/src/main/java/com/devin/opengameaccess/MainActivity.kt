package com.devin.opengameaccess

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Surface
import androidx.compose.material.darkColors
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.platform.LocalView
import com.devin.opengameaccess.ui.LauncherScreen
import com.devin.opengameaccess.ui.SettingsScreen
import me.magnum.melonds.accessibility.AccessibilityScript

/**
 * The launcher, as a Compose Activity.
 *
 * ⛔ WHAT THIS REPLACES. This was a `WebView` hosting `assets/index.html` over
 * `WebViewAssetLoader`, with a `@JavascriptInterface` bridge for the picker and the
 * native calls. That had three costs, one of them audible:
 *
 *  1. The page's status line spoke through its own `hermes_tts` TTS client with
 *     QUEUE_FLUSH, so a launcher message interrupted the game reader mid-sentence.
 *  2. `AccessibilitySpeech`'s live-region path — the one that hands announcements to
 *     the running screen reader instead of putting a second voice on the same
 *     output — needs `setAnnouncementView`, which the old Activity never called, so
 *     the guard silently degraded to TTS. Now wired in [Launcher].
 *  3. Bridging JS to Kotlin was a name-matching problem across two languages, which
 *     is why a sabotage test existed to police it. Compose removes the seam.
 *
 * The picker is no longer `onActivityResult`: Compose's
 * `ActivityResultContracts.OpenDocument` owns it, so there is no request code and no
 * manual result routing to get wrong.
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // ⛔ DO NOT INITIALISE THE SPEECH BRIDGE HERE.
        // `AccessibilityScript.initialize()` reaches a JNI method (`setSpeechBridge`),
        // and the native library is NOT loaded until a ROM loads. Calling it in
        // onCreate therefore kills the app with:
        //
        //     UnsatisfiedLinkError: No implementation found for void
        //     me.magnum.melonds.accessibility.AccessibilityScript.setSpeechBridge(...)
        //
        // Measured on the device, not theorised. The bridge is built on demand when a
        // game starts (`GbAccessibilityScript.start` -> `AccessibilityScript.speechBridge`),
        // and the launcher's own lines are announced by the SCREEN READER through the
        // status view's live region, which needs no native code at all.

        setContent {
            // ⛔ MATERIAL 2, to match the host app (see LauncherScreen's note):
            // the frontend's `libs.compose.material3` is material3.adaptive, not
            // the component library, so material3.MaterialTheme does not resolve.
            // darkColors() keeps the #111-on-#eee look the WebView page had.
            MaterialTheme(colors = darkColors()) {
                Surface {
                    Launcher()
                }
            }
        }
    }

    /**
     * Wires the announcement view and owns the frame clock.
     *
     * ⛔ THE ANNOUNCEMENT VIEW IS NOT OPTIONAL, AND IT MUST BE AN ATTACHED VIEW.
     * `AccessibilitySpeech` announces by mutating a view's live region when a screen
     * reader is running; with no view it falls back to TextToSpeech and the player
     * gets two voices. The view is only announceable once it is in the window, hence
     * [SideEffect] rather than a plain assignment during composition.
     */
    @Composable
    private fun Launcher() {
        val view = LocalView.current
        val context = LocalContext.current

        SideEffect {
            // Kept so the view is handed over as soon as the bridge EXISTS; attaching
            // it before then forwards to a null bridge and does nothing (see
            // GbGameSession.attachAnnouncementView, which also re-forwards on start).
            GbGameSession.attachAnnouncementView(view)
        }

        var showSettings by remember { mutableStateOf(false) }

        if (showSettings) {
            SettingsScreen(
                coreAvailable = GbGameSession.coreAvailable(context),
                onBack = { showSettings = false },
            )
            return
        }

        LauncherScreen(
            session = GbGameSession,
            onOpenSettings = { showSettings = true },
            announce = { text, interrupt ->
                // ⛔ THE STATUS LIVE REGION IS THE VOICE; THIS IS ONLY A FALLBACK.
                // The status Text carries a liveRegion and its contentDescription is
                // this same string, so a running screen reader announces it -- that is
                // how the WebView-era bug was fixed (the screen reader should be the
                // only voice).
                //
                // The bridge only exists once a game has started, so calling it earlier
                // is both a crash (no native library) and unnecessary. Guarded rather
                // than removed: with a game running, this is the same bridge the reader
                // uses, so a launcher line during play queues behind the reader's speech
                // instead of opening a second TTS client.
                if (GbGameSession.isRunning) AccessibilityScript.speak(text, interrupt)
            },
        )
    }

    override fun onDestroy() {
        // Stop the reader before the process goes away, or the emulator thread
        // outlives the Activity that owns the views it announces through.
        GbGameSession.stop()
        super.onDestroy()
    }
}
