//
//  WebWorkerRuntime.mm
//  React Native WebWorker Module
//
//  C++ implementation managing Hermes runtime in worker thread
//

#import "WebWorkerRuntime.h"
#import <hermes/hermes.h>
#import <jsi/jsi.h>
#import <memory>
#import <string>

using namespace facebook::jsi;
using namespace facebook::hermes;

@interface WebWorkerRuntime () {
  std::unique_ptr<Runtime> _hermesRuntime;
  dispatch_queue_t _workerQueue;
}

@property(nonatomic, strong) NSString *workerId;
@property(nonatomic, assign) BOOL isRunning;

@end

@implementation WebWorkerRuntime

- (instancetype)initWithWorkerId:(NSString *)workerId {
  if (self = [super init]) {
    _workerId = workerId;
    _isRunning = NO;

    // Create dedicated serial queue for this worker
    NSString *queueName =
        [NSString stringWithFormat:@"com.webworker.worker.%@", workerId];
    _workerQueue =
        dispatch_queue_create([queueName UTF8String], DISPATCH_QUEUE_SERIAL);

    // Initialize Hermes runtime on worker queue
    dispatch_sync(_workerQueue, ^{
      [self initializeRuntime];
    });
  }
  return self;
}

- (void)initializeRuntime {
  try {
    // Create Hermes runtime with default configuration
    _hermesRuntime = makeHermesRuntime();

    if (_hermesRuntime) {
      // Setup worker global scope
      [self setupWorkerGlobalScope];
      _isRunning = YES;
      NSLog(@"[WebWorker] Initialized runtime for worker: %@", _workerId);
    } else {
      NSLog(@"[WebWorker] Failed to create Hermes runtime for worker: %@",
            _workerId);
    }
  } catch (const std::exception &e) {
    NSLog(@"[WebWorker] Exception creating runtime: %s", e.what());
  }
}

- (void)setupWorkerGlobalScope {
  if (!_hermesRuntime)
    return;

  try {
    Runtime &runtime = *_hermesRuntime;

    // Initialize worker global scope with standard APIs
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
                        __nativeConsoleLog(message);
                    }
                },
                error: function() {
                    var args = Array.prototype.slice.call(arguments);
                    var message = args.map(function(arg) {
                        return typeof arg === 'object' ? JSON.stringify(arg) : String(arg);
                    }).join(' ');
                    if (typeof __nativeConsoleError !== 'undefined') {
                        __nativeConsoleError(message);
                    }
                },
                warn: function() {
                    this.log.apply(this, arguments);
                },
                info: function() {
                    this.log.apply(this, arguments);
                }
            };
            
            self.console = console;
        )";

    runtime.evaluateJavaScript(std::make_shared<StringBuffer>(initScript),
                               "worker-init.js");

    // Install native functions
    [self installNativeFunctions];

  } catch (const std::exception &e) {
    NSLog(@"[WebWorker] Exception setting up global scope: %s", e.what());
  }
}

- (void)installNativeFunctions {
  if (!_hermesRuntime)
    return;

  try {
    Runtime &runtime = *_hermesRuntime;

    // Install __nativePostMessageToHost
    NSString *workerId = _workerId;
    auto postMessageFunc = Function::createFromHostFunction(
        runtime, PropNameID::forAscii(runtime, "__nativePostMessageToHost"), 1,
        [workerId](Runtime &rt, const Value &thisVal, const Value *args,
                   size_t count) -> Value {
          if (count > 0 && args[0].isString()) {
            std::string message = args[0].asString(rt).utf8(rt);
            NSString *msg = [NSString stringWithUTF8String:message.c_str()];
            NSLog(@"[WebWorker %@] Message to host: %@", workerId, msg);
            // TODO: Implement callback to JS/React Native side
          }
          return Value::undefined();
        });

    runtime.global().setProperty(runtime, "__nativePostMessageToHost",
                                 postMessageFunc);

    // Install __nativeConsoleLog
    auto consoleLogFunc = Function::createFromHostFunction(
        runtime, PropNameID::forAscii(runtime, "__nativeConsoleLog"), 1,
        [workerId](Runtime &rt, const Value &thisVal, const Value *args,
                   size_t count) -> Value {
          if (count > 0) {
            std::string message = args[0].toString(rt).utf8(rt);
            NSLog(@"[WebWorker %@] %s", workerId, message.c_str());
          }
          return Value::undefined();
        });

    runtime.global().setProperty(runtime, "__nativeConsoleLog", consoleLogFunc);

    // Install __nativeConsoleError
    auto consoleErrorFunc = Function::createFromHostFunction(
        runtime, PropNameID::forAscii(runtime, "__nativeConsoleError"), 1,
        [workerId](Runtime &rt, const Value &thisVal, const Value *args,
                   size_t count) -> Value {
          if (count > 0) {
            std::string message = args[0].toString(rt).utf8(rt);
            NSLog(@"[WebWorker %@] ERROR: %s", workerId, message.c_str());
          }
          return Value::undefined();
        });

    runtime.global().setProperty(runtime, "__nativeConsoleError",
                                 consoleErrorFunc);

  } catch (const std::exception &e) {
    NSLog(@"[WebWorker] Exception installing native functions: %s", e.what());
  }
}

- (BOOL)loadScriptFromPath:(NSString *)scriptPath {
  __block BOOL success = NO;

  dispatch_sync(_workerQueue, ^{
    if (!_hermesRuntime || !_isRunning) {
      NSLog(@"[WebWorker] Runtime not ready");
      return;
    }

    NSError *error = nil;
    NSString *scriptContent =
        [NSString stringWithContentsOfFile:scriptPath
                                  encoding:NSUTF8StringEncoding
                                     error:&error];

    if (error) {
      NSLog(@"[WebWorker] Failed to read script: %@", error);
      return;
    }

    success = [self executeScript:scriptContent sourceURL:scriptPath];
  });

  return success;
}

- (BOOL)loadScriptFromString:(NSString *)scriptContent
                   sourceURL:(NSString *)sourceURL {
  __block BOOL success = NO;

  dispatch_sync(_workerQueue, ^{
    success = [self executeScript:scriptContent sourceURL:sourceURL];
  });

  return success;
}

- (BOOL)executeScript:(NSString *)scriptContent
            sourceURL:(NSString *)sourceURL {
  if (!_hermesRuntime || !_isRunning) {
    return NO;
  }

  try {
    Runtime &runtime = *_hermesRuntime;
    std::string script = [scriptContent UTF8String];
    std::string url = [sourceURL UTF8String];

    runtime.evaluateJavaScript(std::make_shared<StringBuffer>(script), url);

    NSLog(@"[WebWorker %@] Loaded script: %@", _workerId, sourceURL);
    return YES;

  } catch (const JSError &e) {
    std::string errorMsg = e.getMessage();
    NSLog(@"[WebWorker %@] JS Error: %s", _workerId, errorMsg.c_str());
    return NO;
  } catch (const std::exception &e) {
    NSLog(@"[WebWorker %@] Exception executing script: %s", _workerId,
          e.what());
    return NO;
  }
}

- (void)postMessage:(NSString *)message {
  dispatch_async(_workerQueue, ^{
    if (!_hermesRuntime || !_isRunning) {
      NSLog(@"[WebWorker] Runtime not ready for postMessage");
      return;
    }

    try {
      Runtime &runtime = *_hermesRuntime;

      // Get the __handleMessage function
      auto handleMessageProp =
          runtime.global().getProperty(runtime, "__handleMessage");

      if (handleMessageProp.isObject() &&
          handleMessageProp.asObject(runtime).isFunction(runtime)) {
        auto handleMessage =
            handleMessageProp.asObject(runtime).asFunction(runtime);

        // Call with the message
        std::string msg = [message UTF8String];
        handleMessage.call(runtime, String::createFromUtf8(runtime, msg));

        NSLog(@"[WebWorker %@] Posted message: %@", _workerId, message);
      } else {
        NSLog(@"[WebWorker %@] __handleMessage is not a function", _workerId);
      }

    } catch (const std::exception &e) {
      NSLog(@"[WebWorker %@] Exception posting message: %s", _workerId,
            e.what());
    }
  });
}

- (NSString *)evaluateScript:(NSString *)script {
  __block NSString *result = nil;

  dispatch_sync(_workerQueue, ^{
    if (!_hermesRuntime || !_isRunning) {
      NSLog(@"[WebWorker] Runtime not ready for eval");
      return;
    }

    try {
      Runtime &runtime = *_hermesRuntime;
      std::string scriptStr = [script UTF8String];

      Value evalResult = runtime.evaluateJavaScript(
          std::make_shared<StringBuffer>(scriptStr), "eval.js");

      // Convert result to string
      std::string resultStr;

      if (evalResult.isString()) {
        resultStr = evalResult.asString(runtime).utf8(runtime);
      } else if (evalResult.isNumber()) {
        resultStr = std::to_string(evalResult.asNumber());
      } else if (evalResult.isBool()) {
        resultStr = evalResult.getBool() ? "true" : "false";
      } else if (evalResult.isNull()) {
        resultStr = "null";
      } else if (evalResult.isUndefined()) {
        resultStr = "undefined";
      } else if (evalResult.isObject()) {
        // Try to stringify objects
        try {
          auto JSON = runtime.global().getPropertyAsObject(runtime, "JSON");
          auto stringify = JSON.getPropertyAsFunction(runtime, "stringify");
          auto stringified = stringify.call(runtime, evalResult);
          resultStr = stringified.asString(runtime).utf8(runtime);
        } catch (...) {
          resultStr = "[object]";
        }
      } else {
        resultStr = "[unknown]";
      }

      result = [NSString stringWithUTF8String:resultStr.c_str()];

    } catch (const JSError &e) {
      std::string errorMsg = "JSError: " + e.getMessage();
      result = [NSString stringWithUTF8String:errorMsg.c_str()];
      NSLog(@"[WebWorker %@] JS Error: %s", _workerId, errorMsg.c_str());
    } catch (const std::exception &e) {
      std::string errorMsg = "Exception: " + std::string(e.what());
      result = [NSString stringWithUTF8String:errorMsg.c_str()];
      NSLog(@"[WebWorker %@] Exception: %s", _workerId, e.what());
    }
  });

  return result;
}

- (void)terminate {
  dispatch_sync(_workerQueue, ^{
    if (_hermesRuntime && _isRunning) {
      try {
        // Clear the runtime
        _hermesRuntime.reset();
        _isRunning = NO;
        NSLog(@"[WebWorker %@] Terminated", _workerId);
      } catch (const std::exception &e) {
        NSLog(@"[WebWorker %@] Exception during termination: %s", _workerId,
              e.what());
      }
    }
  });
}

- (void)dealloc {
  [self terminate];
}

@end
