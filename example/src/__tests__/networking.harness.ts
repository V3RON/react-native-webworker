import { describe, it, expect, afterEach } from 'react-native-harness';
import { Worker } from 'react-native-webworker';

// Helper to prevent tests from hanging indefinitely
function withTimeout<T>(
  promise: Promise<T>,
  ms: number = 5000,
  msg: string = 'Operation timed out'
): Promise<T> {
  return Promise.race([
    promise,
    new Promise<T>((_, reject) => setTimeout(() => reject(new Error(msg)), ms)),
  ]);
}

describe('Worker Networking', () => {
  let worker: Worker<any, any>;

  afterEach(async () => {
    if (worker) {
      await worker.terminate();
    }
  });

  it('should support basic fetch GET', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function() {
          try {
            const response = await fetch('https://jsonplaceholder.typicode.com/todos/1');
            const data = await response.json();
            self.postMessage({ status: 'ok', data: data });
          } catch (e) {
            self.postMessage({ status: 'error', error: e.toString() });
          }
        };
      `,
    });

    const responsePromise = new Promise<any>((resolve, reject) => {
      worker.onmessage = (event) => {
        if (event.data.status === 'error') {
          reject(new Error(event.data.error));
        } else {
          resolve(event.data.data);
        }
      };
      worker.onerror = reject;
    });

    await worker.postMessage('start');

    const data = await withTimeout(
      responsePromise,
      5000,
      'Fetch GET timed out'
    );

    expect(data).not.toBe(null);
    expect(data.id).toBe(1);
  });

  it('should support fetch POST with JSON body', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function() {
          try {
            const response = await fetch('https://jsonplaceholder.typicode.com/posts', {
              method: 'POST',
              headers: {
                'Content-Type': 'application/json'
              },
              body: JSON.stringify({
                title: 'foo',
                body: 'bar',
                userId: 1
              })
            });
            const data = await response.json();
            self.postMessage({ status: 'ok', data: data });
          } catch (e) {
            self.postMessage({ status: 'error', error: e.toString() });
          }
        };
      `,
    });

    const responsePromise = new Promise<any>((resolve, reject) => {
      worker.onmessage = (event) => {
        if (event.data.status === 'error') {
          reject(new Error(event.data.error));
        } else {
          resolve(event.data.data);
        }
      };
      worker.onerror = reject;
    });

    await worker.postMessage('start');

    const data = await withTimeout(
      responsePromise,
      5000,
      'Fetch POST timed out'
    );

    expect(data).not.toBe(null);
    expect(data.title).toBe('foo');
    expect(data.body).toBe('bar');
    expect(data.userId).toBe(1);
  });

  it('should read response headers', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function() {
          try {
            const response = await fetch('https://jsonplaceholder.typicode.com/todos/1');
            // Check headers object (it's a plain object in our implementation)
            self.postMessage({ status: 'ok', headers: response.headers });
          } catch (e) {
            self.postMessage({ status: 'error', error: e.toString() });
          }
        };
      `,
    });

    const responsePromise = new Promise<any>((resolve, reject) => {
      worker.onmessage = (event) => {
        if (event.data.status === 'error') {
          reject(new Error(event.data.error));
        } else {
          resolve(event.data.headers);
        }
      };
      worker.onerror = reject;
    });

    await worker.postMessage('start');

    const headers = await withTimeout(
      responsePromise,
      5000,
      'Headers test timed out'
    );

    expect(headers).not.toBe(null);
    // Keys might be lower-cased or not depending on platform implementation
    const contentType = headers['content-type'] || headers['Content-Type'];
    expect(contentType).toBeDefined();
    expect(contentType.includes('application/json')).toBe(true);
  });

  it('should handle fetch errors', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function() {
          try {
            // Invalid domain
            await fetch('https://this-domain-does-not-exist-xyz123.com');
            self.postMessage({ status: 'ok', data: 'should not happen' });
          } catch (e) {
            self.postMessage({ status: 'error', error: e.toString() });
          }
        };
      `,
    });

    const responsePromise = new Promise<any>((resolve) => {
      worker.onmessage = (event) => {
        if (event.data.status === 'error') {
          resolve(event.data.error);
        } else {
          resolve('Did not catch error');
        }
      };
      worker.onerror = (err) => resolve(err);
    });

    await worker.postMessage('start');

    const result = await withTimeout(
      responsePromise,
      5000,
      'Fetch error test timed out'
    );
    // We expect an error string
    expect(typeof result).toBe('string');
    expect(result).not.toBe('Did not catch error');
  });
});
