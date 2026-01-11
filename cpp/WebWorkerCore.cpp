#include "WebWorkerCore.h"
#include "Polyfills.h"
#include "networking/ResponseHostObject.h"
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
    , errorCallback_(nullptr)
    , fetchCallback_(nullptr) {
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
        errorCallback_,
        fetchCallback_
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

void WebWorkerCore::setFetchCallback(FetchCallback callback) {
    fetchCallback_ = callback;
}

void WebWorkerCore::handleFetchResponse(const std::string& workerId, const FetchResponse& response) {
    std::lock_guard<std::mutex> lock(workersMutex_);
    auto it = workers_.find(workerId);
    if (it != workers_.end() && it->second->isRunning()) {
        it->second->handleFetchResponse(response);
    }
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
    ErrorCallback errorCallback,
    FetchCallback fetchCallback
)
    : workerId_(workerId)
    , messageCallback_(messageCallback)
    , consoleCallback_(consoleCallback)
    , errorCallback_(errorCallback)
    , fetchCallback_(fetchCallback) {

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

        // Drain microtasks after each macrotask
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

        // Execute polyfills first (TextEncoder, URL, AbortController, etc.)
        runtime.evaluateJavaScript(
            std::make_shared<StringBuffer>(kPolyfillScript),
            "polyfills.js"
        );

        std::string initScript = R"(
            var self = this;
            var global = this;
            var messageHandlers = [];

            self.onmessage = null;

            self.postMessage = function(message) {
                if (typeof __nativePostMessageToHost !== 'undefined') {
                    __nativePostMessageToHost(JSON.stringify(message));
                }
            };

            self.addEventListener = function(type, handler) {
                if (type === 'message' && typeof handler === 'function') {
                    messageHandlers.push(handler);
                }
            };

            self.removeEventListener = function(type, handler) {
                if (type === 'message') {
                    var index = messageHandlers.indexOf(handler);
                    if (index > -1) {
                        messageHandlers.splice(index, 1);
                    }
                }
            };

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

                if (typeof self.onmessage === 'function') {
                    self.onmessage(event);
                }

                messageHandlers.forEach(function(handler) {
                    handler(event);
                });
            };

            // Basic console
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

            self.queueMicrotask = function(callback) {
                Promise.resolve().then(callback);
            };

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

        // __nativeFetch
        auto fetchFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeFetch"),
            2,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count < 1) return Value::undefined();

                std::string url = args[0].asString(rt).utf8(rt);
                std::string method = "GET";
                std::unordered_map<std::string, std::string> headers;
                std::vector<uint8_t> bodyData;
                double timeout = 0;
                std::string redirect = "follow";

                if (count > 1 && args[1].isObject()) {
                    Object opts = args[1].asObject(rt);

                    if (opts.hasProperty(rt, "method")) {
                        method = opts.getProperty(rt, "method").asString(rt).utf8(rt);
                    }

                    if (opts.hasProperty(rt, "timeout")) {
                        timeout = opts.getProperty(rt, "timeout").asNumber();
                    }

                    if (opts.hasProperty(rt, "redirect")) {
                         redirect = opts.getProperty(rt, "redirect").asString(rt).utf8(rt);
                    }

                    if (opts.hasProperty(rt, "headers")) {
                        Object headersObj = opts.getProperty(rt, "headers").asObject(rt);
                        Array headerNames = headersObj.getPropertyNames(rt);
                        for (size_t i = 0; i < headerNames.size(rt); ++i) {
                            String key = headerNames.getValueAtIndex(rt, i).asString(rt);
                            String value = headersObj.getProperty(rt, key).asString(rt);
                            headers[key.utf8(rt)] = value.utf8(rt);
                        }
                    }

                    if (opts.hasProperty(rt, "body")) {
                        Value bodyVal = opts.getProperty(rt, "body");
                        if (bodyVal.isString()) {
                            std::string bodyStr = bodyVal.asString(rt).utf8(rt);
                            bodyData.assign(bodyStr.begin(), bodyStr.end());
                        } else if (bodyVal.isObject() && bodyVal.asObject(rt).isArrayBuffer(rt)) {
                            auto arrayBuffer = bodyVal.asObject(rt).getArrayBuffer(rt);
                            uint8_t* data = arrayBuffer.data(rt);
                            size_t size = arrayBuffer.size(rt);
                            bodyData.assign(data, data + size);
                        }
                    }
                }

                std::string requestId = std::to_string(self->nextRequestId_++);

                auto promiseCtor = rt.global().getPropertyAsFunction(rt, "Promise");
                return promiseCtor.callAsConstructor(rt, Function::createFromHostFunction(rt, PropNameID::forAscii(rt, "executor"), 2,
                    [self, requestId, url, method, headers, bodyData, timeout, redirect](Runtime& rt, const Value&, const Value* args, size_t) -> Value {
                        auto resolve = std::make_shared<Value>(rt, args[0]);
                        auto reject = std::make_shared<Value>(rt, args[1]);

                        self->pendingFetches_[requestId] = {resolve, reject};

                        if (self->fetchCallback_) {
                            FetchRequest req;
                            req.requestId = requestId;
                            req.url = url;
                            req.method = method;
                            req.headers = headers;
                            req.body = bodyData;
                            req.timeout = timeout;
                            req.redirect = redirect;
                            self->fetchCallback_(self->workerId_, req);
                        }
                        return Value::undefined();
                    }
                ));
            }
        );
        runtime.global().setProperty(runtime, "__nativeFetch", fetchFunc);

        // Fetch API Polyfill
        std::string fetchScript = R"(
            self.fetch = async function(url, options) {
                options = options || {};
                var nativeResponse = await __nativeFetch(url, options);
                
                return {
                    status: nativeResponse.status,
                    ok: nativeResponse.status >= 200 && nativeResponse.status < 300,
                    headers: nativeResponse.headers,
                    text: function() { return Promise.resolve(nativeResponse.text()); },
                    json: function() { 
                        return Promise.resolve(nativeResponse.text()).then(function(txt) {
                            return JSON.parse(txt);
                        });
                    },
                    arrayBuffer: function() { return Promise.resolve(nativeResponse.arrayBuffer()); }
                };
            };
        )";

        runtime.evaluateJavaScript(
            std::make_shared<StringBuffer>(fetchScript),
            "worker-fetch.js"
        );

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

        // __nativeScheduleTimer
        auto scheduleTimerFunc = Function::createFromHostFunction(
            runtime,
            PropNameID::forAscii(runtime, "__nativeScheduleTimer"),
            4,
            [self](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
                if (count < 4) return Value::undefined();

                uint64_t timerId = static_cast<uint64_t>(args[0].asNumber());
                int64_t delay = static_cast<int64_t>(args[1].asNumber());
                bool repeating = args[2].getBool();
                if (delay < 0) delay = 0;

                auto callback = std::make_shared<Value>(rt, args[3]);
                auto callbackHolder = std::make_shared<std::function<void()>>();

                *callbackHolder = [self, callback, timerId, delay, repeating, callbackHolder]() {
                    if (!self->hermesRuntime_ || !self->running_.load()) return;
                    {
                        std::lock_guard<std::mutex> lock(self->cancelledTimersMutex_);
                        if (self->cancelledTimers_.count(timerId) > 0) return;
                    }

                    Runtime& rt = *self->hermesRuntime_;
                    try {
                        if (callback->isObject() && callback->asObject(rt).isFunction(rt)) {
                            callback->asObject(rt).asFunction(rt).call(rt);
                        }
                    } catch (const JSError& e) {
                        if (self->errorCallback_) self->errorCallback_(self->workerId_, "JSError in timer: " + e.getMessage());
                    }

                    if (repeating) {
                        std::lock_guard<std::mutex> lock(self->cancelledTimersMutex_);
                        if (self->cancelledTimers_.count(timerId) == 0) {
                            Task nextTask;
                            nextTask.type = TaskType::Timer;
                            nextTask.id = timerId;
                            nextTask.execute = *callbackHolder;
                            self->taskQueue_.enqueueDelayed(std::move(nextTask), std::chrono::milliseconds(delay));
                        }
                    }
                };

                Task task;
                task.type = TaskType::Timer;
                task.id = timerId;
                task.execute = *callbackHolder;
                self->taskQueue_.enqueueDelayed(std::move(task), std::chrono::milliseconds(delay));
                return Value::undefined();
            }
        );
        runtime.global().setProperty(runtime, "__nativeScheduleTimer", scheduleTimerFunc);

        // __nativeCancelTimer
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

        // Timers JS wrapper
        std::string timerScript = R"(
            var __nextTimerId = 1;
            self.setTimeout = function(callback, delay) {
                if (typeof callback !== 'function') {
                    if (typeof callback === 'string') callback = new Function(callback);
                    else return 0;
                }
                var timerId = __nextTimerId++;
                var args = Array.prototype.slice.call(arguments, 2);
                var wrappedCallback = function() { callback.apply(null, args); };
                __nativeScheduleTimer(timerId, delay || 0, false, wrappedCallback);
                return timerId;
            };
            self.clearTimeout = function(timerId) { if(timerId) __nativeCancelTimer(timerId); };
            self.setInterval = function(callback, delay) {
                if (typeof callback !== 'function') {
                    if (typeof callback === 'string') callback = new Function(callback);
                    else return 0;
                }
                var timerId = __nextTimerId++;
                var args = Array.prototype.slice.call(arguments, 2);
                var wrappedCallback = function() { callback.apply(null, args); };
                __nativeScheduleTimer(timerId, delay || 0, true, wrappedCallback);
                return timerId;
            };
            self.clearInterval = function(timerId) { self.clearTimeout(timerId); };
            self.setImmediate = function(callback) {
                var args = Array.prototype.slice.call(arguments, 1);
                return self.setTimeout(function() { callback.apply(null, args); }, 0);
            };
            self.clearImmediate = function(timerId) { self.clearTimeout(timerId); };
        )";
        runtime.evaluateJavaScript(std::make_shared<StringBuffer>(timerScript), "worker-timers.js");

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

void WorkerRuntime::handleConsoleLog(const std::string& level, const std::string& message) {
    if (consoleCallback_) {
        consoleCallback_(workerId_, level, message);
    }
}

void WorkerRuntime::handleFetchResponse(const FetchResponse& response) {
    if (!running_.load()) return;

    Task task;
    task.type = TaskType::Message; // Reusing Message type for generic immediate execution
    task.id = nextTaskId_++;
    
    // Copy response data to be captured by lambda
    FetchResponse resp = response;

    task.execute = [this, resp]() {
        if (!hermesRuntime_) return;
        Runtime& rt = *hermesRuntime_;

        auto it = pendingFetches_.find(resp.requestId);
        if (it == pendingFetches_.end()) {
            return; // Request not found or already cancelled
        }

        auto resolve = it->second.resolve;
        auto reject = it->second.reject;

        try {
            if (!resp.error.empty()) {
                // Reject
                 if (reject->isObject() && reject->asObject(rt).isFunction(rt)) {
                    reject->asObject(rt).asFunction(rt).call(rt, String::createFromUtf8(rt, resp.error));
                }
            } else {
                // Resolve with HostObject
                auto hostObject = std::make_shared<ResponseHostObject>(
                    resp.status,
                    resp.headers,
                    resp.body
                );
                
                Object responseObj = Object::createFromHostObject(rt, hostObject);
                
                if (resolve->isObject() && resolve->asObject(rt).isFunction(rt)) {
                    resolve->asObject(rt).asFunction(rt).call(rt, responseObj);
                }
            }
        } catch (const JSError& e) {
            // Log error?
        }

        pendingFetches_.erase(it);
    };

    taskQueue_.enqueue(std::move(task));
}


bool WorkerRuntime::loadScript(const std::string& script) {
    if (!running_.load() && !initialized_.load()) {
        while (!initialized_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (!running_.load()) return false;

    {
        std::lock_guard<std::mutex> lock(pendingScriptMutex_);
        pendingScript_ = script;
        hasPendingScript_ = true;
        scriptExecuted_ = false;
    }
    pendingScriptCondition_.notify_all();

    {
        std::unique_lock<std::mutex> lock(pendingScriptMutex_);
        pendingScriptCondition_.wait(lock, [this] {
            return !hasPendingScript_ || !running_.load();
        });
    }

    return scriptExecuted_;
}

bool WorkerRuntime::postMessage(const std::string& message) {
    if (!running_.load()) return false;

    Task task;
    task.type = TaskType::Message;
    task.id = nextTaskId_++;
    task.execute = [this, message]() {
        if (!hermesRuntime_ || !running_.load()) return;
        Runtime& runtime = *hermesRuntime_;
        auto handleMessageProp = runtime.global().getProperty(runtime, "__handleMessage");
        if (handleMessageProp.isObject() && handleMessageProp.asObject(runtime).isFunction(runtime)) {
            handleMessageProp.asObject(runtime).asFunction(runtime).call(runtime, String::createFromUtf8(runtime, message));
        }
    };

    taskQueue_.enqueue(std::move(task));
    return true;
}

std::string WorkerRuntime::evalScript(const std::string& script) {
    if (!hermesRuntime_ || !running_.load()) throw std::runtime_error("Runtime not available");

    std::lock_guard<std::mutex> lock(runtimeMutex_);
    Runtime& runtime = *hermesRuntime_;

    try {
        Value result = runtime.evaluateJavaScript(std::make_shared<StringBuffer>(script), "eval.js");
        static_cast<facebook::hermes::HermesRuntime*>(hermesRuntime_.get())->drainMicrotasks();

        if (result.isString()) return result.asString(runtime).utf8(runtime);
        else if (result.isNumber()) {
            double num = result.asNumber();
            if (num == static_cast<int64_t>(num)) return std::to_string(static_cast<int64_t>(num));
            return std::to_string(num);
        } else if (result.isBool()) return result.getBool() ? "true" : "false";
        else if (result.isNull()) return "null";
        else if (result.isUndefined()) return "undefined";
        else if (result.isObject()) {
            try {
                auto JSON = runtime.global().getPropertyAsObject(runtime, "JSON");
                auto stringify = JSON.getPropertyAsFunction(runtime, "stringify");
                auto stringified = stringify.call(runtime, result);
                if (stringified.isString()) return stringified.asString(runtime).utf8(runtime);
            } catch (...) {}
            return "[object Object]";
        }
        return "[unknown]";
    } catch (const JSError& e) {
        throw std::runtime_error("JSError: " + e.getMessage());
    }
}

void WorkerRuntime::terminate() {
    if (!running_.exchange(false)) return;
    closeRequested_ = true;
    taskQueue_.shutdown();
    pendingScriptCondition_.notify_all();
    if (workerThread_ && workerThread_->joinable()) workerThread_->join();
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        hermesRuntime_.reset();
    }
}

} // namespace webworker