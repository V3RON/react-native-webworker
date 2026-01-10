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

describe('EventLoop', () => {
  let worker: Worker;

  afterEach(async () => {
    await worker.terminate();
  });

  it('should execute setTimeout callback', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function() {
          setTimeout(function() {
            self.postMessage('timeout executed');
          }, 50);
        };
      `,
    });

    const responsePromise = new Promise<string>((resolve) => {
      worker.onmessage = (event) => resolve(event.data as string);
    });

    await worker.postMessage('start');

    const response = await withTimeout(
      responsePromise,
      1000,
      'Worker did not respond with timeout message'
    );

    expect(response).toBe('timeout executed');
  });

  it('should execute setInterval multiple times and then clear it', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function() {
          let count = 0;
          const id = setInterval(function() {
            count++;
            self.postMessage({ type: 'tick', count: count });
            if (count >= 3) {
              clearInterval(id);
              self.postMessage({ type: 'done' });
            }
          }, 50);
        };
      `,
    });

    const ticks: number[] = [];

    const intervalPromise = new Promise<void>((resolve) => {
      worker.onmessage = (event) => {
        const data = event.data as { type: string; count?: number };
        if (data.type === 'tick') {
          ticks.push(data.count!);
        } else if (data.type === 'done') {
          resolve();
        }
      };
    });

    await worker.postMessage('start');

    await withTimeout(
      intervalPromise,
      2000,
      'Worker did not complete interval sequence'
    );

    // Wait a bit longer to ensure no more ticks come in (proving clearInterval worked)
    await new Promise((resolve) => setTimeout(resolve, 150));

    expect(ticks).toEqual([1, 2, 3]);
  });

  it('should support clearTimeout to cancel execution', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function() {
          const id = setTimeout(function() {
            self.postMessage('should not happen');
          }, 50);

          clearTimeout(id);

          // Send a safe message after a delay to prove the other one didn't happen
          setTimeout(function() {
            self.postMessage('safe');
          }, 100);
        };
      `,
    });

    const messages: string[] = [];

    const cancelPromise = new Promise<void>((resolve) => {
      worker.onmessage = (event) => {
        messages.push(event.data as string);
        // If we get 'safe', we are done.
        // If we get 'should not happen', we also resolve so we can fail the assertion.
        if (event.data === 'safe' || event.data === 'should not happen') {
          resolve();
        }
      };
    });

    await worker.postMessage('start');

    await withTimeout(cancelPromise, 1000, 'Worker did not respond');

    expect(messages).toContain('safe');
    expect(messages).not.toContain('should not happen');
  });

  it('should prioritize microtasks (Promises) over macrotasks (setTimeout)', async () => {
    worker = new Worker({
      script: `
        self.onmessage = function() {
          const logs = [];

          setTimeout(function() {
            logs.push('macro');
            if (logs.length === 2) {
                 self.postMessage(logs);
            }
          }, 0);

          Promise.resolve().then(function() {
            logs.push('micro');
             if (logs.length === 2) {
                 self.postMessage(logs);
            }
          });
        };
      `,
    });

    const logsPromise = new Promise<string[]>((resolve) => {
      worker.onmessage = (event) => resolve(event.data as string[]);
    });

    await worker.postMessage('start');

    const logs = await withTimeout(
      logsPromise,
      1000,
      'Worker did not return execution order logs'
    );

    expect(logs).toEqual(['micro', 'macro']);
  });

  it('should execute setImmediate', async () => {
    worker = new Worker({
      script: `
          self.onmessage = function() {
            setImmediate(function() {
              self.postMessage('immediate executed');
            });
          };
        `,
    });

    const responsePromise = new Promise<string>((resolve) => {
      worker.onmessage = (event) => resolve(event.data as string);
    });

    await worker.postMessage('start');

    const response = await withTimeout(
      responsePromise,
      1000,
      'Worker did not execute setImmediate'
    );

    expect(response).toBe('immediate executed');
  });
});
