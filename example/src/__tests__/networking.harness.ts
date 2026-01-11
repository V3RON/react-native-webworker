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

  it('should support binary data with ArrayBuffer', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function(event) {
          try {
            const { action, data } = event.data;
            if (action === 'send-binary') {
              // Create an ArrayBuffer with some test data
              const buffer = new ArrayBuffer(8);
              const view = new Uint8Array(buffer);
              view.set([1, 2, 3, 4, 5, 6, 7, 8]);

              // Send as POST body
              const response = await fetch('https://httpbin.org/post', {
                method: 'POST',
                headers: {
                  'Content-Type': 'application/octet-stream'
                },
                body: buffer
              });

              const result = await response.json();
              self.postMessage({ status: 'ok', result: result });
            } else if (action === 'receive-binary') {
              // Fetch binary data
              const response = await fetch('https://httpbin.org/bytes/8');
              const arrayBuffer = await response.arrayBuffer();

              // Convert to Uint8Array to check contents
              const uint8Array = new Uint8Array(arrayBuffer);
              self.postMessage({ status: 'ok', data: Array.from(uint8Array) });
            }
          } catch (e) {
            self.postMessage({ status: 'error', error: e.toString() });
          }
        };
      `,
    });

    const createResponsePromise = () => new Promise<any>((resolve, reject) => {
      worker.onmessage = (event) => {
        if (event.data.status === 'error') {
          reject(new Error(event.data.error));
        } else {
          resolve(event.data);
        }
      };
      worker.onerror = reject;
    });

    // Test sending binary data
    let currentPromise = createResponsePromise();
    await worker.postMessage({ action: 'send-binary', data: null });
    let result = await withTimeout(
      currentPromise,
      10000,
      'Binary send test timed out'
    );

    expect(result.result).toBeDefined();
    expect(result.result.data).toBeDefined();
    // httpbin.org returns the data as base64, so we just check it exists
    expect(typeof result.result.data).toBe('string');

    // Test receiving binary data
    currentPromise = createResponsePromise();
    await worker.postMessage({ action: 'receive-binary', data: null });
    result = await withTimeout(
      currentPromise,
      10000,
      'Binary receive test timed out'
    );

    expect(Array.isArray(result.data)).toBe(true);
    expect(result.data.length).toBe(8);
    // Should be random bytes, just check they're numbers
    result.data.forEach((byte: number) => {
      expect(typeof byte).toBe('number');
      expect(byte).toBeGreaterThanOrEqual(0);
      expect(byte).toBeLessThanOrEqual(255);
    });
  });

  it('should support request timeout', async () => {
    worker = new Worker({
      script: `
        self.onmessage = async function() {
          try {
            // Request a 2 second delay but set timeout to 100ms
            await fetch('https://httpbin.org/delay/2', {
              timeout: 100
            });
            self.postMessage({ status: 'ok', data: 'should fail' });
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

    const result = await withTimeout(responsePromise, 5000, 'Timeout test failed');
    // Expect error string
    expect(typeof result).toBe('string');
    expect(result).not.toBe('Did not catch error');
  });
});
