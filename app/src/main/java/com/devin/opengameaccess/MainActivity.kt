package com.devin.opengameaccess

import android.app.Activity
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

    override fun onDestroy() {
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
