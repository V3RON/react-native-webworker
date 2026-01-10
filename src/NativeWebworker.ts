import type { TurboModule, CodegenTypes } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

// Event payload types
export type WorkerBinaryMessageEvent = {
  workerId: string;
  /** Base64 encoded structured clone data */
  data: string;
};

export type WorkerConsoleEvent = {
  workerId: string;
  level: string;
  message: string;
};

export type WorkerErrorEvent = {
  workerId: string;
  error: string;
};

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
   * Post a message to a worker using structured clone algorithm
   * @param data Base64 encoded binary data from structured clone serialization
   */
  postMessageBinary(workerId: string, data: string): Promise<boolean>;

  /**
   * Evaluate a script in a worker and return the result
   */
  evalScript(workerId: string, script: string): Promise<string>;

  /**
   * Event emitted when a worker posts a message using structured clone
   */
  readonly onWorkerBinaryMessage: CodegenTypes.EventEmitter<WorkerBinaryMessageEvent>;

  /**
   * Event emitted when a worker logs to console
   */
  readonly onWorkerConsole: CodegenTypes.EventEmitter<WorkerConsoleEvent>;

  /**
   * Event emitted when a worker encounters an error
   */
  readonly onWorkerError: CodegenTypes.EventEmitter<WorkerErrorEvent>;
}

// Get the TurboModule - will throw if not available (New Architecture only)
const NativeWebworker = TurboModuleRegistry.getEnforcing<Spec>('Webworker');

export default NativeWebworker;
