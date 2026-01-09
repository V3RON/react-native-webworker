#include "WebWorkerCore.h"
#include <iostream>
#include <sstream>
#include <chrono>

namespace webworker {

// ============================================================================
// WebWorkerCore Implementation
// ============================================================================

WebWorkerCore::WebWorkerCore()
    : messageCallback_(nullptr)
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
    ConsoleCallback consoleCallback,
    ErrorCallback errorCallback
)
    : workerId_(workerId)
    , messageCallback_(messageCallback)
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

        // Main message loop
        while (running_.load()) {
            processMessageQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Worker thread exception: " + std::string(e.what()));
        }
        initialized_ = true;
    }
}

void WorkerRuntime::processMessageQueue() {
    std::string message;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (messageQueue_.empty()) {
            return;
        }
        message = std::move(messageQueue_.front());
        messageQueue_.pop();
    }

    if (!hermesRuntime_ || !running_.load()) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        Runtime& runtime = *hermesRuntime_;

        auto handleMessageProp = runtime.global().getProperty(runtime, "__handleMessage");

        if (handleMessageProp.isObject() && handleMessageProp.asObject(runtime).isFunction(runtime)) {
            auto handleMessage = handleMessageProp.asObject(runtime).asFunction(runtime);
            handleMessage.call(runtime, String::createFromUtf8(runtime, message));
        }

    } catch (const JSError& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "JSError in message handler: " + e.getMessage());
        }
    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception in message handler: " + std::string(e.what()));
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

            // postMessage - sends messages to the host
            self.postMessage = function(message) {
                if (typeof __nativePostMessageToHost !== 'undefined') {
                    __nativePostMessageToHost(JSON.stringify(message));
                }
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

            // Internal message handler (called from native)
            self.__handleMessage = function(message) {
                var data;
                try {
                    data = JSON.parse(message);
                } catch (e) {
                    data = message;
                }

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

            // setTimeout/clearTimeout basic implementation
            var __timers = {};
            var __timerIdCounter = 1;

            self.setTimeout = function(callback, delay) {
                var timerId = __timerIdCounter++;
                __timers[timerId] = {
                    callback: callback,
                    delay: delay,
                    startTime: Date.now(),
                    cancelled: false
                };
                return timerId;
            };

            self.clearTimeout = function(timerId) {
                if (__timers[timerId]) {
                    __timers[timerId].cancelled = true;
                    delete __timers[timerId];
                }
            };

            // setInterval/clearInterval basic implementation
            self.setInterval = function(callback, delay) {
                var timerId = __timerIdCounter++;
                __timers[timerId] = {
                    callback: callback,
                    delay: delay,
                    interval: true,
                    lastRun: Date.now(),
                    cancelled: false
                };
                return timerId;
            };

            self.clearInterval = function(timerId) {
                self.clearTimeout(timerId);
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

        // __nativePostMessageToHost
        auto postMessageFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativePostMessageToHost"),
            1,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count > 0 && args[0].isString()) {
                    std::string message = args[0].asString(rt).utf8(rt);
                    self->handlePostMessageToHost(message);
                }
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativePostMessageToHost", postMessageFunc);

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

    } catch (const std::exception& e) {
        if (errorCallback_) {
            errorCallback_(workerId_, "Exception installing native functions: " + std::string(e.what()));
        }
    }
}

void WorkerRuntime::handlePostMessageToHost(const std::string& message) {
    if (messageCallback_) {
        messageCallback_(workerId_, message);
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

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        messageQueue_.push(message);
    }
    queueCondition_.notify_one();

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

    // Notify any waiting threads
    pendingScriptCondition_.notify_all();
    queueCondition_.notify_all();

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
