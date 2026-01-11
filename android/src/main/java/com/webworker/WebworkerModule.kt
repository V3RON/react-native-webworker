package com.webworker

import android.util.Log
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import okhttp3.Call
import okhttp3.Callback
import okhttp3.MediaType
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * React Native TurboModule for WebWorker support.
 *
 * This is a thin Android wrapper around the shared C++ WebWorkerCore.
 * Mirrors the iOS implementation in ios/Webworker.mm.
 */
@ReactModule(name = WebworkerModule.NAME)
class WebworkerModule(reactContext: ReactApplicationContext) :
    NativeWebworkerSpec(reactContext), WebWorkerNative.WorkerCallback {

    private val client = OkHttpClient()

    init {
        // Initialize the native core with this module as the callback receiver
        WebWorkerNative.initialize(this)
    }

    override fun getName(): String = NAME

    // ============================================================================
    // Callback handlers - route events from C++ core to JavaScript
    // ============================================================================

    override fun onMessage(workerId: String, message: String) {
        Log.d(TAG, "[$workerId] Message from worker")
        emitOnWorkerMessage(Arguments.createMap().apply {
            putString("workerId", workerId)
            putString("message", message)
        })
    }

    override fun onError(workerId: String, error: String) {
        Log.e(TAG, "[$workerId] Error: $error")
        emitOnWorkerError(Arguments.createMap().apply {
            putString("workerId", workerId)
            putString("error", error)
        })
    }

    override fun onConsole(workerId: String, level: String, message: String) {
        Log.d(TAG, "[$workerId] [$level] $message")
        emitOnWorkerConsole(Arguments.createMap().apply {
            putString("workerId", workerId)
            putString("level", level)
            putString("message", message)
        })
    }

    override fun onFetch(
        workerId: String,
        requestId: String,
        url: String,
        method: String,
        headerKeys: Array<String>,
        headerValues: Array<String>,
        body: ByteArray?,
        timeout: Double,
        redirect: String
    ) {
        val requestBuilder = Request.Builder()
            .url(url)

        // Headers
        var contentType: MediaType? = null
        for (i in headerKeys.indices) {
            val key = headerKeys[i]
            val value = headerValues[i]
            requestBuilder.addHeader(key, value)
            if (key.equals("Content-Type", ignoreCase = true)) {
                contentType = value.toMediaTypeOrNull()
            }
        }

        // Body
        val requestBody = if (body != null && body.isNotEmpty()) {
            body.toRequestBody(contentType)
        } else if (method.equals("POST", ignoreCase = true) || method.equals("PUT", ignoreCase = true) || method.equals("PATCH", ignoreCase = true)) {
             ByteArray(0).toRequestBody(null)
        } else {
             null
        }

        requestBuilder.method(method, requestBody)

        // Configure client based on options
        val requestClient = if (timeout > 0 || redirect != "follow") {
             val builder = client.newBuilder()
             if (timeout > 0) {
                 val timeoutMs = timeout.toLong()
                 builder.callTimeout(timeoutMs, TimeUnit.MILLISECONDS)
                 builder.readTimeout(timeoutMs, TimeUnit.MILLISECONDS)
                 builder.connectTimeout(timeoutMs, TimeUnit.MILLISECONDS)
             }
             if (redirect == "error" || redirect == "manual") {
                 builder.followRedirects(false)
                 builder.followSslRedirects(false)
             }
             builder.build()
        } else {
             client
        }

        requestClient.newCall(requestBuilder.build()).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                WebWorkerNative.handleFetchResponse(
                    workerId, requestId, 0, emptyArray(), emptyArray(), null, e.message
                )
            }

            override fun onResponse(call: Call, response: Response) {
                val responseBody = response.body?.bytes()
                val headers = response.headers
                val keys = mutableListOf<String>()
                val values = mutableListOf<String>()

                for (i in 0 until headers.size) {
                    keys.add(headers.name(i))
                    values.add(headers.value(i))
                }

                WebWorkerNative.handleFetchResponse(
                    workerId,
                    requestId,
                    response.code,
                    keys.toTypedArray(),
                    values.toTypedArray(),
                    responseBody,
                    null
                )
            }
        })
    }

    // ============================================================================
    // TurboModule Methods - mirror iOS implementation
    // ============================================================================

    override fun createWorker(workerId: String, scriptPath: String, promise: Promise) {
        try {
            val scriptContent = loadScriptFromPath(scriptPath)
            val resultId = WebWorkerNative.createWorker(workerId, scriptContent)
            promise.resolve(resultId)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create worker from path: ${e.message}")
            promise.reject("WORKER_ERROR", "Failed to create worker: ${e.message}", e)
        }
    }

    override fun createWorkerWithScript(workerId: String, scriptContent: String, promise: Promise) {
        try {
            val resultId = WebWorkerNative.createWorker(workerId, scriptContent)
            promise.resolve(resultId)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create worker: ${e.message}")
            promise.reject("WORKER_ERROR", "Failed to create worker: ${e.message}", e)
        }
    }

    override fun terminateWorker(workerId: String, promise: Promise) {
        try {
            val success = WebWorkerNative.terminateWorker(workerId)
            promise.resolve(success)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to terminate worker: ${e.message}")
            promise.reject("WORKER_ERROR", "Failed to terminate worker: ${e.message}", e)
        }
    }

    override fun postMessage(workerId: String, message: String, promise: Promise) {
        try {
            val success = WebWorkerNative.postMessage(workerId, message)
            promise.resolve(success)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to post message: ${e.message}")
            promise.reject("POST_MESSAGE_ERROR", "Failed to post message: ${e.message}", e)
        }
    }

    override fun evalScript(workerId: String, script: String, promise: Promise) {
        try {
            val result = WebWorkerNative.evalScript(workerId, script)
            promise.resolve(result)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to evaluate script: ${e.message}")
            promise.reject("EVAL_ERROR", "Failed to evaluate script: ${e.message}", e)
        }
    }

    // ============================================================================
    // Helper methods
    // ============================================================================

    private fun loadScriptFromPath(scriptPath: String): String {
        return try {
            val context = reactApplicationContext
            if (scriptPath.startsWith("assets://")) {
                val assetPath = scriptPath.removePrefix("assets://")
                context.assets.open(assetPath).bufferedReader().use { it.readText() }
            } else {
                java.io.File(scriptPath).readText()
            }
        } catch (e: Exception) {
            throw RuntimeException("Failed to load script from path: $scriptPath", e)
        }
    }

    override fun invalidate() {
        super.invalidate()
        try {
            WebWorkerNative.cleanup()
            Log.d(TAG, "WebWorkerModule invalidated and cleaned up")
        } catch (e: Exception) {
            Log.e(TAG, "Error during cleanup: ${e.message}")
        }
    }

    companion object {
        const val NAME = "Webworker"
        private const val TAG = "WebworkerModule"
    }
}
