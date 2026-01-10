package com.webworker

import android.util.Log
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule

/**
 * React Native TurboModule for WebWorker support.
 *
 * This is a thin Android wrapper around the shared C++ WebWorkerCore.
 * Mirrors the iOS implementation in ios/Webworker.mm.
 */
@ReactModule(name = WebworkerModule.NAME)
class WebworkerModule(reactContext: ReactApplicationContext) :
    NativeWebworkerSpec(reactContext), WebWorkerNative.WorkerCallback {

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
