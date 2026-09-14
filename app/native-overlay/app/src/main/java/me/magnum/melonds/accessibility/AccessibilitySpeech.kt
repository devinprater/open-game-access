package me.magnum.melonds.accessibility

import android.content.Context
import android.speech.tts.TextToSpeech
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

    fun init()
    {
        if (tts != null) return

        tts = TextToSpeech(context.applicationContext) { status ->
            if (status == TextToSpeech.SUCCESS)
            {
                val localeResult = tts?.setLanguage(Locale.getDefault())
                if (localeResult == TextToSpeech.LANG_MISSING_DATA || localeResult == TextToSpeech.LANG_NOT_SUPPORTED)
                {
                    Log.w(TAG, "[pokemon-access] default locale not available for TTS; falling back to US English")
                    tts?.setLanguage(Locale.US)
                }
                tts?.setSpeechRate(1.0f)
                ready.set(true)
                Log.i(TAG, "[pokemon-access] TTS ready")

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

    private fun speakImmediately(text: String, interrupt: Boolean)
    {
        lastSpoken = text
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
