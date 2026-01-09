//
// WebWorkerRuntime.h
// Android JNI header for WebWorker Hermes runtime
//

#ifndef WEBWORKER_RUNTIME_H
#define WEBWORKER_RUNTIME_H

#include <jni.h>
#include <hermes/hermes.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace webworker {

using namespace facebook::jsi;
using namespace facebook::hermes;

/**
 * Represents a single WebWorker with its own Hermes runtime
 */
class WebWorkerRuntime {
public:
    WebWorkerRuntime(const std::string& workerId, JNIEnv* env, jobject callback);
    ~WebWorkerRuntime();

    bool loadScript(const std::string& script, const std::string& sourceUrl);
    void postMessage(const std::string& message);
    std::string evaluateScript(const std::string& script);
    void terminate();
    bool isRunning() const { return running_; }

private:
    void workerThreadMain();
    void initializeRuntime();
    void setupWorkerGlobalScope();
    void installNativeFunctions();
    void processMessage(const std::string& message);

    std::string workerId_;
    std::unique_ptr<HermesRuntime> runtime_;
    std::thread workerThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> messageQueue_;
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;

    // JNI callback reference
    JavaVM* javaVM_;
    jobject callbackRef_;
    jmethodID onMessageMethodId_;
    jmethodID onErrorMethodId_;
    jmethodID onLogMethodId_;
};

} // namespace webworker

#endif // WEBWORKER_RUNTIME_H

