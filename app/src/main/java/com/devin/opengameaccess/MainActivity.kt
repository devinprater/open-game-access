package com.devin.opengameaccess

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.webkit.WebViewAssetLoader
import java.util.Locale

class MainActivity : Activity() {

    private lateinit var webView: WebView
    private var tts: TextToSpeech? = null
    private var ttsReady = false
    private var utteranceSeq = 0

    /// The Game Boy launcher bridge. Created in onCreate, held so the activity
    /// result can be routed back into it.
    private var gbBridge: GbBridge? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        webView = WebView(this)
        setContentView(webView)
        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
        }

        // Serve the app's UI from assets over https://appassets.androidplatform.net
        // — no cleartext HTTP, no embedded server needed.
        val assetLoader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        webView.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: android.webkit.WebResourceRequest
            ): android.webkit.WebResourceResponse? {
                return assetLoader.shouldInterceptRequest(request.url)
            }
        }
        webView.addJavascriptInterface(TtsBridge(), "hermes_tts")

        // ⛔ THE GAME BOY PATH WAS UNREACHABLE WITHOUT THIS. GbAccessibilityScript
        // and the ten Java_..._GbAccessibilityScript_* JNI entry points have been
        // linked into the APK for a while and NOTHING called them, because this
        // launcher is a WebView and the page had no way to reach them. This
        // object is that way, and it also supplies the two things the page
        // cannot do for itself: the SAF picker and turning a content:// URI into
        // a path mGBA can open.
        val bridge = GbBridge(this, webView) { uri ->
            bridgePickedRom = uri
            bridge?.let { deliverPickedRom(uri) }
        }
        gbBridge = bridge
        webView.addJavascriptInterface(bridge, "oga_gb")
        webView.webChromeClient = WebChromeClient()
        webView.loadUrl("https://appassets.androidplatform.net/assets/index.html")

        tts = TextToSpeech(applicationContext) { status ->
            ttsReady = status == TextToSpeech.SUCCESS
            if (ttsReady) {
                tts?.language = Locale.US
            }
            // Completion hook for the speech queue: every utterance carries a
            // unique id so its finish/cancel maps back to exactly one line.
            // Today this is observed (log); when the shared core's
            // announcement queue is ported to this frontend, onUtteranceDone
            // is where poke_announce_done gets called from.
            tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String) {}
                override fun onDone(utteranceId: String) {
                    Log.d("OGA-TTS", "done " + utteranceId)
                }
                @Suppress("DEPRECATION")
                override fun onError(utteranceId: String) {
                    Log.d("OGA-TTS", "error " + utteranceId)
                }
            })
        }
    }

    /// The ROM the player chose, waiting for the page to ask for it.
    private var bridgePickedRom: Uri? = null
    private val bridge: GbBridge? get() = gbBridge

    /**
     * Hand the player's pick back to the page.
     *
     * ⛔ THE PAGE DECIDES WHAT IS SAID, NOT KOTLIN. The wording of the
     * confirmation belongs with the rest of the player-facing flow, which lives
     * in the WebView. Kotlin reports the FACT (here is the URI) and the page
     * speaks it — otherwise the voice would be split across two languages and
     * the tone could not be changed in one place.
     */
    private fun deliverPickedRom(uri: Uri) {
        val name = uri.lastPathSegment ?: uri.toString()
        webView.post {
            webView.evaluateJavascript(
                "window.ogaRomPicked && window.ogaRomPicked(" +
                jsString(uri.toString()) + ", " + jsString(name) + ");", null)
        }
    }

    /// Minimal JSON string escaping for evaluateJavascript.
    private fun jsString(s: String): String {
        val b = StringBuilder("\"")
        for (c in s) when (c) {
            '"' -> b.append("\\\"")
            '\\' -> b.append("\\\\")
            '\n' -> b.append("\\n")
            '\r' -> b.append("\\r")
            else -> if (c < ' ') b.append(String.format("\\u%04x", c.code)) else b.append(c)
        }
        return b.append("\"").toString()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        // The Game Boy picker is the only picker this activity starts; the ROM
        // list inside the WebView uses an <input type=file> and never reaches
        // here. RESULT_CANCELED routes through as a null URI so the page can say
        // "no game selected" rather than going silent.
        gbBridge?.deliverPick(if (resultCode == RESULT_OK) data?.data else null)
    }

    override fun onDestroy() {
        gbBridge?.stopGame()
        tts?.stop(); tts?.shutdown()
        super.onDestroy()
    }

    inner class TtsBridge {
        @JavascriptInterface
        fun speak(text: String, mode: String) {
            if (!ttsReady) return
            utteranceSeq += 1
            val utteranceId = "oga" + utteranceSeq
            if (mode == "queue") tts?.speak(text, TextToSpeech.QUEUE_ADD, null, utteranceId)
            else tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, utteranceId)
        }
        @JavascriptInterface
        fun stop() {
            tts?.stop()
        }
    }
}
