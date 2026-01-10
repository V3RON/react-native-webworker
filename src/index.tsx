import NativeWebworker, {
  type WorkerBinaryMessageEvent,
  type WorkerErrorEvent,
} from './NativeWebworker';
import type { EventSubscription } from 'react-native';
import { decodeStructuredClone } from './StructuredCloneDecoder';
import { encodeStructuredClone } from './StructuredCloneEncoder';

/**
 * Decode a base64 string to a Uint8Array
 */
function base64ToBytes(base64: string): Uint8Array {
  const binaryString = atob(base64);
  const bytes = new Uint8Array(binaryString.length);
  for (let i = 0; i < binaryString.length; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }
  return bytes;
}

/**
 * Encode a Uint8Array to a base64 string
 */
function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]!);
  }
  return btoa(binary);
}

// Re-export DataCloneError
export { DataCloneError } from './StructuredCloneEncoder';

// Types
export interface WorkerOptions {
  /** Inline script content for the worker */
  script?: string;
  /** Path to the script file */
  scriptPath?: string;
  /** Optional name for the worker */
  name?: string;
}

export interface MessageEvent<T = unknown> {
  data: T;
  type: 'message';
}

export type MessageHandler<T = unknown> = (event: MessageEvent<T>) => void;

/**
 * High-level Worker class that wraps the native WebWorker module.
 * Provides a Web Worker-like API for React Native.
 */
export class Worker<TIn = unknown, TOut = unknown> {
  private workerId: string;
  private isTerminated: boolean = false;
  private messageHandlers: Set<MessageHandler<TOut>> = new Set();
  private _onmessage: MessageHandler<TOut> | null = null;
  private _onerror: ((error: Error) => void) | null = null;
  private initPromise: Promise<void>;
  private binaryMessageSubscription: EventSubscription | null = null;
  private errorSubscription: EventSubscription | null = null;

  constructor(options: WorkerOptions) {
    const { script, scriptPath, name } = options;
    this.workerId =
      name || `worker-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;

    // Setup event listeners for this worker
    this.setupEventListeners();

    if (script) {
      this.initPromise = NativeWebworker.createWorkerWithScript(
        this.workerId,
        script
      ).then(() => {});
    } else if (scriptPath) {
      this.initPromise = NativeWebworker.createWorker(
        this.workerId,
        scriptPath
      ).then(() => {});
    } else {
      throw new Error('Either script or scriptPath must be provided');
    }
  }

  private setupEventListeners(): void {
    // Listen for binary messages from this worker using CodegenTypes.EventEmitter
    this.binaryMessageSubscription = NativeWebworker.onWorkerBinaryMessage(
      (event: WorkerBinaryMessageEvent) => {
        if (event.workerId === this.workerId && !this.isTerminated) {
          try {
            // Decode base64 to bytes
            const bytes = base64ToBytes(event.data);
            // Decode structured clone format
            const data = decodeStructuredClone(bytes) as TOut;
            this.dispatchMessage(data);
          } catch (error) {
            if (this._onerror) {
              this._onerror(error as Error);
            }
          }
        }
      }
    );

    // Listen for errors from this worker using CodegenTypes.EventEmitter
    this.errorSubscription = NativeWebworker.onWorkerError(
      (event: WorkerErrorEvent) => {
        if (event.workerId === this.workerId && !this.isTerminated) {
          if (this._onerror) {
            this._onerror(new Error(event.error));
          }
        }
      }
    );
  }

  private cleanupEventListeners(): void {
    if (this.binaryMessageSubscription) {
      this.binaryMessageSubscription.remove();
      this.binaryMessageSubscription = null;
    }
    if (this.errorSubscription) {
      this.errorSubscription.remove();
      this.errorSubscription = null;
    }
  }

  /**
   * Set the message handler
   */
  set onmessage(handler: MessageHandler<TOut> | null) {
    this._onmessage = handler;
  }

  get onmessage(): MessageHandler<TOut> | null {
    return this._onmessage;
  }

  /**
   * Set the error handler
   */
  set onerror(handler: ((error: Error) => void) | null) {
    this._onerror = handler;
  }

  get onerror(): ((error: Error) => void) | null {
    return this._onerror;
  }

  /**
   * Add a message event listener
   */
  addEventListener(type: 'message', handler: MessageHandler<TOut>): void {
    if (type === 'message') {
      this.messageHandlers.add(handler);
    }
  }

  /**
   * Remove a message event listener
   */
  removeEventListener(type: 'message', handler: MessageHandler<TOut>): void {
    if (type === 'message') {
      this.messageHandlers.delete(handler);
    }
  }

  /**
   * Post a message to the worker using structured clone algorithm.
   * Supports complex types like Date, Map, Set, ArrayBuffer, etc.
   *
   * @throws DataCloneError if the message contains non-cloneable types (Function, Symbol, etc.)
   */
  async postMessage(message: TIn): Promise<void> {
    if (this.isTerminated) {
      throw new Error('Worker has been terminated');
    }

    await this.initPromise;

    // Encode using structured clone algorithm
    const bytes = encodeStructuredClone(message);
    const base64Data = bytesToBase64(bytes);
    await NativeWebworker.postMessageBinary(this.workerId, base64Data);
  }

  /**
   * Evaluate a script in the worker
   */
  async eval(script: string): Promise<string> {
    if (this.isTerminated) {
      throw new Error('Worker has been terminated');
    }

    await this.initPromise;

    return await NativeWebworker.evalScript(this.workerId, script);
  }

  /**
   * Terminate the worker
   */
  async terminate(): Promise<void> {
    if (this.isTerminated) {
      return;
    }

    this.isTerminated = true;
    this.cleanupEventListeners();

    await this.initPromise;
    await NativeWebworker.terminateWorker(this.workerId);
    this.messageHandlers.clear();
  }

  /**
   * Get the worker ID
   */
  getId(): string {
    return this.workerId;
  }

  /**
   * Dispatch a message event to all handlers
   * (Called internally when receiving messages from the worker)
   */
  private dispatchMessage(data: TOut): void {
    const event: MessageEvent<TOut> = {
      data,
      type: 'message',
    };

    if (this._onmessage) {
      try {
        this._onmessage(event);
      } catch (error) {
        if (this._onerror) {
          this._onerror(error as Error);
        }
      }
    }

    this.messageHandlers.forEach((handler) => {
      try {
        handler(event);
      } catch (error) {
        if (this._onerror) {
          this._onerror(error as Error);
        }
      }
    });
  }
}

/**
 * Options for creating a WorkerPool
 */
export interface WorkerPoolOptions {
  /** Number of workers in the pool */
  size: number;
  /** Inline script content for all workers */
  script?: string;
  /** Path to the script file */
  scriptPath?: string;
  /** Optional name prefix for workers */
  name?: string;
}

/**
 * WorkerPool for parallel processing using multiple workers.
 * Distributes tasks across workers and aggregates results.
 */
export class WorkerPool<TIn = unknown, TOut = unknown> {
  private workers: Worker<TIn, TOut>[] = [];
  private size: number;
  private isTerminated: boolean = false;

  constructor(options: WorkerPoolOptions) {
    const { size, script, scriptPath, name } = options;
    this.size = size;

    for (let i = 0; i < size; i++) {
      const workerName = name ? `${name}-${i}` : undefined;
      this.workers.push(
        new Worker<TIn, TOut>({
          script,
          scriptPath,
          name: workerName,
        })
      );
    }
  }

  /**
   * Run tasks in parallel across all workers.
   * Each task is sent to a worker, and results are collected.
   */
  async run(tasks: TIn[]): Promise<TOut[]> {
    if (this.isTerminated) {
      throw new Error('WorkerPool has been terminated');
    }

    const results: TOut[] = new Array(tasks.length);
    const taskQueue = tasks.map((task, index) => ({ task, index }));
    const availableWorkers = [...this.workers];

    const processTask = async (
      worker: Worker<TIn, TOut>,
      task: TIn,
      index: number
    ): Promise<void> => {
      return new Promise((resolve, reject) => {
        const handler = (event: MessageEvent<TOut>) => {
          results[index] = event.data;
          worker.removeEventListener('message', handler);
          availableWorkers.push(worker);
          processNextTask();
          resolve();
        };

        worker.addEventListener('message', handler);
        worker.postMessage(task).catch(reject);
      });
    };

    let currentTaskIndex = 0;
    const runningTasks: Promise<void>[] = [];

    const processNextTask = () => {
      if (currentTaskIndex >= taskQueue.length) return;

      const worker = availableWorkers.pop();
      if (!worker) return;

      const { task, index } = taskQueue[currentTaskIndex]!;
      currentTaskIndex++;

      const taskPromise = processTask(worker, task, index);
      runningTasks.push(taskPromise);
    };

    // Start initial batch
    const initialBatchSize = Math.min(this.size, taskQueue.length);
    for (let i = 0; i < initialBatchSize; i++) {
      processNextTask();
    }

    await Promise.all(runningTasks);

    return results;
  }

  /**
   * Execute a function on all workers
   */
  async broadcast(message: TIn): Promise<void> {
    if (this.isTerminated) {
      throw new Error('WorkerPool has been terminated');
    }

    await Promise.all(
      this.workers.map((worker) => worker.postMessage(message))
    );
  }

  /**
   * Terminate all workers in the pool
   */
  async terminate(): Promise<void> {
    if (this.isTerminated) {
      return;
    }

    this.isTerminated = true;
    await Promise.all(this.workers.map((worker) => worker.terminate()));
    this.workers = [];
  }

  /**
   * Get the number of workers in the pool
   */
  getSize(): number {
    return this.size;
  }
}

// Export native module for direct access if needed
export { NativeWebworker };

// Re-export event types
export type {
  WorkerBinaryMessageEvent,
  WorkerErrorEvent,
  WorkerConsoleEvent,
} from './NativeWebworker';

// Export convenience functions
export async function createWorker(
  workerId: string,
  scriptPath: string
): Promise<string> {
  return NativeWebworker.createWorker(workerId, scriptPath);
}

export async function createWorkerWithScript(
  workerId: string,
  scriptContent: string
): Promise<string> {
  return NativeWebworker.createWorkerWithScript(workerId, scriptContent);
}

export async function terminateWorker(workerId: string): Promise<boolean> {
  return NativeWebworker.terminateWorker(workerId);
}

export async function evalScript(
  workerId: string,
  script: string
): Promise<string> {
  return NativeWebworker.evalScript(workerId, script);
}
