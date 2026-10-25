package com.devin.opengameaccess

import android.content.Context

/**
 * Per-game settings: the model behind the "Per Game Settings" panel on
 * each game's library page (docs/design/per-game-settings.md). The
 * twin of iOS's GameSettings.swift — same model, same support table,
 * same key shape in this platform's own store.
 *
 * THE MODEL, in one paragraph. Every setting has a GLOBAL DEFAULT,
 * which lives in the app's Settings screen and applies to every game
 * that can use the setting. A game may carry an OVERRIDE in its own
 * panel. The value a session actually uses is the EFFECTIVE value:
 *
 *     effective = the game's override, if one exists, else the global.
 *
 * A game shows only the settings its reader can use (see
 * [supportedSettings]); a panel row for a setting the game cannot use
 * would be a dead control, and in this app a dead control is
 * indistinguishable from a broken one.
 *
 * KEYS. Everything lives in the "oga_settings" preferences file
 * [CueSettings] opened. The battle-cues global is CueSettings' own
 * "battle_cues" key (it owns that switch, including the silence on
 * off); the gauge-cues global is "hud_gauge_cues_sound" here.
 * Overrides are "game.<adapter id>.<setting key>", where the adapter
 * id is the registry id ("khbbs") — the same id the library data and
 * the native host use. Presence of the key IS the override: clearing
 * one removes the key.
 */
object GameSettings {

    private const val PREFS = "oga_settings"
    private const val KEY_HUD_GAUGE_CUES_SOUND = "hud_gauge_cues_sound"

    /** Adapter registry ids, named where other code must match them. */
    const val ID_KHBBS = "khbbs"

    /** One setting a game can carry an override for. */
    enum class Setting(val key: String, val title: String) {
        /** The lock-on beacon + health beeps + gauge-full earcons family. */
        BATTLE_CUES("battle_cues", "Battle cue sounds"),

        /** Birth by Sleep's HUD gauge-full moments: earcon sounds
         * (true) or the adapter's spoken lines (false). */
        HUD_GAUGE_CUES("hud_gauge_cues_sound", "Gauge-full cues"),
    }

    // ---- which games can use which setting ------------------------------

    /**
     * The settings a game can use, in panel order. Keyed by adapter
     * registry id, kept by hand in the app layer — the same precedent
     * as iOS's AdapterCommand.supported table: what each reader REALLY
     * does today. BATTLE_CUES belongs to exactly the adapters that fill
     * a cue snapshot in Core (dissidia, dbzar, mku, khbbs);
     * HUD_GAUGE_CUES belongs to khbbs alone — the seam is that
     * adapter's own setter (oga::KhbbsHudGaugeCues).
     */
    fun supportedSettings(gameId: String): List<Setting> = when (gameId) {
        ID_KHBBS -> listOf(Setting.BATTLE_CUES, Setting.HUD_GAUGE_CUES)
        "dissidia", "dbzar", "mku" -> listOf(Setting.BATTLE_CUES)
        else -> emptyList()
    }

    // ---- global defaults -------------------------------------------------

    /** The current global default for a setting, from Settings. */
    fun globalDefault(context: Context, setting: Setting): Boolean = when (setting) {
        Setting.BATTLE_CUES -> CueSettings.battleCuesEnabled(context)
        Setting.HUD_GAUGE_CUES -> prefs(context).getBoolean(KEY_HUD_GAUGE_CUES_SOUND, true)
    }

    /** The Settings screen's write path for the gauge-cues global. */
    fun setGlobalHudGaugeCuesSound(context: Context, useSound: Boolean) {
        prefs(context).edit().putBoolean(KEY_HUD_GAUGE_CUES_SOUND, useSound).apply()
    }

    // ---- overrides --------------------------------------------------------

    private fun overrideKey(gameId: String, setting: Setting): String =
        "game.$gameId.${setting.key}"

    /**
     * The game's override for a setting, or null when it has none —
     * null is the normal state and means "use the global default".
     */
    fun override(context: Context, gameId: String, setting: Setting): Boolean? {
        val p = prefs(context)
        val key = overrideKey(gameId, setting)
        return if (p.contains(key)) p.getBoolean(key, false) else null
    }

    /**
     * Set the game's override — or, with null, clear it back to
     * inheriting the global default (the key is removed, not set
     * false: presence is the override).
     */
    fun setOverride(context: Context, gameId: String, setting: Setting, value: Boolean?) {
        val editor = prefs(context).edit()
        val key = overrideKey(gameId, setting)
        if (value == null) editor.remove(key) else editor.putBoolean(key, value)
        editor.apply()
    }

    // ---- resolution --------------------------------------------------------

    /**
     * The value a session uses: the game's override when one exists,
     * else the global default. A null/unknown game id resolves to the
     * global — which is also what the game resolves to once named if
     * it has no override, so the answer never flips mid-start.
     */
    fun effective(context: Context, gameId: String?, setting: Setting): Boolean {
        if (gameId != null) {
            override(context, gameId, setting)?.let { return it }
        }
        return globalDefault(context, setting)
    }

    fun effectiveBattleCues(context: Context, gameId: String?): Boolean =
        effective(context, gameId, Setting.BATTLE_CUES)

    fun effectiveHudGaugeCuesSound(context: Context, gameId: String?): Boolean =
        effective(context, gameId, Setting.HUD_GAUGE_CUES)

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
