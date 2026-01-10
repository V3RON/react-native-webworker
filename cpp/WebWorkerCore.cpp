#include "WebWorkerCore.h"
#include "StructuredClone/StructuredCloneWriter.h"
#include "StructuredClone/StructuredCloneReader.h"
#include "StructuredClone/StructuredCloneError.h"
#include <chrono>

namespace webworker {

// ============================================================================
// WebWorkerCore Implementation
// ============================================================================

WebWorkerCore::WebWorkerCore()
    : messageCallback_(nullptr)
    , binaryMessageCallback_(nullptr)
    , consoleCallback_(nullptr)
    , errorCallback_(nullptr) {
}

WebWorkerCore::~WebWorkerCore() {
    terminateAll();
}

std::string WebWorkerCore::createWorker(
    const std::string& workerId,
    const std::string& script
) {
    std::lock_guard<std::mutex> lock(workersMutex_);

    if (workers_.find(workerId) != workers_.end()) {
        throw std::runtime_error("Worker already exists: " + workerId);
    }

    auto worker = std::make_unique<WorkerRuntime>(
        workerId,
        messageCallback_,
        binaryMessageCallback_,
        consoleCallback_,
        errorCallback_
    );

    if (!worker->loadScript(script)) {
        throw std::runtime_error("Failed to load script for worker: " + workerId);
    }

    workers_[workerId] = std::move(worker);
    return workerId;
}

bool WebWorkerCore::terminateWorker(const std::string& workerId) {
    std::lock_guard<std::mutex> lock(workersMutex_);

    auto it = workers_.find(workerId);
    if (it == workers_.end()) {
        return false;
    }

    it->second->terminate();
    workers_.erase(it);
    return true;
}

void WebWorkerCore::terminateAll() {
    std::lock_guard<std::mutex> lock(workersMutex_);

    for (auto& pair : workers_) {
        pair.second->terminate();
    }
    workers_.clear();
}

bool WebWorkerCore::postMessage(
    const std::string& workerId,
    const std::string& message
) {
    std::lock_guard<std::mutex> lock(workersMutex_);

    auto it = workers_.find(workerId);
    if (it == workers_.end() || !it->second->isRunning()) {
        return false;
    }

    return it->second->postMessage(message);
}

bool WebWorkerCore::postMessageBinary(
    const std::string& workerId,
    const std::vector<uint8_t>& data
) {
    std::lock_guard<std::mutex> lock(workersMutex_);

    auto it = workers_.find(workerId);
    if (it == workers_.end() || !it->second->isRunning()) {
        return false;
    }

    return it->second->postMessageBinary(data);
}

std::string WebWorkerCore::evalScript(
    const std::string& workerId,
    const std::string& script
) {
    std::lock_guard<std::mutex> lock(workersMutex_);

    auto it = workers_.find(workerId);
    if (it == workers_.end() || !it->second->isRunning()) {
        throw std::runtime_error("Worker not found or not running: " + workerId);
    }

    return it->second->evalScript(script);
}

void WebWorkerCore::setMessageCallback(MessageCallback callback) {
    messageCallback_ = callback;
}

void WebWorkerCore::setBinaryMessageCallback(BinaryMessageCallback callback) {
    binaryMessageCallback_ = callback;
}

void WebWorkerCore::setConsoleCallback(ConsoleCallback callback) {
    consoleCallback_ = callback;
}

void WebWorkerCore::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = callback;
}

bool WebWorkerCore::hasWorker(const std::string& workerId) const {
    std::lock_guard<std::mutex> lock(workersMutex_);
    return workers_.find(workerId) != workers_.end();
}

bool WebWorkerCore::isWorkerRunning(const std::string& workerId) const {
    std::lock_guard<std::mutex> lock(workersMutex_);
    auto it = workers_.find(workerId);
    return it != workers_.end() && it->second->isRunning();
}

// ============================================================================
// WorkerRuntime Implementation
// ============================================================================

WorkerRuntime::WorkerRuntime(
    const std::string& workerId,
    MessageCallback messageCallback,
    BinaryMessageCallback binaryMessageCallback,
    ConsoleCallback consoleCallback,
    ErrorCallback errorCallback
)
    : workerId_(workerId)
    , messageCallback_(messageCallback)
    , binaryMessageCallback_(binaryMessageCallback)
    , consoleCallback_(consoleCallback)
    , errorCallback_(errorCallback) {

    // Start worker thread
    workerThread_ = std::make_unique<std::thread>(&WorkerRuntime::workerThreadMain, this);

    // Wait for runtime to be initialized
    while (!initialized_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

WorkerRuntime::~WorkerRuntime() {
    terminate();
}

void WorkerRuntime::workerThreadMain() {
    try {
        // Create Hermes runtime
        auto runtimeConfig = ::hermes::vm::RuntimeConfig::Builder()
            .withIntl(false)
            .build();

        hermesRuntime_ = facebook::hermes::makeHermesRuntime(runtimeConfig);

        if (!hermesRuntime_) {
            if (errorCallback_) {
                errorCallback_(workerId_, "Failed to create Hermes runtime");
            }
            initialized_ = true;
            return;
        }

        setupGlobalScope();
        installNativeFunctions();
        installTimerFunctions();

        running_ = true;
        initialized_ = true;

        // Wait for script to be loaded
        {
            std::unique_lock<std::mutex> lock(pendingScriptMutex_);
            pendingScriptCondition_.wait(lock, [this] {
                return hasPendingScript_ || !running_.load();
            });

            if (!running_.load()) {
                return;
            }

            // Execute the pending script
            if (hasPendingScript_) {
                try {
                    std::lock_guard<std::mutex> runtimeLock(runtimeMutex_);
                    hermesRuntime_->evaluateJavaScript(
                        std::make_shared<StringBuffer>(pendingScript_),
                        "worker-script.js"
                    );

                    // Drain microtasks after script execution
                    static_cast<facebook::hermes::HermesRuntime*>(hermesRuntime_.get())
                        ->drainMicrotasks();

                    scriptExecuted_ = true;
                } catch (const JSError& e) {
                    if (errorCallback_) {
                        errorCallback_(workerId_, "JSError: " + e.getMessage());
                    }
                    scriptExecuted_ = false;
                } catch (const std::exception& e) {
                    if (errorCallback_) {
                        errorCallback_(workerId_, "Exception: " + std::string(e.what()));
                    }
                    scriptExecuted_ = false;
                }
                hasPendingScript_ = false;
            }
        }
        pendingScriptCondition_.notify_all();

        // Run the event loop
        eventLoop();

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Worker thread exception: " + std::string(e.what()));
        }
        initialized_ = true;
    }
}

void WorkerRuntime::eventLoop() {
    while (running_.load() && !closeRequested_.load()) {
        // Calculate wait time (until next timer or max wait)
        auto waitTime = taskQueue_.timeUntilNext();

        // Cap wait time to avoid very long waits
        if (waitTime > std::chrono::milliseconds(1000)) {
            waitTime = std::chrono::milliseconds(1000);
        }

        // Wait for and dequeue next task
        auto task = taskQueue_.dequeue(waitTime);

        if (!task.has_value()) {
            continue;  // Timeout or spurious wake, continue loop
        }

        if (task->cancelled) {
            continue;  // Task was cancelled, skip it
        }

        // Check if this timer was cancelled
        if (task->type == TaskType::Timer) {
            std::lock_guard<std::mutex> lock(cancelledTimersMutex_);
            if (cancelledTimers_.count(task->id) > 0) {
                continue;  // Timer was cancelled
            }
        }

        // Execute the macrotask
        processTask(*task);
    }
}

void WorkerRuntime::processTask(Task& task) {
    if (!hermesRuntime_ || !running_.load()) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(runtimeMutex_);

        // Execute the task
        task.execute();

        // Drain microtasks after each macrotask (HTML spec requirement)
        // This processes all Promise callbacks, queueMicrotask, etc.
        static_cast<facebook::hermes::HermesRuntime*>(hermesRuntime_.get())
            ->drainMicrotasks();

    } catch (const JSError& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "JSError in task: " + e.getMessage());
        }
    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception in task: " + std::string(e.what()));
        }
    }
}

void WorkerRuntime::setupGlobalScope() {
    if (!hermesRuntime_) return;

    try {
        Runtime& runtime = *hermesRuntime_;

        std::string initScript = R"(
            // Worker global scope initialization
            var self = this;
            var global = this;
            var messageHandlers = [];

            self.onmessage = null;

            // postMessage - sends messages to the host using structured clone
            self.postMessage = function(message) {
                __nativePostMessageStructured(message);
            };

            // addEventListener
            self.addEventListener = function(type, handler) {
                if (type === 'message' && typeof handler === 'function') {
                    messageHandlers.push(handler);
                }
            };

            // removeEventListener
            self.removeEventListener = function(type, handler) {
                if (type === 'message') {
                    var index = messageHandlers.indexOf(handler);
                    if (index > -1) {
                        messageHandlers.splice(index, 1);
                    }
                }
            };

            // Internal message handler (called from native with deserialized data)
            self.__handleMessage = function(data) {
                var event = {
                    data: data,
                    type: 'message'
                };

                // Call onmessage if set
                if (typeof self.onmessage === 'function') {
                    self.onmessage(event);
                }

                // Call all registered handlers
                messageHandlers.forEach(function(handler) {
                    handler(event);
                });
            };

            // Basic console implementation
            var console = {
                log: function() {
                    var args = Array.prototype.slice.call(arguments);
                    var message = args.map(function(arg) {
                        return typeof arg === 'object' ? JSON.stringify(arg) : String(arg);
                    }).join(' ');
                    if (typeof __nativeConsoleLog !== 'undefined') {
                        __nativeConsoleLog('log', message);
                    }
                },
                error: function() {
                    var args = Array.prototype.slice.call(arguments);
                    var message = args.map(function(arg) {
                        return typeof arg === 'object' ? JSON.stringify(arg) : String(arg);
                    }).join(' ');
                    if (typeof __nativeConsoleLog !== 'undefined') {
                        __nativeConsoleLog('error', message);
                    }
                },
                warn: function() {
                    var args = Array.prototype.slice.call(arguments);
                    var message = args.map(function(arg) {
                        return typeof arg === 'object' ? JSON.stringify(arg) : String(arg);
                    }).join(' ');
                    if (typeof __nativeConsoleLog !== 'undefined') {
                        __nativeConsoleLog('warn', message);
                    }
                },
                info: function() {
                    var args = Array.prototype.slice.call(arguments);
                    var message = args.map(function(arg) {
                        return typeof arg === 'object' ? JSON.stringify(arg) : String(arg);
                    }).join(' ');
                    if (typeof __nativeConsoleLog !== 'undefined') {
                        __nativeConsoleLog('info', message);
                    }
                }
            };

            self.console = console;

            // queueMicrotask (uses Promise for Hermes compatibility)
            self.queueMicrotask = function(callback) {
                Promise.resolve().then(callback);
            };

            // close() - request worker termination
            self.close = function() {
                if (typeof __nativeRequestClose !== 'undefined') {
                    __nativeRequestClose();
                }
            };
        )";

        runtime.evaluateJavaScript(
            std::make_shared<StringBuffer>(initScript),
            "worker-init.js"
        );

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception setting up global scope: " + std::string(e.what()));
        }
    }
}

void WorkerRuntime::installNativeFunctions() {
    if (!hermesRuntime_) return;

    try {
        Runtime& runtime = *hermesRuntime_;

        // Capture this pointer for callbacks
        auto* self = this;

        // __nativePostMessageStructured - uses structured clone algorithm
        auto postMessageStructuredFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativePostMessageStructured"),
            1,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count > 0) {
                    try {
                        // Serialize using structured clone
                        SerializedData data = StructuredCloneWriter::serialize(rt, args[0]);
                        self->handleBinaryMessageToHost(data.buffer);
                    } catch (const DataCloneError& e) {
                        // Throw the error back to JavaScript
                        throw JSError(rt, e.what());
                    }
                }
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativePostMessageStructured", postMessageStructuredFunc);

        // __nativeConsoleLog
        auto consoleLogFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeConsoleLog"),
            2,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count >= 2) {
                    std::string level = args[0].toString(rt).utf8(rt);
                    std::string message = args[1].toString(rt).utf8(rt);
                    self->handleConsoleLog(level, message);
                } else if (count >= 1) {
                    std::string message = args[0].toString(rt).utf8(rt);
                    self->handleConsoleLog("log", message);
                }
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativeConsoleLog", consoleLogFunc);

        // __nativeRequestClose
        auto requestCloseFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeRequestClose"),
            0,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                self->requestClose();
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativeRequestClose", requestCloseFunc);

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception installing native functions: " + std::string(e.what()));
        }
    }
}

void WorkerRuntime::installTimerFunctions() {
    if (!hermesRuntime_) return;

    try {
        Runtime& runtime = *hermesRuntime_;
        auto* self = this;

        // __nativeScheduleTimer(timerId, delay, repeating, callback)
        auto scheduleTimerFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeScheduleTimer"),
            4,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count < 4) {
                    return Value::undefined();
                }

                uint64_t timerId = static_cast<uint64_t>(args[0].asNumber());
                int64_t delay = static_cast<int64_t>(args[1].asNumber());
                bool repeating = args[2].getBool();

                // Ensure delay is non-negative
                if (delay < 0) delay = 0;

                // Store the callback function
                auto callback = std::make_shared<Value>(rt, args[3]);

                // Create the timer callback using a shared function to avoid code duplication
                std::function<void()> timerCallback;

                // Use a shared_ptr to allow the callback to reference itself for rescheduling
                auto callbackHolder = std::make_shared<std::function<void()>>();

                *callbackHolder = [self, callback, timerId, delay, repeating, callbackHolder]() {
                    if (!self->hermesRuntime_ || !self->running_.load()) {
                        return;
                    }

                    // Check if timer was cancelled
                    {
                        std::lock_guard<std::mutex> lock(self->cancelledTimersMutex_);
                        if (self->cancelledTimers_.count(timerId) > 0) {
                            return;
                        }
                    }

                    Runtime& rt = *self->hermesRuntime_;

                    try {
                        if (callback->isObject() && callback->asObject(rt).isFunction(rt)) {
                            callback->asObject(rt).asFunction(rt).call(rt);
                        }
                    } catch (const JSError& e) {
                        if (self->errorCallback_) {
                            self->errorCallback_(self->workerId_, "JSError in timer: " + e.getMessage());
                        }
                    }

                    // Reschedule if interval and not cancelled
                    if (repeating) {
                        std::lock_guard<std::mutex> lock(self->cancelledTimersMutex_);
                        if (self->cancelledTimers_.count(timerId) == 0) {
                            Task nextTask;
                            nextTask.type = TaskType::Timer;
                            nextTask.id = timerId;
                            nextTask.execute = *callbackHolder;
                            self->taskQueue_.enqueueDelayed(std::move(nextTask),
                                std::chrono::milliseconds(delay));
                        }
                    }
                };

                // Schedule the initial timer
                Task task;
                task.type = TaskType::Timer;
                task.id = timerId;
                task.execute = *callbackHolder;

                self->taskQueue_.enqueueDelayed(std::move(task),
                    std::chrono::milliseconds(delay));

                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativeScheduleTimer", scheduleTimerFunc);

        // __nativeCancelTimer(timerId)
        auto cancelTimerFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeCancelTimer"),
            1,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count > 0 && args[0].isNumber()) {
                    uint64_t timerId = static_cast<uint64_t>(args[0].asNumber());
                    self->cancelTimer(timerId);
                }
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativeCancelTimer", cancelTimerFunc);

        // Now add JavaScript wrapper for setTimeout, setInterval, etc.
        std::string timerScript = R"(
            // Timer ID counter (start high to avoid conflicts)
            var __nextTimerId = 1;

            // setTimeout
            self.setTimeout = function(callback, delay) {
                if (typeof callback !== 'function') {
                    if (typeof callback === 'string') {
                        callback = new Function(callback);
                    } else {
                        return 0;
                    }
                }
                var timerId = __nextTimerId++;
                var args = Array.prototype.slice.call(arguments, 2);
                var wrappedCallback = function() {
                    callback.apply(null, args);
                };
                __nativeScheduleTimer(timerId, delay || 0, false, wrappedCallback);
                return timerId;
            };

            // clearTimeout
            self.clearTimeout = function(timerId) {
                if (timerId) {
                    __nativeCancelTimer(timerId);
                }
            };

            // setInterval
            self.setInterval = function(callback, delay) {
                if (typeof callback !== 'function') {
                    if (typeof callback === 'string') {
                        callback = new Function(callback);
                    } else {
                        return 0;
                    }
                }
                var timerId = __nextTimerId++;
                var args = Array.prototype.slice.call(arguments, 2);
                var wrappedCallback = function() {
                    callback.apply(null, args);
                };
                __nativeScheduleTimer(timerId, delay || 0, true, wrappedCallback);
                return timerId;
            };

            // clearInterval (same as clearTimeout)
            self.clearInterval = function(timerId) {
                self.clearTimeout(timerId);
            };

            // setImmediate (non-standard but common in React Native)
            self.setImmediate = function(callback) {
                var args = Array.prototype.slice.call(arguments, 1);
                return self.setTimeout(function() {
                    callback.apply(null, args);
                }, 0);
            };

            // clearImmediate
            self.clearImmediate = function(timerId) {
                self.clearTimeout(timerId);
            };
        )";

        runtime.evaluateJavaScript(
            std::make_shared<StringBuffer>(timerScript),
            "worker-timers.js"
        );

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception installing timer functions: " + std::string(e.what()));
        }
    }
}

uint64_t WorkerRuntime::scheduleTimer(
    std::function<void()> callback,
    std::chrono::milliseconds delay,
    bool repeating
) {
    uint64_t timerId = nextTimerId_++;

    Task task;
    task.type = TaskType::Timer;
    task.id = timerId;
    task.execute = std::move(callback);

    taskQueue_.enqueueDelayed(std::move(task), delay);
    return timerId;
}

void WorkerRuntime::cancelTimer(uint64_t timerId) {
    {
        std::lock_guard<std::mutex> lock(cancelledTimersMutex_);
        cancelledTimers_.insert(timerId);
    }
    taskQueue_.cancel(timerId);
}

void WorkerRuntime::requestClose() {
    closeRequested_ = true;
    taskQueue_.shutdown();
}

void WorkerRuntime::handlePostMessageToHost(const std::string& message) {
    if (messageCallback_) {
        messageCallback_(workerId_, message);
    }
}

void WorkerRuntime::handleBinaryMessageToHost(const std::vector<uint8_t>& data) {
    if (binaryMessageCallback_) {
        binaryMessageCallback_(workerId_, data);
    }
}

void WorkerRuntime::handleConsoleLog(const std::string& level, const std::string& message) {
    if (consoleCallback_) {
        consoleCallback_(workerId_, level, message);
    }
}

bool WorkerRuntime::loadScript(const std::string& script) {
    if (!running_.load() && !initialized_.load()) {
        // Worker thread hasn't started yet, wait for it
        while (!initialized_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (!running_.load()) {
        return false;
    }

    // Set pending script and notify worker thread
    {
        std::lock_guard<std::mutex> lock(pendingScriptMutex_);
        pendingScript_ = script;
        hasPendingScript_ = true;
        scriptExecuted_ = false;
    }
    pendingScriptCondition_.notify_all();

    // Wait for script to be executed
    {
        std::unique_lock<std::mutex> lock(pendingScriptMutex_);
        pendingScriptCondition_.wait(lock, [this] {
            return !hasPendingScript_ || !running_.load();
        });
    }

    return scriptExecuted_;
}

bool WorkerRuntime::postMessage(const std::string& message) {
    if (!running_.load()) {
        return false;
    }

    // Create a message task and enqueue it
    Task task;
    task.type = TaskType::Message;
    task.id = nextTaskId_++;
    task.execute = [this, message]() {
        if (!hermesRuntime_ || !running_.load()) {
            return;
        }

        Runtime& runtime = *hermesRuntime_;

        auto handleMessageProp = runtime.global().getProperty(runtime, "__handleMessage");

        if (handleMessageProp.isObject() && handleMessageProp.asObject(runtime).isFunction(runtime)) {
            auto handleMessage = handleMessageProp.asObject(runtime).asFunction(runtime);
            handleMessage.call(runtime, String::createFromUtf8(runtime, message));
        }
    };

    taskQueue_.enqueue(std::move(task));
    return true;
}

bool WorkerRuntime::postMessageBinary(const std::vector<uint8_t>& data) {
    if (!running_.load()) {
        return false;
    }

    // Copy the data to ensure it survives until the task executes
    std::vector<uint8_t> dataCopy = data;

    // Create a message task and enqueue it
    Task task;
    task.type = TaskType::Message;
    task.id = nextTaskId_++;
    task.execute = [this, dataCopy = std::move(dataCopy)]() {
        if (!hermesRuntime_ || !running_.load()) {
            return;
        }

        Runtime& runtime = *hermesRuntime_;

        try {
            // Deserialize the binary data to a JavaScript value
            Value deserializedValue = StructuredCloneReader::deserialize(
                runtime, dataCopy.data(), dataCopy.size());

            // Call __handleMessage with the deserialized value
            auto handleMessageProp = runtime.global().getProperty(runtime, "__handleMessage");

            if (handleMessageProp.isObject() && handleMessageProp.asObject(runtime).isFunction(runtime)) {
                auto handleMessage = handleMessageProp.asObject(runtime).asFunction(runtime);
                handleMessage.call(runtime, deserializedValue);
            }
        } catch (const DataCloneError& e) {
            if (errorCallback_) {
                errorCallback_(workerId_, "Deserialization error: " + std::string(e.what()));
            }
        }
    };

    taskQueue_.enqueue(std::move(task));
    return true;
}

std::string WorkerRuntime::evalScript(const std::string& script) {
    if (!hermesRuntime_ || !running_.load()) {
        throw std::runtime_error("Runtime not available");
    }

    std::lock_guard<std::mutex> lock(runtimeMutex_);
    Runtime& runtime = *hermesRuntime_;

    try {
        Value result = runtime.evaluateJavaScript(
            std::make_shared<StringBuffer>(script),
            "eval.js"
        );

        // Drain microtasks after eval
        static_cast<facebook::hermes::HermesRuntime*>(hermesRuntime_.get())
            ->drainMicrotasks();

        // Convert result to string
        if (result.isString()) {
            return result.asString(runtime).utf8(runtime);
        } else if (result.isNumber()) {
            double num = result.asNumber();
            if (num == static_cast<int64_t>(num)) {
                return std::to_string(static_cast<int64_t>(num));
            }
            return std::to_string(num);
        } else if (result.isBool()) {
            return result.getBool() ? "true" : "false";
        } else if (result.isNull()) {
            return "null";
        } else if (result.isUndefined()) {
            return "undefined";
        } else if (result.isObject()) {
            // Try to stringify objects
            try {
                auto JSON = runtime.global().getPropertyAsObject(runtime, "JSON");
                auto stringify = JSON.getPropertyAsFunction(runtime, "stringify");
                auto stringified = stringify.call(runtime, result);
                if (stringified.isString()) {
                    return stringified.asString(runtime).utf8(runtime);
                }
            } catch (...) {
                // Fall through
            }
            return "[object Object]";
        }

        return "[unknown]";

    } catch (const JSError& e) {
        throw std::runtime_error("JSError: " + e.getMessage());
    }
}

void WorkerRuntime::terminate() {
    if (!running_.exchange(false)) {
        return;
    }

    // Signal shutdown
    closeRequested_ = true;
    taskQueue_.shutdown();

    // Notify any waiting threads
    pendingScriptCondition_.notify_all();

    // Wait for worker thread to finish
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }

    // Clean up runtime
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        hermesRuntime_.reset();
    }
}

} // namespace webworker
