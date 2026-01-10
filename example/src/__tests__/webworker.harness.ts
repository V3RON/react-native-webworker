import { describe, it, expect, afterEach } from 'react-native-harness';
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

describe('WebWorker', () => {
  let worker: Worker;

  afterEach(async () => {
    await worker.terminate();
  });

  it('should create a worker and receive a message back', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function(event) {
          self.postMessage('echo: ' + event.data);
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

    await worker.postMessage('hello');

    const response = await withTimeout(
      responsePromise,
      2000,
      'Worker did not echo message'
    );

    expect(response).toBe('echo: hello');
  });

  it('should handle complex objects', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function(event) {
          const data = event.data;
          self.postMessage({
            received: data,
            modified: true
          });
        };
      `,
    });

    const payload = { foo: 'bar', num: 123 };
    const responsePromise = new Promise<any>((resolve, reject) => {
      worker.onmessage = (event) => {
        resolve(event.data);
      };
      worker.onerror = (err) => {
        reject(err);
      };
    });

    await worker.postMessage(payload);

    const response = await withTimeout(
      responsePromise,
      1000,
      'Worker did not handle complex object'
    );

    expect(response.received.foo).toBe(payload.foo);
    expect(response.received.num).toBe(payload.num);
    expect(response.modified).toBe(true);
  });

  it('should handle errors from worker', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function() {
          throw new Error('Test Error');
        };
      `,
    });

    const errorPromise = new Promise<string>((resolve) => {
      worker.onerror = (err) => {
        resolve(err.message);
      };
    });

    await worker.postMessage('trigger error');

    const errorMsg = await withTimeout(
      errorPromise,
      1000,
      'Worker did not report error'
    );

    // The error message format might depend on implementation, but should contain the error text
    expect(errorMsg.includes('Test Error')).toBe(true);
  });

  it('should throw when posting to terminated worker', async () => {
    worker = new Worker({
      script: 'self.onmessage = function() {}',
    });

    await worker.terminate();

    let error;
    try {
      await worker.postMessage('test');
    } catch (e) {
      error = e;
    }

    expect(error).not.toBe(undefined);
    expect((error as Error).message).toBe('Worker has been terminated');
  });
});
