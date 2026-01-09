#pragma once

#include <jsi/jsi.h>
#include <hermes/hermes.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <condition_variable>

#include "TaskQueue.h"

namespace webworker {

using namespace facebook::jsi;

class WorkerRuntime;

/**
 * Callback type for messages sent from worker to host
 */
using MessageCallback = std::function<void(const std::string& workerId, const std::string& message)>;

/**
 * Callback type for console output from workers
 */
using ConsoleCallback = std::function<void(const std::string& workerId, const std::string& level, const std::string& message)>;

/**
 * Callback type for worker errors
 */
using ErrorCallback = std::function<void(const std::string& workerId, const std::string& error)>;

/**
 * WebWorkerCore - Platform-independent worker manager
 *
 * This is the shared C++ core that manages all web workers.
 * Both iOS and Android use this same implementation.
 */
class WebWorkerCore {
public:
    WebWorkerCore();
    ~WebWorkerCore();

    // Worker lifecycle
    std::string createWorker(const std::string& workerId, const std::string& script);
    bool terminateWorker(const std::string& workerId);
    void terminateAll();

    // Communication
    bool postMessage(const std::string& workerId, const std::string& message);
    std::string evalScript(const std::string& workerId, const std::string& script);

    // Callbacks
    void setMessageCallback(MessageCallback callback);
    void setConsoleCallback(ConsoleCallback callback);
    void setErrorCallback(ErrorCallback callback);

    // Query
    bool hasWorker(const std::string& workerId) const;
    bool isWorkerRunning(const std::string& workerId) const;

private:
    std::unordered_map<std::string, std::unique_ptr<WorkerRuntime>> workers_;
    mutable std::mutex workersMutex_;

    MessageCallback messageCallback_;
    ConsoleCallback consoleCallback_;
    ErrorCallback errorCallback_;
};

/**
 * WorkerRuntime - Individual worker runtime with its own Hermes instance
 *
 * Each worker runs in its own thread with a dedicated Hermes runtime.
 * Implements proper event loop following the HTML specification model:
 * - Macrotasks: setTimeout, setInterval, postMessage, etc.
 * - Microtasks: Promise callbacks, queueMicrotask
 */
class WorkerRuntime {
public:
    WorkerRuntime(const std::string& workerId,
                  MessageCallback messageCallback,
                  ConsoleCallback consoleCallback,
                  ErrorCallback errorCallback);
    ~WorkerRuntime();

    // Script execution
    bool loadScript(const std::string& script);
    std::string evalScript(const std::string& script);

    // Messaging
    bool postMessage(const std::string& message);

    // Lifecycle
    void terminate();

    // State
    const std::string& getId() const { return workerId_; }
    bool isRunning() const { return running_.load(); }

private:
    // Thread management
    void workerThreadMain();

    // Event loop (replaces the old processMessageQueue)
    void eventLoop();
    void processTask(Task& task);

    // Runtime setup
    void setupGlobalScope();
    void installNativeFunctions();
    void installTimerFunctions();

    // Message handling (called from native functions)
    void handlePostMessageToHost(const std::string& message);
    void handleConsoleLog(const std::string& level, const std::string& message);

    // Timer management
    uint64_t scheduleTimer(std::function<void()> callback,
                           std::chrono::milliseconds delay,
                           bool repeating);
    void cancelTimer(uint64_t timerId);

    // Close handling
    void requestClose();

    std::string workerId_;
    std::unique_ptr<Runtime> hermesRuntime_;
    std::unique_ptr<std::thread> workerThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> closeRequested_{false};
    std::mutex runtimeMutex_;

    // Task queue for event loop
    TaskQueue taskQueue_;
    std::atomic<uint64_t> nextTaskId_{1};
    std::atomic<uint64_t> nextTimerId_{1};

    // Track cancelled timers for interval cleanup
    std::unordered_set<uint64_t> cancelledTimers_;
    std::mutex cancelledTimersMutex_;

    // Script to execute after initialization
    std::string pendingScript_;
    std::mutex pendingScriptMutex_;
    std::condition_variable pendingScriptCondition_;
    bool hasPendingScript_{false};
    bool scriptExecuted_{false};

    // Callbacks
    MessageCallback messageCallback_;
    ConsoleCallback consoleCallback_;
    ErrorCallback errorCallback_;
};

} // namespace webworker
