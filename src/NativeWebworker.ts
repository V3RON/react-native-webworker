import {
  TurboModuleRegistry,
  type TurboModule,
  NativeEventEmitter,
  Platform,
} from 'react-native';

export interface Spec extends TurboModule {
  /**
   * Create a worker from a script file path
   */
  createWorker(workerId: string, scriptPath: string): Promise<string>;

  /**
   * Create a worker from inline script content
   */
  createWorkerWithScript(
    workerId: string,
    scriptContent: string
  ): Promise<string>;

  /**
   * Terminate a worker
   */
  terminateWorker(workerId: string): Promise<boolean>;

  /**
   * Post a message to a worker
   */
  postMessage(workerId: string, message: string): Promise<boolean>;

  /**
   * Evaluate a script in a worker and return the result
   */
  evalScript(workerId: string, script: string): Promise<string>;

  /**
   * Add event listener (required for NativeEventEmitter on iOS)
   */
  addListener(eventType: string): void;

  /**
   * Remove event listeners (required for NativeEventEmitter on iOS)
   */
  removeListeners(count: number): void;
}

// Get the TurboModule - will throw if not available (New Architecture only)
const NativeWebworker = TurboModuleRegistry.getEnforcing<Spec>('Webworker');

// Event types emitted by the native module
export interface WorkerMessageEvent {
  workerId: string;
  message: string;
}

export interface WorkerConsoleEvent {
  workerId: string;
  level: string;
  message: string;
}

export interface WorkerErrorEvent {
  workerId: string;
  error: string;
}

// Create event emitter for listening to native events
// On iOS, we pass the module itself; on Android, we'll handle it differently
export const webworkerEventEmitter = new NativeEventEmitter(
  Platform.OS === 'ios' ? NativeWebworker : undefined
);

export default NativeWebworker;
