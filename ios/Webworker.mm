//
//  Webworker.mm
//  React Native WebWorker Module
//
//  Thin iOS wrapper around shared C++ WebWorkerCore
//

#import "Webworker.h"
#import "WebWorkerCore.h"
#import "networking/FetchTypes.h"
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

  // Message callback - called when worker posts a message to host
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

  // Fetch callback
  _core->setFetchCallback([weakSelf](const std::string &workerId,
                                     const webworker::FetchRequest &request) {
    Webworker *strongSelf = weakSelf;
    if (strongSelf) {
        [strongSelf performFetch:request workerId:workerId];
    }
  });
}

- (void)performFetch:(const webworker::FetchRequest &)request workerId:(std::string)workerId {
    NSString *urlString = [NSString stringWithUTF8String:request.url.c_str()];
    NSURL *url = [NSURL URLWithString:urlString];
    NSMutableURLRequest *urlRequest = [NSMutableURLRequest requestWithURL:url];
    
    urlRequest.HTTPMethod = [NSString stringWithUTF8String:request.method.c_str()];
    
    // Headers
    for (const auto &header : request.headers) {
        NSString *key = [NSString stringWithUTF8String:header.first.c_str()];
        NSString *value = [NSString stringWithUTF8String:header.second.c_str()];
        [urlRequest setValue:value forHTTPHeaderField:key];
    }
    
    // Body
    if (!request.body.empty()) {
        urlRequest.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];
    }
    
    std::string requestIdStr = request.requestId;
    std::string workerIdStr = workerId;
    
    NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithRequest:urlRequest completionHandler:^(NSData * _Nullable data, NSURLResponse * _Nullable response, NSError * _Nullable error) {
        
        webworker::FetchResponse fetchResponse;
        fetchResponse.requestId = requestIdStr;
        
        if (error) {
            fetchResponse.error = [error.localizedDescription UTF8String];
        } else {
            NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse *)response;
            fetchResponse.status = (int)httpResponse.statusCode;
            
            // Headers
            for (NSString *key in httpResponse.allHeaderFields) {
                NSString *value = httpResponse.allHeaderFields[key];
                fetchResponse.headers[[key UTF8String]] = [value UTF8String];
            }
            
            // Body
            if (data) {
                const uint8_t *bytes = (const uint8_t *)[data bytes];
                fetchResponse.body.assign(bytes, bytes + [data length]);
            }
        }
        
        if (self->_core) {
            self->_core->handleFetchResponse(workerIdStr, fetchResponse);
        }
    }];
    
    [task resume];
}

+ (NSString *)moduleName {
  return @"Webworker";
}

+ (BOOL)requiresMainQueueSetup {
  return NO;
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