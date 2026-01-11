import { afterEach, describe, it, expect } from 'react-native-harness';
import { Worker } from 'react-native-webworker';

// Helper to prevent tests from hanging indefinitely
function withTimeout<T>(
  promise: Promise<T>,
  ms: number = 2000,
  msg: string = 'Operation timed out'
): Promise<T> {
  return Promise.race([
    promise,
    new Promise<T>((_, reject) => setTimeout(() => reject(new Error(msg)), ms)),
  ]);
}

describe('Polyfills', () => {
  let worker: Worker;

  afterEach(() => {
    worker.terminate();
  });

  it('should add AbortController to the global object', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function(event) {
          self.postMessage(typeof AbortController);
        };
      `,
      name: 'test-worker',
    });

    const responsePromise = new Promise<string>((resolve, reject) => {
      worker.onmessage = (event) => {
        resolve(event.data as string);
      };
      worker.onerror = (err) => {
        reject(err);
      };
    });

    await worker.postMessage('start');

    const result = await withTimeout(
      responsePromise,
      2000,
      'Worker did not respond with AbortController type'
    );

    expect(result).toBe('function');
  });
});
