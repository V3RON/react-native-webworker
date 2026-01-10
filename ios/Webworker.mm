//
//  Webworker.mm
//  React Native WebWorker Module
//
//  Thin iOS wrapper around shared C++ WebWorkerCore
//

#import "Webworker.h"
#import "WebWorkerCore.h"
#import <memory>

@interface Webworker () {
  std::shared_ptr<webworker::WebWorkerCore> _core;
}
@end

@implementation Webworker

- (instancetype)init {
  if (self = [super init]) {
    _core = std::make_shared<webworker::WebWorkerCore>();
    [self setupCallbacks];
  }
  return self;
}

- (void)setupCallbacks {
  __weak Webworker *weakSelf = self;

  // Binary message callback - called when worker posts a message using
  // structured clone
  _core->setBinaryMessageCallback([weakSelf](const std::string &workerId,
                                             const std::vector<uint8_t> &data) {
    Webworker *strongSelf = weakSelf;
    if (strongSelf) {
      NSString *workerIdStr = [NSString stringWithUTF8String:workerId.c_str()];
      NSData *binaryData = [NSData dataWithBytes:data.data()
                                          length:data.size()];
      // Convert to base64 for transmission over the bridge
      NSString *base64Data = [binaryData base64EncodedStringWithOptions:0];
      dispatch_async(dispatch_get_main_queue(), ^{
        [strongSelf emitOnWorkerBinaryMessage:@{
          @"workerId" : workerIdStr,
          @"data" : base64Data
        }];
      });
    }
  });

  // Legacy message callback - for backwards compatibility if needed
  _core->setMessageCallback([weakSelf](const std::string &workerId,
                                       const std::string &message) {
    Webworker *strongSelf = weakSelf;
    if (strongSelf) {
      NSString *workerIdStr = [NSString stringWithUTF8String:workerId.c_str()];
      NSString *messageStr = [NSString stringWithUTF8String:message.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        [strongSelf emitOnWorkerMessage:@{
          @"workerId" : workerIdStr,
          @"message" : messageStr
        }];
      });
    }
  });

  // Console callback - called for worker console.log/error/etc
  _core->setConsoleCallback([weakSelf](const std::string &workerId,
                                       const std::string &level,
                                       const std::string &message) {
    Webworker *strongSelf = weakSelf;
    if (strongSelf) {
      NSLog(@"[WebWorker %s] [%s] %s", workerId.c_str(), level.c_str(),
            message.c_str());

      NSString *workerIdStr = [NSString stringWithUTF8String:workerId.c_str()];
      NSString *levelStr = [NSString stringWithUTF8String:level.c_str()];
      NSString *messageStr = [NSString stringWithUTF8String:message.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        [strongSelf emitOnWorkerConsole:@{
          @"workerId" : workerIdStr,
          @"level" : levelStr,
          @"message" : messageStr
        }];
      });
    }
  });

  // Error callback - called when worker encounters an error
  _core->setErrorCallback([weakSelf](const std::string &workerId,
                                     const std::string &error) {
    Webworker *strongSelf = weakSelf;
    if (strongSelf) {
      NSLog(@"[WebWorker %s] ERROR: %s", workerId.c_str(), error.c_str());

      NSString *workerIdStr = [NSString stringWithUTF8String:workerId.c_str()];
      NSString *errorStr = [NSString stringWithUTF8String:error.c_str()];
      dispatch_async(dispatch_get_main_queue(), ^{
        [strongSelf emitOnWorkerError:@{
          @"workerId" : workerIdStr,
          @"error" : errorStr
        }];
      });
    }
  });
}

+ (NSString *)moduleName {
  return @"Webworker";
}

+ (BOOL)requiresMainQueueSetup {
  return NO;
}

- (NSArray<NSString *> *)supportedEvents {
  return @[
    @"onWorkerMessage", @"onWorkerBinaryMessage", @"onWorkerConsole",
    @"onWorkerError"
  ];
}

- (void)invalidate {
  if (_core) {
    _core->terminateAll();
  }
}

// MARK: - TurboModule Methods

RCT_EXPORT_METHOD(createWorker : (NSString *)workerId scriptPath : (NSString *)
                      scriptPath resolve : (RCTPromiseResolveBlock)
                          resolve reject : (RCTPromiseRejectBlock)reject) {

  // For scriptPath, we need to load the file content
  NSError *error = nil;
  NSString *scriptContent =
      [NSString stringWithContentsOfFile:scriptPath
                                encoding:NSUTF8StringEncoding
                                   error:&error];

  if (error) {
    reject(@"FILE_ERROR",
           [NSString stringWithFormat:@"Failed to read script file: %@",
                                      error.localizedDescription],
           error);
    return;
  }

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @try {
          std::string resultId = self->_core->createWorker(
              [workerId UTF8String], [scriptContent UTF8String]);

          dispatch_async(dispatch_get_main_queue(), ^{
            resolve([NSString stringWithUTF8String:resultId.c_str()]);
          });
        } @catch (NSException *exception) {
          dispatch_async(dispatch_get_main_queue(), ^{
            reject(@"WORKER_ERROR", exception.reason, nil);
          });
        }
      });
}

RCT_EXPORT_METHOD(createWorkerWithScript : (NSString *)
                      workerId scriptContent : (NSString *)
                          scriptContent resolve : (RCTPromiseResolveBlock)
                              resolve reject : (RCTPromiseRejectBlock)reject) {

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        try {
          std::string resultId = self->_core->createWorker(
              [workerId UTF8String], [scriptContent UTF8String]);

          dispatch_async(dispatch_get_main_queue(), ^{
            resolve([NSString stringWithUTF8String:resultId.c_str()]);
          });
        } catch (const std::exception &e) {
          NSString *errorMsg = [NSString stringWithUTF8String:e.what()];
          dispatch_async(dispatch_get_main_queue(), ^{
            reject(@"WORKER_ERROR", errorMsg, nil);
          });
        }
      });
}

RCT_EXPORT_METHOD(terminateWorker : (NSString *)workerId resolve : (
    RCTPromiseResolveBlock)resolve reject : (RCTPromiseRejectBlock)reject) {

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        bool success = self->_core->terminateWorker([workerId UTF8String]);

        dispatch_async(dispatch_get_main_queue(), ^{
          resolve(@(success));
        });
      });
}

RCT_EXPORT_METHOD(postMessage : (NSString *)workerId message : (NSString *)
                      message resolve : (RCTPromiseResolveBlock)
                          resolve reject : (RCTPromiseRejectBlock)reject) {

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        try {
          bool success = self->_core->postMessage([workerId UTF8String],
                                                  [message UTF8String]);

          dispatch_async(dispatch_get_main_queue(), ^{
            resolve(@(success));
          });
        } catch (const std::exception &e) {
          NSString *errorMsg = [NSString stringWithUTF8String:e.what()];
          dispatch_async(dispatch_get_main_queue(), ^{
            reject(@"POST_MESSAGE_ERROR", errorMsg, nil);
          });
        }
      });
}

RCT_EXPORT_METHOD(postMessageBinary : (NSString *)workerId data : (NSString *)
                      base64Data resolve : (RCTPromiseResolveBlock)
                          resolve reject : (RCTPromiseRejectBlock)reject) {

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        try {
          // Decode base64 data
          NSData *binaryData =
              [[NSData alloc] initWithBase64EncodedString:base64Data options:0];
          if (binaryData == nil) {
            dispatch_async(dispatch_get_main_queue(), ^{
              reject(@"DECODE_ERROR", @"Failed to decode base64 data", nil);
            });
            return;
          }

          // Convert to std::vector<uint8_t>
          std::vector<uint8_t> dataVec(
              static_cast<const uint8_t *>(binaryData.bytes),
              static_cast<const uint8_t *>(binaryData.bytes) +
                  binaryData.length);

          bool success =
              self->_core->postMessageBinary([workerId UTF8String], dataVec);

          dispatch_async(dispatch_get_main_queue(), ^{
            resolve(@(success));
          });
        } catch (const std::exception &e) {
          NSString *errorMsg = [NSString stringWithUTF8String:e.what()];
          dispatch_async(dispatch_get_main_queue(), ^{
            reject(@"POST_MESSAGE_ERROR", errorMsg, nil);
          });
        }
      });
}

RCT_EXPORT_METHOD(evalScript : (NSString *)workerId script : (NSString *)
                      script resolve : (RCTPromiseResolveBlock)
                          resolve reject : (RCTPromiseRejectBlock)reject) {

  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        try {
          std::string result = self->_core->evalScript([workerId UTF8String],
                                                       [script UTF8String]);

          dispatch_async(dispatch_get_main_queue(), ^{
            resolve([NSString stringWithUTF8String:result.c_str()]);
          });
        } catch (const std::exception &e) {
          NSString *errorMsg = [NSString stringWithUTF8String:e.what()];
          dispatch_async(dispatch_get_main_queue(), ^{
            reject(@"EVAL_ERROR", errorMsg, nil);
          });
        }
      });
}

// MARK: - TurboModule Support

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::NativeWebworkerSpecJSI>(params);
}

@end
