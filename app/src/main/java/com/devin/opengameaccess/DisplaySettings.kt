package com.devin.opengameaccess

import android.content.Context
import android.view.accessibility.AccessibilityManager

/**
 * The "Show game picture" switch: Android's half of iOS's Performance
 * setting (GameSession.showPicture, UserDefaults key `showPicture`).
 *
 * ⛔ THE DEFAULT IS THE SCREEN READER'S ANSWER, AS ON IOS. iOS defaults
 * the picture ON unless VoiceOver is running: the picture exists for
 * someone who can see it, and for a player who is listening it is pure
 * cost — iOS keeps it out of the frame path entirely when off, and
 * here the picture composable is not composed at all (see
 * LauncherScreen). So the first read asks the accessibility manager
 * whether touch exploration (TalkBack's exploring mode) is on and
 * defaults the opposite way. Once the player flips the switch in
 * Settings, their stored answer is the answer.
 *
 * Stored in the app's own preferences — the file CueSettings and
 * GameSettings keep theirs in — under its own key.
 */
object DisplaySettings {

    private const val PREFS = "oga_settings"
    private const val KEY_SHOW_PICTURE = "show_picture"

    fun showPicture(context: Context): Boolean {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (prefs.contains(KEY_SHOW_PICTURE)) {
            return prefs.getBoolean(KEY_SHOW_PICTURE, false)
        }
        val manager = context.getSystemService(Context.ACCESSIBILITY_SERVICE)
            as? AccessibilityManager
        return manager?.isTouchExplorationEnabled != true
    }

    fun setShowPicture(context: Context, show: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_SHOW_PICTURE, show)
            .apply()
    }
}
