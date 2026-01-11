//
// WebWorkerJNI.cpp
// Thin JNI wrapper around shared C++ WebWorkerCore
//

#include <jni.h>
#include <string>
#include <memory>
#include <android/log.h>
#include "WebWorkerCore.h"
#include "networking/FetchTypes.h"

#define LOG_TAG "WebWorkerJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global state
static std::shared_ptr<webworker::WebWorkerCore> gCore;
static JavaVM* gJavaVM = nullptr;
static jobject gCallbackRef = nullptr;
static jmethodID gOnMessageMethod = nullptr;
static jmethodID gOnErrorMethod = nullptr;
static jmethodID gOnConsoleMethod = nullptr;
static jmethodID gOnFetchMethod = nullptr;

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

    // Message callback
    gCore->setMessageCallback([](const std::string& workerId, const std::string& message) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnMessageMethod == nullptr) return;

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

    // Console callback
    gCore->setConsoleCallback([](const std::string& workerId, const std::string& level, const std::string& message) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnConsoleMethod == nullptr) return;

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

    // Error callback
    gCore->setErrorCallback([](const std::string& workerId, const std::string& error) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnErrorMethod == nullptr) return;

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

    // Fetch callback
    gCore->setFetchCallback([](const std::string& workerId, const webworker::FetchRequest& request) {
        JNIEnv* env = getJNIEnv();
        if (env == nullptr || gCallbackRef == nullptr || gOnFetchMethod == nullptr) return;

        jstring jWorkerId = env->NewStringUTF(workerId.c_str());
        jstring jRequestId = env->NewStringUTF(request.requestId.c_str());
        jstring jUrl = env->NewStringUTF(request.url.c_str());
        jstring jMethod = env->NewStringUTF(request.method.c_str());
        jstring jRedirect = env->NewStringUTF(request.redirect.c_str());
        jdouble jTimeout = (jdouble)request.timeout;
        
        jclass strClass = env->FindClass("java/lang/String");
        jobjectArray jHeaderKeys = env->NewObjectArray(request.headers.size(), strClass, nullptr);
        jobjectArray jHeaderValues = env->NewObjectArray(request.headers.size(), strClass, nullptr);
        
        int i = 0;
        for (const auto& header : request.headers) {
            jstring key = env->NewStringUTF(header.first.c_str());
            jstring value = env->NewStringUTF(header.second.c_str());
            env->SetObjectArrayElement(jHeaderKeys, i, key);
            env->SetObjectArrayElement(jHeaderValues, i, value);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(value);
            i++;
        }

        jbyteArray jBody = nullptr;
        if (!request.body.empty()) {
            jBody = env->NewByteArray(request.body.size());
            env->SetByteArrayRegion(jBody, 0, request.body.size(), (const jbyte*)request.body.data());
        }

        env->CallVoidMethod(gCallbackRef, gOnFetchMethod, jWorkerId, jRequestId, jUrl, jMethod, jHeaderKeys, jHeaderValues, jBody, jTimeout, jRedirect);

        env->DeleteLocalRef(jWorkerId);
        env->DeleteLocalRef(jRequestId);
        env->DeleteLocalRef(jUrl);
        env->DeleteLocalRef(jMethod);
        env->DeleteLocalRef(jRedirect);
        env->DeleteLocalRef(jHeaderKeys);
        env->DeleteLocalRef(jHeaderValues);
        if (jBody) env->DeleteLocalRef(jBody);
        
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
    if (gCore) {
        gCore->terminateAll();
        gCore.reset();
    }
    if (gCallbackRef != nullptr) {
        env->DeleteGlobalRef(gCallbackRef);
        gCallbackRef = nullptr;
    }

    gCore = std::make_shared<webworker::WebWorkerCore>();

    if (callback != nullptr) {
        gCallbackRef = env->NewGlobalRef(callback);
        jclass callbackClass = env->GetObjectClass(callback);
        gOnMessageMethod = env->GetMethodID(callbackClass, "onMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
        gOnErrorMethod = env->GetMethodID(callbackClass, "onError", "(Ljava/lang/String;Ljava/lang/String;)V");
        gOnConsoleMethod = env->GetMethodID(callbackClass, "onConsole", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        gOnFetchMethod = env->GetMethodID(callbackClass, "onFetch", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[BDLjava/lang/String;)V");
    }

    setupCallbacks();
}

JNIEXPORT jstring JNICALL
Java_com_webworker_WebWorkerNative_nativeCreateWorker(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring script
) {
    if (!gCore) return nullptr;
    std::string id = jstringToString(env, workerId);
    std::string scriptStr = jstringToString(env, script);
    try {
        std::string resultId = gCore->createWorker(id, scriptStr);
        return env->NewStringUTF(resultId.c_str());
    } catch (const std::exception& e) {
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
    if (!gCore) return JNI_FALSE;
    return gCore->terminateWorker(jstringToString(env, workerId)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativePostMessage(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring message
) {
    if (!gCore) return JNI_FALSE;
    return gCore->postMessage(jstringToString(env, workerId), jstringToString(env, message)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_webworker_WebWorkerNative_nativeEvalScript(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring script
) {
    if (!gCore) return nullptr;
    try {
        std::string result = gCore->evalScript(jstringToString(env, workerId), jstringToString(env, script));
        return env->NewStringUTF(result.c_str());
    } catch (const std::exception& e) {
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
    if (gCore) {
        gCore->terminateAll();
        gCore.reset();
    }
    if (gCallbackRef != nullptr) {
        env->DeleteGlobalRef(gCallbackRef);
        gCallbackRef = nullptr;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativeHasWorker(
    JNIEnv* env,
    jobject thiz,
    jstring workerId
) {
    if (!gCore) return JNI_FALSE;
    return gCore->hasWorker(jstringToString(env, workerId)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_webworker_WebWorkerNative_nativeIsWorkerRunning(
    JNIEnv* env,
    jobject thiz,
    jstring workerId
) {
    if (!gCore) return JNI_FALSE;
    return gCore->isWorkerRunning(jstringToString(env, workerId)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_webworker_WebWorkerNative_nativeHandleFetchResponse(
    JNIEnv* env,
    jobject thiz,
    jstring workerId,
    jstring requestId,
    jint status,
    jobjectArray headerKeys,
    jobjectArray headerValues,
    jbyteArray body,
    jstring error
) {
    if (!gCore) return;

    webworker::FetchResponse response;
    response.requestId = jstringToString(env, requestId);
    
    std::string errorStr = jstringToString(env, error);
    if (!errorStr.empty()) {
        response.error = errorStr;
    } else {
        response.status = status;
        
        int count = env->GetArrayLength(headerKeys);
        for (int i = 0; i < count; i++) {
            jstring key = (jstring)env->GetObjectArrayElement(headerKeys, i);
            jstring value = (jstring)env->GetObjectArrayElement(headerValues, i);
            response.headers[jstringToString(env, key)] = jstringToString(env, value);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(value);
        }
        
        if (body != nullptr) {
            jsize len = env->GetArrayLength(body);
            jbyte* bytes = env->GetByteArrayElements(body, nullptr);
            response.body.assign(bytes, bytes + len);
            env->ReleaseByteArrayElements(body, bytes, JNI_ABORT);
        }
    }
    
    gCore->handleFetchResponse(jstringToString(env, workerId), response);
}

} // extern "C"