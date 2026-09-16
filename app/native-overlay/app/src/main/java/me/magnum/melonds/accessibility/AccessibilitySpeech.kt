package me.magnum.melonds.accessibility

import android.content.Context
import android.provider.Settings
import android.speech.tts.TextToSpeech
import android.view.View
import android.view.accessibility.AccessibilityManager
import android.speech.tts.UtteranceProgressListener
import android.util.Log
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The voice for the Pokémon Access script.
 *
 * Every line main.lua says arrives here through
 * [AccessibilityScript]'s native speech callback (which fires on the emulator
 * thread, inside the frame loop), and goes straight to Android's
 * TextToSpeech. For a blind player this is the whole product: if speech does not
 * come out, nothing else the app does is usable.
 *
 * ⛔ THE NATIVE CALL MUST NEVER BLOCK. `speak()` is called from inside
 * nds->RunFrame()'s caller, so anything slow here stalls the emulated console.
 * TextToSpeech.speak() itself is asynchronous (it queues onto the TTS service),
 * which is exactly why it is used directly rather than through a coroutine.
 *
 * TTS engine bring-up is asynchronous too: the engine reports readiness through
 * OnInitListener, and the script may well say its first line before that lands.
 * Lines arriving early are held in [pendingSpeech] and flushed on init, because
 * dropping the loader line ("Pokemon black accessibility loader running.") is
 * precisely the message that tells the player the tool is alive.
 */
class AccessibilitySpeech(private val context: Context)
{
    companion object
    {
        private const val TAG = "PokemonAccess"
        private const val UTTERANCE_PREFIX = "pokemon-access"
    }

    private var tts: TextToSpeech? = null
    private val ready = AtomicBoolean(false)
    private val pendingSpeech = ArrayDeque<Pair<String, Boolean>>()
    private var lastSpoken = ""

    /**
     * The node game narration is announced through while a screen reader is
     * running. Set by the Activity once its views exist; null until then.
     */
    private var announcementView: android.view.View? = null

    /**
     * Point announcements at the running Activity's live-region view.
     *
     * Needed because this object outlives any one Activity and is created before
     * the views are: the bridge is built when the ROM loads, the Activity's layout
     * is inflated after. Held as a nullable reference rather than a constructor
     * argument for exactly that ordering.
     */
    fun setAnnouncementView(view: android.view.View?)
    {
        announcementView = view
    }

    /**
     * True when a screen reader is running, in which case it — not this class —
     * should be the voice.
     *
     * Mirrors the iOS engine, which hands announcements to VoiceOver when it is
     * running. Two independent voices reading the same product is not a subtle
     * problem: TalkBack and TextToSpeech each own an audio focus request, so they
     * interrupt and are interrupted by each other, and the player hears fragments
     * of both.
     */
    private fun screenReaderRunning(): Boolean
    {
        val manager = context.getSystemService(Context.ACCESSIBILITY_SERVICE) as? AccessibilityManager
            ?: return false
        return manager.isEnabled && manager.isTouchExplorationEnabled
    }

    fun init()
    {
        if (tts != null) return

        // TextToSpeech(context, listener) is deliberately given no engine name:
        // null means "the engine the player selected in system settings", and
        // passing an explicit name would freeze in whichever engine was default
        // at this instant instead of following a later change. The manifest
        // <queries> entry is what makes third-party engines visible to this app
        // at all on Android 11+ — without it the system default is reachable but
        // a player's own installed engine is not.
        tts = TextToSpeech(context.applicationContext) { status ->
            if (status == TextToSpeech.SUCCESS)
            {
                val localeResult = tts?.setLanguage(Locale.getDefault())
                if (localeResult == TextToSpeech.LANG_MISSING_DATA || localeResult == TextToSpeech.LANG_NOT_SUPPORTED)
                {
                    Log.w(TAG, "[pokemon-access] default locale not available for TTS; falling back to US English")
                    tts?.setLanguage(Locale.US)
                }

                // Honour the player's own speech rate, pitch and engine, which
                // they set because it is the speed and voice they can follow.
                //
                // Hard-coding 1.0f overrode all three. For a screen-reader user
                // that setting IS the interface: someone who has tuned their
                // device to a rate they can understand gets it silently reset by
                // this app, and a rate that is too fast is unusable rather than
                // merely annoying. Reading the actual settings is the only way
                // the app matches every other thing on the device that talks.
                applySystemSpeechPreferences()
                ready.set(true)
                val engine = runCatching { tts?.defaultEngine }.getOrNull()
                Log.i(TAG, "[pokemon-access] TTS ready, engine=${engine ?: "unknown"}")

                // Flush anything the script said while the engine was starting.
                synchronized(pendingSpeech)
                {
                    while (pendingSpeech.isNotEmpty())
                    {
                        val (text, interrupt) = pendingSpeech.removeFirst()
                        speakImmediately(text, interrupt)
                    }
                }
            }
            else
            {
                Log.e(TAG, "[pokemon-access] TTS engine failed to initialise (status $status)")
            }
        }

        tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener()
        {
            override fun onStart(utteranceId: String?) {}
            override fun onDone(utteranceId: String?) {}
            @Deprecated("Deprecated in Java")
            override fun onError(utteranceId: String?) {}
        })
    }

    /** Called from the emulator thread through JNI. Must be fast and must not throw. */
    @Suppress("unused")
    fun speak(text: String, interrupt: Boolean)
    {
        if (text.isBlank()) return
        if (!ready.get())
        {
            // Hold it; the engine can take a moment to come up and the script's
            // very first line is the one that matters most.
            synchronized(pendingSpeech)
            {
                if (pendingSpeech.size < 32) pendingSpeech.addLast(text to interrupt)
            }
            init()
            return
        }
        speakImmediately(text, interrupt)
    }

    /** Called from the emulator thread through JNI when the script stops speech. */
    @Suppress("unused")
    fun stop()
    {
        try
        {
            tts?.stop()
        }
        catch (e: Exception)
        {
            Log.w(TAG, "[pokemon-access] TTS stop failed", e)
        }
        synchronized(pendingSpeech) { pendingSpeech.clear() }
    }

    private var soundPool: android.media.SoundPool? = null
    private val soundIds = HashMap<String, Int>()

    /**
     * The cue sounds the GB/GBA reader plays as it walks a map ("wall", "pass",
     * "water", "stairs"...). `pan` is -100..100 and `volume` roughly 0..100, in
     * the game's own units.
     *
     * ⛔ ALSO CALLED FROM THE EMULATOR THREAD, so this must not block. SoundPool
     * loads are asynchronous and the load is only started once per path; playing
     * an id that has not finished loading is a no-op, which is the right
     * behaviour for a cue (a dropped beep is far better than a stalled frame).
     *
     * ⛔ AND IT MUST NOT THROW. A missing SoundPool (no audio device, an emulator
     * without audio) must leave the reader fully functional — a Lua error here
     * would end the whole session, which is the single worst failure this class
     * of app has.
     */
    @Suppress("unused")
    fun playSound(path: String, pan: Int, volume: Int)
    {
        try
        {
            if (path.isBlank()) return

            if (soundPool == null)
            {
                soundPool = android.media.SoundPool.Builder()
                    .setMaxStreams(4)
                    .setAudioAttributes(
                        android.media.AudioAttributes.Builder()
                            .setUsage(android.media.AudioAttributes.USAGE_GAME)
                            .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SONIFICATION)
                            .build()
                    )
                    .build()
            }

            val pool = soundPool ?: return
            val id = soundIds[path] ?: run {
                val loaded = pool.load(path, 1)
                soundIds[path] = loaded
                loaded
            }
            if (id == 0) return

            val clampedVolume = volume.coerceIn(0, 100) / 100f
            // Pan: -100..100 -> 0f..1f, left to right.
            val clampedPan = pan.coerceIn(-100, 100)
            val left = (1f - (clampedPan + 100) / 200f)
            val right = 1f - left

            pool.play(id, left * clampedVolume, right * clampedVolume, 1, 0, 1.0f)
        }
        catch (e: Exception)
        {
            // Deliberately swallowed: see the note above.
            Log.w(TAG, "[pokemon-access] cue sound failed for $path: ${e.message}")
        }
    }

    /**
     * Mirror the device's own text-to-speech settings.
     *
     * Android exposes these as a contract that every other speaking app on the
     * device follows. Copying them is what makes this app sound like the rest of
     * the system instead of like itself — and it is the difference between
     * usable and not for a listener who has already tuned them.
     *
     * Each read is individually guarded: a device can be missing any of these
     * keys (a fresh install, a non-standard engine), and none of them is worth
     * crashing the voice over. A missing rate falls back to 1.0 rather than to
     * zero, which would be silence.
     */
    private fun applySystemSpeechPreferences()
    {
        val resolver = context.contentResolver
        val rate = runCatching {
            Settings.Secure.getInt(resolver, Settings.Secure.TTS_DEFAULT_RATE)
        }.getOrNull()?.takeIf { it > 0 }?.let { it / 100f } ?: 1.0f
        tts?.setSpeechRate(rate)

        val pitch = runCatching {
            Settings.Secure.getInt(resolver, Settings.Secure.TTS_DEFAULT_PITCH)
        }.getOrNull()?.takeIf { it > 0 }?.let { it / 100f } ?: 1.0f
        tts?.setPitch(pitch)

        Log.i(TAG, "[pokemon-access] speech rate=$rate pitch=$pitch (from the player's device settings)")
    }

    private fun speakImmediately(text: String, interrupt: Boolean)
    {
        lastSpoken = text

        // A screen reader is running: let IT be the voice, exactly as iOS hands
        // announcements to VoiceOver. Speaking through TextToSpeech as well would
        // put two voices on the same output, each interrupting the other.
        //
        // The queue/interrupt contract still has to be honoured, and a live region
        // expresses it directly: changing a polite region's text queues behind
        // whatever is being read, while an assertive one interrupts. So a single
        // view carries both cases by switching its polite-ness, rather than
        // needing two mechanisms.
        if (screenReaderRunning())
        {
            val view = announcementView
            if (view != null)
            {
                view.accessibilityLiveRegion = if (interrupt)
                    android.view.View.ACCESSIBILITY_LIVE_REGION_ASSERTIVE
                else
                    android.view.View.ACCESSIBILITY_LIVE_REGION_POLITE
                // Setting the text is what triggers the announcement; clearing it
                // first restarts the region when the same line repeats, which is
                // what the script's "repeat" key does.
                view.contentDescription = text
                Log.i(TAG, "[speech] ${if (interrupt) "say" else "queue"} (screen reader): $text")
                return
            }
            // No view yet (narration started before the layout inflated). Falling
            // through to TTS is better than dropping the line — the loader line is
            // the one that tells the player the tool is alive.
            Log.w(TAG, "[speech] no announcement view yet; speaking via TTS: $text")
        }

        val mode = if (interrupt) TextToSpeech.QUEUE_FLUSH else TextToSpeech.QUEUE_ADD
        try
        {
            tts?.speak(text, mode, null, "$UTTERANCE_PREFIX-${System.nanoTime()}")
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[pokemon-access] TTS speak failed: ${e.message}")
        }
        Log.i(TAG, "[speech] ${if (interrupt) "say" else "queue"}: $text")
    }

    /** The script's repeat key ("U") re-reads the last spoken line. */
    fun repeatLast()
    {
        if (lastSpoken.isNotEmpty()) speak(lastSpoken, true)
    }

    fun shutdown()
    {
        ready.set(false)
        try
        {
            tts?.stop()
            tts?.shutdown()
        }
        catch (_: Exception)
        {
        }
        tts = null
    }
}
