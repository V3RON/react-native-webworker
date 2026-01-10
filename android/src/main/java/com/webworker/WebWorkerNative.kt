package com.webworker

import android.util.Log

/**
 * Native JNI bridge for WebWorker Hermes runtime.
 *
 * This is a thin Kotlin wrapper around the shared C++ WebWorkerCore.
 * All worker logic is implemented in the shared cpp/ directory.
 */
object WebWorkerNative {

    private const val TAG = "WebWorkerNative"
    private var isInitialized = false

    init {
        try {
            System.loadLibrary("webworker")
            Log.d(TAG, "Native library loaded successfully")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load native library: ${e.message}")
        }
    }

    /**
     * Callback interface for worker events.
     * This matches the callback system used in the shared C++ core.
     */
    interface WorkerCallback {
        /** Called when a worker posts a binary message using structured clone */
        fun onBinaryMessage(workerId: String, data: ByteArray)

        /** Called when a worker encounters an error */
        fun onError(workerId: String, error: String)

        /** Called for worker console output (log, error, warn, info) */
        fun onConsole(workerId: String, level: String, message: String)
    }

    /**
     * Initialize the native WebWorkerCore with callbacks.
     * Must be called before any other native methods.
     */
    fun initialize(callback: WorkerCallback) {
        if (!isInitialized) {
            nativeInit(callback)
            isInitialized = true
            Log.d(TAG, "WebWorkerCore initialized")
        }
    }

    /**
     * Create a new worker with the given script content.
     * @return The worker ID on success
     * @throws RuntimeException on failure
     */
    fun createWorker(workerId: String, scriptContent: String): String {
        if (!isInitialized) {
            throw RuntimeException("WebWorkerCore not initialized. Call initialize() first.")
        }
        return nativeCreateWorker(workerId, scriptContent)
    }

    /**
     * Terminate a worker by ID.
     * @return true if worker was found and terminated
     */
    fun terminateWorker(workerId: String): Boolean {
        return nativeTerminateWorker(workerId)
    }

    /**
     * Post a message to a worker using structured clone algorithm.
     * @param data Binary data serialized using structured clone
     * @return true if message was posted successfully
     */
    fun postMessageBinary(workerId: String, data: ByteArray): Boolean {
        return nativePostMessageBinary(workerId, data)
    }

    /**
     * Evaluate a script in a worker and return the result.
     * @return The stringified result of the script evaluation
     * @throws RuntimeException on failure
     */
    fun evalScript(workerId: String, script: String): String {
        return nativeEvalScript(workerId, script)
    }

    /**
     * Check if a worker exists.
     */
    fun hasWorker(workerId: String): Boolean {
        return nativeHasWorker(workerId)
    }

    /**
     * Check if a worker is running.
     */
    fun isWorkerRunning(workerId: String): Boolean {
        return nativeIsWorkerRunning(workerId)
    }

    /**
     * Clean up all workers and release resources.
     */
    fun cleanup() {
        nativeCleanup()
        isInitialized = false
        Log.d(TAG, "WebWorkerCore cleaned up")
    }

    // Native methods - implemented in WebWorkerJNI.cpp

    private external fun nativeInit(callback: WorkerCallback)
    private external fun nativeCreateWorker(workerId: String, script: String): String
    private external fun nativeTerminateWorker(workerId: String): Boolean
    private external fun nativePostMessageBinary(workerId: String, data: ByteArray): Boolean
    private external fun nativeEvalScript(workerId: String, script: String): String
    private external fun nativeHasWorker(workerId: String): Boolean
    private external fun nativeIsWorkerRunning(workerId: String): Boolean
    private external fun nativeCleanup()
}
