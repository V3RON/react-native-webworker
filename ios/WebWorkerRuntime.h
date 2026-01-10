//
//  WebWorkerRuntime.h
//  React Native WebWorker Module
//
//  C++ implementation managing Hermes runtime in worker thread
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface WebWorkerRuntime : NSObject

/// Unique identifier for this worker
@property(nonatomic, readonly) NSString *workerId;

/// Whether the worker runtime is currently running
@property(nonatomic, readonly) BOOL isRunning;

/// Initialize a new worker runtime with the given worker ID
/// @param workerId Unique identifier for this worker
- (instancetype)initWithWorkerId:(NSString *)workerId;

/// Load and execute a JavaScript file from the given path
/// @param scriptPath Path to the JavaScript file
/// @return YES if the script was loaded successfully
- (BOOL)loadScriptFromPath:(NSString *)scriptPath;

/// Load and execute JavaScript code from a string
/// @param scriptContent The JavaScript code to execute
/// @param sourceURL URL to use for error reporting
/// @return YES if the script was executed successfully
- (BOOL)loadScriptFromString:(NSString *)scriptContent
                   sourceURL:(NSString *)sourceURL;

/// Post a message to the worker
/// @param message JSON-encoded message string
- (void)postMessage:(NSString *)message;

/// Evaluate JavaScript code and return the result
/// @param script JavaScript code to evaluate
/// @return String representation of the result
- (nullable NSString *)evaluateScript:(NSString *)script;

/// Terminate the worker and release resources
- (void)terminate;

@end

NS_ASSUME_NONNULL_END

