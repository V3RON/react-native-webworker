//
// WebWorkerJNI.cpp
// Thin JNI wrapper around shared C++ WebWorkerCore
//
// This is the Android equivalent of ios/Webworker.mm - a minimal platform binding
// that delegates all logic to the shared C++ core.
//

#include <jni.h>
#include <string>
#include <memory>
#include <android/log.h>
#include "WebWorkerCore.h"

#define LOG_TAG "WebWorkerJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global state
static std::shared_ptr<webworker::WebWorkerCore> gCore;
static JavaVM* gJavaVM = nullptr;
static jobject gCallbackRef = nullptr;
static jmethodID gOnMessageMethod = nullptr;
static jmethodID gOnBinaryMessageMethod = nullptr;
static jmethodID gOnErrorMethod = nullptr;
static jmethodID gOnConsoleMethod = nullptr;

// Helper to convert jstring to std::string
static std::string jstringToString(JNIEnv* env, jstring jstr) {
    if (jstr == nullptr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

// Helper to get JNIEnv* in any thread
static JNIEnv* getJNIEnv() {
    if (gJavaVM == nullptr) return nullptr;

    JNIEnv* env = nullptr;
    int status = gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        if (gJavaVM->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    }

    return env;
}

// Setup callbacks that route events from C++ core to Java
static void setupCallbacks() {
    if (!gCore) return;

    // Binary message callback - called when worker posts a message using structured clone
    gCore->setBinaryMessageCallback([](const std::string& workerId, const std::vector<uint8_t>& data) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnBinaryMessageMethod == nullptr) {
            LOGE("Cannot invoke onBinaryMessage callback - JNI not ready");
            return;
        }

        jstring jWorkerId = env->NewStringUTF(workerId.c_str());
        jbyteArray jData = env->NewByteArray(static_cast<jsize>(data.size()));
        if (jData != nullptr && !data.empty()) {
            env->SetByteArrayRegion(jData, 0, static_cast<jsize>(data.size()),
                                    reinterpret_cast<const jbyte*>(data.data()));
        }

        env->CallVoidMethod(gCallbackRef, gOnBinaryMessageMethod, jWorkerId, jData);

        env->DeleteLocalRef(jWorkerId);
        if (jData != nullptr) {
            env->DeleteLocalRef(jData);
        }

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    });

    // Legacy message callback (for backwards compatibility, if needed)
    gCore->setMessageCallback([](const std::string& workerId, const std::string& message) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnMessageMethod == nullptr) {
            LOGE("Cannot invoke onMessage callback - JNI not ready");
            return;
        }

        jstring jWorkerId = env->NewStringUTF(workerId.c_str());
        jstring jMessage = env->NewStringUTF(message.c_str());

        env->CallVoidMethod(gCallbackRef, gOnMessageMethod, jWorkerId, jMessage);

        env->DeleteLocalRef(jWorkerId);
        env->DeleteLocalRef(jMessage);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    });

    // Console callback - called for worker console.log/error/etc
    gCore->setConsoleCallback([](const std::string& workerId, const std::string& level, const std::string& message) {
        LOGI("[Worker %s] [%s] %s", workerId.c_str(), level.c_str(), message.c_str());

        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnConsoleMethod == nullptr) {
            return;
        }

        jstring jWorkerId = env->NewStringUTF(workerId.c_str());
        jstring jLevel = env->NewStringUTF(level.c_str());
        jstring jMessage = env->NewStringUTF(message.c_str());

        env->CallVoidMethod(gCallbackRef, gOnConsoleMethod, jWorkerId, jLevel, jMessage);

        env->DeleteLocalRef(jWorkerId);
        env->DeleteLocalRef(jLevel);
        env->DeleteLocalRef(jMessage);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    });

    // Error callback - called when worker encounters an error
    gCore->setErrorCallback([](const std::string& workerId, const std::string& error) {
        LOGE("[Worker %s] ERROR: %s", workerId.c_str(), error.c_str());

        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnErrorMethod == nullptr) {
            return;
        }

        jstring jWorkerId = env->NewStringUTF(workerId.c_str());
        jstring jError = env->NewStringUTF(error.c_str());

        env->CallVoidMethod(gCallbackRef, gOnErrorMethod, jWorkerId, jError);

        env->DeleteLocalRef(jWorkerId);
        env->DeleteLocalRef(jError);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    });
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJavaVM = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_webworker_WebWorkerNative_nativeInit(
    JNIEnv* env,
    jobject thiz,
    jobject callback
) {
    LOGI("Initializing WebWorkerCore");

    // Clean up any existing core
    if (gCore) {
        gCore->terminateAll();
        gCore.reset();
    }

    // Clean up old callback reference
    if (gCallbackRef != nullptr) {
        env->DeleteGlobalRef(gCallbackRef);
        gCallbackRef = nullptr;
    }

    // Create new core
    gCore = std::make_shared<webworker::WebWorkerCore>();

    // Store callback reference and get method IDs
    if (callback != nullptr) {
        gCallbackRef = env->NewGlobalRef(callback);

        jclass callbackClass = env->GetObjectClass(callback);
        gOnMessageMethod = env->GetMethodID(callbackClass, "onMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
        gOnBinaryMessageMethod = env->GetMethodID(callbackClass, "onBinaryMessage", "(Ljava/lang/String;[B)V");
        gOnErrorMethod = env->GetMethodID(callbackClass, "onError", "(Ljava/lang/String;Ljava/lang/String;)V");
        gOnConsoleMethod = env->GetMethodID(callbackClass, "onConsole", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");

        if (gOnBinaryMessageMethod == nullptr || gOnErrorMethod == nullptr || gOnConsoleMethod == nullptr) {
            LOGE("Failed to get callback method IDs");
        }
    }

    // Setup callbacks to route events to Java
    setupCallbacks();

    LOGI("WebWorkerCore initialized successfully");
}

JNIEXPORT jstring JNICALL
Java_com_webworker_WebWorkerNative_nativeCreateWorker(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring script
) {
    if (!gCore) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exClass, "WebWorkerCore not initialized");
        return nullptr;
    }

    std::string id = jstringToString(env, workerId);
    std::string scriptStr = jstringToString(env, script);

    try {
        std::string resultId = gCore->createWorker(id, scriptStr);
        LOGI("Created worker: %s", resultId.c_str());
        return env->NewStringUTF(resultId.c_str());
    } catch (const std::exception& e) {
        LOGE("Failed to create worker: %s", e.what());
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exClass, e.what());
        return nullptr;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativeTerminateWorker(
    JNIEnv* env,
    jobject thiz,
    jstring workerId
) {
    if (!gCore) {
        return JNI_FALSE;
    }

    std::string id = jstringToString(env, workerId);

    bool success = gCore->terminateWorker(id);
    if (success) {
        LOGI("Terminated worker: %s", id.c_str());
    }

    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativePostMessage(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring message
) {
    if (!gCore) {
        return JNI_FALSE;
    }

    std::string id = jstringToString(env, workerId);
    std::string msg = jstringToString(env, message);

    bool success = gCore->postMessage(id, msg);
    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativePostMessageBinary(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jbyteArray data
) {
    if (!gCore) {
        return JNI_FALSE;
    }

    std::string id = jstringToString(env, workerId);

    // Convert jbyteArray to std::vector<uint8_t>
    jsize length = env->GetArrayLength(data);
    std::vector<uint8_t> binaryData(static_cast<size_t>(length));
    if (length > 0) {
        env->GetByteArrayRegion(data, 0, length, reinterpret_cast<jbyte*>(binaryData.data()));
    }

    bool success = gCore->postMessageBinary(id, binaryData);
    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_webworker_WebWorkerNative_nativeEvalScript(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring script
) {
    if (!gCore) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exClass, "WebWorkerCore not initialized");
        return nullptr;
    }

    std::string id = jstringToString(env, workerId);
    std::string scriptStr = jstringToString(env, script);

    try {
        std::string result = gCore->evalScript(id, scriptStr);
        return env->NewStringUTF(result.c_str());
    } catch (const std::exception& e) {
        LOGE("Failed to evaluate script: %s", e.what());
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exClass, e.what());
        return nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_webworker_WebWorkerNative_nativeCleanup(
    JNIEnv* env,
    jobject thiz
) {
    LOGI("Cleaning up WebWorkerCore");

    if (gCore) {
        gCore->terminateAll();
        gCore.reset();
    }

    if (gCallbackRef != nullptr) {
        env->DeleteGlobalRef(gCallbackRef);
        gCallbackRef = nullptr;
    }

    gOnMessageMethod = nullptr;
    gOnBinaryMessageMethod = nullptr;
    gOnErrorMethod = nullptr;
    gOnConsoleMethod = nullptr;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativeHasWorker(
    JNIEnv* env,
    jobject thiz,
    jstring workerId
) {
    if (!gCore) {
        return JNI_FALSE;
    }

    std::string id = jstringToString(env, workerId);
    return gCore->hasWorker(id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativeIsWorkerRunning(
    JNIEnv* env,
    jobject thiz,
    jstring workerId
) {
    if (!gCore) {
        return JNI_FALSE;
    }

    std::string id = jstringToString(env, workerId);
    return gCore->isWorkerRunning(id) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
