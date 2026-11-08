package com.devin.opengameaccess

import android.content.Context

/**
 * The battle-cues switch, Android's half of the iOS Settings "Lock cue"
 * (UserDefaults key `cueBeacon`, off by default there too).
 *
 * ⛔ ONE SWITCH FOR THE WHOLE FAMILY, AS ON IOS. The name here says
 * "battle cues" rather than "lock cue" because that is what it gates:
 * the lock-on beacon AND the one-shot family (health hit beeps,
 * gauge-full earcons) — docs/design/cue-synth.md's rule is that the
 * four ride the existing battle-cues switch together with the beacon,
 * and the Settings text says so in plain words. Off by default: a cue
 * the player did not ask for must never startle them on by surprise,
 * and a PSP game whose reader reports no cues stays silent either way.
 *
 * Stored in the app's own preferences; read by [PspGameSession] each
 * frame (a cheap getBoolean against the in-memory prefs cache), so
 * flipping it mid-session takes effect on the next frame, the way the
 * iOS session's didSet silences the synth when the switch goes off.
 */
object CueSettings {

    private const val PREFS = "oga_settings"
    private const val KEY_BATTLE_CUES = "battle_cues"

    fun battleCuesEnabled(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getBoolean(KEY_BATTLE_CUES, false)

    fun setBattleCuesEnabled(context: Context, enabled: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_BATTLE_CUES, enabled)
            .apply()
        // Silence promptly rather than at the next poll: a cue left
        // sounding after its switch went off is the Android form of
        // the stuck tone iOS's stop() guards against.
        if (!enabled) PspCueSynth.silenceAll()
    }
}

/**
 * The game-sound switch: iOS's Settings "Game sound"
 * (`GameSession.audioEnabled`, on by default there), in the same
 * preferences file as the cue switches.
 *
 * ⛔ IT GATES THE GAME'S AUDIO, NEVER THE CUES OR THE SPEECH. iOS
 * keeps the three apart on purpose — a player may want the lock cue
 * with the music muted — and the Android drain honours the same
 * split: [PspGameSession] silences the game's samples before
 * [PspCueSynth] mixes its cues into the same buffer, so the cues
 * keep sounding with game sound off. Speech never passes through
 * the drain at all.
 *
 * ⛔ PSP ONLY, STATED IN THE SETTINGS TEXT. Game Boy has no audio
 * path in this app at all (no audio export exists on its native
 * surface), so there is no GB sound for this switch to gate; the
 * switch must not pretend otherwise.
 */
object GameSoundSettings {

    private const val PREFS = "oga_settings"
    private const val KEY_GAME_SOUND = "game_sound"

    /** On by default, as on iOS — and as Android behaved before the switch existed. */
    fun gameSoundEnabled(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getBoolean(KEY_GAME_SOUND, true)

    fun setGameSoundEnabled(context: Context, enabled: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_GAME_SOUND, enabled)
            .apply()
    }
}
