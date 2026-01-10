//
//  Webworker.h
//  React Native WebWorker Module
//
//  TurboModule for WebWorker support - iOS thin wrapper (New Architecture only)
//

#ifdef __cplusplus
#import <memory>
#import "WebWorkerCore.h"
#endif

#import <WebworkerSpec/WebworkerSpec.h>

NS_ASSUME_NONNULL_BEGIN

@interface Webworker : NativeWebworkerSpecBase <NativeWebworkerSpec>

@end

NS_ASSUME_NONNULL_END
