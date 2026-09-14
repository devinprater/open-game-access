package com.devin.opengameaccess

import android.app.Activity
import android.os.Bundle
import android.speech.tts.TextToSpeech
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
            if (mode == "queue") tts?.speak(text, TextToSpeech.QUEUE_ADD, null, "a11y")
            else tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, "a11y")
        }
        @JavascriptInterface
        fun stop() {
            tts?.stop()
        }
    }
}
